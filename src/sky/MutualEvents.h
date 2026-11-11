#pragma once
// ============================================================
//  MutualEvents -- phase 141.0: the Galilean moons eclipsing and
//  occulting one another, for IMCCE's PHEMU campaign
//
//  Around each Jupiter equinox (this one is 2026-12-16) the four large
//  moons pass in front of each other and through each other's shadows.
//  Light curves of those events, timed to better than 0.1 s, refine the
//  moons' orbits, and IMCCE collects them from observers worldwide. The
//  season runs from May 2026 to August 2027.
//
//  The predictions come from IMCCE's own service, Opale
//  (opale.imcce.fr/api/v1/phenomena/satellites/599), the API behind
//  ssp.imcce.fr/forms/satellites-events. They are fetched for the
//  owner's use and cached on this machine, never committed: IMCCE's
//  terms do not allow its material to be reproduced without permission.
//
//  This header is pure: parsing Opale's JSON, the cache line format,
//  and choosing tonight's events. PhemuDesk.h fetches and caches.
//  tests/mutual_events.cpp gates it, on synthetic data only.
//
//  The choice rules are IMCCE's, from its observer's guide
//  ("guide-observation-resume.pdf", PHEMU pages):
//    - long events can come 5 to 10 minutes off the prediction, so
//      start early and stop late;
//    - prefer a magnitude drop over 10%;
//    - prefer events more than 3 Jupiter radii from the limb, where the
//      planet's scattered light does not swamp the photometry;
//    - avoid grazing events, which the theory may not reproduce;
//    - prefer occultations to eclipses, and events involving Callisto;
//    - avoid twilight.
//  The margin rule and the thresholds' exact values are this file's
//  choices, stated where they are made.
// ============================================================

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace raduk::sky::phemu {

// ── A minimal JSON reader, enough for Opale's reply ──────────
//
// Objects, arrays, strings (with the common escapes), numbers, true,
// false and null. A malformed document parses to what it could read and
// sets ok = false; nothing here throws.
struct JVal {
    enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<JVal> a;
    std::vector<std::pair<std::string, JVal>> o;

    const JVal* Get(const char* key) const {
        if (kind != Obj) return nullptr;
        for (const auto& kv : o) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    double NumOr(double fallback) const { return kind == Num ? n : fallback; }
};

class JsonReader {
public:
    explicit JsonReader(const std::string& text) : t(text) {}
    bool Parse(JVal& out) {
        ok = true;
        Value(out, 0);
        Ws();
        return ok;
    }
private:
    const std::string& t;
    size_t p = 0;
    bool ok = true;

    void Ws() { while (p < t.size() && strchr(" \t\r\n", t[p])) p++; }
    bool Lit(const char* w) {
        const size_t n = strlen(w);
        if (t.compare(p, n, w) == 0) { p += n; return true; }
        return false;
    }
    void Value(JVal& v, int depth) {
        if (depth > 64) { ok = false; return; }
        Ws();
        if (p >= t.size()) { ok = false; return; }
        const char c = t[p];
        if (c == '{') Object(v, depth);
        else if (c == '[') Array(v, depth);
        else if (c == '"') { v.kind = JVal::Str; String(v.s); }
        else if (Lit("true"))  { v.kind = JVal::Bool; v.b = true; }
        else if (Lit("false")) { v.kind = JVal::Bool; v.b = false; }
        else if (Lit("null"))  { v.kind = JVal::Null; }
        else {
            char* end = nullptr;
            v.n = strtod(t.c_str() + p, &end);
            if (end == t.c_str() + p) { ok = false; return; }
            v.kind = JVal::Num;
            p = (size_t)(end - t.c_str());
        }
    }
    void String(std::string& s) {
        p++;   // opening quote
        while (p < t.size() && t[p] != '"') {
            if (t[p] == '\\' && p + 1 < t.size()) {
                const char e = t[++p];
                switch (e) {
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case 'r': s += '\r'; break;
                    case 'b': s += '\b'; break;
                    case 'f': s += '\f'; break;
                    case 'u': p += 4; s += '?'; break;   // not needed here
                    default:  s += e; break;
                }
                p++;
            } else {
                s += t[p++];
            }
        }
        if (p >= t.size()) { ok = false; return; }
        p++;   // closing quote
    }
    void Array(JVal& v, int depth) {
        v.kind = JVal::Arr;
        p++;
        Ws();
        if (p < t.size() && t[p] == ']') { p++; return; }
        while (ok && p < t.size()) {
            v.a.emplace_back();
            Value(v.a.back(), depth + 1);
            Ws();
            if (p < t.size() && t[p] == ',') { p++; continue; }
            if (p < t.size() && t[p] == ']') { p++; return; }
            ok = false;
        }
        ok = false;
    }
    void Object(JVal& v, int depth) {
        v.kind = JVal::Obj;
        p++;
        Ws();
        if (p < t.size() && t[p] == '}') { p++; return; }
        while (ok && p < t.size()) {
            Ws();
            if (p >= t.size() || t[p] != '"') { ok = false; return; }
            std::string key;
            String(key);
            Ws();
            if (p >= t.size() || t[p] != ':') { ok = false; return; }
            p++;
            v.o.emplace_back(std::move(key), JVal{});
            Value(v.o.back().second, depth + 1);
            Ws();
            if (p < t.size() && t[p] == ',') { p++; continue; }
            if (p < t.size() && t[p] == '}') { p++; return; }
            ok = false;
        }
        ok = false;
    }
};

// ── Time ─────────────────────────────────────────────────────

// "2026-11-11T00:06:15.575" (UTC) to a Julian date. Returns 0 on a
// string it cannot read.
inline double JdFromIsoUtc(const std::string& s) {
    int Y, M, D, h, m;
    double sec = 0.0;
    if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%lf", &Y, &M, &D, &h, &m, &sec) < 5) return 0.0;
    if (M < 1 || M > 12 || D < 1 || D > 31) return 0.0;
    int y = Y, mo = M;
    if (mo <= 2) { y -= 1; mo += 12; }
    const int A = y / 100;
    const int B = 2 - A + A / 4;
    const double jd0 = std::floor(365.25 * (y + 4716)) + std::floor(30.6001 * (mo + 1))
                     + D + B - 1524.5;
    return jd0 + (h + m / 60.0 + sec / 3600.0) / 24.0;
}

// A Julian date to "2026-11-11 00:06:15.6" UTC, for display and logs.
inline std::string IsoFromJd(double jd, int secDecimals = 1) {
    const double z0 = jd + 0.5;
    double Z = std::floor(z0), F = z0 - Z;
    double A = Z;
    if (Z >= 2299161.0) {
        const double alpha = std::floor((Z - 1867216.25) / 36524.25);
        A = Z + 1 + alpha - std::floor(alpha / 4.0);
    }
    const double B = A + 1524, C = std::floor((B - 122.1) / 365.25);
    const double Dd = std::floor(365.25 * C), E = std::floor((B - Dd) / 30.6001);
    const int day = (int)(B - Dd - std::floor(30.6001 * E));
    const int mon = (int)(E < 14 ? E - 1 : E - 13);
    const int year = (int)(mon > 2 ? C - 4716 : C - 4715);
    double secs = F * 86400.0;
    const double unit = std::pow(10.0, -secDecimals);
    secs = std::round(secs / unit) * unit;
    int hh = (int)(secs / 3600.0); secs -= hh * 3600.0;
    int mm = (int)(secs / 60.0);   secs -= mm * 60.0;
    char b[64];
    snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d:%0*.*f", year, mon, day, hh, mm,
             secDecimals > 0 ? secDecimals + 3 : 2, secDecimals, secs);
    return b;
}

// ── One event ────────────────────────────────────────────────

inline const char* MoonName(int body) {
    switch (body) {
        case 501: return "Io";
        case 502: return "Europa";
        case 503: return "Ganymede";
        case 504: return "Callisto";
        default:  return "?";
    }
}

struct Event {
    std::string kind;          // "Eclipse" or "Occultation"
    std::string sub;           // "Partial", "Annular", "Total", "Penumbral"
    int    by = 0, of = 0;     // eclipsing / occulting body, and the one dimmed
    double beginJd = 0, greatestJd = 0, endJd = 0;   // UTC
    double durationMin = 0;
    double dropPct = NAN;      // flux drop at greatest, % (Opale may omit it)
    double impact  = NAN;      // 0 central, 1 grazing
    double limbRadii = NAN;    // dimmed moon's distance from Jupiter's limb, in radii
    double sepArcsec = NAN;    // separation of the pair at the start

    // "Io eclipses Europa (partial)" -- the sentence an observer reads.
    std::string Describe() const {
        std::string v = (kind == "Occultation") ? " occults " : " eclipses ";
        std::string s = std::string(MoonName(by)) + v + MoonName(of);
        if (!sub.empty()) {
            std::string lo = sub;
            for (auto& c : lo) c = (char)tolower((unsigned char)c);
            s += " (" + lo + ")";
        }
        return s;
    }
};

inline bool IsGalilean(int body) { return body >= 501 && body <= 504; }

// Opale's reply to Events. Keeps only mutual events between two
// Galilean moons; Opale also predicts Jupiter's own eclipses and
// transits and the inner moons' events, which PHEMU does not ask for.
// Returns false if the document is not an Opale reply at all.
inline bool ParseOpale(const std::string& json, std::vector<Event>& out,
                       std::string* why = nullptr) {
    JVal root;
    JsonReader r(json);
    const bool parsed = r.Parse(root);
    const JVal* resp = root.Get("response");
    const JVal* data = resp ? resp->Get("data") : nullptr;
    // A reply cut off mid-list would otherwise yield the events before
    // the cut and look complete. A partial season is worse than none,
    // because a missing event looks like a quiet night.
    if (!parsed && data) {
        if (why) *why = "the reply was cut short";
        return false;
    }
    if (!data || data->kind != JVal::Arr) {
        if (why) {
            const JVal* title = root.Get("title");
            *why = title && title->kind == JVal::Str ? "IMCCE said: " + title->s
                 : parsed ? "no event list in the reply" : "the reply is not JSON";
        }
        return false;
    }
    for (const JVal& e : data->a) {
        const JVal* mutual = e.Get("mutualPhenomena");
        if (!mutual || mutual->kind != JVal::Bool || !mutual->b) continue;
        const JVal* by = e.Get("eclipsingBody");
        const JVal* of = e.Get("eclipsedBody");
        if (!by || !of) continue;
        Event ev;
        ev.by = (int)by->NumOr(0);
        ev.of = (int)of->NumOr(0);
        if (!IsGalilean(ev.by) || !IsGalilean(ev.of)) continue;
        if (const JVal* k = e.Get("type"))    if (k->kind == JVal::Str) ev.kind = k->s;
        if (const JVal* k = e.Get("subType")) if (k->kind == JVal::Str) ev.sub  = k->s;
        if (const JVal* k = e.Get("duration")) ev.durationMin = k->NumOr(0);
        if (const JVal* k = e.Get("dropNormalizedFlux")) ev.dropPct = k->NumOr(NAN);
        if (const JVal* k = e.Get("impactFactor")) ev.impact = k->NumOr(NAN);
        const JVal* evs = e.Get("events");
        if (!evs) continue;
        auto when = [&](const char* which, double& jd) {
            const JVal* w = evs->Get(which);
            const JVal* d = w ? w->Get("date") : nullptr;
            if (d && d->kind == JVal::Str) jd = JdFromIsoUtc(d->s);
            return w;
        };
        const JVal* begin = when("begin", ev.beginJd);
        const JVal* great = when("greatest", ev.greatestJd);
        when("end", ev.endJd);
        if (ev.greatestJd <= 0) continue;
        if (ev.beginJd <= 0) ev.beginJd = ev.greatestJd;
        if (ev.endJd <= 0)   ev.endJd   = ev.greatestJd;
        if (great) {
            if (const JVal* dl = great->Get("distanceLimb"))
                if (const JVal* rp = dl->Get("radiusPlanet")) ev.limbRadii = rp->NumOr(NAN);
        }
        if (begin) {
            if (const JVal* s = begin->Get("S")) ev.sepArcsec = s->NumOr(NAN);
        }
        out.push_back(ev);
    }
    std::sort(out.begin(), out.end(),
              [](const Event& a, const Event& b) { return a.greatestJd < b.greatestJd; });
    return true;
}

// ── The cache: one line per event ────────────────────────────
//
// Tab-separated, so the file reads in a text editor, and a fetch can be
// checked by eye against IMCCE's form. Lines starting '#' are comments.
inline std::string CacheLine(const Event& e) {
    char b[320];
    snprintf(b, sizeof(b), "%.8f\t%.8f\t%.8f\t%s\t%s\t%d\t%d\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f",
             e.beginJd, e.greatestJd, e.endJd, e.kind.c_str(),
             e.sub.empty() ? "-" : e.sub.c_str(), e.by, e.of, e.durationMin,
             e.dropPct, e.impact, e.limbRadii, e.sepArcsec);
    return b;
}

inline bool ParseCacheLine(const std::string& line, Event& e) {
    if (line.empty() || line[0] == '#') return false;
    std::vector<std::string> f;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, '\t')) f.push_back(cell);
    if (f.size() < 12) return false;
    auto num = [](const std::string& s) {
        if (s == "nan" || s == "-nan" || s == "NaN") return (double)NAN;
        return strtod(s.c_str(), nullptr);
    };
    e.beginJd = num(f[0]); e.greatestJd = num(f[1]); e.endJd = num(f[2]);
    e.kind = f[3];
    e.sub  = (f[4] == "-") ? "" : f[4];
    e.by = atoi(f[5].c_str()); e.of = atoi(f[6].c_str());
    e.durationMin = num(f[7]); e.dropPct = num(f[8]); e.impact = num(f[9]);
    e.limbRadii = num(f[10]); e.sepArcsec = num(f[11]);
    return e.greatestJd > 0 && IsGalilean(e.by) && IsGalilean(e.of);
}

// ── Recording window ─────────────────────────────────────────
//
// IMCCE: events over 20 minutes can arrive 5 to 10 minutes off the
// prediction, and the flat light before and after the dip is what the
// reduction measures the dip against. This file's rule: start and stop
// at least 10 minutes outside the predicted event, and for a long event
// by its own length, so the baseline on each side is as long as the dip.
inline double MarginMinutes(const Event& e) {
    return (std::max)(10.0, e.durationMin);
}
inline double RecordStartJd(const Event& e) { return e.beginJd - MarginMinutes(e) / 1440.0; }
inline double RecordEndJd(const Event& e)   { return e.endJd   + MarginMinutes(e) / 1440.0; }

// ── Tonight: what the site can see, and what IMCCE would pick ─

// What the sky is doing at one instant, from the app's own ephemeris.
struct SkyAt {
    bool   ok = false;
    double jupiterAltDeg = -90.0;
    double sunAltDeg = 90.0;
};
using SkyFn = std::function<SkyAt(double jdUt)>;

struct Limits {
    double minJupiterAltDeg = 15.0;   // this file's choice: below this the
                                      // extinction changes fast across an event
    double maxSunAltDeg = -6.0;       // civil twilight; IMCCE: avoid twilight
};

struct Pick {
    Event  ev;
    bool   wholeVisible = false;      // the recording window is all in the dark, Jupiter up
    bool   greatestVisible = false;   // at least the middle is
    double jupiterAltAtGreatest = 0.0;
    std::vector<std::string> cautions;   // IMCCE's reasons to prefer another
    int    rank = 0;                     // higher is better; for ordering only
};

// Events whose recording window overlaps [fromJd, toJd], with what the
// site can see of each and IMCCE's cautions. Not-visible events are
// kept only if keepInvisible, so the list can say what was missed.
inline std::vector<Pick> Tonight(const std::vector<Event>& all, double fromJd, double toJd,
                                 const SkyFn& sky, const Limits& lim = {},
                                 bool keepInvisible = false) {
    std::vector<Pick> out;
    for (const Event& e : all) {
        const double a = RecordStartJd(e), b = RecordEndJd(e);
        if (b < fromJd || a > toJd) continue;
        Pick p;
        p.ev = e;
        auto up = [&](double jd, double* alt) {
            const SkyAt s = sky(jd);
            if (alt) *alt = s.jupiterAltDeg;
            return s.ok && s.jupiterAltDeg >= lim.minJupiterAltDeg && s.sunAltDeg <= lim.maxSunAltDeg;
        };
        p.greatestVisible = up(e.greatestJd, &p.jupiterAltAtGreatest);
        p.wholeVisible = p.greatestVisible && up(a, nullptr) && up(b, nullptr);
        if (!p.greatestVisible && !keepInvisible) continue;

        int rank = 0;
        if (p.wholeVisible) rank += 4;
        if (std::isnan(e.dropPct)) p.cautions.push_back("depth not predicted");
        else if (e.dropPct < 10.0) p.cautions.push_back("shallow: under 10% drop");
        else rank += 2;
        if (!std::isnan(e.limbRadii) && e.limbRadii < 3.0) p.cautions.push_back("within 3 radii of Jupiter's limb");
        else rank += 1;
        if (!std::isnan(e.impact) && e.impact > 0.9) p.cautions.push_back("grazing");
        if (e.sub == "Penumbral") p.cautions.push_back("penumbral only");
        if (e.kind == "Occultation") rank += 1;
        if (e.by == 504 || e.of == 504) rank += 1;
        if (!p.wholeVisible && p.greatestVisible) p.cautions.push_back("not all of it in the dark with Jupiter up");
        p.rank = rank;
        out.push_back(p);
    }
    return out;
}

// One line per event for the log, the notification bar and the
// assistant: "2026-12-14 03:12:40.1 UTC  Io occults Europa (partial),
// 4.2 min, 38% drop, record 03:00-03:24, Jupiter 52 deg".
inline std::string PickLine(const Pick& p) {
    const Event& e = p.ev;
    char b[384];
    char drop[32];
    if (std::isnan(e.dropPct)) snprintf(drop, sizeof(drop), "drop n/a");
    else snprintf(drop, sizeof(drop), "%.0f%% drop", e.dropPct);
    const std::string g = IsoFromJd(e.greatestJd, 1);
    const std::string s = IsoFromJd(RecordStartJd(e), 0).substr(11, 5);
    const std::string t = IsoFromJd(RecordEndJd(e), 0).substr(11, 5);
    snprintf(b, sizeof(b), "%s UTC  %s, %.1f min, %s, record %s-%s, Jupiter %.0f deg",
             g.c_str(), e.Describe().c_str(), e.durationMin, drop, s.c_str(), t.c_str(),
             p.jupiterAltAtGreatest);
    std::string line = b;
    if (!p.cautions.empty()) {
        line += "  [";
        for (size_t i = 0; i < p.cautions.size(); i++) {
            if (i) line += "; ";
            line += p.cautions[i];
        }
        line += "]";
    }
    return line;
}

// The Opale request path for nbd days from a UTC date: the API that
// IMCCE's satellites-events form calls. 599 is Jupiter's system.
inline std::string OpalePath(int year, int month, int day, int nbd) {
    char b[160];
    snprintf(b, sizeof(b),
             "/api/v1/phenomena/satellites/599?date=%04d-%02d-%02dT00:00:00.000&timescale=UTC&nbd=%d",
             year, month, day, nbd);
    return b;
}

}  // namespace raduk::sky::phemu
