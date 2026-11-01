// ============================================================
//  What every capture format shares
//
//  Phase 106.2. The sky record, the clock and the quality sidecar
//  moved out of SERWriter so a second format gets the same ones.
//  This pins them, and pins what a SER looks like on disk through
//  the new interface, by reading the file back with fixed offsets
//  from the SER v3 layout rather than with SERHeader.
// ============================================================

#include "core/SERWriter.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace raduk::core;

static int g_fail = 0;

static void Check(bool ok, const char* what) {
    if (ok) {
        printf("  ok    %s\n", what);
    } else {
        printf("  FAIL  %s\n", what);
        ++g_fail;
    }
}

static std::vector<uint8_t> ReadAll(const std::string& p) {
    std::vector<uint8_t> out;
    FILE* f = nullptr;
    if (fopen_s(&f, p.c_str(), "rb") != 0 || !f) return out;
    fseek(f, 0, SEEK_END);
    out.resize((size_t)ftell(f));
    fseek(f, 0, SEEK_SET);
    if (!out.empty()) fread(out.data(), 1, out.size(), f);
    fclose(f);
    return out;
}

template <typename T>
static T At(const std::vector<uint8_t>& b, size_t off) {
    T v{};
    memcpy(&v, b.data() + off, sizeof(T));
    return v;
}

static std::string TempBase() {
    char dir[MAX_PATH];
    GetTempPathA(MAX_PATH, dir);
    char name[64];
    snprintf(name, sizeof(name), "raduk_capture_writer_%lu",
             GetCurrentProcessId());
    return std::string(dir) + name;
}

int main() {
    printf("Capture writer\n");

    // ── The sky record ─────────────────────────────────────────
    {
        SkyAccumulator s;
        Check(!s.Summary().valid, "nothing noted is not valid");
        s.Note(40.0, 180.0, 1.5, 1.2, 0.75);
        s.Note(42.0, 181.0, 1.4, 1.1, 0.70);
        s.Note(39.0, 182.0, 1.6, 1.3, 0.80);
        const SkySummary v = s.Summary();
        Check(v.valid && v.altStart == 40.0 && v.altEnd == 39.0,
              "first and last altitude");
        Check(v.altMin == 39.0 && v.altMax == 42.0, "altitude extremes");
        Check(v.azStart == 180.0 && v.azEnd == 182.0, "azimuth ends");
        Check(fabs(v.airmassMean - 1.5) < 1e-12 && v.airmassMax == 1.6,
              "airmass mean and worst");
        s.Reset();
        Check(!s.Summary().valid && s.samples == 0 && s.airmassMax == 0.0,
              "reset forgets the run, so the next starts clean");
    }

    // ── The clock ──────────────────────────────────────────────
    {
        FrameClock c;
        c.Start();
        const uint64_t a = Timestamp::Now();
        const int64_t  t = c.TicksFor(a);
        Check(c.TicksFor(a + 1000) - t == 10000, "1 ms of stamp is 10000 ticks");
        const int64_t now = FrameClock::NowUtcTicks();
        Check(t > now - 600000000LL && t <= now + 600000000LL,
              "a stamp lands near the wall clock");
        Check(llabs(c.TicksFor(0) - now) < 600000000LL,
              "a zero stamp falls back to now");
        // 1970-01-01 in SER ticks is 719162 days after 0001-01-01.
        Check(FrameClock::kEpochOffset + 116444736000000000LL ==
                  719162LL * 864000000000LL,
              "the FILETIME offset lands 1970 on the right day");
    }

    // ── A SER, written through the interface ───────────────────
    const std::string base = TempBase();
    const std::string path = base + ".ser";
    const std::string qual = base + "_quality.txt";
    {
        std::unique_ptr<ICaptureWriter> w = std::make_unique<SERWriter>();
        Check(w->Open(path, 4, 2, 8, true), "opens");
        Check(w->Path() == path, "the path comes back for AutoStakkert");

        w->NoteSkyConditions(40.0, 180.0, 1.5, 1.2, 0.75);
        const uint8_t f1[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        const uint8_t f2[8] = { 9, 8, 7, 6, 5, 4, 3, 2 };
        const uint64_t t0 = Timestamp::Now();
        Check(w->WriteFrame(f1, 8, t0, 12.5f), "first frame");
        Check(w->WriteFrame(f2, 8, t0 + 5000, 37.5f), "second frame");

        Check(w->SkyConditionsSummary().valid, "the sky is readable before close");
        Check(w->Close(), "closes");
        Check(!w->SkyConditionsSummary().valid, "and close resets it");

        const std::vector<uint8_t> b = ReadAll(path);
        Check(b.size() == 178 + 16 + 16, "header, two frames, two stamps");
        if (b.size() == 210) {
            Check(memcmp(b.data(), "LUCAM-RECORDER", 14) == 0, "file id");
            Check(At<int32_t>(b, 18) == 8, "ColorID is RGGB");
            Check(At<int32_t>(b, 22) == 1, "little-endian flag");
            Check(At<int32_t>(b, 26) == 4 && At<int32_t>(b, 30) == 2, "size");
            Check(At<int32_t>(b, 34) == 8, "depth");
            Check(At<int32_t>(b, 38) == 2, "the count is patched at close");
            Check(strcmp((const char*)b.data() + 82, kCaptureInstrument) == 0,
                  "instrument");
            Check(strcmp((const char*)b.data() + 122, kCaptureTelescope) == 0,
                  "telescope");
            Check(memcmp(b.data() + 178, f1, 8) == 0 &&
                  memcmp(b.data() + 186, f2, 8) == 0, "pixels, in order");
            Check(At<int64_t>(b, 202) - At<int64_t>(b, 194) == 50000,
                  "the trailer keeps capture spacing, 5 ms apart");
        }

        const std::vector<uint8_t> q = ReadAll(qual);
        const std::string text(q.begin(), q.end());
        Check(text.find("# Frames: 2") != std::string::npos, "sidecar frame count");
        Check(text.find("12.50") != std::string::npos &&
              text.find("37.50") != std::string::npos, "sidecar scores");
        Check(text.find("Altitude:    40.0 deg") != std::string::npos,
              "the sidecar has the sky");
        Check(text.find("# Mean: 25.00  Peak: 37.50") != std::string::npos,
              "sidecar summary");
    }
    {
        SERWriter w;
        w.Open(path, 2, 1, 16, false);
        const uint8_t f[4] = { 1, 2, 3, 4 };
        w.WriteFrame(f, 4, Timestamp::Now(), 1.0f);
        w.Close();
        const std::vector<uint8_t> b = ReadAll(path);
        Check(b.size() > 42 && At<int32_t>(b, 18) == 0 && At<int32_t>(b, 34) == 16,
              "bayerRGGB=false writes MONO, and depth follows");
        const std::vector<uint8_t> q = ReadAll(qual);
        const std::string text(q.begin(), q.end());
        Check(text.find("Scope position") == std::string::npos,
              "the previous run's sky does not leak into this one");
    }
    remove(path.c_str());
    remove(qual.c_str());

    if (g_fail == 0) printf("PASS\n"); else printf("FAIL: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
