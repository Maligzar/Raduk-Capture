// ============================================================
//  Solar and lunar framing (phase 97, roadmap row 34)
//
//  The owner raised this while planning 94: "the moon will be harder
//  because the size of my sensor will make the moon very large so
//  will need to account for that."
//
//  Measured on this rig -- 0.203 arcsec/px, 3840x2160 -- the full
//  sensor sees 12.99' x 7.31'. The Moon runs 29.4' to 33.5'. It is
//  two and a half frames wide and four and a half tall, it has never
//  fitted, and the application never said so because it never knew
//  how big the Moon was: SkyObject.h computes the Moon's longitude
//  and latitude and skips the distance term, and SunRADec stops
//  before the radius vector.
//
//  WHAT THIS FILE IS CHECKING AGAINST
//
//  A distance series can only be checked against something other
//  than itself, or the test just restates the code. So the Meeus
//  terms are checked against the PUBLISHED perigee and apogee
//  extremes, and the geometry against hand arithmetic. Two specific
//  errors this is built to catch:
//
//    - the leading distance term is NEGATIVE, so a Moon at perigee
//      is CLOSER than the mean. Flip that sign and the diameter is
//      still in exactly the right range with the variation inverted,
//      which one night's number would never reveal.
//
//    - semidiameter versus diameter is a clean factor of two, and a
//      16-arcmin Moon is not absurd at a glance.
//
//  And the mosaic's own trap: an RA offset without its cos(dec) is
//  correct on the equator and wrong everywhere else.
// ============================================================

#include "sky/MosaicPlan.h"
#include "core/IMount.h"

#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace raduk::sky;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (ok) printf("  ok    %s\n", what);
    else  { printf("  FAIL  %s\n", what); ++g_fail; }
}
static bool Near(double a, double b, double eps) {
    return std::fabs(a - b) <= eps;
}

// The rig, from rig.ini's defaults.
static constexpr double PX   = 0.203;
static constexpr int    SENW = 3840, SENH = 2160;

int main() {
    printf("Lunar and solar framing: how big, how fast, how many panels\n");

    // ───────────────────────────────────────────────────────────
    printf("\n1. The distance series, against published extremes\n");
    {
        // A YEAR, not a month, and the first draft of this test got
        // that wrong in an instructive way.
        //
        // Perigee distance is not fixed: it varies month to month, and
        // the published 356,500 km extreme happens only a few times a
        // year when perigee coincides with syzygy. A typical perigee
        // is 363,300. So sweeping 40 days and comparing against the
        // extreme failed by 6,000 km with nothing wrong in the series
        // at all -- it had simply not been given a month containing an
        // extreme perigee. Over a year it lands within 320 km, which
        // is a far tighter check than the one that failed.
        const double jd0 = 2460000.5;          // 2023-02-24
        double lo = 1e9, hi = -1e9, loJd = 0, hiJd = 0;
        for (double d = 0.0; d < 400.0; d += 0.02) {
            const double km = MoonDistanceKm(jd0 + d);
            if (km < lo) { lo = km; loJd = jd0 + d; }
            if (km > hi) { hi = km; hiJd = jd0 + d; }
        }
        printf("  --    over 400 days: %.0f km to %.0f km\n", lo, hi);
        printf("  --    published extremes: %.0f km to %.0f km\n",
               MOON_PERIGEE_EXTREME_KM, MOON_APOGEE_EXTREME_KM);
        // 500 km is 0.14%, which is what ten terms of the Meeus
        // series actually buys.
        Check(Near(lo, MOON_PERIGEE_EXTREME_KM, 500.0),
              "the year's minimum lands on the published extreme perigee"
              " to within 500 km");
        Check(Near(hi, MOON_APOGEE_EXTREME_KM, 500.0),
              "and the maximum on the extreme apogee");
        Check(hi > lo, "and they are the right way round");
        Check(lo < MOON_PERIGEE_MEAN_KM && hi > MOON_APOGEE_MEAN_KM,
              "the year's extremes sit outside the MEAN perigee and"
              " apogee, which is the distinction that broke the first"
              " version of this test");

        // ── The sign trap, which needs its OWN window ──────
        //
        // The leading distance term is negative, so a Moon at perigee
        // is closer than the mean. Flip that sign and the diameter is
        // still in exactly the right range with the variation
        // inverted -- one night's number would never show it.
        //
        // What pins the phase is that within ONE month the nearest and
        // furthest points are half a month apart. The year sweep above
        // cannot say that: its global extremes happen to be 207 days
        // apart, so a modulo comparison against it passes for the
        // wrong reason.
        double mLo = 1e9, mHi = -1e9, mLoJd = 0, mHiJd = 0;
        for (double d = 0.0; d < 27.55; d += 0.01) {
            const double km = MoonDistanceKm(jd0 + d);
            if (km < mLo) { mLo = km; mLoJd = jd0 + d; }
            if (km > mHi) { mHi = km; mHiJd = jd0 + d; }
        }
        const double sepDays = std::fabs(mHiJd - mLoJd);
        printf("  --    within one month, nearest to furthest:"
               " %.1f days (half an anomalistic month is %.1f)\n",
               sepDays, 27.55 / 2.0);
        Check(Near(sepDays, 27.55 / 2.0, 3.0),
              "half a month apart, which pins the SIGN of the leading"
              " term and not just its size");

        const double mean = MOON_MEAN_DISTANCE_KM;
        Check(lo < mean && hi > mean,
              "and the mean distance falls between them");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n2. Diameter, not semidiameter\n");
    {
        const double atMean = ApparentDiameterArcsec(MOON_RADIUS_KM,
                                                     MOON_MEAN_DISTANCE_KM);
        printf("  --    Moon at mean distance: %.1f arcsec = %.2f arcmin\n",
               atMean, atMean / 60.0);
        // The almanac figure for the mean apparent DIAMETER is 31.1';
        // the semidiameter is 15.5'. Half of this would be a clean,
        // plausible, wrong answer.
        Check(Near(atMean / 60.0, 31.0, 0.6),
              "about 31 arcmin, which is the diameter the almanacs give");
        Check(atMean / 60.0 > 20.0,
              "and emphatically not the ~15.5' semidiameter");

        const double peri = ApparentDiameterArcsec(MOON_RADIUS_KM,
                                              MOON_PERIGEE_EXTREME_KM);
        const double apo  = ApparentDiameterArcsec(MOON_RADIUS_KM,
                                              MOON_APOGEE_EXTREME_KM);
        printf("  --    perigee %.2f' , apogee %.2f'  (a %.0f%% swing)\n",
               peri / 60.0, apo / 60.0, 100.0 * (peri / apo - 1.0));
        Check(Near(peri / 60.0, 33.5, 0.4) && Near(apo / 60.0, 29.4, 0.4),
              "the extremes match the published 33.5' and 29.4'");
        Check(peri / apo > 1.10,
              "a swing over 10%, which is why 'about half a degree' is"
              " not a number you can plan a mosaic from");

        // The Sun, which is the other half of row 34 and a near-
        // coincidence worth pinning: both are close to half a degree,
        // so an error that swapped them would look fine.
        const double sun = SunApparentDiameterArcsec(2460000.5);
        printf("  --    Sun: %.1f arcsec = %.2f arcmin\n", sun, sun / 60.0);
        Check(Near(sun / 60.0, 32.0, 0.6),
              "the Sun is about 32 arcmin, as it should be");
        Check(SunDistanceAu(2460000.5) > 0.98 &&
              SunDistanceAu(2460000.5) < 1.02,
              "from an Earth-Sun distance within 2% of 1 AU");

        // Perihelion is early January, aphelion early July, and the
        // Sun is LARGER at perihelion. Another sign that would look
        // right if inverted.
        const double jan = SunApparentDiameterArcsec(2459951.5); // 2023-01-06
        const double jul = SunApparentDiameterArcsec(2460131.5); // 2023-07-05
        printf("  --    January %.1f\" vs July %.1f\"\n", jan, jul);
        Check(jan > jul,
              "and is bigger in January, at perihelion");

        Check(ApparentDiameterArcsec(MOON_RADIUS_KM, 0.0) == 0.0 &&
              ApparentDiameterArcsec(MOON_RADIUS_KM, 100.0) == 0.0,
              "a distance inside the body returns zero rather than NaN");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n3. What the rig sees, and the drift\n");
    {
        const FieldOfView fov = FrameFov(SENW, SENH, PX);
        printf("  --    full sensor: %.2f' x %.2f'\n",
               fov.widthArcmin, fov.heightArcmin);
        Check(Near(fov.widthArcmin, 12.99, 0.02) &&
              Near(fov.heightArcmin, 7.31, 0.02),
              "12.99' x 7.31' at 0.203 arcsec/px");
        Check(fov.widthArcmin * 60.0 < 31.0 * 60.0,
              "so the Moon has never fitted in one frame");

        printf("  --    lunar drift %.3f arcsec/s = %.2f px/s\n",
               MOON_DRIFT_ARCSEC_PER_SEC,
               DriftPxPerSec(MOON_DRIFT_ARCSEC_PER_SEC, PX));
        Check(Near(MOON_DRIFT_ARCSEC_PER_SEC, 0.549, 0.005),
              "0.55 arcsec/s, from 13.176 deg/day");
        Check(Near(DriftPxPerSec(MOON_DRIFT_ARCSEC_PER_SEC, PX), 2.71, 0.03),
              "2.71 px/s on this plate scale");
        Check(MOON_DRIFT_ARCSEC_PER_SEC > SUN_DRIFT_ARCSEC_PER_SEC * 10.0,
              "and the Moon drifts far faster than the Sun, so the two"
              " rates are not interchangeable");

        // The three timescales, which are the whole point. Conflating
        // them is how people come to believe sidereal tracking blurs a
        // lunar frame.
        const double perExposure = DriftArcsec(MOON_DRIFT_ARCSEC_PER_SEC, 0.020);
        const double perRun      = DriftArcsec(MOON_DRIFT_ARCSEC_PER_SEC, 60.0);
        printf("  --    20 ms: %.3f px | 60 s: %.0f px (%.0f%% of frame width)\n",
               perExposure / PX, perRun / PX, 100.0 * (perRun / PX) / SENW);
        Check(perExposure / PX < 0.1,
              "a 20 ms exposure smears 0.05 px, so sidereal tracking does"
              " NOT blur a lunar frame");
        Check((perRun / PX) / SENW < 0.05,
              "and a 60-second run drifts 4% of the frame, which the"
              " stacker aligns out");
        Check(DriftPxPerSec(MOON_DRIFT_ARCSEC_PER_SEC, 0.0) == 0.0,
              "a zero plate scale gives zero rather than dividing by it");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n4. The panel grid\n");
    {
        const FieldOfView fov = FrameFov(SENW, SENH, PX);

        // Not ceil(span/step). With n panels the covered extent is
        // (n-1)*step + frame, so a target exactly one frame wide needs
        // ONE panel and the naive form asks for two.
        Check(PanelsAcross(12.99, 12.99, 10.39) == 1,
              "a target exactly one frame wide needs one panel, not two");
        Check(PanelsAcross(13.10, 12.99, 10.39) == 2,
              "and a hair wider needs two");
        Check(PanelsAcross(5.0, 12.99, 10.39) == 1, "a small target, one");
        Check(PanelsAcross(31.0, 0.0, 0.0) == 1,
              "a degenerate frame does not loop or divide by zero");

        for (double ov : { 0.0, 0.20, 0.30 }) {
            const MosaicPlan p = PlanMosaic(31.0, fov, ov);
            printf("  --    Moon 31.0' at %2.0f%% overlap: %d x %d = %2d panels,"
                   " covers %.1f' x %.1f'\n", ov * 100, p.cols, p.rows,
                   p.panels, p.coveredWArcmin, p.coveredHArcmin);
            Check(p.coveredWArcmin >= 31.0 && p.coveredHArcmin >= 31.0,
                  "the grid covers the whole disc");
            Check(!p.singleFrame, "and knows it is not a single frame");
        }

        const MosaicPlan one = PlanMosaic(5.0, fov, 0.20);
        Check(one.singleFrame && one.panels == 1,
              "a target that fits is one frame and says so");

        // Overlap is clamped, not trusted: above a half the step goes
        // to nothing and the panel count runs away.
        const MosaicPlan wild = PlanMosaic(31.0, fov, 0.95);
        const MosaicPlan cap  = PlanMosaic(31.0, fov, MOSAIC_MAX_OVERLAP);
        Check(wild.panels == cap.panels && wild.panels < 200,
              "an absurd overlap clamps rather than exploding the count");
        Check(PlanMosaic(31.0, fov, -1.0).panels ==
              PlanMosaic(31.0, fov, 0.0).panels,
              "and a negative one clamps to butt-joined");
        Check(PlanMosaic(0.0, fov, 0.2).panels == 1 &&
              PlanMosaic(31.0, FieldOfView{}, 0.2).panels == 1,
              "no target or no frame degenerates safely");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n5. Where each panel points, and the serpentine\n");
    {
        const FieldOfView fov = FrameFov(SENW, SENH, PX);
        const MosaicPlan p = PlanMosaic(31.0, fov, 0.20);

        // Centred: the offsets must be symmetric about zero, or the
        // mosaic is built off to one side of the target.
        double sumE = 0.0, sumN = 0.0;
        for (int i = 0; i < p.panels; ++i) {
            const Panel q = PanelAt(p, i);
            sumE += q.eastArcsec; sumN += q.northArcsec;
        }
        Check(Near(sumE, 0.0, 0.01) && Near(sumN, 0.0, 0.01),
              "the offsets are symmetric about the target centre");

        // Every grid cell exactly once.
        int seen[64][64] = {};
        bool once = true;
        for (int i = 0; i < p.panels; ++i) {
            const Panel q = PanelAt(p, i);
            if (q.col < 0 || q.row < 0 || q.col > 63 || q.row > 63) once = false;
            else if (++seen[q.row][q.col] > 1) once = false;
        }
        Check(once, "and every cell of the grid is visited exactly once");

        // The serpentine itself. Raster order would fly the mount back
        // across the whole disc at the end of every row, and every
        // second of that is drift.
        const Panel endRow0   = PanelAt(p, p.cols - 1);
        const Panel startRow1 = PanelAt(p, p.cols);
        printf("  --    end of row 0 at east %+.0f\", start of row 1 at %+.0f\"\n",
               endRow0.eastArcsec, startRow1.eastArcsec);
        Check(Near(endRow0.eastArcsec, startRow1.eastArcsec, 0.01),
              "the next row starts where the last one ended, not back at"
              " the far side");
        Check(startRow1.northArcsec < endRow0.northArcsec,
              "one step south, since row 0 is the top");

        Check(PanelAt(p, -5).col == PanelAt(p, 0).col &&
              PanelAt(p, 9999).row == PanelAt(p, p.panels - 1).row,
              "an out-of-range index clamps");

        // ── When skipping the corners is worth anything ────
        //
        // The first draft of this test asserted that the bounding
        // grid's corners are empty sky. At the FULL SENSOR they are
        // not, and the reason is worth keeping: the frame is 13' wide
        // against a 31' Moon, so three columns cover it and the outer
        // two sit only 10.4' off-axis -- their near edges are well
        // inside the limb. A 3-wide grid has no corners to speak of.
        //
        // It pays at a small ROI, which is where someone might
        // reasonably expect it to matter and where the panel count is
        // the problem rather than the geometry.
        const int touching = PanelsTouchingDisc(p, fov, 31.0);
        printf("  --    full sensor: %d of %d panels touch the disc\n",
               touching, p.panels);
        Check(touching == p.panels,
              "at the full sensor EVERY panel holds Moon, because three"
              " 13-wide columns have no empty corners");
        Check(PanelTouchesDisc(p, PanelAt(p, p.cols / 2), fov, 31.0),
              "including the middle of the top row, obviously");

        const FieldOfView crop = FrameFov(640, 480, PX);
        const MosaicPlan pc = PlanMosaic(31.0, crop, 0.20);
        const int tc = PanelsTouchingDisc(pc, crop, 31.0);
        printf("  --    640x480 ROI: %d x %d = %d panels, %d touch,"
               " %d skippable\n", pc.cols, pc.rows, pc.panels, tc,
               pc.panels - tc);
        Check(tc < pc.panels,
              "at a 640x480 crop the corners ARE empty and skipping"
              " them saves real panels");

        // And the number that says not to do that at all.
        const double cropSecs = MosaicSeconds(tc, 20.0, 8.0);
        printf("  --    which would still be %.1f hours of capture\n",
               cropSecs / 3600.0);
        Check(cropSecs > 3600.0,
              "a lunar mosaic at a small ROI takes hours, so the answer"
              " is the full sensor, not a cleverer grid");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n6. cos(dec), the easiest thing to leave out\n");
    {
        const MountOffset eq = RaDecOffset(100.0, 50.0, 0.0);
        Check(Near(eq.raArcsec, 100.0, 0.01) && Near(eq.decArcsec, 50.0, 0.01),
              "on the equator an east offset is an RA offset unchanged");

        const MountOffset d60 = RaDecOffset(100.0, 50.0, 60.0);
        printf("  --    100\" east at dec 0 -> %.1f\" RA;"
               " at dec 60 -> %.1f\"\n", eq.raArcsec, d60.raArcsec);
        Check(Near(d60.raArcsec, 200.0, 0.5),
              "at declination 60 it is twice as much, which is the term"
              " that is correct on the equator and wrong everywhere else");
        Check(Near(d60.decArcsec, 50.0, 0.01),
              "while the Dec offset is untouched");

        // The Moon stays within about 28 degrees of the equator.
        const MountOffset d28 = RaDecOffset(100.0, 0.0, 28.0);
        printf("  --    at the Moon's extreme dec 28: %.1f\" (%.0f%% more)\n",
               d28.raArcsec, 100.0 * (d28.raArcsec / 100.0 - 1.0));
        Check(d28.raArcsec > 110.0,
              "even for the Moon it is a 13% error -- small enough to"
              " look like tracking and large enough to lose the overlap");

        const MountOffset pole = RaDecOffset(100.0, 0.0, 90.0);
        Check(std::isfinite(pole.raArcsec),
              "at the pole it clamps rather than returning infinity");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n7. THE FINDING: does the mosaic close under sidereal?\n");
    {
        const FieldOfView fov = FrameFov(SENW, SENH, PX);
        const MosaicPlan p = PlanMosaic(31.0, fov, 0.20);
        const double capture = 20.0, settle = 8.0;
        const double secs = MosaicSeconds(p.panels, capture, settle);
        const double drift = DriftArcsec(MOON_DRIFT_ARCSEC_PER_SEC, secs);

        printf("  --    %d panels at %.0f s + %.0f s settle = %.0f s"
               " (%.1f min)\n", p.panels, capture, settle, secs, secs / 60.0);
        printf("  --    drift over that: %.0f arcsec; the smaller overlap"
               " is %.0f arcsec\n", drift,
               p.overlapWArcsec < p.overlapHArcsec ? p.overlapWArcsec
                                                   : p.overlapHArcsec);
        Check(drift > (p.overlapWArcsec < p.overlapHArcsec
                       ? p.overlapWArcsec : p.overlapHArcsec),
              "the drift exceeds the ENTIRE overlap, so the last panel"
              " is not where the plan says");
        Check(!MosaicCloses(p, MOON_DRIFT_ARCSEC_PER_SEC, capture, settle),
              "and MosaicCloses says so BEFORE eight minutes are spent");

        // With the lunar rate the mount is following the Moon, so what
        // is left is the error in a mean rate against the true one,
        // which varies by a few per cent over the month.
        const double residual = MOON_DRIFT_ARCSEC_PER_SEC * 0.05;
        printf("  --    at lunar rate the residual is ~5%% of that:"
               " %.0f arcsec over the same run\n",
               DriftArcsec(residual, secs));
        Check(MosaicCloses(p, residual, capture, settle),
              "at lunar rate it closes, which is why phase 97 stopped"
              " pinning TrackingRate to sidereal");

        printf("  --    deadline under sidereal: %.0f s (%.1f min)\n",
               SecondsBeforeOverlapLost(p, MOON_DRIFT_ARCSEC_PER_SEC),
               SecondsBeforeOverlapLost(p, MOON_DRIFT_ARCSEC_PER_SEC) / 60.0);
        Check(SecondsBeforeOverlapLost(p, MOON_DRIFT_ARCSEC_PER_SEC) < secs,
              "the deadline is shorter than the mosaic takes");
        Check(SecondsBeforeOverlapLost(p, 0.0) > 1.0e8,
              "and with no drift there is no deadline");

        // Which overlap the budget uses, pinned explicitly rather
        // than left incidental. The drift is predominantly EAST so it
        // eats the horizontal overlap (156"), but the declination
        // component is real and not modelled, so the bound is the
        // smaller of the two on purpose.
        printf("  --    overlaps: %.0f\" wide, %.0f\" tall;"
               " budget %.0f\"\n", p.overlapWArcsec, p.overlapHArcsec,
               DriftBudgetArcsec(p));
        Check(DriftBudgetArcsec(p) ==
              (p.overlapHArcsec < p.overlapWArcsec ? p.overlapHArcsec
                                                   : p.overlapWArcsec)
              * OVERLAP_MARGIN,
              "the budget is half the SMALLER overlap, a conservative"
              " bound on an unmodelled drift direction");
        Check(DriftBudgetArcsec(p) < p.overlapWArcsec,
              "which is stricter than the horizontal one the east"
              " component alone would allow");

        // A single frame has no overlap to lose.
        const MosaicPlan single = PlanMosaic(5.0, fov, 0.20);
        Check(MosaicCloses(single, MOON_DRIFT_ARCSEC_PER_SEC, 60.0, 8.0),
              "a single frame always closes: there is nothing to join");

        Check(MosaicSeconds(1, 20.0, 8.0) == 20.0,
              "one panel is captured and never settles");
        Check(MosaicSeconds(0, 20.0, 8.0) == 0.0,
              "and no panels take no time");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n8. Honesty flags\n");
    {
        Check(!LUNAR_FRAMING_PROVEN_ON_SKY && !MOSAIC_PROVEN_ON_SKY,
              "both still declare themselves unproven on the sky, which"
              " they are: this is arithmetic against almanac figures");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n9. Which targets get which rate\n");
    {
        // IMount::RateForTarget is fed PROFILE names, which the owner
        // chooses. The first draft required exact equality, so a
        // session filed as "Moon - Copernicus" would have been handed
        // sidereal rate without a murmur -- the same silent near-miss
        // this phase spent its time hunting, reintroduced by the fix
        // for it.
        struct C { const char* name; int want; const char* why; };
        const C cases[] = {
            { "Moon",              1, "the bare catalogue name"        },
            { "moon",              1, "any case"                       },
            { "Moon - Copernicus", 1, "a profile named for a feature"   },
            { "Moon Tycho",        1, "or with a space"                 },
            { "Moon_waxing",       1, "or an underscore"                },
            { "Sun",               2, "the Sun"                         },
            { "Sun: white light",  2, "and a solar profile"             },
            { "Moonrise",          0, "but NOT a word merely starting so" },
            { "Sunset",            0, "nor this one"                    },
            { "Jupiter",           0, "a planet is sidereal"            },
            { "Vega",              0, "so is a star"                    },
            { "",                  0, "and an empty name"               },
        };
        bool allRight = true;
        for (const C& c : cases) {
            const int got = (int)raduk::core::IMount::RateForTarget(c.name);
            if (got != c.want) {
                printf("  --    %-18s wanted %d, got %d\n",
                       c.name, c.want, got);
                allRight = false;
            }
        }
        Check(allRight,
              "a word match with a separator: feature profiles get the"
              " right rate and 'Moonrise' does not");
        Check(raduk::core::IMount::RateForTarget(nullptr) ==
              raduk::core::IMount::RATE_SIDEREAL,
              "and a null name does not dereference it");
    }

    printf("\n%s\n", g_fail ? "FAILURES" : "all good");
    return g_fail ? 1 : 0;
}
