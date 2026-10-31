#pragma once
// ============================================================
// StarMetrics.h - how far out of focus a STAR is (phase 95)
//
// The project has one focus metric, Sharpness.h's Laplacian
// variance, and it is built for a planetary disc: blur a disc and
// only its limb moves, so the variance responds to the one
// feature that changes. On a star it responds to almost nothing,
// because a point source has no edge -- it has a profile.
//
// That is the gap roadmap row 31/32 names, and the comparison
// found it is the gap against every competitor: SharpCap ships
// multi-star FWHM, HFD and Bahtinov aids, FireCapture has an
// autofocus plugin. Nothing in this tree measured a star at all.
//
// ── HFD first, FWHM second, and that order is deliberate ────
//
// HALF FLUX DIAMETER is the diameter of the circle containing
// half the star's light. FULL WIDTH AT HALF MAXIMUM is the width
// of the profile at half its peak. Near focus they agree -- for a
// Gaussian they are identical, both 2.3548 sigma, which is the
// anchor tests/star_metrics.cpp checks against.
//
// They part company exactly where a focus routine needs them to
// work. Defocus an obstructed aperture and the star becomes an
// annulus: the peak is no longer at the centre, so "half the
// peak" is crossed four times along any line and FWHM stops
// meaning anything. HFD does not care where the light is, only
// how spread out it is, so it keeps growing smoothly and
// monotonically all the way out.
//
// So HFD is the number to drive an autofocus sweep with, and
// FWHM is the number to report when the star is near focus and
// an observer wants a figure they recognise. FWHM is marked
// invalid rather than guessed when the profile is not monotonic.
//
// ── Superpixels, not taps ───────────────────────────────────
//
// Sharpness.h learned the hard way that a Bayer mosaic punishes
// naive neighbourhoods: its kernel must reach +/-2 and its
// lattice must be anchored to even coordinates, because on RGGB
// the immediate neighbours of a red pixel are all green, and an
// odd anchor flips which channel is measured when the crop moves.
//
// A radial profile cannot use that trick -- it samples every
// direction at every distance. So this sums each 2x2 quad into
// one superpixel instead. A quad is one complete RGGB set, so a
// superpixel is colour-neutral by construction, there is no
// parity to get wrong, and the mosaic cannot leak into the
// profile at all. The cost is half the spatial resolution, which
// for a pattern 3 px across at this plate scale is the difference
// between 3.06 px and 1.53 superpixels: still sampled.
//
// Everything reported is converted back to RAW SENSOR PIXELS, so
// it can be multiplied by the plate scale without anyone having
// to remember this.
// ============================================================

#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

namespace raduk::core {

struct StarMeasurement {
    bool        valid   = false;
    const char* why     = "";       // why not, when !valid

    float cx = 0.0f, cy = 0.0f;     // centroid, raw sensor pixels
    float background = 0.0f;        // sky level, ADU
    float peak       = 0.0f;        // brightest superpixel above sky
    float fluxTotal  = 0.0f;        // summed ADU above sky in the aperture

    float hfdPx  = 0.0f;            // half flux diameter, raw sensor px
    float fwhmPx = 0.0f;            // 0 when fwhmValid is false
    bool  fwhmValid = false;

    float snr = 0.0f;               // peak over sky noise
    int   saturated = 0;            // superpixels at or above the clip
};

// ── Reading a raw frame as colour-neutral superpixels ───────
//
// One superpixel is the sum of a 2x2 quad, so values run 0..1020
// for 8-bit input. Returned as float because every consumer here
// is doing arithmetic on it anyway.
inline float Superpixel(const uint8_t* buf, uint32_t w, uint8_t bitDepth,
                        uint32_t sx, uint32_t sy) {
    auto px = [&](uint32_t x, uint32_t y) -> float {
        if (bitDepth == 8) return (float)buf[(size_t)y * w + x];
        // ASI RAW16 is left-aligned in the word, so >> 8 (see Sharpness.h).
        return (float)(reinterpret_cast<const uint16_t*>(buf)[(size_t)y * w + x] >> 8);
    };
    const uint32_t x = sx * 2, y = sy * 2;
    return px(x, y) + px(x + 1, y) + px(x, y + 1) + px(x + 1, y + 1);
}

// Saturation, in superpixel units: four clipped 8-bit pixels.
inline constexpr float SUPERPIXEL_CLIP = 4.0f * 255.0f;

// ── The sky, measured in an annulus and not globally ────────
//
// A global estimate is wrong the moment anything else is in
// frame, and on this rig something usually is: a planet, or the
// Moon filling the whole sensor. The annulus has to sit outside
// the star's wings but inside the frame, so both radii are the
// caller's to choose.
//
// The MEDIAN, not the mean. A second star in the annulus, a hot
// pixel or a cosmic ray moves a mean and does not move a median,
// and the sky level feeds straight into both metrics.
struct SkyEstimate {
    bool  valid = false;
    float level = 0.0f;
    float noise = 0.0f;     // robust sigma, from the MAD
};

inline SkyEstimate EstimateSky(const uint8_t* buf, uint32_t w, uint32_t h,
                               uint8_t bitDepth,
                               float cxPx, float cyPx,
                               float rInnerPx, float rOuterPx) {
    SkyEstimate out;
    if (!buf || w < 8 || h < 8) { return out; }
    const uint32_t sw = w / 2, sh = h / 2;
    const float cx = cxPx * 0.5f, cy = cyPx * 0.5f;
    const float r0 = rInnerPx * 0.5f, r1 = rOuterPx * 0.5f;
    if (!(r1 > r0) || !(r0 >= 0.0f)) return out;

    std::vector<float> ring;
    ring.reserve(256);
    const int y0 = (int)std::floor(cy - r1), y1 = (int)std::ceil(cy + r1);
    const int x0 = (int)std::floor(cx - r1), x1 = (int)std::ceil(cx + r1);
    for (int sy = y0; sy <= y1; ++sy) {
        if (sy < 0 || sy >= (int)sh) continue;
        for (int sx = x0; sx <= x1; ++sx) {
            if (sx < 0 || sx >= (int)sw) continue;
            const float dx = sx - cx, dy = sy - cy;
            const float r = std::sqrt(dx*dx + dy*dy);
            if (r < r0 || r > r1) continue;
            ring.push_back(Superpixel(buf, w, bitDepth, (uint32_t)sx, (uint32_t)sy));
        }
    }
    // Too few samples and the median is not a median. 24 is about one
    // ring of superpixels at the smallest radius worth using.
    if (ring.size() < 24) return out;

    std::sort(ring.begin(), ring.end());
    const float med = ring[ring.size() / 2];

    // Median absolute deviation, scaled to a Gaussian sigma. The
    // 1.4826 is the standard consistency factor.
    std::vector<float> dev;
    dev.reserve(ring.size());
    for (float v : ring) dev.push_back(std::fabs(v - med));
    std::sort(dev.begin(), dev.end());
    out.level = med;
    out.noise = dev[dev.size() / 2] * 1.4826f;
    out.valid = true;
    return out;
}

// ── Finding the star ────────────────────────────────────────
//
// The brightest superpixel, which is the right answer for the one
// case this is for: a deliberately chosen star on an otherwise
// empty field. It is the WRONG answer on a field with a planet in
// it, and deliberately so -- a planet is brighter than any star
// and the caller should not be asking this question then.
//
// Returns false rather than a guess when nothing stands above the
// sky by the margin asked for.
inline bool FindBrightestStar(const uint8_t* buf, uint32_t w, uint32_t h,
                              uint8_t bitDepth,
                              float skyLevel, float minAboveSky,
                              float& outCxPx, float& outCyPx) {
    if (!buf || w < 8 || h < 8) return false;
    const uint32_t sw = w / 2, sh = h / 2;
    float best = -1.0f;
    uint32_t bx = 0, by = 0;
    for (uint32_t sy = 0; sy < sh; ++sy)
        for (uint32_t sx = 0; sx < sw; ++sx) {
            const float v = Superpixel(buf, w, bitDepth, sx, sy);
            if (v > best) { best = v; bx = sx; by = sy; }
        }
    if (best - skyLevel < minAboveSky) return false;
    outCxPx = (float)bx * 2.0f + 0.5f;    // superpixel centre, raw px
    outCyPx = (float)by * 2.0f + 0.5f;
    return true;
}

// ── The measurement ─────────────────────────────────────────
//
// aperturePx bounds everything: the centroid, the flux and both
// metrics. It has to be wide enough to hold the wings at the
// worst defocus the caller cares about, because HFD counts flux
// and flux outside the aperture is flux that does not count.
//
// A too-small aperture does not fail, it UNDER-reports, which is
// the dangerous direction: an autofocus sweep would see HFD stop
// growing and read that as approaching focus. The caller is told
// how much flux sat in the outermost ring (edgeFraction) so it
// can notice.
inline StarMeasurement MeasureStar(const uint8_t* buf, uint32_t w, uint32_t h,
                                   uint8_t bitDepth,
                                   float cxPx, float cyPx,
                                   float aperturePx,
                                   const SkyEstimate& sky,
                                   float* edgeFraction = nullptr) {
    StarMeasurement m;
    if (edgeFraction) *edgeFraction = 0.0f;
    if (!buf || w < 8 || h < 8)      { m.why = "frame too small"; return m; }
    if (!sky.valid)                  { m.why = "no sky estimate";  return m; }
    if (!(aperturePx >= 4.0f))       { m.why = "aperture too small"; return m; }

    const uint32_t sw = w / 2, sh = h / 2;
    const float cx = cxPx * 0.5f, cy = cyPx * 0.5f;
    const float R  = aperturePx * 0.5f;         // superpixels

    // Pass 1: flux-weighted centroid, peak, saturation.
    double wsum = 0.0, wx = 0.0, wy = 0.0;
    float  peak = 0.0f;
    int    sat = 0, used = 0;
    const int y0 = (int)std::floor(cy - R), y1 = (int)std::ceil(cy + R);
    const int x0 = (int)std::floor(cx - R), x1 = (int)std::ceil(cx + R);
    for (int sy = y0; sy <= y1; ++sy) {
        if (sy < 0 || sy >= (int)sh) continue;
        for (int sx = x0; sx <= x1; ++sx) {
            if (sx < 0 || sx >= (int)sw) continue;
            const float dx = sx - cx, dy = sy - cy;
            if (dx*dx + dy*dy > R*R) continue;
            const float raw = Superpixel(buf, w, bitDepth, (uint32_t)sx, (uint32_t)sy);
            if (raw >= SUPERPIXEL_CLIP - 0.5f) ++sat;
            const float v = raw - sky.level;
            ++used;
            if (v > peak) peak = v;
            // Negative excursions are sky noise, not light. Including
            // them would drag the centroid toward whichever side of
            // the sky happened to read low.
            if (v <= 0.0f) continue;
            wsum += v; wx += v * sx; wy += v * sy;
        }
    }
    if (used < 16)        { m.why = "aperture mostly off-frame"; return m; }
    if (!(wsum > 0.0))    { m.why = "nothing above the sky";     return m; }

    const float ccx = (float)(wx / wsum), ccy = (float)(wy / wsum);

    // Pass 2: the radial cumulative flux profile, about the CENTROID
    // rather than the brightest pixel. On a defocused annulus the
    // brightest pixel is on the ring and a profile about it is
    // meaningless; the centroid stays in the middle where it belongs.
    const int NB = 128;
    std::vector<double> ringFlux((size_t)NB, 0.0);
    std::vector<double> ringPeak((size_t)NB, 0.0);
    std::vector<int>    ringN((size_t)NB, 0);
    const double binW = R / (double)NB;
    double total = 0.0;
    for (int sy = y0; sy <= y1; ++sy) {
        if (sy < 0 || sy >= (int)sh) continue;
        for (int sx = x0; sx <= x1; ++sx) {
            if (sx < 0 || sx >= (int)sw) continue;
            const float dx = sx - ccx, dy = sy - ccy;
            const double r = std::sqrt((double)dx*dx + (double)dy*dy);
            if (r > R) continue;
            const float v = Superpixel(buf, w, bitDepth, (uint32_t)sx, (uint32_t)sy)
                          - sky.level;
            if (v <= 0.0f) continue;
            int b = (int)(r / binW);
            if (b < 0) b = 0; if (b >= NB) b = NB - 1;
            ringFlux[(size_t)b] += v;
            ringN[(size_t)b]    += 1;
            if (v > ringPeak[(size_t)b]) ringPeak[(size_t)b] = v;
            total += v;
        }
    }
    if (!(total > 0.0)) { m.why = "no flux above the sky"; return m; }

    // ── Isolated bright pixels are not starlight ────────────
    //
    // A hot pixel, a cosmic ray or a cosmetic sprinkle is a single
    // bright sample with no profile around it. It carries real ADU
    // and, spread across a wide aperture, it can carry as much as the
    // star: found end to end on emulator frames, whose background
    // sprinkle held HFD flat at about 30 px at EVERY focuser
    // position, so the metric did not respond to defocus at all.
    //
    // A star's light is smooth at the scale of a ring, so a sample
    // many times its own ring's mean is not part of it. Rejecting
    // those is ordinary aperture photometry, and it is needed on a
    // real sensor for hot pixels whatever the emulator does.
    //
    // A second pass rather than a per-ring median: the rings are
    // already summed, so the mean is free, and storing every sample
    // to take 128 medians would cost more than the pass.
    {
        const double rejectAbove = 6.0;   // times the ring mean
        for (int sy = y0; sy <= y1; ++sy) {
            if (sy < 0 || sy >= (int)sh) continue;
            for (int sx = x0; sx <= x1; ++sx) {
                if (sx < 0 || sx >= (int)sw) continue;
                const float dx = sx - ccx, dy = sy - ccy;
                const double r = std::sqrt((double)dx*dx + (double)dy*dy);
                if (r > R) continue;
                const float v = Superpixel(buf, w, bitDepth,
                                           (uint32_t)sx, (uint32_t)sy) - sky.level;
                if (v <= 0.0f) continue;
                int b = (int)(r / binW);
                if (b < 0) b = 0; if (b >= NB) b = NB - 1;
                const int n = ringN[(size_t)b];
                if (n < 4) continue;      // too few to have a mean worth using
                const double mean = ringFlux[(size_t)b] / (double)n;
                if (mean <= 0.0) continue;
                if ((double)v > mean * rejectAbove) {
                    ringFlux[(size_t)b] -= v;
                    ringN[(size_t)b]    -= 1;
                    total               -= v;
                    // The ring peak has to drop with it, or the FWHM
                    // search would still be chasing the rejected pixel.
                    if ((double)v >= ringPeak[(size_t)b])
                        ringPeak[(size_t)b] = mean;
                }
            }
        }
        if (!(total > 0.0)) { m.why = "only outliers above the sky"; return m; }
    }

    // ── Where the star stops ────────────────────────────────
    //
    // Integrating the whole aperture integrates whatever else is in
    // it, and something always is: another star, a hot pixel, a
    // cosmic ray. Found end to end on an emulator frame, whose
    // background carries a sparse sprinkle of bright pixels -- a
    // focused star measured HFD 169 px in a 128 px aperture, because
    // the scattered sprinkle carried more total flux than the star
    // and dragged the half-flux radius out to the aperture wall.
    //
    // So the star's own extent is found first: walk the ring MEANS
    // outward and stop where the profile sinks into the sky. Past
    // that radius there is no star left to measure, only field.
    //
    // The ring mean, not the ring peak: one bright pixel in a ring is
    // a hot pixel or a neighbour, and a mean over a whole annulus
    // dilutes it while a peak would be fooled by it.
    //
    // Two consecutive rings below the threshold, not one, so a single
    // noisy ring cannot truncate a real wing.
    const double skyBand = (sky.noise > 0.0f) ? (double)sky.noise : 1.0;
    int edgeBin = NB - 1;
    {
        int below = 0;
        for (int b = 1; b < NB; ++b) {
            if (ringN[(size_t)b] == 0) continue;
            const double mean = ringFlux[(size_t)b] / (double)ringN[(size_t)b];
            if (mean <= skyBand) {
                if (++below >= 2) { edgeBin = b; break; }
            } else {
                below = 0;
            }
        }
    }

    // Re-total over the star only. Everything downstream -- the half
    // flux radius, the edge fraction -- uses this.
    double starTotal = 0.0;
    for (int b = 0; b <= edgeBin && b < NB; ++b) starTotal += ringFlux[(size_t)b];
    if (!(starTotal > 0.0)) { m.why = "star does not rise above the sky"; return m; }
    total = starTotal;

    // Flux in the outermost eighth of the STAR's extent. A high value
    // means the profile had not finished falling where it was cut off,
    // which is the aperture being too small -- the failure that makes
    // HFD stop growing and fools a sweep into reading focus.
    if (edgeFraction) {
        double edge = 0.0;
        const int from = edgeBin - (edgeBin / 8);
        for (int b = from; b <= edgeBin && b < NB; ++b) edge += ringFlux[(size_t)b];
        *edgeFraction = (float)(edge / total);
    }

    // HFD: the diameter containing half the flux. Interpolated within
    // the bin it lands in, because the bins are coarse next to a
    // pattern a few pixels across and stepping to a bin edge would
    // quantise the metric an autofocus fit depends on.
    double run = 0.0, hfdR = ((double)edgeBin + 1.0) * binW;
    for (int b = 0; b <= edgeBin && b < NB; ++b) {
        const double before = run;
        run += ringFlux[(size_t)b];
        if (run >= total * 0.5) {
            const double need = total * 0.5 - before;
            const double frac = (ringFlux[(size_t)b] > 0.0)
                              ? need / ringFlux[(size_t)b] : 0.0;
            hfdR = ((double)b + frac) * binW;
            break;
        }
    }
    m.hfdPx = (float)(2.0 * hfdR * 2.0);        // radius -> dia, superpx -> px

    // FWHM: where the radial profile falls to half its peak. Only
    // meaningful while the profile DECREASES from the centre; a
    // defocused annulus rises first, and a half-maximum crossing on
    // that is an arbitrary one of four.
    double innerPeak = 0.0;
    for (int b = 0; b < NB / 8; ++b)
        if (ringPeak[(size_t)b] > innerPeak) innerPeak = ringPeak[(size_t)b];
    double outerMax = 0.0;
    for (int b = 0; b < NB; ++b)
        if (ringPeak[(size_t)b] > outerMax) outerMax = ringPeak[(size_t)b];

    // Is the CENTRE the brightest part? That is the question, and it
    // has to be asked about the star rather than about the aperture.
    //
    // A first attempt tested whether the peak bin fell in the inner
    // eighth of the aperture, which is an aperture-relative threshold
    // for a star-relative property: a tight annulus of radius 4
    // superpixels inside a 45 superpixel aperture sat inside that
    // eighth and was wrongly judged centre-peaked, so FWHM was
    // reported for a donut -- the exact case this guard exists to
    // catch.
    //
    // Comparing the centre to the brightest point is independent of
    // both -- but "the centre" cannot be bin zero, which spans less
    // than one superpixel and is often empty, so an exactly correct
    // test on a Gaussian refused every star.
    //
    // The centre is therefore a region scaled by the star's own
    // measured size, floored at one superpixel so it always holds a
    // sample. For a Gaussian that region sits at 94 per cent of the
    // peak; inside any annulus it sits in the dark hole.
    double centreR = hfdR * 0.3;
    if (centreR < 1.0) centreR = 1.0;
    double centrePeak = 0.0;
    for (int b = 0; b < NB; ++b) {
        if (((double)b + 0.5) * binW > centreR) break;
        if (ringPeak[(size_t)b] > centrePeak) centrePeak = ringPeak[(size_t)b];
    }
    const bool centrePeaked = (innerPeak > 0.0)
                           && (centrePeak >= outerMax * 0.7);
    if (centrePeaked) {
        const double half = innerPeak * 0.5;
        double crossR = -1.0;
        for (int b = 0; b < NB; ++b) {
            if (ringN[(size_t)b] == 0) continue;
            if (ringPeak[(size_t)b] <= half) {
                // Linear between this bin and the last populated one.
                int pb = b - 1;
                while (pb >= 0 && ringN[(size_t)pb] == 0) --pb;
                if (pb < 0) { crossR = (double)b * binW; break; }
                const double a = ringPeak[(size_t)pb], c = ringPeak[(size_t)b];
                const double t = (a > c) ? (a - half) / (a - c) : 0.0;
                crossR = ((double)pb + t) * binW;
                break;
            }
        }
        if (crossR > 0.0) {
            m.fwhmPx    = (float)(2.0 * crossR * 2.0);
            m.fwhmValid = true;
        }
    }

    m.valid      = true;
    m.cx         = ccx * 2.0f;
    m.cy         = ccy * 2.0f;
    m.background = sky.level;
    m.peak       = peak;
    m.fluxTotal  = (float)total;
    m.saturated  = sat;
    m.snr        = (sky.noise > 0.0f) ? (peak / sky.noise) : 0.0f;
    return m;
}

// For a Gaussian star, HFD and FWHM are the same number, both
// 2.3548 sigma. Exposed so a caller can convert either way and so
// the relationship is written down once rather than rediscovered.
inline constexpr double GAUSSIAN_HFD_PER_SIGMA = 2.354820045030949;

// ── What this does NOT yet do, measured ─────────────────────
//
// Everything above is verified against synthetic profiles,
// including the exact Gaussian identity. It is NOT yet verified
// against a real star, and an attempt to verify it against
// emulator frames failed for a reason worth recording.
//
// Measured on 2026-10-31, sweeping the emulated focuser from 0 to
// 800 steps off focus on Vega: HFD read 32, 32, 30, 29, 30, 30,
// 30, 27 px. Flat. The metric did not respond to defocus at all.
//
// The cause is the emulator's background, not the arithmetic. Its
// "stars" are a 1-in-4096 sprinkle of single bright pixels, and
// the frame-wide blur pass that supplies defocus ALSO spreads that
// sprinkle into a smooth low-level haze. A haze is exactly what a
// star's wings look like to any radial test, so the edge detection
// cannot tell them apart and the aperture never closes.
//
// Outlier rejection does not help, and that is the diagnostic: the
// sprinkle is not isolated any more by the time the metric sees it.
//
// So the emulator cannot validate star photometry as it stands.
// Phase 94.5, a real star field, was the thing that would have
// fixed that, and it is backlogged -- which makes this a cost of
// that decision that neither the plan nor the decision anticipated.
//
// What that means for use: the metric is sound on a clean field and
// the synthetic gate proves it, but **the first real test is a star
// on the sky**, and until then the HUD figure should be read as
// provisional. It is wired and gated, not proven in the loop.
inline constexpr bool STAR_METRICS_PROVEN_ON_SKY = false;

// ── The sampling floor, which this rig sits on ──────────────
//
// Below about 2 superpixels of sigma the figure stops tracking the
// closed form, and NOT in a consistent direction. Measured against
// synthetic Gaussians: sigma 2, 3 and 4 superpixels land within a
// few per cent; sigma 1.5 reads about 10 per cent LOW; sigma 1.0
// reads about 20 per cent HIGH, because by then a single central
// superpixel holds most of the flux and spreads it over its own
// width.
//
// So this is a reliability floor, not a bias to correct for.
//
// That is not an edge case here, it is the normal case. A
// perfectly focused star on this rig has its first Airy minimum at
// 3.06 raw pixels, which is 1.53 superpixels, so **the absolute
// HFD figure is unreliable at best focus** and reads low.
//
// It does not break an autofocus sweep, which needs the curve to
// be MONOTONIC and have a findable minimum, not to be accurate.
// It does mean an arcsecond figure quoted near focus is optimistic,
// and that comparing this rig's number against a longer focal
// length is not comparing like with like.
inline constexpr double HFD_UNDERSAMPLED_BELOW_SUPERPX = 2.0;

// Arcseconds, which is the only unit a focus figure is worth
// comparing between rigs.
inline float ToArcsec(float px, float plateScaleArcsecPx) {
    return px * plateScaleArcsecPx;
}

} // namespace raduk::core
