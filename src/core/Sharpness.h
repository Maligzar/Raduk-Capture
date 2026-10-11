#pragma once
// ============================================================
// Sharpness.h — Phase 76
//
// One definition of "how sharp is this raw frame", shared by the live
// analyser and the SER writer's per-frame quality sidecar.
//
// It exists because those two had drifted into measuring different
// things. FrameAnalyzer's kernel was fixed in v1.2.0 — it had been
// sampling neighbouring pixels on a Bayer mosaic, which belong to
// DIFFERENT colour channels, so it computed a red-minus-green colour
// difference and barely responded to focus at all. Nothing stops that
// happening again to a second copy, so there is now only one copy.
//
// Two rules the Bayer layout imposes, and both were got wrong before:
//
//   1. Taps must be +/-2 pixels, not +/-1. On an RGGB mosaic the
//      immediate neighbours of a red pixel are all green, so a +/-1
//      kernel measures the colour difference and the mosaic pattern
//      rather than image detail.
//   2. The sampling lattice must be anchored to EVEN coordinates. The
//      crop origin follows the planet, so an odd start flipped which
//      colour channel was measured every time the planet moved one
//      pixel, and the score jumped between two unrelated values.
// ============================================================

#include <cstdint>

namespace raduk::core {

// Laplacian variance over a crop centred on (cx, cy), in raw pixels.
// Returns the variance itself, NOT a percentage — callers normalise,
// because the live gauge and the recorded sidecar want different scales.
// Returns 0 when the crop is too small to measure.
inline double BayerLaplacianVariance(const uint8_t* buf,
                                     uint32_t w, uint32_t h, uint8_t bitDepth,
                                     uint32_t cx, uint32_t cy,
                                     uint32_t halfCrop = 128,
                                     double* outRaw = nullptr,
                                     double* outNoise = nullptr) {
    if (!buf || w < 16 || h < 16) return 0.0;

    const uint32_t r = halfCrop;
    uint32_t x0 = (cx > r) ? cx - r : 0;
    uint32_t y0 = (cy > r) ? cy - r : 0;
    uint32_t x1 = (cx + r < w - 1) ? cx + r : w - 1;
    uint32_t y1 = (cy + r < h - 1) ? cy + r : h - 1;
    if (x1 < x0 + 8 || y1 < y0 + 8) return 0.0;

    auto px8 = [&](uint32_t x, uint32_t y) -> float {
        if (bitDepth == 8) return (float)buf[(size_t)y * w + x];
        // ASI RAW16 is left-aligned in the word, so >> 8, not >> 4.
        return (float)(reinterpret_cast<const uint16_t*>(buf)[(size_t)y * w + x] >> 8);
    };

    const uint32_t sx = (x0 + 2) & ~1u;   // even anchor — see note above
    const uint32_t sy = (y0 + 2) & ~1u;

    // Pass 1: level histogram, to find the sky so noise can be measured
    // on it. Exact, since px8 returns a 0-255 value either way.
    uint32_t hist[256] = {};
    uint32_t hn = 0;
    for (uint32_t y = sy; y + 2 < y1; y += 2)
        for (uint32_t x = sx; x + 2 < x1; x += 2) {
            hist[(int)px8(x, y)]++; hn++;
        }
    if (hn < 64) return 0.0;
    auto pct = [&](double f) -> double {
        uint32_t want = (uint32_t)(f * hn), acc = 0;
        for (int b = 0; b < 256; b++) { acc += hist[b]; if (acc >= want) return b; }
        return 255.0;
    };
    const double bg    = pct(0.10);
    const double pk    = pct(0.995);
    const double bgCut = bg + 0.10 * (pk - bg);

    // Pass 2: Laplacian variance over the crop, and separately the
    // pixel-to-pixel variance of the sky, which is pure noise.
    double sum = 0.0, sumSq = 0.0;
    uint32_t n = 0, nnz = 0;
    static thread_local uint32_t lapHist[1024];
    for (int i = 0; i < 1024; i++) lapHist[i] = 0;
    for (uint32_t y = sy; y + 2 < y1; y += 2) {
        for (uint32_t x = sx; x + 2 < x1; x += 2) {
            const float c = px8(x, y);
            const float lap = 4.0f * c
                            - px8(x - 2, y) - px8(x + 2, y)
                            - px8(x, y - 2) - px8(x, y + 2);
            sum   += lap;
            sumSq += (double)lap * lap;
            n++;
            if (c < bgCut) {
                // Histogram |lap| on sky pixels: the SAME kernel on signal-
                // free data, so its own response to noise is measured rather
                // than derived. A derived coefficient (20*sigma^2 for this
                // kernel) proved fragile - the emulator's truncating integer
                // noise does not match it, and it has to be re-derived if the
                // kernel ever changes.
                int a = (int)(lap < 0 ? -lap : lap);
                if (a > 1023) a = 1023;
                lapHist[a]++; nnz++;
            }
        }
    }
    if (n < 2) return 0.0;
    const double mean = sum / n;
    double var = (sumSq / n) - (mean * mean);

    // Subtract the noise floor, or this measures the sensor rather than
    // the sky. A Laplacian is a high-pass filter, so broadband noise
    // dominates it: with the kernel {4,-1,-1,-1,-1} independent per-pixel
    // noise of variance s2 contributes 16+4 = 20*s2 all by itself, and
    // the difference of two sky pixels has variance 2*s2.
    //
    // Measured, and the reason this is here: at +/-10 DN of sensor noise
    // the raw variance pinned the gauge at 100% across three consecutive
    // autofocus samples. The peak became a plateau with no curvature to
    // fit, and the result went from 19 steps off true focus to 500.
    // THE SKY-NOISE IDEA DOES NOT WORK HERE, and the reason is worth
    // keeping so nobody tries it a fourth time.
    //
    // The crop is 256x256 centred on the planet. A planetary target
    // routinely FILLS that - the emulated disc is 422 px across - so
    // there is no sky inside the crop at all. What gets measured as
    // 'background' is then the darkest decile of the DISC, whose
    // gradients are real surface structure, and subtracting it removes
    // signal rather than noise.
    //
    // Measured, frozen scene, properly exposed to 200 DN peak:
    //
    //   noise  0 DN   raw   32.5   floor   35.2   net    -2.7
    //   noise  6 DN   raw  216.2   floor  266.0   net   -49.7
    //   noise 14 DN   raw 1201.8   floor 1723.3   net  -521.4
    //
    // The zero-noise row is the proof: a frame with no noise whatsoever
    // still yields a 'noise floor' larger than its entire variance.
    // Clipping was the earlier suspicion and it was wrong - exposing
    // the disc properly changed nothing.
    //
    // The right method for a target that fills the frame is TEMPORAL:
    // noise is what differs between consecutive frames at the same
    // pixel, while structure does not. That needs frame history, which
    // this function does not have, so it belongs in FrameAnalyzer if it
    // is ever wanted.
    //
    // Until then the figure below is reported as a DIAGNOSTIC only and
    // never subtracted. It is the darkest-decile gradient level, not a
    // noise floor, and the [SHARP] log line names it 'noise' only
    // because that is what it was built to estimate.
    // subtracted. Two attempts at subtracting it both over-corrected and
    // drove every frame to zero, and the reason is worth recording: the
    // bright planet clips, so noise is suppressed there but not on the
    // sky, and a sky-derived floor is therefore too high for the crop as
    // a whole. Judging whether that is an emulator artefact (its
    // synthetic disc sits near 255) or real needs a properly exposed
    // frame, so this stays a diagnostic until there is on-sky data.
    //
    // Not subtracting is defensible because the raw metric already
    // separates focus well even with noise present - measured 2% to
    // >100% across a sweep. The real problem was the CEILING, not the
    // noise floor.
    double noiseTerm = 0.0;
    if (nnz > 64) {
        uint32_t half = nnz / 2, acc = 0; int med = 0;
        for (int i = 0; i < 1024; i++) {
            acc += lapHist[i];
            if (acc >= half) { med = i; break; }
        }
        // For zero-mean Gaussian x, median|x| = 0.6745*sigma.
        const double sigLap = (double)med / 0.6745;
        noiseTerm = sigLap * sigLap;
    }
    if (outRaw)   *outRaw   = (sumSq / n) - (mean * mean);
    if (outNoise) *outNoise = noiseTerm;
    return var > 0.0 ? var : 0.0;
}

// Live-gauge scaling.
//
// 8000 was tuned against the broken colour-difference kernel. 100 was
// then calibrated on a NOISELESS emulator frame, where at-focus variance
// is about 45 - and that was the mistake: switch on 10 DN of sensor
// noise, which any real camera has, and at-focus variance is about 610.
// The gauge pinned at 100% across three consecutive autofocus samples,
// the peak became a plateau with no curvature to fit, and the result
// went from 19 steps off true focus to 500.
//
// 1000 leaves headroom for a noisy real frame while keeping a clean one
// on scale. Still worth one on-sky check: if readings sit near the top
// raise it, and if they crowd the bottom lower it. Only the SHAPE of the
// curve matters to autofocus - the ceiling just has to not clip it.
inline constexpr double SHARP_CEIL = 1000.0;

inline float SharpnessPercent(double variance) {
    const double pct = variance / SHARP_CEIL * 100.0;
    return (float)(pct < 100.0 ? pct : 100.0);
}

} // namespace raduk::core
