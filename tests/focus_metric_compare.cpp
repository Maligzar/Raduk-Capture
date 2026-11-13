// ============================================================
//  Phase 145.0: HFD against the edge metric, on synthetic scenes
//
//  The case for two stages, measured on the bench rather than argued:
//
//    Saturn-sized disc (90 px), Jupiter-sized disc (220 px, with belts),
//    and a frame-filling lunar patch (LunarSurface.h's texture and
//    craters). Each frame: defocus (a box blur), seeing (a further blur
//    of 0 to 2 px and a shift of up to 1 px, drawn per frame), and read
//    noise of 12 DN at high gain.
//
//  What is asserted:
//    1. Near focus (blur 0 against 3 px, about one fine step), the edge
//       metric separates the two positions by more frame-to-frame
//       scatters than HFD does. Measured 2026-11-12: HFD 0.1 to 0.2,
//       edge 2.2 to 2.3 on Saturn and Jupiter. This is the fine stage's
//       job.
//    2. On the Moon there is no disc: HFD has nothing to measure, and
//       the edge metric still separates the positions.
//    3. Far from focus (blur 48 against 72 on Saturn), HFD separates
//       them clearly (measured 44 scatters, the edge metric 12). This is
//       the coarse stage's job.
//    4. The 75th percentile of 24 frames scatters far less than one
//       frame, which is why the fine stage takes many frames.
// ============================================================

#include "core/FocusMetric.h"
#include "core/LunarSurface.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace raduk::core;

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("  FAIL %s:%d  ", __FILE__, __LINE__); \
    std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const int W = 640, H = 480;
enum class Scene { Saturn, Jupiter, Moon };

static void BoxBlur(std::vector<double>& v, int r) {
    if (r <= 0) return;
    std::vector<double> t(v.size());
    for (int y = 0; y < H; ++y) {
        double acc = 0; int n = 0;
        for (int x = -r; x < W + r; ++x) {
            if (x + r < W) { acc += v[(size_t)y * W + x + r]; ++n; }
            if (x - r - 1 >= 0) { acc -= v[(size_t)y * W + x - r - 1]; --n; }
            if (x >= 0 && x < W) t[(size_t)y * W + x] = n ? acc / n : 0;
        }
    }
    for (int x = 0; x < W; ++x) {
        double acc = 0; int n = 0;
        for (int y = -r; y < H + r; ++y) {
            if (y + r < H) { acc += t[(size_t)(y + r) * W + x]; ++n; }
            if (y - r - 1 >= 0) { acc -= t[(size_t)(y - r - 1) * W + x]; --n; }
            if (y >= 0 && y < H) v[(size_t)y * W + x] = n ? acc / n : 0;
        }
    }
}

static std::vector<double> Truth(Scene s) {
    std::vector<double> v((size_t)W * H, 0.0);
    const double cx = W / 2.0, cy = H / 2.0;
    const raduk::core::SunDir sun = raduk::core::MoonSunDirection(0.30);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const double dx = x - cx, dy = y - cy;
            double val = 0;
            if (s == Scene::Saturn) {
                if (dx * dx + dy * dy <= 45.0 * 45.0) val = 150;
            } else if (s == Scene::Jupiter) {
                const double r2 = (dx * dx + dy * dy) / (110.0 * 110.0);
                if (r2 <= 1.0) {
                    val = 170 * (0.65 + 0.35 * std::sqrt(1.0 - r2));      // limb darkening
                    const double lat = dy / 110.0;                         // belts: dark bands
                    if ((lat > 0.10 && lat < 0.25) || (lat < -0.12 && lat > -0.28)) val *= 0.75;
                    if (lat > 0.45 && lat < 0.50) val *= 0.85;
                }
            } else {
                // A patch near the terminator: 0.6 deg of lunar surface across.
                const double lat = 10.0 + dy * 0.6 / W, lon = -20.0 + dx * 0.6 / W;
                const double tex = raduk::core::LunarFineTexture(lat * 40.0, lon * 40.0);
                const double cr = raduk::core::LunarCraterShade(lat * 40.0, lon * 40.0, sun, 0.4, 2.0);
                val = 120.0 * tex * cr;
            }
            v[(size_t)y * W + x] = val;
        }
    return v;
}

// One frame at a defocus, with this frame's seeing and noise.
static std::vector<uint8_t> Frame(const std::vector<double>& truth, int blur, std::mt19937& rng) {
    std::vector<double> v = truth;
    BoxBlur(v, blur);
    std::uniform_int_distribution<int> seeBlur(0, 2), shift(-1, 1);
    BoxBlur(v, seeBlur(rng));
    const int sx = shift(rng), sy = shift(rng);
    std::normal_distribution<double> noise(0.0, 12.0);
    std::vector<uint8_t> b((size_t)W * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const int xx = std::clamp(x - sx, 0, W - 1), yy = std::clamp(y - sy, 0, H - 1);
            const double d = 10.0 + v[(size_t)yy * W + xx] + noise(rng);
            b[(size_t)y * W + x] = (uint8_t)(d < 0 ? 0 : d > 255 ? 255 : d);
        }
    return b;
}

struct Scores { std::vector<double> hfd, edge; };

static Scores Measure(Scene s, const std::vector<double>& truth, int blur, int frames, unsigned seed) {
    std::mt19937 rng(seed);
    EdgeScratch sc;
    Scores out;
    const int half = (s == Scene::Saturn) ? 80 : (s == Scene::Jupiter ? 150 : 128);
    const float bg = (s == Scene::Moon) ? 0.0f : 10.0f;
    for (int i = 0; i < frames; ++i) {
        const auto f = Frame(truth, blur, rng);
        const TargetFind tf = FindBrightTarget(f.data(), W, H, 8);
        const FocusMeasure fm = MeasurePlanetFocus(f.data(), W, H, 8, tf);
        if (fm.valid && s != Scene::Moon) out.hfd.push_back(fm.score);
        const EdgeFocus e = MeasureEdgeFocus(f.data(), W, H, 8, W / 2.0f, H / 2.0f, half, bg, sc);
        if (e.valid) out.edge.push_back(e.score);
    }
    return out;
}

static double Median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
static double Sd(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;
    double m = 0; for (double x : v) m += x; m /= v.size();
    double s = 0; for (double x : v) s += (x - m) * (x - m);
    return std::sqrt(s / (v.size() - 1));
}
// How many frame-to-frame scatters apart two positions are.
static double Separation(const std::vector<double>& a, const std::vector<double>& b) {
    const double sd = std::sqrt(0.5 * (Sd(a) * Sd(a) + Sd(b) * Sd(b)));
    if (a.empty() || b.empty()) return 0.0;
    if (sd <= 0) return 1e9;
    return std::fabs(Median(a) - Median(b)) / sd;
}
static double P75(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[(size_t)(0.75 * (v.size() - 1))];
}

int main() {
    std::printf("Focus metrics compared (phase 145.0)\n");
    const int N = 16;

    std::printf("\n1. Near focus: blur 0 against 3 px\n");
    double jupH = 0, jupE = 0;
    for (Scene s : { Scene::Saturn, Scene::Jupiter }) {
        const auto t = Truth(s);
        const Scores a = Measure(s, t, 0, N, 11), b = Measure(s, t, 3, N, 12);
        const double sh = Separation(a.hfd, b.hfd), se = Separation(a.edge, b.edge);
        std::printf("   %-7s HFD apart by %5.1f scatters, edge by %5.1f\n",
                    s == Scene::Saturn ? "Saturn" : "Jupiter", sh, se);
        CHECK(se > sh, "%s: the edge metric separated %.1f, HFD %.1f", s == Scene::Saturn ? "Saturn" : "Jupiter", se, sh);
        if (s == Scene::Jupiter) { jupH = sh; jupE = se; }
    }
    // Single frames: the fine stage scores 24 per position and keeps the
    // 75th percentile, which scatters about 2.5x less (section 4), so a
    // single-frame separation of 1.5 is about 4 at the position.
    CHECK(jupE > 5.0 * jupH && jupE > 1.5, "Jupiter: edge %.1f is not well clear of HFD %.1f", jupE, jupH);

    std::printf("\n2. The Moon: no disc\n");
    {
        const auto t = Truth(Scene::Moon);
        const Scores a = Measure(Scene::Moon, t, 0, N, 21), b = Measure(Scene::Moon, t, 3, N, 22);
        const double se = Separation(a.edge, b.edge);
        std::printf("   edge apart by %.1f scatters (median %.3f against %.3f); HFD not measured\n",
                    se, Median(a.edge), Median(b.edge));
        CHECK(a.edge.size() == (size_t)N && se > 3.0, "Moon: edge separation %.1f", se);
    }

    std::printf("\n3. Far from focus on Saturn: blur 48 against 72 px\n");
    {
        const auto t = Truth(Scene::Saturn);
        const Scores a = Measure(Scene::Saturn, t, 48, N, 31), b = Measure(Scene::Saturn, t, 72, N, 32);
        const double sh = Separation(a.hfd, b.hfd), se = Separation(a.edge, b.edge);
        std::printf("   HFD apart by %.1f scatters, edge by %.1f (edge medians %.3f, %.3f)\n",
                    sh, se, Median(a.edge), Median(b.edge));
        CHECK(sh > 5.0, "far out, HFD separated only %.1f", sh);
    }

    std::printf("\n4. Many frames: the 75th percentile of 24 against one frame\n");
    {
        const auto t = Truth(Scene::Jupiter);
        const Scores pool = Measure(Scene::Jupiter, t, 2, 240, 41);
        std::vector<double> p75s;
        for (size_t k = 0; k + 24 <= pool.edge.size(); k += 24)
            p75s.push_back(P75(std::vector<double>(pool.edge.begin() + k, pool.edge.begin() + k + 24)));
        const double one = Sd(pool.edge), many = Sd(p75s);
        std::printf("   single-frame scatter %.3f, 24-frame P75 scatter %.3f\n", one, many);
        CHECK(many < 0.5 * one, "the percentile scattered %.3f against %.3f", many, one);
    }

    std::printf(g_fail ? "\n%d FAILED\n" : "\nall passed\n", g_fail);
    return g_fail ? 1 : 0;
}
