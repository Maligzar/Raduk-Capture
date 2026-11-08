// ============================================================
//  Finding the target on a real sensor (TargetFinder.h)
//
//  Field finding, 2026-11-07: Saturn at gain 353, 2.25 ms, 8-bit, and
//  the centroid never sat on the planet. The old search thresholded at
//  70% of the way from the frame mean to the single brightest pixel,
//  and at that gain the brightest pixel is a hot pixel. Case 1 keeps
//  that algorithm here and shows it failing on such a frame, so the
//  test pins the cause as well as the cure.
//
//  Frames are synthetic, with a deterministic generator: background
//  pedestal, Gaussian-ish read noise, hot pixels at 255, and targets
//  whose true centres are known exactly because they are symmetric.
// ============================================================

#include "core/TargetFinder.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using raduk::core::FindBrightTarget;
using raduk::core::TargetFind;

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++g_fail; \
    std::printf("  FAIL %s:%d  ", __FILE__, __LINE__); \
    std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 6364136223846793005ull + 1442695040888963407ull) {}
    uint32_t next() { s = s * 6364136223846793005ull + 1442695040888963407ull;
                      return (uint32_t)(s >> 33); }
    double uni() { return (next() + 0.5) / 2147483648.0; }
    // Sum of 4 uniforms: close enough to Gaussian for read noise.
    double gauss() { double a = 0; for (int i = 0; i < 4; ++i) a += uni();
                     return (a - 2.0) * std::sqrt(3.0); }
};

struct Frame {
    int w, h;
    std::vector<double> v;   // DN, 8-bit scale
    Frame(int w_, int h_, double bg) : w(w_), h(h_), v((size_t)w_ * h_, bg) {}
    double& at(int x, int y) { return v[(size_t)y * w + x]; }
};

static void AddNoise(Frame& f, double sigma, uint64_t seed) {
    Rng r(seed);
    for (double& d : f.v) d += sigma * r.gauss();
}
static void AddHotPixels(Frame& f, int n, uint64_t seed) {
    Rng r(seed);
    for (int i = 0; i < n; ++i)
        f.at((int)(r.uni() * f.w), (int)(r.uni() * f.h)) = 255.0;
}
// Disc plus an inclined ring, symmetric about (cx, cy).
static void AddSaturn(Frame& f, double cx, double cy, double rDisc, double amp) {
    for (int y = 0; y < f.h; ++y)
        for (int x = 0; x < f.w; ++x) {
            const double dx = x - cx, dy = y - cy;
            const double r = std::sqrt(dx * dx + dy * dy);
            if (r <= rDisc) f.at(x, y) += amp;
            const double re = std::sqrt(dx * dx + (dy * 2.5) * (dy * 2.5));
            if (re >= rDisc * 1.3 && re <= rDisc * 2.2) f.at(x, y) += amp * 0.6;
        }
}
static void AddAnnulus(Frame& f, double cx, double cy, double r0, double r1, double amp) {
    for (int y = 0; y < f.h; ++y)
        for (int x = 0; x < f.w; ++x) {
            const double r = std::hypot(x - cx, y - cy);
            if (r >= r0 && r <= r1) f.at(x, y) += amp;
        }
}
static void AddStar(Frame& f, double cx, double cy, double sigma, double amp) {
    const int R = (int)(sigma * 5) + 1;
    for (int y = (int)cy - R; y <= (int)cy + R; ++y)
        for (int x = (int)cx - R; x <= (int)cx + R; ++x) {
            if (x < 0 || y < 0 || x >= f.w || y >= f.h) continue;
            const double r2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
            f.at(x, y) += amp * std::exp(-r2 / (2 * sigma * sigma));
        }
}
static std::vector<uint8_t> To8(const Frame& f) {
    std::vector<uint8_t> b(f.v.size());
    for (size_t i = 0; i < b.size(); ++i) {
        const double d = std::round(f.v[i]);
        b[i] = (uint8_t)(d < 0 ? 0 : d > 255 ? 255 : d);
    }
    return b;
}
static std::vector<uint8_t> To16(const Frame& f, uint64_t seed) {
    // Left-aligned RAW16, little-endian, with sub-DN detail in the low byte.
    Rng r(seed);
    std::vector<uint8_t> b(f.v.size() * 2);
    for (size_t i = 0; i < f.v.size(); ++i) {
        double d = f.v[i] * 256.0 + r.uni() * 255.0;
        d = d < 0 ? 0 : d > 65535 ? 65535 : d;
        const uint16_t u = (uint16_t)d;
        b[2 * i] = (uint8_t)(u & 0xFF); b[2 * i + 1] = (uint8_t)(u >> 8);
    }
    return b;
}

// The search as it was until this fix (FrameAnalyzer.cpp, phase 3-126).
static bool OldCentroid(const std::vector<uint8_t>& buf, int w, int h,
                        float& ox, float& oy) {
    uint64_t sum = 0; uint32_t n = 0; uint8_t mx = 0;
    for (int y = 0; y < h; y += 2)
        for (int x = 0; x < w; x += 2) {
            const uint8_t v = buf[(size_t)y * w + x];
            sum += v; ++n; if (v > mx) mx = v;
        }
    if (mx <= 20) return false;
    const float mean = (float)sum / n;
    const float thr = mean + 0.7f * (mx - mean);
    double wx = 0, wy = 0, wt = 0;
    for (int y = 0; y < h; y += 2)
        for (int x = 0; x < w; x += 2) {
            const float v = buf[(size_t)y * w + x];
            if (v >= thr) { wx += x * v; wy += y * v; wt += v; }
        }
    if (wt <= 0) return false;
    ox = (float)(wx / wt); oy = (float)(wy / wt);
    return true;
}

static double Err(const TargetFind& t, double x, double y) {
    return std::hypot(t.x - x, t.y - y);
}

int main() {
    const int W = 1280, H = 960;

    // 1. Tonight's frame: dim Saturn off centre, high-gain noise, hot pixels.
    {
        std::printf("1. dim Saturn, gain-353 noise, 400 hot pixels\n");
        Frame f(W, H, 14.0);
        AddSaturn(f, 911.0, 287.0, 28.0, 30.0);
        AddNoise(f, 7.0, 11);
        AddHotPixels(f, 400, 12);
        const auto b = To8(f);
        float ox = 0, oy = 0;
        const bool oldOk = OldCentroid(b, W, H, ox, oy);
        const double oldErr = oldOk ? std::hypot(ox - 911.0, oy - 287.0) : 1e9;
        std::printf("   old search: %s at (%.0f, %.0f), %.0f px off\n",
                    oldOk ? "valid" : "none", ox, oy, oldErr);
        CHECK(oldErr > 100.0, "the old search was expected to miss (%.1f px)", oldErr);
        const TargetFind t = FindBrightTarget(b.data(), W, H, 8);
        std::printf("   new: found=%d (%.2f, %.2f) snr=%.0f cells=%d bg=%.1f noise=%.1f\n",
                    t.found, t.x, t.y, t.snr, t.cells, t.bgDN, t.noiseDN);
        CHECK(t.found, "planet not found");
        CHECK(Err(t, 911.0, 287.0) < 1.5, "off by %.2f px", Err(t, 911.0, 287.0));

        // Same frame as RAW16.
        const auto b16 = To16(f, 13);
        const TargetFind t16 = FindBrightTarget(b16.data(), W, H, 16);
        CHECK(t16.found && Err(t16, 911.0, 287.0) < 1.5,
              "RAW16: found=%d off by %.2f px", t16.found, Err(t16, 911.0, 287.0));
    }

    // 2. Far out of focus: an obstructed donut, dark in the middle.
    {
        std::printf("2. defocused donut\n");
        Frame f(W, H, 14.0);
        AddAnnulus(f, 402.0, 655.0, 45.0, 120.0, 10.0);
        AddNoise(f, 7.0, 21);
        AddHotPixels(f, 400, 22);
        const auto b = To8(f);
        const TargetFind t = FindBrightTarget(b.data(), W, H, 8);
        std::printf("   found=%d (%.2f, %.2f) snr=%.0f cells=%d\n",
                    t.found, t.x, t.y, t.snr, t.cells);
        CHECK(t.found, "donut not found");
        CHECK(Err(t, 402.0, 655.0) < 2.0, "off by %.2f px", Err(t, 402.0, 655.0));
    }

    // 3. Nothing there: noise and hot pixels only.
    {
        std::printf("3. empty sky\n");
        Frame f(W, H, 14.0);
        AddNoise(f, 7.0, 31);
        AddHotPixels(f, 400, 32);
        const auto b = To8(f);
        const TargetFind t = FindBrightTarget(b.data(), W, H, 8);
        std::printf("   found=%d snr=%.1f\n", t.found, t.snr);
        CHECK(!t.found, "found a target in empty sky at (%.0f, %.0f), snr %.1f",
              t.x, t.y, t.snr);
    }

    // 4. A bright moon off to the side must not pull the centre.
    {
        std::printf("4. Saturn with a moon\n");
        Frame f(W, H, 14.0);
        AddSaturn(f, 640.0, 480.0, 28.0, 60.0);
        AddStar(f, 640.0 + 180.0, 480.0 - 10.0, 1.5, 200.0);
        AddNoise(f, 5.0, 41);
        AddHotPixels(f, 200, 42);
        const auto b = To8(f);
        const TargetFind t = FindBrightTarget(b.data(), W, H, 8);
        std::printf("   found=%d (%.2f, %.2f)\n", t.found, t.x, t.y);
        CHECK(t.found && Err(t, 640.0, 480.0) < 1.0,
              "found=%d off by %.2f px", t.found, Err(t, 640.0, 480.0));
    }

    // 5. A star: a few pixels across, sub-pixel position.
    {
        std::printf("5. star\n");
        Frame f(W, H, 10.0);
        AddStar(f, 300.4, 200.7, 1.8, 120.0);
        AddNoise(f, 4.0, 51);
        AddHotPixels(f, 200, 52);
        const auto b = To8(f);
        const TargetFind t = FindBrightTarget(b.data(), W, H, 8);
        std::printf("   found=%d (%.2f, %.2f) snr=%.0f\n", t.found, t.x, t.y, t.snr);
        CHECK(t.found && Err(t, 300.4, 200.7) < 1.0,
              "found=%d off by %.2f px", t.found, Err(t, 300.4, 200.7));
    }

    // 6. A twilight gradient across the frame.
    {
        std::printf("6. sky gradient\n");
        Frame f(W, H, 0.0);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) f.at(x, y) = 10.0 + 30.0 * x / W;
        AddSaturn(f, 250.0, 700.0, 28.0, 35.0);
        AddNoise(f, 6.0, 61);
        AddHotPixels(f, 300, 62);
        const auto b = To8(f);
        const TargetFind t = FindBrightTarget(b.data(), W, H, 8);
        std::printf("   found=%d (%.2f, %.2f) snr=%.0f\n", t.found, t.x, t.y, t.snr);
        CHECK(t.found && Err(t, 250.0, 700.0) < 2.0,
              "found=%d off by %.2f px", t.found, Err(t, 250.0, 700.0));
    }

    // 7. The full sensor: same answer, and fast enough for the analyzer.
    {
        std::printf("7. 3840x2160\n");
        Frame f(3840, 2160, 14.0);
        AddSaturn(f, 2900.0, 400.0, 28.0, 30.0);
        AddNoise(f, 7.0, 71);
        AddHotPixels(f, 2000, 72);
        const auto b = To8(f);
        const TargetFind t = FindBrightTarget(b.data(), 3840, 2160, 8);
        std::printf("   found=%d (%.2f, %.2f)\n", t.found, t.x, t.y);
        CHECK(t.found && Err(t, 2900.0, 400.0) < 1.5,
              "found=%d off by %.2f px", t.found, Err(t, 2900.0, 400.0));
    }

    if (g_fail) { std::printf("target_finder: %d FAILED\n", g_fail); return 1; }
    std::printf("target_finder: all passed\n");
    return 0;
}
