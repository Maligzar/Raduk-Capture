#pragma once
// ============================================================
// SkyMath.h — Phase 67 step 5: one copy of the alt/az conversion
//
// This maths existed twice: as private statics in SkyMapWindow and,
// after Phase 67, again in MountEmulator. The cord-wrap predictor
// needs it a third time, which is the point at which duplication
// stops being tolerable — three copies drift, and a sky map that
// disagrees with a slew warning is worse than either alone.
//
// The site comes from RigConfig, so callers do not pass it around.
// ============================================================

#include <cmath>
#include <ctime>
#include "RigConfig.h"

namespace raduk::core {

inline double NormDeg360(double d) {
    d = std::fmod(d, 360.0);
    return d < 0 ? d + 360.0 : d;
}

// Shortest signed difference between two bearings, (-180, 180].
inline double ShortestTurnDeg(double fromDeg, double toDeg) {
    double d = NormDeg360(toDeg) - NormDeg360(fromDeg);
    while (d >  180.0) d -= 360.0;
    while (d <= -180.0) d += 360.0;
    return d;
}

inline double CurrentJulianDate() {
    return (double)time(nullptr) / 86400.0 + 2440587.5;
}

// Local sidereal time in hours for the configured site.
inline double LocalSiderealHours() {
    double jd   = CurrentJulianDate();
    double T    = (jd - 2451545.0) / 36525.0;
    double gmst = 280.46061837 + 360.98564736629 * (jd - 2451545.0)
                + 0.000387933 * T * T;
    return NormDeg360(gmst + RigConfig::Get().siteLonDeg) / 15.0;
}

// Alt/az for an equatorial coordinate at the configured site and the
// current time. Azimuth is degrees true, measured from north through
// east, matching what the mount reports.
inline void RADecToAltAzNow(double raHours, double decDeg,
                            double& altDeg, double& azDeg) {
    const double D2R = 3.14159265358979 / 180.0;
    const double R2D = 180.0 / 3.14159265358979;

    double lat = RigConfig::Get().siteLatDeg * D2R;
    double ha  = (LocalSiderealHours() - raHours) * 15.0 * D2R;
    double dec = decDeg * D2R;

    double sinAlt = std::sin(lat) * std::sin(dec)
                  + std::cos(lat) * std::cos(dec) * std::cos(ha);
    if (sinAlt >  1.0) sinAlt =  1.0;
    if (sinAlt < -1.0) sinAlt = -1.0;
    altDeg = std::asin(sinAlt) * R2D;

    // atan2 form rather than acos: it is stable near the zenith, where
    // the cosine form divides by a cosine approaching zero.
    double az = std::atan2(-std::cos(dec) * std::cos(lat) * std::sin(ha),
                           std::sin(dec) - std::sin(lat) * sinAlt);
    azDeg = NormDeg360(az * R2D);
}

// Is a bearing inside the arc running CLOCKWISE from start to end?
//
// Direction matters and cannot be inferred: an arc from 300 to 60 is
// a 120-degree sector across north, while 60 to 300 is the 240-degree
// sector the other way. Both are legitimate descriptions of an
// obstruction, so the convention is fixed as clockwise and the caller
// enters the edges in that order.
inline bool BearingInArc(double bearingDeg, double startDeg, double endDeg) {
    double b = NormDeg360(bearingDeg - startDeg);
    double e = NormDeg360(endDeg      - startDeg);
    if (e == 0.0) return false;          // empty arc, not a full circle
    return b <= e;
}

// Smallest turn from a bearing to the nearer edge of an arc. Negative
// when already inside, giving the depth of intrusion.
inline double DegreesOutsideArc(double bearingDeg, double startDeg, double endDeg) {
    if (BearingInArc(bearingDeg, startDeg, endDeg)) {
        double intoStart = NormDeg360(bearingDeg - startDeg);
        double toEnd     = NormDeg360(endDeg - bearingDeg);
        return -(intoStart < toEnd ? intoStart : toEnd);
    }
    double toStart = std::fabs(ShortestTurnDeg(bearingDeg, startDeg));
    double toEnd   = std::fabs(ShortestTurnDeg(bearingDeg, endDeg));
    return toStart < toEnd ? toStart : toEnd;
}

} // namespace raduk::core
