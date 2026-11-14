// ============================================================
//  Phase 153.0: the core's autofocus feeds, and its seams
//  (src/app/AutofocusFeed.h, Clock.h, UiSink.h)
//
//    1. The fine pass's edge region: the surface, or sized from the disc.
//    2. Fast scores: only while the routine wants them, only from raw
//       Bayer frames, at most every 33 ms on the core's clock; the region
//       chosen once a run, logged once, and forgotten when the run ends.
//    3. Sharpness: only fresh, valid, measured analyses, only while
//       autofocus runs.
//    4. The recording sink and the manual clock.
// ============================================================

#include "app/AutofocusFeed.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace raduk::app;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

struct FakeRoutine {
    bool running = true, wantsFast = true, fineOnly = false;
    int fast = 0, sharp = 0;
    float lastSharp = 0; bool lastValid = false; int lastPeak = -1;
    bool IsRunning() const { return running; }
    bool WantsFastScores() const { return wantsFast; }
    bool FineOnly() const { return fineOnly; }
    void SubmitFastScore(float) { ++fast; }
    void SubmitSharpness(float s, bool v, int p) { ++sharp; lastSharp = s; lastValid = v; lastPeak = p; }
};

// A bright disc with soft edges on a dark sky, 8-bit, so the edge metric
// has something to measure.
static std::vector<uint8_t> Disc(int w, int h, float cx, float cy, float r) {
    std::vector<uint8_t> b((size_t)w * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
            const float v = d < r - 3 ? 200.0f : d > r + 3 ? 10.0f : 10.0f + 190.0f * (r + 3 - d) / 6.0f;
            b[(size_t)y * w + x] = (uint8_t)(v + ((x * 7 + y * 13) % 5));
        }
    return b;
}

int main() {
    printf("The core's autofocus feeds (phase 153.0)\n");

    Check(AutofocusFeed::EdgeRegionHalf(true, 50.0f) == 128, "a filled frame: the surface, 128 px");
    Check(AutofocusFeed::EdgeRegionHalf(false, 0.0f) == 48, "a tiny disc: at least 48 px");
    Check(AutofocusFeed::EdgeRegionHalf(false, 1000.0f) == 200, "a huge disc: at most 200 px");
    Check(AutofocusFeed::EdgeRegionHalf(false, 60.0f) == (int)(60.0f / 1.41421356f * 1.3f) + 24,
          "in between: 1.3 x the radius, plus 24");

    const int W = 400, H = 300;
    auto img = Disc(W, H, 200, 150, 60);
    FrameView f; f.data = img.data(); f.width = W; f.height = H; f.bitDepth = 8; f.bayerRaw = true;
    AnalysisView a; a.valid = true; a.centroidValid = true; a.centroidX = 200; a.centroidY = 150;
    a.focusHfdPx = 85.0f; a.centroidBgDN = 10.0f;

    {
        FakeRoutine af; AutofocusFeed feed; ManualClock clk; RecordingUiSink ui;
        feed.FastScore(af, f, a, clk, &ui);
        Check(af.fast == 1, "the first frame of a fine pass is scored");
        feed.FastScore(af, f, a, clk, &ui);
        clk.Advance(0.020);
        feed.FastScore(af, f, a, clk, &ui);
        Check(af.fast == 1, "not again within 33 ms of the core's clock");
        clk.Advance(0.020);
        feed.FastScore(af, f, a, clk, &ui);
        Check(af.fast == 2, "again once 33 ms have passed");
        Check(ui.Count() == 1 && ui.Saw("fine pass region"), "the region is logged once a run",
              std::to_string(ui.Count()) + " lines");
        const int half = feed.EdgeHalf();
        a.focusHfdPx = 10.0f;
        clk.Advance(0.05); feed.FastScore(af, f, a, clk, &ui);
        Check(feed.EdgeHalf() == half, "the region stays fixed for the run");
        af.running = false;
        feed.FastScore(af, f, a, clk, &ui);
        Check(feed.EdgeHalf() == -1, "and is forgotten when the run ends");
        a.focusHfdPx = 85.0f;
    }
    {
        FakeRoutine af; AutofocusFeed feed; ManualClock clk;
        af.wantsFast = false; feed.FastScore(af, f, a, clk, nullptr);
        Check(af.fast == 0, "nothing while the routine does not want fast scores");
        af.wantsFast = true; FrameView rgb = f; rgb.bayerRaw = false;
        clk.Advance(1); feed.FastScore(af, rgb, a, clk, nullptr);
        Check(af.fast == 0, "nothing from a frame that is not raw Bayer");
        FrameView none = f; none.data = nullptr;
        clk.Advance(1); feed.FastScore(af, none, a, clk, nullptr);
        Check(af.fast == 0, "nothing without a frame");
        af.fineOnly = true; clk.Advance(1); feed.FastScore(af, f, a, clk, nullptr);
        Check(feed.EdgeHalf() == 128, "the Moon path scores the surface");
    }
    {
        FakeRoutine af;
        AnalysisView s = a; s.focusValid = true; s.focusScore = 12.5f; s.centroidPeak = 180;
        AutofocusFeed::Sharpness(af, s, false);
        Check(af.sharp == 0, "a stale analysis is not fed");
        AutofocusFeed::Sharpness(af, s, true);
        Check(af.sharp == 1 && af.lastSharp == 12.5f && af.lastValid && af.lastPeak == 180,
              "a fresh, measured one is fed with its centroid and peak");
        s.focusValid = false; AutofocusFeed::Sharpness(af, s, true);
        Check(af.sharp == 1, "not when the HFD was not measured");
        s.focusValid = true; af.running = false; AutofocusFeed::Sharpness(af, s, true);
        Check(af.sharp == 1, "not when autofocus is not running");
    }
    {
        ManualClock c(5.0); c.Advance(2.5);
        Check(c.Now() == 7.5, "the manual clock moves only when told");
        SteadyClock s; const double t0 = s.Now();
        Check(s.Now() >= t0, "the steady clock does not go back");
        RecordingUiSink ui; ui.Status("autofocus", "Sweeping"); ui.Toast(ToastLevel::Warn, "fade");
        const auto e = ui.Entries();
        Check(e.size() == 2 && e[0].kind == "status" && e[0].area == "autofocus" && e[1].area == "warn",
              "the recording sink keeps what would have been shown");
    }

    if (g_fail) { printf("app_feed: %d FAILED\n", g_fail); return 1; }
    printf("app_feed: all passed\n");
    return 0;
}
