// ============================================================
//  Seeing, as a sequence can use it (phase 114)
//
//  The claim worth testing is that the figure follows the sky and not
//  the camera. Section 1 measures that on real emulator frames, because
//  a test built on the formula would only check the formula. What it
//  cannot show is that real frames behave the same: checklist 37.
// ============================================================

#ifndef RADUK_EMULATOR
#define RADUK_EMULATOR
#endif
#include "core/CameraEmulator.h"
#include "core/SeeingMonitor.h"
#include "core/Sharpness.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (ok) printf("  ok    %s\n", what);
    else  { printf("  FAIL  %s\n", what); ++g_fail; }
}

// The app's two inputs, from a real frame: the Laplacian score around the
// disc and the 99.9th percentile. The median of a few frames, because the
// emulator draws its seeing per frame.
static double MeasureNormalised(ASICamera& cam, int W, int H, float* peakOut) {
    std::vector<uint8_t> f((size_t)W * H);
    ASI_ERROR_CODE err;
    for (int i = 0; i < 4; i++)
        cam.GetVideoFrameEx(f.data(), (int)f.size(), 2000, err);
    std::vector<double> vals, peaks;
    for (int i = 0; i < 9; i++) {
        cam.GetVideoFrameEx(f.data(), (int)f.size(), 2000, err);
        std::vector<uint8_t> s(f);
        std::nth_element(s.begin(), s.begin() + (size_t)(s.size() * 0.999), s.end());
        const float hi = (float)s[(size_t)(s.size() * 0.999)];
        const double v = BayerLaplacianVariance(f.data(), W, H, 8, W / 2, H / 2);
        vals.push_back(SeeingMonitor::Normalised(SharpnessPercent(v), hi));
        peaks.push_back(hi);
    }
    std::sort(vals.begin(), vals.end());
    std::sort(peaks.begin(), peaks.end());
    if (peakOut) *peakOut = (float)peaks[peaks.size() / 2];
    return vals[vals.size() / 2];
}

static void TestOnRealFrames() {
    printf("1. the figure on emulator frames\n");
    ASICamera cam;
    Check(cam.Open(0), "emulator opens");
    const int W = 640, H = 480;
    cam.SetROI(W, H, 1, ASI_IMG_RAW8);
    cam.SetGain(60); cam.SetExposure(4.0);
    cam.SetGamma(50);
    // Without a focuser the emulator skips its blur pass, seeing included.
    // 168000 is its true focus.
    cam.SetFocusPositionProvider([](int& p) { p = 168000; return true; });
    cam.StartVideoCapture();

    float hiA = 0, hiB = 0;
    const double base = MeasureNormalised(cam, W, H, &hiA);
    printf("    base %.3f at highlight %.0f\n", base, hiA);
    Check(base > 0.0, "a measurable figure");

    cam.SetExposure(6.0);
    const double exposed = MeasureNormalised(cam, W, H, &hiB);
    printf("    x1.5 exposure %.3f at highlight %.0f\n", exposed, hiB);
    Check(hiB > hiA * 1.2, "more exposure really raised the signal");
    Check(fabs(exposed / base - 1.0) < 0.15, "and the figure stayed within 15%");

    cam.SetExposure(4.0); cam.SetGain(90);
    const double gained = MeasureNormalised(cam, W, H, &hiB);
    printf("    +3 dB gain %.3f at highlight %.0f\n", gained, hiB);
    Check(hiB > hiA * 1.2, "more gain really raised the signal");
    Check(fabs(gained / base - 1.0) < 0.15, "and the figure stayed within 15%");

    cam.SetGain(60);
    cam.SetSeeingBlurArcsec(3.0f);
    const double blurred = MeasureNormalised(cam, W, H, &hiB);
    printf("    3 arcsec seeing %.3f at highlight %.0f\n", blurred, hiB);
    Check(blurred < base * 0.8, "seeing moves it past the 80% bar");
    cam.StopVideoCapture();
}

static SeeingMonitor Fresh() {
    SeeingMonitor m;
    SeeingMonitor::Config c;
    m.Reset(c);
    return m;
}

// 2 Hz of one sharpness at one highlight, ending at t0 + sec.
static double Feed(SeeingMonitor& m, double t0, double sec, float sharp,
                   float hi = 200.0f) {
    double t = t0;
    for (; t < t0 + sec; t += 0.5) m.Push(t, sharp, hi);
    return t;
}

static void TestNormalised() {
    printf("2. the figure itself\n");
    Check(SeeingMonitor::Normalised(10.0f, 200.0f) == 10.0, "at the reference it is the score");
    Check(fabs(SeeingMonitor::Normalised(10.0f, 100.0f) - 40.0) < 1e-9, "half the signal, four times");
    Check(SeeingMonitor::Normalised(10.0f, 19.0f) == 0.0, "under the floor there is no figure");
    Check(SeeingMonitor::Normalised(10.0f, 20.0f) > 0.0, "on the floor there is");
    Check(SeeingMonitor::Normalised(0.0f, 200.0f) == 0.0, "no score, no figure");
}

static void TestWindow() {
    printf("3. the window\n");
    {
        auto m = Fresh();
        for (int i = 0; i < 7; i++) m.Push(i * 0.5, 10.0f, 200.0f);
        Check(m.Current(3.0) == 0.0, "seven samples say nothing");
        Check(!m.Open(3.0), "and do not open it");
        m.Push(3.5, 10.0f, 200.0f);
        Check(m.Current(3.5) == 10.0, "eight do");
    }
    {
        auto m = Fresh();
        double t = Feed(m, 0, 10, 10.0f);
        for (int i = 0; i < 3; i++, t += 0.5) m.Push(t, 90.0f, 200.0f);
        Check(m.Best() < 11.0, "three good frames do not raise the best");
        Check(m.Current(t) < 11.0, "or the figure");
    }
    {
        auto m = Fresh();
        Feed(m, 0, 10, 10.0f);
        Check(m.Current(40.0) == 0.0, "an analyser that stopped says nothing");
        Check(!m.Open(40.0), "and is not good seeing");
    }
    {
        // An even count is the mean of the middle two, so a window that
        // is half bad reads as between, not as whichever sorted last.
        auto m = Fresh();
        for (int i = 0; i < 4; i++) m.Push(i * 0.1, 10.0f, 200.0f);
        for (int i = 4; i < 8; i++) m.Push(i * 0.1, 20.0f, 200.0f);
        Check(fabs(m.Current(1.0) - 15.0) < 1e-9, "an even median");
    }
}

static void TestReference() {
    printf("4. tonight as the reference\n");
    {
        auto m = Fresh();
        double t = Feed(m, 0, 15, 10.0f);
        Check(m.Open(t), "as good as it has been");
        t = Feed(m, t, 15, 9.0f);
        Check(m.Open(t), "90% of it");
        Check(m.Best() == 10.0, "the best holds");
        t = Feed(m, t, 15, 7.0f);
        Check(!m.Open(t), "70% of it does not");
        t = Feed(m, t, 15, 8.0f);
        Check(m.Open(t), "exactly the bar does");
    }
    {
        auto m = Fresh();
        double t = Feed(m, 0, 15, 10.0f, 200.0f);
        t = Feed(m, t, 15, 2.5f, 100.0f);
        Check(m.Open(t), "AE halving the signal is not bad seeing");
        Check(fabs(m.Best() - 10.0) < 1e-9, "and does not move the best");
    }
    {
        auto m = Fresh();
        double t = Feed(m, 0, 15, 10.0f);
        t = Feed(m, t, 15, 20.0f);
        Check(m.Best() == 20.0, "better seeing raises the bar");
        t = Feed(m, t, 15, 10.0f);
        Check(!m.Open(t), "and the old level no longer clears it");
    }
    {
        auto m = Fresh();
        double t = Feed(m, 0, 15, 10.0f);
        SeeingMonitor::Config c;
        m.Reset(c);
        Check(m.Best() == 0.0 && m.Current(t) == 0.0, "a new sequence starts clean");
    }
}

static void TestRunScore() {
    printf("5. a run's score\n");
    auto m = Fresh();
    double t = Feed(m, 0, 15, 50.0f);
    Check(m.EndRun() == 0.0, "no run, no score");
    m.BeginRun();
    t = Feed(m, t, 60, 10.0f);
    t = Feed(m, t, 10, 1.0f);
    Check(m.EndRun() == 10.0, "ten seconds of cloud does not decide three minutes");
    t = Feed(m, t, 10, 90.0f);
    m.BeginRun();
    t = Feed(m, t, 10, 7.0f);
    Check(m.EndRun() == 7.0, "nothing from before or between runs");
}

static void TestRanking() {
    printf("6. the ranking\n");
    std::vector<RunResult> runs = {
        {1, "a.ser", 3000, 8.0}, {2, "b.ser", 0, 0.0},
        {3, "c.ser", 3000, 10.0}, {4, "d.ser", 3000, 5.0},
        {5, "e.ser", 0, 99.0},
    };
    const auto r = RankRuns(runs);
    Check(r[0].ordinal == 3 && r[1].ordinal == 1 && r[2].ordinal == 4, "best first");
    Check(r[3].frames == 0 && r[4].frames == 0, "empty runs last, whatever they read");
    Check(RankRuns({}).empty(), "nothing ranks to nothing");

    const std::string t = FormatRunRanking(r, "all runs done");
    Check(t.find("all runs done") != std::string::npos, "says how it ended");
    Check(t.find("100%") < t.find("c.ser"), "the best is 100%");
    Check(t.find(" 80%") != std::string::npos, "the next is a share of it");
    Check(t.find(" 50%") != std::string::npos, "and the next");
    Check(t.find("c.ser") < t.find("a.ser") && t.find("a.ser") < t.find("d.ser"),
          "in order");
    for (unsigned char c : t) if (c >= 0x80) { Check(false, "ASCII only"); break; }

    std::vector<RunResult> none = {{1, "", 0, 0.0}};
    const std::string n = FormatRunRanking(RankRuns(none), "stopped");
    Check(n.find("(no file)") != std::string::npos, "a run with no file says so");
    Check(n.find('%') == n.rfind('%') || n.find("nan") == std::string::npos,
          "and divides by nothing");
    Check(n.find("nan") == std::string::npos && n.find("inf") == std::string::npos,
          "no best run is no percentage");
}

int main() {
    TestOnRealFrames();
    TestNormalised();
    TestWindow();
    TestReference();
    TestRunScore();
    TestRanking();
    printf(g_fail ? "\n%d FAILED\n" : "\nall passed\n", g_fail);
    return g_fail ? 1 : 0;
}
