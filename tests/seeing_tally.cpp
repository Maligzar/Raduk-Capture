// ============================================================
//  Phase 138: the seeing forecast logged against the measured sky
// ============================================================

#include "core/SeeingTally.h"

#include <cstdio>
#include <string>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-60s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}
static bool Has(const std::string& s, const char* p) { return s.find(p) != std::string::npos; }

int main() {
    printf("\n=== Phase 138: seeing forecast against the sky ===\n");

    printf("\n1. The hour\n");
    {
        SeeingTally t;
        Check(!t.Due(1000), "the first call only starts the clock");
        Check(!t.Due(1000 + 3599), "not due before the hour");
        Check(t.Due(1000 + 3600), "due at the hour");
        t.TakeLine(4600);
        Check(!t.Due(4601) && t.Due(8200), "taking the line starts the next hour");
    }

    printf("\n2. The line\n");
    {
        SeeingTally t;
        t.Due(0);
        const std::string none = t.TakeLine(3600);
        Check(Has(none, "forecast none from Leoben") && Has(none, "measured none"),
              "nothing known says so, both halves", none);

        SeeingForecast f;
        f.seeing7Timer = 3; f.jet200Kmh = 95; f.jet250Kmh = 131;
        t.Forecast(f);
        for (double v : { 30.0, 34.0, 32.0, 50.0 }) t.Measured(v, "Jupiter");
        t.Measured(0.0, "Jupiter");   // no disc: not a sample
        const std::string l = t.TakeLine(7200);
        Check(Has(l, "7Timer seeing 3 of 8") && Has(l, "jet 200 hPa 95 km/h") && Has(l, "jet 250 hPa 131 km/h"),
              "the forecast, every source given", l);
        Check(Has(l, "(jet above the bad-seeing line)"), "  and a jet over 126 km/h is called out");
        Check(Has(l, "median 33.0, best 50.0 over 4 samples") && Has(l, "target Jupiter"),
              "the measured median, best and count, and the target");

        const std::string next = t.TakeLine(10800);
        Check(Has(next, "7Timer seeing 3 of 8") && Has(next, "measured none"),
              "the forecast carries over; the measurements do not");

        SeeingForecast empty;
        t.Forecast(empty);
        Check(Has(t.TakeLine(14400), "7Timer seeing 3"), "an empty forecast does not erase the last one");
        t.ClearForecast();
        Check(Has(t.TakeLine(18000), "forecast none from Leoben"),
              "but a stale feed does: an old forecast is never paired with new seeing");
    }

    printf("\n3. A new target is a new scale\n");
    {
        SeeingTally t;
        t.Measured(80, "Saturn");
        t.Measured(10, "Jupiter");
        t.Measured(12, "Jupiter");
        const std::string l = t.TakeLine(3600);
        Check(Has(l, "over 2 samples") && Has(l, "target Jupiter") && !Has(l, "80"),
              "Saturn's samples go when the target changes", l);
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
