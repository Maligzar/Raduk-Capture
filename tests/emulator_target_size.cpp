// ============================================================
//  How big the emulator thinks the target is (phase 94.1/94.2)
//
//  Two constants in CameraEmulator.h were standing in for things
//  that are not constant, and both had already gone wrong once.
//
//  THE APPARENT RADIUS. EMU_TARGETS carries a radius "at a
//  representative opposition". Apparent size is the one property
//  of a planet that is never constant, so that number is wrong
//  nearly always: checked against the ephemeris on the day this
//  was written, Mars was 2.3x smaller than the table and Venus
//  2.4x larger. An emulator answering "will it fit in my frame"
//  with a 2x error is answering a different question.
//
//  THE DEFOCUS BLUR. It was a literal 0.0083 px/step, derived for
//  a plate scale of 0.61"/px. Phase 91 moved the scale to 0.223
//  and the literal did not follow, so the blur ran 2.74x too weak
//  and the autofocus V-curve 2.74x too shallow -- which is the
//  same failure the comment above it says it was written to
//  prevent, arriving from the other side.
//
//  What is pinned here is therefore not a number but the shape of
//  the arithmetic: that the override wins, that it is CLEARED
//  when the target changes, and that blur and disc rescale
//  together so the ratio between them survives a change of plate
//  scale. A pixel count cannot be pinned, because the whole point
//  is that it moves.
// ============================================================

#include "core/CameraEmulator.h"
#include "sky/SkyObject.h"

#include <cstdio>
#include <cmath>
#include <string>

using raduk::core::ASICamera;

static int g_fail = 0;

static void Near(double got, double want, double tol, const char* what) {
    if (std::fabs(got - want) <= tol) {
        printf("  ok    %s (%.5f)\n", what, got);
    } else {
        printf("  FAIL  %s: got %.5f, wanted %.5f +/- %.5f\n",
               what, got, want, tol);
        ++g_fail;
    }
}

static void Check(bool ok, const char* what) {
    if (ok) printf("  ok    %s\n", what);
    else  { printf("  FAIL  %s\n", what); ++g_fail; }
}

int main() {
    printf("Emulator target size and defocus scale\n");

    // ---- the table is still the fallback -----------------------
    printf("\n1. With no override, the table is in charge\n");
    {
        ASICamera cam;
        cam.SetTargetName("Jupiter");
        Near(cam.EffectiveRadiusArcsec(), 22.5, 1e-4,
             "Jupiter falls back to the table");
        cam.SetTargetName("Saturn");
        Near(cam.EffectiveRadiusArcsec(), 9.0, 1e-4,
             "and so does Saturn");
        cam.SetTargetName("Moon");
        Near(cam.EffectiveRadiusArcsec(), 900.0, 1e-4,
             "and the Moon, which has no live size until 94.3");
    }

    // ---- the override ------------------------------------------
    printf("\n2. A live size overrides the table\n");
    {
        ASICamera cam;
        cam.SetTargetName("Mars");
        Near(cam.EffectiveRadiusArcsec(), 7.5, 1e-4, "table first");
        cam.SetTargetAngularRadiusArcsec(3.25f);
        Near(cam.EffectiveRadiusArcsec(), 3.25, 1e-4,
             "then the live value, which today is less than half the table");

        // Zero is how a star or the Moon says "no ephemeris for me".
        cam.SetTargetAngularRadiusArcsec(0.0f);
        Near(cam.EffectiveRadiusArcsec(), 7.5, 1e-4,
             "zero hands it back to the table rather than rendering nothing");
        cam.SetTargetAngularRadiusArcsec(-5.0f);
        Near(cam.EffectiveRadiusArcsec(), 7.5, 1e-4,
             "and so does a negative, instead of a negative radius");
    }

    // ---- the one that would have been a silent fault -----------
    printf("\n3. Changing target clears the old target's size\n");
    {
        ASICamera cam;
        cam.SetTargetName("Jupiter");
        cam.SetTargetAngularRadiusArcsec(17.87f);
        Near(cam.EffectiveRadiusArcsec(), 17.87, 1e-4, "Jupiter, live");

        cam.SetTargetName("Mars");
        Near(cam.EffectiveRadiusArcsec(), 7.5, 1e-4,
             "switching to Mars does NOT keep Jupiter's radius");
        Check(cam.EffectiveRadiusArcsec() < 10.0,
              "which would otherwise have drawn Mars at over twice its size");

        // An unrecognised name falls back to target 0, and must clear too.
        cam.SetTargetAngularRadiusArcsec(99.0f);
        cam.SetTargetName("NGC 7000");
        Near(cam.EffectiveRadiusArcsec(), 22.5, 1e-4,
             "an unknown target resets to the default target AND clears");
    }

    // ---- substring matching, which the size lookup depends on ---
    printf("\n4. The name the ephemeris will be asked about\n");
    {
        ASICamera cam;
        // main.cpp reads TargetName() back rather than passing the
        // profile name on, because the ephemeris needs an exact match
        // and this is where the substring matching lives.
        cam.SetTargetName("Jupiter - GRS transit");
        Check(std::string(cam.TargetName()) == "Jupiter",
              "a decorated profile name resolves to the bare planet");
        cam.SetTargetName("saturn rings wide");
        Check(std::string(cam.TargetName()) == "Saturn",
              "and the match is case-insensitive");

        // And that resolved name is one the ephemeris answers for.
        const auto sz = raduk::sky::PlanetApparentSizeAt(cam.TargetName(),
                                                raduk::sky::CurrentJD());
        Check(sz.valid, "the resolved name is one PlanetApparentSizeAt knows");
        Check(sz.discArcsec > 0.0 && sz.discArcsec < 120.0,
              "and it returns a sane angular diameter for a planet");
    }

    // ---- what the ephemeris is actually for --------------------
    printf("\n5. The table is wrong because apparent size moves\n");
    {
        // Mars over one synodic cycle. The point is not the values,
        // which change daily, but that no single constant can serve:
        // if the spread is under 2x the table would be defensible.
        const double jd0 = 2460676.5;        // 2025-01-01
        double lo = 1e9, hi = 0.0;
        for (int d = 0; d < 800; d += 10) {
            const auto s = raduk::sky::PlanetApparentSizeAt("Mars", jd0 + d);
            if (!s.valid) continue;
            if (s.discArcsec < lo) lo = s.discArcsec;
            if (s.discArcsec > hi) hi = s.discArcsec;
        }
        Check(hi > 0.0 && lo < 1e9, "Mars resolves across the window");
        printf("  --    Mars spans %.2f\" to %.2f\" over 800 days\n", lo, hi);
        Check(hi / lo > 3.0,
              "and swings by more than 3x, so one constant cannot serve it");
    }

    // ---- the defocus scale -------------------------------------
    printf("\n6. Defocus blur is derived from the plate scale, not restated\n");
    {
        Near(ASICamera::DefaultBlurPxPerStep(),
             ASICamera::BLUR_ARCSEC_PER_STEP / ASICamera::EMU_PLATE_SCALE_AS_PX,
             1e-9, "the default is the quotient, by construction");
        Check(ASICamera::DefaultBlurPxPerStep() > 0.0f,
              "and it is positive");

        // The old literal, and why it was wrong once the scale moved.
        Near(ASICamera::BLUR_ARCSEC_PER_STEP / 0.61, 0.00832, 1e-5,
             "at the pre-91 scale of 0.61 the old literal 0.0083 was right");
        Check(std::fabs(ASICamera::DefaultBlurPxPerStep() - 0.0083f) > 0.01f,
              "at the current scale it is not, which is the bug this fixes");

        // The invariant: blur and disc are both angular, so their
        // ratio does not depend on the plate scale at all. This is
        // what the old comment's "~25 px at 3000 steps" failed to be.
        const double stepsOff = 3000.0;
        const double jupRadiusAs = 22.5;
        double ratioAt = -1.0;
        for (double scale : { 0.61, 0.223, 0.1, 0.05 }) {
            const double blurPx = stepsOff * ASICamera::BLUR_ARCSEC_PER_STEP / scale;
            const double discPx = 2.0 * jupRadiusAs / scale;
            const double ratio  = blurPx / discPx;
            if (ratioAt < 0.0) ratioAt = ratio;
            Near(ratio, ratioAt, 1e-12, "blur/disc is the same at every scale");
        }
        Near(ratioAt, 0.338, 0.001,
             "and at 3000 steps off focus it is a third of the disc");
    }

    if (g_fail == 0) printf("\nPASS\n"); else printf("\nFAIL: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
