// ============================================================
//  Phase 135.1: a failed write ends the run, and the disk is watched
//
//  Before this, a failed WriteFrame ended the writer thread and left the
//  run "recording": the capture thread kept filling a RAM buffer nobody
//  drained, the panel showed a live capture, and every later frame of
//  the night was lost without a word on screen.
//
//    1. A writer that fails on frame 5: the run is flagged as stopped by
//       a failed write, nothing more is accepted, the frames that could
//       not be written are counted as dropped, and the next run starts
//       clean (no frame of this run lands in it).
//    2. A disk that runs low: flagged "nearly full" within about a
//       second, nothing more accepted, and every frame already accepted
//       still reaches the file.
//    3. DiskFloorBytes at its edges.
// ============================================================

#include "core/CaptureManager.h"
#include "core/DiskGuard.h"
#include "core/FrameSlot.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    fflush(stdout);
    if (!ok) g_fail++;
}

static const int W = 320, H = 240;

// Writes nothing to disk; fails from frame `failAt` (1-based) on.
class StubWriter : public ICaptureWriter {
public:
    int failAt = 0;                       // 0: never
    std::atomic<int> written{0}, calls{0};
    std::vector<uint64_t> stamps;
    bool Open(const std::string& p, int, int, int, bool) override {
        m_path = p; written = 0; calls = 0; stamps.clear(); return true;
    }
    bool WriteFrame(const uint8_t*, size_t, uint64_t ts, float) override {
        const int n = ++calls;
        if (failAt > 0 && n >= failAt) return false;
        stamps.push_back(ts);
        ++written;
        Sleep(2);                          // a disk that takes a moment
        return true;
    }
    bool Close() override { return true; }
    const std::string& Path() const override { return m_path; }
    void NoteSkyConditions(double, double, double, double, double) override {}
    SkySummary SkyConditionsSummary() const override { return {}; }
private:
    std::string m_path;
};

static std::string TempDir() {
    const char* t = getenv("TEMP");
    return std::string(t ? t : ".") + "\\raduk_write_fail_test";
}

// Waits up to ms for cond.
template <class F> static bool WaitFor(F cond, int ms) {
    for (int i = 0; i < ms / 10; ++i) { if (cond()) return true; Sleep(10); }
    return cond();
}

int main() {
    printf("\n=== Phase 135.1: the disk stops the run ===\n\n");
    const std::string dir = TempDir();
    CreateDirectoryA(dir.c_str(), nullptr);
    std::vector<uint8_t> px((size_t)W * H, 100);
    FrameSlot slot{};
    slot.data = px.data(); slot.dataSize = px.size();
    slot.width = W; slot.height = H; slot.bitDepth = 8;

    printf("1. A write fails on frame 5\n");
    {
        CaptureManager cm;
        CaptureConfig cfg;
        cfg.outputDir = dir; cfg.filePrefix = "wfail";
        cfg.limitType = LimitType::Frames; cfg.limitFrames = 100000;
        cm.SetConfig(cfg);
        StubWriter w; w.failAt = 5;
        cm.SetWriterOverride(&w);
        cm.SetFreeSpaceProbe([](const std::string&) { return UINT64_MAX; });
        Check(cm.StartRecording(W, H, 8), "recording starts");
        int accepted = 0;
        for (int i = 0; i < 40; ++i) {
            slot.timestamp = (uint64_t)(1000 + i);
            if (cm.SubmitFromCaptureThread(&slot)) ++accepted;
        }
        Check(WaitFor([&] { return cm.DiskStopped() != CaptureManager::DiskStop::None; }, 2000),
              "the run is flagged as stopped by the disk");
        Check(cm.DiskStopped() == CaptureManager::DiskStop::WriteFailed,
              "as a failed write", CaptureManager::DiskStopReason(cm.DiskStopped()));
        slot.timestamp = 9999;
        Check(!cm.SubmitFromCaptureThread(&slot), "nothing more is accepted after it");
        Check(cm.IsRecording(), "still open until the render loop stops it (as for a limit)");
        cm.StopRecording(false);
        Check(!cm.IsRecording(), "which closes it");
        Check(w.written == 4, "4 frames written before the failure", std::to_string(w.written.load()));
        Check(cm.FramesRecorded() == 4, "and 4 counted as recorded", std::to_string(cm.FramesRecorded()));
        const uint32_t lostExpected = (uint32_t)(accepted - 5);   // the 5th failed, the rest never tried
        Check(cm.FramesDropped() >= lostExpected,
              "the frames left in RAM are counted as dropped",
              std::to_string(cm.FramesDropped()) + " of at least " + std::to_string(lostExpected));
        Check(cm.BufferPending() == 0, "and the buffer is empty", std::to_string(cm.BufferPending()));

        // The next run: a fresh start, and none of the last run's frames.
        w.failAt = 0;
        Check(cm.StartRecording(W, H, 8), "the next run starts");
        Check(cm.DiskStopped() == CaptureManager::DiskStop::None, "with the flag cleared");
        slot.timestamp = 5555;
        Check(cm.SubmitFromCaptureThread(&slot), "and accepts frames");
        cm.StopRecording(true);
        bool stale = false;
        for (uint64_t t : w.stamps) if (t != 5555) stale = true;
        Check(w.written == 1 && !stale, "only its own frame was written",
              std::to_string(w.written.load()) + " frame(s)");
    }

    printf("\n2. The disk runs low\n");
    {
        CaptureManager cm;
        CaptureConfig cfg;
        cfg.outputDir = dir; cfg.filePrefix = "wfull";
        cfg.limitType = LimitType::Frames; cfg.limitFrames = 100000;
        cm.SetConfig(cfg);
        StubWriter w;
        cm.SetWriterOverride(&w);
        std::atomic<uint64_t> freeB{10ull * 1024 * 1024 * 1024};
        cm.SetFreeSpaceProbe([&](const std::string&) { return freeB.load(); });
        Check(cm.StartRecording(W, H, 8), "recording starts");
        int accepted = 0;
        for (int i = 0; i < 20; ++i) { slot.timestamp = (uint64_t)(i + 1); if (cm.SubmitFromCaptureThread(&slot)) ++accepted; }
        Sleep(300);
        Check(cm.DiskStopped() == CaptureManager::DiskStop::None, "10 GB free: carries on");
        freeB = 100ull * 1024 * 1024;                     // under the 256 MiB floor
        for (int i = 0; i < 20; ++i) { slot.timestamp = (uint64_t)(100 + i); if (cm.SubmitFromCaptureThread(&slot)) ++accepted; }
        Check(WaitFor([&] { return cm.DiskStopped() != CaptureManager::DiskStop::None; }, 2500),
              "flagged within about a second");
        Check(cm.DiskStopped() == CaptureManager::DiskStop::DiskFull, "as nearly full",
              CaptureManager::DiskStopReason(cm.DiskStopped()));
        Check(!cm.SubmitFromCaptureThread(&slot), "nothing more is accepted");
        cm.StopRecording(false);
        Check(w.written == accepted, "every accepted frame reached the file",
              std::to_string(w.written.load()) + " of " + std::to_string(accepted));
        Check(cm.FramesDropped() == 0, "none dropped", std::to_string(cm.FramesDropped()));
    }

    printf("\n3. The floor\n");
    {
        const uint64_t MiB = 1024ull * 1024;
        Check(DiskFloorBytes(0, 1228800, 0) == kDiskFloorMinBytes, "an empty buffer: 256 MiB");
        Check(DiskFloorBytes(100, MiB, 100) == kDiskFloorMinBytes, "100 MiB pending: still 256 MiB");
        const uint64_t f = DiskFloorBytes(3000, 1228800, 3000);
        Check(f == 3000ull * 1228800 + 3000 * 8 + kDiskSidecarBytes,
              "3000 full frames pending: all of them, the trailer and the sidecars",
              std::to_string(f / MiB) + " MiB");
        Check(DiskFloorBytes(0, 0, 0) == kDiskFloorMinBytes, "nothing at all: 256 MiB");
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
