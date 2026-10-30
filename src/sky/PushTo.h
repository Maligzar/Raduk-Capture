#pragma once
// ============================================================
// PushTo.h - "which way do I move the scope" (phase 30)
//
// For the sessions a GoTo mount cannot help with: the mount is not
// connected, the alignment is wrong, or the scope is being pushed
// by hand. Plate-solve, compare the answer to the target, and say
// which way to move. Solve again, say it again, until centred.
//
// The architecture note specifies the instruction as "Move N 0.5
// deg E 1.2 deg". That is the right answer for an equatorial
// mount and the wrong one for this rig, which is an alt-az 8SE:
// a person pushing it by hand moves it in altitude and azimuth,
// and converting celestial north-east to up-and-right in their
// head at the eyepiece is exactly the arithmetic this feature
// exists to remove. So both frames are produced. The equatorial
// pair is what the spec asked for and what a wedge or an EQ mount
// wants; the horizon pair is what the person at the tripod wants.
//
// Everything here is pure. Given two coordinates it returns an
// instruction, with no camera, no mount, no solver and no clock
// except the one the horizon conversion asks for. That is what
// lets tests/push_to.cpp check the geometry rather than the
// plumbing, and the geometry is where this is easy to get wrong.
//
// Three places it IS easy to get wrong, all pinned by tests:
//
//   An RA difference is not an angle on the sky until it is
//   multiplied by cos(dec). Near the pole a whole hour of RA is a
//   hand's width; at the equator it is fifteen degrees.
//
//   RA wraps at 24h. A target at 0h05m seen from 23h55m is ten
//   minutes east, not twenty-three hours and fifty west.
//
//   The per-axis numbers are a convenience, not the truth. Two
//   offsets that each look small can still be a large separation,
//   so the great-circle distance is computed independently rather
//   than from the components, and it is what decides "centred".
// ============================================================

#include "../core/SkyMath.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace raduk::sky {

struct PushToHint {
    bool   valid         = false;

    // The honest single number: great-circle angle between where
    // the scope is pointing and where it should point.
    double separationDeg = 0.0;

    // Equatorial breakdown. Positive north means move toward the
    // celestial pole; positive east means move toward increasing
    // RA. Already scaled by cos(dec), so both are on-sky degrees.
    double northDeg      = 0.0;
    double eastDeg       = 0.0;

    // Horizon breakdown, for the person with a hand on the tube.
    // Positive up means raise the altitude; positive right means
    // increase azimuth, which is clockwise seen from above.
    // False when the site is unset, because an alt-az answer from
    // a site of 0,0 is not an answer.
    bool   horizonValid  = false;
    double upDeg         = 0.0;
    double rightDeg      = 0.0;

    bool   centred       = false;
};

// Great-circle separation, via the haversine form.
//
// Not the cosine form: acos of a dot product loses precision for
// small angles, which is the whole range this feature operates in
// once the user is close. Haversine stays accurate there.
inline double AngularSeparationDeg(double ra1Hours, double dec1Deg,
                                   double ra2Hours, double dec2Deg) {
    const double D2R = 3.14159265358979 / 180.0;
    const double R2D = 180.0 / 3.14159265358979;

    const double d1 = dec1Deg * D2R;
    const double d2 = dec2Deg * D2R;
    const double dd = d2 - d1;
    const double dr = raduk::core::ShortestTurnDeg(ra1Hours * 15.0,
                                                   ra2Hours * 15.0) * D2R;

    const double a = std::sin(dd / 2.0) * std::sin(dd / 2.0)
                   + std::cos(d1) * std::cos(d2)
                   * std::sin(dr / 2.0) * std::sin(dr / 2.0);
    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return c * R2D;
}

// How close counts as centred, from the field the solve reported.
//
// A tenth of the frame scales with the optics, where a constant
// would be generous on a finder and useless at f/10. The floor
// stops a very narrow field demanding arcsecond precision from
// somebody pushing a tube by hand, and the fallback covers a
// solver that reported no field at all.
inline double CentredToleranceDeg(double solvedFovWDeg) {
    if (!(solvedFovWDeg > 0.0)) return 0.25;      // also catches NaN
    const double tol = solvedFovWDeg * 0.1;
    return tol < 0.05 ? 0.05 : tol;
}

// The instruction. curRa/curDec is where a plate solve says the
// scope is; tgtRa/tgtDec is where it should be.
//
// centredThresholdDeg is the radius inside which the caller should
// stop telling the user to move. It belongs to the caller because
// it is a property of the field of view, not of the geometry: a
// quarter-degree is centred for a finder and badly off for a
// planetary ROI.
inline PushToHint ComputePushTo(double curRaHours, double curDecDeg,
                                double tgtRaHours, double tgtDecDeg,
                                double centredThresholdDeg) {
    const double D2R = 3.14159265358979 / 180.0;
    PushToHint h;
    h.valid = true;

    h.separationDeg = AngularSeparationDeg(curRaHours, curDecDeg,
                                           tgtRaHours, tgtDecDeg);
    h.centred = (h.separationDeg <= centredThresholdDeg);

    h.northDeg = tgtDecDeg - curDecDeg;

    // cos of the mean declination rather than the current one. At
    // the separations this runs at the difference is negligible,
    // and the mean is symmetric: swapping current and target flips
    // the sign without changing the magnitude, which a reader of
    // the number will assume and which cos(current) would quietly
    // break.
    const double meanDec = 0.5 * (curDecDeg + tgtDecDeg);
    h.eastDeg = raduk::core::ShortestTurnDeg(curRaHours * 15.0,
                                             tgtRaHours * 15.0)
              * std::cos(meanDec * D2R);

    // The horizon pair needs a site and a clock. An unset site
    // reports nothing rather than reporting a confident wrong
    // direction, which is the failure that sends someone pushing
    // the scope the wrong way in the dark.
    if (raduk::core::RigConfig::Get().SiteIsSet()) {
        double curAlt = 0.0, curAz = 0.0, tgtAlt = 0.0, tgtAz = 0.0;
        raduk::core::RADecToAltAzNow(curRaHours, curDecDeg, curAlt, curAz);
        raduk::core::RADecToAltAzNow(tgtRaHours, tgtDecDeg, tgtAlt, tgtAz);

        h.horizonValid = true;
        h.upDeg    = tgtAlt - curAlt;
        const double meanAlt = 0.5 * (curAlt + tgtAlt);
        h.rightDeg = raduk::core::ShortestTurnDeg(curAz, tgtAz)
                   * std::cos(meanAlt * D2R);
    }

    return h;
}

// One line for the HUD. Written here rather than in the panel so
// the wording is covered by the same test as the arithmetic, and
// so the two frames cannot drift apart in how they are phrased.
inline std::string FormatPushTo(const PushToHint& h) {
    if (!h.valid)   return "no solve yet";
    if (h.centred)  return "centred";

    char buf[160];
    if (h.horizonValid) {
        snprintf(buf, sizeof(buf), "move %s %.2f deg, %s %.2f deg  (%.2f deg off)",
                 h.upDeg    >= 0 ? "UP"    : "DOWN",  std::fabs(h.upDeg),
                 h.rightDeg >= 0 ? "RIGHT" : "LEFT",  std::fabs(h.rightDeg),
                 h.separationDeg);
    } else {
        snprintf(buf, sizeof(buf), "move %s %.2f deg, %s %.2f deg  (%.2f deg off)",
                 h.northDeg >= 0 ? "N" : "S", std::fabs(h.northDeg),
                 h.eastDeg  >= 0 ? "E" : "W", std::fabs(h.eastDeg),
                 h.separationDeg);
    }
    return buf;
}

} // namespace raduk::sky
