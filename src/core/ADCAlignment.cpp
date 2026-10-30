// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ADCAlignment.h"

#include <algorithm>
#include <cmath>

namespace raduk::core {

namespace {

constexpr double PI = 3.14159265358979323846;

struct Levels { float background; float peak; };

// Background and peak of a plane, from a 256-bin histogram.
//
// Background is the 10th percentile rather than the minimum. A single
// dead-black pixel would drag the minimum to zero and with it every
// level derived from it, which on a frame that is mostly sky is most of
// the frame. The 10th percentile of a planetary frame is sky, which is
// what background ought to mean.
Levels PlaneLevels(const std::vector<float>& p) {
    int hist[256] = {};
    for (float v : p) {
        int b = (int)v;
        hist[b < 0 ? 0 : b > 255 ? 255 : b]++;
    }
    Levels lv{ 0.0f, 0.0f };
    const size_t tenth = p.size() / 10;
    size_t run = 0;
    for (int v = 0; v < 256; v++) {
        run += hist[v];
        if (run >= tenth) { lv.background = (float)v; break; }
    }
    for (int v = 255; v >= 0; v--)
        if (hist[v] > 0) { lv.peak = (float)v; break; }
    return lv;
}

// Fit a parabola through three samples and return the offset of its
// vertex from the middle one, in samples. This is what turns a search
// over whole samples into a sub-sample answer.
double ParabolicVertex(double left, double centre, double right) {
    const double denom = left - 2.0 * centre + right;
    if (std::fabs(denom) < 1e-12) return 0.0;
    const double d = 0.5 * (left - right) / denom;
    return (d > 1.0) ? 1.0 : (d < -1.0) ? -1.0 : d;   // a vertex further out
                                                      // than one sample means
                                                      // the minimum was not
                                                      // really here
}

float MedianOf(std::vector<float>& v) {
    if (v.empty()) return 0.0f;
    const size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    return v[mid];
}

} // namespace

// ── Matching two planes ─────────────────────────────────────────
//
// Finds the offset that best overlays blue on red, NOT the difference
// of their centroids.
//
// A centroid assumes the two images differ only by a displacement,
// which on the targets this rig points at is false. Jupiter's and
// Saturn's belts are red-brown, so red's first moment sits elsewhere on
// the disc than blue's for reasons that have nothing to do with the
// atmosphere. Measured on the emulator's banded Jupiter, which has no
// dispersion whatever, a centroid difference read 0.63 arcsec and
// drifted as the planet rotated -- a tool that says 0.63 when the
// answer is zero would have you dialling a corrector to chase cloud
// belts. Matching the whole 2D structure does not care: the belts are
// in both planes in the same places at different contrast, and the best
// overlap is still the true displacement.
DispersionMeasurement ADCAlignment::MatchPlanes(const std::vector<float>& red,
                                                const std::vector<float>& blue,
                                                int pw, int ph,
                                                float pxPerSample,
                                                float plateScaleArcsecPx) {
    DispersionMeasurement m;
    if (pw < 8 || ph < 8) return m;

    const Levels rl = PlaneLevels(red);
    const Levels bl = PlaneLevels(blue);

    // A plane with no contrast at all -- a blank frame, a closed
    // shutter, a target too faint to show -- has nothing to match.
    if (rl.peak <= rl.background || bl.peak <= bl.background) return m;

    const float rSpan = rl.peak - rl.background;
    const float bSpan = bl.peak - bl.background;
    const float rThr  = rl.background + rSpan * THRESHOLD_FRAC;
    const float bThr  = bl.background + bSpan * THRESHOLD_FRAC;

    // Normalized to each plane's OWN span, so red and blue arrive at the
    // same scale however different their brightness is through the
    // filter in use or the white balance in force.
    std::vector<float> R(red.size()), B(blue.size());
    for (size_t i = 0; i < red.size(); i++) {
        float rv = (red[i]  - rl.background) / rSpan;
        float bv = (blue[i] - bl.background) / bSpan;
        R[i] = rv < 0.0f ? 0.0f : rv > 1.0f ? 1.0f : rv;
        B[i] = bv < 0.0f ? 0.0f : bv > 1.0f ? 1.0f : bv;
    }

    // ── Match on edges, not on brightness ───────────────────────
    //
    // Replacing each plane with its gradient magnitude before matching,
    // for the same reason an experienced imager dials an ADC by
    // watching the LIMB rather than the disc: the limb is in both
    // channels at the same place with the same shape, while the belts
    // are in both at wildly different contrast.
    //
    // Matching brightness directly lets the belts dominate the cost,
    // because a deep belt in red against a shallow one in blue is a
    // large squared difference that shifts position with the offset.
    // Measured on the suite's deliberately extreme banded target --
    // 0.55 belt depth in red against 0.08 in blue, nearly 7:1, with no
    // dispersion at all -- matching brightness invented 0.24 arcsec of
    // residual, which is more than the window is willing to call
    // aligned. On gradients the same target reads near zero.
    //
    // The target is still LOCATED by brightness above: gradient is zero
    // across the middle of a flat disc, so a bounding box taken from it
    // would find the limb and not the planet.
    {
        auto ToEdges = [pw, ph](std::vector<float>& plane) {
            std::vector<float> g(plane.size(), 0.0f);
            float peak = 0.0f;
            for (int y = 1; y < ph - 1; y++) {
                for (int x = 1; x < pw - 1; x++) {
                    const size_t i = (size_t)y * pw + x;
                    const float gx = plane[i + 1]  - plane[i - 1];
                    const float gy = plane[i + pw] - plane[i - pw];
                    const float mag = std::sqrt(gx * gx + gy * gy);
                    g[i] = mag;
                    if (mag > peak) peak = mag;
                }
            }
            // Each to its own peak, so the two planes are comparable
            // whatever their contrast.
            if (peak > 1e-6f)
                for (float& v : g) v /= peak;
            plane.swap(g);
        };
        ToEdges(R);
        ToEdges(B);
    }

    // ── Where the target is ─────────────────────────────────────
    //
    // Bounding box of everything either plane considers signal, so the
    // match runs over the target rather than over a frame that is mostly
    // sky. Sky matches itself perfectly at every offset and would
    // flatten the minimum into uselessness.
    const int search = (std::max)(2, (int)std::lround(SEARCH_PX / pxPerSample));
    int x0 = pw, y0 = ph, x1 = -1, y1 = -1, signal = 0;
    for (int y = 0; y < ph; y++) {
        for (int x = 0; x < pw; x++) {
            const size_t i = (size_t)y * pw + x;
            if (red[i] <= rThr && blue[i] <= bThr) continue;
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
            signal++;
        }
    }
    // Reported in full-resolution pixels, since that is what the window
    // says and what a sensor spec is quoted in.
    m.signalPx = (int)(signal * pxPerSample * pxPerSample);
    if (signal < 16 || m.signalPx < MIN_SIGNAL_PX || x1 < x0 || y1 < y0) return m;

    // Pad so the target can slide within the window by the whole search
    // range without running out of samples to compare.
    x0 = (std::max)(0,      x0 - search);
    y0 = (std::max)(0,      y0 - search);
    x1 = (std::min)(pw - 1, x1 + search);
    y1 = (std::min)(ph - 1, y1 + search);

    const int winW = x1 - x0 + 1;
    const int winH = y1 - y0 + 1;
    if (winW < search * 2 + 6 || winH < search * 2 + 6) return m;

    // Cost of lining blue up with red at a given offset, over a window
    // that can be strided to keep the coarse pass affordable.
    auto Cost = [&](int ox, int oy, int stride) -> double {
        const int ylo = (std::max)(y0 + search, -oy);
        const int yhi = (std::min)(y1 - search, ph - 1 - oy);
        const int xlo = (std::max)(x0 + search, -ox);
        const int xhi = (std::min)(x1 - search, pw - 1 - ox);
        if (yhi <= ylo || xhi <= xlo) return 1e30;
        double sum = 0.0;
        int    n   = 0;
        for (int y = ylo; y <= yhi; y += stride) {
            const float* rRow = &R[(size_t)y * pw];
            const float* bRow = &B[(size_t)(y + oy) * pw];
            for (int x = xlo; x <= xhi; x += stride) {
                const double d = (double)rRow[x] - (double)bRow[x + ox];
                sum += d * d;
                n++;
            }
        }
        return (n > 0) ? sum / n : 1e30;
    };

    // ── Coarse pass ─────────────────────────────────────────────
    //
    // Strided, because this runs per frame while the window is open and
    // an unstrided search over the whole range would cost more than the
    // rest of the frame put together. Precision is restored by the fine
    // pass below, so the stride costs search speed and not answer
    // resolution.
    const int stride = (std::max)(1, ((std::max)(winW, winH) + SUB_TARGET - 1)
                                     / SUB_TARGET);
    int    bestX = 0, bestY = 0;
    double bestCost = 1e30;
    for (int oy = -search; oy <= search; oy++)
        for (int ox = -search; ox <= search; ox++) {
            const double c = Cost(ox, oy, stride);
            if (c < bestCost) { bestCost = c; bestX = ox; bestY = oy; }
        }

    // A minimum pinned to the edge of the search means the real
    // displacement is outside the range, so the answer would be a clamp
    // rather than a measurement.
    if (std::abs(bestX) >= search || std::abs(bestY) >= search) return m;

    // ── Fine pass ───────────────────────────────────────────────
    //
    // Unstrided, over a 5x5 neighbourhood of the coarse answer, then a
    // parabola through the minimum for the sub-sample part. Twenty-five
    // passes instead of the hundreds an unstrided search would need, and
    // it puts the precision near zero, which is where a corrector is
    // being dialled to.
    constexpr int REFINE = 2;
    double grid[2 * REFINE + 1][2 * REFINE + 1];
    int    fx = 0, fy = 0;
    double fbest = 1e30;
    for (int oy = -REFINE; oy <= REFINE; oy++)
        for (int ox = -REFINE; ox <= REFINE; ox++) {
            const double c = Cost(bestX + ox, bestY + oy, 1);
            grid[oy + REFINE][ox + REFINE] = c;
            if (c < fbest) { fbest = c; fx = ox; fy = oy; }
        }

    double dx = bestX, dy = bestY;
    if (std::abs(fx) < REFINE && std::abs(fy) < REFINE && fbest < 1e29) {
        dx = bestX + fx + ParabolicVertex(grid[fy + REFINE][fx + REFINE - 1],
                                          grid[fy + REFINE][fx + REFINE],
                                          grid[fy + REFINE][fx + REFINE + 1]);
        dy = bestY + fy + ParabolicVertex(grid[fy + REFINE - 1][fx + REFINE],
                                          grid[fy + REFINE][fx + REFINE],
                                          grid[fy + REFINE + 1][fx + REFINE]);
    }

    // The cost compared R at (x,y) against B at (x+ox, y+oy), so the
    // offset that matched is where blue's copy of a feature sits
    // relative to red's -- blue's displacement itself, not its negative.
    m.dxPx = (float)(dx * pxPerSample);
    m.dyPx = (float)(dy * pxPerSample);
    m.magPx     = std::sqrt(m.dxPx * m.dxPx + m.dyPx * m.dyPx);
    m.magArcsec = m.magPx * plateScaleArcsecPx;

    // Compass from frame up. Frame y grows downward, so "up the frame"
    // is -dy; atan2(dx, -dy) gives 0 at up, increasing clockwise.
    m.angleDeg = (float)(std::atan2((double)m.dxPx, (double)-m.dyPx) * 180.0 / PI);
    if (m.angleDeg < 0.0f) m.angleDeg += 360.0f;

    m.valid = true;
    return m;
}

DispersionMeasurement ADCAlignment::MeasureBayer(const uint8_t* raw, int w, int h,
                                                 int bitDepth,
                                                 float plateScaleArcsecPx) {
    DispersionMeasurement m;
    if (!raw || w < 16 || h < 16) return m;

    const int pw = w / 2, ph = h / 2;
    std::vector<float> red((size_t)pw * ph), blue((size_t)pw * ph);

    // RGGB: red at (even, even), blue at (odd, odd). Each becomes a
    // half-resolution plane of its own.
    if (bitDepth == 16) {
        const uint16_t* r16 = reinterpret_cast<const uint16_t*>(raw);
        for (int j = 0; j < ph; j++)
            for (int i = 0; i < pw; i++) {
                // >> 8, not >> 4: ASI RAW16 is left-aligned in the word.
                red [(size_t)j * pw + i] = (float)(r16[(size_t)(2*j)   * w + 2*i]     >> 8);
                blue[(size_t)j * pw + i] = (float)(r16[(size_t)(2*j+1) * w + 2*i + 1] >> 8);
            }
    } else {
        for (int j = 0; j < ph; j++)
            for (int i = 0; i < pw; i++) {
                red [(size_t)j * pw + i] = (float)raw[(size_t)(2*j)   * w + 2*i];
                blue[(size_t)j * pw + i] = (float)raw[(size_t)(2*j+1) * w + 2*i + 1];
            }
    }

    m = MatchPlanes(red, blue, pw, ph, 2.0f, plateScaleArcsecPx);
    if (!m.valid) return m;

    // ── Undo the Bayer site offset ──────────────────────────────
    //
    // This is the whole reason the measurement reads the raw frame
    // rather than the debayered preview buffer.
    //
    // Red and blue are not sampled at the same place on the sensor. In
    // RGGB, red sits at (2i, 2j) and blue at (2i+1, 2j+1) -- one pixel
    // right and one pixel down. A feature at full-resolution (X, Y)
    // therefore lands at index X/2 in the red plane and at index
    // (X-1)/2 in the blue one, so blue reads half a sample low in both
    // axes, which is one full-resolution pixel. Add it back.
    //
    // The preview's debayer makes this worse rather than better: it is a
    // 2x2 superpixel debayer that writes red from the block's top-left
    // and blue from its bottom-right into the same block, so the offset
    // survives into m_rgbBuffer with the sampling grid hidden. Measured
    // on the emulator's dispersion-free Jupiter, measuring the
    // debayered buffer read 1.70 px of residual that no corrector could
    // ever remove, pointing up-left, which is exactly this.
    m.dxPx += 1.0f;
    m.dyPx += 1.0f;
    m.magPx     = std::sqrt(m.dxPx * m.dxPx + m.dyPx * m.dyPx);
    m.magArcsec = m.magPx * plateScaleArcsecPx;
    m.angleDeg  = (float)(std::atan2((double)m.dxPx, (double)-m.dyPx) * 180.0 / PI);
    if (m.angleDeg < 0.0f) m.angleDeg += 360.0f;
    return m;
}

void ADCAlignment::Push(const DispersionMeasurement& m) {
    // Invalid frames are dropped rather than stored. A frame the target
    // drifted out of should not pull the median toward zero and read as
    // a corrector that just came into alignment.
    if (!m.valid) return;
    m_history.push_back(m);
    if ((int)m_history.size() > WINDOW)
        m_history.erase(m_history.begin());
}

void ADCAlignment::Reset() { m_history.clear(); }

DispersionMeasurement ADCAlignment::Smoothed(float plateScaleArcsecPx) const {
    DispersionMeasurement out;
    if (m_history.empty()) return out;

    std::vector<float> xs, ys;
    xs.reserve(m_history.size());
    ys.reserve(m_history.size());
    int minSignal = m_history.front().signalPx;
    for (const DispersionMeasurement& s : m_history) {
        xs.push_back(s.dxPx);
        ys.push_back(s.dyPx);
        minSignal = (std::min)(minSignal, s.signalPx);
    }

    // Median each component separately, then derive magnitude and angle
    // from the medians. Taking the median of the magnitudes instead
    // would be wrong: magnitude is non-negative, so seeing scatter about
    // a true zero gives a median well above zero, and a corrector that
    // IS aligned would never read as aligned.
    out.dxPx = MedianOf(xs);
    out.dyPx = MedianOf(ys);
    out.magPx = std::sqrt(out.dxPx * out.dxPx + out.dyPx * out.dyPx);
    out.magArcsec = out.magPx * plateScaleArcsecPx;
    out.angleDeg = (float)(std::atan2((double)out.dxPx, (double)-out.dyPx) * 180.0 / PI);
    if (out.angleDeg < 0.0f) out.angleDeg += 360.0f;
    out.signalPx = minSignal;
    out.valid = true;
    return out;
}

} // namespace raduk::core
