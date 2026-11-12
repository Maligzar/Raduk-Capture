// ============================================================
//  Phase 139.0 to 139.2: focus that respects backlash
//
//    1. Two V-curves a known slack apart, sampled as autofocus samples
//       (11 points, 600 steps apart): fitting each direction gives the
//       slack back.
//    2. Their average peaks about half the slack from the outward peak,
//       which is where an outward final approach must land, and is
//       flatter: what the averaging since F9 did.
//    3. One-sided moves: every move ends travelling outward by at least
//       the backlash, so the owner's test (find focus, go inward 1000,
//       come back to the number) returns to the same mirror position.
//    4. The backlash figure says where it came from.
// ============================================================

#include "core/FocusDirection.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}
static std::string F(double v) { char b[32]; snprintf(b, sizeof(b), "%.0f", v); return b; }

static constexpr int kFocus = 169000;
static float V(double opticPos) {           // the autofocus_saturn curve
    const double d = (opticPos - kFocus) / 1800.0;
    return (float)(6.0 + 235.0 / (1.0 + d * d));
}

// A focuser with slack: the mirror trails the count by up to half the
// slack either way, as the emulator models it.
struct Slack {
    int half; double optic;
    void Go(int step) {
        if (step > optic + half) optic = step - half;
        if (step < optic - half) optic = step + half;
    }
};

int main() {
    printf("Focus direction (phase 139.0 to 139.2)\n");
    const int B = 400;

    printf("\n1. Each direction fitted alone\n");
    std::vector<int> pos;
    std::vector<float> out, back;
    {
        const int start = kFocus - 3000 + 170;  // not centred on a sample
        Slack s{B / 2, (double)start};
        for (int i = 0; i < 11; i++) pos.push_back(start + i * 600);
        out.assign(11, 0); back.assign(11, 0);
        for (int i = 0; i < 11; i++)  { s.Go(pos[i]); out[i] = V(s.optic); }
        for (int i = 10; i >= 0; i--) { s.Go(pos[i]); back[i] = V(s.optic); }
    }
    const DirectionFit f = FitDirections(pos, out, back);
    Check(f.out.ok && f.back.ok, "both directions give a peak");
    Check(std::fabs(f.out.pos - (kFocus + B / 2)) < 120, "outward peak at focus + half the slack", F(f.out.pos));
    Check(std::fabs(f.back.pos - (kFocus - B / 2)) < 120, "inward peak at focus - half the slack", F(f.back.pos));
    Check(f.haveBacklash && std::fabs(f.backlash - B) < 150, "their difference is the slack", F(f.backlash));

    printf("\n2. The average, as fitted since F9\n");
    Check(f.mean.ok && std::fabs(f.mean.pos - kFocus) < 120, "the average peaks near the middle", F(f.mean.pos));
    Check(std::fabs(f.mean.pos - f.out.pos) > B / 2 - 120,
          "half the slack short of where an outward approach must land", F(f.out.pos - f.mean.pos));
    Check(f.mean.height < f.out.height, "and its peak is lower: flattened");
    {
        // Approach outward to each answer, as autofocus's final move does.
        Slack s{B / 2, 0.0};
        auto land = [&](double target) { s.Go((int)target - 1000); s.Go((int)target); return s.optic; };
        const double byOut = land(f.out.pos), byMean = land(f.mean.pos);
        Check(std::fabs(byOut - kFocus) < 120, "outward fit, outward approach: on focus", F(byOut - kFocus));
        Check(std::fabs(byMean - kFocus) > B / 2 - 120, "averaged fit, outward approach: off by half the slack",
              F(byMean - kFocus));
    }

    printf("\n3. One-sided moves\n");
    {
        ApproachSequencer q;
        q.SetBacklash(B);
        Slack s{B / 2, 169500.0};
        int at = 169500;
        auto move = [&](int to) {
            int leg = q.Begin(at, to);
            int legs = 0;
            while (leg >= 0) { s.Go(leg); at = leg; legs++; leg = q.NextLeg(at); }
            return legs;
        };
        move(169200);                    // focus found, approached somehow
        const double sharp = s.optic;
        move(168200);                    // 1000 inward
        move(169200);                    // back to the number
        Check(std::fabs(s.optic - sharp) < 1e-9, "inward 1000 and back: the same mirror position",
              F(s.optic) + " vs " + F(sharp));
        move(170200);                    // 1000 outward
        move(169200);
        Check(std::fabs(s.optic - sharp) < 1e-9, "outward 1000 and back: the same", F(s.optic));
        Check(move(169250) == 1, "a small outward nudge, slack already taken: one leg");
        Check(move(169150) == 2, "a small inward nudge: past and back, two legs");
        Check(at == 169150, "and it ends on the number asked for", std::to_string(at));
        q.SetBacklash(0);
        Check(move(168000) == 1, "backlash 0: off, one leg");
        move(169200); const double up = s.optic;
        move(170200); move(169200);
        Check(std::fabs(s.optic - up) > B - 1,
              "without it, the owner's report: same number from above, a slack away", F(s.optic - up));
        q.SetBacklash(B);
        q.Begin(at, 167000);
        q.Cancel();
        Check(!q.Pending() && q.NextLeg(at) < 0, "a stop cancels the second leg");
        OneSidedApproach a; a.backlash = B;
        Check(a.ViaFor(100, 50) == 0, "never below step 0");
    }

    printf("\n4. Which figure\n");
    const char* s1 = ""; const char* s2 = ""; const char* s3 = "";
    const int r1 = ResolveBacklash(350, 80, &s1);
    const int r2 = ResolveBacklash(-1, 80, &s2);
    const int r3 = ResolveBacklash(-1, 0, &s3);
    Check(r1 == 350 && std::string(s1).find("rig.ini") != std::string::npos, "rig.ini first", s1);
    Check(r2 == 80 && std::string(s2).find("EAF") != std::string::npos, "then the EAF's own", s2);
    Check(r3 == 200 && std::string(s3).find("not measured") != std::string::npos,
          "then 200, and says it is a guess", s3);

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
