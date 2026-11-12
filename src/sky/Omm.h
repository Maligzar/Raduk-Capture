#pragma once
// ============================================================
//  Omm.h -- phase 136: satellite elements as OMM, not TLE
//
//  CelesTrak's five-digit catalog numbers ran out on 2026-07-11, and the
//  two-line format cannot carry six. The newest objects, the rocket
//  bodies this rig chases, either never appear in TLE output or appear
//  under an Alpha-5 number, and nothing said so. OMM (CCSDS Orbit Mean-
//  Elements Message) carries the same SGP4 mean elements with the
//  catalog number as a plain integer; CelesTrak serves it as JSON from
//  the same gp.php query with FORMAT=json.
//
//  This reads that JSON into the same Tle struct SGP4 already takes, so
//  the propagator and everything downstream are unchanged. The parser
//  is deliberately small: CelesTrak's reply is an array of flat objects
//  of strings and numbers, and several replies may be concatenated in
//  one cache file.
//
//  Pure: tests/omm_elements.cpp.
// ============================================================

#include "Sgp4.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace raduk::sky {

// "2026-11-12T03:41:22.123456" (UTC) to a Julian date.
inline double JdFromOmmEpoch(const std::string& s) {
    int y = 0, mo = 0, d = 0, hh = 0, mm = 0; double ss = 0;
    if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%lf", &y, &mo, &d, &hh, &mm, &ss) < 5) return 0;
    if (mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    const int a = (14 - mo) / 12, yy = y + 4800 - a, m3 = mo + 12 * a - 3;
    const long jdn = d + (153 * m3 + 2) / 5 + 365L * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
    return (double)jdn - 0.5 + (hh + mm / 60.0 + ss / 3600.0) / 24.0;
}

// The flat objects of a JSON text, as key -> raw value (strings without
// their quotes). Nested values are not expected and are skipped.
inline std::vector<std::map<std::string, std::string>> OmmObjects(const std::string& t) {
    std::vector<std::map<std::string, std::string>> out;
    size_t i = 0;
    const size_t n = t.size();
    auto ws = [&] { while (i < n && (t[i] == ' ' || t[i] == '\n' || t[i] == '\r' || t[i] == '\t')) ++i; };
    auto str = [&](std::string& v) {           // at the opening quote
        v.clear(); ++i;
        while (i < n && t[i] != '"') {
            if (t[i] == '\\' && i + 1 < n) { ++i; v += (t[i] == 'n') ? '\n' : t[i]; }
            else v += t[i];
            ++i;
        }
        ++i;
    };
    while (i < n) {
        if (t[i] != '{') { ++i; continue; }
        ++i;
        std::map<std::string, std::string> obj;
        bool bad = false;
        for (;;) {
            ws();
            if (i >= n) { bad = true; break; }
            if (t[i] == '}') { ++i; break; }
            if (t[i] == ',') { ++i; continue; }
            if (t[i] != '"') { bad = true; break; }
            std::string k, v; str(k); ws();
            if (i >= n || t[i] != ':') { bad = true; break; }
            ++i; ws();
            if (i < n && t[i] == '"') str(v);
            else if (i < n && (t[i] == '{' || t[i] == '[')) { bad = true; break; }
            else { const size_t s = i; while (i < n && t[i] != ',' && t[i] != '}') ++i; v = t.substr(s, i - s);
                   while (!v.empty() && (v.back() == ' ' || v.back() == '\n' || v.back() == '\r')) v.pop_back(); }
            obj[k] = v;
        }
        if (bad) break;   // a truncated reply: keep what was whole
        out.push_back(std::move(obj));
    }
    return out;
}

inline Tle TleFromOmm(const std::map<std::string, std::string>& o) {
    Tle t;
    auto get = [&](const char* k) -> const std::string* { auto it = o.find(k); return it == o.end() ? nullptr : &it->second; };
    auto num = [&](const char* k, double& v) { const std::string* s = get(k); if (!s || s->empty() || *s == "null") return false; v = std::atof(s->c_str()); return true; };
    const std::string* name = get("OBJECT_NAME");
    t.name = name ? *name : std::string();
    double cat = 0, mm = 0, ecc = -1, inc = 0, raan = 0, argp = 0, ma = 0, bstar = 0;
    const std::string* ep = get("EPOCH");
    if (!num("NORAD_CAT_ID", cat) || !ep || !num("MEAN_MOTION", mm) || !num("ECCENTRICITY", ecc) ||
        !num("INCLINATION", inc) || !num("RA_OF_ASC_NODE", raan) || !num("ARG_OF_PERICENTER", argp) ||
        !num("MEAN_ANOMALY", ma)) {
        t.why = "an OMM record missing a mean element";
        return t;
    }
    num("BSTAR", bstar);
    t.catalog = (int)std::llround(cat);
    if (t.name.empty()) t.name = "#" + std::to_string(t.catalog);
    t.epochJd = JdFromOmmEpoch(*ep);
    if (t.epochJd <= 0) { t.why = "an OMM epoch that does not read"; return t; }
    t.bstar = bstar;
    t.inclRad = inc * sgp4c::D2R; t.raanRad = raan * sgp4c::D2R; t.ecc = ecc;
    t.argpRad = argp * sgp4c::D2R; t.meanAnomRad = ma * sgp4c::D2R;
    t.revPerDay = mm;
    t.meanMotionRadMin = mm / sgp4c::XPDOTP;
    if (t.meanMotionRadMin <= 0.0 || ecc < 0.0 || ecc >= 1.0) { t.why = "OMM elements out of range"; return t; }
    t.valid = true;
    return t;
}

// Every usable element set in the text, one per catalog number (the
// newest epoch wins where groups overlap). dropped counts the rest.
inline std::vector<Tle> ParseOmmJson(const std::string& text, int* dropped = nullptr) {
    std::map<int, Tle> byCat;
    int bad = 0;
    for (const auto& o : OmmObjects(text)) {
        const Tle t = TleFromOmm(o);
        if (!t.valid) { ++bad; continue; }
        auto it = byCat.find(t.catalog);
        if (it == byCat.end() || t.epochJd > it->second.epochJd) byCat[t.catalog] = t;
    }
    std::vector<Tle> out;
    out.reserve(byCat.size());
    for (auto& kv : byCat) out.push_back(kv.second);
    if (dropped) *dropped = bad;
    return out;
}

}  // namespace raduk::sky
