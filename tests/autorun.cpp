// ============================================================
//  Autorun (phase 113)
//
//  A sequence runs for hours with nobody watching, so what is worth
//  pinning is how it ends: every way out has to leave the files closed
//  and say why, and none of them may keep recording empty sky.
//
//  This drives the runner with a fake clock and a fake rig. What it
//  cannot see is the wiring in main.cpp, which is checklist 36.
// ============================================================

#include "core/AutorunRunner.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace raduk::core;
using AR = AutorunRunner;

static int g_fail = 0;

static void Check(bool ok, const char* what) {
    if (ok) printf("  ok    %s\n", what);
    else  { printf("  FAIL  %s\n", what); ++g_fail; }
}

// Plays the rig. A run lasts runSec unless told otherwise, and a sweep
// lasts focusSec.
struct Rig {
    AR       ar;
    double   now       = 1000.0;
    double   step      = 1.0;
    double   runSec    = 180.0;
    double   focusSec  = 40.0;
    bool     cameraDead = false;
    bool     focusStuck = false;
    bool     refuseRun  = false;
    uint32_t framesPerRun = 3000;
    bool     alternateEmpty = false;
    bool     targetSeen = true;
    double   altDeg     = 45.0;
    bool     disk       = true;

    bool     recording = false;
    bool     focusing  = false;
    double   until     = 0.0;
    uint32_t frames    = 0;
    int      runsStarted = 0;
    int      sweeps      = 0;
    int      stops       = 0;
    int      focusAborts = 0;
    int      checksFilled = 0;
    std::vector<char> order;

    AR::Config Config() const {
        AR::Config c;
        c.runs = 4; c.runSec = runSec; c.gapSec = 15.0; c.refocusEvery = 2;
        return c;
    }

    void Advance() {
        now += step;
        if (recording && !cameraDead && now >= until) recording = false;
        if (focusing && !focusStuck && now >= until)  focusing  = false;

        AR::Inputs in;
        in.recording = recording;
        in.focusing  = focusing;
        in.frames    = frames;
        if (ar.AboutToCheck(now)) {
            ++checksFilled;
            in.targetSeen  = targetSeen;
            in.altDeg      = altDeg;
            in.diskHasRoom = disk;
        }
        switch (ar.Tick(now, in)) {
        case AR::Action::StartRun:
            order.push_back('R');
            if (refuseRun) { ar.RunRefused(); break; }
            ++runsStarted;
            recording = true; until = now + runSec;
            frames = (alternateEmpty && runsStarted % 2 == 1) ? 0 : framesPerRun;
            break;
        case AR::Action::StartFocus:
            order.push_back('F');
            ++sweeps;
            focusing = true; until = now + focusSec;
            break;
        case AR::Action::StopRun:
            ++stops;
            recording = false;
            break;
        case AR::Action::AbortFocus:
            ++focusAborts;
            focusing = false;
            break;
        default: break;
        }
    }

    void RunFor(double sec) {
        const double end = now + sec;
        while (now < end) Advance();
    }
    void RunOut(double limitSec = 100000.0) {
        const double end = now + limitSec;
        while (ar.Running() && now < end) Advance();
        Advance();   // anything owed from the last tick
    }
    std::string Order() const { return std::string(order.begin(), order.end()); }
};

static void TestCleanSequence() {
    printf("a clean sequence\n");
    Rig r;
    Check(r.ar.Start(r.Config(), r.now), "it starts");
    r.RunOut();
    Check(r.ar.State() == AR::Phase::Complete, "it completes");
    Check(r.runsStarted == 4 && r.ar.Done() == 4, "four runs");
    Check(r.ar.Reason() == "all runs done", "and says why");
    Check(!r.recording && !r.focusing, "nothing left going");
}

static void TestCadence() {
    printf("focus comes first, then every N runs\n");
    {
        Rig r;
        r.ar.Start(r.Config(), r.now);
        r.RunOut();
        Check(r.Order() == "FRRFRR", "F R R F R R");
    }
    {
        Rig r;
        auto c = r.Config(); c.refocusEvery = 1; c.runs = 3;
        r.ar.Start(c, r.now);
        r.RunOut();
        Check(r.Order() == "FRFRFR", "every run");
    }
    {
        Rig r;
        auto c = r.Config(); c.refocusEvery = 0;
        r.ar.Start(c, r.now);
        r.RunOut();
        Check(r.sweeps == 0 && r.runsStarted == 4, "0 never focuses");
    }
    {
        Rig r;
        auto c = r.Config(); c.refocusEvery = 4;
        r.ar.Start(c, r.now);
        r.RunOut();
        Check(r.sweeps == 1, "no sweep after the last run");
    }
}

static void TestGap() {
    printf("the gap\n");
    Rig r;
    auto c = r.Config(); c.refocusEvery = 0; c.gapSec = 30.0; c.runs = 2;
    r.ar.Start(c, r.now);
    r.Advance();
    Check(r.recording, "the first run has no gap before it");
    r.RunFor(r.runSec + 20.0);
    Check(!r.recording && r.runsStarted == 1, "20 s into a 30 s gap, waiting");
    Check(r.ar.State() == AR::Phase::Waiting, "and says so");
    r.RunFor(15.0);
    Check(r.runsStarted == 2, "past it, the next run");
}

static void TestChecksAreLazy() {
    printf("the checks are asked for only when they count\n");
    Rig r;
    auto c = r.Config(); c.refocusEvery = 0; c.runs = 2;
    r.ar.Start(c, r.now);
    r.RunOut();
    Check(r.checksFilled == 2, "once for each run, not once a tick");
}

static void TestAltitude() {
    printf("the altitude floor\n");
    {
        Rig r;
        auto c = r.Config(); c.refocusEvery = 0;
        r.ar.Start(c, r.now);
        r.RunFor(r.runSec + 5.0);
        r.altDeg = 19.0;
        r.RunOut();
        Check(r.ar.State() == AR::Phase::Complete, "a clean finish");
        Check(r.runsStarted == 1, "no run below it");
        Check(r.ar.Reason().find("altitude") != std::string::npos, "and says why");
    }
    {
        Rig r;
        r.altDeg = 20.0;
        r.ar.Start(r.Config(), r.now);
        r.RunOut();
        Check(r.runsStarted == 4, "exactly on it runs");
    }
    {
        Rig r;
        r.altDeg = -1.0;
        r.ar.Start(r.Config(), r.now);
        r.RunOut();
        Check(r.runsStarted == 4, "unknown altitude does not stop it");
    }
    {
        Rig r;
        r.altDeg = 10.0; r.targetSeen = false;
        r.ar.Start(r.Config(), r.now);
        r.RunOut();
        Check(r.ar.State() == AR::Phase::Complete,
              "a target that sank and left the frame is the floor, not a fault");
    }
}

static void TestDisk() {
    printf("the disk\n");
    Rig r;
    auto c = r.Config(); c.refocusEvery = 0;
    r.ar.Start(c, r.now);
    r.RunFor(r.runSec + 5.0);
    r.disk = false;
    r.RunOut();
    Check(r.ar.State() == AR::Phase::Failed, "fails");
    Check(r.runsStarted == 1, "before the run, not during it");
    Check(r.ar.Reason().find("disk") != std::string::npos, "and says why");
}

static void TestTargetLost() {
    printf("a lost target\n");
    {
        Rig r;
        r.targetSeen = false;
        r.ar.Start(r.Config(), r.now);
        r.RunOut();
        Check(r.ar.State() == AR::Phase::Failed, "gives up");
        Check(r.runsStarted == 0 && r.sweeps == 0,
              "having neither recorded nor swept on empty sky");
        Check(r.ar.Reason().find("left the frame") != std::string::npos,
              "and says why");
    }
    {
        Rig r;
        r.targetSeen = false;
        r.ar.Start(r.Config(), r.now);
        r.RunFor(60.0);
        Check(r.ar.Running(), "still holding at a minute");
        r.targetSeen = true;
        r.RunOut();
        Check(r.ar.State() == AR::Phase::Complete, "and picks up when it returns");
    }
    {
        // The hold has to restart: two short losses are not one long one.
        Rig r;
        auto c = r.Config(); c.refocusEvery = 0;
        r.ar.Start(c, r.now);
        r.targetSeen = false;
        r.RunFor(100.0);
        r.targetSeen = true;
        r.RunFor(r.runSec + 5.0);
        Check(r.runsStarted == 1 && !r.recording, "into the gap");
        r.targetSeen = false;
        r.RunFor(100.0);
        Check(r.ar.Running(), "a second loss gets its own two minutes");
    }
    {
        // Lost during the sweep: back through the checks before recording.
        Rig r;
        r.ar.Start(r.Config(), r.now);
        r.RunFor(5.0);
        Check(r.focusing, "sweeping");
        r.targetSeen = false;
        r.RunFor(r.focusSec + 5.0);
        Check(r.runsStarted == 0, "a sweep that lost the target does not record");
    }
}

static void TestRefusedRun() {
    printf("a recording that will not start\n");
    Rig r;
    r.refuseRun = true;
    auto c = r.Config(); c.refocusEvery = 0;
    r.ar.Start(c, r.now);
    r.RunOut();
    Check(r.ar.State() == AR::Phase::Failed, "fails at once");
    Check(r.order.size() == 1, "having tried once");
}

static void TestCameraDied() {
    printf("a camera that stops mid-run\n");
    Rig r;
    auto c = r.Config(); c.refocusEvery = 0;
    r.ar.Start(c, r.now);
    r.cameraDead = true;
    const double t0 = r.now;
    r.RunOut();
    Check(r.ar.State() == AR::Phase::Failed, "fails rather than waiting for ever");
    Check(r.stops == 1 && !r.recording, "and closes the file");
    const double limit = AR::RunLimitSec(r.runSec);
    Check(r.now - t0 >= limit && r.now - t0 < limit + 10.0,
          "at the run limit, not before");
    Check(r.ar.Reason().find("camera") != std::string::npos, "and says why");

    // A slow rate on a frame limit is not a dead camera.
    Rig s;
    auto d = s.Config(); d.refocusEvery = 0; d.runs = 2;
    s.runSec = d.runSec * 1.3;
    s.ar.Start(d, s.now);
    s.RunOut();
    Check(s.ar.State() == AR::Phase::Complete && s.stops == 0,
          "a run somewhat over its estimate is left to finish");
}

static void TestFocus() {
    printf("focus\n");
    {
        Rig r;
        r.focusStuck = true;
        r.ar.Start(r.Config(), r.now);
        r.RunOut();
        Check(r.ar.State() == AR::Phase::Failed, "a stuck sweep fails");
        Check(r.focusAborts == 1, "and is aborted");
        Check(r.runsStarted == 0, "with no run after it");
    }
    {
        Rig r;
        r.ar.Start(r.Config(), r.now);
        r.RunFor(5.0);
        r.ar.NoteFocusFailed();
        r.RunOut();
        Check(r.ar.State() == AR::Phase::Complete, "a failed sweep does not stop it");
        Check(r.ar.FocusFailures() == 1, "and is counted");
    }
    {
        // Focuser gone: the sweep is refused, which looks like one that
        // finished at once. The run goes ahead.
        Rig r;
        r.focusSec = 0.0;
        r.ar.Start(r.Config(), r.now);
        r.RunOut();
        Check(r.runsStarted == 4, "a refused sweep does not stop the runs");
    }
}

static void TestEmptyRuns() {
    printf("empty runs\n");
    {
        Rig r;
        r.framesPerRun = 0;
        auto c = r.Config(); c.refocusEvery = 0;
        r.ar.Start(c, r.now);
        r.RunOut();
        Check(r.ar.State() == AR::Phase::Failed, "two in a row fails");
        Check(r.runsStarted == 2, "after two");
        Check(r.ar.Empty() == 2, "and counts them");
    }
    {
        // One cloud passing is not the end of the night.
        Rig r;
        auto c = r.Config(); c.refocusEvery = 0;
        r.ar.Start(c, r.now);
        r.framesPerRun = 0;
        r.RunFor(r.runSec - 5.0);
        r.framesPerRun = 3000;
        r.frames = 0;
        r.RunOut();
        Check(r.ar.State() == AR::Phase::Complete, "one alone does not");
        Check(r.ar.Empty() == 1, "though it is counted");
    }
    {
        Rig r;
        auto c = r.Config(); c.refocusEvery = 0; c.runs = 1;
        r.framesPerRun = 0;
        r.ar.Start(c, r.now);
        r.RunOut();
        Check(r.ar.State() == AR::Phase::Complete, "a limit of one finishes");
    }
    {
        Rig r;
        r.alternateEmpty = true;
        auto c = r.Config(); c.refocusEvery = 0;
        r.ar.Start(c, r.now);
        r.RunOut();
        Check(r.ar.State() == AR::Phase::Complete && r.ar.Empty() == 2,
              "two apart are not two in a row");
    }
}

static void TestStops() {
    printf("stopping\n");
    {
        Rig r;
        auto c = r.Config(); c.refocusEvery = 0;
        r.ar.Start(c, r.now);
        r.RunFor(30.0);
        Check(r.recording, "recording");
        r.ar.Abort("operator");
        Check(!r.ar.Running(), "stopped");
        r.Advance();
        Check(r.stops == 1 && !r.recording, "and the file is closed");
        r.RunFor(600.0);
        Check(r.runsStarted == 1, "nothing starts afterwards");
        Check(r.ar.State() == AR::Phase::Aborted && r.ar.Reason() == "operator",
              "and says who");
    }
    {
        Rig r;
        r.ar.Start(r.Config(), r.now);
        r.RunFor(5.0);
        Check(r.focusing, "sweeping");
        r.ar.Abort("operator");
        r.Advance();
        Check(r.focusAborts == 1 && !r.focusing, "a sweep is aborted");
        Check(r.stops == 0, "with no stray stop");
    }
    {
        Rig r;
        auto c = r.Config(); c.refocusEvery = 0;
        r.ar.Start(c, r.now);
        r.RunFor(r.runSec + 5.0);
        Check(r.ar.State() == AR::Phase::Waiting, "in the gap");
        r.ar.Abort("operator");
        r.Advance();
        Check(r.stops == 0 && r.focusAborts == 0, "nothing is owed");
    }
    {
        Rig r;
        AR::Inputs in;
        r.ar.Abort("nothing to stop");
        Check(r.ar.State() == AR::Phase::Idle, "stopping nothing changes nothing");
        Check(r.ar.Tick(0.0, in) == AR::Action::None, "and an idle tick does nothing");
        Check(!r.ar.AboutToCheck(1e9), "nor asks for any checks");
    }
    {
        // A finished sequence is left alone by a late stop.
        Rig r;
        r.ar.Start(r.Config(), r.now);
        r.RunOut();
        r.ar.Abort("late");
        Check(r.ar.State() == AR::Phase::Complete &&
              r.ar.Reason() == "all runs done", "a late stop changes no record");
    }
}

static void TestRestart() {
    printf("starting again\n");
    Rig r;
    auto c = r.Config(); c.refocusEvery = 0; c.runs = 1;
    r.framesPerRun = 0;
    r.ar.Start(c, r.now);
    r.ar.NoteFocusFailed();
    r.RunOut();
    r.framesPerRun = 3000;
    r.ar.Start(r.Config(), r.now);
    Check(r.ar.Done() == 0 && r.ar.Empty() == 0 && r.ar.FocusFailures() == 0,
          "every count clears");
    Check(r.ar.Reason().empty(), "and so does the reason");
    r.RunOut();
    Check(r.ar.State() == AR::Phase::Complete, "and it runs");
}

static void TestRefusals() {
    printf("configs that may not start\n");
    AR::Config c; c.runSec = 180.0;
    Check(AR::Refusal(c) == nullptr, "a good one may");
    { auto b = c; b.runs = 0;          Check(AR::Refusal(b), "no runs"); }
    { auto b = c; b.runSec = 0.0;      Check(AR::Refusal(b), "no length"); }
    { auto b = c; b.gapSec = -1.0;     Check(AR::Refusal(b), "negative gap"); }
    { auto b = c; b.refocusEvery = -1; Check(AR::Refusal(b), "negative refocus"); }
    { auto b = c; b.gapSec = 0.0;      Check(!AR::Refusal(b), "no gap is allowed"); }

    AR ar; AR::Config b = c; b.runSec = 0.0;
    Check(!ar.Start(b, 0.0), "Start refuses");
    Check(ar.State() == AR::Phase::Failed && !ar.Reason().empty(), "and says why");
    Check(!ar.Running(), "and does not run");
}

static void TestRunLength() {
    printf("how long a run is\n");
    auto near = [](double a, double b) { return a > b - 1e-9 && a < b + 1e-9; };
    Check(near(AutorunRunSec(176, false, 0, 600, 100), 176), "the window beats a longer limit");
    Check(near(AutorunRunSec(176, false, 0, 60, 100), 60), "a shorter limit beats the window");
    Check(near(AutorunRunSec(176, false, 0, 0, 100), 176), "no limit, the window");
    Check(near(AutorunRunSec(176, true, 50000, 0, 100), 176), "frames: longer than the window");
    Check(near(AutorunRunSec(176, true, 1000, 0, 100), 10), "frames: shorter");
    Check(near(AutorunRunSec(176, true, 1000, 0, 0), 176), "no rate yet, the window");
    Check(near(AutorunRunSec(0, false, 0, 90, 100), 90), "the Moon: the limit");
    Check(near(AutorunRunSec(0, true, 1000, 0, 50), 20), "the Moon: frames over rate");
    Check(near(AutorunRunSec(0, true, 1000, 0, 0), 0), "no window and no rate is unknown");
    Check(near(AutorunRunSec(0, false, 0, 0, 100), 0), "no window and no limit is unknown");
}

static void TestRunBytes() {
    printf("what a run writes\n");
    const uint64_t b8 = AutorunRunBytes(1000, 1000, 8, 100.0, 10.0);
    Check(b8 == 1100000000ull, "RAW8: a million bytes, 1000 frames, a tenth over");
    Check(AutorunRunBytes(1000, 1000, 16, 100.0, 10.0) == 2 * b8, "RAW16 is two bytes");
    const uint64_t big = AutorunRunBytes(3840, 2160, 16, 47.0, 176.0);
    Check(big > 100ull * 1000 * 1000 * 1000, "the full sensor does not overflow 32 bits");
    Check(AutorunRunBytes(0, 1000, 8, 100, 10) == 0, "no frame, no bytes");
    Check(AutorunRunBytes(1000, 1000, 8, 0, 10) == 0, "no rate, no estimate");
}

int main() {
    TestCleanSequence();
    TestCadence();
    TestGap();
    TestChecksAreLazy();
    TestAltitude();
    TestDisk();
    TestTargetLost();
    TestRefusedRun();
    TestCameraDied();
    TestFocus();
    TestEmptyRuns();
    TestStops();
    TestRestart();
    TestRefusals();
    TestRunLength();
    TestRunBytes();
    printf(g_fail ? "\n%d FAILED\n" : "\nall passed\n", g_fail);
    return g_fail ? 1 : 0;
}
