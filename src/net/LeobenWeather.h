#pragma once
// ============================================================
// LeobenWeather.h -- weather and sky conditions from Leoben
//
// Leoben is the owner's assistant on another PC on the home network
// (Maligzar/Leoben). It already runs an ensemble weather service for
// the observing site -- Open-Meteo, 7Timer, GFS and NWS, an imaging
// score, wind stability, dew risk -- and serves the latest snapshot at
// GET /weather/current. RadukCapture reads that rather than building a
// second weather service of its own.
//
// This header is the pure half: parsing the reply, converting units,
// and the one piece of physics that matters -- see StationPressureHpa.
// LeobenClient.h does the fetching. tests/leoben_weather.cpp gates this.
//
// THE PRESSURE TRAP. Leoben's pressure_inhg is SEA-LEVEL pressure
// (Open-Meteo's pressure_msl), as every weather report quotes it. The
// ADC model needs the pressure of the air actually above the
// telescope, which at the site's elevation is several percent lower.
// Feeding the sea-level figure straight in would overstate dispersion
// by that much -- the error Phase 66 removed from the elevation model,
// reintroduced through the front door. It is reduced to station
// pressure here, with the site's own elevation from rig.ini.
// ============================================================

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace raduk::net {

struct LeobenConditions {
    bool        valid = false;
    int64_t     fetchedUnix = 0;       // when RadukCapture received it (UTC s)

    // As Leoben reports them, in Leoben's units.
    double      cloudPct       = 0;
    double      tempF          = 0;
    double      humidityPct    = 0;
    double      windMph        = 0;
    double      gustMph        = 0;
    double      dewPointF      = 0;
    double      pressureMslInHg= 0;    // SEA LEVEL -- see the header
    double      visibilityMi   = 0;
    int         imagingScore   = -1;   // 0-100, -1 = absent
    std::string imagingLabel;          // e.g. "EXCELLENT"
    std::string dewRisk;               // "safe" | "elevated" | "warning"
    std::string goNoGo;                // "GO" | "CAUTION" | "NO-GO"
    std::string capturedAt;            // Leoben's own ISO timestamp

    double TempC()      const { return (tempF - 32.0) * 5.0 / 9.0; }
    double DewPointC()  const { return (dewPointF - 32.0) * 5.0 / 9.0; }
    double WindMs()     const { return windMph * 0.44704; }
    double GustMs()     const { return gustMph * 0.44704; }
    double PressureMslHpa() const { return pressureMslInHg * 33.8639; }
};

// ── Minimal JSON field reads ─────────────────────────────────
//
// No JSON library is in the tree, and the reply's keys are unique
// within it, so a key-then-value scan is enough. Whitespace around the
// colon is allowed (Python and FastAPI differ on whether they emit it).
// A key must be found as a whole quoted key: "cloud_pct" does not
// match inside "max_cloud_pct".

namespace leoben_detail {
inline size_t ValueStart(const std::string& j, const char* key) {
    const std::string k = std::string("\"") + key + "\"";
    size_t pos = 0;
    while ((pos = j.find(k, pos)) != std::string::npos) {
        size_t p = pos + k.size();
        while (p < j.size() && (j[p] == ' ' || j[p] == '\t' || j[p] == '\r' || j[p] == '\n')) ++p;
        if (p < j.size() && j[p] == ':') {
            ++p;
            while (p < j.size() && (j[p] == ' ' || j[p] == '\t' || j[p] == '\r' || j[p] == '\n')) ++p;
            return p;
        }
        pos += k.size();
    }
    return std::string::npos;
}
} // namespace leoben_detail

// A number, or false if the key is absent, null or not a number.
inline bool JsonNumber(const std::string& j, const char* key, double& out) {
    const size_t p = leoben_detail::ValueStart(j, key);
    if (p == std::string::npos || p >= j.size()) return false;
    const char c = j[p];
    if (!(c == '-' || (c >= '0' && c <= '9'))) return false;
    char* end = nullptr;
    const double v = std::strtod(j.c_str() + p, &end);
    if (end == j.c_str() + p || !std::isfinite(v)) return false;
    out = v;
    return true;
}

// A string without escapes in it -- every string read here is a short
// label or a timestamp. A string with a backslash is refused rather
// than half-decoded.
inline bool JsonString(const std::string& j, const char* key, std::string& out) {
    const size_t p = leoben_detail::ValueStart(j, key);
    if (p == std::string::npos || p >= j.size() || j[p] != '"') return false;
    const size_t e = j.find('"', p + 1);
    if (e == std::string::npos) return false;
    const std::string s = j.substr(p + 1, e - p - 1);
    if (s.find('\\') != std::string::npos || s.size() > 64) return false;
    out = s;
    return true;
}

inline bool JsonTrue(const std::string& j, const char* key) {
    const size_t p = leoben_detail::ValueStart(j, key);
    return p != std::string::npos && j.compare(p, 4, "true") == 0;
}

// The body of GET /weather/current for the home site. Valid needs
// "available": true and the four readings the app actually uses;
// everything else is optional and left at its default when absent.
inline bool ParseLeobenCurrent(const std::string& body, LeobenConditions& out) {
    out = LeobenConditions{};
    if (!JsonTrue(body, "available")) return false;
    bool ok = JsonNumber(body, "cloud_pct", out.cloudPct)
           && JsonNumber(body, "temp_f", out.tempF)
           && JsonNumber(body, "wind_mph", out.windMph)
           && JsonNumber(body, "pressure_inhg", out.pressureMslInHg);
    if (!ok) return false;
    // A pressure of 0 is Leoben's dataclass default, not a reading.
    if (out.pressureMslInHg < 25.0 || out.pressureMslInHg > 32.5) return false;
    JsonNumber(body, "humidity_pct", out.humidityPct);
    JsonNumber(body, "gust_mph",     out.gustMph);
    JsonNumber(body, "dew_point_f",  out.dewPointF);
    JsonNumber(body, "visibility_mi", out.visibilityMi);
    double score = -1;
    if (JsonNumber(body, "imaging_score", score) && score >= 0 && score <= 100)
        out.imagingScore = (int)std::lround(score);
    JsonString(body, "imaging_label", out.imagingLabel);
    JsonString(body, "dew_risk",      out.dewRisk);
    JsonString(body, "go_nogo",       out.goNoGo);
    JsonString(body, "captured_at",   out.capturedAt);
    out.valid = true;
    return true;
}

// ── Station pressure ─────────────────────────────────────────
//
// Sea-level pressure reduced to the site's elevation, using the site's
// temperature -- the inverse of the reduction the weather service made
// to get the sea-level figure in the first place (the hypsometric
// relation with a 6.5 K/km lapse rate, as in the WMO reduction):
//
//   P_st = P_msl * (1 - L*h / (T + L*h))^(g*M / (R*L))
//
// with T the station temperature in kelvin, L = 0.0065 K/m and the
// exponent 5.25588. At 0 m it returns P_msl unchanged.
inline double StationPressureHpa(double mslHpa, double elevM, double tempC) {
    constexpr double L = 0.0065;
    const double T = tempC + 273.15;
    if (elevM <= 0.0 || T < 150.0) return mslHpa;
    return mslHpa * std::pow(1.0 - L * elevM / (T + L * elevM), 5.25588);
}

// ── Where Leoben is ──────────────────────────────────────────
//
// LeobenUrl in rig.ini: "http://host:port", optionally with a trailing
// slash. Only http -- Leoben serves plain HTTP on the home network --
// and no path, user or query, because none of those has a meaning here
// and accepting them would only hide a typo.
struct LeobenEndpoint {
    std::wstring host;
    unsigned     port = 0;
};

inline bool ParseLeobenUrl(const std::string& url, LeobenEndpoint& out) {
    out = LeobenEndpoint{};
    const std::string pre = "http://";
    if (url.compare(0, pre.size(), pre) != 0) return false;
    std::string rest = url.substr(pre.size());
    if (!rest.empty() && rest.back() == '/') rest.pop_back();
    if (rest.empty() || rest.find_first_of("/?#@ ") != std::string::npos) return false;
    std::string host = rest;
    unsigned port = 80;
    const size_t colon = rest.rfind(':');
    if (colon != std::string::npos) {
        host = rest.substr(0, colon);
        const std::string ps = rest.substr(colon + 1);
        if (ps.empty() || ps.size() > 5 || ps.find_first_not_of("0123456789") != std::string::npos)
            return false;
        port = (unsigned)std::stoul(ps);
        if (port == 0 || port > 65535) return false;
    }
    if (host.empty()) return false;
    for (char c : host)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-'))
            return false;
    out.host.assign(host.begin(), host.end());
    out.port = port;
    return true;
}

// How old a reading may be and still be used for physics. Leoben
// refreshes current conditions every 10 minutes; past half an hour
// something has stopped and the standard atmosphere is the honest
// fallback.
constexpr int64_t kLeobenFreshSec = 30 * 60;

inline bool LeobenFresh(const LeobenConditions& c, int64_t nowUnix) {
    return c.valid && c.fetchedUnix > 0 && nowUnix - c.fetchedUnix <= kLeobenFreshSec
        && nowUnix >= c.fetchedUnix;
}

} // namespace raduk::net
