// ============================================================
//  A focus sample belongs to its frame (phase 161.1)
//
//  The analyser gives a result about twice a second, so a result that
//  arrives just after autofocus opens a collection window was measured
//  on a frame from up to half a second earlier: before the settle had
//  ended, sometimes before the move had. Each score now carries its
//  frame's time, and a window takes only frames taken after it opened.
//
//  The real routine on the emulated focuser. The feed is an analyser
//  with a lag: every score is of where the mirror WAS `lag` ago, and is
//  stamped with that time. With the lag longer than the settle, a
//  routine that took scores by arrival would credit each position with
//  its neighbour's sharpness, and the curve would slide.
//
//  Also here: Start() reads as running at once, and the completion is
//  handed over before the state says done.
// ============================================================

#define RADUK_EMULATOR 1
#include "core/AutofocusRoutine.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <mutex>
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

static constexpr int kFocus = 167300;
static float Score(int mirror) {
    const double d = (mirror - kFocus) / 900.0;
    return (float)(6.0 + 235.0 / (1.0 + d * d));
}

struct Out { bool done = false; AFResult res; int end = 0; int dropped = 0; bool runningAtCallback = false; };

// stamped: scores carry their frame's time. Otherwise 0, "not known",
// which is how the routine was fed before.
static Out Run(int lagMs, bool stamped) {
    EAFFocuser f; f.Open(0);
    AutofocusRoutine af;
    af.SetFocuser(&f);
    AutofocusParams p;
    p.settleMs = 40;             // shorter than the lag, as on the rig
    p.fineStage = false;
    p.backlash = 0;              // a perfect focuser: no half-backlash shift in the result
    p.scanRange = 3000;          // 300-step spacing: a lagged score is a neighbour's
    af.SetParams(p);
    af.SetLogger([](const char*) {});
    Out out;
    std::atomic<bool> done{false}, feed{true};
    af.SetOnComplete([&](AFResult r) {
        out.res = r;
        out.runningAtCallback = af.IsRunning();
        done.store(true);
    });
    std::thread feeder([&] {
        struct Past { uint64_t t; int mirror; };
        std::deque<Past> history;
        while (feed.load()) {
            const uint64_t now = Timestamp::Now();
            history.push_back({now, f.GetOpticalPosition()});
            while (history.size() > 1 && history[1].t + (uint64_t)lagMs * 1000 <= now) history.pop_front();
            const Past& was = history.front();   // the frame the "analyser" has just finished
            if (was.t + (uint64_t)lagMs * 1000 <= now)
                af.SubmitSharpness(Score(was.mirror), true, 120, stamped ? was.t : 0);
            Ms(4);
        }
    });
    af.Start(f.GetPosition());
    for (int i = 0; i < 6000 && !done.load(); ++i) Ms(10);
    out.done = done.load();
    out.dropped = af.StaleDropped();
    feed.store(false);
    feeder.join();
    out.end = f.GetOpticalPosition();
    f.Close();
    return out;
}

int main() {
    std::printf("A focus sample belongs to its frame (phase 161.1)\n");

    std::printf("\n1. An analyser 250 ms behind, scores stamped with their frame's time\n");
    {
        const Out r = Run(250, true);
        Check(r.done && r.res.success, "autofocus finishes", r.res.message);
        Check(r.dropped > 0, "scores from frames taken before a window opened were left out",
              N(r.dropped) + " left out");
        Check(std::abs(r.end - kFocus) <= 60, "and it lands on focus", N(r.end - kFocus));
        Check(r.runningAtCallback, "the result is handed over while the routine still reads as running");
    }

    std::printf("\n2. The same analyser, scores taken by arrival as before\n");
    {
        const Out r = Run(250, false);
        Check(r.done, "it finishes", r.res.message);
        Check(r.dropped == 0, "nothing is left out: there is no time to judge by");
        std::printf("         ended %+d from focus (case 1: stamped). Not asserted: how far a\n"
                    "         lagged feed drags the result depends on timing; that it CAN is why\n"
                    "         the stamp exists.\n", r.end - kFocus);
    }

    std::printf("\n3. Running from the first instant\n");
    {
        EAFFocuser f; f.Open(0);
        AutofocusRoutine af;
        af.SetFocuser(&f);
        af.SetLogger([](const char*) {});
        int notRunning = 0, secondStarted = 0;
        for (int k = 0; k < 50; ++k) {
            af.Start(f.GetPosition());
            if (!af.IsRunning()) ++notRunning;            // the line after Start()
            if (af.Start(f.GetPosition())) ++secondStarted;
            af.Abort();
            for (int i = 0; i < 2000 && af.IsRunning(); ++i) Ms(1);
        }
        Check(notRunning == 0, "IsRunning() is true the moment Start() returns, 50 times", N(notRunning));
        Check(secondStarted == 0, "so a second Start() is refused, not left to join a live sweep",
              N(secondStarted));
        f.Close();
    }

    std::printf("\n%s\n", g_fail ? "FAILED" : "af_sample_window: all passed");
    return g_fail ? 1 : 0;
}
