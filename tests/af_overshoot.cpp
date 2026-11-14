// ============================================================
//  Overshoot is not backlash (phase 161.2)
//
//  One number did two jobs: how far a move goes past before it comes
//  back the final way, and the half-backlash shift of a coarse result.
//  The first has only to exceed the knob's slack; the second has to be
//  right. With the unmeasured default of 200 against a real slack that
//  may be several times that, the overshoot was too short to take the
//  slack up, so a step number stopped meaning one focus position.
//
//  The emulated focuser has a slack the app does NOT know (it believes
//  200), as on a rig before clear-night item 59 has measured it.
//    1. The default overshoot, and rig.ini's.
//    2. A one-sided move: the same step from above and from below is one
//       mirror position when the overshoot exceeds the slack, and two
//       when it does not.
//    3. Autofocus with the fine pass: it lands on focus whatever the
//       slack is, as long as the overshoot exceeds it.
// ============================================================

#define RADUK_EMULATOR 1
#include "core/AutofocusRoutine.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <thread>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const char* what, const std::string& detail = "") {
    std::printf("  [%s] %-66s %s\n", ok ? "PASS" : "FAIL", what, detail.c_str());
    if (!ok) ++g_fail;
}
static std::string N(long long v) { return std::to_string(v); }
static void Ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
static void Settle(EAFFocuser& f) { for (int t = 0; t < 30000 && f.IsMoving(); t += 5) Ms(5); }

static constexpr int kHome  = 166500;
static constexpr int kFocus = 167300;   // where the MIRROR is in focus

// The same step number, reached from 3,000 below and from 3,000 above:
// how far apart the mirror ends.
static int TwoSides(int slack, int overshoot) {
    EAFFocuser f; f.Open(0);
    f.SetSimulatedSlack(slack);
    f.SetApproachBacklash(overshoot);
    f.MoveTo(kHome - 3000); Settle(f);
    f.MoveTo(kHome);        Settle(f);
    const int fromBelow = f.GetOpticalPosition();
    f.MoveTo(kHome + 3000); Settle(f);
    f.MoveTo(kHome);        Settle(f);
    const int fromAbove = f.GetOpticalPosition();
    f.Close();
    return fromAbove - fromBelow;
}

// Autofocus on a focuser whose slack is `slack`, told the backlash is
// 200. Returns the mirror's distance from focus at the end.
static int Focus(int slack, int overshoot, std::string* msg) {
    EAFFocuser f; f.Open(0);
    f.SetSimulatedSlack(slack);
    AutofocusRoutine af;
    af.SetFocuser(&f);
    AutofocusParams p;
    p.settleMs = 30;
    p.backlash = 200;            // what the app believes
    p.overshoot = overshoot;
    p.fineStage = true;
    af.SetParams(p);
    af.SetLogger([](const char*) {});
    std::atomic<bool> done{false}, feed{true};
    AFResult res;
    af.SetOnComplete([&](AFResult r) { res = r; done.store(true); });
    std::mt19937 rng(slack + overshoot);
    std::thread feeder([&] {
        std::normal_distribution<double> seeing(1.0, 0.02);
        while (feed.load()) {
            const double d = (f.GetOpticalPosition() - kFocus) / 1800.0;
            af.SubmitSharpness((float)((6.0 + 235.0 / (1.0 + d * d)) * seeing(rng)), true, 120);
            for (int k = 0; k < 3; ++k) {
                if (af.WantsFastScores()) {
                    const double e = (f.GetOpticalPosition() - kFocus) / 350.0;
                    af.SubmitFastScore((float)((0.5 + 60.0 / (1.0 + e * e)) * seeing(rng)));
                }
                Ms(5);
            }
        }
    });
    af.Start(f.GetPosition());
    for (int i = 0; i < 12000 && !done.load(); ++i) Ms(10);
    feed.store(false);
    feeder.join();
    if (msg) *msg = res.message;
    const int off = f.GetOpticalPosition() - kFocus;
    f.Close();
    return done.load() && res.success ? off : 99999;
}

int main() {
    std::printf("Overshoot is not backlash (phase 161.2)\n");
    CoreLogHook() = [](const char*) {};

    std::printf("\n1. Which overshoot\n");
    {
        const char* src = "";
        const int d = ResolveOvershoot(-1, 200, &src);
        Check(d == 1500, "not set, backlash 200: 1500, the floor", src);
        Check(ResolveOvershoot(-1, 800, &src) == 2400, "not set, backlash 800: three times it");
        Check(ResolveOvershoot(-1, 0, &src) == 1500, "not set, no backlash figure: still 1500");
        const int r = ResolveOvershoot(2500, 200, &src);
        Check(r == 2500 && std::string(src).find("rig.ini") != std::string::npos,
              "rig.ini's wins, and says so", src);
        Check(ResolveOvershoot(0, 200, &src) == 0, "rig.ini may turn it off");
    }

    std::printf("\n2. One step number, reached from both sides (slack 1000, not known to the app)\n");
    {
        const int shortOver = TwoSides(1000, 200);
        const int longOver  = TwoSides(1000, 1500);
        Check(std::abs(shortOver) > 300, "past by 200, the old way: two mirror positions",
              N(shortOver) + " steps apart");
        Check(longOver == 0, "past by 1500: one mirror position", N(longOver) + " steps apart");
    }

    std::printf("\n3. Autofocus, told the backlash is 200\n");
    {
        struct C { int slack, over; const char* what; };
        const C cases[] = {
            {  400, 1500, "slack 400 (2x), the default overshoot" },
            { 1000, 1500, "slack 1000 (5x), the default overshoot" },
            { 2000, 2500, "slack 2000 (10x), FocuserOvershootSteps=2500" },
        };
        for (const C& c : cases) {
            std::string m;
            const int off = Focus(c.slack, c.over, &m);
            Check(std::abs(off) <= 60, c.what, N(off) + " steps from focus " + m);
        }
        std::string m;
        const int old = Focus(1000, 0, &m);
        std::printf("         for comparison, slack 1000 with the overshoot left at the backlash\n"
                    "         figure (200), as before 161.2: %d steps from focus %s\n", old, m.c_str());
    }

    std::printf("\n%s\n", g_fail ? "FAILED" : "af_overshoot: all passed");
    return g_fail ? 1 : 0;
}
