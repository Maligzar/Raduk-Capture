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
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <windows.h>

namespace raduk::core {

enum class LimitType { Frames, Time };

struct CaptureConfig {
    std::string outputDir   = "D:\\RadukCapture\\captures";
    std::string filePrefix  = "Capture";      // e.g. "Jupiter"
    LimitType   limitType   = LimitType::Frames;
    uint32_t    limitFrames = 1000;           // frames to capture
    double      limitTimeSec = 60.0;          // seconds to capture
    int         writeBufferSlots = 512;       // RAM buffer depth
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

    // ── Recording control ──────────────────────────────────────

    // Start recording. Allocates write buffer, opens SER file, starts writer thread.
    // frameSizeBytes: expected bytes per frame (width * height * (bitDepth/8))
    bool StartRecording(int width, int height, int bitDepth) {
        if (m_status.recording.load()) {
            printf("[CaptureManager] Already recording\n");
            return false;
        }

        // Ensure output directory exists
        CreateDirectoryA(m_config.outputDir.c_str(), nullptr);

        // Build output filename
        std::string path = BuildFilePath(width, height, bitDepth);

        // Allocate write buffer (RAM ring)
        size_t frameBytes = (size_t)width * height * (bitDepth == 16 ? 2 : 1);
        if (!m_writeBuffer.IsAllocated() ||
            m_writeBuffer.NumSlots() != m_config.writeBufferSlots) {
            m_writeBuffer.Allocate(m_config.writeBufferSlots, frameBytes);
        }

        // Open SER writer
        if (!m_writer.Open(path, width, height, bitDepth)) {
            printf("[CaptureManager] Failed to open SER: %s\n", path.c_str());
            return false;
        }

        // Reset stats
        m_status.framesRecorded.store(0);
        m_status.framesDropped.store(0);
        m_status.bytesWritten.store(0);
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

        // Start writer thread
        m_writerRunning.store(true);
        m_writerThread = std::thread(&CaptureManager::WriterLoop, this);

        m_status.recording.store(true);

        printf("[CaptureManager] Recording started: %s\n", path.c_str());
        if (m_log) m_log->Capture("Recording started: %s  %dx%d  %d-bit",
                                   path.c_str(), width, height, bitDepth);
        return true;
    }

    // Stop recording. Drains remaining write buffer, closes SER file.
    void StopRecording() {
        if (!m_status.recording.load()) return;

        m_status.recording.store(false);

        // Signal writer thread to finish draining and exit
        m_writerRunning.store(false);
        if (m_writerThread.joinable()) m_writerThread.join();

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
    }

    // ── Frame submission ───────────────────────────────────────

    // Called from the render loop for each preview frame when recording.
    // sharpness: current FrameAnalyzer sharpnessPct (0-100), 0 = no analysis yet.
    bool SubmitFrame(const FrameSlot* slot, float sharpness = 0.0f) {
        if (!m_status.recording.load()) return false;
        if (!slot || !slot->data) return false;

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
            slot->width, slot->height, slot->bitDepth, sharpness);

        if (!ok) {
            m_status.framesDropped.fetch_add(1);
            printf("[CaptureManager] Write buffer FULL — frame dropped!\n");
            return false;
        }

        return true;
    }

    // ── Status ─────────────────────────────────────────────────

    bool IsRecording()      const { return m_status.recording.load(); }
    uint32_t FramesRecorded() const { return m_status.framesRecorded.load(); }
    uint32_t FramesDropped()  const { return m_status.framesDropped.load(); }
    double   ElapsedSec()     const { return m_status.elapsedSec.load(); }
    uint64_t BytesWritten()   const { return m_status.bytesWritten.load(); }
    uint64_t BufferPending()  const { return m_writeBuffer.Pending(); }
    uint64_t BufferTotal()    const { return (uint64_t)m_writeBuffer.NumSlots(); }

    uint32_t LimitFrames()    const { return m_config.limitFrames; }
    LimitType GetLimitType()  const { return m_config.limitType; }
    double    LimitTimeSec()  const { return m_config.limitTimeSec; }

    std::string CurrentFile() {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_status.currentFile;
    }

private:
    // ── Writer thread ──────────────────────────────────────────
    // Drains write buffer to disk as fast as the disk allows.
    // Completely independent of the capture thread.

    void WriterLoop() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        printf("[CaptureManager] Writer thread started\n");

        while (m_writerRunning.load() || m_writeBuffer.Pending() > 0) {
            const WriteSlot* slot = m_writeBuffer.Peek();
            if (!slot) {
                Sleep(1);
                continue;
            }

            bool ok = m_writer.WriteFrame(slot->data, slot->dataSize,
                                          slot->timestampUs, slot->sharpness);
            m_writeBuffer.Pop();

            if (ok) {
                m_status.framesRecorded.fetch_add(1);
                m_status.bytesWritten.fetch_add(slot->dataSize);
            } else {
                printf("[CaptureManager] Disk write error — stopping\n");
                break;
            }
        }

        printf("[CaptureManager] Writer thread finished — %u frames written\n",
               m_status.framesRecorded.load());
    }

    // ── File naming ────────────────────────────────────────────

    std::string BuildFilePath(int width, int height, int bitDepth) {
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
            sprintf_s(name, "%s_%s_%dx%d_%dbit_%04d.ser",
                m_config.filePrefix.c_str(), timebuf,
                width, height, bitDepth, run);
            std::string path = m_config.outputDir + "\\" + name;
            FILE* f = nullptr;
            if (fopen_s(&f, path.c_str(), "rb") != 0) return path; // doesn't exist
            fclose(f);
            run++;
        }
    }

    CaptureConfig   m_config;
    WriteBuffer     m_writeBuffer;
    SERWriter       m_writer;
    SessionLog*     m_log = nullptr;
    CaptureStatus   m_status;
    std::mutex      m_mutex;

    std::thread     m_writerThread;
    std::atomic<bool> m_writerRunning{false};

    int       m_frameWidth  = 0;
    int       m_frameHeight = 0;
    int       m_bitDepth    = 8;
    std::chrono::steady_clock::time_point m_startTime;
};

} // namespace raduk::core
