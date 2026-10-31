// tests/motion.cpp -- phase 96.5, the motion tokens
//
// The arithmetic behind Theme::Motion: the duration ladder, the fade
// curve, and the transient-surface timeline. The curve test is the one
// that matters, because it checks the claim the curve rests on against
// this app's own colour tokens rather than against taste.

#include <cstdio>
#include <cmath>
#include "Theme.h"

static int failures = 0;
static int checks   = 0;

static void ok(bool cond, const char* what) {
    ++checks;
    if (!cond) { ++failures; printf("  FAIL: %s\n", what); }
}
static void CloseTo(double a, double b, double tol, const char* what) {
    ++checks;
    if (std::fabs(a - b) > tol) {
        ++failures;
        printf("  FAIL: %s (got %.4f, want %.4f +/- %.4f)\n", what, a, b, tol);
    }
}

namespace M = raduk::gui::Theme::Motion;

// ── sRGB and CIE L*, so the curve's premise is checked, not assumed ──
static double Linearize(double v255) {
    const double c = v255 / 255.0;
    return (c <= 0.04045) ? c / 12.92
                          : std::pow((c + 0.055) / 1.055, 2.4);
}
static double Luminance(double r, double g, double b) {
    return 0.2126 * Linearize(r) + 0.7152 * Linearize(g) + 0.0722 * Linearize(b);
}
static double Lstar(double y) {
    return (y > 0.008856) ? 116.0 * std::cbrt(y) - 16.0 : 903.3 * y;
}

// The exponent an alpha ramp needs for CIE L* to rise linearly, for a
// given foreground over a given backdrop. Found from the alpha at
// which half the final lightness is reached: that alpha is 0.5^n.
static double PerceptualExponent(double yFg, double yBg) {
    const double half  = Lstar(yFg) / 2.0;
    const double yHalf = std::pow((half + 16.0) / 116.0, 3.0);
    const double aHalf = (yHalf - yBg) / (yFg - yBg);
    return std::log(aHalf) / std::log(0.5);
}

static void TestLadder() {
    printf("Duration ladder\n");

    // Half-decade: each rung sqrt(10) from the last, anchored at 150.
    ok(M::BASE_MS == 150, "base is 150 ms");
    ok(M::FAST == M::BASE_MS, "rung 0 is the anchor");
    CloseTo((double)M::LONG / M::FAST, 10.0, 0.001, "rung 2 is a full decade up");

    // The two durations that were already in the app land on rungs 1
    // and 2. This is the whole justification for the anchor, so if a
    // later phase retunes a rung, this is the assertion that should
    // make it stop and think.
    const double trueRung1 = M::BASE_MS * std::sqrt(10.0);     // 474.3
    ok(M::CONTENT == 450, "ObjectPanel's measured 450 ms is kept as rung 1");
    ok(std::fabs(M::CONTENT - trueRung1) / trueRung1 < 0.055,
       "rung 1 is within 5.5% of the true half-decade rung");
    ok(M::LONG == 1500, "ShareView's measured 1500 ms is rung 2");

    // 24 ms is the error rung 1 carries by keeping the measured value.
    // One frame at 60 Hz is 16.7 ms, so it is under two frames.
    ok(trueRung1 - M::CONTENT < 2.0 * M::FRAME_MS,
       "rung 1's rounding error is under two frames");

    ok(M::StepAt(M::D_FAST)    == M::FAST,    "step 0");
    ok(M::StepAt(M::D_CONTENT) == M::CONTENT, "step 1");
    ok(M::StepAt(M::D_LONG)    == M::LONG,    "step 2");
    ok(M::StepAt(-4)  == M::FAST, "a negative step clamps low");
    ok(M::StepAt(900) == M::LONG, "a step past the end clamps high");

    // Ordered and distinct, the same requirement the type and space
    // ladders carry: a ladder with a repeated rung has a rung nothing
    // can ever select.
    for (int i = 1; i < M::STEPS; ++i)
        ok(M::kRamp[i] > M::kRamp[i - 1], "the ladder strictly ascends");

    // Dwells are reading time, and the notice outlasts the
    // confirmation on purpose.
    ok(M::DWELL_NOTICE > M::DWELL_CONFIRM,
       "a notice dwells longer than a confirmation");
    ok(M::DWELL_CONFIRM > M::LONG,
       "even the short dwell outlasts the longest motion");
}

static void TestProgress() {
    printf("Progress\n");
    CloseTo(M::Progress(0, 450),    0.0,  1e-6, "nothing elapsed is 0");
    CloseTo(M::Progress(225, 450),  0.5,  1e-6, "half elapsed is 0.5");
    CloseTo(M::Progress(450, 450),  1.0,  1e-6, "fully elapsed is 1");
    CloseTo(M::Progress(9000, 450), 1.0,  1e-6, "past the end clamps to 1");
    CloseTo(M::Progress(-50, 450),  0.0,  1e-6, "a negative elapsed clamps to 0");

    // A caller that disables an animation by zeroing its duration must
    // get the end state, not a division by zero.
    CloseTo(M::Progress(0, 0),   1.0, 1e-6, "a zero duration is complete at once");
    CloseTo(M::Progress(10, -5), 1.0, 1e-6, "a negative duration is complete too");
}

static void TestCurve() {
    printf("The fade curve\n");

    CloseTo(M::FadeIn(0, 450),    0.0,  1e-6, "a fade-in starts transparent");
    CloseTo(M::FadeIn(450, 450),  1.0,  1e-6, "a fade-in ends opaque");
    CloseTo(M::FadeIn(225, 450),  0.25, 1e-6, "the midpoint is a quarter alpha");
    CloseTo(M::FadeOut(0, 450),   0.0,  1e-6, "a fade-out ends transparent");
    CloseTo(M::FadeOut(450, 450), 1.0,  1e-6, "a fade-out starts opaque");

    // FadeOut is the same curve, parameterised by the time left
    // rather than the time spent -- so a surface that fades in and out
    // cannot drift into two different shapes. Stated here as the
    // identity it is; the property with real content is the symmetry
    // of the composed timeline, asserted in TestTransient below.
    for (int ms = 0; ms <= 450; ms += 45)
        CloseTo(M::FadeOut(ms, 450), M::FadeIn(ms, 450), 1e-9,
             "fading out is the entrance curve read from the other end");

    // Monotonic, which a fade must be: a moment where it dims while
    // arriving reads as a flicker.
    float prev = -1.0f;
    for (int ms = 0; ms <= 450; ++ms) {
        const float a = M::FadeIn(ms, 450);
        ok(a >= prev, "the fade never goes backwards");
        prev = a;
    }

    printf("The curve's premise, against this app's own tokens\n");

    // Night vision collapses every token to the red channel at the
    // luminance the night ladder assigns it: TEXT 200 over DEEP 14.
    const double yNightFg = Luminance(200, 0, 0);
    const double yNightBg = Luminance(14,  0, 0);
    // Day: TEXT 0xC8E8F0 over DEEP 0x070D16.
    const double yDayFg = Luminance(0xC8, 0xE8, 0xF0);
    const double yDayBg = Luminance(0x07, 0x0D, 0x16);

    // A plain power law is only adequate because the surfaces are
    // near-black. If a later phase lifts the panel background, this is
    // the assertion that fails first, and the curve would then need
    // the backdrop's luminance at the call site.
    ok(yNightBg / yNightFg < 0.02, "the night backdrop is under 2% of the text");
    ok(yDayBg   / yDayFg   < 0.02, "the day backdrop is under 2% of the text");

    const double nNight = PerceptualExponent(yNightFg, yNightBg);
    const double nDay   = PerceptualExponent(yDayFg,   yDayBg);
    printf("  measured exponents: night %.2f, day %.2f\n", nNight, nDay);

    // The exponent is NOT 3. Alpha to luminance to L* suggests a cube,
    // but the -16 offset in L* matters when night text only reaches
    // L* 42, and it pulls the exponent back to roughly 2.
    ok(nNight < 2.5 && nDay < 2.5, "neither exponent is anywhere near 3");
    CloseTo(nNight, 1.97, 0.05, "the night exponent is ~1.97");
    CloseTo(nDay,   2.42, 0.05, "the day exponent is ~2.42");
    ok(nNight < 2.0 && nDay > 2.0, "a square sits between the two cases");

    // And a square is within the just-noticeable difference of ideal
    // in the night case, which is the condition the app is built for.
    const double ySquare = yNightBg + 0.25 * (yNightFg - yNightBg);
    const double errL    = std::fabs(Lstar(ySquare) - Lstar(yNightFg) / 2.0);
    printf("  square vs ideal at the midpoint: %.2f L* (JND ~1.0)\n", errL);
    ok(errL < 1.0, "at night a square is within one JND of perceptually linear");

    // In the day case it errs bright, so a fade-in arrives a shade
    // early rather than lurking. The direction is the point.
    const double yDaySquare = yDayBg + 0.25 * (yDayFg - yDayBg);
    ok(Lstar(yDaySquare) > Lstar(yDayFg) / 2.0,
       "by day the square errs bright, not dark");
}

static void TestAlphaByte() {
    printf("AlphaByte\n");
    ok(M::AlphaByte(0.0f) == 0,   "transparent is 0");
    ok(M::AlphaByte(1.0f) == 255, "opaque is 255, not 254");
    ok(M::AlphaByte(0.5f) == 128, "a half rounds up to 128");
    ok(M::AlphaByte(-1.0f) == 0,  "below zero clamps");
    ok(M::AlphaByte(4.0f) == 255, "above one clamps");
    // Monotonic across the whole range, so a fade cannot stall at a
    // byte boundary.
    int prev = -1;
    for (int i = 0; i <= 1000; ++i) {
        const int a = M::AlphaByte(i / 1000.0f);
        ok(a >= prev, "AlphaByte never goes backwards");
        prev = a;
    }
}

static void TestTransient() {
    printf("The transient-surface timeline\n");
    const int dwell = M::DWELL_CONFIRM;
    const int edge  = M::FAST;
    bool alive = false;

    CloseTo(M::TransientAlpha(0, dwell, edge, alive), 0.0, 1e-6,
         "it starts transparent");
    ok(alive, "and alive");

    CloseTo(M::TransientAlpha(edge, dwell, edge, alive), 1.0, 1e-6,
         "opaque the moment the entrance ends");
    CloseTo(M::TransientAlpha(edge + dwell / 2.0, dwell, edge, alive), 1.0, 1e-6,
         "and stays opaque through the dwell");
    ok(alive, "still alive mid-dwell");

    // The dwell sits between the edges rather than containing them, so
    // the full life is dwell plus both fades.
    const double life = (double)dwell + 2.0 * edge;
    CloseTo(M::TransientAlpha(life - edge, dwell, edge, alive), 1.0, 1e-6,
         "opaque until the exit begins");
    ok(M::TransientAlpha(life - edge / 2.0, dwell, edge, alive) < 1.0,
       "dimming once the exit has begun");
    ok(alive, "still alive mid-exit");

    M::TransientAlpha(life, dwell, edge, alive);
    ok(!alive, "dead at the end of its life");
    M::TransientAlpha(life + 10000, dwell, edge, alive);
    ok(!alive, "and stays dead");
    CloseTo(M::TransientAlpha(life + 1, dwell, edge, alive), 0.0, 1e-6,
         "and transparent once dead");

    // Monotonic up then monotonic down, with no gap: the two edges and
    // the hold have to meet exactly or the surface jumps.
    float peak = 0.0f;
    for (double ms = 0; ms <= life; ms += 1.0) {
        const float a = M::TransientAlpha(ms, dwell, edge, alive);
        ok(a >= -1e-6f && a <= 1.0f + 1e-6f, "alpha stays in range");
        if (ms <= edge) { ok(a >= peak - 1e-6f, "the entrance only rises"); peak = a; }
    }

    // The timeline is symmetric in time: however far into its life
    // the surface is, it is at the same opacity as it will be that
    // far before the end. This is the assertion with content -- it
    // spans the entrance, the hold, and the exit, and it is what
    // would break if the three phases stopped meeting exactly.
    for (double e = 0; e <= life; e += 7.0)
        CloseTo(M::TransientAlpha(e, dwell, edge, alive),
                M::TransientAlpha(life - e, dwell, edge, alive), 1e-9,
                "the timeline is symmetric end to end");

    // Raising the dwell must not shorten the fades. This is the
    // property that made the dwell sit outside the edges, and it is
    // what the two-timer arrangement it replaced could get wrong.
    bool a2 = false;
    CloseTo(M::TransientAlpha(edge / 2.0, dwell,      edge, alive),
         M::TransientAlpha(edge / 2.0, dwell * 4,  edge, a2), 1e-6,
         "the entrance is independent of the dwell");

    // A zero dwell still fades: the surface arrives and leaves without
    // a hold, which is degenerate but must not divide by zero or
    // invert the timeline.
    const float mid = M::TransientAlpha(edge, 0, edge, alive);
    CloseTo(mid, 1.0, 1e-6, "a zero-dwell surface still reaches full");
    ok(alive, "and is alive at its peak");
    M::TransientAlpha(2.0 * edge, 0, edge, alive);
    ok(!alive, "a zero-dwell surface dies after both edges");
}

int main() {
    printf("=== phase 96.5: motion ===\n\n");
    TestLadder();
    TestProgress();
    TestCurve();
    TestAlphaByte();
    TestTransient();
    printf("\n%d checks, %d failures\n", checks, failures);
    if (failures == 0) printf("PASS\n");
    return failures ? 1 : 0;
}
