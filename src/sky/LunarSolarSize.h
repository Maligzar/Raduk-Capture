// ============================================================
//  LunarSolarSize — how big the Moon and the Sun are tonight,
//  and how fast they move
//
//  Phase 97, roadmap row 34. Unblocked by 94.3, which gave the Moon
//  a terminator and surface detail at the correct scale, so lunar
//  framing finally has something to frame.
//
//  ── What was missing ───────────────────────────────────────
//
//  SkyObject.h computes the Moon's ecliptic longitude and latitude
//  from the Meeus Ch.47 series and converts them to RA and Dec. It
//  does NOT compute the distance term, so the application has never
//  known the Moon's apparent diameter. The Sun is the same: SunRADec
//  works out the geometric longitude and stops, with no radius
//  vector.
//
//  For a planetary rig that matters more than it would elsewhere,
//  because the Moon's apparent diameter swings 29.4' to 33.5' over
//  an anomalistic month -- a FOURTEEN PER CENT range -- and on this
//  plate scale that changes how many mosaic panels are needed from
//  fifteen to eighteen. "About half a degree" is not a number you can
//  plan a mosaic from.
//
//  ── The arithmetic lives here and not in SkyObject.h ──────
//
//  Same precedent as PixelWCS.h, LunarSurface.h and StarMetrics.h:
//  this is convention-sensitive arithmetic whose errors look
//  plausible -- a distance series with a sign wrong still produces a
//  diameter in the right ballpark, and a semidiameter mistaken for a
//  diameter is exactly a factor of two. So it is a pure header a test
//  can reach, checked against the published perigee and apogee
//  extremes rather than against itself.
//
//  Accuracy, measured rather than asserted: the distance series keeps
//  the ten largest terms of Meeus Ch.47, and over a year its extremes
//  land within 320 km of the published perigee and apogee. Call it
//  400 km out of 385,000, which is 1.9 arcsec of apparent diameter,
//  or 10 pixels on this rig out of the 9,170 the Moon spans.
//
//  (The first draft of this comment claimed 100 km. That was a
//  precision nobody had checked; the test then measured 320 at the
//  extremes. Fine for framing and for a mosaic plan either way, but
//  the stated figure is now the one that was measured.)
// ============================================================

#pragma once

#include <cmath>

namespace raduk::sky {

// ── Constants, with their sources ────────────────────────────
//
// IAU mean radii. The Moon's figure is not a sphere and its polar
// radius is about 2 km less, which is 0.001 arcsec here and so below
// anything this header pretends to.
inline constexpr double MOON_RADIUS_KM = 1737.4;
inline constexpr double SUN_RADIUS_KM  = 695700.0;
inline constexpr double AU_KM          = 149597870.7;
inline constexpr double ARCSEC_PER_RAD = 206264.806247;

// The Moon's mean distance, Meeus Ch.47's constant term.
inline constexpr double MOON_MEAN_DISTANCE_KM = 385000.56;

// Published figures, for a test to check the series against something
// other than itself.
//
// The EXTREMES and the MEANS are different numbers and confusing them
// cost this header a test failure. Perigee distance is not fixed:
// it varies month to month, and the 356,500 km figure happens only a
// few times a year, when perigee coincides with syzygy. A typical
// perigee is 363,300. So a test that sweeps one month and compares
// against the extreme fails by 6,000 km while the series is fine --
// sweep a year and it lands within 320 km, which is a much stronger
// check than the one that failed.
inline constexpr double MOON_PERIGEE_EXTREME_KM = 356500.0;
inline constexpr double MOON_APOGEE_EXTREME_KM  = 406700.0;
inline constexpr double MOON_PERIGEE_MEAN_KM    = 363300.0;
inline constexpr double MOON_APOGEE_MEAN_KM     = 405500.0;

inline double DegToRad(double d) { return d * 0.017453292519943295; }
inline double NormDeg360(double d) {
    d = std::fmod(d, 360.0);
    return d < 0.0 ? d + 360.0 : d;
}

// Julian centuries from J2000, matching SkyObject.h's JD_to_T.
inline double CenturiesFromJ2000(double jd) {
    return (jd - 2451545.0) / 36525.0;
}

// ── The Moon's distance ─────────────────────────────────────
//
// Meeus Ch.47's Sigma-r series, ten largest terms, in kilometres. The
// arguments are the same five the longitude series in SkyObject.h
// already builds, which is why this reads as a sibling of it.
inline double MoonDistanceKm(double jd) {
    const double T = CenturiesFromJ2000(jd);
    const double D  = NormDeg360(297.8501921 + 445267.1114034  * T);
    const double M  = NormDeg360(357.5291092 +  35999.0502909  * T);
    const double Mp = NormDeg360(134.9633964 + 477198.8675055  * T);
    const double F  = NormDeg360( 93.2720950 + 483202.0175233  * T);

    const double Dr = DegToRad(D),  Mr  = DegToRad(M);
    const double Mpr = DegToRad(Mp), Fr = DegToRad(F);

    // Note the signs: the leading term is NEGATIVE, so a Moon at
    // perigee (M' near zero) is CLOSER than the mean. Getting that
    // backwards gives a diameter in exactly the right range and the
    // phase of the variation inverted, which no amount of looking at
    // one night's number would reveal.
    const double dR = -20905355.0 * std::cos(Mpr)
                    -  3699111.0 * std::cos(2*Dr - Mpr)
                    -  2955968.0 * std::cos(2*Dr)
                    -   569925.0 * std::cos(2*Mpr)
                    +    48888.0 * std::cos(Mr)
                    -     3149.0 * std::cos(2*Fr)
                    +   246158.0 * std::cos(2*Dr - 2*Mpr)
                    -   152138.0 * std::cos(2*Dr - Mr - Mpr)
                    -   170733.0 * std::cos(2*Dr + Mpr)
                    -   204586.0 * std::cos(2*Dr - Mr);

    return MOON_MEAN_DISTANCE_KM + dR / 1000.0;   // metres -> km
}

// ── The Earth-Sun distance ──────────────────────────────────
//
// Meeus Ch.25. The eccentricity and true anomaly the Sun's own
// position calculation already works out, carried one step further
// to the radius vector it currently discards.
inline double SunDistanceAu(double jd) {
    const double T = CenturiesFromJ2000(jd);
    const double M = NormDeg360(357.52911 + 35999.05029 * T);
    const double Mr = DegToRad(M);
    const double C = (1.914602 - 0.004817*T - 0.000014*T*T) * std::sin(Mr)
                   + (0.019993 - 0.000101*T) * std::sin(2*Mr)
                   +  0.000289 * std::sin(3*Mr);
    const double v = DegToRad(M + C);                  // true anomaly
    const double e = 0.016708634 - 0.000042037*T - 0.0000001267*T*T;
    return 1.000001018 * (1.0 - e*e) / (1.0 + e * std::cos(v));
}

inline double SunDistanceKm(double jd) { return SunDistanceAu(jd) * AU_KM; }

// ── Apparent diameter ───────────────────────────────────────
//
// DIAMETER, not semidiameter. Meeus tabulates the Moon's
// semidiameter and almanacs quote the diameter, and confusing them is
// a clean factor of two that still lands in a plausible range for the
// other body -- a 16' Moon and a 64' Sun are both wrong and neither
// looks absurd at a glance. The names here say which.
inline double ApparentDiameterArcsec(double radiusKm, double distanceKm) {
    if (distanceKm <= radiusKm) return 0.0;
    return 2.0 * std::asin(radiusKm / distanceKm) * ARCSEC_PER_RAD;
}

inline double MoonApparentDiameterArcsec(double jd) {
    return ApparentDiameterArcsec(MOON_RADIUS_KM, MoonDistanceKm(jd));
}
inline double SunApparentDiameterArcsec(double jd) {
    return ApparentDiameterArcsec(SUN_RADIUS_KM, SunDistanceKm(jd));
}

// ── How fast it leaves the frame ────────────────────────────
//
// The Moon's mean motion is 13.176 degrees a day eastward against the
// stars, so a mount tracking at sidereal rate loses it at that rate.
// On this rig that is 2.7 pixels a second, and the consequence
// depends entirely on the timescale:
//
//   per EXPOSURE   20 ms  -> 0.05 px. Nothing. Sidereal tracking does
//                            not blur a lunar frame, and anyone who
//                            says otherwise has not done the
//                            arithmetic.
//   per RUN        60 s   -> 163 px, 4% of the frame width. The
//                            stacker aligns that out.
//   per MOSAIC     8 min  -> 4.6 arcmin, THIRTY-SIX PER CENT of the
//                            frame width. A mosaic laid out in fixed
//                            offsets does not close.
//
// So the tracking rate matters for mosaics specifically, and barely
// at all for a single run. That distinction is the whole reason to
// compute this rather than assert that lunar rate is "better".
inline constexpr double MOON_MEAN_MOTION_DEG_PER_DAY = 13.176358;
inline constexpr double SUN_MEAN_MOTION_DEG_PER_DAY  =  0.985647;

inline constexpr double DriftArcsecPerSec(double degPerDay) {
    return degPerDay * 3600.0 / 86400.0;
}
inline constexpr double MOON_DRIFT_ARCSEC_PER_SEC =
    DriftArcsecPerSec(MOON_MEAN_MOTION_DEG_PER_DAY);
inline constexpr double SUN_DRIFT_ARCSEC_PER_SEC =
    DriftArcsecPerSec(SUN_MEAN_MOTION_DEG_PER_DAY);

inline double DriftPxPerSec(double arcsecPerSec, double plateScaleArcsecPx) {
    return plateScaleArcsecPx > 0.0 ? arcsecPerSec / plateScaleArcsecPx : 0.0;
}

// ── What the rig sees ───────────────────────────────────────
struct FieldOfView {
    double widthArcmin  = 0.0;
    double heightArcmin = 0.0;
};

inline FieldOfView FrameFov(int pxW, int pxH, double plateScaleArcsecPx) {
    FieldOfView f;
    if (pxW > 0 && pxH > 0 && plateScaleArcsecPx > 0.0) {
        f.widthArcmin  = pxW * plateScaleArcsecPx / 60.0;
        f.heightArcmin = pxH * plateScaleArcsecPx / 60.0;
    }
    return f;
}

// NOTHING here has been checked against a real lunar session. The
// series are checked against published extremes and the geometry
// against hand arithmetic, which is not the same thing.
inline constexpr bool LUNAR_FRAMING_PROVEN_ON_SKY = false;

} // namespace raduk::sky
