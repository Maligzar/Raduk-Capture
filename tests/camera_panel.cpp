// ============================================================
//  The camera panel's arithmetic (phase 98)
//
//  From the owner, using the application: "there is a problem with
//  the camera sliders they are difficult to interact with. also the
//  Auto Exposure feature seems odd."
//
//  Both turned out to be measurable rather than matters of feel, and
//  this file pins the measurements.
//
//  THE SLIDER. It was linear over 1 to 60,000 microseconds on 425
//  pixels of travel -- 141 us per pixel -- which put the Moon's 1-5 ms
//  into 28 pixels, 6.7% of the control, while Saturn's 20-60 ms took
//  67%. 3.00 ms could not be set at all: the reachable neighbours were
//  2.966 and 3.107 ms. Exposure is judged in stops, so 2-to-4 ms
//  matters as much as 20-to-40, and a linear scale gives the first a
//  tenth of the room.
//
//  AUTO EXPOSURE. The correction is multiplicative and was discarded
//  unless it moved the exposure by at least a flat 0.1 ms. At the
//  smallest error that escapes the deadband, that made 3.4 ms the
//  shortest exposure it could move at all -- and below that the gain
//  branch is gated on being AT a limit, which it was not, so nothing
//  happened and the panel said "AE: limit". It did not fail. It
//  reported a limit it was nowhere near, which is the "odd".
// ============================================================

#include "core/ExposureScale.h"

#include <cstdio>
#include <cmath>
#include <algorithm>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (ok) printf("  ok    %s\n", what);
    else  { printf("  FAIL  %s\n", what); ++g_fail; }
}
static bool Near(double a, double b, double eps) {
    return std::fabs(a - b) <= eps;
}

int main() {
    printf("Camera panel: the slider scale and the AE dead zone\n");

    // ───────────────────────────────────────────────────────────
    printf("\n1. The scale reaches both of its own ends\n");
    {
        // An exposure scale whose top is 59,994 us because of a
        // rounding error cannot reach its documented ceiling, and
        // nothing in the middle of the range would ever show it.
        Check(ExposureUsAt(0) == EXP_MIN_US,
              "position 0 is exactly the minimum");
        Check(ExposureUsAt(EXP_SLIDER_STEPS) == EXP_MAX_US,
              "and the last position is exactly the maximum");
        Check(ExposureUsAt(-5) == EXP_MIN_US &&
              ExposureUsAt(99999) == EXP_MAX_US,
              "out-of-range positions clamp rather than extrapolating");
        Check(ExposurePosFor(EXP_MIN_US) == 0 &&
              ExposurePosFor(EXP_MAX_US) == EXP_SLIDER_STEPS,
              "and the inverse agrees at both ends");
        Check(ExposurePosFor(1.0) == 0 &&
              ExposurePosFor(1.0e9) == EXP_SLIDER_STEPS,
              "a value outside the range maps to the nearer end");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n2. Monotonic, and a round trip that holds\n");
    {
        bool rising = true;
        for (int i = 1; i <= EXP_SLIDER_STEPS; ++i)
            if (ExposureUsAt(i) <= ExposureUsAt(i - 1)) rising = false;
        Check(rising, "every step is longer than the one below it");

        // Position -> us -> position must be the identity, or the
        // thumb jumps when auto-exposure writes back a value it just
        // read off the same slider.
        bool round = true;
        for (int i = 0; i <= EXP_SLIDER_STEPS; ++i)
            if (ExposurePosFor(ExposureUsAt(i)) != i) round = false;
        Check(round, "position -> microseconds -> position is exact, so a"
                     " writeback cannot nudge the thumb");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n3. THE FIX: the travel is shared out properly now\n");
    {
        struct B { const char* what; double lo, hi; };
        const B bands[] = {
            { "Moon     1-5 ms",   1000.0,  5000.0 },
            { "Jupiter  5-20 ms",  5000.0, 20000.0 },
            { "Saturn  20-60 ms", 20000.0, 60000.0 },
        };
        // What the old LINEAR scale gave, for comparison. 425 px of
        // travel over 1..60000 us.
        const double linPx = 425.0, linSpan = 59999.0;
        bool allFair = true;
        for (const B& b : bands) {
            const int steps = ExposurePosFor(b.hi) - ExposurePosFor(b.lo);
            const double logPct = 100.0 * steps / EXP_SLIDER_STEPS;
            const double linPct = 100.0 * ((b.hi - b.lo) / linSpan);
            printf("  --    %-16s linear %4.1f%%  ->  log %4.1f%%\n",
                   b.what, linPct, logPct);
            if (logPct < 10.0) allFair = false;
        }
        Check(allFair,
              "no band is squeezed under a tenth of the control, where"
              " the Moon used to sit at 6.7%");

        // The specific value that could not be set.
        const int p3 = ExposurePosFor(3000.0);
        printf("  --    3.00 ms -> step %d -> %.3f ms  (linear gave"
               " 2.966 / 3.107, nothing between)\n",
               p3, ExposureUsAt(p3) / 1000.0);
        Check(Near(ExposureUsAt(p3) / 1000.0, 3.0, 0.02),
              "3.00 ms is now reachable to within a fiftieth of a ms");

        // Uniform relative precision is the property being bought.
        double worst = 0.0;
        for (int i = 1; i <= EXP_SLIDER_STEPS; ++i) {
            const double rel = ExposureUsAt(i) / ExposureUsAt(i - 1) - 1.0;
            worst = (std::max)(worst, std::fabs(rel - 0.0253));
        }
        printf("  --    every step is 2.53%%, within %.4f of each other\n",
               worst);
        Check(worst < 0.001,
              "the step is the same FRACTION everywhere, which is the"
              " whole point: the error is proportional to the value");
        (void)linPx;
    }

    // ───────────────────────────────────────────────────────────
    printf("\n4. The keyboard is usable at all\n");
    {
        // The slider never set a line or page size, so the defaults
        // applied: on the old linear scale an arrow key moved ONE
        // MICROSECOND -- sixty thousand presses to cross -- and a page
        // key jumped twelve milliseconds, with nothing in between.
        Check(EXP_LINE_STEPS >= 1, "an arrow key moves at least a step");
        const double oneArrow =
            ExposureUsAt(100 + EXP_LINE_STEPS) / ExposureUsAt(100) - 1.0;
        const double onePage =
            ExposureUsAt(100 + EXP_PAGE_STEPS) / ExposureUsAt(100) - 1.0;
        printf("  --    arrow %+.1f%%, page %+.1f%% (was 1 us and 12 ms,"
               " with nothing between)\n", oneArrow * 100, onePage * 100);
        Check(oneArrow > 0.01 && oneArrow < 0.10,
              "an arrow is a few per cent: a nudge, not a nothing");
        Check(onePage > oneArrow * 4.0 && onePage < 1.0,
              "and a page is a real jump without crossing the range");
        Check(EXP_LIVE_APPLY_MS >= 50.0 && EXP_LIVE_APPLY_MS <= 500.0,
              "a drag applies often enough to watch and seldom enough"
              " not to flood the USB bus");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n5. THE AE DEAD ZONE, before and after\n");
    {
        // Reproduced from the assistant's own constants rather than
        // asserted: the deadband is (215-185)/2, and a correction is
        // kept only if it moves the exposure by at least the minimum
        // step.
        const double deadband = 15.0;
        const double iGainOld = 0.10 * 5.0;   // the bare 5 in the old code
        const double iGainNew = 0.50;         // folded in, same arithmetic
        const double oldStep  = 0.1;          // flat ms
        const double newFrac  = 0.01, newFloor = 0.001;

        printf("  --    exposure   smallest error that moves it\n");
        printf("  --              before     after\n");
        int stalledBefore = 0, stalledAfter = 0;
        for (double exp : { 0.5, 1.0, 2.0, 3.0, 3.5, 5.0, 10.0, 50.0 }) {
            const double eOld = oldStep / exp * 255.0 / iGainOld;
            const double nStep = (std::max)(newFloor, exp * newFrac);
            const double eNew = nStep / exp * 255.0 / iGainNew;
            printf("  --    %5.1f ms   %6.1f DN   %5.1f DN%s\n",
                   exp, eOld, eNew, eOld > deadband ? "   <- stalled" : "");
            if (eOld > deadband) ++stalledBefore;
            if (eNew > deadband) ++stalledAfter;
        }
        Check(stalledBefore >= 4,
              "the old flat 0.1 ms step stalled auto-exposure at four of"
              " these eight exposures");
        Check(stalledAfter == 0,
              "a step relative to the exposure stalls at none of them");

        // The threshold the fix delivers is the SAME at every exposure,
        // which is the property worth having: a controller whose
        // sensitivity depends on where it happens to be is not one.
        double lo = 1e9, hi = -1e9;
        for (double exp : { 0.5, 1.0, 2.0, 5.0, 10.0, 50.0 }) {
            const double nStep = (std::max)(newFloor, exp * newFrac);
            const double e = nStep / exp * 255.0 / iGainNew;
            lo = (std::min)(lo, e); hi = (std::max)(hi, e);
        }
        printf("  --    after the fix the threshold is %.1f DN at every"
               " exposure\n", lo);
        Check(Near(lo, hi, 0.01),
              "and it is identical across the range, not a function of"
              " where the controller happens to be sitting");
        Check(hi < deadband,
              "comfortably inside the deadband, so anything AE decides"
              " to act on it can actually act on");

        // The old jitter rejection must not be lost at the long end,
        // where a flat 0.1 ms was doing a real job.
        const double at50 = (std::max)(newFloor, 50.0 * newFrac);
        printf("  --    and at 50 ms the step is %.2f ms, where the flat"
               " figure was 0.10\n", at50);
        Check(at50 > oldStep,
              "at 50 ms the relative step is LARGER than the old flat"
              " one, so jitter rejection improved rather than traded");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n6. The planetary range the panel is actually for\n");
    {
        // Every exposure the help file recommends has to be settable.
        struct T { const char* what; double ms; };
        const T targets[] = {
            { "Moon",    2.0 }, { "Jupiter", 12.0 }, { "Saturn",  30.0 },
            { "Mars",     8.0 }, { "Venus",   1.0 }, { "deep sky", 60.0 },
        };
        bool reachable = true;
        for (const T& t : targets) {
            const int pos = ExposurePosFor(t.ms * 1000.0);
            const double got = ExposureUsAt(pos) / 1000.0;
            const double errPct = 100.0 * std::fabs(got - t.ms) / t.ms;
            printf("  --    %-9s %5.1f ms -> %6.3f ms  (%.2f%% out)\n",
                   t.what, t.ms, got, errPct);
            if (errPct > 1.3) reachable = false;
        }
        Check(reachable,
              "every recommended exposure lands within 1.3%, half a"
              " step, which is the best a 256-step control can do");
        Check(ExposurePosFor(60000.0) == EXP_SLIDER_STEPS,
              "and the 60 ms ceiling is the last step, not something"
              " short of it");
    }

    printf("\n%s\n", g_fail ? "FAILURES" : "all good");
    return g_fail ? 1 : 0;
}
