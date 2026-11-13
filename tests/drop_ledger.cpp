// ============================================================
//  Phase 146.0: the drop ledger (src/core/DropLedger.h)
//
//    1. A steady run: no gaps, the rate right, transfer-limited.
//    2. One hole of 3 intervals: one gap, 2 frames implied missing,
//       unexplained when nothing else reports them.
//    3. The same hole explained by the SDK, by the gate, by our buffer.
//    4. Jitter under 1.5x the median is not a gap.
//    5. The SDK count is the run's own share, not the camera's total.
//    6. Exposure-limited against transfer-limited.
//    7. Too few frames to time, and the log line.
// ============================================================

#include "core/DropLedger.h"

#include <cstdio>
#include <string>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-58s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}
static std::string U(unsigned long long v) { return std::to_string(v); }

int main() {
    printf("Drop ledger (phase 146.0)\n");
    const uint64_t T = 20000;   // 50 fps

    {
        DropLedger L; L.Begin(0);
        for (int i = 0; i < 500; ++i) L.OnWritten(1000000 + (uint64_t)i * T);
        const DropReport r = L.Finish(0, 0, 0, 5.0);
        Check(r.haveTiming && r.gaps == 0 && r.impliedMissing == 0 && r.unexplained == 0,
              "steady: no gaps, nothing missing");
        Check(std::fabs(r.nominalFps - 50.0) < 0.01 && std::fabs(r.effectiveFps - 50.0) < 0.01,
              "steady: 50 fps nominal and effective", std::to_string(r.effectiveFps));
        Check(!r.exposureLimited, "5 ms exposure at 20 ms a frame: transfer-limited");
    }

    auto holey = [&](DropLedger& L) {
        uint64_t t = 1000000;
        for (int i = 0; i < 300; ++i) {
            L.OnWritten(t);
            t += (i == 150) ? 3 * T : T;   // two frames never arrive
        }
    };
    {
        DropLedger L; L.Begin(0); holey(L);
        const DropReport r = L.Finish(0, 0, 0, 5.0);
        Check(r.gaps == 1 && r.impliedMissing == 2, "one hole of 3 intervals: 1 gap, 2 missing",
              U(r.gaps) + " / " + U(r.impliedMissing));
        Check(r.unexplained == 2, "nothing reports them: 2 unexplained", U(r.unexplained));
    }
    {
        DropLedger L; L.Begin(7); holey(L);
        Check(L.Finish(9, 0, 0, 5.0).unexplained == 0, "the SDK reports 2 more drops: explained");
    }
    {
        DropLedger L; L.Begin(0); holey(L);
        Check(L.Finish(0, 0, 2, 5.0).unexplained == 0, "the gate skipped 2: explained, not lost");
    }
    {
        DropLedger L; L.Begin(0); holey(L);
        const DropReport r = L.Finish(0, 1, 0, 5.0);
        Check(r.unexplained == 1 && r.droppedBuffer == 1, "our buffer dropped 1: 1 still unexplained");
    }
    {
        DropLedger L; L.Begin(0);
        uint64_t t = 0;
        // USB jitter spreads round one interval: 0.75x to 1.4x of it.
        const double j[] = { 0.75, 0.9, 1.0, 1.1, 1.25, 1.4, 1.0, 0.95 };
        for (int i = 0; i < 400; ++i) { L.OnWritten(t); t += (uint64_t)(T * j[i % 8]); }
        const DropReport r = L.Finish(0, 0, 0, 5.0);
        Check(r.gaps == 0, "jitter under 1.5x the median: no gaps", U(r.gaps));
    }
    {
        DropLedger L; L.Begin(1200);
        for (int i = 0; i < 100; ++i) L.OnWritten((uint64_t)i * T);
        const DropReport r = L.Finish(1203, 0, 0, 5.0);
        Check(r.droppedSdk == 3, "the run is charged its own 3 SDK drops, not 1203", U(r.droppedSdk));
        DropLedger L2; L2.Begin(50);
        Check(L2.Finish(10, 0, 0, 5.0).droppedSdk == 0, "a counter reset (camera reopened) is not negative");
    }
    {
        DropLedger L; L.Begin(0);
        for (int i = 0; i < 50; ++i) L.OnWritten((uint64_t)i * 2000000);   // 2 s exposures
        const DropReport r = L.Finish(0, 0, 0, 2000.0);
        Check(r.exposureLimited, "2 s exposures arriving every 2 s: exposure-limited");
    }
    {
        DropLedger L; L.Begin(0);
        L.OnWritten(0); L.OnWritten(T);
        const DropReport r = L.Finish(4, 0, 0, 5.0);
        Check(!r.haveTiming && r.droppedSdk == 4, "two frames: too few to time, drops still counted");
        const std::string line = DropLine(r, "ZWO ASI678MC");
        Check(line.find("too few to time") != std::string::npos && line.find("ZWO ASI678MC") == 0,
              "the line names the camera and says so", line.substr(0, 40));
        DropLedger M; M.Begin(0); holey(M);
        const std::string full = DropLine(M.Finish(0, 0, 0, 5.0), "");
        Check(full.find("2 unexplained") != std::string::npos && full.find("transfer-limited") != std::string::npos,
              "the full line gives the unexplained count and the limit");
    }

    printf(g_fail ? "\n%d FAILED\n" : "\nall passed\n", g_fail);
    return g_fail ? 1 : 0;
}
