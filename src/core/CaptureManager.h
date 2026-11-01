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
#include "ObservationRecord.h"
#include "Paths.h"
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
    std::string outputDir   = Paths::Captures();
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

    // Where the scope is pointing, forwarded into the SER sidecar so a
    // morning-after review can tell one run from another by how much
    // atmosphere it was shot through. Fed from the readout loop.
    void NoteSky(double altDeg, double azDeg, double airmass,
                 double seeingFactor, double dispersionArcsec) {
        if (m_status.recording.load())
            m_writer.NoteSkyConditions(altDeg, azDeg, airmass,
                                       seeingFactor, dispersionArcsec);
    }

    // Phase 48: who fills in the observation record. Called on the
    // thread that calls StartRecording(), just after the SER opens,
    // and expected to fill in what only the app knows -- target,
    // profile, exposure, gain, focuser, mount position. The manager
    // adds the file, the format and the run's own numbers itself, so
    // EVERY route into a recording leaves a record, including the
    // format-change restart, not just the RECORD button.
    void SetObservationSource(std::function<void(ObservationRecord&)> fn) {
        m_obsSource = std::move(fn);
    }
    // Path of the most recently written record, empty if none yet.
    const std::string& LastObservationPath() const { return m_lastObsPath; }

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

    // ── Recording from the capture thread (Phase: decoupling) ──
    //
    // SubmitFrame() below is called from the render loop, which takes
    // ONE frame per pass via GetLatestFrame(). That made the recorded
    // frame rate a property of the UI: Window::Run() ends in Sleep(1),
    // which on a default-granularity Windows costs 16 ms, so the loop
    // tops out near 62 passes a second before any rendering work. Every
    // frame the camera produced in between was overwritten in the ring
    // and silently lost -- on a camera that does well over 100 fps at
    // this ROI, more than half of every capture.
    //
    // So the capture thread submits directly now, and these carry what
    // it needs from the render loop, which still owns the policy.
    //
    // The gate decision is published rather than recomputed: it keys off
    // a seeing score that only updates at about 2 Hz, so there is
    // nothing to gain by evaluating it per frame and a shared analysis
    // object to race if we tried.
    void SetGateOpen(bool open) { m_gateOpen.store(open, std::memory_order_relaxed); }

    // Where to measure sharpness, and the live gate's hint. The writer
    // thread scores each frame itself; this only tells it where to look.
    void SetFrameHints(uint32_t cropCX, uint32_t cropCY, float sharpnessHint) {
        m_hintCX.store(cropCX, std::memory_order_relaxed);
        m_hintCY.store(cropCY, std::memory_order_relaxed);
        m_hintSharp.store(sharpnessHint, std::memory_order_relaxed);
    }

    // Called ON THE CAPTURE THREAD, once per frame the camera delivers.
    // Never blocks and never stops the recording: StopRecording() joins
    // the writer thread, and a buffer sized to a whole run can take
    // seconds to drain. Stalling frame acquisition for that long would
    // overflow the camera's own buffer -- so hitting the limit only
    // raises a flag, and the render loop does the stopping.
    bool SubmitFromCaptureThread(const FrameSlot* slot);

    // Set when a frame or time limit was reached. The render loop polls
    // this and calls StopRecording() on its own thread.
    bool LimitReached() const { return m_limitReached.load(std::memory_order_relaxed); }

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
    // How long the current run has been going, in seconds.
    //
    // Phase 105. This used to return m_status.elapsedSec, a field that
    // something else had to remember to keep current -- and only one
    // path did. In SubmitFromCaptureThread the store sat INSIDE
    // `if (m_config.limitType == LimitType::Time)`, so a frame-limited
    // or unlimited run never updated it and the panel's timer sat at
    // 00:00 for the whole capture.
    //
    // Phase 48 hit the same field from the other side: the observation
    // record said a three and a half minute capture took 0.00 s. It
    // worked around it by measuring from m_startTime by hand and left
    // the field broken, so the bug stayed live for the display.
    //
    // Derived now, not remembered. A clock that is computed cannot go
    // stale, and it keeps running when no frames are arriving -- while
    // paused, or with the gate shut -- which a frame-driven counter
    // could not. After the run it returns the final figure
    // StopRecording stored, so the panel can say how long it ran.
    double   ElapsedSec() const {
        if (!m_status.recording.load()) return m_status.elapsedSec.load();
        const int64_t startNs = m_startNs.load(std::memory_order_relaxed);
        if (startNs == 0) return 0.0;
        const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const int64_t d = nowNs - startNs;
        return d > 0 ? (double)d * 1e-9 : 0.0;
    }
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
    std::function<void(ObservationRecord&)> m_obsSource;
    ObservationRecord m_obs;         // this run's record, filled at start
    std::string     m_lastObsPath;
    void WriteObservationRecord();
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

    void      ReportPushFailure(size_t frameBytes);

    // Frames handed to the write buffer, which is what a frame LIMIT
    // actually means. Deliberately not m_status.framesRecorded: that is
    // incremented by the WRITER thread once a frame reaches the disk,
    // so checking a limit against it asks "how many have landed" when
    // the question is "how many have I taken".
    //
    // The difference did not show while the render loop did the
    // submitting, because at ~60 submissions a second the writer kept
    // pace and the two counts stayed within a frame or two. Submitting
    // from the capture thread runs far ahead of the disk by design --
    // that is the entire point of the buffer -- and a 5-frame limit
    // then recorded 40 frames before the writer caught up enough to
    // trip it.
    std::atomic<uint32_t> m_framesSubmitted{0};

    double    m_lastKnownFps = 0.0;
    std::atomic<bool>     m_gateOpen{true};
    std::atomic<bool>     m_limitReached{false};
    std::atomic<uint32_t> m_hintCX{0}, m_hintCY{0};
    std::atomic<float>    m_hintSharp{0.0f};
    int       m_frameWidth  = 0;
    int       m_frameHeight = 0;
    int       m_bitDepth    = 8;
    std::chrono::steady_clock::time_point m_startTime;

    // Phase 105. The same instant as m_startTime, as a plain integer
    // of nanoseconds, so ElapsedSec() can read it from the UI thread
    // without racing the capture thread that sets it. A
    // steady_clock::time_point is not atomic and this is read on every
    // status tick while the capture thread is running.
    std::atomic<int64_t>  m_startNs{0};
};

} // namespace raduk::core
