#pragma once
// ============================================================
// CaptureManager.h — Recording orchestration for RadukCapture
// Coordinates: RingBuffer (preview) -> WriteBuffer (RAM) ->
//              writer thread -> SERWriter (disk).
//
// The capture thread keeps running at full speed (it only feeds
// the preview RingBuffer). CaptureManager taps the preview ring
// from the render loop: each preview frame is also pushed into
// the WriteBuffer when recording is active.
//
// The writer thread drains WriteBuffer to disk asynchronously,
// completely decoupled from the capture thread.
// ============================================================

#include "WriteBuffer.h"
#include "SERWriter.h"
#include "SessionLog.h"
#include "FrameSlot.h"
#include "Sharpness.h"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <functional>   // SetOnComplete
#include <cstdio>
#include <ctime>
#include <windows.h>

namespace raduk::core {

enum class LimitType { Frames, Time };

struct CaptureConfig {
    std::string outputDir   = "D:\\RadukCapture\\captures";
    std::string filePrefix  = "Capture";      // e.g. "Jupiter"
    std::string frameType   = "Light";        // Phase 44: Light/Dark/Flat/Bias
    LimitType   limitType   = LimitType::Frames;
    uint32_t    limitFrames = 1000;
    double      limitTimeSec = 60.0;
    int         writeBufferSlots = 512;
};

struct CaptureStatus {
    std::atomic<bool>     recording{false};
    std::atomic<uint32_t> framesRecorded{0};
    std::atomic<uint32_t> framesDropped{0};    // write buffer overruns
    std::atomic<uint64_t> bytesWritten{0};
    std::atomic<double>   elapsedSec{0.0};
    std::string           currentFile;         // guarded by m_mutex
    std::string           lastError;           // guarded by m_mutex
};

class CaptureManager {
public:
    CaptureManager() = default;
    ~CaptureManager() { StopRecording(); }

    CaptureManager(const CaptureManager&) = delete;
    CaptureManager& operator=(const CaptureManager&) = delete;

    // ── Setup ──────────────────────────────────────────────────

    void SetConfig(const CaptureConfig& cfg) { m_config = cfg; }
    const CaptureConfig& Config() const      { return m_config; }

    void SetSessionLog(SessionLog* log) { m_log = log; }

    // The rate the camera is actually delivering, fed from the readout
    // loop. Used only to size the write buffer at StartRecording: how
    // many seconds of disk stall a given slot count absorbs depends
    // entirely on the frame rate, and the manager cannot know it
    // otherwise. 0 until the first measurement arrives.
    void SetObservedFps(double fps) { m_lastKnownFps = fps; }

    // ── Recording control ──────────────────────────────────────

    // Start recording. Allocates write buffer, opens SER file, starts writer thread.
    // frameSizeBytes: expected bytes per frame (width * height * (bitDepth/8))
    bool StartRecording(int width, int height, int bitDepth);

    // Stop recording. Drains remaining write buffer, closes SER file.
    void StopRecording(bool normal = true);

    // ── Frame submission ───────────────────────────────────────

    // Called from the render loop for each preview frame when recording.
    // sharpness: current FrameAnalyzer sharpnessPct (0-100), 0 = no analysis yet.
    // Path of the most recently FINISHED recording, empty before the
    // first one completes.
    const std::string& LastFilePath() const { return m_lastPath; }

    // Fired once a recording has fully closed, with the finished path and
    // whether it ended NORMALLY. 'Normal' means the user pressed stop or
    // the configured frame/time limit was reached; it is false for an
    // emergency stop or an ROI change. Anything expensive hung off the
    // end of a capture must only run on a normal finish.
    //
    // This exists because the limit path matters most: a planned capture
    // ends by hitting its frame count inside SubmitFrame, not by anyone
    // pressing a button, so a hook on the button alone would almost never
    // fire in real use.
    void SetOnComplete(std::function<void(const std::string&, bool)> cb) {
        m_onComplete = std::move(cb);
    }

    bool SubmitFrame(const FrameSlot* slot, float sharpness = 0.0f,
                     uint32_t cropCX = 0, uint32_t cropCY = 0);

    // ── Pause / Resume (Phase 41) ─────────────────────────────
    // Pause skips incoming frames without closing the SER file.
    // The writer thread keeps draining the buffer during pause.
    void Pause()   { m_paused.store(true);  printf("[CaptureManager] PAUSED\n"); }
    void Resume()  { m_paused.store(false); printf("[CaptureManager] RESUMED\n"); }
    void TogglePause() { m_paused.load() ? Resume() : Pause(); }
    bool IsPaused() const { return m_paused.load(); }

    // ── Status ─────────────────────────────────────────────────

    bool IsRecording()      const { return m_status.recording.load(); }
    uint32_t FramesRecorded() const { return m_status.framesRecorded.load(); }
    uint32_t FramesDropped()  const { return m_status.framesDropped.load(); }
    double   ElapsedSec()     const { return m_status.elapsedSec.load(); }
    uint64_t BytesWritten()   const { return m_status.bytesWritten.load(); }

    // Every diagnostic in here was a printf, and this is a GUI build
    // where that reaches nothing. The writer thread can stop on a disk
    // error, the buffer can fail to allocate and frames can be dropped,
    // and all three announced themselves into a void. A capture that
    // silently produces a 0-byte file is exactly the case where these
    // lines are the whole answer.
    void SetLogCallback(std::function<void(const std::string&)> cb) {
        m_logFn = std::move(cb);
    }
    uint64_t BufferPending()  const { return m_writeBuffer.Pending(); }
    uint64_t BufferTotal()    const { return (uint64_t)m_writeBuffer.NumSlots(); }

    uint32_t LimitFrames()    const { return m_config.limitFrames; }
    LimitType GetLimitType()  const { return m_config.limitType; }
    double    LimitTimeSec()  const { return m_config.limitTimeSec; }

    std::string CurrentFile();

private:
    // ── Writer thread ──────────────────────────────────────────
    // Drains write buffer to disk as fast as the disk allows.
    // Completely independent of the capture thread.

    void WriterLoop();

    // ── File naming ────────────────────────────────────────────

    std::string BuildFilePath(int width, int height, int bitDepth);

    std::string     m_lastPath;      // most recently finished recording
    std::function<void(const std::string&, bool)> m_onComplete;
    CaptureConfig   m_config;
    WriteBuffer     m_writeBuffer;
    // Named m_logFn, not m_log: m_log is already the SessionLog pointer
    // below, which writes the session summary file. Different jobs.
    std::function<void(const std::string&)> m_logFn;
    void Say(const std::string& s) const { if (m_logFn) m_logFn("[CAPTURE] " + s); }
    SERWriter       m_writer;
    SessionLog*     m_log = nullptr;
    CaptureStatus   m_status;
    std::mutex      m_mutex;

    std::thread     m_writerThread;
    std::atomic<bool> m_writerRunning{false};
    // Push failures repeat every frame; report the reason once.
    std::atomic<bool> m_pushFailureReported{false};
    std::atomic<bool> m_paused{false};         // Phase 41: pause without closing SER

    double    m_lastKnownFps = 0.0;
    int       m_frameWidth  = 0;
    int       m_frameHeight = 0;
    int       m_bitDepth    = 8;
    std::chrono::steady_clock::time_point m_startTime;
};

} // namespace raduk::core
