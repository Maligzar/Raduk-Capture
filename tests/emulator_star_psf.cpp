// ============================================================
//  Does the emulator produce a POINT SOURCE (phase 94.1)
//
//  Until this phase every emulator target was an extended disc,
//  so the one thing the request was for -- varying what focus is
//  judged on, between a star and a planet -- could not be done.
//  The background "stars" were a 1-in-4096 hash sprinkle of
//  single pixels: positionally meaningless, one pixel wide, with
//  no profile to measure.
//
//  Why a point source is a different problem and not a smaller
//  one: blur a disc and only its limb moves, a one-dimensional
//  edge on a two-dimensional object, so a sharpness metric
//  changes slowly. Blur a point and ALL of its energy spreads, so
//  the peak falls as the square of the blur. That is why focus is
//  judged on a star, and it is what autofocus, FWHM, the
//  sharpness graph and the centroid tracker have never been run
//  against.
//
//  So the checks here are comparative. "A bright pixel exists"
//  would pass against the hash sprinkle. What has to be true is
//  that a star's radial profile is CONCENTRATED where a planet's
//  is a plateau, and that defocus collapses the star's peak far
//  faster than the planet's.
//
//  The Airy maths is checked against textbook values separately,
//  because those are facts rather than renderer behaviour: J1 at
//  known arguments, the first dark ring at x = 3.8317, and the
//  first bright ring at 1.75 per cent of the core.
// ============================================================

#include "core/CameraEmulator.h"

#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

using raduk::core::ASICamera;

static int g_fail = 0;

static void Near(double got, double want, double tol, const char* what) {
    if (std::fabs(got - want) <= tol) printf("  ok    %s (%.6f)\n", what, got);
    else { printf("  FAIL  %s: got %.6f, wanted %.6f +/- %.6f\n",
                  what, got, want, tol); ++g_fail; }
}

static void Check(bool ok, const char* what) {
    if (ok) printf("  ok    %s\n", what);
    else  { printf("  FAIL  %s\n", what); ++g_fail; }
}

// One frame of the named target, at a given focuser position.
// -1 means "no focus provider", which the emulator treats as in focus.
static bool Grab(const char* target, int focusPos,
                 std::vector<uint8_t>& out, int& W, int& H,
                 float lunarPhase = 0.25f) {
    ASICamera cam;
    if (!cam.Open(0)) return false;
    W = 640; H = 480;
    cam.SetROI(W, H, 1, ASI_IMG_RAW8);
    int aB = 1; ASI_IMG_TYPE aT = ASI_IMG_RAW8;
    cam.GetROI(W, H, aB, aT);
    cam.SetTargetName(target);
    cam.SetLunarPhase(lunarPhase);
    cam.SetExposure(5.0);
    cam.SetPeakTargetDn(200);          // a known peak, so DN is comparable
    cam.SetNoiseAmplitude(0);          // profile, not noise
    cam.SetFreezeSurface(true);
    if (focusPos >= 0) {
        cam.SetTrueFocusPosition(168000);
        cam.SetFocusPositionProvider([focusPos](int& p){ p = focusPos; return true; });
    }
    if (!cam.StartVideoCapture()) { cam.Close(); return false; }
    out.assign((size_t)W * H, 0);
    bool got = false;
    for (int i = 0; i < 8; i++)
        got = cam.GetVideoFrame(out.data(), (int)out.size(), 8000) || got;
    cam.StopVideoCapture();
    cam.Close();
    return got;
}

// Mean DN in an annulus between two radii of frame centre.
static double RingMean(const std::vector<uint8_t>& f, int W, int H,
                       double r0, double r1) {
    const double cx = W * 0.5, cy = H * 0.5;
    double sum = 0.0; long n = 0;
    for (int y = 0; y < H; y++) {
        const double dy = y - cy;
        for (int x = 0; x < W; x++) {
            const double dx = x - cx;
            const double r = std::sqrt(dx*dx + dy*dy);
            if (r >= r0 && r < r1) { sum += f[(size_t)y*W + x]; ++n; }
        }
    }
    return n ? sum / (double)n : 0.0;
}

static int PeakDn(const std::vector<uint8_t>& f) {
    int mx = 0;
    for (uint8_t v : f) if (v > mx) mx = v;
    return mx;
}

int main() {
    printf("Emulator star PSF: is it actually a point source\n");

    // ---- the maths, against textbook values --------------------
    printf("\n1. The Airy profile is the real one\n");
    {
        Near(ASICamera::BesselJ1(1.0),  0.4400505857, 1e-9, "J1(1)");
        Near(ASICamera::BesselJ1(2.0),  0.5767248078, 1e-9, "J1(2)");
        Near(ASICamera::BesselJ1(5.0), -0.3275791376, 1e-9, "J1(5)");
        Near(ASICamera::BesselJ1(ASICamera::AIRY_X1), 0.0, 1e-9,
             "J1 at 3.8317 is the first zero, which is the first dark ring");

        float lut[1024];
        ASICamera::BuildAiryLUT(lut, 1024, 4.0);
        Near(lut[0], 1.0, 1e-6, "the core is normalised to 1");

        // First dark ring at exactly 1 Airy radius.
        const int iMin = (int)(1023 / 4.0 + 0.5);
        Check(lut[iMin] < 1e-4, "and the intensity there is essentially zero");

        // First bright ring: textbook 1.75% of the core, at x = 5.136,
        // which is 5.136/3.8317 = 1.3404 Airy radii.
        int bi = 0; float best = 0.0f;
        for (int i = iMin; i < 1024; i++) if (lut[i] > best) { best = lut[i]; bi = i; }
        Near(best, 0.01750, 0.0005, "the first bright ring is 1.75% of the core");
        Near(4.0 * bi / 1023.0, 1.3404, 0.01,
             "and sits at 1.34 Airy radii, not at the dark ring");

        // The rig samples it, which is why drawing rings is worthwhile.
        Near(ASICamera::AiryRadiusArcsec(), 0.6818, 1e-3,
             "203 mm at 550 nm puts the first minimum at 0.68 arcsec");
        Check(ASICamera::AiryRadiusPx() > 2.0f,
              "which is over two pixels here, so the pattern is sampled");
    }

    // ---- a star is concentrated, a planet is a plateau ---------
    printf("\n2. A star's light is concentrated; a planet's is spread\n");
    std::vector<uint8_t> star, planet;
    int W = 0, H = 0, W2 = 0, H2 = 0;
    const bool okStar   = Grab("Vega",    -1, star,   W,  H);
    const bool okPlanet = Grab("Jupiter", -1, planet, W2, H2);
    Check(okStar,   "a star frame was delivered");
    Check(okPlanet, "a planet frame was delivered");

    if (okStar && okPlanet) {
        // Jupiter today is tens of arcsec across; at 0.223"/px that is
        // scores of pixels. Vega's whole pattern is under 20 px.
        const double sIn  = RingMean(star,   W,  H,  0.0,  4.0);
        const double sOut = RingMean(star,   W,  H, 30.0, 60.0);
        const double pIn  = RingMean(planet, W2, H2, 0.0,  4.0);
        const double pOut = RingMean(planet, W2, H2, 30.0, 60.0);

        const int sPeak = PeakDn(star), pPeak = PeakDn(planet);
        printf("  --    star   peak %d, r<2 %.1f DN, 30-60px out %.2f DN\n",
               sPeak, sIn, sOut);
        printf("  --    planet peak %d, r<2 %.1f DN, 30-60px out %.2f DN\n",
               pPeak, pIn, pOut);

        // A point source is measured by its PEAK, not by a mean over a
        // disc: averaging over 4 px here would span the first dark ring
        // and dilute the very concentration being tested.
        Check(sPeak > 100, "the star has a bright core");
        Check(sOut < 5.0, "and essentially nothing 30 to 60 px out");
        Check(pOut > 20.0,
              "the planet is still bright out there, because it is a disc");
        Check((sPeak / (sOut + 1.0)) > 10.0 * (pPeak / (pPeak * 0.0 + pOut + 1.0)),
              "so the star's peak-to-halo ratio is an order of magnitude higher");
    }

    // ---- the rings are in the PIXELS, not just in the LUT ------
    printf("\n2b. The diffraction rings survive into the frame\n");
    if (okStar) {
        // The first dark ring sits at AiryRadiusPx(), about 3.06 px, so
        // the 3-4 px annulus should fall to background and the 4-5 px
        // annulus should come back up. That is the whole claim of
        // rendering an Airy pattern rather than a blob, and it is only
        // true because this rig samples the pattern at all.
        const double r01 = RingMean(star, W, H, 0.0, 2.0);
        const double r34 = RingMean(star, W, H, 3.0, 4.0);
        const double r45 = RingMean(star, W, H, 4.0, 5.0);
        const double bg  = RingMean(star, W, H, 40.0, 60.0);
        printf("  --    r<2 %.1f | first dark ring (3-4px) %.2f | "
               "first bright ring (4-5px) %.2f | background %.2f\n",
               r01, r34, r45, bg);

        Check(r01 > 50.0, "the core is bright");
        Check(r34 < r01 * 0.1,
              "the 3-4 px annulus has collapsed, which is the first dark ring");
        Check(r45 > r34,
              "and the 4-5 px annulus comes back up: the first bright ring");
        Near(ASICamera::AiryRadiusPx(), 3.06, 0.05,
             "which is where 1.22*lambda/D said it would be");
    }

    // ---- which is why focus behaves differently ----------------
    printf("\n3. Defocus collapses a point far faster than a disc\n");
    if (okStar && okPlanet) {
        std::vector<uint8_t> starDef, planetDef;
        int a=0,b=0,c=0,d=0;
        // 2000 steps off focus. At the derived 0.0228 px/step that is
        // about 46 px of blur, far wider than the star's whole pattern
        // and a fraction of Jupiter's disc.
        const bool o1 = Grab("Vega",    166000, starDef,   a, b);
        const bool o2 = Grab("Jupiter", 166000, planetDef, c, d);
        Check(o1 && o2, "both defocused frames were delivered");
        if (o1 && o2) {
            const double sKeep = (double)PeakDn(starDef)   / (double)PeakDn(star);
            const double pKeep = (double)PeakDn(planetDef) / (double)PeakDn(planet);
            printf("  --    star keeps %.3f of its peak, planet keeps %.3f\n",
                   sKeep, pKeep);
            Check(sKeep < pKeep,
                  "the star loses more of its peak than the planet does");
            Check(sKeep < 0.5,
                  "and loses at least half of it, so the metric has a signal");
        }
    }

    // ---- star types --------------------------------------------
    printf("\n4. Spectral class reaches the pixels\n");
    {
        // The frame is Bayer: (even,even) is red, (odd,odd) is blue. A
        // red giant and a blue-white dwarf must differ in that ratio,
        // or "star types" is a label with no effect.
        auto redBlue = [](const char* name) -> double {
            std::vector<uint8_t> f; int w=0,h=0;
            if (!Grab(name, -1, f, w, h)) return -1.0;
            const int cx = w/2, cy = h/2;
            long rs=0, bs=0; int rn=0, bn=0;
            for (int y = cy-8; y <= cy+8; y++)
                for (int x = cx-8; x <= cx+8; x++) {
                    if (x<0||y<0||x>=w||y>=h) continue;
                    const uint8_t v = f[(size_t)y*w+x];
                    if (y%2==0 && x%2==0) { rs+=v; ++rn; }
                    else if (y%2!=0 && x%2!=0) { bs+=v; ++bn; }
                }
            if (!rn || !bn || bs == 0) return -1.0;
            return ((double)rs/rn) / ((double)bs/bn);
        };
        const double vega = redBlue("Vega");
        const double bete = redBlue("Betelgeuse");
        printf("  --    Vega R/B %.3f, Betelgeuse R/B %.3f\n", vega, bete);
        Check(vega > 0.0 && bete > 0.0, "both stars rendered with colour");
        Check(bete > vega * 1.3,
              "Betelgeuse is markedly redder than Vega, as its class says");
        Check(vega < 1.0, "and Vega is blue-biased, as an A0 should be");
    }

    // ---- the Moon: is there anything to focus on --------------
    printf("\n5. The Moon has a terminator in the frame (94.3)\n");
    {
        // The complaint this fixes: a flat tinted disc overflowing the
        // sensor is a uniform bright rectangle, and a uniform
        // rectangle has no detail at ANY focus position. So the check
        // is not "is it bright" but "does it VARY", and most of all
        // "does it vary from fully lit to dark across the frame".
        std::vector<uint8_t> quarter, full;
        int w1=0,h1=0,w2=0,h2=0;
        const bool oq = Grab("Moon", -1, quarter, w1, h1, 0.25f);
        const bool of = Grab("Moon", -1, full,    w2, h2, 0.50f);
        Check(oq && of, "both lunar frames were delivered");

        if (oq && of) {
            // At first quarter the terminator passes through the disc
            // centre, which is the frame centre, so the left and right
            // halves must differ enormously.
            auto halfMean = [](const std::vector<uint8_t>& f, int w, int h,
                               bool leftHalf) {
                double sum = 0.0; long n = 0;
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++) {
                        const bool isLeft = (x < w/2);
                        if (isLeft != leftHalf) continue;
                        sum += f[(size_t)y*w + x]; ++n;
                    }
                return n ? sum / n : 0.0;
            };
            const double qL = halfMean(quarter, w1, h1, true);
            const double qR = halfMean(quarter, w1, h1, false);
            const double fL = halfMean(full,    w2, h2, true);
            const double fR = halfMean(full,    w2, h2, false);
            printf("  --    quarter: left %.1f DN, right %.1f DN\n", qL, qR);
            printf("  --    full:    left %.1f DN, right %.1f DN\n", fL, fR);

            const double qSplit = (qL > qR) ? qL/(qR+1.0) : qR/(qL+1.0);
            const double fSplit = (fL > fR) ? fL/(fR+1.0) : fR/(fL+1.0);
            Check(qSplit > 5.0,
                  "at first quarter one half of the frame is many times "
                  "brighter: the terminator is in shot");
            Check(fSplit < 1.5,
                  "at full the two halves match, because the Sun is behind "
                  "the observer and nothing is shadowed");
            Check(qSplit > fSplit * 3.0,
                  "so phase genuinely changes what is in the frame");

            // And the surface is not flat even on the lit side, which
            // is what the maria and craters are for.
            double lo = 255.0, hi = 0.0;
            const int cx = w2/2, cy = h2/2;
            for (int y = cy-60; y < cy+60; y++)
                for (int x = cx-60; x < cx+60; x++) {
                    if (x<0||y<0||x>=w2||y>=h2) continue;
                    const double v = full[(size_t)y*w2+x];
                    if (v < lo) lo = v; if (v > hi) hi = v;
                }
            printf("  --    full Moon, central 120x120: %.0f to %.0f DN\n", lo, hi);
            Check(hi - lo > 10.0,
                  "even a full Moon has albedo structure, not a flat field");
        }
    }

    if (g_fail == 0) printf("\nPASS\n"); else printf("\nFAIL: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
