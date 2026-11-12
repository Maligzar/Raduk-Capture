// ============================================================
//  Phase 135.3: a stalled camera, measured in time
//
//  The capture loop restarted after 20 consecutive timeouts, which it
//  called ~500 ms and which was at least 10 s. It now restarts when no
//  frame has arrived for max(1.5 s, 10 frame intervals), and no single
//  SDK call may outlast that. Checked here with a fake clock, driving
//  the same WedgeClock the loop drives, call by call.
// ============================================================

#include "core/WedgeBudget.h"

#include <cstdio>
#include <string>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

// Simulate a camera that stops: every call times out after `callMs`.
// Returns the milliseconds from the last good frame to the restart, or -1
// if none came within `limitMs`.
static double TimeToRestart(double exposureMs, double fps, double limitMs) {
    const double budget = WedgeBudgetMs(exposureMs, fps);
    const int callMs = CallTimeoutMs(exposureMs, budget);
    WedgeClock c;
    uint64_t now = 1000000;   // arbitrary start, in us
    c.OnFrame(now);
    const uint64_t start = now;
    while (double(now - start) / 1000.0 < limitMs) {
        now += (uint64_t)callMs * 1000;
        if (c.OnTimeout(now, budget)) return double(now - start) / 1000.0;
    }
    return -1;
}

int main() {
    printf("\n=== Phase 135.3: wedge detection measured in time ===\n");

    printf("\n1. The budget\n");
    {
        Check(WedgeBudgetMs(8.0, 120.0) == 1500.0, "planetary: the 1.5 s floor");
        Check(WedgeBudgetMs(5000.0, 0.2) == 50000.0, "a 5 s exposure: ten frames, 50 s");
        Check(WedgeBudgetMs(8.0, 2.0) == 5000.0, "a slow measured rate counts: 2 fps gives 5 s");
        Check(WedgeBudgetMs(0.0, 0.0) == 1500.0 && WedgeBudgetMs(-5.0, -1.0) == 1500.0,
              "nothing known, or nonsense: the floor");
        Check(CallTimeoutMs(8.0, 1500.0) == 516, "planetary call timeout unchanged (ZWO's 2x + 500)");
        Check(CallTimeoutMs(1000.0, 1500.0) == 1500, "capped at the budget");
    }

    printf("\n2. A camera that stops\n");
    {
        const double old = 20 * (2 * 8.0 + 500);   // what the 20-timeout rule gave
        const double t = TimeToRestart(8.0, 120.0, 60000);
        Check(t >= 1500 && t <= 2100, "planetary: restarted within 1.5-2.1 s", std::to_string((int)t) + " ms");
        Check(t < old / 4, "  against the old rule's 10.3 s", std::to_string((int)old) + " ms then");
        const double lunar = TimeToRestart(2000.0, 0.5, 120000);
        Check(lunar >= 20000, "a 2 s exposure is never called a wedge in under 20 s",
              std::to_string((int)lunar) + " ms");
    }

    printf("\n3. The clock\n");
    {
        WedgeClock c;
        c.OnFrame(0);
        Check(!c.OnTimeout(1499000, 1500), "1499 ms: not yet");
        Check(c.OnTimeout(1500000, 1500), "1500 ms: a wedge");
        Check(!c.OnTimeout(2000000, 1500), "  and the clock restarts, so the next needs a full budget");
        c.OnFrame(5000000);
        Check(!c.OnTimeout(6000000, 1500), "a good frame resets it");
        Check(!c.OnTimeout(1000, 1500) && c.LastGoodUs() == 1000,
              "a clock that goes backwards does not fire, and re-bases");
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
