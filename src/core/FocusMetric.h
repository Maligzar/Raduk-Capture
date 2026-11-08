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

} // namespace raduk::core
