// ============================================================
//  scenario_runner -- phase 125
//
//  The script reader and the step logic, on a fake clock, before any of
//  it drives the app. The app half (actions, sandbox, screenshots) is
//  exercised by tests/run-scenarios.ps1 against the emulator build.
// ============================================================

#include "../src/core/Scenario.h"

#include <cstdio>

using namespace raduk::core;
using K = ScenarioAction::Kind;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) ++g_fail;
}

static bool LoadOk(ScenarioRunner& r, const char* text) {
    std::string why;
    const bool ok = r.Load(text, why);
    if (!ok) printf("    (load: %s)\n", why.c_str());
    return ok;
}

int main() {
    printf("Scenario runner\n");

    printf("\n1. Reading a script\n");
    {
        ScenarioRunner r;
        Check(LoadOk(r,
            "# a comment\n"
            "name   Runs\r\n"
            "timeout 90\n"
            "rig AutorunRuns=2\n"
            "env RADUK_EMU_SEEING_AS = 1\n"
            "\n"
            "wait 3   # settle\n"
            "goto Jupiter\n"
            "expect 60 Acquisition: Locked\n"
            "press RUNS\n"
            "absent 5 Recording started\n"
            "shot after\n"), "a full script loads");
        Check(r.Name() == "Runs" && r.TimeoutSec() == 90, "name and timeout");
        Check(r.Rig().size() == 1 && r.Rig()[0].first == "AutorunRuns" && r.Rig()[0].second == "2",
              "a rig setting");
        Check(r.Env().size() == 1 && r.Env()[0].first == "RADUK_EMU_SEEING_AS" && r.Env()[0].second == "1",
              "an environment variable, spaces trimmed");
        std::string why;
        ScenarioRunner a, b, c, d;
        Check(!a.Load("jump 3\n", why) && why.find("line 1") != std::string::npos, "an unknown step is refused, with its line");
        Check(!b.Load("expect Locked\n", why), "an expect with no time is refused");
        Check(!c.Load("rig AutorunRuns\n", why), "a rig line with no value is refused");
        Check(!d.Load("name only\n", why), "a script with no steps is refused");
    }

    printf("\n2. Steps in order\n");
    {
        ScenarioRunner r;
        LoadOk(r, "wait 2\npress RUNS\nexpect 5 run 1 of\nexpect 5 run 2 of\nshot done\n");
        Check(r.Tick(0).kind == K::None, "nothing while waiting");
        Check(r.Tick(1.9).kind == K::None, "still waiting at 1.9 s");
        ScenarioAction a = r.Tick(2.0);
        Check(a.kind == K::Press && a.arg == "RUNS", "then the press is handed out");
        Check(r.Tick(2.1).kind == K::None, "then it waits for the log");
        r.Observe("[AUTORUN] run 2 of 2\n[AUTORUN] run 1 of 2", 2.5);
        a = r.Tick(2.6);
        Check(a.kind == K::Shot && a.arg == "done",
              "lines after one action match in either order, then the shot");
        Check(r.Tick(3.2).kind == K::Finish && r.Passed(), "and it finishes passed");
        Check(r.Report().find("Result: PASS") != std::string::npos, "the report says so");
    }

    {
        ScenarioRunner r;
        LoadOk(r, "press RUNS\nexpect 5 Recording started\nexpect 5 Recording started\n");
        r.Tick(0);
        r.Observe("[CAPTURE] Recording started: 1.ser", 0.5);
        Check(r.Tick(0.6).kind == K::None, "a repeated expect needs a second line, not the same one again");
        r.Observe("[CAPTURE] Recording started: 2.ser", 1.0);
        Check(r.Tick(1.1).kind == K::Finish && r.Passed(), "and the second line satisfies it");
    }
    {
        ScenarioRunner r;
        LoadOk(r, "expect 5 Locked\npress RUNS\nexpect 5 Locked\n");
        r.Observe("Acquisition: Locked", 0.1);
        Check(r.Tick(0.2).kind == K::Press, "the first expect matches, and the press follows at once");
        Check(r.Tick(5.5).kind == K::Finish && !r.Passed(),
              "a line from before the last action does not count for the expects after it");
    }

    printf("\n3. Failing\n");
    {
        ScenarioRunner r;
        LoadOk(r, "expect 3 Locked\nshot never\n");
        r.Observe("Acquisition: Searching", 1);
        Check(r.Tick(1).kind == K::None, "not yet");
        Check(r.Tick(3.9).kind == K::None, "the clock starts at the first tick, so 3 s runs to 4");
        Check(r.Tick(4.1).kind == K::Finish && !r.Passed(), "an expect that times out fails");
        const std::string rep = r.Report();
        Check(rep.find("never saw 'Locked'") != std::string::npos, "and says what it waited for");
        Check(rep.find("Acquisition: Searching") != std::string::npos, "with the last lines the app said");
        Check(rep.find("skip") != std::string::npos, "and the steps it never reached");
    }
    {
        ScenarioRunner r;
        LoadOk(r, "absent 5 Recording started\n");
        r.Tick(0);
        r.Observe("[CAPTURE] Recording started: x.ser", 2);
        Check(r.Tick(2.1).kind == K::Finish && !r.Passed(), "an absent line that appears fails");
    }
    {
        ScenarioRunner r;
        LoadOk(r, "absent 5 Recording started\n");
        r.Observe("[CAPTURE] Recording started: earlier.ser", 0);
        r.Tick(0.5);
        Check(r.Tick(5.6).kind == K::Finish && r.Passed(), "but one from before the step does not count");
    }
    {
        ScenarioRunner r;
        LoadOk(r, "timeout 10\nwait 60\n");
        r.Tick(0);
        Check(r.Tick(10.5).kind == K::Finish && !r.Passed() &&
              r.FailureReason().find("timeout") != std::string::npos, "the whole scenario has a timeout");
    }
    {
        ScenarioRunner r;
        LoadOk(r, "press NOPE\nwait 1\n");
        Check(r.Tick(0).kind == K::Press, "a press is handed out");
        r.ActionFailed("no button called NOPE");
        Check(r.Tick(0.1).kind == K::Finish && !r.Passed() &&
              r.Report().find("no button called NOPE") != std::string::npos,
              "and a press the app cannot do fails the scenario");
    }

    printf("\n%s\n", g_fail ? "FAILED" : "All passed");
    return g_fail ? 1 : 0;
}
