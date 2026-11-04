// ============================================================
//  Ephemeris — following anything JPL Horizons can predict
//
//  Phase 121. Phases 116 to 118 fly satellites from two-line elements.
//  Most of what else moves fast across this sky has no elements: a
//  near-Earth asteroid on its close approach, a bright comet, a
//  spacecraft. JPL Horizons predicts all of them, so this reads its
//  table and hands the tracker the same alt-az and rates SGP4 did.
//
//  ── The site stays here ─────────────────────────────────────
//
//  Horizons would gladly compute the view from this site, but that
//  means sending the site. So the request is for positions seen from
//  the centre of the Earth, and parallax is applied here, where the site
//  already is. For a close approach parallax is not a detail: Apophis
//  in April 2029 passes at 38,000 km, where a site moves it by degrees.
//
//  Vectors are ICRF, light-time corrected, at one-minute steps with
//  velocities, so a cubic between rows is good to metres even at the
//  rate a close pass moves. Times are UT.
// ============================================================

#pragma once

#include "SatPass.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace raduk::sky {

struct EphemPoint {
    double jd = 0.0;      // UT
    LVec   r;             // km, geocentric ICRF
    LVec   v;             // km/s
};

struct Ephemeris {
    std::string name;
    std::vector<EphemPoint> pts;
    bool ok = false;
    std::string why;       // for a person, when not ok
};

// Horizons' vector table: a header, then between $$SOE and $$EOE
//   2461346.052083333 = A.D. 2026-Nov-01 13:15:00.0000 UTC
//    X =-1.59E+03 Y = 4.58E+03 Z = 4.75E+03
//    VX=-7.15E+00 VY= 3.14E-01 VZ=-2.70E+00
// Anything else, including an ambiguous name, comes back as prose,
// and the useful line of it is passed on rather than "parse failed".
inline Ephemeris ParseHorizonsVectors(const std::string& text) {
    Ephemeris e;
    const size_t nameAt = text.find("Target body name:");
    if (nameAt != std::string::npos) {
        size_t s = nameAt + 17, end = text.find('\n', s);
        std::string n = text.substr(s, end == std::string::npos ? std::string::npos : end - s);
        const size_t cut = n.find('{');
        if (cut != std::string::npos) n = n.substr(0, cut);
        while (!n.empty() && (n.front() == ' ')) n.erase(n.begin());
        while (!n.empty() && (n.back() == ' ' || n.back() == '\r')) n.pop_back();
        e.name = n;
    }
    const size_t a = text.find("$$SOE"), b = text.find("$$EOE");
    if (a == std::string::npos || b == std::string::npos || b < a) {
        // The first line that says something.
        size_t at = 0;
        while (at < text.size()) {
            size_t end = text.find('\n', at);
            if (end == std::string::npos) end = text.size();
            std::string l = text.substr(at, end - at);
            if (l.find("No matches") != std::string::npos ||
                l.find("Multiple") != std::string::npos ||
                l.find("ERROR") != std::string::npos ||
                l.find("Cannot") != std::string::npos) {
                while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
                e.why = "JPL: " + l.substr(0, 160);
                return e;
            }
            at = end + 1;
        }
        e.why = "JPL sent no table";
        return e;
    }

    auto num = [](const std::string& s, const char* key, double& out) {
        const size_t k = s.find(key);
        if (k == std::string::npos) return false;
        out = std::strtod(s.c_str() + k + std::strlen(key), nullptr);
        return true;
    };
    std::vector<std::string> lines;
    size_t at = a;
    while (at < b) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos || end > b) end = b;
        lines.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    for (size_t i = 0; i + 2 < lines.size(); ++i) {
        if (lines[i].find("= A.D.") == std::string::npos) continue;
        EphemPoint p;
        p.jd = std::strtod(lines[i].c_str(), nullptr);
        const std::string& r = lines[i + 1];
        const std::string& v = lines[i + 2];
        if (p.jd > 2.0e6 && num(r, "X =", p.r.x) && num(r, "Y =", p.r.y) &&
            num(r, "Z =", p.r.z) && num(v, "VX=", p.v.x) && num(v, "VY=", p.v.y) &&
            num(v, "VZ=", p.v.z)) {
            e.pts.push_back(p);
            i += 2;
        }
    }
    e.ok = e.pts.size() >= 2;
    if (!e.ok) e.why = "JPL's table had no rows";
    return e;
}

// Cubic Hermite between the two rows either side: the velocities make
// it exact for any motion that is smooth over a minute.
inline bool EphemerisAt(const Ephemeris& e, double jd, LVec& out) {
    if (e.pts.size() < 2 || jd < e.pts.front().jd || jd > e.pts.back().jd) return false;
    auto it = std::upper_bound(e.pts.begin(), e.pts.end(), jd,
                               [](double t, const EphemPoint& p) { return t < p.jd; });
    if (it == e.pts.end()) it = e.pts.end() - 1;
    if (it == e.pts.begin()) it = e.pts.begin() + 1;
    const EphemPoint& p0 = *(it - 1);
    const EphemPoint& p1 = *it;
    const double h = (p1.jd - p0.jd) * 86400.0;      // seconds
    if (h <= 0.0) { out = p0.r; return true; }
    const double s = (jd - p0.jd) * 86400.0 / h;
    const double s2 = s * s, s3 = s2 * s;
    const double h00 = 2 * s3 - 3 * s2 + 1, h10 = s3 - 2 * s2 + s;
    const double h01 = -2 * s3 + 3 * s2,   h11 = s3 - s2;
    out = p0.r * h00 + p0.v * (h10 * h) + p1.r * h01 + p1.v * (h11 * h);
    return true;
}

// J2000 to the mean equator of date: DateToJ2000 run backwards.
inline LVec J2000ToDate(const LVec& v, double jdTd) {
    const double T = CenturiesFromJ2000(jdTd);
    const double AS = DegToRad(1.0 / 3600.0);
    const double zeta  = (2306.2181*T + 0.30188*T*T + 0.017998*T*T*T) * AS;
    const double z     = (2306.2181*T + 1.09468*T*T + 0.018203*T*T*T) * AS;
    const double theta = (2004.3109*T - 0.42665*T*T - 0.041833*T*T*T) * AS;
    return RotZ(RotY(RotZ(v, zeta), -theta), z);
}

// The same answer SatLookAt gives, from a table instead of a model.
inline SatLook EphemLookAt(const Ephemeris& e, double jdUt, const LunarObserver& o) {
    SatLook l;
    LVec r;
    if (!EphemerisAt(e, jdUt, r)) return l;
    const double td = JdTdFromUt(jdUt);
    const LVec rel = J2000ToDate(r, td) - ObserverVectorKm(o, jdUt);
    LocalHorizon(rel, jdUt, o, l.altDeg, l.azDeg);
    l.rangeKm = rel.Len();
    VecToRaDec(DateToJ2000(rel, td), l.raHours, l.decDeg);
    double az = 0.0;
    LocalHorizon(SunDirectionOfDate(jdUt), jdUt, o, l.sunAltDeg, az);
    l.ok = true;
    return l;
}

inline SatRates EphemRatesAt(const Ephemeris& e, double jdUt, const LunarObserver& o) {
    SatRates r;
    const double h = 0.25 / 86400.0;
    const SatLook a = EphemLookAt(e, jdUt - h, o);
    const SatLook b = EphemLookAt(e, jdUt + h, o);
    const SatLook c = EphemLookAt(e, jdUt, o);
    if (!a.ok || !b.ok || !c.ok) return r;
    r.altDeg = c.altDeg;
    r.azDeg  = c.azDeg;
    r.altRate = (b.altDeg - a.altDeg) / 0.5;
    r.azRate  = WrapDeg180(b.azDeg - a.azDeg) / 0.5;
    r.ok = true;
    return r;
}

// The first stretch from fromJd inside the tracking band, measured as a
// pass is, so the same preflight and plan arithmetic apply. A slow
// target can sit in the band for hours; the window ends with the table.
inline SatPassInfo EphemWindow(const Ephemeris& e, const LunarObserver& o,
                               double fromJd, const SatPassLimits& lim) {
    SatPassInfo p;
    if (!e.ok) return p;
    const double dt = 2.0 / 86400.0;
    const double last = e.pts.back().jd;
    bool in = false;
    double lastAz = 0.0;
    for (double t = (std::max)(fromJd, e.pts.front().jd); t <= last; t += dt) {
        const SatRates r = EphemRatesAt(e, t, o);
        if (!r.ok) continue;
        const bool band = r.altDeg >= lim.minAltDeg && r.altDeg <= lim.maxAltDeg;
        if (!in) {
            if (!band) continue;
            in = true;
            p.trackStartJd = p.riseJd = t;
            p.startAlt = r.altDeg; p.startAz = p.riseAz = r.azDeg;
            lastAz = r.azDeg;
        } else if (!band) {
            p.cutByCeiling = r.altDeg > lim.maxAltDeg;
            break;
        }
        p.trackEndJd = p.setJd = t;
        p.endAlt = r.altDeg; p.endAz = p.setAz = r.azDeg;
        if (r.altDeg > p.peakAlt) { p.peakAlt = r.altDeg; p.peakJd = t; p.peakAz = r.azDeg; }
        const double az = std::fabs(r.azRate), alt = std::fabs(r.altRate);
        if (az > p.peakAzRate)  p.peakAzRate = az;
        if (alt > p.peakAltRate) p.peakAltRate = alt;
        const double sky = std::hypot(az * std::cos(DegToRad(r.altDeg)), alt);
        if (sky > p.peakSkyRate) p.peakSkyRate = sky;
        if (az > lim.maxRateDeg || alt > lim.maxRateDeg) p.saturatedSec += 2.0;
        p.azTravelDeg += WrapDeg180(r.azDeg - lastAz);
        lastAz = r.azDeg;
    }
    p.trackable = in && p.trackEndJd > p.trackStartJd + 10.0 / 86400.0;
    p.peakRangeKm = in ? EphemLookAt(e, p.peakJd, o).rangeKm : 0.0;
    return p;
}

// The request, as a path for the Horizons API. Geocentric on purpose:
// see the header.
inline std::string HorizonsVectorsPath(const std::string& target, double fromJd, double toJd) {
    auto enc = [](const std::string& s) {
        std::string o;
        for (unsigned char c : s) {
            if (isalnum(c) || c == '-' || c == '.' || c == '_') o += (char)c;
            else { char b[4]; snprintf(b, sizeof(b), "%%%02X", c); o += b; }
        }
        return o;
    };
    char t[64];
    std::string p = "/api/horizons.api?format=text";
    p += "&COMMAND=" + enc("'" + target + "'");
    p += "&OBJ_DATA=" + enc("'NO'") + "&MAKE_EPHEM=" + enc("'YES'");
    p += "&EPHEM_TYPE=" + enc("'VECTORS'") + "&CENTER=" + enc("'500@399'");
    p += "&REF_PLANE=" + enc("'FRAME'") + "&REF_SYSTEM=" + enc("'ICRF'");
    p += "&VEC_CORR=" + enc("'LT'") + "&VEC_TABLE=" + enc("'2'");
    p += "&OUT_UNITS=" + enc("'KM-S'") + "&TIME_TYPE=" + enc("'UT'");
    snprintf(t, sizeof(t), "'JD %.6f'", fromJd); p += "&START_TIME=" + enc(t);
    snprintf(t, sizeof(t), "'JD %.6f'", toJd);   p += "&STOP_TIME=" + enc(t);
    p += "&STEP_SIZE=" + enc("'1 min'");
    return p;
}

// Checked against Horizons' own view from fixed sites, never against a
// target in this telescope.
inline constexpr bool EPHEMERIS_TRACKING_PROVEN_ON_SKY = false;

} // namespace raduk::sky
