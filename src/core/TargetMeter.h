#pragma once
// ============================================================
//  TargetMeter.h -- phase 140.1: meter the planet, not the frame
//
//  Auto-exposure used to steer on the 99.9th percentile of the WHOLE
//  frame, and its clip guard fired when 1.5% of the whole frame was
//  clipped. A planet covers less than that: Saturn's globe is about
//  0.07% of the full sensor at 0.203"/px, Uranus 0.04% to 0.12%. So the
//  percentile landed on the sky beside the planet, AE read "too dark"
//  and added light, and no planet could ever clip enough of the frame to
//  stop it. The owner's audit of 41 captures (Phase140 plan, 140.0)
//  found it in the Uranus files: the whole-frame figure read 148 DN on a
//  disc that was 48% clipped, with gain run up to 528.
//
//  This finds the target in the analyser's own luminance histogram and
//  measures only it:
//    - its peak is the level of the Nth brightest sampled superpixel, so
//      a handful of hot pixels cannot pose as a planet;
//    - its pixels are those above halfway between the sky (the median)
//      and that peak, the same rule the audit used;
//    - its highlight is the 99th percentile of those pixels;
//    - its clipping is the share of those pixels clipped in any channel.
//  With nothing well above the sky, it says no target was found, and
//  auto-exposure holds rather than steering on noise.
//
//  Pure: tests/target_meter.cpp gates it.
// ============================================================

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace raduk::core {

struct TargetMeter {
    bool     found = false;
    std::string why;            // when not found
    float    highlightDN = 0;   // 99th percentile of the target's pixels, 0-255
    float    clipPct = 0;       // % of the target's pixels clipped in any channel
    float    peakDN = 0, skyDN = 0, thresholdDN = 0;
    uint32_t px = 0;            // the target's sampled superpixels
    float    framePct = 0;      // the target's share of the frame
};

// Defaults: the 8th brightest sampled superpixel is the peak (a few hot
// pixels are not a planet; Uranus at full sensor is still a few dozen
// sampled superpixels), and the target must stand 16 DN clear of the sky.
inline TargetMeter MeterTarget(const std::array<uint32_t, 256>& lum,
                               const std::array<uint32_t, 256>& histR,
                               const std::array<uint32_t, 256>& histG,
                               const std::array<uint32_t, 256>& histB,
                               uint32_t count, uint32_t minPx = 8,
                               float minContrastDN = 16.0f, float pctile = 0.99f) {
    TargetMeter m;
    if (count == 0) { m.why = "no frame"; return m; }

    // The sky: the median of all sampled superpixels.
    uint32_t acc = 0;
    int sky = 0;
    for (int i = 0; i < 256; ++i) {
        acc += lum[i];
        if (acc * 2 >= count) { sky = i; break; }
    }
    // The peak: the minPx-th brightest.
    acc = 0;
    int peak = -1;
    for (int i = 255; i >= 0; --i) {
        acc += lum[i];
        if (acc >= minPx) { peak = i; break; }
    }
    m.skyDN = (float)sky;
    if (peak < 0) { m.why = "the frame is too small to measure"; return m; }
    m.peakDN = (float)peak;
    if ((float)(peak - sky) < minContrastDN) {
        m.why = "nothing in the frame stands clear of the sky";
        return m;
    }

    // The target: everything above halfway from sky to peak.
    const int thr = sky + (peak - sky + 1) / 2;
    m.thresholdDN = (float)thr;
    uint32_t n = 0;
    for (int i = thr; i < 256; ++i) n += lum[i];
    if (n < minPx) { m.why = "the target is too small to measure"; return m; }
    m.px = n;
    m.framePct = 100.0f * (float)n / (float)count;

    // Its highlight: the level with (1 - pctile) of its pixels above it.
    const uint32_t want = (std::max)(1u, (uint32_t)((1.0f - pctile) * (float)n));
    acc = 0;
    for (int i = 255; i >= thr; --i) {
        acc += lum[i];
        if (acc >= want) { m.highlightDN = (float)i; break; }
    }

    // Its clipping: the sky never clips, so every clipped superpixel is
    // the target's. The worst channel decides.
    const uint32_t clipped = (std::max)({histR[255], histG[255], histB[255]});
    m.clipPct = (std::min)(100.0f, 100.0f * (float)clipped / (float)n);
    m.found = true;
    return m;
}

}  // namespace raduk::core
