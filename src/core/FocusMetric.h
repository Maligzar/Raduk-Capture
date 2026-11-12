#pragma once
// ============================================================
// FocusMetric.h — what autofocus maximises, on a planet
//
// 2026-11-07, Saturn at gain ~411: the Laplacian-variance sharpness
// autofocus used was flat across a 6000-step sweep (1150-1330) and
// jumped between sweeps (1150 -> 1850) as auto-exposure moved. A pixel
// gradient over a 256 px patch at that gain measures the sensor's noise
// more than the planet's edge, and it scales with brightness. Three
// sweeps followed the noise and the operator had to step in.
//
// The half-flux diameter of the planet's light does not have those
// problems: blur spreads the light, so HFD is smallest at focus; it is
// a flux-weighted sum, so pixel noise averages out; and it is a ratio,
// so exposure cancels. StarMetrics.h already measures it for stars; this
// points it at the target TargetFinder found, with an aperture sized to
// the target's extent so a defocused disc stays inside it.
//
// The score is 1000 / HFD: larger is better, as autofocus expects.
// ============================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "StarMetrics.h"
#include "TargetFinder.h"

namespace raduk::core {

struct FocusMeasure {
    bool  valid  = false;
    float hfdPx  = 0.0f;
    float score  = 0.0f;   // 1000 / hfdPx
    float aperturePx = 0.0f;
    // Phase 133.2: the summed flux above the sky in that aperture. Unlike
    // the peak it does not change with focus, so FadeWatch can tell the
    // sky dimming from a defocus.
    float fluxTotal = 0.0f;
};

inline FocusMeasure MeasurePlanetFocus(const uint8_t* buf, uint32_t w, uint32_t h,
                                       uint8_t bitDepth, const TargetFind& tf,
                                       int cellPx = 8) {
    FocusMeasure out;
    if (!buf || !tf.found || tf.cells <= 0) return out;
    // The target's extent as a radius, from the cells it covers.
    const float radius = std::sqrt((float)tf.cells * (float)(cellPx * cellPx) / 3.14159265f);
    float ap = (std::max)(48.0f, 1.8f * radius + 24.0f);
    // Capped: a full-sensor frame would otherwise sum millions of pixels.
    const float maxAp = (std::min)(400.0f, 0.45f * (float)(std::min)(w, h));
    ap = (std::min)(ap, maxAp);
    const SkyEstimate sky = EstimateSky(buf, w, h, bitDepth, tf.x, tf.y,
                                        ap + 8.0f, ap + 48.0f);
    if (!sky.valid) return out;
    const StarMeasurement m = MeasureStar(buf, w, h, bitDepth, tf.x, tf.y, ap, sky);
    if (!m.valid || !(m.hfdPx > 0.5f)) return out;
    out.valid = true;
    out.hfdPx = m.hfdPx;
    out.score = 1000.0f / m.hfdPx;
    out.aperturePx = ap;
    out.fluxTotal = m.fluxTotal;
    return out;
}

// ============================================================
//  Phase 145.1: an edge metric for the fine pass
//
//  HFD is the right coarse measure (wide capture range, noise and
//  exposure cancel), but on a disc much larger than the blur its half-flux
//  diameter barely moves near focus, and on a frame-filling Moon there is
//  no disc to measure. This is Tenengrad: the energy of the image's
//  gradient, which answers to edge sharpness directly.
//
//  Built from the owner's 2026-11-12 sketch, with six corrections:
//   1. Sobel runs only where the smoothed grid is defined (the sketch
//      read an unwritten zero ring, a false edge round every ROI).
//   2. The gate is in the gradient's own units: k times the noise of
//      Sobel-after-Gaussian, from the measured noise and the composite
//      kernel's L2 norm (the sketch gated at 1.5x the RAW noise, which
//      nearly all noise passes).
//   3. The sum is divided by the fixed number of grid points, not by the
//      count that passed the gate (which makes the score a mean over the
//      edges found, and holds it up as blur removes them).
//   4. Pattern-free: each 2x2 cell's four sites are summed (a luminance
//      superpixel), anchored to even coordinates as Sharpness.h requires,
//      so RGGB and GRBG agree; RAW16 is read at full precision on the
//      8-bit scale (left-aligned, as everywhere in the analyzer).
//   5. No unused gain argument: dividing by the mean squared cancels
//      exposure and gain.
//   6. A caller-owned scratch buffer, so 30 calls a second allocate
//      nothing after the first.
//
//  The noise is measured on the grid itself, robustly: the median of the
//  absolute response of Immerkaer's noise kernel, scaled to a sigma. A
//  median, because edges occupy a minority of a planet's or the Moon's
//  grid; measured here, because the disc's shot noise is what the gate
//  has to clear, and the Moon has no sky to measure it on.
//
//  The ROI is the caller's, and must stay the same size through a sweep:
//  the score is a sum over it, normalised by its size and its mean.
// ============================================================

struct EdgeScratch { std::vector<float> lum, sm, tmp; };

struct EdgeFocus {
    bool  valid   = false;
    float score   = 0.0f;   // gated gradient energy / (points x mean^2)
    float noise   = 0.0f;   // superpixel noise sigma, 8-bit scale x4
    int   active  = 0;      // grid points above the gate
    int   points  = 0;      // grid points scored
};

inline constexpr float kEdgeGateK = 3.0f;

// The L2 norm of Sobel-x convolved with the 3x3 Gaussian: how much one
// unit of independent noise on the grid becomes in Gx.
inline double EdgeKernelNorm() {
    static const double n = [] {
        const double g[3][3] = {{1, 2, 1}, {2, 4, 2}, {1, 2, 1}};
        const double sx[3][3] = {{-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1}};
        double k[5][5] = {};
        for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b)
            for (int c = 0; c < 3; ++c) for (int d = 0; d < 3; ++d)
                k[a + c][b + d] += g[a][b] / 16.0 * sx[c][d];
        double s2 = 0.0;
        for (auto& row : k) for (double v : row) s2 += v * v;
        return std::sqrt(s2);
    }();
    return n;
}

// (cx, cy) and halfPx in raw sensor pixels; bgPerPx the sky level per raw
// pixel on the 8-bit scale (0 when unknown, as on the Moon).
inline EdgeFocus MeasureEdgeFocus(const uint8_t* buf, uint32_t w, uint32_t h, uint8_t bitDepth,
                                  float cx, float cy, int halfPx, float bgPerPx,
                                  EdgeScratch& sc, float gateK = kEdgeGateK) {
    EdgeFocus out;
    if (!buf || w < 16 || h < 16 || halfPx < 8) return out;
    halfPx = (std::min)(halfPx, 400);
    int x0 = (int)std::lround(cx) - halfPx, y0 = (int)std::lround(cy) - halfPx;
    x0 = (std::max)(0, (std::min)(x0, (int)w - 2 * halfPx)) & ~1;
    y0 = (std::max)(0, (std::min)(y0, (int)h - 2 * halfPx)) & ~1;
    const int sw = (std::min)(halfPx, ((int)w - x0) / 2);
    const int sh = (std::min)(halfPx, ((int)h - y0) / 2);
    if (sw < 8 || sh < 8) return out;

    auto px = [&](int x, int y) -> float {
        if (bitDepth == 16)
            return (float)reinterpret_cast<const uint16_t*>(buf)[(size_t)y * w + x] * (1.0f / 256.0f);
        return (float)buf[(size_t)y * w + x];
    };
    const size_t n = (size_t)sw * sh;
    sc.lum.assign(n, 0.0f);
    sc.sm.assign(n, 0.0f);
    double sum = 0.0;
    for (int j = 0; j < sh; ++j)
        for (int i = 0; i < sw; ++i) {
            const int x = x0 + 2 * i, y = y0 + 2 * j;
            const float v = px(x, y) + px(x + 1, y) + px(x, y + 1) + px(x + 1, y + 1);
            sc.lum[(size_t)j * sw + i] = v;
            sum += v;
        }
    const float* L = sc.lum.data();
    float* S = sc.sm.data();

    // Noise: Immerkaer's kernel [1 -2 1; -2 4 -2; 1 -2 1] (L2 norm 6),
    // median absolute response / 0.6745 / 6.
    sc.tmp.clear();
    sc.tmp.reserve((size_t)(sw - 2) * (sh - 2));
    for (int j = 1; j < sh - 1; ++j)
        for (int i = 1; i < sw - 1; ++i) {
            const float* r0 = L + (size_t)(j - 1) * sw + i;
            const float* r1 = r0 + sw;
            const float* r2 = r1 + sw;
            const float v = r0[-1] - 2 * r0[0] + r0[1] - 2 * r1[-1] + 4 * r1[0] - 2 * r1[1]
                          + r2[-1] - 2 * r2[0] + r2[1];
            sc.tmp.push_back(std::fabs(v));
        }
    if (sc.tmp.empty()) return out;
    auto mid = sc.tmp.begin() + (std::ptrdiff_t)(sc.tmp.size() / 2);
    std::nth_element(sc.tmp.begin(), mid, sc.tmp.end());
    const float sigma = (float)(*mid / 0.6745 / 6.0);

    // 3x3 Gaussian, defined on 1..n-2.
    for (int j = 1; j < sh - 1; ++j)
        for (int i = 1; i < sw - 1; ++i) {
            const float* r0 = L + (size_t)(j - 1) * sw + i;
            const float* r1 = r0 + sw;
            const float* r2 = r1 + sw;
            S[(size_t)j * sw + i] = (r0[-1] + 2 * r0[0] + r0[1] + 2 * r1[-1] + 4 * r1[0] + 2 * r1[1]
                                   + r2[-1] + 2 * r2[0] + r2[1]) * (1.0f / 16.0f);
        }

    // Sobel on 2..n-3, where every tap is a smoothed value.
    const double gate = gateK * sigma * EdgeKernelNorm();
    const double gate2 = gate * gate;
    double energy = 0.0;
    int active = 0, pts = 0;
    for (int j = 2; j < sh - 2; ++j)
        for (int i = 2; i < sw - 2; ++i) {
            const float* r0 = S + (size_t)(j - 1) * sw + i;
            const float* r1 = r0 + sw;
            const float* r2 = r1 + sw;
            const double gx = (r0[1] + 2 * r1[1] + r2[1]) - (r0[-1] + 2 * r1[-1] + r2[-1]);
            const double gy = (r2[-1] + 2 * r2[0] + r2[1]) - (r0[-1] + 2 * r0[0] + r0[1]);
            const double g2 = gx * gx + gy * gy;
            ++pts;
            if (g2 > gate2) { energy += g2; ++active; }
        }
    if (pts == 0) return out;
    double mean = sum / (double)n - 4.0 * (double)bgPerPx;
    if (mean < 1.0) mean = 1.0;
    out.valid  = true;
    out.score  = (float)(energy / ((double)pts * mean * mean));
    out.noise  = sigma;
    out.active = active;
    out.points = pts;
    return out;
}

} // namespace raduk::core
