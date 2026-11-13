// ============================================================
//  Autofocus on Saturn (2026-11-07)
//
//  At the rig that night the sharpness score ran ~2400 against a
//  ceiling of 1000, so the percentage autofocus was fed sat at 100%
//  for every position near focus. The V-curve had a flat top, and five
//  of eight runs ended "not bracketed" and drove the focuser to the end
//  of the sweep, away from focus.
//
//  A synthetic curve shaped like that night's: true focus 169000, the
//  uncapped score ~240% at focus, falling over a few thousand steps,
//  with seeing noise on every frame. Fed the CAPPED score (as before)
//  the routine is shown to miss; fed the uncapped score, from near
//  focus and from 4000 and 8000 steps off, it must land within 400.
// ============================================================

#define RADUK_EMULATOR 1
#include "core/AutofocusRoutine.h"

#include <atomic>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <random>
#include <thread>

using namespace raduk::core;

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("  FAIL %s:%d  ", __FILE__, __LINE__); \
    std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static constexpr int kFocus = 169000;

static float Score(int pos, std::mt19937& rng, bool capped) {
    const double d = (pos - kFocus) / 1800.0;
    double s = 6.0 + 235.0 / (1.0 + d * d);
    std::normal_distribution<double> seeing(1.0, 0.06);
    s *= seeing(rng);
    if (capped && s > 100.0) s = 100.0;
    return (float)s;
}

// Phase 145: the edge metric the fine pass reads. Narrower than the HFD
// score near focus (what tests/focus_metric_compare.cpp measured), with
// more single-frame scatter, which the fine pass's 75th percentile of 20
// frames absorbs.
static float EdgeScore(int pos, std::mt19937& rng) {
    const double d = (pos - kFocus) / 350.0;
    double s = 0.5 + 60.0 / (1.0 + d * d);
    std::normal_distribution<double> seeing(1.0, 0.15);
    s *= seeing(rng);
    return (float)(s < 0 ? 0 : s);
}

// A Jupiter-sized disc: HFD barely moves near focus, so the coarse score
// has a flat top a few thousand steps wide (quartic, not quadratic).
static float FlatScore(int pos, std::mt19937& rng) {
    const double d = (pos - kFocus) / 2600.0;
    double s = 6.0 + 235.0 / (1.0 + d * d * d * d);
    std::normal_distribution<double> seeing(1.0, 0.06);
    return (float)(s * seeing(rng));
}

struct RunOpts {
    bool fine = true;        // the fine pass (phase 145)
    bool feedEdge = true;    // false: no edge scores arrive (the fallback)
    bool flat = false;       // the Jupiter-like flat-topped coarse score
    bool fineOnly = false;   // the Moon: no coarse sweep
    int  dir = 1;            // FocuserFinalDirection
    double edgeFadePerSec = 0.0;   // a fade the edge scores alone see (the fine pass)
};

static std::atomic<int> g_fadeCorrected{0};

// slack > 0: the knob's slack (phase 139). The score follows the MIRROR,
// and the result is where the mirror ends up, which is what the image
// shows; autofocus is given the same figure as its backlash.
static int Run(int start, bool capped, std::string& msg, double fadePerSec = 0.0,
               int slack = 0, int* measured = nullptr, RunOpts o = {}) {
    EAFFocuser f;
    f.Open(0);
    f.SetSimulatedSlack(slack);
    f.MoveTo(start);
    while (f.IsMoving()) std::this_thread::sleep_for(std::chrono::milliseconds(5));

    AutofocusRoutine af;
    af.SetFocuser(&f);
    AutofocusParams p;
    p.settleMs = 50;
    if (slack > 0) p.backlash = slack;
    p.fineStage = o.fine;
    p.finalDir = o.dir;
    af.SetParams(p);
    af.SetLogger([](const char* s) {
        if (std::strstr(s, "scores corrected")) g_fadeCorrected.fetch_add(1);
        if (getenv("AFLOG")) std::printf("%s", s);
    });
    std::atomic<bool> done{false};
    AFResult res;
    af.SetOnComplete([&](AFResult r) { res = r; done.store(true); });

    std::mt19937 rng(start * 7 + (capped ? 1 : 0));
    std::mt19937 rngE(start * 13 + 5);
    std::atomic<bool> feed{true};
    const auto t0 = std::chrono::steady_clock::now();
    std::thread feeder([&] {
        while (feed.load()) {
            // A target fading as Saturn did on 2026-11-08 (dew or haze):
            // the score falls with time, whatever the focus.
            const double el = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t0).count();
            const float fade = (float)std::exp(-fadePerSec * el);
            const int opt = f.GetOpticalPosition();
            af.SubmitSharpness((o.flat ? FlatScore(opt, rng) : Score(opt, rng, capped)) * fade, true, 120);
            for (int k = 0; k < 3; ++k) {   // the edge metric arrives faster
                if (o.feedEdge && af.WantsFastScores())
                    af.SubmitFastScore(EdgeScore(f.GetOpticalPosition(), rngE) * fade *
                                       (float)std::exp(-o.edgeFadePerSec * el));
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    });
    af.Start(start, o.fineOnly);
    for (int i = 0; i < 6000 && !done.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    feed.store(false);
    feeder.join();
    msg = res.message;
    if (measured) *measured = af.MeasuredBacklash();
    return f.GetOpticalPosition();
}

int main() {
    struct Case { const char* name; int start; };
    const Case cases[] = { { "near focus", kFocus + 700 },
                           { "4000 steps off", kFocus - 4000 },
                           { "8000 steps off", kFocus + 8000 } };

    std::printf("1. the capped score, as fed until now\n");
    {
        std::string m;
        const int end = Run(kFocus + 700, true, m);
        std::printf("   from %d ended at %d (%+d from focus) %s\n",
                    kFocus + 700, end, end - kFocus, m.c_str());
        CHECK(std::abs(end - kFocus) > 400 || !m.empty(),
              "the capped score was expected to struggle (ended %+d)", end - kFocus);
    }

    std::printf("2. the uncapped score\n");
    for (const auto& c : cases) {
        std::string m;
        const int end = Run(c.start, false, m);
        std::printf("   %-15s from %d ended at %d (%+d from focus) %s\n",
                    c.name, c.start, end, end - kFocus, m.c_str());
        CHECK(std::abs(end - kFocus) <= 400, "%s: ended %+d steps from focus",
              c.name, end - kFocus);
    }

    std::printf("3. the target fades through the sweep\n");
    {
        std::string m;
        const int end = Run(kFocus - 1500, false, m, 0.15);
        std::printf("   from %d ended at %d (%+d from focus) %s\n",
                    kFocus - 1500, end, end - kFocus, m.c_str());
        CHECK(std::abs(end - kFocus) <= 500, "fading: ended %+d steps from focus",
              end - kFocus);
    }

    std::printf("4. the knob's slack (phase 139): 400 steps\n");
    for (const auto& c : cases) {
        std::string m;
        int meas = -1;
        const int end = Run(c.start, false, m, 0.0, 400, &meas);
        std::printf("   %-15s from %d: mirror ended at %d (%+d from focus), backlash measured %d %s\n",
                    c.name, c.start, end, end - kFocus, meas, m.c_str());
        CHECK(std::abs(end - kFocus) <= 400, "%s with slack: mirror ended %+d from focus",
              c.name, end - kFocus);
        // A diagnostic, not the position: one sweep at 600-step spacing
        // read this 400-step slack anywhere from 230 to 740 over repeated
        // runs (the near-focus start reads high), which is why the log
        // asks for several sweeps that agree before FocuserBacklashSteps
        // is set. This catches a missing or wrong-signed reading.
        CHECK(meas > 100 && meas < 900, "%s: the sweep measured a slack (%d)", c.name, meas);
    }

    // ── Phase 145: the fine pass ──────────────────────────────
    std::printf("5. a Jupiter-sized disc: the coarse curve has a flat top\n");
    {
        RunOpts coarse; coarse.flat = true; coarse.fine = false;
        RunOpts both;   both.flat = true;
        std::string m1, m2;
        int worstC = 0, worstF = 0;
        for (int st : { kFocus + 900, kFocus - 2500 }) {
            const int c = Run(st, false, m1, 0.0, 0, nullptr, coarse);
            const int b = Run(st, false, m2, 0.0, 0, nullptr, both);
            std::printf("   from %d: coarse alone %+d, with the fine pass %+d %s\n",
                        st, c - kFocus, b - kFocus, m2.c_str());
            worstC = (std::max)(worstC, std::abs(c - kFocus));
            worstF = (std::max)(worstF, std::abs(b - kFocus));
        }
        CHECK(worstF <= 150, "with the fine pass, worst %d steps from focus", worstF);
        CHECK(worstF < worstC || worstC <= 150, "the fine pass did not improve on the coarse (%d against %d)",
              worstF, worstC);
    }

    std::printf("6. no edge scores arrive: the coarse result stands\n");
    {
        RunOpts o; o.feedEdge = false;
        std::string m;
        const int end = Run(kFocus + 700, false, m, 0.0, 0, nullptr, o);
        std::printf("   ended %+d from focus %s\n", end - kFocus, m.c_str());
        CHECK(std::abs(end - kFocus) <= 400, "fallback: ended %+d steps from focus", end - kFocus);
    }

    std::printf("7. the Moon: the fine pass alone\n");
    {
        RunOpts o; o.fineOnly = true;
        std::string m;
        const int end = Run(kFocus + 800, false, m, 0.0, 0, nullptr, o);
        std::printf("   from %d ended %+d from focus %s\n", kFocus + 800, end - kFocus, m.c_str());
        CHECK(std::abs(end - kFocus) <= 150, "fine only: ended %+d steps from focus", end - kFocus);
    }

    std::printf("8. finishing inward (FocuserFinalDirection=in), 400 steps of slack\n");
    for (const auto& c : cases) {
        RunOpts o; o.dir = -1;
        std::string m;
        const int end = Run(c.start, false, m, 0.0, 400, nullptr, o);
        std::printf("   %-15s from %d: mirror ended %+d from focus %s\n", c.name, c.start, end - kFocus, m.c_str());
        CHECK(std::abs(end - kFocus) <= 200, "%s inward: mirror ended %+d from focus", c.name, end - kFocus);
    }

    // ── Phase 145.5 ──────────────────────────────────────────
    std::printf("9. the target fades through both stages (2%% a second)\n");
    {
        RunOpts o; o.flat = true;
        std::string m;
        int worst = 0;
        for (int st : { kFocus + 900, kFocus - 2500 }) {
            const int end = Run(st, false, m, 0.02, 0, nullptr, o);
            std::printf("   from %d ended %+d from focus %s\n", st, end - kFocus, m.c_str());
            worst = (std::max)(worst, std::abs(end - kFocus));
        }
        CHECK(worst <= 150, "fading through the fine pass: worst %d steps from focus", worst);
    }
    std::printf("9b. a fade the fine pass itself sees: 6%% a second on the edge scores\n");
    {
        RunOpts o; o.flat = true; o.edgeFadePerSec = 0.06;
        std::string m;
        int worst = 0;
        g_fadeCorrected.store(0);
        for (int st : { kFocus + 900, kFocus - 2500 }) {
            const int end = Run(st, false, m, 0.0, 0, nullptr, o);
            std::printf("   from %d ended %+d from focus %s\n", st, end - kFocus, m.c_str());
            worst = (std::max)(worst, std::abs(end - kFocus));
        }
        std::printf("   fine passes corrected for a fade: %d\n", g_fadeCorrected.load());
        CHECK(g_fadeCorrected.load() >= 1, "the fade was never seen and corrected");
        CHECK(worst <= 150, "a fade inside the fine pass: worst %d steps from focus", worst);
    }

    std::printf("10. the Moon from 3000 steps off, beyond the fine span\n");
    {
        RunOpts o; o.fineOnly = true;
        std::string m;
        const int st = kFocus + 3000;
        const int end = Run(st, false, m, 0.0, 0, nullptr, o);
        std::printf("   from %d ended %+d from focus %s\n", st, end - kFocus, m.c_str());
        CHECK(std::abs(end - kFocus) <= std::abs(st - kFocus),
              "it never ends further from focus than it began (%+d)", end - kFocus);
        CHECK(std::abs(end - kFocus) <= 150 || !m.empty(),
              "either it focused, or it said why not (%+d, '%s')", end - kFocus, m.c_str());
    }

    if (g_fail) { std::printf("autofocus_saturn: %d FAILED\n", g_fail); return 1; }
    std::printf("autofocus_saturn: all passed\n");
    return 0;
}
