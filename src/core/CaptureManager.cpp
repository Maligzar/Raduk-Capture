// ============================================================
// CaptureManager.cpp — Recording orchestration
// Out-of-line definitions for CaptureManager.h.
// ============================================================

#include "CaptureManager.h"

namespace raduk::core {

bool CaptureManager::StartRecording(int width, int height, int bitDepth) {
    if (m_status.recording.load()) {
        Say("Already recording");
        return false;
    }

    // Ensure output directory exists
    CreateDirectoryA(m_config.outputDir.c_str(), nullptr);

    // Build output filename
    std::string path = BuildFilePath(width, height, bitDepth);

    // Allocate write buffer (RAM ring).
    // Phase 59 (review C3): reallocate when the per-slot CAPACITY no
    // longer fits the frame, not just when the slot count changed —
    // recording a larger ROI into small slots made every Push fail,
    // yielding a zero-frame SER with console spam only. And a failed
    // Allocate (512 full-frame slots ≈ 4.2GB) must abort the start,
    // not silently "record" nothing.
    size_t frameBytes = (size_t)width * height * (bitDepth == 16 ? 2 : 1);
    {
        char b[160];
        snprintf(b, sizeof(b),
                 "Write buffer sized for %dx%d %d-bit = %zu bytes/slot",
                 width, height, bitDepth, frameBytes);
        Say(b);
    }
    // Size the buffer to the RUN, not to a constant. A planetary
    // capture is bounded -- 3000 frames is 3.7 GB at 1280x960 RAW8 --
    // and this machine has the memory to hold one outright, which takes
    // the disk off the critical path entirely rather than merely
    // cushioning it. See PlanWriteBufferSlots().
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);

    const double planFps = (m_lastKnownFps > 1.0) ? m_lastKnownFps : 60.0;
    const int slots = PlanWriteBufferSlots(
        m_config.limitType == LimitType::Frames ? m_config.limitFrames : 0,
        m_config.limitType == LimitType::Time   ? m_config.limitTimeSec : 0.0,
        planFps, frameBytes, mem.ullAvailPhys);

    {
        char b[220];
        snprintf(b, sizeof(b),
                 "Write buffer %d slots = %.0f MB, absorbs %.1f s of total "
                 "disk stall at %.0f fps",
                 slots, slots * (double)frameBytes / 1048576.0,
                 slots / planFps, planFps);
        Say(b);
    }

    if (!m_writeBuffer.IsAllocated() ||
        m_writeBuffer.NumSlots() != slots ||
        m_writeBuffer.SlotCapacity() < frameBytes) {
        if (!m_writeBuffer.Allocate(slots, frameBytes)) {
            printf("[CaptureManager] Write buffer allocation FAILED "
                   "(%d x %zu bytes) — recording not started\n",
                   slots, frameBytes);
            std::lock_guard<std::mutex> lk(m_mutex);
            m_status.lastError = "Write buffer allocation failed (RAM)";
            if (m_log) m_log->Capture("ERROR: write buffer allocation failed");
            return false;
        }
    }

    // Open SER writer
    if (!m_writer.Open(path, width, height, bitDepth)) {
        { char b[512]; snprintf(b, sizeof(b), "Failed to open SER: %s", path.c_str()); Say(b); }
        return false;
    }

    // Phase 48: the observation record, snapshotted at the start --
    // where the mount was pointing when the run began is the framing
    // worth returning to, not wherever it had drifted to by the end.
    m_obs = ObservationRecord{};
    {
        const size_t slash = path.find_last_of("\\/");
        m_obs.file = (slash == std::string::npos) ? path : path.substr(slash + 1);
    }
    m_obs.frameType = m_config.frameType;
    m_obs.startUnix = (int64_t)time(nullptr);
    m_obs.width     = width;
    m_obs.height    = height;
    m_obs.bitDepth  = bitDepth;
    m_obs.colour    = "RGGB";   // SERWriter::Open's default, which is what is written
    if (m_obsSource) m_obsSource(m_obs);

    // Reset stats
    m_status.framesRecorded.store(0);
    m_status.framesDropped.store(0);
    m_status.bytesWritten.store(0);
    m_paused.store(false);  // always start unpaused
    m_status.elapsedSec.store(0.0);
    m_frameWidth  = width;
    m_frameHeight = height;
    m_bitDepth    = bitDepth;
    m_startTime   = std::chrono::steady_clock::now();
    m_startNs.store(std::chrono::duration_cast<std::chrono::nanoseconds>(
        m_startTime.time_since_epoch()).count(), std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_status.currentFile = path;
        m_status.lastError.clear();
    }

    m_pushFailureReported.store(false);
    m_framesSubmitted.store(0);
    m_limitReached.store(false);

    // Start writer thread
    m_writerRunning.store(true);
    m_writerThread = std::thread(&CaptureManager::WriterLoop, this);

    m_status.recording.store(true);

    { char b[512]; snprintf(b, sizeof(b), "Recording started: %s", path.c_str()); Say(b); }
    if (m_log) m_log->Capture("Recording started: %s  %dx%d  %d-bit",
                               path.c_str(), width, height, bitDepth);
    return true;
}

void CaptureManager::StopRecording(bool normal) {
    if (!m_status.recording.load()) return;

    m_status.recording.store(false);

    // Phase 48: the run ends HERE, not when the drain below finishes --
    // a buffer sized to the whole run can take minutes to reach disk.
    // Measured directly because elapsedSec is only kept up to date for
    // time-limited runs; a frame-limited run left it at 0, and the
    // first record written in the app said a 3.5 minute capture took
    // 0.00 s.
    m_obs.endUnix     = (int64_t)time(nullptr);
    m_obs.durationSec = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - m_startTime).count();

    // Phase 105: and keep it, so ElapsedSec() can report the finished
    // run. It derives the live value from m_startNs while recording
    // and falls back to this once recording stops, which is what lets
    // the panel say how long the capture ran after it has ended.
    //
    // Note this is stored AFTER m_status.recording went false just
    // above, which is the order the reader needs: a reader that sees
    // recording false must not then read a stale elapsed.
    m_status.elapsedSec.store(m_obs.durationSec);

    // Signal writer thread to finish draining and exit
    m_writerRunning.store(false);
    if (m_writerThread.joinable()) m_writerThread.join();

    // Remember the finished file before closing, so the caller can
    // hand it straight to AutoStakkert without reconstructing a path.
    m_lastPath = m_writer.Path();

    // The run's sky figures, read before Close() resets them.
    {
        const SERWriter::SkySummary sky = m_writer.SkyConditionsSummary();
        if (sky.valid) {
            m_obs.altStartDeg = sky.altStart;  m_obs.altEndDeg = sky.altEnd;
            m_obs.altMinDeg   = sky.altMin;    m_obs.altMaxDeg = sky.altMax;
            m_obs.azEndDeg    = sky.azEnd;
            m_obs.airmassMean = sky.airmassMean;
            m_obs.airmassMax  = sky.airmassMax;
        }
    }

    // Close SER writer (writes timestamp trailer, patches frame count)
    m_writer.Close();
    WriteObservationRecord();

    uint32_t frames = m_status.framesRecorded.load();
    uint32_t drops  = m_status.framesDropped.load();
    double   mb     = m_status.bytesWritten.load() / (1024.0 * 1024.0);
    double   secs   = m_status.elapsedSec.load();

    printf("[CaptureManager] Recording stopped: %u frames  %u drops  %.1f MB  %.1fs\n",
           frames, drops, mb, secs);

    if (m_log) m_log->Capture("Recording stopped: %u frames  %u drops  %.1f MB  %.1fs",
                               frames, drops, mb, secs);

    // Fires on EVERY path out of a recording, including the frame/time
    // limit inside SubmitFrame - which is how a planned capture
    // normally ends. Callers distinguish by the flag.
    if (m_onComplete) m_onComplete(m_lastPath, normal);
}

// Phase 48: <base>_observation.txt beside <base>.ser. Written on every
// way out of a recording, an emergency stop included -- the frames on
// disk are the same frames whatever ended the run, and a record that
// only exists for tidy finishes is one nobody can rely on.
void CaptureManager::WriteObservationRecord() {
    if (m_lastPath.empty()) return;
    m_obs.frames       = (int)m_status.framesRecorded.load();
    m_obs.framesDropped = (int)m_status.framesDropped.load();

    const std::string obsPath = ObservationPathFor(m_lastPath);
    const std::string text    = SerializeObservation(m_obs);
    FILE* f = nullptr;
    if (fopen_s(&f, obsPath.c_str(), "w") != 0 || !f) {
        Say("Could not write the observation record: " + obsPath);
        return;
    }
    const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    fclose(f);
    if (!ok) {
        Say("Observation record write failed: " + obsPath);
        return;
    }
    m_lastObsPath = obsPath;
    Say("Observation record: " + obsPath);
}

// Why a Push() failed, said once per recording -- the failing case
// fails on every frame and a line each would bury everything else.
//
// The two causes need opposite fixes: a full buffer means the disk is
// not keeping up, a frame too large means the buffer was sized for a
// different format than the camera is sending. Calling both "FULL"
// sent anyone reading the log after the wrong one.
void CaptureManager::ReportPushFailure(size_t frameBytes) {
    if (m_pushFailureReported.exchange(true)) return;
    char b[200];
    const size_t cap = m_writeBuffer.SlotCapacity();
    if (frameBytes > cap)
        snprintf(b, sizeof(b),
                 "FRAME TOO LARGE for the write buffer: frame is %zu "
                 "bytes, slots hold %zu. Nothing will be recorded. The "
                 "buffer was sized for a different format than the "
                 "camera is sending.",
                 frameBytes, cap);
    else
        snprintf(b, sizeof(b),
                 "Write buffer FULL (%llu of %d slots pending) - the "
                 "disk is not keeping up",
                 (unsigned long long)m_writeBuffer.Pending(),
                 m_writeBuffer.NumSlots());
    Say(b);
}

// Called ON THE CAPTURE THREAD. See the header for why this exists.
bool CaptureManager::SubmitFromCaptureThread(const FrameSlot* slot) {
    if (!m_status.recording.load()) return false;
    if (!slot || !slot->data) return false;
    if (m_paused.load()) return false;

    // Limits are checked here but acted on elsewhere. StopRecording()
    // joins the writer thread, and with a buffer sized to a whole run
    // that drain can take seconds -- during which this thread would not
    // be pulling frames and the camera's own buffer would overflow. So
    // raise the flag and let the render loop stop us.
    if (m_config.limitType == LimitType::Frames &&
        m_framesSubmitted.load(std::memory_order_relaxed) >= m_config.limitFrames) {
        m_limitReached.store(true, std::memory_order_relaxed);
        return false;
    }
    if (m_config.limitType == LimitType::Time) {
        // Phase 105: the limit check only. The store that used to sit
        // here was the single reason elapsedSec ever had a value, and
        // being inside this branch is why a frame-limited run showed
        // 00:00 throughout. ElapsedSec() derives it now.
        const double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - m_startTime).count();
        if (elapsed >= m_config.limitTimeSec) {
            m_limitReached.store(true, std::memory_order_relaxed);
            return false;
        }
    }

    // A gated frame is deliberately skipped, not lost, so it must not
    // count as a drop.
    if (!m_gateOpen.load(std::memory_order_relaxed)) return false;

    const bool ok = m_writeBuffer.Push(
        slot->data, slot->dataSize, slot->timestamp,
        slot->width, slot->height, slot->bitDepth,
        m_hintSharp.load(std::memory_order_relaxed),
        m_hintCX.load(std::memory_order_relaxed),
        m_hintCY.load(std::memory_order_relaxed));

    if (ok) {
        m_framesSubmitted.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_status.framesDropped.fetch_add(1);
        ReportPushFailure(slot->dataSize);
    }
    return ok;
}

bool CaptureManager::SubmitFrame(const FrameSlot* slot, float sharpness,
                                 uint32_t cropCX, uint32_t cropCY) {
    if (!m_status.recording.load()) return false;
    if (!slot || !slot->data) return false;

    // Phase 41 — Pause: skip frames without closing the SER file
    if (m_paused.load()) return false;

    uint32_t recorded = m_status.framesRecorded.load();
    if (m_config.limitType == LimitType::Frames &&
        recorded >= m_config.limitFrames) {
        StopRecording();
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - m_startTime).count();

    if (m_config.limitType == LimitType::Time &&
        elapsed >= m_config.limitTimeSec) {
        StopRecording();
        return false;
    }

    bool ok = m_writeBuffer.Push(
        slot->data, slot->dataSize, slot->timestamp,
        slot->width, slot->height, slot->bitDepth, sharpness,
        cropCX, cropCY);

    if (!ok) {
        m_status.framesDropped.fetch_add(1);
        ReportPushFailure(slot->dataSize);
        return false;
    }

    return true;
}

std::string CaptureManager::CurrentFile() {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_status.currentFile;
}

void CaptureManager::WriterLoop() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    Say("Writer thread started");

    while (m_writerRunning.load() || m_writeBuffer.Pending() > 0) {
        const WriteSlot* slot = m_writeBuffer.Peek();
        if (!slot) {
            Sleep(1);
            continue;
        }

        // Phase 76: score THIS frame, here, rather than recording
        // whatever the analyser last happened to produce. The
        // analyser runs at ~2Hz and drops most frames, so the old
        // sidecar held one value across long runs of frames and
        // could not rank anything. Scoring costs microseconds on a
        // thread already waiting on disk.
        uint32_t ccx = slot->cropCX ? slot->cropCX : slot->width  / 2;
        uint32_t ccy = slot->cropCY ? slot->cropCY : slot->height / 2;
        float realSharp = SharpnessPercent(
            BayerLaplacianVariance(slot->data, slot->width, slot->height,
                                   slot->bitDepth, ccx, ccy));
        bool ok = m_writer.WriteFrame(slot->data, slot->dataSize,
                                      slot->timestampUs, realSharp);
        m_writeBuffer.Pop();

        if (ok) {
            m_status.framesRecorded.fetch_add(1);
            m_status.bytesWritten.fetch_add(slot->dataSize);
        } else {
            Say("Disk write error — stopping");
            break;
        }
    }

    { char b[128]; snprintf(b, sizeof(b), "Writer thread finished - %u frames written",
                            m_status.framesRecorded.load()); Say(b); }
}

std::string CaptureManager::BuildFilePath(int width, int height, int bitDepth) {
    time_t now = time(nullptr);
    struct tm t; localtime_s(&t, &now);
    char timebuf[32];
    sprintf_s(timebuf, "%04d%02d%02d_%02d%02d%02d",
        t.tm_year+1900, t.tm_mon+1, t.tm_mday,
        t.tm_hour, t.tm_min, t.tm_sec);

    // Find next run number for this prefix+date combination
    int run = 1;
    while (true) {
        char name[256];
        sprintf_s(name, "%s_%s_%s_%dx%d_%dbit_%04d.ser",
            m_config.filePrefix.c_str(), timebuf,
            m_config.frameType.c_str(),
            width, height, bitDepth, run);
        std::string path = m_config.outputDir + "\\" + name;
        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "rb") != 0) return path; // doesn't exist
        fclose(f);
        run++;
    }
}

} // namespace raduk::core
