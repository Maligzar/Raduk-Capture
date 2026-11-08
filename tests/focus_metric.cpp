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

    if (g_fail) { std::printf("focus_metric: %d FAILED\n", g_fail); return 1; }
    std::printf("focus_metric: all passed\n");
    return 0;
}
