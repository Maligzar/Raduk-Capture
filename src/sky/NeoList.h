#pragma once
// ============================================================
//  NeoList -- phase 128: near-Earth objects on the sky map
//
//  Which NEOs: JPL's Close-Approach Data (ssd-api.jpl.nasa.gov/cad.api),
//  the objects passing within kMaxDistAu of the Earth from a week ago to
//  a month ahead. That is the list that changes, and the one worth a
//  look on any given night; the 35 000 known NEOs are mostly too faint
//  and too far.
//
//  Where: a JPL Horizons vector table per object, geocentric as phase 121
//  decided, so the site never leaves this machine; EphemLookAt applies it
//  here. That matters more for an NEO than for anything else on the map:
//  at a tenth of a lunar distance the parallax is a degree.
//
//  How bright: the IAU H-G system with G = 0.15, from H (in the CAD row),
//  the distance from the Sun and from the Earth, and the phase angle. The
//  Sun's position comes from one more Horizons table, so every distance
//  is in the same frame. Good to a few tenths of a magnitude, which is
//  all the map's cut needs.
//
//  Pure: parsing and arithmetic. NeoDesk fetches.
// ============================================================

#include "Ephemeris.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace raduk::sky {

inline constexpr double kAuKm = 149597870.7;

struct NeoCandidate {
    std::string des;        // "2024 YR4", or "99942" once numbered
    std::string fullname;   // "99942 Apophis (2004 MN4)", tidied
    double      cdJd = 0;   // close approach, TDB (minutes off UT: fine here)
    double      distAu = 0; // nominal miss distance
    double      h = 99;     // absolute magnitude, 99 when not given
};

namespace neo_detail {

// The strings and nulls of one JSON array, from just inside its '['.
// CAD rows hold nothing else.
inline size_t ReadArray(const std::string& s, size_t i, std::vector<std::string>& out) {
    out.clear();
    while (i < s.size()) {
        const char c = s[i];
        if (c == ']') return i + 1;
        if (c == '"') {
            std::string v;
            ++i;
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < s.size()) { v += s[i + 1]; i += 2; continue; }
                v += s[i++];
            }
            out.push_back(v);
            ++i;
        } else if (s.compare(i, 4, "null") == 0) {
            out.push_back("");
            i += 4;
        } else {
            ++i;
        }
    }
    return i;
}

inline std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

} // namespace neo_detail

// The CAD API's JSON. Columns are found by name, so an added field does
// not break this.
inline std::vector<NeoCandidate> ParseCad(const std::string& json) {
    using namespace neo_detail;
    std::vector<NeoCandidate> out;
    const size_t f = json.find("\"fields\"");
    const size_t d = json.find("\"data\"");
    if (f == std::string::npos || d == std::string::npos) return out;
    std::vector<std::string> fields;
    ReadArray(json, json.find('[', f) + 1, fields);
    auto col = [&](const char* name) {
        for (size_t i = 0; i < fields.size(); ++i) if (fields[i] == name) return (int)i;
        return -1;
    };
    const int cDes = col("des"), cJd = col("jd"), cDist = col("dist"),
              cH = col("h"), cName = col("fullname");
    if (cDes < 0 || cJd < 0 || cDist < 0) return out;

    size_t i = json.find('[', d);
    if (i == std::string::npos) return out;
    ++i;
    std::vector<std::string> row;
    while (i < json.size()) {
        const size_t open = json.find_first_of("[]", i);
        if (open == std::string::npos || json[open] == ']') break;
        i = ReadArray(json, open + 1, row);
        if ((int)row.size() <= (std::max)({cDes, cJd, cDist})) continue;
        NeoCandidate n;
        n.des = Trim(row[(size_t)cDes]);
        n.cdJd = atof(row[(size_t)cJd].c_str());
        n.distAu = atof(row[(size_t)cDist].c_str());
        if (cH >= 0 && (int)row.size() > cH && !row[(size_t)cH].empty())
            n.h = atof(row[(size_t)cH].c_str());
        n.fullname = (cName >= 0 && (int)row.size() > cName) ? Trim(row[(size_t)cName]) : n.des;
        // "(2024 YR4)" -> "2024 YR4"
        if (n.fullname.size() > 2 && n.fullname.front() == '(' && n.fullname.back() == ')')
            n.fullname = n.fullname.substr(1, n.fullname.size() - 2);
        if (!n.des.empty()) out.push_back(n);
    }
    return out;
}

// The window asked for, as calendar dates CAD accepts.
inline std::string CadPath(double nowJd, int daysBack, int daysAhead, double maxDistAu) {
    auto ymd = [](double jd) {
        const time_t t = (time_t)((jd - 2440587.5) * 86400.0);
        struct tm g;
        gmtime_s(&g, &t);
        char b[16];
        snprintf(b, sizeof(b), "%04d-%02d-%02d", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday);
        return std::string(b);
    };
    char p[220];
    snprintf(p, sizeof(p),
             "/cad.api?date-min=%s&date-max=%s&dist-max=%.3f&body=Earth&fullname=true&sort=dist",
             ymd(nowJd - daysBack).c_str(), ymd(nowJd + daysAhead).c_str(), maxDistAu);
    return p;
}

// What Horizons is asked for. A number names a numbered asteroid; a
// provisional designation has to be marked as one, or Horizons searches
// names and can come back with a list instead of a body.
inline std::string HorizonsCommandFor(const std::string& des) {
    const bool numbered = !des.empty() &&
        std::all_of(des.begin(), des.end(), [](char c) { return c >= '0' && c <= '9'; });
    return numbered ? des + ";" : "DES=" + des + ";";
}

// IAU H-G apparent magnitude, G = 0.15.
inline double ApparentMag(double h, double sunDistAu, double earthDistAu, double phaseDeg) {
    if (sunDistAu <= 0 || earthDistAu <= 0) return 99.0;
    const double a = phaseDeg * 3.14159265358979 / 180.0;
    const double t = std::tan(a / 2.0);
    const double phi1 = std::exp(-3.33 * std::pow(t, 0.63));
    const double phi2 = std::exp(-1.87 * std::pow(t, 1.22));
    const double g = 0.15;
    const double phase = (1.0 - g) * phi1 + g * phi2;
    if (phase <= 0) return 99.0;
    return h + 5.0 * std::log10(sunDistAu * earthDistAu) - 2.5 * std::log10(phase);
}

struct NeoView {
    bool   ok = false;
    double altDeg = 0, azDeg = 0, raHours = 0, decDeg = 0;
    double mag = 99;
    double earthDistAu = 0;
    // Against the stars, not the horizon: the sky's own turn is the same
    // for everything on the map and says nothing about the object.
    double rateDegPerHour = 0;
    double raNext = 0, decNext = 0;   // an hour on, for the map's tick
};

// Where it is from the site at jd, and how bright, given its table and
// the Sun's (both geocentric).
inline NeoView ViewNeo(const Ephemeris& neo, const Ephemeris& sun, double h,
                       double jd, const LunarObserver& site) {
    NeoView v;
    LVec rn, rs;
    if (!EphemerisAt(neo, jd, rn)) return v;
    const SatLook l = EphemLookAt(neo, jd, site);
    if (!l.ok) return v;
    v.altDeg = l.altDeg; v.azDeg = l.azDeg; v.raHours = l.raHours; v.decDeg = l.decDeg;
    v.earthDistAu = std::sqrt(rn.x * rn.x + rn.y * rn.y + rn.z * rn.z) / kAuKm;
    if (EphemerisAt(sun, jd, rs)) {
        const double dx = rn.x - rs.x, dy = rn.y - rs.y, dz = rn.z - rs.z;
        const double sunDist = std::sqrt(dx * dx + dy * dy + dz * dz) / kAuKm;
        // Phase angle: at the object, between the Sun and the Earth.
        const double c = (-dx * -rn.x + -dy * -rn.y + -dz * -rn.z) /
                         (sunDist * kAuKm * v.earthDistAu * kAuKm);
        const double phase = std::acos((std::max)(-1.0, (std::min)(1.0, c))) * 180.0 / 3.14159265358979;
        v.mag = ApparentMag(h, sunDist, v.earthDistAu, phase);
    }
    const SatLook n = EphemLookAt(neo, jd + 1.0 / 24.0, site);
    if (n.ok) {
        v.raNext = n.raHours; v.decNext = n.decDeg;
        const double d2r = 3.14159265358979 / 180.0;
        const double cs = std::sin(l.decDeg * d2r) * std::sin(n.decDeg * d2r) +
                          std::cos(l.decDeg * d2r) * std::cos(n.decDeg * d2r) *
                          std::cos((n.raHours - l.raHours) * 15.0 * d2r);
        v.rateDegPerHour = std::acos((std::max)(-1.0, (std::min)(1.0, cs))) / d2r;
    } else {
        v.raNext = v.raHours; v.decNext = v.decDeg;
    }
    v.ok = true;
    return v;
}

} // namespace raduk::sky
