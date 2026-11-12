#pragma once
// ============================================================
//  JupiterEvents.h -- phase 137: which hour of a Jupiter night is
//  worth the derotation window
//
//  Three things decide it, and nothing outside the emulator knew any of
//  them: where the central meridian is (and so when the Great Red Spot
//  faces us), and when a moon or its shadow crosses the disc.
//
//  ── Central meridians ───────────────────────────────────────
//  Meeus, Astronomical Algorithms ch. 43, the lower-accuracy method,
//  for Systems I and II (recalled from the book). System III is the same
//  structure with the IAU System III prime meridian and rate (W0 284.95,
//  870.536 deg/day). The method takes Terrestrial Time; fed UT it was a
//  steady 0.66 deg behind JPL Horizons, which is DELTA_T_SEC of rotation.
//  With TT it matches Horizons' System III to about 0.01 deg over
//  November 2026 to February 2027 (tests/jupiter_events.cpp).
//
//  ── The Great Red Spot ──────────────────────────────────────
//  It transits when the System II meridian reaches its longitude. That
//  longitude is measured, not computed, and it drifts: published values
//  disagree (Sky & Telescope assumed 91 deg on 2026-06-01 drifting
//  1.75 deg a month; Project Pluto about 16 deg a year), and each degree
//  is 1.65 minutes of transit time. So it comes from rig.ini with an
//  epoch and a drift, and a stale epoch is said out loud.
//
//  ── Moons and shadows ───────────────────────────────────────
//  Moon transits, occultations and eclipses come from Horizons' own
//  visibility codes (observer quantity 12, limb to limb). A shadow on
//  the disc is not in those codes, so it is worked out here from
//  jovicentric vectors of the moon, the Sun and the Earth: the ray from
//  the moon away from the Sun meets the (oblate) disc, on the side
//  facing the Earth. Times are for the shadow's centre, as seen from the
//  Earth (light time added). Checked by working a moon transit the same
//  way: it matches Horizons' codes to within a minute.
//
//  Pure: tests/jupiter_events.cpp, against saved Horizons replies.
// ============================================================

#include "Ephemeris.h"     // ParseHorizonsVectors, Ephemeris, LVec
#include "LunarGeometry.h" // DELTA_T_SEC

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace raduk::sky::jupiter {

inline double Deg2Rad(double d) { return d * 3.14159265358979323846 / 180.0; }
inline double Norm360(double d) { d = std::fmod(d, 360.0); return d < 0 ? d + 360.0 : d; }

// ── Central meridians ─────────────────────────────────────────
struct Meridians {
    double sysI = 0, sysII = 0, sysIII = 0;   // degrees, west longitude
    double distanceAu = 0;                    // Earth to Jupiter
};

inline Meridians CentralMeridians(double jdUt) {
    const double d = (jdUt + DELTA_T_SEC / 86400.0) - 2451545.0;   // TT
    const double V = 172.74 + 0.00111588 * d;
    const double M = 357.529 + 0.9856003 * d;
    const double N = 20.020 + 0.0830853 * d + 0.329 * std::sin(Deg2Rad(V));
    const double J = 66.115 + 0.9025179 * d - 0.329 * std::sin(Deg2Rad(V));
    const double A = 1.915 * std::sin(Deg2Rad(M)) + 0.020 * std::sin(Deg2Rad(2 * M));
    const double B = 5.555 * std::sin(Deg2Rad(N)) + 0.168 * std::sin(Deg2Rad(2 * N));
    const double K = J + A - B;
    const double R = 1.00014 - 0.01671 * std::cos(Deg2Rad(M)) - 0.00014 * std::cos(Deg2Rad(2 * M));
    const double r = 5.20872 - 0.25208 * std::cos(Deg2Rad(N)) - 0.00611 * std::cos(Deg2Rad(2 * N));
    const double D = std::sqrt(r * r + R * R - 2 * r * R * std::cos(Deg2Rad(K)));
    const double psi = std::asin(R / D * std::sin(Deg2Rad(K))) * 180.0 / 3.14159265358979323846;
    const double t = d - D / 173.0;            // light time, days (173 AU a day)
    Meridians m;
    m.sysI   = Norm360(210.98 + 877.8169088 * t + psi - B);
    m.sysII  = Norm360(187.23 + 870.1869088 * t + psi - B);
    // System III: System II's constant moved by the IAU prime meridians
    // (284.95 - 43.3), its rate by the IAU rate less Jupiter's orbital
    // motion, as Meeus's own two rates are.
    m.sysIII = Norm360(187.23 + (284.95 - 43.3) + (870.536 - 0.0830853) * t + psi - B);
    m.distanceAu = D;
    return m;
}

// ── The Great Red Spot ────────────────────────────────────────
struct GrsModel {
    double lonSysII   = 91.0;        // degrees, at the epoch
    double epochJd    = 2461192.5;   // 2026-06-01 0h UT
    double driftDegPerMonth = 1.75;  // + is increasing System II longitude
    static constexpr double kMonthDays = 30.4375;
    double LongitudeAt(double jd) const {
        return Norm360(lonSysII + driftDegPerMonth * (jd - epochJd) / kMonthDays);
    }
    // Days since the measurement; past ~60 the transit times drift by
    // minutes and the owner should set a new figure.
    double AgeDays(double jd) const { return jd - epochJd; }
    static constexpr double kStaleDays = 60.0;
};

// "YYYY-MM-DD" to the JD of its 0h UT; 0 if it does not read.
inline double JdFromIsoDate(const std::string& s) {
    int y = 0, m = 0, d = 0;
    if (sscanf(s.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
    const int a = (14 - m) / 12, yy = y + 4800 - a, mm = m + 12 * a - 3;
    const long jdn = d + (153 * mm + 2) / 5 + 365L * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
    return (double)jdn - 0.5;
}

// Every time in [fromJd, toJd] the System II meridian passes the spot.
inline std::vector<double> GrsTransits(const GrsModel& g, double fromJd, double toJd) {
    std::vector<double> out;
    auto f = [&](double jd) {   // signed angle from the spot to the meridian
        double a = CentralMeridians(jd).sysII - g.LongitudeAt(jd);
        while (a > 180) a -= 360;
        while (a < -180) a += 360;
        return a;
    };
    const double step = 0.5 / 24.0;   // half an hour: the meridian turns 18 deg
    double a0 = f(fromJd);
    for (double t = fromJd; t < toJd; t += step) {
        const double t1 = (std::min)(t + step, toJd);
        const double a1 = f(t1);
        if (a0 < 0 && a1 >= 0) {          // rising through zero, not the 180 wrap
            double lo = t, hi = t1;
            for (int i = 0; i < 40; ++i) {
                const double mid = 0.5 * (lo + hi);
                (f(mid) < 0 ? lo : hi) = mid;
            }
            out.push_back(0.5 * (lo + hi));
        }
        a0 = a1;
    }
    return out;
}

// ── Moon and shadow events ────────────────────────────────────
enum class Kind { Transit, ShadowTransit, Occultation, Eclipse };
inline const char* KindName(Kind k) {
    switch (k) {
        case Kind::Transit:       return "transit";
        case Kind::ShadowTransit: return "shadow transit";
        case Kind::Occultation:   return "occultation";
        case Kind::Eclipse:       return "eclipse";
    }
    return "?";
}
inline const char* MoonName(int moon) {
    static const char* n[] = { "?", "Io", "Europa", "Ganymede", "Callisto" };
    return (moon >= 1 && moon <= 4) ? n[moon] : n[0];
}

struct Event {
    int    moon = 0;            // 1 Io .. 4 Callisto
    Kind   kind = Kind::Transit;
    double startJd = 0, endJd = 0;   // UT as seen from the Earth
    bool   startKnown = true, endKnown = true;   // false: under way at the table's edge
};

// One row of Horizons' observer table with quantity 12: the time and the
// visibility code after the "/" ("*", "t", "O", "p", "P", "u", "U").
struct VisRow { double jd = 0; char code = '*'; };

inline double JdFromHorizonsDate(const char* s) {
    // "2026-Nov-13 15:06" (or with :SS)
    static const char* mon[] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
    int y = 0, d = 0, hh = 0, mm = 0; double ss = 0; char m3[4] = {};
    if (sscanf(s, "%d-%3c-%d %d:%d:%lf", &y, m3, &d, &hh, &mm, &ss) < 5) return 0;
    int m = 0;
    for (int i = 0; i < 12; ++i) if (!strncmp(m3, mon[i], 3)) m = i + 1;
    if (!m) return 0;
    // Fliegel and Van Flandern, for the Gregorian calendar.
    const int a = (14 - m) / 12, yy = y + 4800 - a, mmo = m + 12 * a - 3;
    const long jdn = d + (153 * mmo + 2) / 5 + 365L * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
    return (double)jdn - 0.5 + (hh + mm / 60.0 + ss / 3600.0) / 24.0;
}

inline std::vector<VisRow> ParseVisibility(const std::string& text) {
    std::vector<VisRow> rows;
    const size_t a = text.find("$$SOE"), b = text.find("$$EOE");
    if (a == std::string::npos || b == std::string::npos || b < a) return rows;
    size_t at = text.find('\n', a);
    while (at != std::string::npos && at < b) {
        const size_t end = text.find('\n', at + 1);
        const std::string l = text.substr(at + 1, (end == std::string::npos ? b : end) - at - 1);
        at = end;
        const size_t slash = l.find('/');
        if (slash == std::string::npos || slash + 1 >= l.size()) continue;
        VisRow r;
        // Horizons may put solar/lunar presence markers between the time
        // and the numbers; the date is the first 17 characters.
        r.jd = JdFromHorizonsDate(l.c_str() + (l[0] == ' ' ? 1 : 0));
        r.code = l[slash + 1];
        if (r.jd > 2.0e6) rows.push_back(r);
    }
    return rows;
}

// Code changes into events. A run under way at either end of the table
// is kept, marked as such.
inline std::vector<Event> EventsFromVisibility(int moon, const std::vector<VisRow>& rows) {
    std::vector<Event> out;
    auto is = [](Kind k, char c) {
        switch (k) {
            case Kind::Transit:     return c == 't';
            case Kind::Occultation: return c == 'O' || c == 'P' || c == 'U';
            case Kind::Eclipse:     return c == 'p' || c == 'P' || c == 'u' || c == 'U';
            default:                return false;
        }
    };
    for (Kind k : { Kind::Transit, Kind::Occultation, Kind::Eclipse }) {
        bool in = false; Event e;
        for (size_t i = 0; i < rows.size(); ++i) {
            const bool now = is(k, rows[i].code);
            if (now && !in) {
                e = Event{}; e.moon = moon; e.kind = k;
                if (i == 0) { e.startJd = rows[i].jd; e.startKnown = false; }
                else e.startJd = 0.5 * (rows[i - 1].jd + rows[i].jd);   // the change lies between rows
                in = true;
            } else if (!now && in) {
                e.endJd = 0.5 * (rows[i - 1].jd + rows[i].jd);
                out.push_back(e); in = false;
            }
        }
        if (in) { e.endJd = rows.back().jd; e.endKnown = false; out.push_back(e); }
    }
    return out;
}

// Linear between rows; at 10-minute steps a moon moves under a degree
// of its orbit, so the error is far below the disc's size.
inline bool VecAt(const Ephemeris& e, double jd, LVec& out) {
    if (e.pts.size() < 2 || jd < e.pts.front().jd || jd > e.pts.back().jd) return false;
    auto it = std::upper_bound(e.pts.begin(), e.pts.end(), jd,
                               [](double t, const EphemPoint& p) { return t < p.jd; });
    if (it == e.pts.begin()) it++;
    if (it == e.pts.end()) it--;
    const EphemPoint& p1 = *it; const EphemPoint& p0 = *(it - 1);
    const double f = (jd - p0.jd) / (p1.jd - p0.jd);
    out = p0.r + (p1.r - p0.r) * f;
    return true;
}

inline constexpr double kJupiterReqKm = 71492.0, kJupiterRpolKm = 66854.0;
inline constexpr double kLightKmPerDay = 299792.458 * 86400.0;
inline constexpr double kMoonRadiusKm[5] = { 0, 1821.6, 1560.8, 2631.2, 2410.3 };

// Jupiter's north pole, ICRF (IAU, J2000; its slow motion is ignored).
inline LVec JupiterPole() {
    const double ra = Deg2Rad(268.057), de = Deg2Rad(64.495);
    return { std::cos(de) * std::cos(ra), std::cos(de) * std::sin(ra), std::sin(de) };
}

// Where the shadow of a moon at m (jovicentric, km) falls on the disc,
// with the Sun in direction sunHat. False when it misses.
inline bool ShadowPoint(const LVec& m, const LVec& sunHat, LVec& p) {
    const LVec pole = JupiterPole();
    const double k = kJupiterReqKm / kJupiterRpolKm;
    auto sc = [&](const LVec& v) { return v + pole * ((k - 1.0) * v.Dot(pole)); };
    const LVec q = sc(m), d = sc(sunHat * -1.0);
    const double A = d.Dot(d), B = 2 * q.Dot(d), C = q.Dot(q) - kJupiterReqKm * kJupiterReqKm;
    const double disc = B * B - 4 * A * C;
    if (disc <= 0) return false;
    const double l = (-B - std::sqrt(disc)) / (2 * A);
    if (l <= 0) return false;
    p = m - sunHat * l;
    return true;
}

// Shadow transits and (as a check on the geometry) moon transits from
// jovicentric geometric vectors of the moon, the Sun and the Earth on
// the same time grid. Times returned are as seen from the Earth.
inline std::vector<Event> EventsFromVectors(int moon, const Ephemeris& moonV, const Ephemeris& sunV,
                                            const Ephemeris& earthV, bool shadows = true,
                                            bool transits = false, double stepSec = 20.0) {
    std::vector<Event> out;
    if (moonV.pts.size() < 2 || sunV.pts.size() < 2 || earthV.pts.size() < 2) return out;
    const double t0 = (std::max)({ moonV.pts.front().jd, sunV.pts.front().jd, earthV.pts.front().jd });
    const double t1 = (std::min)({ moonV.pts.back().jd, sunV.pts.back().jd, earthV.pts.back().jd });
    bool inS = false, inT = false, first = true;
    Event es, et;
    double lastE = 0;
    for (double tau = t0; tau <= t1; tau += stepSec / 86400.0) {
        LVec m, s, e;
        if (!VecAt(moonV, tau, m) || !VecAt(sunV, tau, s) || !VecAt(earthV, tau, e)) continue;
        const double tE = tau + e.Len() / kLightKmPerDay;
        const LVec eh = e.Unit(), sh = s.Unit();
        LVec p;
        const bool S = shadows && ShadowPoint(m, sh, p) && p.Dot(eh) > 0;
        const double along = m.Dot(eh);
        const double perp = (m - eh * along).Len();
        const bool T = transits && along > 0 && perp < kJupiterReqKm + kMoonRadiusKm[moon];
        if (S && !inS) { es = Event{}; es.moon = moon; es.kind = Kind::ShadowTransit; es.startJd = tE; es.startKnown = !first; }
        if (!S && inS) { es.endJd = tE; out.push_back(es); }
        if (T && !inT) { et = Event{}; et.moon = moon; et.kind = Kind::Transit; et.startJd = tE; et.startKnown = !first; }
        if (!T && inT) { et.endJd = tE; out.push_back(et); }
        inS = S; inT = T; first = false; lastE = tE;
    }
    if (inS) { es.endJd = lastE; es.endKnown = false; out.push_back(es); }
    if (inT) { et.endJd = lastE; et.endKnown = false; out.push_back(et); }
    return out;
}

// ── Requests ──────────────────────────────────────────────────
inline std::string HzEnc(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '.' || c == '_') o += (char)c;
        else { char b[4]; snprintf(b, sizeof(b), "%%%02X", c); o += b; }
    }
    return o;
}

// Observer table, geocentric, with the visibility codes (quantity 12).
inline std::string VisibilityPath(int moon, double fromJd, double toJd, const char* step = "2 min") {
    char t[64];
    std::string p = "/api/horizons.api?format=text";
    p += "&COMMAND=" + HzEnc("'50" + std::to_string(moon) + "'");
    p += "&OBJ_DATA=" + HzEnc("'NO'") + "&MAKE_EPHEM=" + HzEnc("'YES'");
    p += "&EPHEM_TYPE=" + HzEnc("'OBSERVER'") + "&CENTER=" + HzEnc("'500@399'");
    p += "&QUANTITIES=" + HzEnc("'12'") + "&TIME_TYPE=" + HzEnc("'UT'");
    snprintf(t, sizeof(t), "'JD %.6f'", fromJd); p += "&START_TIME=" + HzEnc(t);
    snprintf(t, sizeof(t), "'JD %.6f'", toJd);   p += "&STOP_TIME=" + HzEnc(t);
    p += "&STEP_SIZE=" + HzEnc(std::string("'") + step + "'");
    return p;
}

// Geometric vectors centred on Jupiter (no light-time: the geometry at
// Jupiter, carried to the Earth by EventsFromVectors).
inline std::string JovicentricVectorsPath(const std::string& target, double fromJd, double toJd,
                                          const char* step = "10 min") {
    char t[64];
    std::string p = "/api/horizons.api?format=text";
    p += "&COMMAND=" + HzEnc("'" + target + "'");
    p += "&OBJ_DATA=" + HzEnc("'NO'") + "&MAKE_EPHEM=" + HzEnc("'YES'");
    p += "&EPHEM_TYPE=" + HzEnc("'VECTORS'") + "&CENTER=" + HzEnc("'500@599'");
    p += "&REF_PLANE=" + HzEnc("'FRAME'") + "&REF_SYSTEM=" + HzEnc("'ICRF'");
    p += "&VEC_CORR=" + HzEnc("'NONE'") + "&VEC_TABLE=" + HzEnc("'2'");
    p += "&OUT_UNITS=" + HzEnc("'KM-S'") + "&TIME_TYPE=" + HzEnc("'UT'");
    snprintf(t, sizeof(t), "'JD %.6f'", fromJd); p += "&START_TIME=" + HzEnc(t);
    snprintf(t, sizeof(t), "'JD %.6f'", toJd);   p += "&STOP_TIME=" + HzEnc(t);
    p += "&STEP_SIZE=" + HzEnc(std::string("'") + step + "'");
    return p;
}

// ── Text ──────────────────────────────────────────────────────
inline std::string HhMm(double jd) {
    const double f = jd + 0.5 - std::floor(jd + 0.5);
    int mins = (int)std::lround(f * 1440.0) % 1440;
    char b[8]; snprintf(b, sizeof(b), "%02d:%02d", mins / 60, mins % 60);
    return b;
}
inline std::string EventLine(const Event& e) {
    char b[160];
    snprintf(b, sizeof(b), "%s %s %s%s to %s%s UTC", MoonName(e.moon), KindName(e.kind),
             e.startKnown ? "" : "(under way) ", HhMm(e.startJd).c_str(),
             HhMm(e.endJd).c_str(), e.endKnown ? "" : " (or later)");
    return b;
}

}  // namespace raduk::sky::jupiter
