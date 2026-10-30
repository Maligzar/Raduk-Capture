// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// ============================================================
//  Sensor analysis — the numbers the histogram could not show
//
//  Phase 36. docs/HISTOGRAM-ACCURACY.md established that the histogram
//  itself is arithmetically correct, and found two things it cannot
//  say: a Gr/Gb split is invisible because the two greens are averaged
//  before anything sees them, and nothing anywhere converts a
//  distribution into "how much more exposure can this take".
//
//  This covers both, plus the advice built on top of them. Synthetic
//  frames only — no camera, no sky.
// ============================================================

#include "core/FrameAnalyzer.h"
#include "core/FrameSlot.h"

#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <chrono>
#include <thread>

using namespace raduk::core;

static int g_fail = 0;

static void Check(bool ok, const std::string& what, const std::string& detail) {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static void CheckNear(float got, float want, float tol, const std::string& what) {
    char d[160];
    snprintf(d, sizeof(d), "got %.2f, expected %.2f +/- %.2f", got, want, tol);
    Check(std::fabs(got - want) <= tol, what, d);
}

// ── An RGGB frame with each Bayer site set independently ─────
static std::vector<uint8_t> MakeFrame(int w, int h,
                                      uint8_t R, uint8_t Gr, uint8_t Gb, uint8_t B) {
    std::vector<uint8_t> f((size_t)w * h, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const bool ey = (y % 2) == 0, ex = (x % 2) == 0;
            f[(size_t)y * w + x] = ey ? (ex ? R : Gr) : (ex ? Gb : B);
        }
    return f;
}

static AnalysisResult Analyse(FrameAnalyzer& a, std::vector<uint8_t>& px,
                              int w, int h) {
    FrameSlot slot{};
    slot.data      = px.data();
    slot.dataSize  = px.size();
    slot.width     = (uint32_t)w;
    slot.height    = (uint32_t)h;
    slot.bitDepth  = 8;
    slot.timestamp = 0;

    // Submit is non-blocking and skips if busy, so retry until taken.
    for (int i = 0; i < 400; ++i) {
        a.Submit(&slot);
        for (int j = 0; j < 100; ++j) {
            if (a.HasResult()) return a.GetResult();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    printf("  !! analyzer never produced a result\n");
    return {};
}

int main() {
    printf("\n=== Sensor analysis: what the averaged histogram hides ===\n");

    FrameAnalyzer a;
    a.Start();
    const int W = 256, H = 256;

    // ── 1. The Gr/Gb split, which histG cannot express ───────
    printf("\n1. The two greens, kept apart\n");
    {
        // The exact case docs/HISTOGRAM-ACCURACY.md measured: Gr=20 and
        // Gb=180 produce a G histogram occupying one bin at 100 --
        // identical to Gr=Gb=100. A 160-count split, completely
        // invisible. The split figure is what makes it visible.
        std::vector<uint8_t> f = MakeFrame(W, H, 100, 20, 180, 100);
        AnalysisResult r = Analyse(a, f, W, H);
        Check(r.valid, "a split-green frame analyses", "");
        CheckNear(r.meanGr, 20.0f, 1.0f, "Gr is reported on its own");
        CheckNear(r.meanGb, 180.0f, 1.0f, "Gb is reported on its own");
        CheckNear(r.meanG, 100.0f, 1.0f, "G is still their mean, as the display shows");
        CheckNear(r.greenSplitPct, -160.0f, 3.0f,
                  "the split is stated as a percentage, signed");
        Check(GreenSplitAdvice(r).find("very high") != std::string::npos,
              "and called out as a fault", GreenSplitAdvice(r));
    }
    {
        std::vector<uint8_t> f = MakeFrame(W, H, 100, 100, 100, 100);
        AnalysisResult r = Analyse(a, f, W, H);
        CheckNear(r.greenSplitPct, 0.0f, 0.5f, "matched greens read zero split");
        Check(GreenSplitAdvice(r) == "normal", "and are called normal",
              GreenSplitAdvice(r));
    }
    {
        // A few percent is every colour sensor. It may be mentioned;
        // it must not be presented as a fault, or the one time it IS a
        // fault nobody will be reading the line any more.
        std::vector<uint8_t> f = MakeFrame(W, H, 100, 102, 98, 100);
        AnalysisResult r = Analyse(a, f, W, H);
        const std::string adv = GreenSplitAdvice(r);
        Check(adv.find("suspect") == std::string::npos &&
              adv.find("fault")   == std::string::npos,
              "a 4% split is not raised as a fault", adv);
    }
    {
        // No green at all: the split is undefined, not infinite.
        std::vector<uint8_t> f = MakeFrame(W, H, 100, 0, 0, 100);
        AnalysisResult r = Analyse(a, f, W, H);
        Check(std::isfinite(r.greenSplitPct) && r.greenSplitPct == 0.0f,
              "a frame with no green reports 0, not a division by zero", "");
    }

    // ── 1b. The peak level the histogram panel reports ───────
    //
    // Phase 92. This is the figure the display now carries, so it is
    // worth checking against frames whose level is known by
    // construction rather than against whatever the code produces.
    printf("\n1b. Per-channel peak level\n");
    {
        std::vector<uint8_t> f = MakeFrame(W, H, 128, 128, 128, 128);
        AnalysisResult r = Analyse(a, f, W, H);
        CheckNear(r.highlightR, 128.0f, 1.0f, "a frame at 128 reports 128");
        CheckNear(r.highlightG, 128.0f, 1.0f, "in green too");
        CheckNear(r.highlightB, 128.0f, 1.0f, "and in blue");
        // Which is what the panel turns into a percentage: 128/255.
        CheckNear(r.highlightR / 255.0f * 100.0f, 50.2f, 0.5f,
                  "and the panel's percentage comes out at half scale");
    }
    {
        // Channels at different levels must be told apart -- reading
        // colour balance off the three figures is most of the point.
        std::vector<uint8_t> f = MakeFrame(W, H, 200, 100, 100, 50);
        AnalysisResult r = Analyse(a, f, W, H);
        CheckNear(r.highlightR, 200.0f, 1.0f, "red at 200 reads 200");
        CheckNear(r.highlightG, 100.0f, 1.0f, "green at 100 reads 100");
        CheckNear(r.highlightB,  50.0f, 1.0f, "blue at 50 reads 50");
    }
    {
        // A clipped channel reads full scale, which is what lets the
        // panel's one number carry clipping as well as level.
        std::vector<uint8_t> f = MakeFrame(W, H, 255, 128, 128, 128);
        AnalysisResult r = Analyse(a, f, W, H);
        CheckNear(r.highlightR, 255.0f, 0.5f, "a clipped channel reads 255");
        Check(r.highlightR / 255.0f * 100.0f >= 99.0f,
              "so the panel shows 100% and colours it as blown", "");
        Check(r.clipR > 0.05f, "and the clipped fraction agrees",
              std::to_string(r.clipR));
    }
    {
        // The headroom the footer quotes must come from the same level
        // the rows show, or the two disagree on screen.
        std::vector<uint8_t> f = MakeFrame(W, H, 64, 64, 64, 64);
        AnalysisResult r = Analyse(a, f, W, H);
        CheckNear(r.highlightR, 64.0f, 1.0f, "level and headroom share a source");
        CheckNear(r.headroomR,   2.0f, 0.05f, "-- 64 is two stops below 255");
    }

    // ── 2. Headroom, in the unit the adjustment is made in ───
    printf("\n2. Headroom to clipping\n");
    {
        // Half scale is exactly one stop below clipping.
        std::vector<uint8_t> f = MakeFrame(W, H, 128, 128, 128, 128);
        AnalysisResult r = Analyse(a, f, W, H);
        CheckNear(r.headroomR, 1.0f, 0.05f, "a frame at half scale has 1 stop left");
        CheckNear(r.headroomStops, 1.0f, 0.05f, "and that is the limiting figure");
    }
    {
        // A quarter scale is two stops.
        std::vector<uint8_t> f = MakeFrame(W, H, 64, 64, 64, 64);
        AnalysisResult r = Analyse(a, f, W, H);
        CheckNear(r.headroomR, 2.0f, 0.05f, "a quarter scale has 2 stops left");
    }
    {
        // Red hot, blue cold: red decides the exposure, and saying so
        // is the point. Raising exposure to suit blue would clip red.
        std::vector<uint8_t> f = MakeFrame(W, H, 240, 128, 128, 32);
        AnalysisResult r = Analyse(a, f, W, H);
        Check(r.limitingChannel == 'R', "the hottest channel is named as limiting",
              std::string(1, r.limitingChannel));
        Check(r.headroomB > r.headroomR,
              "and the cold channel is reported as having more room", "");
    }
    {
        // Clipped reads NEGATIVE, not zero. Zero would be
        // indistinguishable from "exactly at the limit", and those call
        // for opposite actions.
        std::vector<uint8_t> f = MakeFrame(W, H, 255, 255, 255, 255);
        AnalysisResult r = Analyse(a, f, W, H);
        Check(r.headroomStops < 0.0f,
              "a clipped frame reports negative headroom, not zero",
              std::to_string(r.headroomStops));
        Check(ExposureAdvice(r).find("CLIPPING") != std::string::npos &&
              ExposureAdvice(r).find("stops") == std::string::npos,
              "and the advice names the clipped fraction, not invented stops",
              ExposureAdvice(r));
    }

    // ── 3. The advice itself ─────────────────────────────────
    printf("\n3. Turning numbers into something to do\n");
    {
        AnalysisResult r;
        r.valid = true; r.limitingChannel = 'R';

        r.headroomStops = 0.1f;
        Check(ExposureAdvice(r).find("limit") != std::string::npos,
              "0.1 stops left reads as at the limit", ExposureAdvice(r));

        r.headroomStops = 0.5f;
        Check(ExposureAdvice(r).find("good") != std::string::npos,
              "half a stop left reads as good", ExposureAdvice(r));

        r.headroomStops = 2.0f;
        Check(ExposureAdvice(r).find("under by") != std::string::npos,
              "two stops left reads as under", ExposureAdvice(r));

        r.headroomStops = 5.0f;
        Check(ExposureAdvice(r).find("noise floor") != std::string::npos,
              "five stops left reads as far under", ExposureAdvice(r));

        // Nothing should ever be advised off a frame that does not exist.
        AnalysisResult none;
        Check(ExposureAdvice(none) == "no frame" &&
              GreenSplitAdvice(none) == "no frame" &&
              BalanceAdvice(none)    == "no frame",
              "an invalid result advises nothing", "");
    }
    {
        AnalysisResult r;
        r.valid = true;
        r.meanR = 100.0f; r.meanG = 100.0f; r.meanB = 100.0f;
        Check(BalanceAdvice(r).find("check") == std::string::npos,
              "a neutral frame does not nag about white balance", BalanceAdvice(r));
        r.meanR = 200.0f;
        Check(BalanceAdvice(r).find("check") != std::string::npos,
              "a 2:1 red cast does", BalanceAdvice(r));
        r.meanG = 0.0f;
        Check(BalanceAdvice(r) == "no frame",
              "and no green means no ratio, rather than a division by zero", "");
    }

    a.Stop();
    printf("\n");
    printf("=== %s ===\n\n", g_fail == 0
           ? "the sensor numbers say what they claim"
           : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
