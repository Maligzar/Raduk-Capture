// ============================================================
//  satellite_plan -- phase 118
//
//  Choosing a night's queue and running it. Passes are built by hand
//  here, because the question is the schedule and not the orbit, and
//  a hand-built case has a known answer. Section 1 also plans a real
//  day of real elements, so the two meet once.
// ============================================================

#include "../src/core/SatPlan.h"

#include <cstdio>

using namespace raduk::core;
using raduk::sky::SatPassInfo;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) ++g_fail;
}

static constexpr double NOW = 2461346.0;

// Minutes from now.
static SatPassRow Row(const char* name, double startMin, double lenMin,
                      double peak, double startAz, double endAz,
                      bool visible = true, double startAlt = 15.0) {
    SatPassRow r;
    r.name = name;
    auto& p = r.pass;
    p.trackable = true;
    p.trackStartJd = NOW + startMin / 1440.0;
    p.trackEndJd   = NOW + (startMin + lenMin) / 1440.0;
    p.peakAlt = peak;
    p.visible = visible;
    p.startAlt = startAlt; p.endAlt = 15.0;
    p.startAz = startAz;   p.endAz = endAz;
    return r;
}

static bool Same(const std::vector<int>& a, std::vector<int> b) { return a == b; }

int main() {
    printf("Satellite plan: a night of targets\n");
    SatPlanOptions o;

    printf("\n1. Choosing\n");
    {
        std::vector<SatPassRow> r = {Row("A", 10, 5, 40, 100, 200),
                                     Row("B", 30, 5, 40, 100, 200),
                                     Row("C", 50, 5, 40, 100, 200)};
        Check(Same(BuildSatPlan(r, NOW, 30, 100, o), {0, 1, 2}),
              "passes that fit all go in, in time order");
    }
    {
        // Overlapping: the higher one wins.
        std::vector<SatPassRow> r = {Row("low", 10, 6, 25, 100, 200),
                                     Row("high", 12, 6, 75, 100, 200)};
        Check(Same(BuildSatPlan(r, NOW, 30, 100, o), {1}), "of two that overlap, the higher");
    }
    {
        // Greedy loses: the early low pass blocks two later ones.
        std::vector<SatPassRow> r = {Row("long", 10, 20, 30, 100, 200),
                                     Row("x", 12, 4, 60, 100, 200),
                                     Row("y", 20, 4, 60, 200, 300)};
        Check(Same(BuildSatPlan(r, NOW, 30, 100, o), {1, 2}),
              "two good ones beat the first one to come along");
    }
    {
        // No overlap, but 170 degrees away with 45 s of gap.
        std::vector<SatPassRow> r = {Row("a", 10, 5, 60, 100, 10),
                                     Row("b", 15.75, 5, 50, 190, 280)};
        Check(!SatCanFollow(r[0], r[1], o), "a gap the tube cannot cross is a conflict");
        Check(BuildSatPlan(r, NOW, 30, 100, o).size() == 1, "and only one goes in");
        r[1].pass.startAz = 12.0;
        Check(SatCanFollow(r[0], r[1], o), "the same gap is fine if the next starts nearby");
    }
    {
        // Wrap: 350 to 10 is twenty degrees, not 340.
        Check(SatChangeoverSec(20, 350, 20, 10, o) < 60.0, "azimuth is measured the short way");
    }
    {
        // Starts too soon from where the tube is.
        std::vector<SatPassRow> r = {Row("now", 0.5, 5, 80, 280, 200),
                                     Row("later", 20, 5, 40, 100, 200)};
        Check(Same(BuildSatPlan(r, NOW, 30, 100, o), {1}), "a pass it cannot reach in time is left out");
    }
    {
        std::vector<SatPassRow> r = {Row("dark", 10, 5, 80, 100, 200, false),
                                     Row("lit", 30, 5, 30, 100, 200, true)};
        SatPlanOptions v = o; v.visibleOnly = true;
        Check(Same(BuildSatPlan(r, NOW, 30, 100, v), {1}), "visible only means visible only");
        Check(BuildSatPlan({}, NOW, 30, 100, o).empty(), "nothing in, nothing out");
    }
    {
        // A pass whose window has gone, and one that is not trackable.
        std::vector<SatPassRow> r = {Row("gone", -20, 5, 80, 100, 200),
                                     Row("no", 20, 5, 80, 100, 200)};
        r[1].pass.trackable = false;
        Check(BuildSatPlan(r, NOW, 30, 100, o).empty(), "past and untrackable passes stay out");
    }
    {
        std::vector<SatPassRow> r = {Row("a", 10, 5, 60, 100, 200),
                                     Row("b", 12, 5, 50, 100, 200),
                                     Row("c", 40, 5, 50, 100, 200)};
        Check(FirstSatConflict(r, {0, 2}, o) == -1, "an edited queue that works is passed");
        Check(FirstSatConflict(r, {0, 1, 2}, o) == 1, "and one that does not is pinned to its pass");
    }

    printf("\n1b. A real day\n");
    {
        const raduk::sky::Tle t = raduk::sky::ParseTle("ISS",
            "1 25544U 98067A   26305.53114906  .00006056  00000+0  11739-3 0  9995",
            "2 25544  51.6332 340.4448 0007086 335.8887  24.1768 15.49140542593046");
        raduk::sky::Sgp4 s;
        s.Init(t);
        const raduk::sky::LunarObserver site{33.354136, 243.1375, 1704.89, true};
        const double from = t.epochJd;
        std::vector<SatPassRow> rows;
        for (const auto& p : raduk::sky::FindPasses(s, site, from, 24.0, {}))
            rows.push_back({"ISS", t, p});
        const auto plan = BuildSatPlan(rows, from, 45, 180, o);
        printf("  %d passes, %d planned\n", (int)rows.size(), (int)plan.size());
        Check(!rows.empty() && !plan.empty(), "a real day plans");
        Check(FirstSatConflict(rows, plan, o) == -1, "with no conflict in it");
        const auto& f = rows[plan[0]].pass;
        Check(f.startAlt >= 14.9 && f.startAlt < 17.0, "and a window opens at the floor");
        Check(f.endAlt >= 14.9, "and closes above it");
    }

    std::vector<SatPassRow> three = {Row("A", 10, 5, 40, 100, 200),
                                     Row("B", 30, 5, 40, 100, 200),
                                     Row("C", 50, 5, 40, 100, 200)};
    const std::vector<int> all = {0, 1, 2};
    using S = SatPlanRunner::Step;

    printf("\n2. Running\n");
    {
        SatPlanRunner p;
        p.Begin(three, all, o);
        Check(p.Running() && p.Total() == 3, "it starts with three");
        Check(p.Tick(NOW, 30, 100, false, SatOutcome::None).kind == S::Wait,
              "and waits while the first is still far off");
        const double soon = NOW + 9.0 / 1440.0;
        const auto a = p.Tick(soon, 30, 100, false, SatOutcome::None);
        Check(a.kind == S::Start && a.index == 0, "and starts it in time to get there");
        Check(p.Tick(soon + 1e-4, 30, 100, true, SatOutcome::None).kind == S::Wait,
              "and leaves a pass in hand alone");
        const double after = NOW + 15.5 / 1440.0;
        Check(p.Tick(after, 15, 200, false, SatOutcome::Complete).kind == S::Wait,
              "the next is a way off, so it waits");
        Check(p.Captured() == 1, "one captured");
        const auto b = p.Tick(NOW + 29.0 / 1440.0, 15, 200, false, SatOutcome::None);
        Check(b.kind == S::Start && b.index == 1, "then the second");
        p.Tick(NOW + 35.5 / 1440.0, 15, 200, false, SatOutcome::Lost);
        Check(p.Running() && p.Missed() == 1, "a lost pass costs that pass and the plan goes on");
        const auto c = p.Tick(NOW + 49.0 / 1440.0, 15, 200, false, SatOutcome::None);
        Check(c.kind == S::Start && c.index == 2, "to the third");
        const auto d = p.Tick(NOW + 55.5 / 1440.0, 15, 200, false, SatOutcome::Complete);
        Check(d.kind == S::Finished && !p.Running(), "and then it is finished");
        Check(p.Captured() == 2, "two of three captured");
    }

    printf("\n3. What ends the night\n");
    {
        SatPlanRunner p;
        p.Begin(three, all, o);
        p.Tick(NOW + 9.0 / 1440.0, 30, 100, false, SatOutcome::None);
        const auto s = p.Tick(NOW + 12.0 / 1440.0, 30, 100, false, SatOutcome::Fault);
        Check(s.kind == S::Finished && !p.Running(), "a mount fault stops the plan");
        Check(p.Tick(NOW + 29.0 / 1440.0, 30, 100, false, SatOutcome::None).kind == S::Wait,
              "and nothing starts after it");
    }
    {
        SatPlanRunner p;
        p.Begin(three, all, o);
        p.Tick(NOW + 9.0 / 1440.0, 30, 100, false, SatOutcome::None);
        const auto s = p.Tick(NOW + 12.0 / 1440.0, 30, 100, false, SatOutcome::Stopped);
        Check(s.kind == S::Finished && !p.Running(), "Escape stops the plan, not just the pass");
    }
    {
        SatPlanRunner p;
        p.Begin(three, all, o);
        p.Abort("a jog");
        Check(!p.Running() && p.Why() == "a jog", "an abort between passes holds too");
        Check(p.Tick(NOW + 9.0 / 1440.0, 30, 100, false, SatOutcome::None).kind == S::Wait,
              "and no pass starts");
    }
    {
        SatPlanRunner p;
        p.Begin(three, all, o);
        p.Tick(NOW + 9.0 / 1440.0, 30, 100, false, SatOutcome::None);
        p.Refused();
        Check(p.Running() && p.Missed() == 1, "a pass refused by the cord costs only that pass");
    }
    {
        // Woken late: the app was busy and the first has gone.
        SatPlanRunner p;
        p.Begin(three, all, o);
        const auto s = p.Tick(NOW + 29.0 / 1440.0, 30, 100, false, SatOutcome::None);
        Check(s.kind == S::Start && s.index == 1 && p.Missed() == 1,
              "a pass already gone is skipped, and the next taken");
    }
    {
        SatPlanRunner p;
        p.Begin(three, {}, o);
        Check(!p.Running(), "an empty plan does not run");
    }

    printf("\n%s\n", g_fail ? "FAILED" : "All passed");
    return g_fail ? 1 : 0;
}
