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

static int Run(int start, bool capped, std::string& msg, double fadePerSec = 0.0) {
    EAFFocuser f;
    f.Open(0);
    f.MoveTo(start);
    while (f.IsMoving()) std::this_thread::sleep_for(std::chrono::milliseconds(5));

    AutofocusRoutine af;
    af.SetFocuser(&f);
    AutofocusParams p;
    p.settleMs = 50;
    af.SetParams(p);
    af.SetLogger([](const char*) {});
    std::atomic<bool> done{false};
    AFResult res;
    af.SetOnComplete([&](AFResult r) { res = r; done.store(true); });

    std::mt19937 rng(start * 7 + (capped ? 1 : 0));
    std::atomic<bool> feed{true};
    const auto t0 = std::chrono::steady_clock::now();
    std::thread feeder([&] {
        while (feed.load()) {
            // A target fading as Saturn did on 2026-11-08 (dew or haze):
            // the score falls with time, whatever the focus.
            const double el = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t0).count();
            const float fade = (float)std::exp(-fadePerSec * el);
            af.SubmitSharpness(Score(f.GetPosition(), rng, capped) * fade, true, 120);
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    });
    af.Start(start);
    for (int i = 0; i < 6000 && !done.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    feed.store(false);
    feeder.join();
    msg = res.message;
    return f.GetPosition();
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

    if (g_fail) { std::printf("autofocus_saturn: %d FAILED\n", g_fail); return 1; }
    std::printf("autofocus_saturn: all passed\n");
    return 0;
}
