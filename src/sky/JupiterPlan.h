#pragma once
// ============================================================
//  JupiterPlan.h -- phase 144: Jupiter's events put to work
//
//  Phase 137 lists the Red Spot's transits and the moons' events on J.
//  This turns them into answers the rest of the app can use:
//
//    OnDisc(jd)        what is on the disc at that moment: the Red Spot
//                      within its window of the meridian, moons and
//                      shadows crossing, moons hidden.
//    Prime             a moment worth the derotation window: the Red
//                      Spot near the meridian, or a moon or its shadow
//                      on the disc. Autorun does not spend one on a
//                      focus sweep (AutorunRunner, deferFocusInPrime).
//    PrimeEndJd        when the present prime spell ends, for the log.
//    TonightLines      the next hours for the assistant's snapshot.
//
//  The Red Spot window is +-50 minutes of its transit: about +-30 deg
//  of longitude at System II's rate. That is the usual amateur reckoning
//  for "well placed" (inferred from common practice, not a measured
//  limit); foreshortening grows quickly beyond it. A constant here, not
//  a setting, until a night says otherwise.
//
//  Pure: tests/jupiter_plan.cpp.
// ============================================================

#include "JupiterEvents.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace raduk::sky::jupiter {

// System II turns once in 9h 55m 40.6s.
inline constexpr double kSysIIDegPerMin = 360.0 / 595.677;
inline constexpr double kGrsWindowMin   = 50.0;

// Minutes from the Red Spot's transit: negative before it, positive
// after, within half a rotation.
inline double GrsMinutesFromMeridian(const GrsModel& g, double jd) {
    double a = CentralMeridians(jd).sysII - g.LongitudeAt(jd);
    while (a > 180) a -= 360;
    while (a < -180) a += 360;
    return a / kSysIIDegPerMin;
}

struct DiscState {
    double grsMin = 0;          // GrsMinutesFromMeridian
    bool   grsNear = false;     // within kGrsWindowMin
    std::vector<Event> active;  // events under way at the moment
    bool Prime() const {
        if (grsNear) return true;
        for (const auto& e : active)
            if (e.kind == Kind::Transit || e.kind == Kind::ShadowTransit) return true;
        return false;
    }
};

inline DiscState OnDisc(double jd, const GrsModel& g, const std::vector<Event>& events) {
    DiscState d;
    d.grsMin = GrsMinutesFromMeridian(g, jd);
    d.grsNear = std::fabs(d.grsMin) <= kGrsWindowMin;
    for (const auto& e : events)
        if (e.startJd <= jd && jd <= e.endJd) d.active.push_back(e);
    return d;
}

// "Red Spot 12 min past the meridian; Io shadow transit; Europa transit",
// or "" when nothing is on or behind the disc.
inline std::string DiscLine(const DiscState& d) {
    std::string out;
    auto add = [&](const std::string& s) { if (!out.empty()) out += "; "; out += s; };
    if (d.grsNear) {
        char b[64];
        const int m = (int)std::lround(d.grsMin);
        if (m == 0)     snprintf(b, sizeof(b), "Red Spot on the meridian");
        else if (m < 0) snprintf(b, sizeof(b), "Red Spot %d min before the meridian", -m);
        else            snprintf(b, sizeof(b), "Red Spot %d min past the meridian", m);
        add(b);
    }
    for (const auto& e : d.active)
        add(std::string(MoonName(e.moon)) + " " + KindName(e.kind));
    return out;
}

// When the prime spell under way at jd ends: the latest end among what
// makes it prime, the Red Spot's window included. jd itself if not prime.
// One step only: an event starting inside the spell is not chained.
inline double PrimeEndJd(double jd, const GrsModel& g, const std::vector<Event>& events) {
    const DiscState d = OnDisc(jd, g, events);
    double end = jd;
    if (d.grsNear) end = (std::max)(end, jd + (kGrsWindowMin - d.grsMin) / 1440.0);
    for (const auto& e : d.active)
        if (e.kind == Kind::Transit || e.kind == Kind::ShadowTransit) end = (std::max)(end, e.endJd);
    return end;
}

// The next `hours` for the assistant, one line each, time order: the Red
// Spot's transits and the moons' events, those for which `up` holds at
// either end (Jupiter up and the sky dark at the site; the caller judges
// that). Empty when there is nothing.
inline std::vector<std::string> TonightLines(double nowJd, double hours, const GrsModel& g,
                                             const std::vector<Event>& events,
                                             const std::function<bool(double)>& up) {
    struct Item { double jd; std::string text; };
    std::vector<Item> items;
    const double to = nowJd + hours / 24.0;
    for (double t : GrsTransits(g, nowJd - kGrsWindowMin / 1440.0, to)) {
        if (up && !up(t)) continue;
        items.push_back({t, "Red Spot on the meridian " + HhMm(t) + " UTC (well placed " +
                            HhMm(t - kGrsWindowMin / 1440.0) + " to " +
                            HhMm(t + kGrsWindowMin / 1440.0) + ")"});
    }
    for (const auto& e : events) {
        if (e.endJd < nowJd || e.startJd > to) continue;
        if (up && !up(e.startJd) && !up(e.endJd)) continue;
        items.push_back({e.startJd, EventLine(e)});
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.jd < b.jd; });
    std::vector<std::string> out;
    for (auto& i : items) out.push_back(std::move(i.text));
    return out;
}

}  // namespace raduk::sky::jupiter
