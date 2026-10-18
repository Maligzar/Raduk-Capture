// ============================================================
//  Planet apparent size, against the sky's own numbers
//
//  The object panel said nothing about how much of the frame a planet
//  fills, because `size_arcmin` is NULL for every planet in the
//  knowledge database -- and it always will be, since a planet does
//  not have one. Mars runs 3.5" to 25" and the figure is only true
//  for a date.
//
//  So the panel computes it, and this checks the computation against
//  figures that do not come from this code: the published ranges of
//  apparent diameter and of Earth-planet distance. A wrong diameter
//  constant, an AU mixed up with a kilometre, or Earth placed on the
//  wrong side of the Sun all show up as a range violation, and none
//  of them would be visible in the panel -- a confident 8.3" for
//  Jupiter looks exactly like a correct one.
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

// Published extremes, from the Astronomical Almanac's tabulated
// ranges. Deliberately a little wider than the textbook figures: this
// is a simplified ephemeris (~1 arcmin on position) and the test is
// for blunders, not for arcsecond accuracy.
struct Expect {
    const char* name;
    double distMinAu, distMaxAu;   // Earth-planet
    double diamMinAs, diamMaxAs;   // apparent equatorial diameter
};

static const Expect kExpect[] = {
    { "Mercury", 0.52,  1.52,   4.4,  13.5 },
    { "Venus",   0.25,  1.76,   9.3,  67.0 },
    { "Mars",    0.36,  2.70,   3.4,  26.0 },
    { "Jupiter", 3.92,  6.48,  29.6,  50.5 },
    { "Saturn",  7.95, 11.10,  14.4,  21.0 },
    { "Uranus", 17.20, 21.20,   3.2,   4.2 },
    { "Neptune",28.70, 31.40,   2.1,   2.4 },
};

int main() {
    printf("\n=== Planet apparent size ===\n");

    // A sweep rather than one date. Any single date can be right by
    // luck, and the extremes are what catch a wrong constant.
    const double JD_2026_01_01 = CalcJD(2026, 1, 1.0);
    const int    DAYS = 366 * 12;      // twelve years, every day
    const int    STEP = 3;

    for (const Expect& e : kExpect) {
        printf("\n%s\n", e.name);
        double dMin = 1e9, dMax = -1e9, sMin = 1e9, sMax = -1e9;
        int samples = 0, invalid = 0;
        for (int d = 0; d < DAYS; d += STEP) {
            const PlanetApparentSize ps =
                PlanetSizeByName(e.name, JD_2026_01_01 + d);
            if (!ps.valid) { invalid++; continue; }
            samples++;
            dMin = (ps.distanceAu  < dMin) ? ps.distanceAu  : dMin;
            dMax = (ps.distanceAu  > dMax) ? ps.distanceAu  : dMax;
            sMin = (ps.globeArcsec < sMin) ? ps.globeArcsec : sMin;
            sMax = (ps.globeArcsec > sMax) ? ps.globeArcsec : sMax;
        }
        char buf[160];

        Check(invalid == 0 && samples > 1000, "every date in the sweep answers",
              (snprintf(buf, sizeof(buf), "%d samples, %d invalid",
                        samples, invalid), buf));

        Check(dMin >= e.distMinAu && dMax <= e.distMaxAu,
              "distance stays inside the published range",
              (snprintf(buf, sizeof(buf), "%.3f-%.3f AU, allowed %.2f-%.2f",
                        dMin, dMax, e.distMinAu, e.distMaxAu), buf));

        Check(sMin >= e.diamMinAs && sMax <= e.diamMaxAs,
              "apparent diameter stays inside the published range",
              (snprintf(buf, sizeof(buf), "%.2f-%.2f\", allowed %.1f-%.1f",
                        sMin, sMax, e.diamMinAs, e.diamMaxAs), buf));

        // The ranges above are only a real test if the sweep actually
        // reaches them. A planet stuck near mean distance would pass
        // every bound while being wrong.
        const double spread = (sMax - sMin) / sMin;
        Check(spread > 0.02, "the sweep covers a real spread, not one distance",
              (snprintf(buf, sizeof(buf), "%.0f%% between nearest and farthest",
                        spread * 100.0), buf));
    }

    // ── The relation the whole thing rests on ──
    printf("\nThe arithmetic\n");
    {
        const double jd = CalcJD(2026, 10, 18.0);
        const PlanetApparentSize j = PlanetSizeByName("Jupiter", jd);
        char buf[160];
        Check(j.valid, "Jupiter answers for today", "");
        // diameter = diameter at 1 AU / distance. Checked against the
        // table entry rather than recomputed from it, so a change to
        // one and not the other is caught.
        double expect = 0.0;
        for (const auto& p : kPlanets)
            if (std::string(p.name) == "Jupiter") expect = p.diamArcsec1AU / j.distanceAu;
        Check(std::fabs(j.globeArcsec - expect) < 1e-9,
              "disc is the 1 AU diameter over the distance",
              (snprintf(buf, sizeof(buf), "%.4f\" vs %.4f\"",
                        j.globeArcsec, expect), buf));

        // Rings. Saturn is the only one that has them here, and the
        // panel frames on the span -- 41" against an 18" globe is the
        // difference between a ringed planet and a cropped one.
        const PlanetApparentSize sa = PlanetSizeByName("Saturn", jd);
        Check(sa.valid && sa.ringsIncluded, "Saturn reports a ring span", "");
        Check(sa.extentArcsec > sa.globeArcsec * 2.2 &&
              sa.extentArcsec < sa.globeArcsec * 2.35,
              "the span is the A ring, not the globe",
              (snprintf(buf, sizeof(buf), "globe %.1f\", span %.1f\"",
                        sa.globeArcsec, sa.extentArcsec), buf));
        Check(sa.extentArcsec > 33.0 && sa.extentArcsec < 48.0,
              "and lands where Saturn with rings really does",
              (snprintf(buf, sizeof(buf), "%.1f\"", sa.extentArcsec), buf));

        // Everything else frames on the disc, so extent and globe are
        // the same number. A ring factor leaking onto Jupiter would
        // make the panel ask for a frame twice the size it needs.
        Check(!j.ringsIncluded && j.extentArcsec == j.globeArcsec,
              "Jupiter frames on its disc, with no ring span",
              (snprintf(buf, sizeof(buf), "%.2f\"", j.extentArcsec), buf));

        // A name that is not a planet is not an error, and must not
        // come back as a zero-sized object the panel would draw.
        for (const char* n : { "M13", "Vega", "", "Jupiter II" }) {
            const PlanetApparentSize no = PlanetSizeByName(n, jd);
            Check(!no.valid, std::string("'") + n + "' is not a planet", "");
        }

        // Spelled as the profiles and the combo spell it.
        Check(PlanetSizeByName("jupiter", jd).valid &&
              PlanetSizeByName("JUPITER", jd).valid,
              "the name match ignores case", "");
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
