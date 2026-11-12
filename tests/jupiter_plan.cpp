// ============================================================
//  Phase 144: Jupiter's events put to work (src/sky/JupiterPlan.h)
//
//    1. The Red Spot's distance from the meridian in minutes: zero at
//       the transit phase 137 finds, signed, and moving at System II's
//       rate; inside the window at +-40 min, outside at +-60.
//    2. What is on the disc: a shadow transit is prime, an occultation
//       alone is not, and the line says what is there.
//    3. When a prime spell ends: the Red Spot's window, or the latest
//       transit under way.
//    4. The assistant's lines: time order, and only what the site sees.
// ============================================================

#include "sky/JupiterPlan.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace raduk::sky::jupiter;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}
static std::string F(double v, int d = 2) { char b[48]; snprintf(b, sizeof(b), "%.*f", d, v); return b; }
static constexpr double kMin = 1.0 / 1440.0;

int main() {
    printf("Jupiter plan (phase 144)\n");
    const GrsModel g;   // the defaults: 91 deg on 2026-06-01
    const double from = JdFromIsoDate("2026-12-01");
    const auto tr = GrsTransits(g, from, from + 1.0);
    if (tr.empty()) { printf("  [FAIL] no Red Spot transit found to work from\n"); return 1; }
    const double t = tr.front();

    printf("\n1. The Red Spot against the meridian\n");
    Check(std::fabs(GrsMinutesFromMeridian(g, t)) < 0.1, "zero at the transit", F(GrsMinutesFromMeridian(g, t), 3));
    Check(std::fabs(GrsMinutesFromMeridian(g, t + 30 * kMin) - 30) < 0.2, "+30 min half an hour after",
          F(GrsMinutesFromMeridian(g, t + 30 * kMin)));
    Check(std::fabs(GrsMinutesFromMeridian(g, t - 30 * kMin) + 30) < 0.2, "-30 min half an hour before",
          F(GrsMinutesFromMeridian(g, t - 30 * kMin)));
    const std::vector<Event> none;
    Check(OnDisc(t + 40 * kMin, g, none).grsNear && OnDisc(t - 40 * kMin, g, none).grsNear,
          "inside the window at +-40 min");
    Check(!OnDisc(t + 60 * kMin, g, none).grsNear && !OnDisc(t - 60 * kMin, g, none).grsNear,
          "outside it at +-60 min");
    Check(!OnDisc(t + 5 * 60 * kMin, g, none).Prime(), "half a rotation away, nothing prime");

    printf("\n2. What is on the disc\n");
    std::vector<Event> ev;
    ev.push_back({1, Kind::ShadowTransit, t + 120 * kMin, t + 240 * kMin});
    ev.push_back({2, Kind::Occultation,   t + 180 * kMin, t + 210 * kMin});
    ev.push_back({3, Kind::Transit,       t + 20 * kMin,  t + 70 * kMin});
    {
        const DiscState d = OnDisc(t + 195 * kMin, g, ev);
        Check(d.active.size() == 2 && d.Prime(), "a shadow transit with an occultation is prime");
        Check(DiscLine(d) == "Io shadow transit; Europa occultation", "the line names both", "'" + DiscLine(d) + "'");
    }
    {
        std::vector<Event> occ{ev[1]};
        Check(!OnDisc(t + 195 * kMin, g, occ).Prime(), "an occultation alone is not");
    }
    {
        const std::string l = DiscLine(OnDisc(t + 12 * kMin, g, none));
        Check(l == "Red Spot 12 min past the meridian", "the Red Spot's line", "'" + l + "'");
        const std::string b = DiscLine(OnDisc(t - 25 * kMin, g, none));
        Check(b == "Red Spot 25 min before the meridian", "and before it", "'" + b + "'");
        Check(DiscLine(OnDisc(t + 300 * kMin, g, none)).empty(), "empty when nothing is there");
    }

    printf("\n3. When a spell ends\n");
    Check(std::fabs((PrimeEndJd(t, g, none) - t) / kMin - 50) < 0.5, "the Red Spot's window: 50 min after the transit",
          F((PrimeEndJd(t, g, none) - t) / kMin, 1) + " min");
    Check(std::fabs((PrimeEndJd(t + 30 * kMin, g, ev) - t) / kMin - 70) < 0.01,
          "Ganymede's transit outlasts the Red Spot's window", F((PrimeEndJd(t + 30 * kMin, g, ev) - t) / kMin, 1) + " min");
    Check(std::fabs((PrimeEndJd(t + 150 * kMin, g, ev) - t) / kMin - 240) < 0.01, "Io's shadow ends the spell at 240 min");
    Check(PrimeEndJd(t + 300 * kMin, g, ev) == t + 300 * kMin, "not prime: the moment itself");

    printf("\n4. The assistant's lines\n");
    {
        const auto all = TonightLines(t - 120 * kMin, 12.0, g, ev, nullptr);
        // Two Red Spot transits (t and one rotation later) and three events.
        Check(all.size() == 5, "two transits and three events in 12 hours", std::to_string(all.size()));
        Check(!all.empty() && all.front().rfind("Red Spot on the meridian", 0) == 0, "time order: the Red Spot first");
        Check(all.size() > 1 && all[1].rfind("Ganymede transit", 0) == 0, "then Ganymede's transit", all.size() > 1 ? all[1] : "");
        const auto seen = TonightLines(t - 120 * kMin, 12.0, g, ev,
                                       [&](double jd) { return jd < t + 5.0 / 24.0; });
        Check(seen.size() == 4, "the second transit, Jupiter set, is left out", std::to_string(seen.size()));
        const auto later = TonightLines(t + 300 * kMin, 1.0, g, ev, nullptr);
        Check(later.empty(), "an empty hour gives no lines");
    }

    printf(g_fail ? "\n%d FAILED\n" : "\nall passed\n", g_fail);
    return g_fail ? 1 : 0;
}
