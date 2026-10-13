// Histogram accuracy deep dive — the bench half.
//
// Drives the real FrameAnalyzer with synthetic RGGB frames whose answers
// can be worked out by hand, and checks what comes back. The questions
// are the ones that decide whether the per-channel histogram means
// anything:
//
//   1. Does each channel read the pixels it claims to?
//   2. What does the G channel actually contain?
//   3. Do the clip percentages count saturation, and in 16-bit too?
//   4. How much of the frame is it really measuring?
//
// Nothing here needs a camera or a sky.

#include <cstdio>
#include <cstring>
#include <vector>
#include <chrono>
#include <thread>
#include "core/FrameAnalyzer.h"
#include "core/FrameSlot.h"

using namespace raduk::core;

static int g_fail = 0;

static void Check(const char* what, bool ok, const char* detail = "") {
    printf("  [%s] %-52s %s\n", ok ? "PASS" : "FAIL", what, detail);
    if (!ok) ++g_fail;
}

// Build an RGGB frame where each Bayer position gets a chosen value.
// RGGB means, within every 2x2 block:
//     (even row, even col) = R     (even row, odd col) = Gr
//     (odd  row, even col) = Gb    (odd  row, odd col) = B
static std::vector<uint8_t> MakeFrame8(uint32_t w, uint32_t h,
                                       uint8_t R, uint8_t Gr,
                                       uint8_t Gb, uint8_t B) {
    std::vector<uint8_t> f((size_t)w * h);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const bool er = (y % 2) == 0, ec = (x % 2) == 0;
            f[(size_t)y * w + x] = er ? (ec ? R : Gr) : (ec ? Gb : B);
        }
    return f;
}

static std::vector<uint8_t> MakeFrame16(uint32_t w, uint32_t h,
                                        uint16_t R, uint16_t Gr,
                                        uint16_t Gb, uint16_t B) {
    std::vector<uint8_t> f((size_t)w * h * 2);
    auto* p = reinterpret_cast<uint16_t*>(f.data());
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const bool er = (y % 2) == 0, ec = (x % 2) == 0;
            p[(size_t)y * w + x] = er ? (ec ? R : Gr) : (ec ? Gb : B);
        }
    return f;
}

static AnalysisResult Run(FrameAnalyzer& a, const std::vector<uint8_t>& data,
                          uint32_t w, uint32_t h, int bitDepth) {
    FrameSlot slot{};
    slot.data      = const_cast<uint8_t*>(data.data());
    slot.dataSize  = data.size();
    slot.width     = w;
    slot.height    = h;
    slot.bitDepth  = bitDepth;
    slot.timestamp = 0;

    // Submit is non-blocking and skips if busy, so retry until taken.
    for (int i = 0; i < 400; ++i) {
        a.Submit(&slot);
        for (int j = 0; j < 100; ++j) {
            if (a.HasResult()) return a.GetResult();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    printf("  !! analyzer never produced a result\n");
    return {};
}

int main() {
    printf("\n=== Histogram deep dive: FrameAnalyzer against known frames ===\n\n");

    FrameAnalyzer a;
    a.Start();
    const uint32_t W = 256, H = 256;

    // ── 1. Channel assignment ─────────────────────────────────
    // Give every Bayer position a distinct value. If the sampler reads
    // the positions it claims, each channel's mean is its own value.
    printf("1. Channel assignment  (R=40  Gr=80  Gb=120  B=200)\n");
    {
        auto f = MakeFrame8(W, H, 40, 80, 120, 200);
        auto r = Run(a, f, W, H, 8);
        char d[96];
        snprintf(d, sizeof(d), "meanR=%.1f", r.meanR);
        Check("R reads the even-row/even-col pixel", (int)(r.meanR + 0.5f) == 40, d);
        snprintf(d, sizeof(d), "meanB=%.1f", r.meanB);
        Check("B reads the odd-row/odd-col pixel",   (int)(r.meanB + 0.5f) == 200, d);
        snprintf(d, sizeof(d), "meanG=%.1f  (Gr=80, Gb=120, mean of the two = 100)", r.meanG);
        Check("G is the AVERAGE of Gr and Gb, not one of them",
              (int)(r.meanG + 0.5f) == 100, d);
    }

    // ── 2. What G averaging costs ─────────────────────────────
    // Same mean, different spread. Gr/Gb far apart vs identical: G's
    // histogram should be a single bin either way, because averaging
    // collapses the pair before the histogram sees it.
    printf("\n2. What the G average hides  (Gr/Gb split vs equal)\n");
    {
        auto split = MakeFrame8(W, H, 100, 20, 180, 100);   // G mean 100
        auto flat  = MakeFrame8(W, H, 100, 100, 100, 100);  // G mean 100
        auto rs = Run(a, split, W, H, 8);
        auto rf = Run(a, flat,  W, H, 8);

        int binsSplit = 0, binsFlat = 0;
        for (int i = 0; i < 256; ++i) {
            if (rs.histG[i]) ++binsSplit;
            if (rf.histG[i]) ++binsFlat;
        }
        char d[128];
        snprintf(d, sizeof(d), "Gr=20/Gb=180 occupies %d bin(s); Gr=Gb=100 occupies %d",
                 binsSplit, binsFlat);
        Check("a 160-count Gr/Gb split is invisible in the G histogram",
              binsSplit == 1 && binsFlat == 1, d);
        snprintf(d, sizeof(d), "meanG split=%.1f  flat=%.1f", rs.meanG, rf.meanG);
        Check("both report the same G mean", (int)(rs.meanG+0.5f) == (int)(rf.meanG+0.5f), d);
    }

    // ── 3. Clip percentage ────────────────────────────────────
    printf("\n3. Clip percentage\n");
    {
        auto sat = MakeFrame8(W, H, 255, 255, 255, 255);
        auto r = Run(a, sat, W, H, 8);
        char d[96];
        snprintf(d, sizeof(d), "clipR=%.1f%% clipG=%.1f%% clipB=%.1f%%", r.clipR, r.clipG, r.clipB);
        Check("a fully saturated 8-bit frame reads 100% clipped",
              r.clipR > 99.5f && r.clipG > 99.5f && r.clipB > 99.5f, d);

        auto half = MakeFrame8(W, H, 255, 128, 128, 128);
        auto r2 = Run(a, half, W, H, 8);
        snprintf(d, sizeof(d), "clipR=%.1f%% clipB=%.1f%%", r2.clipR, r2.clipB);
        Check("only the saturated channel reports clipping",
              r2.clipR > 99.5f && r2.clipB < 0.5f, d);
    }

    // ── 4. 16-bit handling ────────────────────────────────────
    // ASI RAW16 is left-aligned, so a 12-bit sensor at full scale is
    // 4095 << 4 = 65520. That must read as saturated, and mid-scale
    // must not.
    printf("\n4. 16-bit left-aligned data\n");
    {
        auto full = MakeFrame16(W, H, 65520, 65520, 65520, 65520);
        auto r = Run(a, full, W, H, 16);
        char d[96];
        snprintf(d, sizeof(d), "clipR=%.1f%%  meanR=%.1f", r.clipR, r.meanR);
        Check("12-bit full scale (4095<<4) reads as clipped", r.clipR > 99.5f, d);

        auto mid = MakeFrame16(W, H, 2048 << 4, 2048 << 4, 2048 << 4, 2048 << 4);
        auto r2 = Run(a, mid, W, H, 16);
        snprintf(d, sizeof(d), "meanR=%.1f (expected 128), clipR=%.1f%%", r2.meanR, r2.clipR);
        Check("12-bit mid scale lands at 8-bit mid, not clipped",
              (int)(r2.meanR + 0.5f) == 128 && r2.clipR < 0.5f, d);
    }

    // ── 5. How much of the frame is sampled ───────────────────
    // Count the superpixels the histogram actually contains and compare
    // with the frame size.
    printf("\n5. Sampling coverage\n");
    {
        auto f = MakeFrame8(W, H, 10, 10, 10, 10);
        auto r = Run(a, f, W, H, 8);
        uint64_t n = 0;
        for (int i = 0; i < 256; ++i) n += r.histR[i];
        const uint64_t pixels = (uint64_t)W * H;
        char d[128];
        snprintf(d, sizeof(d), "%llu samples from %llu pixels = %.2f%% of the frame",
                 (unsigned long long)n, (unsigned long long)pixels,
                 100.0 * n / (double)pixels);
        Check("histogram is built from a SAMPLE, not the whole frame",
              n > 0 && n < pixels / 8, d);
    }

    a.Stop();
    printf("\n=== %s ===\n\n", g_fail == 0 ? "all checks behaved as the code says"
                                           : "SOME CHECKS FAILED");
    // Non-zero on failure: this is a merge gate, and a gate that
    // always reports success is worse than no gate, because it is
    // believed.
    return g_fail == 0 ? 0 : 1;
}
