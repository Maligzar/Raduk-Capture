// ============================================================
//  Phase 140.1: auto-exposure meters the planet, not the frame
//
//  Histograms are built as FrameAnalyzer builds them (sampled
//  superpixels, luminance 0-255, per channel), for a full-sensor frame
//  of 960 x 540 samples with a small planet on a dark sky:
//
//    1. The fault, reproduced: a Saturn-sized disc covering about 0.07%
//       of the frame and clipped. The whole-frame 99.9th percentile reads
//       the sky; the target meter reads 255 and the clipping.
//    2. A well exposed disc: the highlight is the disc's own level.
//    3. Clipping is a share of the target: a few percent of the disc
//       clipped is a few percent, not 0.01% of the frame.
//    4. Hot pixels alone are not a target, and an empty sky is reported
//       as no target, so AE holds instead of chasing noise.
//    5. A big target (the Moon filling the frame) still meters.
// ============================================================

#include "core/TargetMeter.h"

#include <cstdio>
#include <string>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}
static std::string F(double v, int d = 1) { char b[48]; snprintf(b, sizeof(b), "%.*f", d, v); return b; }

struct Hists {
    std::array<uint32_t, 256> lum{}, r{}, g{}, b{};
    uint32_t count = 0;
    void Add(int lv, uint32_t n, bool clipR = false) {
        lum[lv] += n; g[lv] += n; b[lv] += n;
        r[clipR ? 255 : lv] += n;
        count += n;
    }
};

// The whole-frame 99.9th percentile, as FrameAnalyzer computes highlightDN.
static int FrameHighlight(const Hists& h) {
    const uint32_t want = (uint32_t)(0.001f * (float)h.count);
    uint32_t seen = 0;
    for (int i = 255; i >= 0; --i) { seen += h.lum[i]; if (seen > want) return i; }
    return 0;
}

int main() {
    printf("Target meter (phase 140.1)\n");
    const uint32_t frame = 960 * 540;   // sampled superpixels, full sensor

    printf("\n1. The fault: a small clipped disc on a full frame\n");
    {
        Hists h;
        const uint32_t disc = 360;                     // about 0.07% of the frame
        h.Add(8, frame - disc - 200);                  // sky
        h.Add(30, 200);                                // the limb and halo
        h.Add(200, disc / 2);                          // the disc...
        h.Add(255, disc / 2, true);                    // ...half of it clipped
        const TargetMeter m = MeterTarget(h.lum, h.r, h.g, h.b, h.count);
        const int whole = FrameHighlight(h);
        Check(whole < 100, "the whole-frame percentile reads the sky, not the disc", std::to_string(whole));
        Check(m.found, "the target is found", m.why);
        Check(m.highlightDN == 255, "the target's highlight is 255: clipped", F(m.highlightDN, 0));
        Check(m.clipPct > 40.0f, "about half the target is clipped", F(m.clipPct) + "%");
        Check(m.framePct < 0.1f, "the target is under 0.1% of the frame", F(m.framePct, 3) + "%");
    }

    printf("\n2. A well exposed disc\n");
    {
        Hists h;
        h.Add(10, frame - 2000);
        h.Add(120, 1000);
        h.Add(190, 900);
        h.Add(200, 100);
        const TargetMeter m = MeterTarget(h.lum, h.r, h.g, h.b, h.count);
        Check(m.found && m.highlightDN >= 190 && m.highlightDN <= 200,
              "the highlight is the disc's bright side", F(m.highlightDN, 0));
        Check(m.clipPct == 0.0f, "nothing clipped");
        Check(m.thresholdDN > 10 && m.thresholdDN < 120, "the threshold is halfway from sky to peak",
              F(m.thresholdDN, 0));
    }

    printf("\n3. Clipping as a share of the target\n");
    {
        Hists h;
        h.Add(10, frame - 1000);
        h.Add(210, 970);
        h.Add(255, 30, true);                          // 3% of the disc
        const TargetMeter m = MeterTarget(h.lum, h.r, h.g, h.b, h.count);
        Check(m.found && m.clipPct > 2.5f && m.clipPct < 3.5f, "3% of the target, not 0.006% of the frame",
              F(m.clipPct, 2) + "%");
    }

    printf("\n4. What is not a target\n");
    {
        Hists h;
        h.Add(10, frame - 5);
        h.Add(255, 5, true);                            // five hot pixels
        const TargetMeter m = MeterTarget(h.lum, h.r, h.g, h.b, h.count);
        Check(!m.found, "five hot pixels are not a planet", m.why);
        Hists sky;
        sky.Add(10, frame - 3000);
        sky.Add(14, 3000);                              // noise, a few DN above
        const TargetMeter e = MeterTarget(sky.lum, sky.r, sky.g, sky.b, sky.count);
        Check(!e.found && e.why.find("clear of the sky") != std::string::npos,
              "an empty sky: no target, and why", e.why);
        Hists none;
        Check(!MeterTarget(none.lum, none.r, none.g, none.b, 0).found, "no frame: no target");
    }

    printf("\n5. A big target\n");
    {
        Hists h;
        h.Add(5, frame / 10);                           // a little sky in a corner
        h.Add(150, frame * 8 / 10);
        h.Add(180, frame / 10);
        const TargetMeter m = MeterTarget(h.lum, h.r, h.g, h.b, h.count);
        Check(m.found && m.highlightDN == 180, "the Moon filling the frame meters on itself", F(m.highlightDN, 0));
        // With the Moon filling the frame the median IS the Moon, so the
        // "target" is its brighter part. The highlight is unaffected, and
        // clipping is measured over the bright part, the strict side.
        Check(m.thresholdDN > 150 && m.framePct < 20.0f,
              "the median is the Moon, so its bright part is metered", F(m.framePct, 0) + "%");
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
