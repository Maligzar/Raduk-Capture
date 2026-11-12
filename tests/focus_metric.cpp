// ============================================================
//  The planetary focus metric (FocusMetric.h)
//
//  Synthetic Saturn frames as the rig took them on 2026-11-07: a disc
//  and ring, blurred by defocus, at a gain where read noise is ~12 DN,
//  hot pixels, and a peak that saturates near focus. The old
//  Laplacian sharpness is printed beside it for comparison.
//
//  What the metric must do:
//    - fall monotonically as blur grows, from focus out to a wide blur;
//    - barely change when exposure doubles (auto-exposure moved during
//      that night's sweeps) or the disc saturates.
// ============================================================

#include "core/FocusMetric.h"
#include "core/Sharpness.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace raduk::core;

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("  FAIL %s:%d  ", __FILE__, __LINE__); \
    std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const int W = 1280, H = 960;

// Saturn-ish: disc r=22 px plus an inclined ring, then a box blur of
// half-width `blur` (a defocused obstructed scope is closer to a disc
// than a Gaussian; a box is between the two), then noise.
static std::vector<uint8_t> Frame(int blur, double gainScale, unsigned seed) {
    std::vector<double> v((size_t)W * H, 0.0);
    const double cx = 700, cy = 420;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const double dx = x - cx, dy = y - cy;
            double s = 0;
            if (dx * dx + dy * dy <= 22.0 * 22.0) s = 150;
            const double re = std::sqrt(dx * dx + (dy * 2.6) * (dy * 2.6));
            if (re >= 30 && re <= 50) s += 90;
            v[(size_t)y * W + x] = s;
        }
    if (blur > 0) {   // separable box blur
        std::vector<double> t(v.size());
        for (int y = 0; y < H; ++y) {
            double acc = 0; int n = 0;
            for (int x = -blur; x < W + blur; ++x) {
                if (x + blur < W) { acc += v[(size_t)y * W + x + blur]; ++n; }
                if (x - blur - 1 >= 0) { acc -= v[(size_t)y * W + x - blur - 1]; --n; }
                if (x >= 0 && x < W) t[(size_t)y * W + x] = n ? acc / n : 0;
            }
        }
        for (int x = 0; x < W; ++x) {
            double acc = 0; int n = 0;
            for (int y = -blur; y < H + blur; ++y) {
                if (y + blur < H) { acc += t[(size_t)(y + blur) * W + x]; ++n; }
                if (y - blur - 1 >= 0) { acc -= t[(size_t)(y - blur - 1) * W + x]; --n; }
                if (y >= 0 && y < H) v[(size_t)y * W + x] = n ? acc / n : 0;
            }
        }
    }
    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0.0, 12.0);
    std::uniform_int_distribution<int> px(0, W * H - 1);
    std::vector<uint8_t> b(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        const double d = 10.0 + v[i] * gainScale + noise(rng);
        b[i] = (uint8_t)(d < 0 ? 0 : d > 255 ? 255 : d);
    }
    for (int k = 0; k < 300; ++k) b[px(rng)] = 255;    // hot pixels
    return b;
}

int main() {
    std::printf("1. defocus series\n");
    const int blurs[] = { 0, 4, 8, 16, 32, 64 };
    float prev = 1e9f;
    for (int bl : blurs) {
        const auto f = Frame(bl, 1.0, 100 + bl);
        const TargetFind tf = FindBrightTarget(f.data(), W, H, 8);
        const FocusMeasure fm = MeasurePlanetFocus(f.data(), W, H, 8, tf);
        const double var = BayerLaplacianVariance(f.data(), W, H, 8,
                                                  (uint32_t)tf.x, (uint32_t)tf.y, 128);
        std::printf("   blur %2d px: HFD %6.1f px  score %6.2f  (old sharpness %6.0f%%)\n",
                    bl, fm.hfdPx, fm.score, SharpnessPercent(var) * 1.0);
        CHECK(fm.valid, "blur %d: no measurement", bl);
        CHECK(fm.score < prev, "blur %d: score %.2f did not fall (previous %.2f)",
              bl, fm.score, prev);
        prev = fm.score;
    }

    std::printf("2. exposure and saturation\n");
    for (int bl : { 0, 16 }) {
        const auto a = Frame(bl, 1.0, 7);
        const auto b = Frame(bl, 2.0, 8);   // twice the exposure: the disc saturates
        const TargetFind ta = FindBrightTarget(a.data(), W, H, 8);
        const TargetFind tb = FindBrightTarget(b.data(), W, H, 8);
        const FocusMeasure fa = MeasurePlanetFocus(a.data(), W, H, 8, ta);
        const FocusMeasure fb = MeasurePlanetFocus(b.data(), W, H, 8, tb);
        const float ch = std::fabs(fb.score - fa.score) / fa.score * 100.0f;
        std::printf("   blur %2d px: score %.2f at 1x, %.2f at 2x (%.1f%% change)\n",
                    bl, fa.score, fb.score, ch);
        CHECK(ch < 12.0f, "blur %d: exposure moved the score %.1f%%", bl, ch);
    }

    // ── 3. Phase 145.1: the edge metric, one check per correction ──
    std::printf("3. edge metric (Tenengrad, gated, normalised)\n");
    EdgeScratch sc;
    {
        // (1) The border: a flat field has no gradient anywhere. The
        // sketch read an unwritten zero ring and scored every ROI's edge.
        std::vector<uint8_t> flat((size_t)W * H, 120);
        const EdgeFocus e = MeasureEdgeFocus(flat.data(), W, H, 8, 640, 480, 100, 0.0f, sc);
        std::printf("   flat field: score %.4f, %d of %d above the gate\n", e.score, e.active, e.points);
        CHECK(e.valid && e.score == 0.0f && e.active == 0, "a flat field scored %.4f", e.score);
    }
    float sharpEdge = 0.0f;
    {
        // (3) Falls with blur, at a fixed ROI round the disc.
        float prevE = 1e9f;
        for (int bl : blurs) {
            const auto f = Frame(bl, 1.0, 300 + bl);
            const EdgeFocus e = MeasureEdgeFocus(f.data(), W, H, 8, 700, 420, 110, 10.0f, sc);
            std::printf("   blur %2d px: edge score %8.3f  (%d active)\n", bl, e.score, e.active);
            CHECK(e.valid, "blur %d: no edge measurement", bl);
            CHECK(e.score < prevE, "blur %d: edge score %.3f did not fall (previous %.3f)", bl, e.score, prevE);
            if (bl == 0) sharpEdge = e.score;
            prevE = e.score;
        }
    }
    {
        // (2) Pure noise mostly fails the gate, so it scores a small
        // fraction of a sharp planet. At 1.5 x raw sigma nearly all of it passed.
        std::mt19937 rng(5);
        std::normal_distribution<double> nz(0.0, 12.0);
        std::vector<uint8_t> nf((size_t)W * H);
        for (auto& v : nf) { const double d = 100.0 + nz(rng); v = (uint8_t)(d < 0 ? 0 : d > 255 ? 255 : d); }
        const EdgeFocus e = MeasureEdgeFocus(nf.data(), W, H, 8, 640, 480, 110, 0.0f, sc);
        const float frac = e.points ? (float)e.active / (float)e.points : 1.0f;
        std::printf("   pure noise: %.2f%% above the gate, score %.4f (sharp planet %.3f)\n",
                    frac * 100.0f, e.score, sharpEdge);
        CHECK(frac < 0.03f, "noise passed the gate at %.1f%%", frac * 100.0f);
        CHECK(e.score < 0.02f * sharpEdge, "noise scored %.4f against a planet's %.3f", e.score, sharpEdge);
    }
    {
        // (5) Exposure cancels: half and full exposure, below saturation.
        for (int bl : { 0, 8 }) {
            const auto a1 = Frame(bl, 1.0, 21), a2 = Frame(bl, 0.5, 22);
            const EdgeFocus e1 = MeasureEdgeFocus(a1.data(), W, H, 8, 700, 420, 110, 10.0f, sc);
            const EdgeFocus e2 = MeasureEdgeFocus(a2.data(), W, H, 8, 700, 420, 110, 10.0f, sc);
            const float ch = std::fabs(e2.score - e1.score) / e1.score * 100.0f;
            std::printf("   blur %2d px: edge %.3f at 1x, %.3f at 0.5x (%.1f%%)\n", bl, e1.score, e2.score, ch);
            CHECK(ch < 15.0f, "blur %d: exposure moved the edge score %.1f%%", bl, ch);
        }
    }
    {
        // (4a) RAW16, left-aligned, agrees with RAW8 of the same scene.
        // Its random low byte adds half a count to every pixel on the
        // 8-bit scale, so its sky is 10.5, not 10.
        const auto f8 = Frame(4, 1.0, 31);
        std::vector<uint16_t> f16(f8.size());
        std::mt19937 rng(9);
        std::uniform_int_distribution<int> lo(0, 255);
        for (size_t i = 0; i < f8.size(); ++i) f16[i] = (uint16_t)((f8[i] << 8) | lo(rng));
        const EdgeFocus e8  = MeasureEdgeFocus(f8.data(), W, H, 8, 700, 420, 110, 10.0f, sc);
        const EdgeFocus e16 = MeasureEdgeFocus(reinterpret_cast<const uint8_t*>(f16.data()), W, H, 16,
                                               700, 420, 110, 10.5f, sc);
        const float ch = std::fabs(e16.score - e8.score) / e8.score * 100.0f;
        std::printf("   RAW8 %.3f, RAW16 %.3f (%.1f%%)\n", e8.score, e16.score, ch);
        CHECK(ch < 3.0f, "RAW16 and RAW8 differ by %.1f%%", ch);
    }
    {
        // (4b) Pattern-free: one scene mosaicked as RGGB and as GRBG,
        // with unequal channel gains, scores the same.
        const auto base = Frame(2, 1.0, 41);
        auto mosaic = [&](bool grbg) {
            std::vector<uint8_t> m(base.size());
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    const int ex = x & 1, ey = y & 1;
                    // RGGB: R at (0,0), B at (1,1); GRBG: R at (1,0), B at (0,1).
                    double g = 1.0;
                    if (!grbg) { if (!ex && !ey) g = 0.6; else if (ex && ey) g = 0.4; }
                    else       { if (ex && !ey) g = 0.6; else if (!ex && ey) g = 0.4; }
                    m[(size_t)y * W + x] = (uint8_t)(base[(size_t)y * W + x] * g);
                }
            return m;
        };
        const auto a1 = mosaic(false), a2 = mosaic(true);
        const EdgeFocus e1 = MeasureEdgeFocus(a1.data(), W, H, 8, 700, 420, 110, 6.0f, sc);
        const EdgeFocus e2 = MeasureEdgeFocus(a2.data(), W, H, 8, 700, 420, 110, 6.0f, sc);
        const float ch = std::fabs(e2.score - e1.score) / e1.score * 100.0f;
        std::printf("   RGGB %.3f, GRBG %.3f (%.1f%%)\n", e1.score, e2.score, ch);
        CHECK(ch < 3.0f, "RGGB and GRBG differ by %.1f%%", ch);
    }

    if (g_fail) { std::printf("focus_metric: %d FAILED\n", g_fail); return 1; }
    std::printf("focus_metric: all passed\n");
    return 0;
}
