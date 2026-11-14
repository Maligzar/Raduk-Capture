// ============================================================
//  Auto-exposure between autofocus's stages (phase 161)
//
//  The owner, 2026-11-14: AE is part of autofocus. It already settled
//  before the sweep and was held through it. But it settled on whatever
//  focus the rig was left at, and a defocused disc is dim: once the
//  coarse sweep gathers the light back up, that exposure can clip the
//  limb the fine pass is about to read.
//
//  So the routine parks at the coarse focus and gives AE a turn before
//  the fine pass. The real routine on the emulated focuser, with a
//  stand-in for the loop that ticks AE:
//    - the window opens once, after the coarse sweep, near focus, with
//      the focuser at rest;
//    - it is closed during both stages (AE held);
//    - the routine goes on as soon as AE answers, and by itself if AE
//      never does;
//    - an abort inside the window ends the run as an abort;
//    - with the window off (0), nothing changes.
// ============================================================

#define RADUK_EMULATOR 1
#include "core/AutofocusRoutine.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <thread>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const char* what, const std::string& detail = "") {
    std::printf("  [%s] %-64s %s\n", ok ? "PASS" : "FAIL", what, detail.c_str());
    if (!ok) ++g_fail;
}
static std::string N(long long v) { return std::to_string(v); }
static void Ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

static constexpr int kFocus = 167300;

struct Run {
    bool     done = false;
    AFResult res;
    int      windows = 0;          // times the window opened
    int      offFocusAtOpen = 0;   // mirror minus focus when it opened
    bool     movingInWindow = false;
    bool     fastInWindow = false; // the fine pass asked for scores while it was open
    double   windowSec = 0.0;
    int      end = 0;
};
enum class Ae { Answers, Silent, AbortInside };

static Run Go(int windowMs, Ae ae, int answerAfterMs = 300) {
    EAFFocuser f; f.Open(0);
    AutofocusRoutine af;
    af.SetFocuser(&f);
    AutofocusParams p;
    p.settleMs = 30;
    p.fineStage = true;
    p.exposureWindowMs = windowMs;
    af.SetParams(p);
    af.SetLogger([](const char*) {});
    Run out;
    std::atomic<bool> done{false}, feed{true};
    af.SetOnComplete([&](AFResult r) { out.res = r; done.store(true); });
    std::mt19937 rng(7);
    std::thread feeder([&] {
        std::normal_distribution<double> seeing(1.0, 0.03);
        while (feed.load()) {
            const int opt = f.GetOpticalPosition();
            const double d = (opt - kFocus) / 1800.0;
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
    // The loop that ticks AE, as main.cpp's does through the controller.
    std::thread aeLoop([&] {
        bool was = false;
        std::chrono::steady_clock::time_point opened;
        while (feed.load()) {
            const bool is = af.WantsExposure();
            if (is && !was) {
                ++out.windows;
                opened = std::chrono::steady_clock::now();
                out.offFocusAtOpen = f.GetOpticalPosition() - kFocus;
            }
            if (is) {
                // Only a move seen with the window open on both sides of
                // it counts: the routine moves on the instant it closes.
                const int a = f.GetPosition(); Ms(10);
                const int b = f.GetPosition();
                if (af.WantsExposure()) {
                    if (b != a) out.movingInWindow = true;
                    if (af.WantsFastScores()) out.fastInWindow = true;
                }
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - opened).count();
                if (ae == Ae::Answers && ms >= answerAfterMs) af.ExposureSettled();
                if (ae == Ae::AbortInside && ms >= 150) af.Abort();
            }
            if (!is && was)
                out.windowSec = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - opened).count();
            was = is;
            Ms(5);
        }
    });
    af.Start(f.GetPosition());
    for (int i = 0; i < 6000 && !done.load(); ++i) Ms(10);
    out.done = done.load();
    feed.store(false);
    feeder.join();
    aeLoop.join();
    out.end = f.GetOpticalPosition();
    f.Close();
    return out;
}

int main() {
    std::printf("Auto-exposure between autofocus's stages (phase 161)\n");

    std::printf("\n1. AE answers\n");
    {
        const Run r = Go(4000, Ae::Answers);
        Check(r.done && r.res.success, "autofocus finishes", r.res.message);
        Check(r.windows == 1, "AE is given one turn", N(r.windows));
        Check(std::abs(r.offFocusAtOpen) < 600, "at the coarse focus, not where the sweep ended",
              N(r.offFocusAtOpen) + " steps from focus");
        Check(!r.movingInWindow, "with the focuser at rest");
        Check(!r.fastInWindow, "and the fine pass not yet reading");
        Check(r.windowSec > 0.25 && r.windowSec < 1.5, "the routine goes on as soon as AE is done",
              N((long long)(r.windowSec * 1000)) + " ms");
        Check(std::abs(r.end - kFocus) < 150, "and lands on focus", N(r.end - kFocus));
    }

    std::printf("\n2. AE never answers\n");
    {
        const Run r = Go(1200, Ae::Silent);
        Check(r.done && r.res.success, "autofocus still finishes", r.res.message);
        Check(r.windows == 1 && r.windowSec > 1.1 && r.windowSec < 2.5,
              "after the window's limit, not before and not for ever",
              N((long long)(r.windowSec * 1000)) + " ms");
        Check(std::abs(r.end - kFocus) < 150, "on focus", N(r.end - kFocus));
    }

    std::printf("\n3. An abort while AE has its turn\n");
    {
        const Run r = Go(8000, Ae::AbortInside);
        Check(r.done && !r.res.success && r.res.message == "Aborted by user",
              "ends as an abort, promptly", r.res.message);
        Check(r.windowSec < 1.0, "without sitting out the window",
              N((long long)(r.windowSec * 1000)) + " ms");
    }

    std::printf("\n4. The window off\n");
    {
        const Run r = Go(0, Ae::Answers);
        Check(r.done && r.res.success && r.windows == 0, "AE is held from the first move to the last",
              N(r.windows));
        Check(std::abs(r.end - kFocus) < 150, "on focus", N(r.end - kFocus));
    }

    std::printf("\n%s\n", g_fail ? "FAILED" : "af_exposure_window: all passed");
    return g_fail ? 1 : 0;
}
