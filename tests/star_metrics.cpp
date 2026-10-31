// ============================================================
//  Measuring a star's focus (phase 95)
//
//  The project's only focus metric is Sharpness.h's Laplacian
//  variance, which is built for a planetary disc: blur a disc and
//  only its limb moves. A star has no limb, it has a profile, so
//  the variance barely responds. That is roadmap row 31/32, and
//  the comparison found it is the gap against every competitor.
//
//  What makes this testable rather than a matter of taste is one
//  exact identity: for a Gaussian star, HFD and FWHM are the SAME
//  number, both 2.3548 sigma.
//
//    HFD is the diameter containing half the flux:
//      1 - exp(-R^2/2s^2) = 0.5  ->  R = s*sqrt(2 ln 2) = 1.1774s
//    FWHM is the width at half the peak:
//      exp(-r^2/2s^2) = 0.5      ->  r = s*sqrt(2 ln 2) = 1.1774s
//
//  Same radius, by coincidence of the Gaussian. So a synthetic
//  Gaussian pins both metrics against a closed form and against
//  each other, with no reference implementation to trust.
//
//  Where they MUST disagree is the other half of the point.
//  Defocus an obstructed aperture and the star becomes an
//  annulus: the peak leaves the centre, half-maximum is crossed
//  four times along any line, and FWHM stops meaning anything
//  while HFD keeps growing. An autofocus sweep driven by FWHM
//  would wander there; driven by HFD it would not. So the test
//  checks that HFD stays monotonic across a defocus series and
//  that FWHM REFUSES rather than returning one of its four
//  crossings.
//
//  Two failure modes that would pass a careless test:
//
//    A metric measured about the brightest pixel instead of the
//    centroid. On a Gaussian they are the same point and
//    everything agrees; on an annulus the brightest pixel is on
//    the ring and the profile about it is nonsense.
//
//    An aperture too small for the wings. HFD then stops growing
//    with defocus, which an autofocus routine reads as
//    approaching focus -- it would converge confidently on the
//    wrong end of the sweep.
// ============================================================

#include "core/StarMetrics.h"

#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

using namespace raduk::core;

static int g_fail = 0;

static void Near(double got, double want, double tol, const char* what) {
    if (std::fabs(got - want) <= tol) printf("  ok    %s (%.4f)\n", what, got);
    else { printf("  FAIL  %s: got %.4f, wanted %.4f +/- %.4f\n",
                  what, got, want, tol); ++g_fail; }
}

static void Check(bool ok, const char* what) {
    if (ok) printf("  ok    %s\n", what);
    else  { printf("  FAIL  %s\n", what); ++g_fail; }
}

// ── Synthetic Bayer frames ─────────────────────────────────
//
// Written as RAW8 with every pixel of a 2x2 quad set to a quarter
// of the intended superpixel value, so Superpixel() recovers it
// exactly and the mosaic cannot influence the result.
static const int FW = 256, FH = 256;

static void Fill(std::vector<uint8_t>& f, double sky) {
    f.assign((size_t)FW * FH, (uint8_t)(sky / 4.0));
}

// Add a radially symmetric profile centred on the frame, given a
// function of radius in SUPERPIXELS returning superpixel ADU.
template <typename F>
static void AddProfile(std::vector<uint8_t>& f, double sky, F prof) {
    const int sw = FW/2, sh = FH/2;
    const double cx = sw * 0.5 - 0.5, cy = sh * 0.5 - 0.5;
    for (int sy = 0; sy < sh; ++sy)
        for (int sx = 0; sx < sw; ++sx) {
            const double dx = sx - cx, dy = sy - cy;
            const double r  = std::sqrt(dx*dx + dy*dy);
            double v = sky + prof(r);
            if (v < 0.0) v = 0.0;
            if (v > 1020.0) v = 1020.0;
            const uint8_t q = (uint8_t)(v / 4.0 + 0.5);
            const int x = sx*2, y = sy*2;
            f[(size_t)y*FW + x] = q;  f[(size_t)y*FW + x+1] = q;
            f[(size_t)(y+1)*FW + x] = q;  f[(size_t)(y+1)*FW + x+1] = q;
        }
}

struct Measured { StarMeasurement m; float edge; };

static Measured Run(const std::vector<uint8_t>& f, float aperturePx) {
    Measured out;
    const float cx = (float)(FW/2), cy = (float)(FH/2);
    const SkyEstimate sky = EstimateSky(f.data(), FW, FH, 8, cx, cy,
                                        aperturePx + 20.0f, aperturePx + 60.0f);
    out.m = MeasureStar(f.data(), FW, FH, 8, cx, cy, aperturePx, sky, &out.edge);
    return out;
}

int main() {
    printf("Star metrics: HFD and FWHM\n");

    // ---- the exact identity -----------------------------------
    printf("\n1. For a Gaussian, HFD and FWHM are the same number\n");
    {
        // sigma in SUPERPIXELS. 2.0 superpx = 4 raw px, comfortably
        // sampled and small next to the aperture.
        // sigma 1.5 is deliberately absent: see section 1b. It sits
        // below the sampling floor and reads low, which is the normal
        // case on this rig and is documented rather than tolerated.
        for (double sigma : { 2.0, 3.0, 4.0 }) {
            std::vector<uint8_t> f;
            Fill(f, 40.0);
            AddProfile(f, 40.0, [&](double r) {
                return 600.0 * std::exp(-(r*r) / (2.0*sigma*sigma));
            });
            const Measured q = Run(f, 60.0f);
            if (!q.m.valid) { printf("  FAIL  sigma %.1f did not measure: %s\n",
                                     sigma, q.m.why); ++g_fail; continue; }

            // Closed form, converted superpixels -> raw px.
            const double want = GAUSSIAN_HFD_PER_SIGMA * sigma * 2.0;
            char buf[96];
            snprintf(buf, sizeof(buf), "sigma %.1f: HFD is 2.3548 sigma", sigma);
            Near(q.m.hfdPx, want, want * 0.06, buf);

            Check(q.m.fwhmValid, "FWHM is valid on a Gaussian");
            if (q.m.fwhmValid) {
                snprintf(buf, sizeof(buf), "sigma %.1f: FWHM equals HFD", sigma);
                Near(q.m.fwhmPx, q.m.hfdPx, q.m.hfdPx * 0.10, buf);
            }
        }
    }

    // ---- the sampling floor, which this rig sits on ------------
    printf("\n1b. Below two superpixels of sigma the figure stops tracking\n");
    {
        // Not a defect to fix, a limit to know. A perfectly focused
        // star on this rig has its first Airy minimum at 3.06 raw px,
        // which is 1.53 superpixels -- so this IS the normal case near
        // focus, and the absolute figure is optimistic there.
        for (double sigma : { 1.0, 1.5 }) {
            std::vector<uint8_t> f;
            Fill(f, 40.0);
            AddProfile(f, 40.0, [&](double r) {
                return 600.0 * std::exp(-(r*r) / (2.0*sigma*sigma));
            });
            const Measured q = Run(f, 60.0f);
            if (!q.m.valid) { printf("  FAIL  sigma %.1f: %s\n", sigma, q.m.why);
                              ++g_fail; continue; }
            const double want = GAUSSIAN_HFD_PER_SIGMA * sigma * 2.0;
            printf("  --    sigma %.2f superpx: HFD %.2f px, closed form %.2f, "
                   "%.0f%% low\n",
                   sigma, q.m.hfdPx, want, (1.0 - q.m.hfdPx/want) * 100.0);
            // NOT asserted to be low. The first version of this test
            // claimed undersampling biases HFD downward; sigma 1.0
            // reads 20 per cent HIGH, because by then one central
            // superpixel holds most of the flux and spreads it over
            // its own width. The floor is unreliability, not a bias.
            Check(std::fabs(q.m.hfdPx - want) > want * 0.05,
                  "it departs from the closed form by more than 5 per cent");
            Check(std::fabs(q.m.hfdPx - want) < want * 0.30,
                  "but stays within 30 per cent, so a sweep is still usable");
        }
        Check(HFD_UNDERSAMPLED_BELOW_SUPERPX == 2.0,
              "and the floor is a named constant, not folklore");
        Check(3.06 / 2.0 < HFD_UNDERSAMPLED_BELOW_SUPERPX,
              "which this rig's in-focus Airy pattern sits below, so the "
              "absolute figure near best focus is not to be quoted");
    }

    // ---- it scales ---------------------------------------------
    printf("\n2. Both metrics scale with the star's width\n");
    {
        double prevH = 0.0;
        bool rising = true;
        for (double sigma : { 1.0, 2.0, 4.0, 6.0 }) {
            std::vector<uint8_t> f;
            Fill(f, 40.0);
            AddProfile(f, 40.0, [&](double r) {
                return 600.0 * std::exp(-(r*r) / (2.0*sigma*sigma));
            });
            const Measured q = Run(f, 80.0f);
            if (!q.m.valid) { rising = false; break; }
            printf("  --    sigma %.1f superpx -> HFD %.2f px\n", sigma, q.m.hfdPx);
            if (q.m.hfdPx <= prevH) rising = false;
            prevH = q.m.hfdPx;
        }
        Check(rising, "HFD rises with every increase in sigma");
    }

    // ---- the case that separates them --------------------------
    printf("\n3. On a defocused annulus, HFD works and FWHM refuses\n");
    {
        // An obstructed aperture out of focus: a bright ring with a
        // dark hole, which is what an SCT actually shows.
        double prevH = 0.0;
        bool monotone = true;
        int refusedFwhm = 0, measuredCount = 0;
        for (double ringR : { 4.0, 7.0, 11.0, 16.0 }) {
            std::vector<uint8_t> f;
            Fill(f, 40.0);
            const double thick = ringR * 0.35;
            AddProfile(f, 40.0, [&](double r) {
                const double d = std::fabs(r - ringR);
                if (d > thick) return 0.0;
                return 500.0 * (1.0 - (d/thick)*(d/thick));
            });
            const Measured q = Run(f, 90.0f);
            if (!q.m.valid) { printf("  FAIL  ring %.0f: %s\n", ringR, q.m.why);
                              ++g_fail; continue; }
            ++measuredCount;
            printf("  --    ring radius %.0f superpx -> HFD %.2f px, FWHM %s\n",
                   ringR, q.m.hfdPx,
                   q.m.fwhmValid ? "reported" : "refused");
            if (q.m.hfdPx <= prevH) monotone = false;
            prevH = q.m.hfdPx;
            if (!q.m.fwhmValid) ++refusedFwhm;
        }
        Check(measuredCount == 4, "every annulus measured");
        Check(monotone,
              "HFD grows monotonically through the whole defocus series");
        Check(refusedFwhm == 4,
              "and FWHM refuses on all four, rather than returning one of "
              "its four half-maximum crossings");
    }

    // ---- the centroid, not the brightest pixel -----------------
    printf("\n4. Measured about the centroid, which an annulus exposes\n");
    {
        std::vector<uint8_t> f;
        Fill(f, 40.0);
        AddProfile(f, 40.0, [](double r) {
            const double d = std::fabs(r - 10.0);
            return d > 3.0 ? 0.0 : 500.0 * (1.0 - (d/3.0)*(d/3.0));
        });
        const Measured q = Run(f, 90.0f);
        Check(q.m.valid, "the annulus measured");
        if (q.m.valid) {
            // The centroid of a symmetric ring is its centre, even
            // though no bright pixel is there at all.
            Near(q.m.cx, (double)(FW/2) - 1.0, 3.0,
                 "the centroid sits in the middle of the ring");
            Near(q.m.cy, (double)(FH/2) - 1.0, 3.0, "in both axes");
            // And the hole really is dark, so a profile about the
            // brightest pixel would have been measuring the rim.
            Check(q.m.hfdPx > 10.0,
                  "HFD reflects the ring's size, not a core that is not there");
        }
    }

    // ---- an aperture too small is the dangerous failure --------
    printf("\n5. A too-small aperture under-reports, and says so\n");
    {
        std::vector<uint8_t> f;
        Fill(f, 40.0);
        AddProfile(f, 40.0, [](double r) {
            return 600.0 * std::exp(-(r*r) / (2.0*6.0*6.0));
        });
        const Measured wide   = Run(f, 90.0f);
        const Measured narrow = Run(f, 16.0f);
        Check(wide.m.valid && narrow.m.valid, "both apertures measured");
        if (wide.m.valid && narrow.m.valid) {
            printf("  --    wide aperture HFD %.2f px (edge %.3f), "
                   "narrow HFD %.2f px (edge %.3f)\n",
                   wide.m.hfdPx, wide.edge, narrow.m.hfdPx, narrow.edge);
            Check(narrow.m.hfdPx < wide.m.hfdPx,
                  "the narrow aperture under-reports, which is the direction "
                  "that would fool an autofocus sweep");
            Check(narrow.edge > wide.edge * 2.0,
                  "and it reports far more flux in its outer ring, which is "
                  "how a caller can tell");
        }
    }

    // ---- the sky ----------------------------------------------
    printf("\n6. The sky is a median in an annulus, not a mean of the frame\n");
    {
        std::vector<uint8_t> f;
        Fill(f, 40.0);
        AddProfile(f, 40.0, [](double r) {
            return 600.0 * std::exp(-(r*r) / (2.0*2.0*2.0));
        });
        const float cx = (float)(FW/2), cy = (float)(FH/2);

        const SkyEstimate clean = EstimateSky(f.data(), FW, FH, 8, cx, cy,
                                              60.0f, 110.0f);
        Check(clean.valid, "the annulus had enough samples");
        Near(clean.level, 40.0, 4.0, "and reads the sky level it was given");

        // Drop a bright intruder into the annulus: a second star, a
        // hot pixel, a satellite. A mean would move; a median must not.
        std::vector<uint8_t> g = f;
        for (int sy = 40; sy < 48; ++sy)
            for (int sx = 40; sx < 48; ++sx) {
                const int x = sx*2, y = sy*2;
                g[(size_t)y*FW + x] = 250;  g[(size_t)y*FW + x+1] = 250;
                g[(size_t)(y+1)*FW + x] = 250; g[(size_t)(y+1)*FW + x+1] = 250;
            }
        const SkyEstimate spoiled = EstimateSky(g.data(), FW, FH, 8, cx, cy,
                                                60.0f, 110.0f);
        Check(spoiled.valid, "still measures with an intruder present");
        Near(spoiled.level, clean.level, 2.0,
             "and the median ignores it, where a mean would have moved");

        // Too small an annulus is refused rather than averaged badly.
        const SkyEstimate tiny = EstimateSky(f.data(), FW, FH, 8, cx, cy,
                                             2.0f, 3.0f);
        Check(!tiny.valid, "an annulus with too few samples is refused");
        const SkyEstimate inverted = EstimateSky(f.data(), FW, FH, 8, cx, cy,
                                                 80.0f, 20.0f);
        Check(!inverted.valid, "and so is one whose radii are the wrong way round");
    }

    // ---- finding the star, and declining to -------------------
    printf("\n7. Finding the star, and refusing when there is none\n");
    {
        std::vector<uint8_t> f;
        Fill(f, 40.0);
        AddProfile(f, 40.0, [](double r) {
            return 600.0 * std::exp(-(r*r) / (2.0*2.0*2.0));
        });
        float sx = 0, sy = 0;
        Check(FindBrightestStar(f.data(), FW, FH, 8, 40.0f, 100.0f, sx, sy),
              "a star 600 ADU above the sky is found");
        Near(sx, (double)(FW/2) - 1.0, 4.0, "at about the right x");
        Near(sy, (double)(FH/2) - 1.0, 4.0, "and y");

        std::vector<uint8_t> blank;
        Fill(blank, 40.0);
        Check(!FindBrightestStar(blank.data(), FW, FH, 8, 40.0f, 100.0f, sx, sy),
              "an empty field is refused rather than returning a noise pixel");
    }

    // ---- refusals ---------------------------------------------
    printf("\n8. What it refuses to measure\n");
    {
        std::vector<uint8_t> f;
        Fill(f, 40.0);
        AddProfile(f, 40.0, [](double r) {
            return 600.0 * std::exp(-(r*r) / (2.0*2.0*2.0));
        });
        const float cx = (float)(FW/2), cy = (float)(FH/2);
        const SkyEstimate sky = EstimateSky(f.data(), FW, FH, 8, cx, cy,
                                            60.0f, 110.0f);

        SkyEstimate bad;   // never populated
        Check(!MeasureStar(f.data(), FW, FH, 8, cx, cy, 40.0f, bad).valid,
              "no sky estimate");
        Check(!MeasureStar(f.data(), FW, FH, 8, cx, cy, 1.0f, sky).valid,
              "an aperture of one pixel");
        Check(!MeasureStar(nullptr, FW, FH, 8, cx, cy, 40.0f, sky).valid,
              "a null frame");

        std::vector<uint8_t> blank;
        Fill(blank, 40.0);
        const SkyEstimate bsky = EstimateSky(blank.data(), FW, FH, 8, cx, cy,
                                             60.0f, 110.0f);
        const StarMeasurement none =
            MeasureStar(blank.data(), FW, FH, 8, cx, cy, 40.0f, bsky);
        Check(!none.valid, "and a field with nothing above the sky");
        Check(std::string(none.why).size() > 0, "with a reason given");
        printf("  --    it says: \"%s\"\n", none.why);
    }

    // ---- saturation -------------------------------------------
    printf("\n9. A clipped core is reported, not silently measured\n");
    {
        std::vector<uint8_t> f;
        Fill(f, 40.0);
        // Deliberately over the clip, so the core flattens.
        AddProfile(f, 40.0, [](double r) {
            return 3000.0 * std::exp(-(r*r) / (2.0*3.0*3.0));
        });
        const Measured q = Run(f, 70.0f);
        Check(q.m.valid, "a saturated star still measures");
        Check(q.m.saturated > 0,
              "and the clipped superpixels are counted, because a flat top "
              "makes both metrics read wide");
        printf("  --    %d superpixels at the clip\n", q.m.saturated);
    }

    // ---- arcseconds -------------------------------------------
    printf("\n10. The figure converts to arcseconds\n");
    {
        Near(ToArcsec(10.0f, 0.203f), 2.03, 1e-6, "10 px at 0.203\"/px");
        Near(ToArcsec(0.0f, 0.203f), 0.0, 1e-9, "and zero stays zero");
    }

    // Phase 99: asserted at last. STAR_METRICS_PROVEN_ON_SKY is the
    // honesty flag phase 95 declared and this file never read -- the
    // dead-code sweep found it, and the three later flags
    // (LUNAR_FRAMING_PROVEN_ON_SKY, MOSAIC_PROVEN_ON_SKY and
    // MOSAIC_RUN_PROVEN_ON_SKY) are each asserted by their own test.
    // A flag nothing checks is a comment with a type.
    printf("\nThe honesty flag\n");
    if (raduk::core::STAR_METRICS_PROVEN_ON_SKY) {
        printf("  FAIL  STAR_METRICS_PROVEN_ON_SKY claims the sky has"
               " confirmed this; no star has been measured yet\n");
        ++g_fail;
    } else {
        printf("  ok    still unproven on the sky, which it is\n");
    }

    if (g_fail == 0) printf("\nPASS\n"); else printf("\nFAIL: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
