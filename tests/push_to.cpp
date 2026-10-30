// ============================================================
//  Which way to move the scope (phase 30)
//
//  A plate solve says where the scope is pointing. The target
//  says where it should point. This is the arithmetic between
//  them, and it is worth testing on a bench because every way of
//  getting it wrong produces a confident number that sends
//  somebody pushing a telescope the wrong way in the dark.
//
//  Three failure modes are pinned here:
//
//    Forgetting cos(dec). An hour of RA is fifteen degrees at the
//    equator and a few arcminutes near the pole. A push-to that
//    says "move east 15 degrees" to a target four arcminutes away
//    is worse than saying nothing.
//
//    The wrap at 24h. From 23h55m to 0h05m is ten minutes east,
//    not nearly a full turn west.
//
//    Treating the per-axis numbers as the distance. They are a
//    convenience; the great-circle separation is the truth, and
//    it is what decides whether to stop.
//
//  The horizon pair (up/right) is what this rig actually needs,
//  since an 8SE is alt-az and a person pushing it moves it in
//  altitude and azimuth. It depends on the site and the clock, so
//  it is exercised through the behaviour that does not: that an
//  unset site reports nothing rather than guessing.
// ============================================================

#include "sky/PushTo.h"

#include <cstdio>
#include <cmath>
#include <string>

using namespace raduk::sky;

static int g_fail = 0;

static void Near(double got, double want, double tol, const char* what) {
    if (std::fabs(got - want) <= tol) {
        printf("  ok    %s (%.4f)\n", what, got);
    } else {
        printf("  FAIL  %s: got %.4f, wanted %.4f +/- %.4f\n", what, got, want, tol);
        ++g_fail;
    }
}

static void Check(bool ok, const char* what) {
    if (ok) printf("  ok    %s\n", what);
    else  { printf("  FAIL  %s\n", what); ++g_fail; }
}

int main() {
    printf("Push-to: which way to move the scope\n");

    // ---- separation ------------------------------------------
    printf("\n1. Great-circle separation\n");
    Near(AngularSeparationDeg(5.0, 20.0, 5.0, 20.0), 0.0, 1e-9,
         "a coordinate is zero from itself");
    Near(AngularSeparationDeg(0.0, 0.0, 0.0, 10.0), 10.0, 1e-6,
         "ten degrees of declination is ten degrees");
    Near(AngularSeparationDeg(0.0, 0.0, 1.0, 0.0), 15.0, 1e-6,
         "one hour of RA on the equator is fifteen degrees");

    // The one that catches a missing cos(dec). At dec 60 the same
    // hour of RA is half the angle.
    // 7.4839, not the 7.5 that 15*cos(60) suggests. The small-angle
    // form is an approximation and the great circle is slightly
    // shorter; the gap widens with separation. Pinned exactly so a
    // future rewrite to the cheaper formula shows up here.
    Near(AngularSeparationDeg(0.0, 60.0, 1.0, 60.0), 7.4839, 1e-3,
         "one hour of RA at dec 60 is 7.48 degrees, not 15");

    // Symmetry: distance does not care which way round it is asked.
    Near(AngularSeparationDeg(3.0, -20.0, 4.5, 11.0),
         AngularSeparationDeg(4.5, 11.0, 3.0, -20.0), 1e-9,
         "separation is symmetric");

    // ---- the wrap --------------------------------------------
    printf("\n2. RA wraps at 24h\n");
    Near(AngularSeparationDeg(23.9167, 0.0, 0.0833, 0.0), 2.5, 1e-3,
         "23h55m to 0h05m is 2.5 degrees, not 357.5");

    {
        PushToHint h = ComputePushTo(23.9167, 0.0, 0.0833, 0.0, 0.1);
        Check(h.eastDeg > 0.0, "and the direction across the wrap is east, not west");
        Near(h.eastDeg, 2.5, 1e-3, "with the right magnitude");
    }

    // ---- the components --------------------------------------
    printf("\n3. The per-axis breakdown\n");
    {
        PushToHint h = ComputePushTo(5.0, 20.0, 5.0, 22.0, 0.1);
        Near(h.northDeg, 2.0, 1e-9, "two degrees further north reads +2 north");
        Near(h.eastDeg,  0.0, 1e-9, "and nothing east");
        Check(!h.centred, "two degrees out is not centred");
    }
    {
        PushToHint h = ComputePushTo(5.0, 20.0, 5.0, 18.0, 0.1);
        Near(h.northDeg, -2.0, 1e-9, "two degrees south reads -2 north");
    }
    {
        // cos(dec) again, this time on the component rather than
        // the separation. 0.1h = 1.5 deg of RA; at dec 60 that is
        // 0.75 deg on the sky.
        PushToHint h = ComputePushTo(5.0, 60.0, 5.1, 60.0, 0.01);
        Near(h.eastDeg, 0.75, 1e-3, "an RA component is scaled by cos(dec)");
    }

    // ---- separation is not the sum of the parts ---------------
    printf("\n4. Separation is computed, not inferred\n");
    {
        PushToHint h = ComputePushTo(5.0, 20.0, 5.2, 23.0, 0.1);
        const double naive = std::fabs(h.northDeg) + std::fabs(h.eastDeg);
        Check(h.separationDeg < naive,
              "the great circle is shorter than adding the components");
        Check(h.separationDeg > std::fabs(h.northDeg),
              "and longer than either component alone");
    }

    // ---- the stop condition ----------------------------------
    printf("\n5. Centred\n");
    {
        PushToHint h = ComputePushTo(5.0, 20.0, 5.0, 20.0, 0.25);
        Check(h.centred, "on target is centred");
        Check(FormatPushTo(h) == "centred", "and says so");
    }
    {
        // Just inside and just outside a quarter-degree.
        PushToHint in  = ComputePushTo(5.0, 20.0, 5.0, 20.2, 0.25);
        PushToHint out = ComputePushTo(5.0, 20.0, 5.0, 20.3, 0.25);
        Check(in.centred,   "0.2 deg inside a 0.25 deg threshold is centred");
        Check(!out.centred, "0.3 deg outside it is not");
    }

    // ---- the wording -----------------------------------------
    printf("\n6. The line the user reads\n");
    {
        PushToHint h = ComputePushTo(5.0, 20.0, 5.0, 22.0, 0.1);
        const std::string s = FormatPushTo(h);
        Check(s.find("2.00") != std::string::npos, "the magnitude appears");
        Check(s.find("off")  != std::string::npos, "so does the total separation");
        // Which frame it names depends on whether a site is set,
        // and both are legitimate; what must never happen is a
        // direction word missing entirely.
        const bool hasDir = s.find("UP") != std::string::npos
                         || s.find("DOWN") != std::string::npos
                         || s.find("N ") != std::string::npos
                         || s.find("S ") != std::string::npos;
        Check(hasDir, "and a direction to move in");
    }
    {
        PushToHint none;
        Check(FormatPushTo(none) == "no solve yet",
              "before the first solve it says so rather than showing zeros");
    }

    // ---- the site guard --------------------------------------
    printf("\n7. An unset site does not guess\n");
    {
        // The default rig.ini in a bench run has no site, so the
        // horizon pair must be withheld. If a site IS configured
        // where this runs, the pair is valid instead; both are
        // correct, and what is being pinned is that the flag and
        // the numbers agree.
        PushToHint h = ComputePushTo(5.0, 20.0, 5.0, 22.0, 0.1);
        if (h.horizonValid) {
            printf("  --    a site is configured here; horizon pair active\n");
            Check(std::fabs(h.upDeg) + std::fabs(h.rightDeg) > 0.0,
                  "and it carries real numbers");
        } else {
            Check(h.upDeg == 0.0 && h.rightDeg == 0.0,
                  "no site, so no up/right numbers are offered");
            Check(FormatPushTo(h).find("UP") == std::string::npos,
                  "and the line falls back to the equatorial frame");
        }
    }

    if (g_fail == 0) printf("\nPASS\n"); else printf("\nFAIL: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
