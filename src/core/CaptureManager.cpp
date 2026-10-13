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
    if (!m_writeBuffer.IsAllocated() ||
        m_writeBuffer.NumSlots() != m_config.writeBufferSlots ||
        m_writeBuffer.SlotCapacity() < frameBytes) {
        if (!m_writeBuffer.Allocate(m_config.writeBufferSlots, frameBytes)) {
            printf("[CaptureManager] Write buffer allocation FAILED "
                   "(%d x %zu bytes) — recording not started\n",
                   m_config.writeBufferSlots, frameBytes);
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

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_status.currentFile = path;
        m_status.lastError.clear();
    }

    m_pushFailureReported.store(false);

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

    // Signal writer thread to finish draining and exit
    m_writerRunning.store(false);
    if (m_writerThread.joinable()) m_writerThread.join();

    // Remember the finished file before closing, so the caller can
    // hand it straight to AutoStakkert without reconstructing a path.
    m_lastPath = m_writer.Path();

    // Close SER writer (writes timestamp trailer, patches frame count)
    m_writer.Close();

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
    m_status.elapsedSec.store(elapsed);

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
        // Push fails for two quite different reasons and this message
        // only ever named one of them: the buffer being full, or the
        // frame not fitting a slot. They need opposite fixes -- a full
        // buffer means the disk is not keeping up, a frame too large
        // means the buffer was sized for a different format than the
        // camera is sending -- and calling both "FULL" sent anyone
        // reading the log after the wrong one.
        //
        // Said once per recording, because the failing case fails on
        // every single frame and a line each would bury everything.
        if (!m_pushFailureReported.exchange(true)) {
            char b[200];
            const size_t cap = m_writeBuffer.SlotCapacity();
            if (slot->dataSize > cap)
                snprintf(b, sizeof(b),
                         "FRAME TOO LARGE for the write buffer: frame is %zu "
                         "bytes, slots hold %zu. Nothing will be recorded. The "
                         "buffer was sized for a different format than the "
                         "camera is sending.",
                         slot->dataSize, cap);
            else
                snprintf(b, sizeof(b),
                         "Write buffer FULL (%llu of %d slots pending) - the "
                         "disk is not keeping up",
                         (unsigned long long)m_writeBuffer.Pending(),
                         m_writeBuffer.NumSlots());
            Say(b);
        }
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
