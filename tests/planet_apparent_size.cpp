// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// ============================================================
//  How big a planet looks tonight
//
//  The education panel's "does it fit in your frame" line never
//  appeared for the only objects this rig points at. size_arcmin is
//  NULL for every planet in the knowledge database, and that is the
//  right thing for the database to hold: a planet has no fixed
//  apparent size. Mars runs 3.5" to 25" over its synodic cycle, a
//  factor of seven, so any constant would be wrong nearly always.
//
//  It is computed instead, from the Earth-planet distance the
//  ephemeris was already working out and discarding.
//
//  The checks that matter here are not "does it return a number".
//  They are whether the number MOVES correctly over years, and
//  whether its extremes land where the published ranges say they
//  must. A frozen or mis-scaled distance still returns a plausible
//  number; it cannot reproduce Mars swinging by 7x.
// ============================================================

#include "sky/SkyObject.h"

#include <cstdio>
#include <string>
#include <cmath>

using namespace raduk::sky;

static int g_fail = 0;

static void Check(bool ok, const std::string& what, const std::string& detail) {
    printf("  [%s] %-56s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

// Sweep a planet over enough years to cover its synodic cycle and
// report the extremes of apparent disc diameter.
static void Sweep(const char* name, double& lo, double& hi,
                  double years = 16.0) {
    lo = 1e9; hi = -1e9;
    const double jd0 = CalcJD(2026, 1, 1.0);
    for (double d = 0.0; d < years * 365.25; d += 2.0) {
        const auto s = PlanetApparentSizeAt(name, jd0 + d);
        if (!s.valid) continue;
        if (s.discArcsec < lo) lo = s.discArcsec;
        if (s.discArcsec > hi) hi = s.discArcsec;
    }
}

// Published extremes of apparent equatorial diameter, arcseconds.
struct Known { const char* name; double lo, hi; };

int main() {
    printf("\n=== How big a planet looks tonight ===\n");

    // -- 1. The arithmetic, against a hand-worked value ------
    printf("\n1. Angular diameter from first principles\n");
    {
        // Jupiter at opposition sits about 4.2 AU away.
        //   71492 / (4.2 * 149597870.7) = 1.13784e-4 rad (half-angle)
        //   x2, x206265                 = 46.9 arcsec
        const double a = AngularDiameterArcsec(71492.0, 4.2);
        char d[96];
        snprintf(d, sizeof(d), "%.2f at 4.2 AU, hand-worked 46.9", a);
        Check(std::fabs(a - 46.9) < 0.3,
              "Jupiter at 4.2 AU subtends 46.9 arcsec", d);

        // Halve the distance, double the angle. Small-angle, but the
        // code uses atan, so this also says atan is not misapplied.
        const double near2 = AngularDiameterArcsec(71492.0, 2.1);
        snprintf(d, sizeof(d), "%.2f vs %.2f", near2, a);
        Check(std::fabs(near2 / a - 2.0) < 0.001,
              "and twice as big at half the distance", d);

        Check(AngularDiameterArcsec(71492.0, 0.0) == 0.0,
              "a zero distance yields nothing rather than infinity", "");
        Check(AngularDiameterArcsec(0.0, 4.2) == 0.0,
              "and so does a zero radius", "");
    }

    // -- 2. The extremes land where they are known to --------
    //
    // This is the real test. A wrong distance -- wrong units, wrong
    // sign on the Earth vector, a frozen value -- still produces a
    // believable arcsecond figure. It cannot produce the RIGHT
    // extremes over a full cycle.
    printf("\n2. Extremes over a full synodic cycle\n");
    {
        const Known kKnown[] = {
            {"Mercury",  4.5, 13.0},
            {"Venus",    9.7, 66.0},
            {"Mars",     3.5, 25.1},
            {"Jupiter", 29.8, 50.1},
            {"Saturn",  14.5, 20.1},
        };
        for (const auto& k : kKnown) {
            double lo = 0.0, hi = 0.0;
            Sweep(k.name, lo, hi);
            char d[128];
            snprintf(d, sizeof(d), "%.1f-%.1f, published %.1f-%.1f",
                     lo, hi, k.lo, k.hi);
            // 10% allows for a low-precision Kepler ephemeris and for
            // a 16-year window not hitting the single most favourable
            // opposition in a century.
            const bool ok = std::fabs(lo - k.lo) < k.lo * 0.10 + 0.4 &&
                            std::fabs(hi - k.hi) < k.hi * 0.10 + 0.4;
            Check(ok, std::string(k.name) + " spans its published range", d);
        }
    }

    // -- 3. It actually varies -------------------------------
    printf("\n3. The distance is live, not frozen\n");
    {
        double lo = 0.0, hi = 0.0;
        Sweep("Mars", lo, hi);
        char d[96];
        snprintf(d, sizeof(d), "%.2fx between closest and farthest", hi / lo);
        // Mars is the one that makes this worth computing at all.
        Check(hi / lo > 5.0,
              "Mars changes size by more than 5x, which is the point", d);

        double jlo = 0.0, jhi = 0.0;
        Sweep("Jupiter", jlo, jhi);
        snprintf(d, sizeof(d), "%.2fx", jhi / jlo);
        Check(jhi / jlo > 1.4 && jhi / jlo < 1.9,
              "and Jupiter by about 1.6x, not more", d);
    }

    // -- 4. Saturn is its rings ------------------------------
    printf("\n4. Saturn's rings, which are what has to fit\n");
    {
        const auto s = PlanetApparentSizeAt("Saturn", CalcJD(2026, 10, 17.0));
        char d[128];
        snprintf(d, sizeof(d), "disc %.1f, rings %.1f, ratio %.2f",
                 s.discArcsec, s.framingArcsec,
                 s.framingArcsec / s.discArcsec);
        Check(s.valid, "Saturn resolves", d);
        // 136775 / 60268 = 2.269. Framing on the disc alone understates
        // what must fit by more than double.
        Check(std::fabs(s.framingArcsec / s.discArcsec - 2.269) < 0.01,
              "the ring span is 2.27x the disc", d);

        // Every other planet frames on its own disc.
        const auto j = PlanetApparentSizeAt("Jupiter", CalcJD(2026, 10, 17.0));
        Check(j.framingArcsec == j.discArcsec,
              "and Jupiter frames on its disc, with no ring fudge", "");
    }

    // -- 5. Absence stays absence ----------------------------
    printf("\n5. Things that are not planets\n");
    {
        const auto m = PlanetApparentSizeAt("M42", CalcJD(2026, 10, 17.0));
        Check(!m.valid && m.discArcsec == 0.0,
              "a deep-sky object is not given a planet's size", "");
        const auto e = PlanetApparentSizeAt("", CalcJD(2026, 10, 17.0));
        Check(!e.valid, "and nor is an empty name", "");
        const auto mo = PlanetApparentSizeAt("Moon", CalcJD(2026, 10, 17.0));
        Check(!mo.valid,
              "the Moon is not in this ephemeris and does not pretend to be",
              "its distance comes from elsewhere");
    }

    // -- 6. The existing callers are untouched ---------------
    printf("\n6. The distance out-param is optional\n");
    {
        const double jd = CalcJD(2026, 10, 17.0);
        const PlanetElements* jup = nullptr;
        for (const auto& e : kPlanets)
            if (std::string("Jupiter") == e.name) jup = &e;

        double ra1 = 0.0, dec1 = 0.0;
        double ra2 = 0.0, dec2 = 0.0, dist = 0.0;
        PlanetRADec(*jup, jd, ra1, dec1);              // old call shape
        PlanetRADec(*jup, jd, ra2, dec2, &dist);       // new one

        char d[128];
        snprintf(d, sizeof(d), "RA %.6fh both ways, %.4f AU", ra1, dist);
        Check(ra1 == ra2 && dec1 == dec2,
              "asking for the distance does not change the position", d);
        Check(dist > 3.9 && dist < 6.5,
              "and the distance is in the right neighbourhood", d);
    }

    printf("\n");
    if (g_fail == 0)
        printf("=== the panel can say how big it looks ===\n\n");
    else
        printf("=== %d CHECK(S) FAILED ===\n\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
