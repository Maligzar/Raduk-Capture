// ============================================================
//  Phase 153.0: the application core, headless (src/app/AppCore.h)
//
//  Builds the same AppCore main.cpp builds, on the emulated camera and
//  focuser, with a recording UI sink, and drives it the way the render
//  loop does -- no window. In groups, so a change to one subsystem runs
//  its own group:
//
//    app_headless            every group (CI)
//    app_headless connect    the camera opens and frames reach the analyzer
//    app_headless focus      autofocus, fed through AppCore, finds focus
//    app_headless record     60 frames to SER, the record beside it, no drops
//
//  Real time for now (about a minute a group): the core's clock reaches
//  each subsystem as its controller is extracted (153.1 on), and the run
//  gets faster with it. Tracking (GoTo, lock) joins at 153.5.
// ============================================================

#define RADUK_EMULATOR 1
#include "app/AppCore.h"
// The pipeline's own sources, compiled here so they see the emulator
// build's definitions, as tracking_emulator does. The writers do not
// depend on it and come in through run-tests.ps1's Sources.
#include "core/CaptureThread.cpp"
#include "core/FrameAnalyzer.cpp"
#include "core/CentroidTracker.cpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

using namespace raduk::app;
using namespace raduk::core;
namespace fs = std::filesystem;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    fflush(stdout);
    if (!ok) g_fail++;
}

static constexpr int kW = 640, kH = 480;
static constexpr int kTrueFocus = 168000;

// The camera and the frame pipeline, started as RunApp starts them.
static bool StartPipeline(AppCore& core) {
    if (!core.cam.Open(0)) return false;
    core.cam.SetTargetName("Jupiter");
    core.cam.SetRegion(kW, kH, 1, ImageType::Raw8);
    core.cam.SetExposure(8.0);
    core.cam.SetGain(100);
    if (!core.ring.Allocate(DEFAULT_RING_SIZE, FrameSizeBytes(kW, kH, ImageType::Raw8))) return false;
    if (!core.capture.Start(core.cam, core.ring, 8.0)) return false;
    core.analyzer.Start();
    return true;
}

static void StopPipeline(AppCore& core) {
    core.capture.SetFrameSink(nullptr);
    if (core.capturer.IsRecording()) core.capturer.StopRecording(false);
    core.analyzer.Stop();
    core.capture.Stop();
    if (core.focuser.IsOpen()) core.focuser.Close();
    core.cam.Close();
}

// One pass of the render loop's core half: the newest frame to the
// analyzer and to autofocus's fine-pass feed, a fresh analysis to the
// coarse feed. Returns whether an analysis arrived.
static bool Pump(AppCore& core, AnalysisResult& last) {
    bool fresh = false;
    if (FrameSlot* f = core.ring.GetLatestFrame()) {
        core.analyzer.Submit(f);
        core.FeedAutofocusFrame(f, last);
        core.ring.ReleaseRead(f);
    }
    if (core.analyzer.HasResult()) { last = core.analyzer.GetResult(); fresh = true; }
    core.FeedAutofocusAnalysis(last, fresh);
    return fresh;
}

static void GroupConnect() {
    printf("connect: the camera opens and frames reach the analyzer\n");
    AppCore core; RecordingUiSink ui; core.SetUi(&ui);
    Check(StartPipeline(core), "the emulated camera opens and the capture thread starts");
    AnalysisResult last{};
    int analyses = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(15) && analyses < 5) {
        if (Pump(core, last)) ++analyses;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Check(analyses >= 5, "frames flow through the ring to the analyzer", std::to_string(analyses) + " analyses");
    Check(last.valid && last.centroidValid, "and the analyzer finds the planet",
          last.valid ? "centroid " + std::to_string((int)last.centroidX) + "," + std::to_string((int)last.centroidY) : "no result");
    StopPipeline(core);
}

static void GroupFocus() {
    printf("focus: autofocus, fed through AppCore, finds focus\n");
    AppCore core; RecordingUiSink ui; core.SetUi(&ui);
    Check(StartPipeline(core), "the pipeline starts");
    // The emulator's closed loop, as RunApp wires it: the image blurs with
    // the distance of the mirror from true focus.
    core.cam.SetTrueFocusPosition(kTrueFocus);
    core.cam.SetBlurPxPerStep(ASICamera::DefaultBlurPxPerStep());
    core.cam.SetFocusPositionProvider([&core](int& pos) { pos = core.focuser.GetOpticalPosition(); return true; });
    Check(core.focuser.Open(0), "the emulated focuser opens");

    AutofocusParams p;
    p.settleMs = 50;
    core.autofocus.SetParams(p);
    core.autofocus.SetFocuser(&core.focuser);
    core.autofocus.SetLogger([&ui](const char* s) { ui.Line(s); });
    // Phase 153.1: started, and finished, through the controller main.cpp uses.
    std::atomic<bool> done{false};
    std::string lastStatus;
    std::mutex statusMu;
    AutofocusHooks h;
    h.status = [&](const std::string& s) { std::lock_guard<std::mutex> lk(statusMu); lastStatus = s; };
    core.afCtl.SetHooks(h);
    core.autofocus.SetOnComplete([&](AFResult r) { core.afCtl.OnComplete(r); done.store(true); });

    const int start = kTrueFocus + 700;
    core.focuser.MoveTo(start);
    while (core.focuser.IsMoving()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    core.afCtl.Start();
    Check(core.afCtl.WaitingForAE(), "the controller starts 700 steps from focus, waiting for AE");

    AnalysisResult last{};
    const auto t0 = std::chrono::steady_clock::now();
    while (!done.load() && std::chrono::steady_clock::now() - t0 < std::chrono::minutes(8)) {
        Pump(core, last);
        // The readout's part: AE is not fed here, so the sweep starts at
        // the settle limit, as on a rig whose AE cannot reach its target.
        core.afCtl.Tick(false, last.valid && last.surfaceTracked, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const int end = core.focuser.GetOpticalPosition();
    Check(done.load(), "the sweep finishes",
          std::to_string((int)std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::steady_clock::now() - t0).count()) + " s");
    Check(std::abs(end - kTrueFocus) <= 200, "the mirror ends near true focus",
          std::to_string(end - kTrueFocus) + " steps");
    Check(ui.Saw("[AF]"), "its lines reach the core's UI sink");
    {
        std::lock_guard<std::mutex> lk(statusMu);
        Check(lastStatus.rfind("AF done:", 0) == 0, "and the panel would say it is done", lastStatus);
    }
    StopPipeline(core);
}

static void GroupRecord() {
    printf("record: 60 frames to SER, the record beside it, no drops\n");
    AppCore core; RecordingUiSink ui; core.SetUi(&ui);
    Check(StartPipeline(core), "the pipeline starts");

    const fs::path dir = fs::temp_directory_path() / "raduk_app_headless";
    std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);
    CaptureConfig cfg;
    cfg.outputDir   = dir.string();
    cfg.filePrefix  = "Jupiter";
    cfg.limitType   = LimitType::Frames;
    cfg.limitFrames = 60;
    cfg.format      = CaptureFormat::SER;
    core.capturer.SetConfig(cfg);
    core.capturer.SetLogCallback([&ui](const std::string& s) { ui.Line(s); });
    std::atomic<bool> complete{false};
    std::string path;
    core.capturer.SetOnComplete([&](const std::string& p, bool) { path = p; complete.store(true); });
    core.capture.SetFrameSink([&core](const FrameSlot* s) { core.capturer.SubmitFromCaptureThread(s); });

    Check(core.capturer.StartRecording(kW, kH, 8), "a recording starts");
    AnalysisResult last{};
    const auto t0 = std::chrono::steady_clock::now();
    while (!complete.load() && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(60)) {
        Pump(core, last);   // the loop keeps running beside a recording
        // The capture thread only raises the limit; the render loop stops
        // the run (main.cpp's endRecording), because StopRecording joins
        // the writer. Done here as the loop does it.
        if (core.capturer.IsRecording() && core.capturer.LimitReached())
            core.capturer.StopRecording(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Check(complete.load(), "it stops at its frame limit");
    Check(core.capturer.FramesRecorded() == 60, "60 frames recorded",
          std::to_string(core.capturer.FramesRecorded()));
    std::error_code e2;
    const bool ser = !path.empty() && fs::exists(path, e2) && fs::file_size(path, e2) > 178u + 60u * kW * kH;
    Check(ser, "the SER is on disk with every frame", path);
    const std::string obs = path.empty() ? "" : ObservationPathFor(path);
    Check(!obs.empty() && fs::exists(obs, e2), "the observation record is beside it", obs);
    const DropReport d = core.capturer.LastDropReport();
    Check(d.unexplained == 0, "no frame unexplained by the drop ledger",
          std::to_string(d.unexplained) + " unexplained of " + std::to_string(d.framesWritten));
    StopPipeline(core);
    fs::remove_all(dir, ec);
}

int main(int argc, char** argv) {
    _putenv("RADUK_EMU_NEXSTAR=1");
    const std::string g = argc > 1 ? argv[1] : "all";
    printf("Application core, headless (phase 153.0): %s\n", g.c_str());
    bool ran = false;
    if (g == "all" || g == "connect") { GroupConnect(); ran = true; }
    if (g == "all" || g == "focus")   { GroupFocus();   ran = true; }
    if (g == "all" || g == "record")  { GroupRecord();  ran = true; }
    if (!ran) { printf("unknown group '%s': connect, focus, record or all\n", g.c_str()); return 2; }
    if (g_fail) { printf("app_headless: %d FAILED\n", g_fail); return 1; }
    printf("app_headless: all passed\n");
    return 0;
}
