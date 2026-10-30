#pragma once

#include <functional>
// ============================================================
// CaptureThread.h — High-priority capture loop for RadukCapture
// Section 6.2 of Architecture Doc
// ============================================================

#include "DeviceFactory.h"
#include "RingBuffer.h"
#include "Timestamp.h"
#include <thread>
#include <atomic>
#include <cstdint>

namespace raduk::core {

struct CaptureStats {
    std::atomic<uint64_t> framesCaptured{0};
    std::atomic<uint64_t> timeouts{0};
    std::atomic<uint64_t> errors{0};
    std::atomic<int>      droppedFrames{0};   // SDK-reported drops
    std::atomic<uint64_t> bufferOverruns{0};  // Ring buffer overruns
    std::atomic<double>   fps{0.0};           // Measured FPS
    std::atomic<uint64_t> lastFrameTimestamp{0};
    // Phase 58 (review C4 / Architecture §6.3)
    std::atomic<uint32_t> restarts{0};        // pipeline-wedge auto-resets
    std::atomic<bool>     cameraFault{false}; // camera removed / unrecovered
};

class CaptureThread {
public:
    CaptureThread() = default;
    ~CaptureThread() { Stop(); }

    // Non-copyable
    CaptureThread(const CaptureThread&) = delete;
    CaptureThread& operator=(const CaptureThread&) = delete;

    // Start capturing frames from camera into the ring buffer.
    // exposureMs is used to calculate the SDK timeout.
    // cpuCore is the core to pin this thread to (-1 for no pinning).
    bool Start(ICamera& camera, RingBuffer& ringBuffer,
               double exposureMs, int cpuCore = -1);

    // Stop capture. Blocks until thread exits.
    void Stop();

    // Called for EVERY frame, on the capture thread, before the frame
    // is committed to the ring. This is the recording path: the ring is
    // for the preview, which is allowed to skip frames.
    //
    // Must not block -- anything slow here stalls frame acquisition and
    // overflows the camera's own buffer.
    void SetFrameSink(std::function<void(const FrameSlot*)> sink) {
        m_frameSink = std::move(sink);
    }

    // True if the capture thread is running.
    bool IsRunning() const { return m_running.load(std::memory_order_relaxed); }

    // Live statistics (safe to read from any thread).
    const CaptureStats& Stats() const { return m_stats; }

    // Update exposure (affects SDK timeout calculation).
    void SetExposure(double milliseconds) {
        m_exposureMs.store(milliseconds, std::memory_order_relaxed);
    }

private:
    void Run();

    ICamera*                m_camera = nullptr;
    RingBuffer*             m_ringBuffer = nullptr;
    std::atomic<double>     m_exposureMs{8.0};
    int                     m_cpuCore = -1;

    std::thread             m_thread;

    std::function<void(const FrameSlot*)> m_frameSink;
    std::atomic<bool>       m_running{false};
    CaptureStats            m_stats;

    // FPS measurement
    uint64_t                m_fpsStartTime = 0;
    uint64_t                m_fpsFrameCount = 0;
    static constexpr double FPS_UPDATE_INTERVAL_SEC = 0.5;
};

} // namespace raduk::core
