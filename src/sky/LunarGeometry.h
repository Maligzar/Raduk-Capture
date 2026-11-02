// ============================================================
//  LunarGeometry — where a place on the Moon is on the sky, tonight,
//  from this site
//
//  Phase 115, lunar feature targeting. The frame is 13' x 7' and the
//  Moon is 31' across, so "go to the Moon" is not a target on this rig:
//  a crater is. Pointing at one needs three things the application did
//  not have, and the first turned out to matter for every lunar slew
//  ever made:
//
//  ── Parallax, which nothing here applied ───────────────────
//
//  MoonRADec answers for the centre of the Earth. The Moon is sixty
//  Earth radii away, so from the ground it sits up to 61' from that
//  answer: 0.95 deg x cos(altitude), which at 30 deg up is 49'. The
//  Moon's RADIUS is 15.5'. A GoTo to the old figure could finish
//  entirely off the disc, and so could the middle of a mosaic. JPL
//  Horizons shows 40' between the two for a site at 33N.
//
//  ── A longer series ────────────────────────────────────────
//
//  MoonRADec carried ten longitude and seven latitude terms, labelled
//  good to 0.1 deg: 6', half a frame height. This is Meeus Ch.47 in
//  full, and the time scale the series wants: it runs on dynamical
//  time, the Moon moves 0.55"/s, and ignoring the 69 s between TD and
//  UT is 38" or 187 px. Measured against JPL Horizons the worst case
//  is 23", periodic and mostly in RA, which is what is left of the
//  series after truncation: 113 px of a 3840 px frame, and a sixth
//  of a 640x480 ROI's width.
//
//  ── Libration, done with a rotation and not with formulas ──
//
//  The sub-Earth point wanders +/-8 deg in longitude and +/-7 in
//  latitude, which at the disc centre is 3.6' and at a crater near the
//  limb is the difference between seen and not seen. Meeus Ch.53 gives
//  closed forms, but only for the geocentre, and the topocentric
//  correction is further algebra. Instead the Moon's body axes are
//  built from Cassini's laws as one rotation, and the observer, the
//  Sun and the feature are all just vectors in that frame. So
//  parallax in libration, the position angle of the pole and the
//  sub-solar point are not separate cases that can disagree.
//
//  Physical libration is left out on purpose: at most 0.03 deg, which
//  on the Moon is 0.9 km and from here 0.5". The test measures what
//  that costs against Horizons.
//
//  Selenographic longitude is positive EAST, IAU convention, where
//  "east" is Mare Crisium's side. That is WEST on the sky. Every sign
//  error in a lunar atlas lives here, so eastArcsec below means east
//  on the sky and a test holds Crisium to it.
//
//  Times passed in are UT, as CurrentJD gives them. RA and Dec come
//  out J2000, the application's standard.
// ============================================================

#pragma once

#include "LunarSolarSize.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace raduk::sky {

// TT - UT, seconds. It has moved less than a second a decade lately,
// and a second of it is 0.55" of Moon.
inline constexpr double DELTA_T_SEC = 69.2;

inline double JdTdFromUt(double jdUt) { return jdUt + DELTA_T_SEC / 86400.0; }

// ── The Moon's position, Meeus Ch.47 ─────────────────────────
struct LunarTerm { signed char D, M, Mp, F; int sl; int sr; };
struct LunarLatTerm { signed char D, M, Mp, F; int sb; };

// Table 47.A: longitude in 1e-6 deg, distance in metres.
inline constexpr LunarTerm kLunarLonDist[] = {
    {0,0,1,0,6288774,-20905355}, {2,0,-1,0,1274027,-3699111},
    {2,0,0,0,658314,-2955968},   {0,0,2,0,213618,-569925},
    {0,1,0,0,-185116,48888},     {0,0,0,2,-114332,-3149},
    {2,0,-2,0,58793,246158},     {2,-1,-1,0,57066,-152138},
    {2,0,1,0,53322,-170733},     {2,-1,0,0,45758,-204586},
    {0,1,-1,0,-40923,-129620},   {1,0,0,0,-34720,108743},
    {0,1,1,0,-30383,104755},     {2,0,0,-2,15327,10321},
    {0,0,1,2,-12528,0},          {0,0,1,-2,10980,79661},
    {4,0,-1,0,10675,-34782},     {0,0,3,0,10034,-23210},
    {4,0,-2,0,8548,-21636},      {2,1,-1,0,-7888,24208},
    {2,1,0,0,-6766,30824},       {1,0,-1,0,-5163,-8379},
    {1,1,0,0,4987,-16675},       {2,-1,1,0,4036,-12831},
    {2,0,2,0,3994,-10445},       {4,0,0,0,3861,-11650},
    {2,0,-3,0,3665,14403},       {0,1,-2,0,-2689,-7003},
    {2,0,-1,2,-2602,0},          {2,-1,-2,0,2390,10056},
    {1,0,1,0,-2348,6322},        {2,-2,0,0,2236,-9884},
    {0,1,2,0,-2120,5751},        {0,2,0,0,-2069,0},
    {2,-2,-1,0,2048,-4950},      {2,0,1,-2,-1773,4130},
    {2,0,0,2,-1595,0},           {4,-1,-1,0,1215,-3958},
    {0,0,2,2,-1110,0},           {3,0,-1,0,-892,3258},
    {2,1,1,0,-810,2616},         {4,-1,-2,0,759,-1897},
    {0,2,-1,0,-713,-2117},       {2,2,-1,0,-700,2354},
    {2,1,-2,0,691,0},            {2,-1,0,-2,596,0},
    {4,0,1,0,549,-1423},         {0,0,4,0,537,-1117},
    {4,-1,0,0,520,-1571},        {1,0,-2,0,-487,-1739},
    {2,1,0,-2,-399,0},           {0,0,2,-2,-381,-4421},
    {1,1,1,0,351,0},             {3,0,-2,0,-340,0},
    {4,0,-3,0,330,0},            {2,-1,2,0,327,0},
    {0,2,1,0,-323,1165},         {1,1,-1,0,299,0},
    {2,0,3,0,294,0},             {2,0,-1,-2,0,8752},
};

// Table 47.B: latitude in 1e-6 deg.
inline constexpr LunarLatTerm kLunarLat[] = {
    {0,0,0,1,5128122}, {0,0,1,1,280602},  {0,0,1,-1,277693},
    {2,0,0,-1,173237}, {2,0,-1,1,55413},  {2,0,-1,-1,46271},
    {2,0,0,1,32573},   {0,0,2,1,17198},   {2,0,1,-1,9266},
    {0,0,2,-1,8822},   {2,-1,0,-1,8216},  {2,0,-2,-1,4324},
    {2,0,1,1,4200},    {2,1,0,-1,-3359},  {2,-1,-1,1,2463},
    {2,-1,0,1,2211},   {2,-1,-1,-1,2065}, {0,1,-1,-1,-1870},
    {4,0,-1,-1,1828},  {0,1,0,1,-1794},   {0,0,0,3,-1749},
    {0,1,-1,1,-1565},  {1,0,0,1,-1491},   {0,1,1,1,-1475},
    {0,1,1,-1,-1410},  {0,1,0,-1,-1344},  {1,0,0,-1,-1335},
    {0,0,3,1,1107},    {4,0,0,-1,1021},   {4,0,-1,1,833},
    {0,0,1,-3,777},    {4,0,-2,1,671},    {2,0,0,-3,607},
    {2,0,2,-1,596},    {2,-1,1,-1,491},   {2,0,-2,1,-451},
    {0,0,3,-1,439},    {2,0,2,1,422},     {2,0,-3,-1,421},
    {2,1,-1,1,-366},   {2,1,0,1,-351},    {4,0,0,1,331},
    {2,-1,1,1,315},    {2,-2,0,-1,302},   {0,0,1,3,-283},
    {2,1,1,-1,-229},   {1,1,0,-1,223},    {1,1,0,1,223},
    {0,1,-2,-1,-220},  {2,1,-1,-1,-220},  {1,0,1,1,-185},
    {2,-1,-2,-1,181},  {0,1,2,1,-177},    {4,0,-2,-1,176},
    {4,-1,-1,-1,166},  {1,0,1,-1,-164},   {4,0,1,-1,132},
    {1,0,-1,-1,-119},  {4,-1,0,-1,115},   {2,-2,0,1,107},
};

struct LunarEcliptic {
    double lonDeg = 0.0;        // mean equinox of date
    double latDeg = 0.0;
    double distKm = 0.0;
    double sigmaL = 0.0, sigmaB = 0.0, sigmaR = 0.0;   // for the test
    double meanNodeDeg = 0.0;
    double argLatDeg   = 0.0;   // F
};

// jdTd is dynamical time.
inline LunarEcliptic MoonEclipticAt(double jdTd) {
    const double T  = CenturiesFromJ2000(jdTd);
    const double T2 = T * T, T3 = T2 * T, T4 = T3 * T;

    const double Lp = NormDeg360(218.3164477 + 481267.88123421 * T
                      - 0.0015786 * T2 + T3 / 538841.0 - T4 / 65194000.0);
    const double D  = NormDeg360(297.8501921 + 445267.1114034 * T
                      - 0.0018819 * T2 + T3 / 545868.0 - T4 / 113065000.0);
    const double M  = NormDeg360(357.5291092 + 35999.0502909 * T
                      - 0.0001536 * T2 + T3 / 24490000.0);
    const double Mp = NormDeg360(134.9633964 + 477198.8675055 * T
                      + 0.0087414 * T2 + T3 / 69699.0 - T4 / 14712000.0);
    const double F  = NormDeg360(93.2720950 + 483202.0175233 * T
                      - 0.0036539 * T2 - T3 / 3526000.0 + T4 / 863310000.0);
    const double A1 = NormDeg360(119.75 + 131.849 * T);
    const double A2 = NormDeg360(53.09 + 479264.290 * T);
    const double A3 = NormDeg360(313.45 + 481266.484 * T);
    // The Earth's orbit is slowly rounding, which scales every term
    // that involves the Sun's anomaly.
    const double E  = 1.0 - 0.002516 * T - 0.0000074 * T2;

    const double Dr = DegToRad(D), Mr = DegToRad(M);
    const double Mpr = DegToRad(Mp), Fr = DegToRad(F);
    auto ecc = [E](int m) {
        const int a = m < 0 ? -m : m;
        return a == 2 ? E * E : (a == 1 ? E : 1.0);
    };

    double sl = 0.0, sr = 0.0, sb = 0.0;
    for (const auto& t : kLunarLonDist) {
        const double arg = t.D * Dr + t.M * Mr + t.Mp * Mpr + t.F * Fr;
        const double e = ecc(t.M);
        sl += e * t.sl * std::sin(arg);
        sr += e * t.sr * std::cos(arg);
    }
    for (const auto& t : kLunarLat) {
        const double arg = t.D * Dr + t.M * Mr + t.Mp * Mpr + t.F * Fr;
        sb += ecc(t.M) * t.sb * std::sin(arg);
    }
    const double Lr = DegToRad(Lp);
    sl += 3958.0 * std::sin(DegToRad(A1)) + 1962.0 * std::sin(Lr - Fr)
        +  318.0 * std::sin(DegToRad(A2));
    sb += -2235.0 * std::sin(Lr) + 382.0 * std::sin(DegToRad(A3))
        + 175.0 * std::sin(DegToRad(A1) - Fr)
        + 175.0 * std::sin(DegToRad(A1) + Fr)
        + 127.0 * std::sin(Lr - Mpr) - 115.0 * std::sin(Lr + Mpr);

    LunarEcliptic out;
    out.sigmaL = sl; out.sigmaB = sb; out.sigmaR = sr;
    out.lonDeg = NormDeg360(Lp + sl / 1.0e6);
    out.latDeg = sb / 1.0e6;
    out.distKm = MOON_MEAN_DISTANCE_KM + sr / 1000.0;
    out.argLatDeg = F;
    out.meanNodeDeg = NormDeg360(125.0445479 - 1934.1362891 * T
                      + 0.0020754 * T2 + T3 / 467441.0 - T4 / 60616000.0);
    return out;
}

// The Sun's geometric longitude, as SunRADec works it out. 0.01 deg is
// far inside what a sub-solar point is used for.
inline double SunEclipticLonDeg(double jdTd) {
    const double T = CenturiesFromJ2000(jdTd);
    const double M = DegToRad(NormDeg360(357.52911 + 35999.05029 * T));
    const double C = (1.914602 - 0.004817*T - 0.000014*T*T) * std::sin(M)
                   + (0.019993 - 0.000101*T) * std::sin(2*M)
                   +  0.000289 * std::sin(3*M);
    return NormDeg360(280.46646 + 36000.76983 * T + C);
}

// 0 new, 0.25 first quarter, 0.5 full, which is MoonRADec's convention.
inline double LunarPhaseFraction(double jdUt) {
    const double td = JdTdFromUt(jdUt);
    return NormDeg360(MoonEclipticAt(td).lonDeg - SunEclipticLonDeg(td))
         / 360.0;
}

// ── Vectors ──────────────────────────────────────────────────
struct LVec {
    double x = 0.0, y = 0.0, z = 0.0;
    LVec operator+(const LVec& o) const { return {x+o.x, y+o.y, z+o.z}; }
    LVec operator-(const LVec& o) const { return {x-o.x, y-o.y, z-o.z}; }
    LVec operator*(double k)      const { return {x*k, y*k, z*k}; }
    double Dot(const LVec& o)     const { return x*o.x + y*o.y + z*o.z; }
    double Len()                  const { return std::sqrt(Dot(*this)); }
    LVec Unit() const { const double l = Len(); return l > 0 ? *this*(1/l) : *this; }
    LVec Cross(const LVec& o) const {
        return {y*o.z - z*o.y, z*o.x - x*o.z, x*o.y - y*o.x};
    }
};

// asin that a unit vector's rounding cannot push out of its domain.
inline double SafeAsinDeg(double s) {
    return std::asin(s > 1.0 ? 1.0 : (s < -1.0 ? -1.0 : s)) / DegToRad(1.0);
}

inline LVec RotX(const LVec& v, double a) {
    const double c = std::cos(a), s = std::sin(a);
    return {v.x, c*v.y - s*v.z, s*v.y + c*v.z};
}
inline LVec RotY(const LVec& v, double a) {
    const double c = std::cos(a), s = std::sin(a);
    return {c*v.x + s*v.z, v.y, -s*v.x + c*v.z};
}
inline LVec RotZ(const LVec& v, double a) {
    const double c = std::cos(a), s = std::sin(a);
    return {c*v.x - s*v.y, s*v.x + c*v.y, v.z};
}

inline double MeanObliquityRad(double jdTd) {
    const double T = CenturiesFromJ2000(jdTd);
    return DegToRad(23.4392911 - 0.0130042 * T);
}

// Mean equator of date back to J2000, Meeus Ch.21 run in reverse.
// The application's PrecessDateToJ2000 is an annual rate; 26 years out
// it is a couple of arcsec adrift, which is 10 px, and this is the
// one path that cares.
inline LVec DateToJ2000(const LVec& v, double jdTd) {
    const double T = CenturiesFromJ2000(jdTd);
    const double AS = DegToRad(1.0 / 3600.0);
    const double zeta  = (2306.2181*T + 0.30188*T*T + 0.017998*T*T*T) * AS;
    const double z     = (2306.2181*T + 1.09468*T*T + 0.018203*T*T*T) * AS;
    const double theta = (2004.3109*T - 0.42665*T*T - 0.041833*T*T*T) * AS;
    return RotZ(RotY(RotZ(v, -z), theta), -zeta);
}

inline void VecToRaDec(const LVec& v, double& raHours, double& decDeg) {
    const LVec u = v.Unit();
    raHours = NormDeg360(std::atan2(u.y, u.x) / DegToRad(1.0)) / 15.0;
    decDeg  = SafeAsinDeg(u.z);
}

// ── The observer ─────────────────────────────────────────────
struct LunarObserver {
    double latDeg = 0.0;
    double lonDeg = 0.0;        // +E
    double elevM  = 0.0;
    bool   set    = false;      // false: the geocentre
};

inline double GreenwichMeanSiderealDeg(double jdUt) {
    const double T = CenturiesFromJ2000(jdUt);
    return NormDeg360(280.46061837 + 360.98564736629 * (jdUt - 2451545.0)
                      + 0.000387933 * T * T - T * T * T / 38710000.0);
}

// From the geocentre, km, mean equator of date. Meeus Ch.11.
inline LVec ObserverVectorKm(const LunarObserver& o, double jdUt) {
    if (!o.set) return {};
    const double phi = DegToRad(o.latDeg);
    const double u   = std::atan(0.99664719 * std::tan(phi));
    const double h   = o.elevM / 6378140.0;
    const double rs  = 0.99664719 * std::sin(u) + h * std::sin(phi);
    const double rc  = std::cos(u) + h * std::cos(phi);
    const double lst = DegToRad(GreenwichMeanSiderealDeg(jdUt) + o.lonDeg);
    return LVec{rc * std::cos(lst), rc * std::sin(lst), rs} * 6378.14;
}

// ── Tonight's Moon from here ─────────────────────────────────
struct LunarView {
    double raHours = 0.0, decDeg = 0.0;   // J2000, of the disc centre
    double distKm  = 0.0;                 // observer to centre
    double subEarthLonDeg = 0.0, subEarthLatDeg = 0.0;  // the libration
    double subSolarLonDeg = 0.0, subSolarLatDeg = 0.0;
    double colongitudeDeg = 0.0;          // morning terminator
    double poleAngleDeg   = 0.0;          // north pole, east of north
    double jdTd = 0.0;

    // The frame everything is measured in: equator of date, km.
    LVec centre;                          // from the observer
    LVec axisX, axisY, axisZ;             // the Moon's body axes
    LVec sunDir;                          // from the Moon
};

// A body vector out to the equatorial frame.
inline LVec BodyToEquatorial(const LunarView& v, const LVec& b) {
    return v.axisX * b.x + v.axisY * b.y + v.axisZ * b.z;
}

inline LunarView LunarViewAt(double jdUt, const LunarObserver& obs) {
    const double td = JdTdFromUt(jdUt);
    const LunarEcliptic m = MoonEclipticAt(td);
    const double eps = MeanObliquityRad(td);
    const double lam = DegToRad(m.lonDeg), bet = DegToRad(m.latDeg);

    const LVec eclMoon{std::cos(bet) * std::cos(lam),
                       std::cos(bet) * std::sin(lam), std::sin(bet)};
    const LVec moon = RotX(eclMoon, eps) * m.distKm;     // from the geocentre

    LunarView v;
    v.jdTd   = td;
    v.centre = moon - ObserverVectorKm(obs, jdUt);
    v.distKm = v.centre.Len();
    VecToRaDec(DateToJ2000(v.centre, td), v.raHours, v.decDeg);

    // Cassini's laws. The equator is inclined I to the ecliptic, with
    // its descending node on the orbit's ascending node, and the prime
    // meridian faces the mean Earth, so the body X axis lies at the
    // mean longitude plus 180, which is Omega + 180 + F.
    const double I = DegToRad(1.54242);
    const double a = DegToRad(m.meanNodeDeg + 180.0);
    const double F = DegToRad(m.argLatDeg);
    auto axis = [&](const LVec& b) {
        return RotX(RotZ(RotX(RotZ(b, F), I), a), eps);
    };
    v.axisX = axis({1, 0, 0});
    v.axisY = axis({0, 1, 0});
    v.axisZ = axis({0, 0, 1});

    auto subPoint = [&](const LVec& dir, double& lon, double& lat) {
        const LVec u = dir.Unit();
        const double bx = u.Dot(v.axisX), by = u.Dot(v.axisY);
        double l = std::atan2(by, bx) / DegToRad(1.0);
        if (l > 180.0) l -= 360.0;
        lon = l;
        lat = SafeAsinDeg(u.Dot(v.axisZ));
    };
    subPoint(v.centre * -1.0, v.subEarthLonDeg, v.subEarthLatDeg);

    const double sl = DegToRad(SunEclipticLonDeg(td));
    const LVec sun = RotX(LVec{std::cos(sl), std::sin(sl), 0.0}, eps)
                   * SunDistanceKm(td);
    v.sunDir = (sun - moon).Unit();
    subPoint(v.sunDir, v.subSolarLonDeg, v.subSolarLatDeg);
    v.colongitudeDeg = NormDeg360(90.0 - v.subSolarLonDeg);

    // The pole on the sky, measured in the J2000 tangent plane so it
    // agrees with the offsets below.
    const LVec c = DateToJ2000(v.centre, td).Unit();
    const LVec p = DateToJ2000(v.axisZ, td);
    const LVec north = (LVec{0, 0, 1} - c * c.z).Unit();
    const LVec east  = north.Cross(c);
    v.poleAngleDeg = NormDeg360(std::atan2(p.Dot(east), p.Dot(north))
                                / DegToRad(1.0));
    return v;
}

// ── A place on the surface ───────────────────────────────────
struct LunarSpot {
    bool   visible = false;      // on the side facing this observer
    double raHours = 0.0, decDeg = 0.0;   // J2000
    double eastArcsec  = 0.0;    // from the disc centre, ON THE SKY
    double northArcsec = 0.0;
    double sunAltDeg   = 0.0;    // the Sun's height there; < 0 is night
    double viewAltDeg  = 0.0;    // our height above its horizon; 90 is overhead
};

inline LunarSpot LunarSpotAt(const LunarView& v, double latDeg, double lonDeg) {
    const double b = DegToRad(latDeg), l = DegToRad(lonDeg);
    const LVec normal = BodyToEquatorial(v, {std::cos(b) * std::cos(l),
                                             std::cos(b) * std::sin(l),
                                             std::sin(b)});
    const LVec p = v.centre + normal * MOON_RADIUS_KM;

    LunarSpot s;
    const double view = -normal.Dot(p.Unit());
    s.visible    = view > 0.0;
    s.viewAltDeg = SafeAsinDeg(view);
    s.sunAltDeg  = SafeAsinDeg(normal.Dot(v.sunDir));

    const LVec pj = DateToJ2000(p, v.jdTd).Unit();
    VecToRaDec(pj, s.raHours, s.decDeg);

    const LVec c = DateToJ2000(v.centre, v.jdTd).Unit();
    const LVec north = (LVec{0, 0, 1} - c * c.z).Unit();
    const LVec east  = north.Cross(c);
    s.eastArcsec  = pj.Dot(east)  * ARCSEC_PER_RAD;
    s.northArcsec = pj.Dot(north) * ARCSEC_PER_RAD;
    return s;
}

// The terminator crosses the Moon at this rate, the synodic month's
// 360 deg in 29.53 days.
inline constexpr double TERMINATOR_DEG_PER_DAY = 12.19;

// Days until the morning terminator reaches this longitude.
inline double DaysToSunrise(const LunarView& v, double lonDeg) {
    return NormDeg360(v.subSolarLonDeg - (lonDeg + 90.0))
         / TERMINATOR_DEG_PER_DAY;
}

// What the sun angle is worth. On the Moon relief is shadow, and
// shadow is low sun: a crater that is superb at 5 deg is a flat grey
// disc at 60. The bands are an observer's rule of thumb, not physics,
// and will want looking at on real frames.
inline constexpr double LUNAR_LOW_SUN_DEG  = 20.0;
inline constexpr double LUNAR_HIGH_SUN_DEG = 50.0;

// One line for the log, for the moment the slew goes. A feature's
// lighting decides whether the slew was worth making, and the night
// side of the terminator is otherwise found by looking at a black
// frame.
inline std::string DescribeLunarSpot(const char* name, const LunarSpot& s,
                                     const LunarView& v, double lonDeg,
                                     double sizeKm) {
    const double arcsec = sizeKm / v.distKm * ARCSEC_PER_RAD;
    char b[240];
    if (!s.visible) {
        snprintf(b, sizeof(b), "%s is past the limb tonight: libration "
                 "has it %.1f deg round the far side.", name, -s.viewAltDeg);
    } else if (s.sunAltDeg <= 0.0) {
        snprintf(b, sizeof(b), "%s is in darkness: %.0f km, %.0f\" across. "
                 "Sunrise there in %.1f days.", name, sizeKm, arcsec,
                 DaysToSunrise(v, lonDeg));
    } else {
        const char* light = s.sunAltDeg < LUNAR_LOW_SUN_DEG
                          ? "low sun, good relief"
                          : s.sunAltDeg > LUNAR_HIGH_SUN_DEG
                          ? "high sun, little relief" : "moderate sun";
        snprintf(b, sizeof(b), "%s: %.0f km, %.0f\" across. Sun %.0f deg "
                 "up, %s.%s", name, sizeKm, arcsec, s.sunAltDeg, light,
                 s.viewAltDeg < 10.0 ? " Close to the limb." : "");
    }
    return b;
}

// Measured against JPL Horizons, not against the sky. Whether a crater
// lands in the frame also depends on how well the mount was aligned.
inline constexpr bool LUNAR_TARGETING_PROVEN_ON_SKY = false;

} // namespace raduk::sky
