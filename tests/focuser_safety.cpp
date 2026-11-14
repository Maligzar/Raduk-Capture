// ============================================================
//  Focuser safety (phase 161.0a)
//
//  From the first-principles review of 2026-11-14. A failed position
//  read used to come back as 0 and become the base of a move; a move
//  that was refused, stalled or stopped short went unnoticed; a second
//  key press stepped from wherever the motor happened to be.
//
//  One case per rule, on the pure checks (FocuserGuard.h), on the
//  emulated focuser with its injected faults, and on the real autofocus
//  routine driving it. The assertion that matters most: no single fault
//  can make the focuser travel further than the guard.
// ============================================================

#define RADUK_EMULATOR 1
#include "core/AutofocusRoutine.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
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
static bool WaitStopped(EAFFocuser& f, int limitMs = 20000) {
    for (int t = 0; t < limitMs; t += 5) { if (!f.IsMoving()) return true; Ms(5); }
    return false;
}

static constexpr int kHome = 166500;   // where the emulator opens

// ── The real routine, on a smooth score that peaks at kFocus ──
static constexpr int kFocus = 167200;
struct AfRun {
    bool        done = false;
    AFResult    res;
    int         excursion = 0;
    bool        movingAfter = false;
    double      seconds = 0.0;
};
// `during` runs once the sweep is under way, and injects the fault.
template <class Fault>
static AfRun RunAf(EAFFocuser& f, Fault during, int guard = kDefaultMaxTravelSteps) {
    f.SetMaxTravel(guard);
    f.ResetExcursion();
    AutofocusRoutine af;
    af.SetFocuser(&f);
    AutofocusParams p;
    p.settleMs = 30;
    p.fineStage = false;
    af.SetParams(p);
    af.SetLogger([](const char*) {});
    std::atomic<bool> done{false}, feed{true};
    AfRun out;
    af.SetOnComplete([&](AFResult r) { out.res = r; done.store(true); });
    std::thread feeder([&] {
        while (feed.load()) {
            const double d = (f.GetOpticalPosition() - kFocus) / 1800.0;
            af.SubmitSharpness((float)(6.0 + 235.0 / (1.0 + d * d)), true, 120);
            Ms(8);
        }
    });
    const auto t0 = std::chrono::steady_clock::now();
    af.Start(f.GetPosition());
    // Let it get three positions into the sweep, then break something.
    for (int i = 0; i < 2000 && af.CurrentStep() < 3 && !done.load(); ++i) Ms(5);
    during(af);
    for (int i = 0; i < 4000 && !done.load(); ++i) Ms(10);
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    out.done = done.load();
    Ms(50);
    const int a = f.GetPosition(); Ms(60); const int b = f.GetPosition();
    out.movingAfter = (a != b);
    out.excursion = f.MaxExcursion();
    feed.store(false);
    feeder.join();
    return out;
}

int main() {
    std::printf("Focuser safety (phase 161.0a)\n");

    std::printf("\n1. May a move start? (pure)\n");
    {
        Check(CheckMove(false, 0, 100, 600000, 12000, false) == MoveRefusal::PositionUnknown,
              "not from a position that was not read");
        Check(CheckMove(true, 168000, -5, 600000, 12000, false) == MoveRefusal::OutOfRange,
              "not below 0");
        Check(CheckMove(true, 168000, 600001, 600000, 12000, false) == MoveRefusal::OutOfRange,
              "not beyond the focuser's maximum");
        Check(CheckMove(true, 168000, 0, 600000, 12000, false) == MoveRefusal::TooFar,
              "168000 to 0 is refused: the fault this phase exists for");
        Check(CheckMove(true, 168000, 180000, 600000, 12000, false) == MoveRefusal::None,
              "exactly the guard is allowed");
        Check(CheckMove(true, 168000, 180001, 600000, 12000, false) == MoveRefusal::TooFar,
              "one step past it is not");
        Check(CheckMove(true, 168000, 0, 600000, 12000, true) == MoveRefusal::None,
              "an override passes the guard");
        Check(CheckMove(true, 168000, -1, 600000, 12000, true) == MoveRefusal::OutOfRange,
              "and nothing else: the range still holds");
        Check(CheckMove(false, 0, 100, 600000, 12000, true) == MoveRefusal::PositionUnknown,
              "nor does it stand in for a position");
        Check(CheckMove(true, 168000, 0, 600000, 0, false) == MoveRefusal::None,
              "0 turns the guard off");
        Check(RelativeBase(-1, 500) == 500 && RelativeBase(990, 850) == 990,
              "a relative step adds to the commanded target while one is in flight");
    }

    std::printf("\n2. A second press past the guard (pure)\n");
    {
        FarMoveConfirm c;
        Check(!c.Confirmed(120000, 10.0), "the first ask is not a yes");
        Check(c.Confirmed(120000, 13.0), "the same target again within 5 s is");
        Check(!c.Confirmed(120000, 14.0), "and it is used up");
        Check(!c.Confirmed(130000, 15.0) && !c.Confirmed(120000, 16.0),
              "a different target starts over");
        Check(!c.Confirmed(120000, 30.0), "too late starts over");
    }

    std::printf("\n3. A position read that fails\n");
    {
        EAFFocuser f; f.Open(0); f.ResetExcursion();
        f.FailReads(1);
        int p = -1;
        Check(!f.TryGetPosition(p) && p == -1, "TryGetPosition says so, and writes nothing");
        f.FailReads(1);
        Check(f.GetPosition() == kHome, "GetPosition gives the last good position, never 0",
              N(f.GetPosition()));
        f.FailReads(1);
        Check(!f.MoveRelative(-10) && f.LastRefusalKind() == MoveRefusal::PositionUnknown,
              "a relative step is refused, and says why", f.LastRefusal());
        f.FailReads(1);
        Check(!f.MoveTo(kHome + 500) && f.LastRefusalKind() == MoveRefusal::PositionUnknown,
              "an absolute move is refused too: its legs are planned from the position");
        Ms(100);
        Check(f.MaxExcursion() == 0 && f.GetPosition() == kHome, "the motor never turned",
              N(f.MaxExcursion()));
        Check(f.MoveRelative(-10) && WaitStopped(f) && f.GetPosition() == kHome - 10,
              "and the next good read moves as asked", N(f.GetPosition()));
        f.Close();
    }

    std::printf("\n4. The travel guard and the range\n");
    {
        EAFFocuser f; f.Open(0); f.ResetExcursion();
        Check(!f.MoveTo(0) && f.LastRefusalKind() == MoveRefusal::TooFar,
              "166500 to 0 is refused", f.LastRefusal());
        Check(!f.MoveRelative(-50000) && f.LastRefusalKind() == MoveRefusal::TooFar,
              "so is a relative step that long");
        Check(!f.MoveToDirect(kHome + 12001) && f.LastRefusalKind() == MoveRefusal::TooFar,
              "and a sweep's one-leg move");
        Check(!f.MoveToFar(-1) && f.LastRefusalKind() == MoveRefusal::OutOfRange,
              "below 0 is refused even with the override");
        Check(!f.MoveToFar(f.GetMaxPosition() + 1), "and beyond the maximum");
        Ms(50);
        Check(f.MaxExcursion() == 0, "none of them turned the motor", N(f.MaxExcursion()));
        Check(f.MoveToFar(kHome + 14000), "the override makes the long move");
        Ms(100); f.Stop();
        Check(f.MaxExcursion() > 0, "which does turn it");
        f.SetMaxTravel(0);
        Check(f.MoveTo(kHome + 20000), "guard off: a long move is accepted");
        f.Stop();
        f.Close();
    }

    std::printf("\n5. Two quick presses of \"in 10\" during a one-sided move\n");
    {
        EAFFocuser f; f.Open(0);
        f.SetApproachBacklash(400);          // inward: 400 past, then back
        Check(f.MoveRelative(-10), "the first press");
        Ms(15);                              // mid overshoot leg
        const int mid = f.GetPosition();
        Check(f.MoveRelative(-10), "the second, while the first is in flight", "at " + N(mid));
        Check(WaitStopped(f) && f.GetPosition() == kHome - 20,
              "ends 20 below the start, not 10 below wherever the motor was", N(f.GetPosition()));
        Ms(30);
        Check(f.MoveRelative(30) && WaitStopped(f) && f.GetPosition() == kHome + 10,
              "and a step after it has stopped adds to the position", N(f.GetPosition()));
        f.Close();
    }

    std::printf("\n6. A move the device refuses, and one that stops short\n");
    {
        EAFFocuser f; f.Open(0); f.ResetExcursion();
        f.RefuseMoves(1);
        Check(!f.MoveTo(kHome + 300) && f.LastRefusalKind() == MoveRefusal::Device,
              "a refused command comes back false", f.LastRefusal());
        Check(!f.IsMoving() && f.MaxExcursion() == 0, "and nothing is left pending");

        f.SetStopsShortBy(40);
        Check(f.MoveToDirect(kHome + 300), "a move that will stop 40 short is accepted");
        const auto t0 = std::chrono::steady_clock::now();
        const bool stopped = WaitStopped(f, 6000);
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        Check(stopped && f.LastRefusalKind() == MoveRefusal::StoppedShort,
              "it is called short, not arrived", f.LastRefusal());
        Check(s > 1.5 && s < 4.0, "after the grace, not at once and not for ever", N((int)(s * 1000)) + " ms");
        f.SetStopsShortBy(0);
        f.Close();
    }

    std::printf("\n7. A motor slow to start does not lose the second leg\n");
    {
        EAFFocuser f; f.Open(0);
        f.SetSimulatedSlack(300);
        f.SetApproachBacklash(400);
        f.SetMovingLagMs(250);               // "not moving" for 250 ms after each command
        Check(f.MoveTo(kHome - 200), "an inward one-sided move, with the lag");
        Ms(20);
        Check(f.IsMoving(), "it counts as moving while short of its leg");
        Check(WaitStopped(f) && f.GetPosition() == kHome - 200, "it arrives", N(f.GetPosition()));
        // Finishing outward, the mirror trails the count by half the slack.
        Check(f.GetOpticalPosition() == kHome - 200 - 150,
              "by the overshoot leg and back: the slack is taken up outward",
              N(f.GetOpticalPosition() - (kHome - 200)));
        f.Close();
    }

    std::printf("\n8. Autofocus, with a fault part way through the sweep\n");
    {
        EAFFocuser f; f.Open(0);
        const AfRun r = RunAf(f, [](AutofocusRoutine&) {});
        Check(r.done && r.res.success, "no fault: it finishes", r.res.message);
        Check(std::abs(f.GetOpticalPosition() - kFocus) < 400, "near focus",
              N(f.GetOpticalPosition() - kFocus));
        Check(r.excursion <= kDefaultMaxTravelSteps, "inside the guard", N(r.excursion));
        f.Close();
    }
    {
        EAFFocuser f; f.Open(0);
        const AfRun r = RunAf(f, [&](AutofocusRoutine&) { f.RefuseMoves(1000); });
        Check(r.done && !r.res.success && r.res.message.find("refused") != std::string::npos,
              "moves refused: the sweep ends and says so", r.res.message);
        Check(!r.movingAfter && r.excursion <= kDefaultMaxTravelSteps, "stopped, inside the guard",
              N(r.excursion));
        f.Close();
    }
    {
        EAFFocuser f; f.Open(0);
        const AfRun r = RunAf(f, [&](AutofocusRoutine&) { f.SetStalled(true); });
        Check(r.done && !r.res.success && r.res.message.find("stalled") != std::string::npos,
              "a stall: the sweep ends and says so", r.res.message);
        f.SetStalled(false);
        Ms(200);
        const int a = f.GetPosition(); Ms(100);
        Check(a == f.GetPosition(), "and the motor was stopped: freed, it does not run on");
        f.Close();
    }
    {
        EAFFocuser f; f.Open(0);
        const AfRun r = RunAf(f, [&](AutofocusRoutine&) { f.FailReads(1000000); });
        Check(r.done && !r.res.success, "reads failing from here on: the sweep ends", r.res.message);
        f.FailReads(0);
        Check(!r.movingAfter && r.excursion <= kDefaultMaxTravelSteps,
              "stopped, inside the guard: it never moved from a guess", N(r.excursion));
        f.Close();
    }
    {
        EAFFocuser f; f.Open(0);
        const AfRun r = RunAf(f, [&](AutofocusRoutine&) { f.SetStopsShortBy(60); });
        Check(r.done && !r.res.success &&
              (r.res.message.find("stopped") != std::string::npos ||
               r.res.message.find("refused") != std::string::npos),
              "stopping short: no sample is credited to a step it never reached", r.res.message);
        f.Close();
    }
    {
        EAFFocuser f; f.Open(0);
        const AfRun r = RunAf(f, [&](AutofocusRoutine& af) { af.Abort(); });
        Check(r.done && !r.res.success && r.res.message == "Aborted by user",
              "an abort reads as an abort, never as a timeout", r.res.message);
        Check(!r.movingAfter, "and the motor is stopped");
        f.Close();
    }
    {
        EAFFocuser f; f.Open(0);
        const AfRun r = RunAf(f, [](AutofocusRoutine&) {}, 1500);
        Check(r.done && r.res.success, "a guard narrower than the sweep: it still focuses", r.res.message);
        Check(r.excursion <= 1500, "and never leaves the guard round its start", N(r.excursion));
        Check(std::abs(f.GetOpticalPosition() - kFocus) < 400, "near focus, which lay inside it",
              N(f.GetOpticalPosition() - kFocus));
        f.Close();
    }

    std::printf("\n%s\n", g_fail ? "FAILED" : "focuser_safety: all passed");
    return g_fail ? 1 : 0;
}
