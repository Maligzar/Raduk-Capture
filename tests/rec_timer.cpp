// ============================================================
//  The recording timer
//
//  The owner's report: "the record indicator does not actually record
//  time it just stays at 0:00."
//
//  ElapsedSec() returned m_status.elapsedSec, a field something else
//  had to remember to keep current, and only one path did. In
//  SubmitFromCaptureThread -- the live path -- the store sat INSIDE
//
//      if (m_config.limitType == LimitType::Time) { ... }
//
//  so a frame-limited or unlimited run never wrote it and the panel's
//  "REC 00:00" never moved.
//
//  Phase 48 met the same field from the other side and wrote it down:
//  "elapsedSec is only kept up to date for time-limited runs; a
//  frame-limited run left it at 0, and the first record written in the
//  app said a 3.5 minute capture took 0.00 s." It measured from
//  m_startTime by hand for the observation record and left the field
//  broken, so the display kept the bug for another fifty phases.
//
//  It is derived from the start instant now. A clock that is computed
//  cannot go stale.
// ============================================================

#include "core/CaptureManager.h"
#include "core/FrameSlot.h"

#include <cstdio>
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include <cstdlib>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static const int W = 320, H = 240;

struct Fixture {
    std::vector<uint8_t> pixels;
    FrameSlot slot{};
    Fixture() : pixels((size_t)W * H, 128) {
        slot.data      = pixels.data();
        slot.dataSize  = pixels.size();
        slot.width     = W;
        slot.height    = H;
        slot.bitDepth  = 8;
        slot.timestamp = 1;
    }
};

static std::string TempDir() {
    const char* t = getenv("TEMP");
    return std::string(t ? t : ".") + "\\raduk_rectimer_test";
}

static void Sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int main() {
    printf("\n=== The recording timer ===\n\n");

    Fixture fx;
    const std::string dir = TempDir();
    CreateDirectoryA(dir.c_str(), nullptr);

    // ── The reported case ──
    //
    // A FRAME-limited run. This is the configuration that showed
    // 00:00 for the whole capture, and it is the default for planetary
    // work, so it is the one almost every real run used.
    printf("1. A frame-limited run (the case that read 00:00)\n");
    {
        CaptureManager cm;
        CaptureConfig cfg;
        cfg.outputDir   = dir;
        cfg.filePrefix  = "rectimer_frames";
        cfg.limitType   = LimitType::Frames;
        cfg.limitFrames = 100000;        // high, so the run does not end
        cm.SetConfig(cfg);

        Check(cm.ElapsedSec() == 0.0, "zero before the run starts");
        Check(cm.StartRecording(W, H, 8), "recording starts");

        Sleep_ms(250);
        const double a = cm.ElapsedSec();
        printf("       after ~250 ms: %.3f s\n", a);
        Check(a > 0.10, "the clock has moved", "this is the bug");
        Check(a < 5.0,  "and is not absurd");

        Sleep_ms(250);
        const double b = cm.ElapsedSec();
        printf("       after ~500 ms: %.3f s\n", b);
        Check(b > a, "it keeps moving");

        // No frames have been submitted at all, which is the sharper
        // form of the same defect: a clock driven by frame arrivals
        // stops whenever frames stop, and recording is not the same
        // thing as frames arriving.
        Check(cm.FramesRecorded() == 0, "and did so with no frames submitted");

        cm.StopRecording();
        const double fin = cm.ElapsedSec();
        printf("       after stopping: %.3f s\n", fin);
        Check(fin >= b, "the finished run reports at least what it last read");
        Check(fin < 30.0, "and a sane total");
    }

    // ── Still right for the one case that used to work ──
    printf("\n2. A time-limited run still reports correctly\n");
    {
        CaptureManager cm;
        CaptureConfig cfg;
        cfg.outputDir    = dir;
        cfg.filePrefix   = "rectimer_time";
        cfg.limitType    = LimitType::Time;
        cfg.limitTimeSec = 600.0;
        cm.SetConfig(cfg);

        cm.StartRecording(W, H, 8);
        Sleep_ms(250);
        const double a = cm.ElapsedSec();
        printf("       after ~250 ms: %.3f s\n", a);
        Check(a > 0.10, "the clock moves on a time limit too");
        cm.StopRecording();
    }

    // ── It does not run when nothing is recording ──
    printf("\n3. It does not run outside a recording\n");
    {
        CaptureManager cm;
        CaptureConfig cfg;
        cfg.outputDir  = dir;
        cfg.filePrefix = "rectimer_idle";
        cfg.limitType  = LimitType::Frames;
        cfg.limitFrames = 10;
        cm.SetConfig(cfg);

        Check(cm.ElapsedSec() == 0.0, "zero before anything has run");
        Sleep_ms(120);
        Check(cm.ElapsedSec() == 0.0, "still zero after waiting");

        cm.StartRecording(W, H, 8);
        Sleep_ms(150);
        cm.StopRecording();
        const double fin = cm.ElapsedSec();
        Check(fin > 0.05, "a finished run keeps its duration", "");

        // And that figure must not drift once the run is over: the
        // panel shows it after the fact, and a number that kept
        // climbing would say a stopped capture was still going.
        Sleep_ms(250);
        const double later = cm.ElapsedSec();
        printf("       %.3f s at stop, %.3f s a quarter second later\n",
               fin, later);
        Check(later == fin, "and it is frozen, not still counting");
    }

    // ── A second run does not inherit the first ──
    printf("\n4. A second run starts from zero\n");
    {
        CaptureManager cm;
        CaptureConfig cfg;
        cfg.outputDir   = dir;
        cfg.filePrefix  = "rectimer_twice";
        cfg.limitType   = LimitType::Frames;
        cfg.limitFrames = 100000;
        cm.SetConfig(cfg);

        cm.StartRecording(W, H, 8);
        Sleep_ms(300);
        cm.StopRecording();
        const double first = cm.ElapsedSec();

        cm.StartRecording(W, H, 8);
        Sleep_ms(80);
        const double second = cm.ElapsedSec();
        printf("       first run %.3f s, second run at ~80 ms: %.3f s\n",
               first, second);
        Check(second < first, "the second run is not carrying the first's time");
        Check(second > 0.0,   "but is running");
        cm.StopRecording();
    }

    printf("\n%s\n", g_fail ? "=== FAILED ===" : "=== PASS ===");
    return g_fail ? 1 : 0;
}
