#pragma once
// ============================================================
// CaptureThread.h — High-priority capture loop for RadukCapture
// ============================================================

#include "ASICamera.h"
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
    std::atomic<uint64_t> recoveryResets{0}; // Track self-healing events
    std::atomic<int>      droppedFrames{0};
    std::atomic<uint64_t> bufferOverruns{0};
    std::atomic<double>   fps{0.0};
    std::atomic<uint64_t> lastFrameTimestamp{0};
};

class CaptureThread {
public:
    CaptureThread() = default;
    ~CaptureThread() { Stop(); }

    CaptureThread(const CaptureThread&) = delete;
    CaptureThread& operator=(const CaptureThread&) = delete;

    bool Start(ASICamera& camera, RingBuffer& ringBuffer, double exposureMs, int cpuCore = -1);
    void Stop();
    bool IsRunning() const { return m_running.load(std::memory_order_relaxed); }
    const CaptureStats& Stats() const { return m_stats; }
    void SetExposure(double milliseconds) { m_exposureMs.store(milliseconds, std::memory_order_relaxed); }

private:
    void Run();

    ASICamera* m_camera = nullptr;
    RingBuffer* m_ringBuffer = nullptr;
    std::atomic<double>     m_exposureMs{8.0};
    int                     m_cpuCore = -1;

    std::thread             m_thread;
    std::atomic<bool>       m_running{false};
    CaptureStats            m_stats;

    // Recovery State
    int                     m_consecutiveTimeouts = 0;
    const int               m_maxTimeoutsBeforeReset = 20;

    uint64_t                m_fpsStartTime = 0;
    uint64_t                m_fpsFrameCount = 0;
    static constexpr double FPS_UPDATE_INTERVAL_SEC = 0.5;
};

} // namespace raduk::core
