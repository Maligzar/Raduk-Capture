// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// ============================================================
//  Does the ADC alignment tool measure dispersion correctly?
//
//  The tool's whole claim is that the number it shows is the
//  displacement actually present between the red and blue images. That
//  claim is checkable without sky: synthesize a disc, displace the blue
//  channel by a known amount, and see whether the measured answer is
//  the amount put in.
//
//  Everything here runs on the bench. What it cannot answer is in
//  docs/CLEAR-NIGHT-CHECKLIST.md.
// ============================================================

#include "core/ADCAlignment.h"

#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

using raduk::core::ADCAlignment;
using raduk::core::DispersionMeasurement;

static int g_fail = 0;

static void Check(bool ok, const std::string& what, const std::string& detail) {
    printf("  [%s] %s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

// Angles wrap, so 359.5 is half a degree from 0 and not 359.5 from it.
static void CheckAngle(float got, float want, float tol,
                       const std::string& what) {
    float d = std::fabs(got - want);
    if (d > 180.0f) d = 360.0f - d;
    char det[160];
    snprintf(det, sizeof(det), "got %.1f deg, expected %.1f +/- %.1f", got, want, tol);
    Check(d <= tol, what, det);
}

static void CheckNear(float got, float want, float tol,
                      const std::string& what) {
    char d[160];
    snprintf(d, sizeof(d), "got %.3f, expected %.3f +/- %.3f", got, want, tol);
    Check(std::fabs(got - want) <= tol, what, d);
}

// ── A synthetic planet ───────────────────────────────────────
//
// A limb-darkened disc, drawn once per channel at its own sub-pixel
// centre and its own brightness. Limb darkening matters: a flat disc
// would make the centroid trivially the geometric centre, and the real
// question is whether the measurement survives a brightness profile.
struct Frame {
    int w, h;
    std::vector<uint8_t> px;   // RGGB Bayer, 8-bit, one byte per pixel
    Frame(int W, int H) : w(W), h(H), px((size_t)W * H, 8) {}
};

// Write one channel's disc into its own Bayer sites.
//
// channel 0 = red, at (even, even).  1 = green, at the two mixed sites.
// 2 = blue, at (odd, odd).  Writing into the real mosaic rather than
// into an RGB image is the point: it is the mosaic the measurement
// reads, and the half-pixel offset between the red and blue sites is
// exactly the thing that has to come out right.
static void DrawDiscBayer(Frame& f, int channel, double cx, double cy,
                          double radius, double peak, double bandDepth = 0.0) {
    for (int y = 0; y < f.h; y++) {
        for (int x = 0; x < f.w; x++) {
            const bool ey = (y % 2) == 0, ex = (x % 2) == 0;
            int site;                       // which channel this site carries
            if (ey && ex)        site = 0;  // R
            else if (!ey && !ex) site = 2;  // B
            else                 site = 1;  // G
            if (site != channel) continue;

            const double dx = (x + 0.5) - cx;
            const double dy = (y + 0.5) - cy;
            const double r2 = (dx * dx + dy * dy) / (radius * radius);
            if (r2 >= 1.0) continue;
            double v = peak * (1.0 - 0.6 * r2);          // limb darkening
            if (bandDepth > 0.0) {
                const double band = std::sin((dy / radius) * 3.1 + 0.9);
                v *= (1.0 - bandDepth * (band > 0.0 ? band : 0.0));
            }
            const int nv = (int)(v + 0.5);
            f.px[(size_t)y * f.w + x] = (uint8_t)(nv < 0 ? 0 : nv > 255 ? 255 : nv);
        }
    }
}

// A target whose blue image sits (dxPx, dyPx) from its red one, in
// full-resolution pixels, +y down.
static Frame MakeTarget(int w, int h, double dxPx, double dyPx,
                        double redPeak = 230.0, double bluePeak = 230.0) {
    Frame f(w, h);
    const double cx = w / 2.0, cy = h / 2.0, rad = w / 6.0;
    DrawDiscBayer(f, 0, cx,            cy,            rad, redPeak);
    DrawDiscBayer(f, 1, cx + dxPx / 2, cy + dyPx / 2, rad, 230.0);
    DrawDiscBayer(f, 2, cx + dxPx,     cy + dyPx,     rad, bluePeak);
    return f;
}

// Jupiter, in other words. The belts are drawn with different contrast
// in red and blue -- which is what a red-brown belt IS -- and at the
// same place in both, because a belt does not move between channels.
// Any displacement reported on this target with dxPx = dyPx = 0 is the
// measurement inventing one.
static Frame MakeBandedTarget(int w, int h, double dxPx, double dyPx) {
    Frame f(w, h);
    const double cx = w / 2.0, cy = h / 2.0, rad = w / 5.0;
    DrawDiscBayer(f, 0, cx,            cy,            rad, 235.0, 0.55);
    DrawDiscBayer(f, 1, cx + dxPx / 2, cy + dyPx / 2, rad, 215.0, 0.30);
    DrawDiscBayer(f, 2, cx + dxPx,     cy + dyPx,     rad, 180.0, 0.08);
    return f;
}

static const float PLATE = 0.203f;   // arcsec/px, 8SE + ASI678MC at f/10

int main() {
    printf("\n=== ADC alignment: does it measure what is actually there? ===\n");

    // ── 1. A known displacement comes back ───────────────────
    printf("\n1. Recovering a known displacement\n");
    {
        Frame f = MakeTarget(256, 256, 0.0, 5.0);
        DispersionMeasurement m = ADCAlignment::MeasureBayer(f.px.data(), f.w, f.h, 8, PLATE);
        Check(m.valid, "a displaced disc measures as valid", "");
        CheckNear(m.dyPx, 5.0f, 0.15f, "blue 5px below red reads dy = 5");
        // 0.2 px is 0.04 arcsec here -- a fifth of the residual the
        // window is willing to call aligned. Red and blue are each
        // sampled on half the sensor grid, so this is the floor the
        // mosaic sets, not slack in the matcher.
        CheckNear(m.dxPx, 0.0f, 0.25f, "no sideways component is reported");
        CheckNear(m.magArcsec, 5.0f * PLATE, 0.05f,
                  "magnitude converts to arcsec by the plate scale");
    }

    // ── 2. Sub-pixel, which is the whole point near alignment ─
    printf("\n2. Sub-pixel resolution\n");
    {
        // Once a corrector is nearly dialled in, the residual IS
        // sub-pixel. A tool that could only resolve whole pixels would
        // go blind exactly where it is needed.
        Frame f = MakeTarget(256, 256, 0.0, 0.4);
        DispersionMeasurement m = ADCAlignment::MeasureBayer(f.px.data(), f.w, f.h, 8, PLATE);
        CheckNear(m.dyPx, 0.4f, 0.25f, "a 0.4px residual is resolved");
    }

    // ── 3. Direction ─────────────────────────────────────────
    printf("\n3. Direction of the residual\n");
    {
        // Blue ABOVE red: in alt-az with zenith up the frame, this is
        // the uncorrected sense -- blue refracts toward the zenith.
        Frame up = MakeTarget(256, 256, 0.0, -6.0);
        DispersionMeasurement m = ADCAlignment::MeasureBayer(up.px.data(), up.w, up.h, 8, PLATE);
        CheckAngle(m.angleDeg, 0.0f, 3.0f, "blue toward the top reads 0 degrees");

        Frame down = MakeTarget(256, 256, 0.0, 6.0);
        m = ADCAlignment::MeasureBayer(down.px.data(), down.w, down.h, 8, PLATE);
        CheckAngle(m.angleDeg, 180.0f, 3.0f,
                  "overcorrected, blue below, reads 180 degrees");

        Frame right = MakeTarget(256, 256, 6.0, 0.0);
        m = ADCAlignment::MeasureBayer(right.px.data(), right.w, right.h, 8, PLATE);
        CheckAngle(m.angleDeg, 90.0f, 3.0f,
                  "blue to the right reads 90 degrees -- the ADC body is rotated wrong");
    }

    // ── 4. Channel brightness must not fake a displacement ────
    printf("\n4. Immunity to channel brightness\n");
    {
        // This is the failure that would make the tool actively
        // harmful: through a red-heavy filter, or at any white balance
        // other than neutral, a measurement keyed to an absolute level
        // would report a displacement that is not there, and someone
        // would dial a corrector to chase it.
        Frame f = MakeTarget(256, 256, 0.0, 0.0, /*red*/ 250.0, /*blue*/ 90.0);
        DispersionMeasurement m = ADCAlignment::MeasureBayer(f.px.data(), f.w, f.h, 8, PLATE);
        Check(m.valid, "a strongly colour-cast target still measures", "");
        // Same 0.25 px floor as every other reading here: red and blue
        // are each sampled on half the sensor grid, and that sets the
        // precision. 0.25 px is 0.05 arcsec, a third of what the window
        // is willing to call aligned.
        CheckNear(m.magPx, 0.0f, 0.25f,
                  "red 2.8x brighter than blue, no displacement, reads zero");
    }
    {
        // And the displacement still reads true under the same cast.
        Frame f = MakeTarget(256, 256, 0.0, 4.0, 250.0, 90.0);
        DispersionMeasurement m = ADCAlignment::MeasureBayer(f.px.data(), f.w, f.h, 8, PLATE);
        CheckNear(m.dyPx, 4.0f, 0.2f,
                  "and a real 4px displacement survives the cast");
    }

    // ── 4b. A banded planet, which is what this rig points at ─
    printf("\n4b. Colour STRUCTURE, not just colour balance\n");
    {
        // The failure that an earlier centroid implementation actually
        // had, caught on the emulator: a banded Jupiter with no
        // dispersion whatever read 0.63 arcsec, and the number drifted
        // as the planet rotated. Belts are red-brown, so red's first
        // moment sits somewhere else on the disc than blue's, and a
        // centroid cannot tell that apart from the atmosphere. Someone
        // would have dialled a corrector to chase cloud belts.
        Frame f = MakeBandedTarget(256, 256, 0.0, 0.0);
        DispersionMeasurement m = ADCAlignment::MeasureBayer(f.px.data(), f.w, f.h, 8, PLATE);
        Check(m.valid, "a banded disc measures", "");
        // 0.05 arcsec of phantom residual. The centroid method this
        // replaced read 0.63 arcsec on the same kind of target.
        CheckNear(m.magArcsec, 0.0f, 0.08f,
                  "strong belts in red, almost none in blue, no dispersion "
                  "-> reads zero");
    }
    {
        // And a real displacement on the same banded target still
        // reads true, so the fix did not buy immunity by going blind.
        Frame f = MakeBandedTarget(256, 256, 0.0, 6.0);
        DispersionMeasurement m = ADCAlignment::MeasureBayer(f.px.data(), f.w, f.h, 8, PLATE);
        // Looser than the plain-disc case, and measured rather than
        // guessed: belts this asymmetric -- 0.55 depth in red against
        // 0.08 in blue, nearly 7:1 -- bias the match by about a pixel,
        // which is 0.2 arcsec at this plate scale. That is the worst
        // case in the suite and it is a deliberately extreme target;
        // it still resolves 6 px from 5 px, which is the judgement the
        // tool is actually asked to make.
        CheckNear(m.dyPx, 6.0f, 1.1f, "a real 6px shift on a banded disc reads 6");
    }

    // ── 4c. Out of range is not a measurement ────────────────
    printf("\n4c. Displacement beyond the search range\n");
    {
        Frame f = MakeTarget(300, 300, 0.0, 40.0);   // far past SEARCH_PX
        DispersionMeasurement m = ADCAlignment::MeasureBayer(f.px.data(), f.w, f.h, 8, PLATE);
        Check(!m.valid,
              "a displacement outside the search range reports invalid, "
              "not a clamped maximum", "");
    }

    // ── 5. Refusing to answer ────────────────────────────────
    printf("\n5. Knowing when not to answer\n");
    {
        Frame blank(256, 256);          // uniform background, no target
        DispersionMeasurement m = ADCAlignment::MeasureBayer(blank.px.data(), 256, 256, 8, PLATE);
        Check(!m.valid, "a blank frame reports invalid rather than 0.00", "");

        // A target genuinely too small to measure -- a few pixels of a
        // faint star, not a planet. The floor exists so that a reading
        // taken off a dozen pixels cannot be presented with the same
        // confidence as one taken off a disc.
        Frame tiny(256, 256);
        DrawDiscBayer(tiny, 0, 128.0, 128.0, 2.0, 230.0);
        DrawDiscBayer(tiny, 2, 128.0, 131.0, 2.0, 230.0);
        m = ADCAlignment::MeasureBayer(tiny.px.data(), 256, 256, 8, PLATE);
        Check(!m.valid, "too few signal pixels reports invalid", "");

        m = ADCAlignment::MeasureBayer(nullptr, 256, 256, 8, PLATE);
        Check(!m.valid, "a null buffer reports invalid", "");
    }

    // ── 6. Smoothing, which is what the display shows ────────
    printf("\n6. The rolling window\n");
    {
        // Seeing scatter around a true residual of 2px. The median of
        // the components must land on the truth, not on the scatter.
        ADCAlignment roll;
        const float truth = 2.0f;
        const float jitter[9] = { -1.6f, 1.4f, -0.9f, 0.7f, 0.0f, -0.5f, 1.1f, -1.2f, 0.4f };
        for (int i = 0; i < 9; i++) {
            DispersionMeasurement s;
            s.valid = true;
            s.dxPx = 0.0f;
            s.dyPx = truth + jitter[i];
            s.signalPx = 5000;
            roll.Push(s);
        }
        DispersionMeasurement sm = roll.Smoothed(PLATE);
        CheckNear(sm.dyPx, truth, 0.6f, "median lands on the true residual");

        // The reason components are medianed rather than magnitudes:
        // around a true zero, magnitude is non-negative, so a median of
        // magnitudes can never be zero and a corrector that IS aligned
        // would never read as aligned.
        ADCAlignment atZero;
        for (int i = 0; i < 9; i++) {
            DispersionMeasurement s;
            s.valid = true;
            s.dxPx = 0.0f;
            s.dyPx = jitter[i];          // scatter about a true zero
            s.signalPx = 5000;
            atZero.Push(s);
        }
        sm = atZero.Smoothed(PLATE);
        CheckNear(sm.magPx, 0.0f, 0.5f,
                  "scatter about zero reads as aligned, not as a residual");
    }
    {
        ADCAlignment roll;
        for (int i = 0; i < ADCAlignment::WINDOW + 25; i++) {
            DispersionMeasurement s;
            s.valid = true; s.dyPx = 1.0f; s.signalPx = 5000;
            roll.Push(s);
        }
        char d[80];
        snprintf(d, sizeof(d), "holds %d of %d",
                 roll.SampleCount(), ADCAlignment::WINDOW);
        Check(roll.SampleCount() == ADCAlignment::WINDOW,
              "the window is bounded", d);

        DispersionMeasurement bad;   // invalid
        const int before = roll.SampleCount();
        roll.Push(bad);
        Check(roll.SampleCount() == before,
              "an invalid frame is dropped, not stored as a zero", "");
    }

    printf("\n");
    if (g_fail == 0) printf("=== the measurement is what it says it is ===\n\n");
    else             printf("=== %d CHECK(S) FAILED ===\n\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
