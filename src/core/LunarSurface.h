#pragma once
// ============================================================
// LunarSurface.h - where the terminator is, and what the patch
//                  under the sensor looks like (phase 94.3)
//
// The emulator rendered the Moon as a flat tinted disc. At 900
// arcsec radius that disc overflows every sensor this rig owns,
// which is correct and was the point of phase 91 -- but a flat
// overflowing disc is a uniform bright rectangle, and a uniform
// rectangle has no detail at ANY focus position. For the one job
// the Moon does well indoors, standing in as a focus target, it
// was worse than a planet.
//
// What lunar focus is actually judged on is the TERMINATOR, where
// grazing sunlight throws shadows and gives the highest contrast
// edge in the solar system. So that is what this computes, and
// the rest of the surface is dressing around it.
//
// ── Why this is a patch and not a disc ───────────────────────
//
// At the emulator's 0.223 arcsec/px the full sensor spans 0.476
// of the lunar DIAMETER across and 0.268 down. A 640x480 planetary
// ROI spans 0.079 and 0.059. So the sensor sees the middle of the
// disc with the limb outside the frame, and every other target in
// the emulator fits inside the frame instead. That makes this a
// different rendering problem: a piece of a sphere, not a circle
// with things drawn on it.
//
// (The plan this came from said "roughly one eighth at full
// sensor". That was wrong, and wrong in the direction that
// mattered: at nearly half the diameter the terminator is often
// IN frame, which is the whole reason this is worth rendering.)
//
// ── What is deliberately not modelled ───────────────────────
//
// The POSITION ANGLE of the bright limb. On the sky the
// terminator's tilt depends on the Moon's orientation, the
// observer's latitude and the hour angle; here it runs along the
// sensor's x axis. Getting that right needs a parallactic angle
// this emulator has no way to check, and for a focus target the
// terminator's PRESENCE and SHARPNESS are what matter, not its
// tilt. Stated here rather than left for someone to discover.
//
// Libration, earthshine, opposition surge and the real albedo map
// are also absent. The maria are procedural: four large low
// contrast regions placed by hand so the field is recognisable
// rather than accurate.
// ============================================================

#include <cmath>
#include <cstdint>

namespace raduk::core {

// Direction to the Sun, in a frame where +z points at the
// observer, +x is sensor-right and +y is sensor-down.
//
// phaseFrac follows MoonRADec's convention: 0 new, 0.25 first
// quarter, 0.5 full, 0.75 last quarter.
//
// The Sun-Moon-Earth angle is |180 - 360*phase| degrees, so a full
// Moon has the Sun behind the observer and a new Moon behind the
// Moon. Which SIDE the lit limb falls on flips at full, which is
// the difference between waxing and waning.
struct SunDir { double x = 0.0, y = 0.0, z = 1.0; };

inline SunDir MoonSunDirection(double phaseFrac) {
    // Wrap, so a phase handed in slightly out of range does not
    // produce a Sun pointing somewhere impossible.
    double p = std::fmod(phaseFrac, 1.0);
    if (p < 0.0) p += 1.0;

    const double D2R   = 3.14159265358979 / 180.0;
    const double theta = std::fabs(180.0 - 360.0 * p) * D2R;

    // Waxing (p < 0.5) lights the side the Sun is leading toward;
    // waning lights the other. The absolute handedness cannot be
    // settled on a bench -- it needs one look at a real crescent --
    // so it is one sign in one place.
    const double side = (p < 0.5) ? +1.0 : -1.0;

    SunDir s;
    s.x = side * std::sin(theta);
    s.y = 0.0;
    s.z = std::cos(theta);
    return s;
}

// Illumination of the point on the sphere whose offset from disc
// centre is (u, v) in units of lunar radius, u right and v down.
//
// Returns the cosine of the incidence angle, clamped at zero, so
// the night side is 0 and the sub-solar point is 1. Negative
// return means off the disc entirely.
//
// The terminator is exactly where this crosses zero, which is why
// it is one expression and not a drawn line: a line would have to
// be kept in step with the shading, and the shading is what makes
// the edge look like an edge.
inline double LunarIllumination(double u, double v, const SunDir& s) {
    const double r2 = u * u + v * v;
    if (r2 > 1.0) return -1.0;                 // off the limb
    const double nz = std::sqrt(1.0 - r2);     // toward the observer
    const double c  = u * s.x + v * s.y + nz * s.z;
    return c > 0.0 ? c : 0.0;
}

// Selenographic latitude and longitude of that same point, in
// degrees, for placing surface features. Longitude is measured
// from the sub-observer meridian, so it is not true selenographic
// longitude -- libration is not modelled -- but it is stable and
// that is all a procedural albedo map needs.
inline void LunarLatLon(double u, double v, double& latDeg, double& lonDeg) {
    const double R2D = 180.0 / 3.14159265358979;
    const double r2  = u * u + v * v;
    const double nz  = (r2 < 1.0) ? std::sqrt(1.0 - r2) : 0.0;
    latDeg = std::asin(v > 1.0 ? 1.0 : (v < -1.0 ? -1.0 : v)) * R2D;
    // v is sensor-down, so positive v is southern. Flip so north
    // is positive, which is what anyone reading a lunar map wants.
    latDeg = -latDeg;
    lonDeg = std::atan2(u, nz) * R2D;
}

// ── Albedo ───────────────────────────────────────────────────
//
// Four maria, placed by hand at roughly the right selenographic
// coordinates and given a radius in degrees. Not a map: enough
// large dark regions that the field is recognisable and the
// sharpness metric has low-frequency structure to sit on, which a
// uniform surface denies it.
struct Mare { double lat, lon, radiusDeg, darken; };

inline constexpr int LUNAR_MARE_COUNT = 4;
inline const Mare LUNAR_MARIA[LUNAR_MARE_COUNT] = {
    //  lat    lon   radius  darken
    {  23.0, -31.0,  17.0,  0.34 },   // Imbrium
    {   8.0,  31.0,  15.0,  0.30 },   // Tranquillitatis
    { -13.0, -22.0,  12.0,  0.28 },   // Nubium / Humorum region
    {  17.0,  59.0,  11.0,  0.32 },   // Crisium
};

// Base reflectance at a point, before illumination. 1.0 is
// highland, lower is mare.
inline double LunarAlbedo(double latDeg, double lonDeg) {
    double a = 1.0;
    for (int i = 0; i < LUNAR_MARE_COUNT; ++i) {
        const Mare& m = LUNAR_MARIA[i];
        const double dLat = latDeg - m.lat;
        const double dLon = (lonDeg - m.lon) * std::cos(latDeg * 3.14159265358979 / 180.0);
        const double d    = std::sqrt(dLat * dLat + dLon * dLon);
        if (d < m.radiusDeg) {
            // Soft edge: maria do not have cliffs at their borders,
            // and a hard step would be a feature the sharpness
            // metric could lock onto that is not really there.
            const double t = 1.0 - (d / m.radiusDeg);
            const double w = t * t * (3.0 - 2.0 * t);     // smoothstep
            a -= m.darken * w;
        }
    }
    return a < 0.15 ? 0.15 : a;
}

// ── Craters ──────────────────────────────────────────────────
//
// Procedural, from a hash of a coarse lat/lon cell, so the same
// place always has the same crater and nothing moves between
// frames. Shadow length grows as the Sun gets lower, which is
// what makes the terminator the place to judge focus: the same
// crater is invisible at noon and a hard black crescent near
// sunrise.
//
// Returns a multiplier on the illuminated brightness: 1.0 for
// untouched ground, less inside a shadow, more on a sunlit rim.
inline double LunarCraterShade(double latDeg, double lonDeg,
                               const SunDir& s, double illum,
                               double cellDeg) {
    if (!(cellDeg > 0.0)) return 1.0;

    // Which cell, and a stable hash of it.
    const double cl = std::floor(latDeg / cellDeg);
    const double cn = std::floor(lonDeg / cellDeg);
    // Hashed in unsigned arithmetic. cellDeg is a parameter, so a
    // caller asking for fine cells pushes the cell index past the
    // point where a signed multiply overflows -- which is undefined,
    // not merely wrapped, and would be a crater field that changes
    // under optimisation.
    uint32_t h = (uint32_t)(int32_t)cl * 73856093u
               ^ (uint32_t)(int32_t)cn * 19349663u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;

    // Only some cells carry a crater, or the surface is gravel.
    if ((h & 3u) != 0u) return 1.0;

    // Position and size within the cell, from the same hash.
    const double fx = (double)((h >> 8)  & 255u) / 255.0;
    const double fy = (double)((h >> 16) & 255u) / 255.0;
    const double fr = (double)((h >> 24) & 255u) / 255.0;

    const double ccLat = (cl + fy) * cellDeg;
    const double ccLon = (cn + fx) * cellDeg;
    const double rad   = cellDeg * (0.18 + 0.30 * fr);

    const double dLat = latDeg - ccLat;
    const double dLon = (lonDeg - ccLon)
                      * std::cos(latDeg * 3.14159265358979 / 180.0);
    const double d    = std::sqrt(dLat * dLat + dLon * dLon);
    if (d > rad) return 1.0;

    // Where the Sun is coming from, projected into the lat/lon
    // plane. Only the horizontal component casts a visible shadow.
    const double sunLen = std::sqrt(s.x * s.x + s.y * s.y);
    if (sunLen < 1e-6) return 1.0;           // Sun overhead, no shadow
    const double sx = s.x / sunLen;

    // The low Sun near the terminator is what lengthens shadows.
    // illum is already the cosine of incidence, so (1 - illum) is
    // the grazing factor and goes to 1 exactly at the terminator.
    const double graze = 1.0 - illum;

    // Inside the crater: the side the Sun comes from is a lit rim,
    // the far side is in shadow. The sign of the along-Sun offset
    // decides which.
    const double along = (dLon * sx) / rad;        // -1 .. +1
    const double rim   = 1.0 - (d / rad);

    if (along < 0.0) {
        // Shadowed side. Deeper as the Sun gets lower.
        const double depth = 0.85 * graze * (-along) * rim;
        return 1.0 - (depth > 0.95 ? 0.95 : depth);
    }
    // Sunlit rim, brighter than the surrounding ground.
    const double lift = 0.55 * graze * along * rim;
    return 1.0 + lift;
}

} // namespace raduk::core
