// ============================================================
//  The recording path now runs on the capture thread
//
//  It used to run on the render loop, which took ONE frame per pass via
//  GetLatestFrame(). Window::Run() ends in Sleep(1), measured at 15.98
//  ms on this machine at default Windows timer granularity, so the loop
//  capped near 62 passes a second before any rendering work -- and
//  every frame the camera produced in between was overwritten in the
//  ring and lost without trace. On a camera that does well over 100 fps
//  at this ROI that is most of every capture.
//
//  Moving it onto the capture thread introduces one hazard worth a test
//  of its own: StopRecording() joins the writer thread, and a buffer
//  sized to a whole run can take seconds to drain. If the capture
//  thread ever called it, frame acquisition would stall for that whole
//  time and the camera's own buffer would overflow. So the limit must
//  only ever be FLAGGED here.
// ============================================================

#include "core/CaptureManager.h"
#include "core/FrameSlot.h"

#include <cstdio>
#include <chrono>
#include <string>
#include <vector>
#include <cstdlib>

using namespace raduk::core;

static int g_fail = 0;

static void Check(bool ok, const std::string& what, const std::string& detail) {
    printf("  [%s] %-60s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
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
    return std::string(t ? t : ".") + "\\raduk_submit_test";
}

int main() {
    printf("\n=== Recording from the capture thread ===\n\n");

    Fixture fx;
    const std::string dir = TempDir();
    CreateDirectoryA(dir.c_str(), nullptr);

    // ── The hazard: a limit must not stop the recording here ──
    printf("1. Hitting the limit flags, and does not stop\n");
    {
        CaptureManager cm;
        CaptureConfig cfg;
        cfg.outputDir        = dir;
        cfg.filePrefix       = "submit_test";
        cfg.limitType        = LimitType::Frames;
        cfg.limitFrames      = 5;
        cm.SetConfig(cfg);

        Check(cm.StartRecording(W, H, 8), "recording starts", "");
        Check(!cm.LimitReached(), "no limit at the start", "");

        // Push past the limit from "the capture thread".
        for (int i = 0; i < 40; i++) {
            fx.slot.timestamp = (uint64_t)(i + 1);
            cm.SubmitFromCaptureThread(&fx.slot);
        }

        Check(cm.LimitReached(), "the limit is flagged once reached", "");
        Check(cm.IsRecording(),
              "and recording is STILL OPEN -- stopping is the render "
              "loop's job", "");

        // The render loop's half of the contract.
        cm.StopRecording();
        Check(!cm.IsRecording(), "which closes it when it gets there", "");
    }

    // ── Phase 108: the derotation stop, and the rename ───────
    printf("\n1b. The derotation window stops a frame-limited run\n");
    {
        CaptureManager cm;
        CaptureConfig cfg;
        cfg.outputDir         = dir;
        cfg.filePrefix        = "submit_derot";
        cfg.limitType         = LimitType::Frames;
        cfg.limitFrames       = 1000000;
        cfg.derotationStopSec = 0.02;
        cfg.winJuposNames     = true;
        cm.SetConfig(cfg);

        Check(cm.StartRecording(W, H, 8), "recording starts", "");
        Check(cm.SubmitFromCaptureThread(&fx.slot),
              "a frame inside the window is taken", "");
        Check(!cm.DerotationStopped(), "nothing has stopped it yet", "");
        Sleep(40);
        Check(!cm.SubmitFromCaptureThread(&fx.slot),
              "a frame past the window is not", "");
        Check(cm.LimitReached() && cm.DerotationStopped(),
              "and it says the window did it", "");
        cm.StopRecording();

        const std::string last = cm.LastFilePath();
        const std::string name = last.substr(last.find_last_of('\\') + 1);
        Check(name.size() > 18 && name[4] == '-' && name[15] == '_' &&
              name.find("submit_derot") == 18,
              "the finished file has the WinJUPOS stamp first", name);
        Check(cm.LastObservationPath().find(name.substr(0, 17)) != std::string::npos,
              "and the observation record followed it", "");
        FILE* f = nullptr;
        Check(fopen_s(&f, last.c_str(), "rb") == 0, "the file is there", last);
        if (f) fclose(f);
    }
    {
        CaptureManager cm;
        CaptureConfig cfg;
        cfg.outputDir     = dir;
        cfg.filePrefix    = "submit_dark";
        cfg.frameType     = "Dark";
        cfg.winJuposNames = true;
        cm.SetConfig(cfg);
        cm.StartRecording(W, H, 8);
        cm.StopRecording();
        const std::string last = cm.LastFilePath();
        Check(last.substr(last.find_last_of('\\') + 1).rfind("submit_dark", 0) == 0,
              "a dark keeps its plain name", last);
    }

    // ── A gated frame is skipped, not dropped ────────────────
    printf("\n2. The gate skips; it does not drop\n");
    {
        CaptureManager cm;
        CaptureConfig cfg;
        cfg.outputDir   = dir;
        cfg.filePrefix  = "submit_gate";
        cfg.limitType   = LimitType::Frames;
        cfg.limitFrames = 100000;
        cm.SetConfig(cfg);
        cm.StartRecording(W, H, 8);

        cm.SetGateOpen(false);
        int accepted = 0;
        for (int i = 0; i < 20; i++) {
            fx.slot.timestamp = (uint64_t)(i + 1);
            if (cm.SubmitFromCaptureThread(&fx.slot)) accepted++;
        }
        Check(accepted == 0, "a closed gate accepts nothing",
              std::to_string(accepted) + " accepted");
        char d[96];
        snprintf(d, sizeof(d), "%u dropped", cm.FramesDropped());
        Check(cm.FramesDropped() == 0,
              "20 gated frames count as 0 drops", d);

        cm.SetGateOpen(true);
        fx.slot.timestamp = 999;
        Check(cm.SubmitFromCaptureThread(&fx.slot),
              "and opening the gate lets frames through again", "");
        cm.StopRecording();
    }

    // ── Not recording, or paused ─────────────────────────────
    printf("\n3. Refusing when it should\n");
    {
        CaptureManager cm;
        Check(!cm.SubmitFromCaptureThread(&fx.slot),
              "submitting while not recording does nothing", "");
        Check(!cm.SubmitFromCaptureThread(nullptr),
              "a null slot does nothing", "");
    }

    // ── Throughput: it must not be the bottleneck ────────────
    printf("\n4. The path itself is not a bottleneck\n");
    {
        CaptureManager cm;
        CaptureConfig cfg;
        cfg.outputDir   = dir;
        cfg.filePrefix  = "submit_rate";
        cfg.limitType   = LimitType::Frames;
        cfg.limitFrames = 100000;
        cm.SetConfig(cfg);
        cm.StartRecording(W, H, 8);
        cm.SetGateOpen(true);

        const int N = 2000;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < N; i++) {
            fx.slot.timestamp = (uint64_t)(i + 1);
            cm.SubmitFromCaptureThread(&fx.slot);
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double us =
            std::chrono::duration<double, std::micro>(t1 - t0).count() / N;

        char d[128];
        snprintf(d, sizeof(d), "%.1f us/frame -> %.0f fps ceiling", us, 1e6 / us);
        // The old path's ceiling was the render loop's ~62 passes/sec.
        // Anything in this league is no longer the limiting factor.
        Check(1e6 / us > 1000.0,
              "submission sustains far more than the old 62 fps ceiling", d);
        cm.StopRecording();
    }

    printf("\n");
    if (g_fail == 0)
        printf("=== recording no longer rides the render loop ===\n\n");
    else
        printf("=== %d CHECK(S) FAILED ===\n\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
