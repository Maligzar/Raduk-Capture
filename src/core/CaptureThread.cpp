#include "CaptureThread.h"
#include <cstdio>
#include <windows.h>

namespace raduk::core {

bool CaptureThread::Start(ASICamera& camera, RingBuffer& ringBuffer, double exposureMs, int cpuCore) {
    if (m_running.load()) return false;
    if (!camera.IsOpen()) return false;
    if (!ringBuffer.IsAllocated()) return false;

    m_camera = &camera;
    m_ringBuffer = &ringBuffer;
    m_exposureMs.store(exposureMs, std::memory_order_relaxed);
    m_cpuCore = cpuCore;

    // Reset Stats
    m_stats.framesCaptured = 0; m_stats.timeouts = 0; m_stats.errors = 0;
    m_stats.recoveryResets = 0; m_stats.droppedFrames = 0; m_stats.bufferOverruns = 0;
    m_stats.fps = 0.0; m_consecutiveTimeouts = 0;

    if (!m_camera->StartVideoCapture()) return false;

    m_running.store(true, std::memory_order_release);
    m_thread = std::thread(&CaptureThread::Run, this);
    return true;
}

void CaptureThread::Stop() {
    if (!m_running.load()) return;
    m_running.store(false, std::memory_order_release);
    if (m_thread.joinable()) m_thread.join();
    if (m_camera) m_camera->StopVideoCapture();
}

void CaptureThread::Run() {
    if (m_cpuCore >= 0) SetThreadAffinityMask(GetCurrentThread(), (1ULL << m_cpuCore));
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    int roiW, roiH, roiB; ASI_IMG_TYPE roiT;
    m_camera->GetROI(roiW, roiH, roiB, roiT);
    int frameSize = ASICamera::FrameSize(roiW, roiH, roiT);
    uint8_t bitDepth = (roiT == ASI_IMG_RAW16) ? 16 : 8;

    m_fpsStartTime = Timestamp::Now();

    while (m_running.load(std::memory_order_relaxed)) {
        double expMs = m_exposureMs.load(std::memory_order_relaxed);
        int timeoutMs = static_cast<int>(expMs * 2.0) + 500;

        FrameSlot* slot = m_ringBuffer->AcquireWriteSlot();
        if (!slot) { m_stats.bufferOverruns++; continue; }

        if (m_camera->GetVideoFrame(slot->data, static_cast<int>(slot->bufferCapacity), timeoutMs)) {
            m_consecutiveTimeouts = 0; // Reset counter on success
            slot->dataSize = frameSize;
            slot->timestamp = Timestamp::Now();
            slot->frameNumber = m_stats.framesCaptured.load();
            slot->width = roiW; slot->height = roiH;
            slot->bitDepth = bitDepth; slot->imgType = roiT;

            m_ringBuffer->CommitWrite(slot);
            m_stats.framesCaptured++;
            m_stats.lastFrameTimestamp = slot->timestamp;

            m_fpsFrameCount++;
            double elapsed = Timestamp::ElapsedSec(m_fpsStartTime, slot->timestamp);
            if (elapsed >= FPS_UPDATE_INTERVAL_SEC) {
                m_stats.fps = static_cast<double>(m_fpsFrameCount) / elapsed;
                m_fpsStartTime = slot->timestamp; m_fpsFrameCount = 0;
            }
        } else {
            m_ringBuffer->ReleaseWrite(slot);
            m_stats.timeouts++;
            
            // --- AUTO-RECOVERY LOGIC ---
            m_consecutiveTimeouts++;
            if (m_consecutiveTimeouts >= m_maxTimeoutsBeforeReset) {
                printf("[CaptureThread] Pipeline wedged! Attempting auto-restart...\n");
                if (m_camera->RestartCapture()) m_stats.recoveryResets++;
                m_consecutiveTimeouts = 0;
            }
        }
        m_stats.droppedFrames = m_camera->GetDroppedFrames();
    }
}

} // namespace raduk::core
