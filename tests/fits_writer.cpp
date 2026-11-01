// ============================================================
//  A FITS cube, through the real CaptureManager
//
//  Phase 106.3. Frames go in the way the capture thread sends them,
//  and the file on disk is parsed back here: every pixel, every
//  timestamp, and the header's claims. The parser is local and
//  knows nothing of FITSFormat.h.
//
//  What cannot be gated: whether Siril opens the cube and debayers
//  it correctly. That is a real file in a real program, and it is
//  owed in the status row.
// ============================================================

#include "core/CaptureManager.h"
#include "core/FrameSlot.h"
#include "core/ICamera.h"

#include <chrono>
#include <share.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const std::string& what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) g_fail++;
}

// ── The local reader ─────────────────────────────────────────

static std::vector<uint8_t> ReadAll(const std::string& p) {
    std::vector<uint8_t> out;
    // Shared, because section 6 reads a file the writer still holds
    // open. fopen_s would refuse it.
    FILE* f = _fsopen(p.c_str(), "rb", _SH_DENYNO);
    if (!f) return out;
    _fseeki64(f, 0, SEEK_END);
    out.resize((size_t)_ftelli64(f));
    _fseeki64(f, 0, SEEK_SET);
    if (!out.empty()) fread(out.data(), 1, out.size(), f);
    fclose(f);
    return out;
}

struct Hdu {
    size_t      headerStart = 0;
    size_t      dataStart   = 0;      // first block after END
    std::string text;
    bool        ended = false;
};

static Hdu ReadHeader(const std::vector<uint8_t>& b, size_t start) {
    Hdu h;
    h.headerStart = start;
    for (size_t off = start; off + 2880 <= b.size() && !h.ended; off += 2880) {
        for (size_t c = 0; c < 36; ++c) {
            const std::string card((const char*)b.data() + off + c * 80, 80);
            if (card.substr(0, 8) == "END     ") {
                h.ended = true;
                h.dataStart = off + 2880;
                break;
            }
            h.text += card;
        }
    }
    return h;
}

static std::string Card(const Hdu& h, const std::string& key) {
    std::string k = key;
    k.resize(8, ' ');
    for (size_t i = 0; i + 80 <= h.text.size(); i += 80)
        if (h.text.compare(i, 8, k) == 0 && h.text.compare(i + 8, 2, "= ") == 0)
            return h.text.substr(i, 80);
    return "";
}

static long long Int(const Hdu& h, const std::string& key) {
    const std::string c = Card(h, key);
    return c.empty() ? -1 : atoll(c.c_str() + 10);
}

static std::string Str(const Hdu& h, const std::string& key) {
    const std::string c = Card(h, key);
    if (c.size() < 11 || c[10] != '\'') return "";
    const size_t e = c.find('\'', 11);
    std::string v = c.substr(11, e - 11);
    while (!v.empty() && v.back() == ' ') v.pop_back();
    return v;
}

static double BigDouble(const uint8_t* p) {
    uint8_t r[8];
    for (int i = 0; i < 8; ++i) r[i] = p[7 - i];
    double d;
    memcpy(&d, r, 8);
    return d;
}

static float BigFloat(const uint8_t* p) {
    uint8_t r[4];
    for (int i = 0; i < 4; ++i) r[i] = p[3 - i];
    float f;
    memcpy(&f, r, 4);
    return f;
}

// ── Running a recording ──────────────────────────────────────

static const int W = 32, H = 16, N = 40;

static std::string TempDir() {
    const char* t = getenv("TEMP");
    return std::string(t ? t : ".") + "\\raduk_fits_writer_test";
}

static uint16_t Sample(int frame, int i) {
    return (uint16_t)((frame * 977 + i * 131) * 64);   // left-aligned
}

struct Run {
    std::string path;
    std::vector<uint64_t> stamps;
    uint32_t recorded = 0;
};

static Run Record(const std::string& dir, const char* prefix, int bits,
                  CaptureFormat format, const char* colour, int frames) {
    CaptureManager cm;
    CaptureConfig cfg;
    cfg.outputDir   = dir;
    cfg.filePrefix  = prefix;
    cfg.limitType   = LimitType::Frames;
    cfg.limitFrames = 100000;
    cfg.format      = format;
    cm.SetConfig(cfg);
    cm.SetObservationSource([&](ObservationRecord& r) {
        r.target     = "Jupiter";
        r.exposureMs = 8.0;
        r.gain       = 250.0;
        r.colour     = colour;
        r.pixelUm    = 2.0;
        r.appVersion = "9.9.9";
        r.siteLatDeg = 47.60621234;
    });

    Run run;
    if (!cm.StartRecording(W, H, bits)) return run;
    cm.NoteSky(45.0, 180.0, 1.414, 1.2, 0.5);

    const size_t bytes = (size_t)W * H * bits / 8;
    std::vector<uint8_t> px(bytes);
    uint64_t t0 = Timestamp::Now() + 100000;
    for (int f = 0; f < frames; ++f) {
        for (int i = 0; i < W * H; ++i) {
            const uint16_t s = Sample(f, i);
            if (bits == 16) {
                px[i * 2]     = (uint8_t)(s & 0xFF);
                px[i * 2 + 1] = (uint8_t)(s >> 8);
            } else {
                px[i] = (uint8_t)(s >> 8);
            }
        }
        FrameSlot slot{};
        slot.data        = px.data();
        slot.dataSize    = bytes;
        slot.width       = W;
        slot.height      = H;
        slot.bitDepth    = bits;
        slot.timestamp   = t0 + f * 10000ULL;
        run.stamps.push_back(slot.timestamp);
        cm.SubmitFromCaptureThread(&slot);
        while (cm.FramesRecorded() < (uint32_t)f + 1 &&
               cm.FramesDropped() == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    run.path = cm.CurrentFile();
    cm.StopRecording();
    run.recorded = cm.FramesRecorded();
    return run;
}

// ── Checks on a finished cube ────────────────────────────────

static void CheckCube(const Run& run, int bits, int frames) {
    const std::vector<uint8_t> b = ReadAll(run.path);
    const size_t frameBytes = (size_t)W * H * bits / 8;

    Check(!b.empty() && b.size() % 2880 == 0, "the file is whole blocks");
    const Hdu h = ReadHeader(b, 0);
    Check(h.ended && h.dataStart == 3 * 2880, "the data starts after the reserve");
    Check(Int(h, "BITPIX") == bits, "BITPIX");
    Check(Int(h, "NAXIS1") == W && Int(h, "NAXIS2") == H, "width and height");
    Check(Int(h, "NAXIS3") == frames, "NAXIS3 was patched at close");
    Check(Str(h, "OBJECT") == "Jupiter", "the record reached the header");
    Check(Str(h, "SWCREATE") == "RadukCapture 9.9.9", "the version reached it");
    Check(Card(h, "OBJCTALT").empty(), "no mount, no altitude");
    Check(!Card(h, "AIRMASS").empty(), "the sky summary reached it");
    Check(Card(h, "OBSERVER").empty(), "no OBSERVER");

    bool pixels = b.size() >= h.dataStart + frameBytes * frames;
    for (int f = 0; pixels && f < frames; ++f) {
        const uint8_t* p = b.data() + h.dataStart + frameBytes * f;
        for (int i = 0; i < W * H; ++i) {
            const uint16_t s = Sample(f, i);
            const int got = (bits == 16)
                ? (int)(int16_t)((p[i * 2] << 8) | p[i * 2 + 1]) + 32768
                : p[i];
            const int want = (bits == 16) ? s : (s >> 8);
            if (got != want) { pixels = false; break; }
        }
    }
    Check(pixels, "every pixel of every frame, in order");

    const size_t imageEnd = h.dataStart + frameBytes * frames;
    const size_t tableAt  = imageEnd + (2880 - imageEnd % 2880) % 2880;
    bool zero = tableAt <= b.size();
    for (size_t i = imageEnd; zero && i < tableAt; ++i) if (b[i]) zero = false;
    Check(zero, "the image pads to a block with zeros");

    if (tableAt >= b.size()) { Check(false, "the table exists"); return; }
    const Hdu t = ReadHeader(b, tableAt);
    Check(t.ended && Str(t, "XTENSION") == "BINTABLE", "the table follows the image");
    Check(Str(t, "EXTNAME") == "FRAMES", "by the name the header promises");
    Check(Int(t, "NAXIS2") == frames && Int(t, "NAXIS1") == 12, "one row per frame");

    bool spacing = t.dataStart + 12 * (size_t)frames <= b.size() && frames > 0;
    if (spacing) {
        const double first = BigDouble(b.data() + t.dataStart);
        for (int f = 0; f < frames; ++f) {
            const uint8_t* row = b.data() + t.dataStart + 12 * f;
            const double dtUs = (BigDouble(row) - first) * 86400e6;
            const double want = (double)(run.stamps[f] - run.stamps[0]);
            if (fabs(dtUs - want) > 2.0) spacing = false;
            const float sh = BigFloat(row + 8);
            if (!(sh >= 0.0f && sh <= 100.0f)) spacing = false;
        }
        Check(first > 50000.0 && first < 80000.0, "MJD_MID is a plausible date");
    }
    Check(spacing, "the table keeps each frame's capture spacing");
    Check(Str(h, "DATE-OBS") < Str(h, "DATE-AVG") &&
          Str(h, "DATE-AVG") < Str(h, "DATE-END"),
          "start, middle and end, in order");

    Check(GetFileAttributesA(ObservationPathFor(run.path).c_str()) !=
              INVALID_FILE_ATTRIBUTES, "the observation record is beside it");
    Check(GetFileAttributesA((CaptureBasePath(run.path) + "_quality.txt").c_str()) !=
              INVALID_FILE_ATTRIBUTES, "the quality sidecar is beside it");
}

int main() {
    printf("\n=== FITS through CaptureManager ===\n");

    const std::string dir = TempDir();
    CreateDirectoryA(dir.c_str(), nullptr);

    printf("\n1. A RAW16 cube\n");
    {
        const Run r = Record(dir, "fw16", 16, CaptureFormat::FITS, "RGGB", N);
        Check(r.path.size() > 5 && r.path.substr(r.path.size() - 5) == ".fits",
              "the name says .fits");
        Check(r.recorded == (uint32_t)N, "every frame was recorded");
        CheckCube(r, 16, N);
        const Hdu h = ReadHeader(ReadAll(r.path), 0);
        Check(Str(h, "BAYERPAT") == "RGGB", "BAYERPAT");
        Check(Int(h, "BZERO") == 32768, "BZERO");
        Check(!Card(h, "XPIXSZ").empty(), "the pixel size reached it");
    }

    printf("\n2. A RAW8 cube\n");
    {
        const Run r = Record(dir, "fw8", 8, CaptureFormat::FITS, "RGGB", N);
        CheckCube(r, 8, N);
        Check(Card(ReadHeader(ReadAll(r.path), 0), "BZERO").empty(),
              "8-bit has no BZERO");
    }

    printf("\n3. One frame, and none\n");
    {
        CheckCube(Record(dir, "fw1", 16, CaptureFormat::FITS, "RGGB", 1), 16, 1);
        const Run r = Record(dir, "fw0", 8, CaptureFormat::FITS, "RGGB", 0);
        const std::vector<uint8_t> b = ReadAll(r.path);
        const Hdu h = ReadHeader(b, 0);
        Check(Int(h, "NAXIS3") == 0, "a run with no frames says so");
        const Hdu t = ReadHeader(b, h.dataStart);
        Check(t.ended && Int(t, "NAXIS2") == 0 && b.size() % 2880 == 0,
              "and is still a valid file, with an empty table");
    }

    printf("\n4. The colour is the camera's\n");
    {
        const Run f = Record(dir, "fwm", 8, CaptureFormat::FITS, "MONO", 2);
        Check(Card(ReadHeader(ReadAll(f.path), 0), "BAYERPAT").empty(),
              "a mono camera claims no pattern in FITS");
        const Run s = Record(dir, "fwms", 8, CaptureFormat::SER, "MONO", 2);
        const std::vector<uint8_t> b = ReadAll(s.path);
        int32_t colorId = -1;
        if (b.size() > 22) memcpy(&colorId, b.data() + 18, 4);
        Check(colorId == 0, "and SER's ColorID says MONO, which it never did");
        Check(std::string(BayerName(BayerPattern::GB)) == "GBRG" &&
              std::string(BayerName(BayerPattern::None)) == "MONO",
              "the names, FITS's spelling");
    }

    printf("\n5. SER stays the default\n");
    {
        const Run s = Record(dir, "fws", 8, CaptureConfig{}.format, "RGGB", 2);
        Check(s.path.size() > 4 && s.path.substr(s.path.size() - 4) == ".ser",
              "the name says .ser");
        const std::vector<uint8_t> b = ReadAll(s.path);
        Check(b.size() > 14 && memcmp(b.data(), "LUCAM-RECORDER", 14) == 0,
              "and it is a SER");
    }

    printf("\n6. A run that never closes\n");
    {
        const std::string p = dir + "\\abandoned.fits";
        {
            auto w = std::make_unique<FITSWriter>();
            Check(w->Open(p, W, H, 8, true), "opens");
            std::vector<uint8_t> px((size_t)W * H, 7);
            Check(w->WriteFrame(px.data(), px.size(), Timestamp::Now(), 1.0f),
                  "a frame");
            Check(!w->WriteFrame(px.data(), px.size() - 1, Timestamp::Now(), 1.0f),
                  "a frame of the wrong size is refused");

            // As a crash would leave it: flushed, and read from outside.
            // _flushall reaches the writer's private FILE*.
            _flushall();
            const std::vector<uint8_t> b = ReadAll(p);
            const Hdu h = ReadHeader(b, 0);
            Check(h.ended && Int(h, "NAXIS3") == 0 && Int(h, "NAXIS1") == W,
                  "the header already identifies the file");
            Check(b.size() == 3 * 2880 + (size_t)W * H,
                  "and the frame is there to be found");
        }
        const Hdu h = ReadHeader(ReadAll(p), 0);
        Check(Int(h, "NAXIS3") == 1, "destruction closes it properly");
    }
    {
        FITSWriter w;
        Check(!w.Open(dir + "\\twelve.fits", W, H, 12, true),
              "a depth FITS is not given here is refused at open");
    }

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\"", dir.c_str());
    system(cmd);

    if (g_fail == 0) printf("\nPASS\n"); else printf("\nFAIL: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
