#pragma once
// ============================================================
// SkyObject.h — Sky object catalog for Phase 6 GoTo tab
//
// Provides:
//  - Solar System planet positions via simplified VSOP87
//    (accurate to ~1 arcmin for 2000-2100)
//  - Messier catalog (110 objects) with J2000 coordinates
//  - NGC bright object subset (selected visual targets)
//  - Search by name or RA/DEC
//
// All coordinates are J2000.0 (ICRS) RA in hours, Dec in degrees.
// ============================================================

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <ctime>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace raduk::sky {

struct SkyObject {
    std::string name;
    std::string type;       // "Planet", "Messier", "NGC", "Star"
    double      ra;         // hours, J2000
    double      dec;        // degrees, J2000
    float       magnitude;  // visual magnitude
    std::string notes;      // e.g. "Great Red Spot visible"

    // For sorting/display
    bool isPlanet() const { return type == "Planet"; }
};

// ── Julian Date helpers ────────────────────────────────────────

inline double CalcJD(int year, int month, double day) {
    if (month <= 2) { year--; month += 12; }
    int A = year / 100;
    int B = 2 - A + A / 4;
    return (int)(365.25 * (year + 4716)) + (int)(30.6001 * (month + 1))
           + day + B - 1524.5;
}

inline double CurrentJD() {
    time_t t = time(nullptr);
    struct tm* utc = gmtime(&t);
    double day = utc->tm_mday
               + utc->tm_hour / 24.0
               + utc->tm_min  / 1440.0
               + utc->tm_sec  / 86400.0;
    return CalcJD(utc->tm_year + 1900, utc->tm_mon + 1, day);
}

inline double JD_to_T(double jd) {
    return (jd - 2451545.0) / 36525.0; // Julian centuries from J2000.0
}

inline double NormDeg(double d) {
    d = fmod(d, 360.0);
    return d < 0 ? d + 360.0 : d;
}

inline double Rad(double d) { return d * M_PI / 180.0; }
inline double Deg(double r) { return r * 180.0 / M_PI; }

// ── Equinox conversion: of-date → J2000 (Phase 54 / review S5) ────
// The ephemeris formulas below produce equinox-of-date coordinates, but
// everything else in the app (Messier catalog, ASTAP plate solves, sync
// targets) is J2000 — by 2026 the difference is ~22', nearly 3x the
// solver FOV. Low-precision annual-rate precession is plenty here
// (error ~arcsec/decade): m = 46.124"/yr, n = 20.043"/yr.
inline void PrecessDateToJ2000(double jd, double& ra_h, double& dec_deg) {
    double Tyr  = (jd - 2451545.0) / 365.25;   // Julian years since J2000
    double raR  = Rad(ra_h * 15.0);
    double decC = dec_deg;
    if (decC >  89.0) decC =  89.0;            // tan() guard — planets/Moon
    if (decC < -89.0) decC = -89.0;            // never get near the pole
    double decR = Rad(decC);
    double dRA  = (46.124 + 20.043 * sin(raR) * tan(decR)) * Tyr; // arcsec
    double dDec = (20.043 * cos(raR)) * Tyr;                       // arcsec
    ra_h    = ra_h - dRA / 3600.0 / 15.0;
    if (ra_h < 0)   ra_h += 24.0;
    if (ra_h >= 24) ra_h -= 24.0;
    dec_deg = dec_deg - dDec / 3600.0;
}

// ── Simplified planet ephemeris ────────────────────────────────
// Based on Jean Meeus "Astronomical Algorithms" Ch.33
// Accuracy: ~1 arcmin for 2000-2050 — sufficient for GoTo pointing

struct PlanetElements {
    const char* name;
    float magnitude; // approximate visual magnitude at mean distance
    // Mean orbital elements at J2000.0 and their rates per century
    double L0, Lr;   // mean longitude (deg, deg/century)
    double a;        // semi-major axis (AU) — nearly constant
    double e0, er;   // eccentricity
    double i0, ir;   // inclination (deg, deg/century)
    double O0, Or;   // long. of ascending node (deg, deg/century)
    double w0, wr;   // long. of perihelion (deg, deg/century)
};

static const PlanetElements kPlanets[] = {
    // Mercury
    {"Mercury", -0.5f,
     252.250906, 149474.0722491,  0.387098, 0.20563175,  0.000020406,
       7.004986,    -0.0059516, 48.330893,  -0.1254229, 77.456119,   0.1588643},
    // Venus
    {"Venus", -4.4f,
     181.979801,  58519.2130302,  0.723330, 0.00677188, -0.000047766,
       3.394662,    -0.0008568, 76.679920,  -0.2780080,131.563707,   0.0048646},
    // Mars
    {"Mars", 0.5f,
     355.433275,  19141.6964746,  1.523679, 0.09340062,  0.000090483,
       1.849726,    -0.0081477, 49.558093,  -0.2949846,336.060234,   0.4439016},
    // Jupiter
    {"Jupiter", -2.7f,
      34.351519,   3036.3027748,  5.202603, 0.04849485,  0.000163244,
       1.303270,    -0.0019872,100.464441,   0.1766828, 14.331309,   0.2155209},
    // Saturn
    {"Saturn", 0.7f,
      50.077444,   1223.5110686,  9.554909, 0.05550825, -0.000346641,
       2.488878,     0.0025514,113.665524,  -0.2566649, 93.057136,   0.5665415},
    // Uranus
    {"Uranus", 5.7f,
     314.055005,    429.8640561, 19.218446, 0.04629590, -0.000027337,
       0.773196,    -0.0016869, 74.005947,   0.0741461,173.005159,   1.4862480},
    // Neptune
    {"Neptune", 8.0f,
     304.348665,    219.8833092, 30.110387, 0.00898809,  0.000006408,
       1.769952,     0.0002257,131.784057,  -0.0061651, 48.123691,   1.4262677},
};

// Compute heliocentric ecliptic longitude/latitude/radius from orbital elements
// Returns geocentric RA/Dec in J2000 (simplified — ignores proper aberration)
// distanceAU, when asked for, receives the Earth-planet distance. It
// was already being computed here -- it is the denominator of the dec
// line below -- and then discarded, which is why nothing in the app
// could say how big a planet looks tonight.
inline bool PlanetRADec(const PlanetElements& p, double jd,
                         double& ra_h, double& dec_deg,
                         double* distanceAU = nullptr) {
    double T = JD_to_T(jd);

    // Orbital elements at epoch
    double L = NormDeg(p.L0 + p.Lr * T);
    double e = p.e0 + p.er * T;
    double i = Rad(p.i0 + p.ir * T);
    double O = Rad(NormDeg(p.O0 + p.Or * T));
    double w = Rad(NormDeg(p.w0 + p.wr * T));
    double a = p.a;

    // Mean anomaly
    double M = Rad(NormDeg(L - Deg(w)));

    // Solve Kepler's equation (5 iterations sufficient)
    double E = M;
    for (int k = 0; k < 5; k++)
        E = E - (E - e * sin(E) - M) / (1.0 - e * cos(E));

    // Heliocentric rectangular coordinates (ecliptic plane)
    double xh = a * (cos(E) - e);
    double yh = a * sqrt(1.0 - e*e) * sin(E);

    // Rotate to ecliptic frame
    double cosO = cos(O), sinO = sin(O);
    double cosw = cos(w - O), sinw = sin(w - O);
    double cosi = cos(i), sini = sin(i);

    double x = xh * (cosO * cosw - sinO * sinw * cosi)
             - yh * (cosO * sinw + sinO * cosw * cosi);
    double y = xh * (sinO * cosw + cosO * sinw * cosi)
             - yh * (sinO * sinw - cosO * cosw * cosi);
    double z = xh * (sinw * sini) + yh * (cosw * sini);

    // Sun's geocentric ecliptic longitude (Ler) and distance (re).
    // (Xe, Ye) is therefore the GEOCENTRIC SUN vector, and Earth's
    // heliocentric position is its negative.
    double Le = NormDeg(280.46646 + 36000.76983 * T);
    double Me = Rad(NormDeg(357.52911 + 35999.05029 * T));
    double re = 1.000140 - 0.016708 * cos(Me) - 0.000141 * cos(2*Me);
    double Ler = Rad(Le + 1.9146 * sin(Me) + 0.01997 * sin(2*Me));
    double Xe = re * cos(Ler);
    double Ye = re * sin(Ler);

    // Geocentric ecliptic = helio_planet − helio_Earth = helio_planet + geo_Sun.
    // (The previous `x - Xe` subtracted the geocentric Sun — i.e. placed Earth
    // on the wrong side of the Sun — making planet RA/Dec wrong by up to tens
    // of degrees for the inner planets.)
    double gx = x + Xe;
    double gy = y + Ye;
    double gz = z;

    // Obliquity of ecliptic
    double eps = Rad(23.439291 - 0.013004 * T);

    // Convert to equatorial
    const double geoDist = sqrt(gx*gx + gy*gy + gz*gz);
    if (distanceAU) *distanceAU = geoDist;

    double ra  = atan2(gy * cos(eps) - gz * sin(eps), gx);
    double dec = asin((gy * sin(eps) + gz * cos(eps)) / geoDist);

    ra_h    = NormDeg(Deg(ra)) / 15.0; // hours
    dec_deg = Deg(dec);
    PrecessDateToJ2000(jd, ra_h, dec_deg); // of-date → J2000 (app standard)
    return true;
}

// ── How big a planet looks tonight ───────────────────────────────
//
// The education panel asks "does it fit in your frame", and for the
// only targets this rig actually points at -- Jupiter, Saturn, Mars --
// it could not answer. The knowledge database has size_arcmin NULL for
// every planet, and rightly so: a planet has no fixed apparent size.
// Mars runs from 3.5" to 25" depending on where the two orbits are,
// a factor of seven. A constant in a table would be wrong nearly all
// the time.
//
// So it is computed, from the distance the ephemeris already works out.
// Apparent diameter = 2 * atan(R / d), which is the whole of it.
//
// Equatorial radii, IAU 2015 nominal values, km.
struct PlanetBody { const char* name; double radiusKm; };

static const PlanetBody kPlanetBodies[] = {
    {"Mercury",  2439.7},
    {"Venus",    6051.8},
    {"Mars",     3396.2},
    {"Jupiter", 71492.0},
    {"Saturn",  60268.0},
    {"Uranus",  25559.0},
    {"Neptune", 24764.0},
};

// Outer edge of Saturn's A ring. The rings are what anyone at an
// eyepiece is actually looking at, and they span 2.27 times the disc,
// so framing advice based on the disc alone is wrong by more than
// double. The major axis is the full span whatever the tilt -- only
// the minor axis closes up when the rings go edge-on -- so this needs
// no ring-opening angle to answer "will it fit".
inline constexpr double SATURN_RING_OUTER_KM = 136775.0;

inline constexpr double AU_KM = 149597870.7;

struct PlanetApparentSize {
    double discArcsec    = 0.0;  // the globe, equatorial
    double framingArcsec = 0.0;  // what has to fit: rings, for Saturn
    double distanceAU    = 0.0;
    bool   valid         = false;
};

// Angular diameter of a body of radius R km at d AU.
inline double AngularDiameterArcsec(double radiusKm, double distAU) {
    if (radiusKm <= 0.0 || distAU <= 0.0) return 0.0;
    const double d = distAU * AU_KM;
    return 2.0 * atan(radiusKm / d) * 180.0 / M_PI * 3600.0;
}

inline PlanetApparentSize PlanetApparentSizeAt(const std::string& name,
                                               double jd) {
    PlanetApparentSize out;

    const PlanetBody* body = nullptr;
    for (const auto& b : kPlanetBodies)
        if (name == b.name) { body = &b; break; }
    if (!body) return out;            // not a planet; the caller falls back

    const PlanetElements* el = nullptr;
    for (const auto& e : kPlanets)
        if (name == e.name) { el = &e; break; }
    if (!el) return out;

    double ra = 0.0, dec = 0.0, dist = 0.0;
    if (!PlanetRADec(*el, jd, ra, dec, &dist)) return out;
    if (!(dist > 0.0)) return out;

    out.distanceAU    = dist;
    out.discArcsec    = AngularDiameterArcsec(body->radiusKm, dist);
    out.framingArcsec = (name == "Saturn")
                      ? AngularDiameterArcsec(SATURN_RING_OUTER_KM, dist)
                      : out.discArcsec;
    out.valid = out.discArcsec > 0.0;
    return out;
}

// ── Sun position (Meeus Ch.25, low-precision ~0.01°) ──────────────
inline void SunRADec(double jd, double& ra_h, double& dec_deg) {
    double T  = JD_to_T(jd);
    double L0 = NormDeg(280.46646 + 36000.76983 * T);
    double M  = Rad(NormDeg(357.52911 + 35999.05029 * T));
    double C  = (1.914602 - 0.004817*T - 0.000014*T*T) * sin(M)
              + (0.019993 - 0.000101*T) * sin(2*M)
              +  0.000289 * sin(3*M);
    double sunLon = Rad(NormDeg(L0 + C));
    double eps    = Rad(23.439291 - 0.013004 * T);
    ra_h    = NormDeg(Deg(atan2(cos(eps)*sin(sunLon), cos(sunLon)))) / 15.0;
    dec_deg = Deg(asin(sin(eps) * sin(sunLon)));
    PrecessDateToJ2000(jd, ra_h, dec_deg); // of-date → J2000 (app standard)
}

// ── Moon position (Meeus Ch.47, simplified ~0.1°) ─────────────────
// Also returns phase angle (0=new, 0.5=full, 1=new)
inline void MoonRADec(double jd, double& ra_h, double& dec_deg,
                      double& phaseAngle) {
    double T  = JD_to_T(jd);
    // Fundamental arguments (deg)
    double Lp = NormDeg(218.3164477 + 481267.88123421 * T); // mean long Moon
    double D  = NormDeg(297.8501921 + 445267.1114034  * T); // mean elongation
    double M  = NormDeg(357.5291092 +  35999.0502909  * T); // Sun mean anomaly
    double Mp = NormDeg(134.9633964 + 477198.8675055  * T); // Moon mean anomaly
    double F  = NormDeg( 93.2720950 + 483202.0175233  * T); // lat argument

    double Lr = Rad(Lp), Dr = Rad(D), Mr = Rad(M), Mpr = Rad(Mp), Fr = Rad(F);

    // Longitude corrections (arcsec → deg)
    double dL = 6288774*sin(Mpr)
              + 1274027*sin(2*Dr-Mpr)
              +  658314*sin(2*Dr)
              +  213618*sin(2*Mpr)
              -  185116*sin(Mr)
              -  114332*sin(2*Fr)
              +   58793*sin(2*Dr-2*Mpr)
              +   57066*sin(2*Dr-Mr-Mpr)
              +   53322*sin(2*Dr+Mpr)
              +   45758*sin(2*Dr-Mr);
    double lon = NormDeg(Lp + dL / 1000000.0);

    // Latitude corrections (arcsec → deg)
    double dB = 5128122*sin(Fr)
              +  280602*sin(Mpr+Fr)
              +  277693*sin(Mpr-Fr)
              +  173237*sin(2*Dr-Fr)
              +   55413*sin(2*Dr-Mpr+Fr)
              +   46271*sin(2*Dr-Mpr-Fr)
              +   32573*sin(2*Dr+Fr);
    double lat = dB / 1000000.0;

    // Convert ecliptic → equatorial
    double eps = Rad(23.439291 - 0.013004 * T);
    double lonR = Rad(lon), latR = Rad(lat);
    ra_h    = NormDeg(Deg(atan2(sin(lonR)*cos(eps) - tan(latR)*sin(eps),
                                cos(lonR)))) / 15.0;
    dec_deg = Deg(asin(sin(latR)*cos(eps) + cos(latR)*sin(eps)*sin(lonR)));
    PrecessDateToJ2000(jd, ra_h, dec_deg); // of-date → J2000 (app standard)

    // Phase: elongation of Moon from Sun
    double sunRA, sunDec;
    SunRADec(jd, sunRA, sunDec);
    double elong = NormDeg(lon - (sunRA * 15.0)); // approx ecliptic elongation
    phaseAngle = elong / 360.0; // 0=new, 0.25=first quarter, 0.5=full
}


// 110 objects with J2000 coordinates and visual magnitudes
// Coordinates: RA in decimal hours, Dec in decimal degrees

struct MessierEntry { int id; double ra; double dec; float mag; const char* type; const char* name; };

static const MessierEntry kMessier[] = {
    { 1, 5.575, +22.017,  8.4f, "SNR",  "Crab Nebula"},
    { 2, 21.558, -0.817,  6.5f, "GC",   ""},
    { 3, 13.703, +28.383,  6.2f, "GC",   ""},
    { 4, 16.393, -26.533,  5.6f, "GC",   ""},
    { 5, 15.310, +2.083,  5.6f, "GC",   ""},
    { 6, 17.668, -32.217,  4.2f, "OC",   "Butterfly Cluster"},
    { 7, 17.898, -34.817,  3.3f, "OC",   "Ptolemy Cluster"},
    { 8, 18.063, -24.383,  6.0f, "EN",   "Lagoon Nebula"},
    { 9, 17.320, -18.517,  7.7f, "GC",   ""},
    {10, 16.952, -4.100,  6.6f, "GC",   ""},
    {11, 18.852, -6.267,  5.8f, "OC",   "Wild Duck Cluster"},
    {12, 16.787, -1.950,  6.7f, "GC",   ""},
    {13, 16.695, +36.467,  5.8f, "GC",   "Hercules Cluster"},
    {14, 17.627, -3.250,  7.6f, "GC",   ""},
    {15, 21.500, +12.167,  6.2f, "GC",   ""},
    {16, 18.313, -13.800,  6.4f, "OC",   "Eagle Nebula"},
    {17, 18.347, -16.183,  7.0f, "EN",   "Omega Nebula"},
    {18, 18.333, -17.133,  6.9f, "OC",   ""},
    {19, 17.043, -26.267,  6.8f, "GC",   ""},
    {20, 18.043, -23.033,  8.5f, "EN",   "Trifid Nebula"},
    {21, 18.077, -22.500,  5.9f, "OC",   ""},
    {22, 18.607, -23.900,  5.1f, "GC",   ""},
    {23, 17.947, -19.017,  5.5f, "OC",   ""},
    {24, 18.283, -18.567,  4.6f, "SC",   "Sagittarius Star Cloud"},
    {25, 18.527, -19.233,  4.6f, "OC",   ""},
    {26, 18.757, -9.383,  8.0f, "OC",   ""},
    {27, 19.993, +22.717,  7.4f, "PN",   "Dumbbell Nebula"},
    {28, 18.410, -24.867,  6.8f, "GC",   ""},
    {29, 20.397, +38.533,  6.6f, "OC",   ""},
    {30, 21.672, -23.183,  7.2f, "GC",   ""},
    {31, 0.712, +41.267,  3.4f, "Gal",  "Andromeda Galaxy"},
    {32, 0.712, +40.867,  8.7f, "Gal",  ""},
    {33, 1.563, +30.650,  5.7f, "Gal",  "Triangulum Galaxy"},
    {34, 2.702, +42.767,  5.2f, "OC",   ""},
    {35, 6.148, +24.333,  5.1f, "OC",   ""},
    {36, 5.602, +34.133,  6.0f, "OC",   ""},
    {37, 5.873, +32.550,  5.6f, "OC",   ""},
    {38, 5.478, +35.833,  6.4f, "OC",   ""},
    {39, 21.532, +48.433,  4.6f, "OC",   ""},
    {40, 12.367, +58.083,  8.0f, "DS",   "Winnecke 4"},
    {41, 6.783, -20.733,  4.5f, "OC",   ""},
    {42, 5.588, -5.383,  4.0f, "EN",   "Orion Nebula"},
    {43, 5.592, -5.267,  9.0f, "EN",   "De Mairan's Nebula"},
    {44, 8.670, +19.983,  3.1f, "OC",   "Beehive Cluster"},
    {45, 3.788, +24.117,  1.6f, "OC",   "Pleiades"},
    {46, 7.697, -14.817,  6.1f, "OC",   ""},
    {47, 7.610, -14.500,  4.4f, "OC",   ""},
    {48, 8.230, -5.800,  5.8f, "OC",   ""},
    {49, 12.497, +8.000,  8.4f, "Gal",  ""},
    {50, 7.053, -8.367,  5.9f, "OC",   ""},
    {51, 13.498, +47.183,  8.4f, "Gal",  "Whirlpool Galaxy"},
    {52, 23.403, +61.583,  6.9f, "OC",   ""},
    {53, 13.215, +18.167,  7.6f, "GC",   ""},
    {54, 18.917, -30.483,  7.6f, "GC",   ""},
    {55, 19.667, -30.967,  6.3f, "GC",   ""},
    {56, 19.277, +30.183,  8.3f, "GC",   ""},
    {57, 18.893, +33.033,  8.8f, "PN",   "Ring Nebula"},
    {58, 12.628, +11.817,  9.7f, "Gal",  ""},
    {59, 12.700, +11.650,  9.6f, "Gal",  ""},
    {60, 12.727, +11.550,  8.8f, "Gal",  ""},
    {61, 12.367, +4.467,  9.7f, "Gal",  ""},
    {62, 17.023, -30.117,  6.5f, "GC",   ""},
    {63, 13.263, +42.033,  8.6f, "Gal",  "Sunflower Galaxy"},
    {64, 12.945, +21.683,  8.5f, "Gal",  "Black Eye Galaxy"},
    {65, 11.315, +13.083,  9.3f, "Gal",  ""},
    {66, 11.337, +12.983,  8.9f, "Gal",  ""},
    {67, 8.840, +11.817,  6.9f, "OC",   ""},
    {68, 12.657, -26.750,  7.6f, "GC",   ""},
    {69, 18.523, -32.350,  7.6f, "GC",   ""},
    {70, 18.720, -32.283,  7.9f, "GC",   ""},
    {71, 19.897, +18.783,  6.1f, "GC",   ""},
    {72, 20.892, -12.533,  9.3f, "GC",   ""},
    {73, 20.983, -12.633,  9.0f, "OC",   ""},
    {74, 1.612, +15.783,  9.4f, "Gal",  ""},
    {75, 20.102, -21.917,  8.5f, "GC",   ""},
    {76, 1.703, +51.567, 10.1f, "PN",   "Little Dumbbell"},
    {77, 2.712, -0.017,  8.9f, "Gal",  ""},
    {78, 5.778, +0.083,  8.3f, "EN",   ""},
    {79, 5.402, -24.517,  7.7f, "GC",   ""},
    {80, 16.283, -22.967,  7.3f, "GC",   ""},
    {81, 9.927, +69.067,  6.9f, "Gal",  "Bode's Galaxy"},
    {82, 9.932, +69.683,  8.4f, "Gal",  "Cigar Galaxy"},
    {83, 13.617, -29.867,  7.5f, "Gal",  "Southern Pinwheel"},
    {84, 12.418, +12.883,  9.1f, "Gal",  ""},
    {85, 12.423, +18.183,  9.2f, "Gal",  ""},
    {86, 12.437, +12.950,  8.9f, "Gal",  ""},
    {87, 12.512, +12.383,  8.6f, "Gal",  "Virgo A"},
    {88, 12.532, +14.417,  9.6f, "Gal",  ""},
    {89, 12.593, +12.567,  9.7f, "Gal",  ""},
    {90, 12.613, +13.167,  9.5f, "Gal",  ""},
    {91, 12.595, +14.500,  9.9f, "Gal",  ""},
    {92, 17.285, +43.133,  6.4f, "GC",   ""},
    {93, 7.742, -23.867,  6.2f, "OC",   ""},
    {94, 12.852, +41.117,  8.2f, "Gal",  ""},
    {95, 10.733, +11.700,  9.7f, "Gal",  ""},
    {96, 10.777, +11.817,  9.2f, "Gal",  ""},
    {97, 11.248, +55.017,  9.9f, "PN",   "Owl Nebula"},
    {98, 12.228, +14.900,  9.9f, "Gal",  ""},
    {99, 12.313, +14.417,  9.9f, "Gal",  ""},
    {100, 12.382, +15.817,  9.4f, "Gal",  ""},
    {101, 14.053, +54.350,  7.9f, "Gal",  "Pinwheel Galaxy"},
    {102, 15.108, +55.767,  9.9f, "Gal",  ""},
    {103, 1.557, +60.650,  7.4f, "OC",   ""},
    {104, 12.667, -11.617,  8.0f, "Gal",  "Sombrero Galaxy"},
    {105, 10.797, +12.583,  9.8f, "Gal",  ""},
    {106, 12.317, +47.300,  8.4f, "Gal",  ""},
    {107, 16.543, -13.050,  7.9f, "GC",   ""},
    {108, 11.192, +55.667, 10.0f, "Gal",  ""},
    {109, 11.958, +53.383,  9.8f, "Gal",  ""},
    {110, 0.673, +41.683,  8.0f, "Gal",  ""},
};


// ── Bright Star Catalog — 255 stars mag <= 4.5 ────────────────
// J2000 coordinates: ra (decimal hours), dec (decimal degrees)
// Source: Yale BSC / Hipparcos — accurate to <1 arcmin for J2000
// Sorted by magnitude (brightest first)
struct BrightStarEntry {
    const char* name;
    double      ra;   // hours
    double      dec;  // degrees
    float       mag;
    const char* constellation; // for context labels
};

static const BrightStarEntry kBrightStars[] = {
    // The 50 most prominent stars — covers all naked-eye reference stars
    // near planets and useful for alignment
    {"Sirius",       6.7525,  -16.716,  -1.46f, "CMa"},
    {"Canopus",      6.3992,  -52.695,  -0.74f, "Car"},
    {"Arcturus",    14.2613,  +19.182,  -0.05f, "Boo"},
    {"Vega",        18.6157,  +38.784,   0.03f, "Lyr"},
    {"Capella",      5.2783,  +45.998,   0.08f, "Aur"},
    {"Rigel",        5.2422,  -8.202,    0.13f, "Ori"},
    {"Procyon",      7.6550,  +5.225,    0.34f, "CMi"},
    {"Betelgeuse",   5.9192,  +7.407,    0.42f, "Ori"},
    {"Achernar",     1.6285,  -57.237,   0.46f, "Eri"},
    {"Hadar",       14.0637,  -60.373,   0.61f, "Cen"},
    {"Altair",      19.8463,  +8.868,    0.76f, "Aql"},
    {"Acrux",       12.4433,  -63.099,   0.77f, "Cru"},
    {"Aldebaran",    4.5987,  +16.509,   0.87f, "Tau"},
    {"Antares",     16.4901,  -26.432,   0.96f, "Sco"},
    {"Spica",       13.4199,  -11.161,   0.98f, "Vir"},
    {"Pollux",       7.7553,  +28.026,   1.16f, "Gem"},
    {"Fomalhaut",   22.9608,  -29.622,   1.16f, "PsA"},
    {"Deneb",       20.6905,  +45.280,   1.25f, "Cyg"},
    {"Mimosa",      12.7954,  -59.689,   1.25f, "Cru"},
    {"Regulus",     10.1395,  +11.967,   1.36f, "Leo"},
    {"Adhara",       6.9771,  -28.972,   1.50f, "CMa"},
    {"Castor",       7.5767,  +31.888,   1.58f, "Gem"},
    {"Shaula",      17.5603,  -37.103,   1.62f, "Sco"},
    {"Gacrux",      12.5193,  -57.114,   1.63f, "Cru"},
    {"Bellatrix",    5.4188,  +6.349,    1.64f, "Ori"},
    {"Elnath",       5.4382,  +28.608,   1.65f, "Tau"},
    {"Miaplacidus",  9.2200,  -69.717,   1.67f, "Car"},
    {"Alnilam",      5.6036,  -1.202,    1.70f, "Ori"},
    {"Al Nair",     22.1374,  -46.961,   1.73f, "Gru"},
    {"Alnitak",      5.6791,  -1.943,    1.77f, "Ori"},
    {"Mirfak",       3.4082,  +49.861,   1.79f, "Per"},
    {"Regor",        8.1590,  -47.337,   1.83f, "Vel"},
    {"Dubhe",       11.0621,  +61.751,   1.81f, "UMa"},
    {"Wezen",        7.1398,  -26.393,   1.84f, "CMa"},
    {"Kaus Australis",18.4028,-34.385,   1.85f, "Sgr"},
    {"Avior",        8.3748,  -59.510,   1.86f, "Car"},
    {"Alkaid",      13.7923,  +49.313,   1.85f, "UMa"},
    {"Sargas",      17.6217,  -42.998,   1.86f, "Sco"},
    {"Menkent",     14.1113,  -36.370,   2.06f, "Cen"},
    {"Atria",       16.8111,  -69.028,   1.91f, "TrA"},
    {"Peacock",     20.4276,  -56.735,   1.94f, "Pav"},
    {"Merak",       11.0307,  +56.382,   2.37f, "UMa"},
    {"Phecda",      11.8968,  +53.695,   2.44f, "UMa"},
    {"Megrez",      12.2572,  +57.033,   3.31f, "UMa"},
    {"Alioth",      12.9004,  +55.960,   1.76f, "UMa"},
    {"Mizar",       13.3988,  +54.926,   2.23f, "UMa"},
    {"Denebola",    11.8177,  +14.572,   2.14f, "Leo"},
    {"Algieba",     10.3329,  +19.842,   2.28f, "Leo"},
    {"Zosma",       11.2352,  +20.524,   2.56f, "Leo"},
    // Zodiac belt — key for planetary context
    {"Nunki",       18.9211,  -26.296,   2.02f, "Sgr"},
    {"Ascella",     19.0432,  -29.880,   2.60f, "Sgr"},
    {"Kaus Media",  18.3503,  -29.828,   2.70f, "Sgr"},
    {"Kaus Borealis",18.9167,-25.421,    2.81f, "Sgr"},
    {"Porrima",     12.6942,  -1.449,    2.74f, "Vir"},
    {"Vindemiatrix", 13.0362, +10.959,   2.85f, "Vir"},
    {"Sadalsuud",   21.5260,  -5.571,    2.90f, "Aqr"},
    {"Sadalmelik",  22.0961,  -0.320,    2.95f, "Aqr"},
    {"Skat",        22.9109,  -15.821,   3.27f, "Aqr"},
    {"Nashira",     21.6685,  -16.662,   3.68f, "Cap"},
    {"Dabih",       20.3503,  -14.782,   3.05f, "Cap"},
    {"Algedi",      20.3000,  -12.544,   3.57f, "Cap"},
    // Aquila region (near Jupiter's common apparition zone)
    {"Tarazed",     19.7711,  +10.613,   2.72f, "Aql"},
    {"Alshain",     19.9217,  +6.407,    3.71f, "Aql"},
    // Gemini/Taurus (Jupiter/Saturn frequent)
    {"Propus",       6.2415,  +22.514,   3.28f, "Gem"},
    {"Mebsuda",      6.7328,  +25.131,   3.06f, "Gem"},
    {"Wasat",        7.3348,  +21.982,   3.54f, "Gem"},
    {"Tejat Prior",  6.2822,  +22.514,   3.15f, "Gem"},
    {"Alhena",       6.6285,  +16.399,   1.93f, "Gem"},
    {"Mekbuda",      7.0682,  +20.570,   3.78f, "Gem"},
    // Cancer / Virgo (planet zone)
    {"Acubens",      8.9745,  +11.858,   4.26f, "Cnc"},
    {"Asellus Australis",8.7448,+18.154, 3.94f, "Cnc"},
    {"Asellus Borealis", 8.7210,+21.469, 4.66f, "Cnc"},
    // Scorpius (near ecliptic)
    {"Dschubba",    16.0050,  -22.622,   2.29f, "Sco"},
    {"Graffias",    15.9484,  -19.805,   2.62f, "Sco"},
    {"Acrab",       15.9484,  -19.805,   2.62f, "Sco"},
    {"Alniyat",     16.3548,  -25.593,   2.89f, "Sco"},
    {"Lesath",      17.5313,  -37.296,   2.70f, "Sco"},
    {"Girtab",      17.6217,  -42.998,   2.41f, "Sco"},
    // Bright reference stars for alignment
    {"Schedar",      0.6751,  +56.537,   2.24f, "Cas"},
    {"Caph",         0.1529,  +59.150,   2.28f, "Cas"},
    {"Ruchbah",      1.4304,  +60.235,   2.66f, "Cas"},
    {"Navi",         0.9453,  +60.717,   2.15f, "Cas"},
    {"Segin",        1.9067,  +63.670,   3.37f, "Cas"},
    {"Polaris",      2.5303,  +89.264,   1.97f, "UMi"},
    {"Kochab",      14.8451,  +74.156,   2.08f, "UMi"},
    {"Pherkad",     15.3454,  +71.834,   3.05f, "UMi"},
    {"Cor Caroli",  12.9338,  +38.318,   2.89f, "CVn"},
    {"Izar",        14.7498,  +27.074,   2.35f, "Boo"},
    {"Muphrid",     13.9115,  +18.398,   2.68f, "Boo"},
    {"Nekkar",      15.0325,  +40.390,   3.49f, "Boo"},
    {"Alphecca",    15.5782,  +26.715,   2.22f, "CrB"},
    {"Nusakan",     15.4636,  +29.106,   3.65f, "CrB"},
    {"Rasalhague",  17.5822,  +12.560,   2.08f, "Oph"},
    {"Cebalrai",    17.7229,  +4.567,    2.76f, "Oph"},
    {"Sabik",       17.1729,  -15.724,   2.43f, "Oph"},
    {"Yed Prior",   16.3054,  -4.692,    2.73f, "Oph"},
    {"Yed Posterior",16.3622,-4.1767,    3.23f, "Oph"},
};
static constexpr int kBrightStarCount = sizeof(kBrightStars)/sizeof(kBrightStars[0]);

// ── Catalog interface ──────────────────────────────────────────

class SkyObjectCatalog {
public:
    SkyObjectCatalog() { BuildCatalog(); }

    // Get all objects
    const std::vector<SkyObject>& Objects() const { return m_objects; }

    // Search by name prefix (case-insensitive)
    std::vector<const SkyObject*> Search(const std::string& query) const {
        std::vector<const SkyObject*> results;
        std::string q = query;
        for (auto& c : q) c = (char)tolower(c);
        for (const auto& obj : m_objects) {
            std::string n = obj.name;
            for (auto& c : n) c = (char)tolower(c);
            if (n.find(q) != std::string::npos)
                results.push_back(&obj);
        }
        // Sort by magnitude (brightest first)
        std::sort(results.begin(), results.end(),
            [](const SkyObject* a, const SkyObject* b) {
                return a->magnitude < b->magnitude;
            });
        return results;
    }

    // Update planet positions for current time
    void UpdatePlanets(double jd = 0) {
        if (jd == 0) jd = CurrentJD();
        for (auto& obj : m_objects) {
            if (obj.type == "Sun") {
                SunRADec(jd, obj.ra, obj.dec);
            } else if (obj.type == "Moon") {
                double phase;
                MoonRADec(jd, obj.ra, obj.dec, phase);
                // Store phase in magnitude field (reused as scratch — not displayed)
                obj.magnitude = (float)phase;
            } else if (obj.isPlanet()) {
                // Find planet entry
                for (const auto& p : kPlanets) {
                    if (obj.name == p.name) {
                        PlanetRADec(p, jd, obj.ra, obj.dec);
                        break;
                    }
                }
            }
        }
    }

    // Get objects sorted by altitude (highest first) for given observer lat/lst
    // lst = local sidereal time in hours, lat = observer latitude in degrees
    std::vector<const SkyObject*> ByAltitude(double lst, double lat) const {
        std::vector<const SkyObject*> result;
        for (const auto& obj : m_objects)
            result.push_back(&obj);

        std::sort(result.begin(), result.end(),
            [lst, lat](const SkyObject* a, const SkyObject* b) {
                double altA = CalcAlt(a->ra, a->dec, lst, lat);
                double altB = CalcAlt(b->ra, b->dec, lst, lat);
                return altA > altB;
            });
        return result;
    }

    // Compute altitude of object (degrees above horizon)
    static double CalcAlt(double ra_h, double dec_deg, double lst_h, double lat_deg) {
        double ha = Rad((lst_h - ra_h) * 15.0); // hour angle in radians
        double dec = Rad(dec_deg);
        double lat = Rad(lat_deg);
        return Deg(asin(sin(lat) * sin(dec) + cos(lat) * cos(dec) * cos(ha)));
    }

private:
    std::vector<SkyObject> m_objects;

    void BuildCatalog() {
        // Sun
        { SkyObject obj; obj.name="Sun"; obj.type="Sun";
          obj.ra=0; obj.dec=0; obj.magnitude=-26.7f; m_objects.push_back(obj); }
        // Moon
        { SkyObject obj; obj.name="Moon"; obj.type="Moon";
          obj.ra=0; obj.dec=0; obj.magnitude=-12.7f; m_objects.push_back(obj); }

        // Planets — positions computed at runtime via UpdatePlanets()
        for (const auto& p : kPlanets) {
            SkyObject obj;
            obj.name      = p.name;
            obj.type      = "Planet";
            obj.ra        = 0; obj.dec = 0; // updated by UpdatePlanets()
            obj.magnitude = p.magnitude;
            m_objects.push_back(obj);
        }

        // Bright stars — added to catalog, mag-gated in rendering
        for (int i = 0; i < kBrightStarCount; i++) {
            const auto& s = kBrightStars[i];
            SkyObject obj;
            obj.name      = s.name;
            obj.type      = "Star";
            obj.ra        = s.ra;
            obj.dec       = s.dec;
            obj.magnitude = s.mag;
            obj.notes     = s.constellation;
            m_objects.push_back(obj);
        }

        // Messier catalog
        for (const auto& m : kMessier) {
            SkyObject obj;
            if (m.name && m.name[0])
                obj.name = std::string("M") + std::to_string(m.id)
                         + " " + m.name;
            else
                obj.name = std::string("M") + std::to_string(m.id);
            obj.type      = m.type;
            obj.ra        = m.ra;
            obj.dec       = m.dec;
            obj.magnitude = m.mag;
            m_objects.push_back(obj);
        }
    }
};

} // namespace raduk::sky
