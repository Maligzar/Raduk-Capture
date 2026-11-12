// ============================================================
//  Phase 135.2: the header counts its frames while the run is going
//
//  SER's FrameCount was written as 0 at open and patched only at close,
//  so a crash or a power cut left a file of good frames whose header
//  said it held none. FITS's NAXIS3 likewise. Now both are brought up to
//  date once a second, by the frames' own timestamps.
//
//    1. SER: 25 frames 100 ms apart, never closed. The header, read
//       from disk with a separate handle, counts within one second's
//       worth of 25; closing then sets it exactly.
//    2. FITS: the same for NAXIS3.
//    3. The rule: due on the first frame, then each second, and again
//       at once if the clock goes backwards.
// ============================================================

#include "core/SERWriter.h"
#include "core/FITSWriter.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static std::vector<uint8_t> ReadHead(const std::string& p, size_t n) {
    std::vector<uint8_t> b(n, 0);
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return {};
    b.resize(fread(b.data(), 1, n, f));
    fclose(f);
    return b;
}
static int SerCount(const std::string& p) {
    const auto h = ReadHead(p, 178);
    if (h.size() < 42) return -1;
    int32_t c; memcpy(&c, h.data() + 38, 4);
    return c;
}
static int FitsNaxis3(const std::string& p) {
    const auto h = ReadHead(p, 2880 * 4);
    const std::string s(h.begin(), h.end());
    const size_t k = s.find("NAXIS3  =");
    return k == std::string::npos ? -1 : atoi(s.c_str() + k + 10);
}
static std::string Tmp(const char* name) {
    const char* t = getenv("TEMP");
    return std::string(t ? t : ".") + "/" + name;
}

int main() {
    printf("Live header counts (phase 135.2)\n");
    const int W = 64, H = 48, K = 25;
    std::vector<uint8_t> px((size_t)W * H, 90);

    printf("\n1. SER, never closed\n");
    {
        const std::string p = Tmp("raduk_live_header.ser");
        SERWriter w;
        Check(w.Open(p, W, H, 8, true), "opens");
        for (int i = 0; i < K; ++i) w.WriteFrame(px.data(), px.size(), 1000000ull + (uint64_t)i * 100000, 0.0f);
        const int c = SerCount(p);   // still open: as a crash would leave it
        Check(c >= K - 10 && c <= K, "the header on disk counts within a second of 25",
              std::to_string(c));
        Check(c > 0, "not 0, as it was until now");
        w.Close();
        Check(SerCount(p) == K, "closed: exactly 25", std::to_string(SerCount(p)));
        remove(p.c_str());
    }

    printf("\n2. FITS, never closed\n");
    {
        const std::string p = Tmp("raduk_live_header.fits");
        FITSWriter w;
        ObservationRecord r; r.width = W; r.height = H; r.bitDepth = 8; r.colour = "RGGB";
        w.NoteObservation(r);
        Check(w.Open(p, W, H, 8, true), "opens");
        for (int i = 0; i < K; ++i) w.WriteFrame(px.data(), px.size(), 1000000ull + (uint64_t)i * 100000, 0.0f);
        const int n = FitsNaxis3(p);
        Check(n >= K - 10 && n <= K, "NAXIS3 on disk within a second of 25", std::to_string(n));
        w.Close();
        Check(FitsNaxis3(p) == K, "closed: exactly 25", std::to_string(FitsNaxis3(p)));
        remove(p.c_str());
    }

    printf("\n3. When a patch is due\n");
    Check(HeaderPatchDue(0, 5, true), "the first frame");
    Check(!HeaderPatchDue(1000000, 1999999, false), "not within the second");
    Check(HeaderPatchDue(1000000, 2000000, false), "a second later");
    Check(HeaderPatchDue(5000000, 1000, false), "a clock that went backwards");

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
