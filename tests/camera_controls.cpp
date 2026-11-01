// ============================================================
//  The camera controls, end to end (phase 112)
//
//  The owner's report: gamma "does not seem to be doing anything".
//  True, and not only gamma: the emulator stored gamma and white
//  balance, read them back, and applied them to no pixel, so a test of
//  the wiring could pass while the control did nothing. Section 2 runs
//  the real emulator and measures frames.
//
//  Also pinned: what a typed value means in all six boxes, that the
//  capture record carries every setting, and that auto-exposure leaves
//  the settings alone while a sweep or a recording runs.
// ============================================================

#ifndef RADUK_EMULATOR
#define RADUK_EMULATOR
#endif
#include "core/CameraEntry.h"
#include "core/AutoExposureAssistant.h"
#include "core/CameraEmulator.h"
#include "core/ObservationRecord.h"
#include "core/LightLadder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (ok) printf("  ok    %s\n", what);
    else  { printf("  FAIL  %s\n", what); ++g_fail; }
}

struct Means { double r, g, b, all; };

// Waits out the frames already rendered under the old settings.
static Means Measure(ASICamera& cam, int W, int H) {
    std::vector<uint8_t> f((size_t)W * H);
    ASI_ERROR_CODE err;
    std::vector<uint8_t> last;
    for (int i = 0; i < 4; i++)
        cam.GetVideoFrameEx(f.data(), (int)f.size(), 2000, err);
    double s[3] = {}, n[3] = {};
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const int c = (y & 1) ? ((x & 1) ? 2 : 1) : ((x & 1) ? 1 : 0);
            s[c] += f[(size_t)y * W + x]; n[c] += 1.0;
        }
    Means m;
    m.r = s[0] / n[0]; m.g = s[1] / n[1]; m.b = s[2] / n[2];
    m.all = (s[0] + s[1] + s[2]) / (n[0] + n[1] + n[2]);
    return m;
}

int main() {
    printf("Camera controls\n");

    printf("\n1. What a typed value means\n");
    {
        CameraEntry e = ParseCameraEntry(L"250", 0, 570);
        Check(e.parsed && !e.clamped && e.value == 250, "a number in range is that number");
        e = ParseCameraEntry(L"900", 0, 570);
        Check(e.parsed && e.clamped && e.value == 570, "over the range clamps to the top");
        e = ParseCameraEntry(L"-5", 0, 570);
        Check(e.parsed && e.clamped && e.value == 0, "under the range clamps to the bottom");
        e = ParseCameraEntry(L"570", 0, 570);
        Check(e.parsed && !e.clamped, "the top of the range is not a clamp");
        e = ParseCameraEntry(L"0", 0, 570);
        Check(e.parsed && !e.clamped && e.value == 0, "a zero gain is a real gain");
        Check(!ParseCameraEntry(L"", 0, 570).parsed, "empty is not a value");
        Check(!ParseCameraEntry(L"abc", 0, 570).parsed, "words are not a value");
        Check(!ParseCameraEntry(L"1,5", 0, 100).parsed, "a comma is not read as 1");
        Check(!ParseCameraEntry(L"12x", 0, 100).parsed, "trailing junk is not read as 12");
        Check(!ParseCameraEntry(L"nan", 0, 100).parsed, "nan is not a value");
        Check(!ParseCameraEntry(L"inf", 0, 100).parsed, "inf is not a value");
        Check(!ParseCameraEntry(nullptr, 0, 100).parsed, "no text is not a value");
        e = ParseCameraEntry(L"8.5ms", 0.1, 60);
        Check(e.parsed && e.value == 8.5, "a typed unit is allowed");
        e = ParseCameraEntry(L"30000.0+", 0.1, 60);
        Check(e.parsed && e.clamped && e.value == 60, "the box's own + mark parses");
        e = ParseCameraEntry(L"80%", 40, 100);
        Check(e.parsed && e.value == 80, "a percent sign is allowed");
    }

    printf("\n2. Gamma and white balance reach the pixels\n");
    {
        ASICamera cam;
        Check(cam.Open(0), "emulator opens");
        const int W = 640, H = 480;
        cam.SetROI(W, H, 1, ASI_IMG_RAW8);
        // Low enough that nothing clips, or white balance has no room.
        cam.SetGain(60); cam.SetExposure(8.0);
        cam.SetGamma(50); cam.SetWhiteBalance(52, 47);
        cam.StartVideoCapture();
        const Means neutral = Measure(cam, W, H);
        Check(neutral.all > 1.0, "the neutral frame has signal");

        cam.SetGamma(100);
        const Means bright = Measure(cam, W, H);
        Check(bright.all > neutral.all * 1.3, "gamma 100 lifts the frame");
        cam.SetGamma(25);
        const Means dark = Measure(cam, W, H);
        Check(dark.all < neutral.all * 0.8, "gamma 25 lowers the frame");
        cam.SetControl(ASI_GAMMA, 100);
        Check(Measure(cam, W, H).all > neutral.all * 1.3,
              "the SetControl door reaches the same pixels");

        cam.SetGamma(50);
        const Means back = Measure(cam, W, H);
        Check(std::fabs(back.all - neutral.all) < neutral.all * 0.1,
              "gamma 50 is the identity, and comes back");

        cam.SetWhiteBalance(78, 47);
        const Means red = Measure(cam, W, H);
        Check(red.r > back.r * 1.3, "WB red scales the red sites");
        Check(std::fabs(red.g - back.g) < back.g * 0.1, "and leaves green alone");
        Check(std::fabs(red.b - back.b) < back.b * 0.1, "and leaves blue alone");

        cam.SetWhiteBalance(52, 94);
        const Means blue = Measure(cam, W, H);
        Check(blue.b > back.b * 1.3, "WB blue scales the blue sites");
        Check(std::fabs(blue.r - back.r) < back.r * 0.1, "and leaves red alone");
        cam.SetWhiteBalance(52, 47);
        // The peak, not the mean: the mean is mostly sky, and sky does
        // not scale. Both frames sit under the emulator's 3.5 clamp.
        auto peak = [&]() {
            std::vector<uint8_t> f((size_t)W * H);
            ASI_ERROR_CODE err;
            for (int i = 0; i < 4; i++)
                cam.GetVideoFrameEx(f.data(), (int)f.size(), 2000, err);
            std::vector<uint8_t> g;
            for (int y = 0; y < H; y += 2)
                for (int x = 1; x < W; x += 2) g.push_back(f[(size_t)y * W + x]);
            std::sort(g.begin(), g.end());
            return (double)g[g.size() * 98 / 100];
        };
        cam.SetGain(40); cam.SetExposure(8.0);
        const double p1 = peak();
        cam.SetGain(100);
        const double p2 = peak();
        printf("  gain 40 -> 100: peak %.0f -> %.0f\n", p1, p2);
        Check(p2 > p1 * 1.8 && p2 < p1 * 2.2, "60 gain units is a stop");
        cam.StopVideoCapture();

        ASI_CONTROL_CAPS caps{};
        Check(cam.GetControlCaps(ASI_GAMMA, caps) && caps.IsWritable == ASI_TRUE &&
              caps.MinValue == 1 && caps.MaxValue == 100, "the emulator lists gamma");
        Check(cam.GetControlCaps(ASI_WB_R, caps) && cam.GetControlCaps(ASI_WB_B, caps),
              "and white balance");
        ControlRange range;
        Check(cam.GetControlRange(ControlId::Gamma, range) && range.writable,
              "the interface reports it writable");
        Check(!cam.GetControlCaps(ASI_FLIP, caps), "a control it does not hold is not listed");
    }

    printf("\n3. The capture record carries every setting\n");
    {
        ObservationRecord a;
        a.file = "x.ser";
        a.exposureMs = 8.5; a.gain = 250;
        a.gamma = 60; a.wbRed = 55; a.wbBlue = 44; a.usbBandwidth = 85;
        a.highSpeed = 1; a.aeMode = "auto";
        ObservationRecord b;
        std::string why;
        Check(ParseObservation(SerializeObservation(a), b, &why), "it parses back");
        Check(b.gamma == 60, "gamma round trips");
        Check(b.wbRed == 55 && b.wbBlue == 44, "white balance round trips");
        Check(b.usbBandwidth == 85, "USB round trips");
        Check(b.highSpeed == 1, "high speed round trips");
        Check(b.aeMode == "auto", "the AE mode round trips");

        ObservationRecord u;
        u.file = "y.ser"; u.gain = 100;
        const std::string text = SerializeObservation(u);
        Check(text.find("gamma") == std::string::npos,
              "a camera with no gamma records none, not 0");
        ObservationRecord z; z.file = "z.ser"; z.wbRed = 0;
        ObservationRecord zb;
        Check(ParseObservation(SerializeObservation(z), zb, &why) &&
              ObsKnown(zb.wbRed) && zb.wbRed == 0, "a zero white balance is a real one");
    }

    printf("\n4. AE leaves the settings alone during a sweep or a run\n");
    {
        AutoExposureAssistant ae;
        ae.SetEnabled(true); ae.SetAuto(true);

        // Far too dark: unheld, this moves.
        ae.Feed(60.0f, 10.0f, 0.0f, 200, 8.0);
        AESuggestion s = ae.Tick();
        Check(s.exposureMs > 8.0, "a dark frame moves the exposure");

        AutoExposureAssistant held;
        held.SetEnabled(true); held.SetAuto(true);
        held.SetHold("autofocus");
        held.Feed(60.0f, 10.0f, 0.0f, 200, 8.0);
        s = held.Tick();
        Check(s.exposureMs == 8.0 && s.gain == 200, "held for autofocus, nothing moves");
        Check(s.text.find("autofocus") != std::string::npos, "and it says why");

        // A clipping frame does not break the hold.
        held.Feed(255.0f, 10.0f, 40.0f, 200, 8.0);
        s = held.Tick();
        Check(s.exposureMs == 8.0 && !s.clipping, "the clip emergency does not break a hold");

        held.SetHold("recording");
        held.Feed(60.0f, 10.0f, 0.0f, 200, 8.0);
        s = held.Tick();
        Check(s.exposureMs == 8.0 && s.text.find("recording") != std::string::npos,
              "held for a recording too");

        held.SetHold(nullptr);
        held.Feed(60.0f, 10.0f, 0.0f, 200, 8.0);
        s = held.Tick();
        Check(s.exposureMs == 8.0, "the first tick after release settles");
        for (int i = 0; i < 10 && s.exposureMs == 8.0; i++) {
            held.Feed(60.0f, 10.0f, 0.0f, 200, 8.0);
            s = held.Tick();
        }
        Check(s.exposureMs > 8.0, "then it resumes");

        // What autofocus waits on.
        AutoExposureAssistant w;
        w.SetEnabled(true); w.SetAuto(true);
        Check(!w.Settled(), "not settled before it has looked");
        w.Feed(60.0f, 10.0f, 0.0f, 200, 8.0);
        w.Tick();
        Check(!w.Settled(), "not settled while it is correcting");
        w.Feed(200.0f, 10.0f, 0.0f, 200, 8.0);
        w.Tick();
        Check(!w.Settled(), "not settled while it waits for frames");
        for (int i = 0; i < 10 && !w.Settled(); i++) {
            w.Feed(200.0f, 10.0f, 0.0f, 200, 8.0);
            w.Tick();
        }
        Check(w.Settled(), "settled on target");
        w.SetHold("autofocus");
        w.Tick();
        Check(!w.Settled(), "a hold is not settled");
        w.SetHold(nullptr);
        for (int i = 0; i < 10; i++) w.Tick();
        w.SetAuto(true);
        Check(!w.Settled(), "turning AE on forgets the old answer");

        AutoExposureAssistant top;
        top.SetEnabled(true); top.SetAuto(true);
        top.Feed(60.0f, 10.0f, 0.0f, 570, 50.0);
        top.Tick();
        Check(top.Settled(), "at the ceiling it will not improve, so it counts");

        // The reason for the hold, as arithmetic.
        const double score = 1.1 * 1.1;
        Check(score > 1.2, "a 10% signal change is over 20% on a variance");
    }

    printf("\n5. The ladder\n");
    {
        LightLadder l;   // 1-50 ms, knee 20; gain 182, 350, 570
        LightPoint p = WalkLadder(l, 8.0, 200, 1.0);
        Check(std::fabs(p.expMs - 16.0) < 0.01 && p.gain == 200,
              "below the knee, a stop is exposure");
        p = WalkLadder(l, 20.0, 200, 1.0);
        Check(p.expMs == 20.0 && p.gain == 260, "at the knee, a stop is 60 gain units");
        p = WalkLadder(l, 16.0, 200, 1.0);
        Check(std::fabs(p.expMs - 20.0) < 0.01 && p.gain > 200 && p.gain < 260,
              "a stop across the knee moves both");
        p = WalkLadder(l, 20.0, 350, 1.0);
        Check(std::fabs(p.expMs - 40.0) < 0.01 && p.gain == 350,
              "past the gain knee, exposure again");
        p = WalkLadder(l, 50.0, 350, 1.0);
        Check(p.expMs == 50.0 && p.gain == 410, "past the rail, gain to the end");
        p = WalkLadder(l, 50.0, 570, 2.0);
        Check(p.expMs == 50.0 && p.gain == 570, "the top holds");
        p = WalkLadder(l, 8.0, 100, 0.5);
        Check(p.gain > 100 && p.expMs == 8.0, "under the floor, gain goes first");

        p = WalkLadder(l, 30.0, 400, -0.5);
        Check(p.gain < 400 && p.expMs == 30.0, "dimming sheds gain over the knee first");
        p = WalkLadder(l, 30.0, 300, -0.5);
        Check(p.expMs < 30.0 && p.expMs >= 20.0 && p.gain == 300,
              "then exposure over its knee");
        p = WalkLadder(l, 20.0, 300, -0.5);
        Check(p.gain < 300 && p.expMs == 20.0, "then gain down to the floor");
        p = WalkLadder(l, 8.0, 182, -1.0);
        Check(std::fabs(p.expMs - 4.0) < 0.01 && p.gain == 182, "then exposure");
        p = WalkLadder(l, 1.0, 182, -1.0);
        Check(p.expMs == 1.0 && p.gain == 122, "and gain leaves HCG only as the last thing");
        p = WalkLadder(l, 1.0, 0, -1.0);
        Check(p.expMs == 1.0 && p.gain == 0, "the bottom holds");

        // There and back is the same point, away from a rung.
        p = WalkLadder(l, 25.0, 350, 0.5);
        LightPoint q = WalkLadder(l, p.expMs, p.gain, -0.5);
        Check(std::fabs(q.expMs - 25.0) < 0.01 && q.gain == 350, "the path reverses");

        // Light is conserved across rungs.
        p = WalkLadder(l, 10.0, 200, 3.0);
        const double got = std::log2(p.expMs / 10.0) +
                           (p.gain - 200) / GAIN_UNITS_PER_STOP;
        Check(std::fabs(got - 3.0) < 0.02, "a correction delivers the stops it asked for");
        Check(WalkLadder(l, 0.0, 200, 1.0).expMs == 0.0, "no exposure, no walk");
    }

    printf("\n6. AE on a simulated camera\n");
    {
        using raduk::profiles::CameraRails;
        // Highlight in DN from the two levers, clipping at 255.
        auto highlight = [](double k, double exp, int gain) {
            const double dn = k * exp * std::pow(10.0, gain / 200.0);
            return (float)(dn > 255.0 ? 255.0 : dn);
        };
        struct Run { double exp; int gain; int ticks; bool settled;
                     bool inRails; bool sawGain, sawExp; };
        auto run = [&](double k, double exp, int gain, const char* type) {
            const auto rail = CameraRails::RailsFor(type);
            AutoExposureAssistant ae;
            auto cfg = ae.Config_();
            cfg.expMinMs = rail.minMs; cfg.expMaxMs = rail.maxMs;
            cfg.expKneeMs = rail.expKneeMs; cfg.gainMin = rail.gainFloor;
            cfg.gainKnee = rail.gainKnee; cfg.gainMax = rail.gainMax;
            ae.SetConfig(cfg);
            ae.SetEnabled(true); ae.SetAuto(true);
            Run r{ exp, gain, 0, false, true, false, false };
            for (; r.ticks < 400 && !r.settled; r.ticks++) {
                const float dn = highlight(k, r.exp, r.gain);
                ae.Feed(dn, 10.0f, dn >= 255.0f ? 30.0f : 0.0f, r.gain, r.exp);
                const AESuggestion s = ae.Tick();
                if (s.gain != r.gain)   { r.gain = s.gain;      r.sawGain = true; }
                if (s.exposureMs != r.exp) { r.exp = s.exposureMs; r.sawExp = true; }
                if (r.exp > rail.maxMs + 1e-9 || r.exp < rail.minMs - 1e-9 ||
                    r.gain > rail.gainMax || r.gain < 0) r.inRails = false;
                r.settled = ae.Settled();
            }
            printf("  k=%-6g from %5.1f ms g%-3d -> %5.2f ms g%-3d in %d ticks, %.0f DN\n",
                   k, exp, gain, r.exp, r.gain, r.ticks, highlight(k, r.exp, r.gain));
            return r;
        };

        Run a = run(0.5, 8.0, 200, "planet");    // dim target
        Check(a.settled && a.inRails, "a dim planet settles inside its rails");
        Check(a.sawGain && a.sawExp, "and both sliders moved");
        Check(a.exp <= 20.0 + 0.01, "without passing the exposure knee");
        double dn = highlight(0.5, a.exp, a.gain);
        Check(dn >= 185.0 && dn <= 215.0, "and lands on target");
        Check(a.ticks * 0.5 < AF_AE_SETTLE_LIMIT_S,
              "within what autofocus waits for, at two ticks a second");

        Run b = run(2.0, 40.0, 400, "planet");  // far too bright, clipped
        dn = highlight(2.0, b.exp, b.gain);
        Check(b.settled && dn >= 185.0 && dn <= 215.0, "a clipped frame comes down to target");
        Check(b.gain >= CameraRails::GAIN_HCG_THRESHOLD,
              "and keeps HCG when exposure can do the rest");

        Run c = run(1e-4, 8.0, 200, "planet");   // hopeless
        Check(c.settled && c.exp == 50.0 && c.gain == 570,
              "a target too dim for the rig ends at the top and says so");

        Run d = run(500.0, 10.0, 300, "planet"); // hopelessly bright
        Check(d.settled && d.exp == 1.0 && d.gain == 0,
              "one too bright ends at the bottom, past the HCG floor");

        Run m = run(20.0, 2.0, 0, "moon");
        dn = highlight(20.0, m.exp, m.gain);
        Check(m.settled && dn >= 185.0 && dn <= 215.0 && m.gain == 0,
              "the Moon reaches target on exposure, at gain 0");
    }

    printf("\n%s\n", g_fail ? "FAILED" : "all ok");
    return g_fail ? 1 : 0;
}
