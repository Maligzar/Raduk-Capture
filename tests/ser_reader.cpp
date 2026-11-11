// ============================================================
//  2026-11-10: reading a SER back, for the PLAY window
//
//  core/SerReader.h and core/SerScan.h. Checked:
//    1. a file from the app's own SERWriter reads back pixel for pixel,
//       with its timestamps and frame rate
//    2. an unclosed recording (count 0, no trailer, a partial last
//       frame) serves every whole frame and no timestamps
//    3. 16-bit byte order comes from the data, not the flag: big-endian
//       samples are read right whichever flag the writer set, and the
//       app's own little-endian files are not swapped
//    4. a 12-bit file is scaled to 16 bits
//    5. junk is refused with a reason rather than read
//    6. each Bayer pattern puts red and blue where they belong
//    7. frame offsets past 4 GB (64-bit seeks), on a sparse file
//    8. the quality scan finds the disc on frames that have one and
//       scores the empty frames 0
// ============================================================

#include "core/SERWriter.h"
#include "core/SerReader.h"
#include "core/SerScan.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-60s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static std::string Temp(const char* tag) {
    char dir[MAX_PATH];
    GetTempPathA(MAX_PATH, dir);
    char name[96];
    snprintf(name, sizeof(name), "raduk_ser_reader_%lu_%s.ser", GetCurrentProcessId(), tag);
    return std::string(dir) + name;
}

// A header by hand, so tests can set what SERWriter never writes.
static std::vector<uint8_t> Header(int color, int le, int w, int h, int depth, int count) {
    std::vector<uint8_t> b(178, 0);
    memcpy(b.data(), "LUCAM-RECORDER", 14);
    auto put = [&](size_t off, int32_t v) { memcpy(b.data() + off, &v, 4); };
    put(18, color); put(22, le); put(26, w); put(30, h); put(34, depth); put(38, count);
    return b;
}

static void WriteFile(const std::string& p, const std::vector<uint8_t>& bytes) {
    FILE* f = fopen(p.c_str(), "wb");
    fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
}

// A smooth 16-bit test pattern (a ramp plus a gentle wave), the kind of
// image whose high byte changes slowly. value(x, y, frame).
static uint16_t Smooth(int x, int y, int k) {
    return (uint16_t)(8000 + x * 37 + y * 23 + k * 101 + (int)(900.0 * std::sin(x * 0.05)));
}

int main() {
    printf("SER reader\n");

    // ── 1. The app's own file ──────────────────────────────────
    {
        const std::string p = Temp("own");
        const int W = 64, H = 48, N = 5;
        SERWriter w;
        Check(w.Open(p, W, H, 16, true), "SERWriter opens a 16-bit RGGB file");
        const uint64_t t0 = Timestamp::Now();
        std::vector<std::vector<uint16_t>> frames(N, std::vector<uint16_t>(W * H));
        for (int k = 0; k < N; ++k) {
            for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x)
                frames[k][y * W + x] = Smooth(x, y, k);
            w.WriteFrame((const uint8_t*)frames[k].data(), W * H * 2, t0 + (uint64_t)k * 25000);
        }
        w.Close();

        ser::Reader r;
        Check(r.Open(p), "the reader opens it", r.GetInfo().error);
        const ser::Info& in = r.GetInfo();
        Check(in.width == W && in.height == H && in.depth == 16 && in.colorId == ser::BAYER_RGGB,
              "size, depth and pattern from the header");
        Check(in.frames == N && !in.recovered, "the header's count is used");
        Check(!in.swap16, "the app's little-endian data is not swapped");
        Check(in.hasTimestamps, "the trailer is found");
        bool same = true;
        std::vector<uint8_t> raw;
        for (int k = 0; k < N; ++k)
            same = same && r.ReadFrame(k, raw) && memcmp(raw.data(), frames[k].data(), raw.size()) == 0;
        Check(same, "every frame reads back byte for byte");
        Check(r.Timestamp(1) - r.Timestamp(0) == 250000, "timestamps 25 ms apart");
        Check(std::fabs(r.FrameRate() - 40.0) < 0.01, "40 fps from the trailer",
              std::to_string(r.FrameRate()));
        Check(!r.ReadFrame(N, raw) && !r.ReadFrame(-1, raw), "out-of-range frames are refused");
        r.Close();
        remove(p.c_str());
        remove((p.substr(0, p.size() - 4) + "_quality.txt").c_str());
    }

    // ── 2. An unclosed recording ───────────────────────────────
    {
        const std::string p = Temp("unclosed");
        const int W = 16, H = 8;
        auto b = Header(ser::MONO, 1, W, H, 8, 0);
        for (int k = 0; k < 7; ++k) for (int i = 0; i < W * H; ++i) b.push_back((uint8_t)(k * 10 + i % 7));
        for (int i = 0; i < 40; ++i) b.push_back(0xEE);   // a partial eighth frame
        WriteFile(p, b);
        ser::Reader r;
        Check(r.Open(p), "an unclosed recording opens", r.GetInfo().error);
        Check(r.GetInfo().frames == 7 && r.GetInfo().recovered, "7 whole frames recovered from the size",
              std::to_string(r.GetInfo().frames));
        Check(!r.GetInfo().hasTimestamps && r.Timestamp(0) == 0 && r.FrameRate() == 0.0,
              "and no timestamps claimed");
        std::vector<uint8_t> raw;
        Check(r.ReadFrame(6, raw) && raw[0] == 60, "the last whole frame is the right one");
        r.Close();
        remove(p.c_str());

        // A count larger than the file holds: the file wins.
        auto c = Header(ser::MONO, 1, W, H, 8, 100);
        for (int i = 0; i < W * H * 3; ++i) c.push_back(1);
        WriteFile(p, c);
        Check(r.Open(p) && r.GetInfo().frames == 3 && r.GetInfo().recovered,
              "a count past the end of the file is not believed");
        r.Close();
        remove(p.c_str());
    }

    // ── 3. Byte order from the data ────────────────────────────
    for (int flag = 0; flag <= 1; ++flag) {
        const std::string p = Temp(flag ? "be_flag1" : "be_flag0");
        const int W = 64, H = 32;
        auto b = Header(ser::MONO, flag, W, H, 16, 3);
        for (int k = 0; k < 3; ++k)
            for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
                const uint16_t v = Smooth(x, y, k);
                b.push_back((uint8_t)(v >> 8)); b.push_back((uint8_t)(v & 0xFF));   // big-endian
            }
        WriteFile(p, b);
        ser::Reader r;
        r.Open(p);
        Check(r.GetInfo().swap16, std::string("big-endian samples are swapped, flag ") + (flag ? "1" : "0"));
        std::vector<uint8_t> raw; std::vector<uint16_t> plane;
        r.ReadFrame(1, raw);
        ser::ToPlane16(raw.data(), r.GetInfo(), plane);
        Check(plane[5 * W + 9] == Smooth(9, 5, 1), "and read as the values written");
        r.Close();
        remove(p.c_str());
    }
    {
        // Little-endian under flag 0, as several capture programs write.
        const std::string p = Temp("le_flag0");
        const int W = 64, H = 32;
        auto b = Header(ser::MONO, 0, W, H, 16, 2);
        for (int k = 0; k < 2; ++k)
            for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
                const uint16_t v = Smooth(x, y, k);
                b.push_back((uint8_t)(v & 0xFF)); b.push_back((uint8_t)(v >> 8));
            }
        WriteFile(p, b);
        ser::Reader r;
        r.Open(p);
        Check(!r.GetInfo().swap16, "little-endian data under flag 0 is not swapped");
        r.Close();
        remove(p.c_str());
    }

    // ── 4. 12-bit samples ──────────────────────────────────────
    {
        ser::Info in = ser::ParseHeader(Header(ser::MONO, 1, 2, 1, 12, 1).data(), 178 + 4);
        Check(in.ok && in.BytesPerSample() == 2 && in.frameBytes == 4, "12-bit is two bytes a sample");
        const uint8_t raw[4] = { 0xFF, 0x0F, 0x00, 0x08 };   // 4095, 2048
        std::vector<uint16_t> plane;
        ser::ToPlane16(raw, in, plane);
        Check(plane[0] == 0xFFF0 && plane[1] == 0x8000, "and scaled up to 16 bits");
    }

    // ── 5. Junk ────────────────────────────────────────────────
    {
        auto h = Header(ser::MONO, 1, 16, 16, 8, 1);
        memcpy(h.data(), "NOT-A-SER-FILE", 14);
        Check(!ser::ParseHeader(h.data(), 178 + 256).ok, "a wrong file id is refused");
        Check(!ser::ParseHeader(Header(7, 1, 16, 16, 8, 1).data(), 178 + 256).ok, "an unknown colour id is refused");
        Check(!ser::ParseHeader(Header(ser::MONO, 1, 0, 16, 8, 1).data(), 178 + 256).ok, "a zero width is refused");
        Check(!ser::ParseHeader(Header(ser::MONO, 1, 16, 16, 24, 1).data(), 178 + 256).ok, "24-bit is refused");
        const ser::Info e = ser::ParseHeader(Header(ser::MONO, 1, 16, 16, 8, 1).data(), 178 + 100);
        Check(!e.ok && !e.error.empty(), "no whole frame is refused, with a reason", e.error);
        ser::Reader r;
        Check(!r.Open(Temp("missing")) && !r.GetInfo().error.empty(), "a missing file says why");
    }

    // ── 6. Bayer patterns ──────────────────────────────────────
    {
        // One 2x2 cell: red site 200, green sites 100, blue site 50.
        struct P { int id; int rx, ry; const char* name; } pats[] = {
            { ser::BAYER_RGGB, 0, 0, "RGGB" }, { ser::BAYER_GRBG, 1, 0, "GRBG" },
            { ser::BAYER_GBRG, 0, 1, "GBRG" }, { ser::BAYER_BGGR, 1, 1, "BGGR" } };
        for (const P& p : pats) {
            ser::Info in = ser::ParseHeader(Header(p.id, 1, 2, 2, 8, 1).data(), 178 + 4);
            uint8_t raw[4];
            for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x)
                raw[y * 2 + x] = (x == p.rx && y == p.ry) ? 200 : (x != p.rx && y != p.ry) ? 50 : 100;
            std::vector<uint8_t> bgra;
            ser::ToBgra(raw, in, true, ser::Stretch{0, 65535}, bgra);
            Check(bgra[2] == 200 && bgra[1] == 100 && bgra[0] == 50 && bgra[12] == 50 && bgra[14] == 200,
                  std::string(p.name) + ": red and blue land where they belong");
            ser::ToBgra(raw, in, false, ser::Stretch{0, 65535}, bgra);
            Check(bgra[0] == raw[0] && bgra[4] == raw[1], std::string(p.name) + ": RAW shows the mosaic");
        }
    }

    // ── 7. Past 4 GB ───────────────────────────────────────────
    {
        // A 1 MB frame, 4200 frames claimed: the last starts past 4 GB.
        // Written sparse (seek, then a frame), so it costs no disk.
        const std::string p = Temp("big");
        const int W = 1024, H = 1024, N = 4200;
        HANDLE hf = CreateFileA(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD got = 0;
        DeviceIoControl(hf, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &got, nullptr);
        auto h = Header(ser::MONO, 1, W, H, 8, N);
        WriteFile(hf, h.data(), 178, &got, nullptr);
        LARGE_INTEGER at; at.QuadPart = 178 + (int64_t)(N - 1) * W * H;
        SetFilePointerEx(hf, at, nullptr, FILE_BEGIN);
        std::vector<uint8_t> last((size_t)W * H, 0);
        last[0] = 77; last.back() = 99;
        WriteFile(hf, last.data(), (DWORD)last.size(), &got, nullptr);
        CloseHandle(hf);
        ser::Reader r;
        Check(r.Open(p) && r.GetInfo().frames == N, "a 4.4 GB file opens with all its frames");
        std::vector<uint8_t> raw;
        Check(r.ReadFrame(N - 1, raw) && raw[0] == 77 && raw.back() == 99,
              "the frame past 4 GB is read from the right place");
        r.Close();
        remove(p.c_str());
    }

    // ── 8. The quality scan ────────────────────────────────────
    {
        // 130 frames of sky noise; frames 40-89 have a bright disc on them.
        const std::string p = Temp("scan");
        const int W = 160, H = 120, N = 130;
        auto b = Header(ser::MONO, 1, W, H, 8, N);
        std::mt19937 rng(7);
        std::normal_distribution<float> noise(20.0f, 4.0f);
        for (int k = 0; k < N; ++k) {
            const bool disc = k >= 40 && k < 90;
            for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
                float v = noise(rng);
                const float r = std::hypot(x - 80.0f, y - 60.0f);
                if (disc && r < 18.0f) v += 180.0f;
                else if (disc && r < 21.0f) v += 180.0f * (21.0f - r) / 3.0f;
                b.push_back((uint8_t)std::clamp(v, 0.0f, 255.0f));
            }
        }
        WriteFile(p, b);
        ser::Scan scan;
        scan.Start(p, N);
        for (int i = 0; i < 400 && scan.Running(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(25));
        Check(!scan.Running() && scan.Scanned() == N, "the scan reaches every frame",
              std::to_string(scan.Scanned()));
        int hits = 0, misses = 0;
        for (int k = 0; k < N; ++k) {
            const bool disc = k >= 40 && k < 90;
            const float s = scan.Score(k);
            if (disc && s > 0.0f) ++hits;
            if (!disc && s == 0.0f) ++misses;
        }
        Check(hits == 50, "the disc is found on every frame that has it", std::to_string(hits) + "/50");
        Check(misses == 80, "and the empty frames score 0", std::to_string(misses) + "/80");
        scan.Stop();
        remove(p.c_str());
    }

    printf("\n%s\n", g_fail ? "FAILED" : "all passed");
    return g_fail ? 1 : 0;
}
