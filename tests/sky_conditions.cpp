// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// ============================================================
//  Sky conditions — airmass, the imaging window, and the trend
//
//  Altitude is the biggest thing the observer controls on a planetary
//  night. These turn it into something actionable, so the numbers want
//  checking against values that are known independently rather than
//  against whatever the code happens to produce.
// ============================================================

#include "core/SkyConditions.h"

#include <cstdio>
#include <cmath>
#include <string>

using namespace raduk::core;

static int g_fail = 0;

static void Check(bool ok, const std::string& what, const std::string& detail) {
    printf("  [%s] %-56s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static void CheckNear(double got, double want, double tol,
                      const std::string& what) {
    char d[128];
    snprintf(d, sizeof(d), "got %.4f, expected %.4f +/- %.4f", got, want, tol);
    Check(std::fabs(got - want) <= tol, what, d);
}

int main() {
    printf("\n=== Sky conditions ===\n");

    // ── 1. Airmass against known values ──────────────────────
    printf("\n1. Airmass\n");
    {
        CheckNear(Airmass(90.0), 1.000, 0.001, "the zenith is 1.00 by definition");

        // Away from the horizon Kasten-Young and the schoolbook 1/sin(h)
        // agree closely, so these can be checked against a value anyone
        // can work out by hand.
        CheckNear(Airmass(60.0), 1.0 / std::sin(60.0 * 3.14159265 / 180.0),
                  0.005, "60 deg matches 1/sin(h) -- about 1.15");
        CheckNear(Airmass(30.0), 2.000, 0.02, "30 deg is about 2 air masses");

        // Near the horizon the two part company, and that is the whole
        // reason for using this formula. 1/sin(5 deg) says 11.5; the
        // real figure is near 10.3.
        const double naive = 1.0 / std::sin(5.0 * 3.14159265 / 180.0);
        char d[128];
        snprintf(d, sizeof(d), "Kasten-Young %.2f vs naive 1/sin %.2f",
                 Airmass(5.0), naive);
        Check(Airmass(5.0) < naive - 0.5,
              "at 5 deg it beats 1/sin, which is why it is used", d);

        Check(Airmass(-5.0) >= 39.0, "below the horizon saturates rather than "
              "going negative", std::to_string(Airmass(-5.0)));
        Check(Airmass(0.5) <= 40.0, "and is bounded at the horizon", "");
    }

    // ── 2. What the airmass costs ────────────────────────────
    printf("\n2. Resolution cost\n");
    {
        CheckNear(SeeingFactor(90.0), 1.000, 0.001, "nothing is lost overhead");
        CheckNear(SeeingFactor(60.0), std::pow(1.1547, 0.6), 0.01,
                  "60 deg costs under a tenth");
        Check(SeeingFactor(30.0) > 1.4,
              "30 deg costs more than 40%", std::to_string(SeeingFactor(30.0)));
        Check(SeeingFactor(30.0) > SeeingFactor(60.0) &&
              SeeingFactor(60.0) > SeeingFactor(90.0),
              "and it is monotonic with altitude", "");
    }

    // ── 3. The owner's 60-90-60 window ───────────────────────
    printf("\n3. The imaging window\n");
    {
        Check(WindowFor(90.0) == SkyWindow::Prime, "the zenith is prime", "");
        Check(WindowFor(60.0) == SkyWindow::Prime, "60 deg exactly is prime", "");
        Check(WindowFor(59.9) == SkyWindow::Good,  "just under 60 is not", "");
        Check(WindowFor(45.0) == SkyWindow::Good,     "45 is good", "");
        Check(WindowFor(30.0) == SkyWindow::Marginal, "30 is marginal", "");
        Check(WindowFor(20.0) == SkyWindow::Poor,     "20 is poor", "");
        Check(WindowFor(10.0) == SkyWindow::TooLow,   "10 is not worth it", "");
    }

    // ── 4. The trend, from observed altitude ─────────────────
    printf("\n4. Trend and projection\n");
    {
        // Climbing at a tenth of a degree a minute, starting at 55 and
        // sampled for five minutes -- so the CURRENT altitude at the end
        // is 55.5, and prime at 60 is 45 minutes off, not 50. The
        // projection runs from where the scope is now, which is the only
        // altitude the observer cares about.
        SkyTracker t;
        for (int i = 0; i <= 20; i++)           // 20 samples over 5 minutes
            t.Push(55.0 + 0.1 * (i * 15.0 / 60.0), 180.0, i * 15.0);
        const SkyQuality q = t.Evaluate();
        Check(q.valid, "a tracked climb evaluates", "");
        CheckNear(q.trendDegPerMin, 0.1, 0.01, "the rate comes back as 0.1 deg/min");
        CheckNear(q.minutesToPrime, 45.0, 2.0,
                  "and prime is 45 min from the CURRENT 55.5 deg");
        Check(q.window == SkyWindow::Good, "55 deg reads as GOOD", "");
        Check(WindowAdvice(q).find("prime in") != std::string::npos,
              "the advice says when", WindowAdvice(q));
    }
    {
        // Descending through the top of the window.
        SkyTracker t;
        for (int i = 0; i <= 20; i++)
            t.Push(63.0 - 0.1 * (i * 15.0 / 60.0), 180.0, i * 15.0);
        const SkyQuality q = t.Evaluate();
        Check(q.trendDegPerMin < 0.0, "a descent reads negative",
              std::to_string(q.trendDegPerMin));
        Check(q.minutesLeftInPrime > 15.0 && q.minutesLeftInPrime < 35.0,
              "and says how long is left in prime",
              std::to_string(q.minutesLeftInPrime));
    }
    {
        // A stationary mount must not be told it reaches 60 degrees in
        // nine thousand minutes. True, and useless.
        SkyTracker t;
        for (int i = 0; i <= 20; i++) t.Push(40.0, 90.0, i * 15.0);
        const SkyQuality q = t.Evaluate();
        Check(q.minutesToPrime < 0.0,
              "a parked mount is given no projection at all", "");
    }
    {
        // A slew is not a drift. Fitting across one would report
        // degrees per second and predict nonsense.
        SkyTracker t;
        for (int i = 0; i < 10; i++) t.Push(30.0, 90.0, i * 15.0);
        t.Push(75.0, 200.0, 10 * 15.0);          // slewed
        const SkyQuality q = t.Evaluate();
        CheckNear(q.altitudeDeg, 75.0, 0.01, "the new altitude is taken");
        Check(q.trendDegPerMin == 0.0,
              "and the pre-slew history is discarded, not fitted through", "");
    }
    {
        SkyTracker t;
        t.Push(70.0, 90.0, 0.0);
        t.Push(70.1, 90.0, 5.0);
        const SkyQuality q = t.Evaluate();
        Check(q.valid, "two close samples still give a position", "");
        Check(q.trendDegPerMin == 0.0,
              "but five seconds is not enough span to claim a rate", "");
    }

    // ── 5. Nothing to report ─────────────────────────────────
    printf("\n5. Before the mount has said anything\n");
    {
        SkyTracker t;
        const SkyQuality q = t.Evaluate();
        Check(!q.valid, "an empty tracker is invalid, not zero degrees", "");
        Check(WindowAdvice(q) == "no mount altitude",
              "and says so rather than advising on a position it lacks",
              WindowAdvice(q));
    }

    printf("\n");
    if (g_fail == 0) printf("=== the sky numbers are what they claim ===\n\n");
    else             printf("=== %d CHECK(S) FAILED ===\n\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
