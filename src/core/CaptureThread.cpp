// ============================================================
// CaptureThread.cpp — High-priority capture loop for RadukCapture
// Section 6.2 of Architecture Doc
// ============================================================

#include "CaptureThread.h"
#include <cstdio>
#include <thread>
#include <chrono>

namespace raduk::core {

bool CaptureThread::Start(ASICamera& camera, RingBuffer& ringBuffer,
                           double exposureMs, int cpuCore) {
    if (m_running.load()) {
        printf("[CaptureThread] Already running\n");
        return false;
    }

    if (!camera.IsOpen()) {
        printf("[CaptureThread] Camera not open\n");
        return false;
    }

    if (!ringBuffer.IsAllocated()) {
        printf("[CaptureThread] Ring buffer not allocated\n");
        return false;
    }

    m_camera = &camera;
    m_ringBuffer = &ringBuffer;
    m_exposureMs.store(exposureMs, std::memory_order_relaxed);
    m_cpuCore = cpuCore;

    // Reset stats
    m_stats.framesCaptured.store(0, std::memory_order_relaxed);
    m_stats.timeouts.store(0, std::memory_order_relaxed);
    m_stats.errors.store(0, std::memory_order_relaxed);
    m_stats.droppedFrames.store(0, std::memory_order_relaxed);
    m_stats.bufferOverruns.store(0, std::memory_order_relaxed);
    m_stats.fps.store(0.0, std::memory_order_relaxed);
    m_stats.lastFrameTimestamp.store(0, std::memory_order_relaxed);
    m_fpsStartTime = 0;
    m_fpsFrameCount = 0;

    // Start video capture on the camera
    if (!m_camera->StartVideoCapture()) {
        printf("[CaptureThread] Failed to start video capture\n");
        return false;
    }

    // Launch the thread
    m_running.store(true, std::memory_order_release);
    m_thread = std::thread(&CaptureThread::Run, this);

    printf("[CaptureThread] Started (exposure=%.2fms, core=%d)\n",
           exposureMs, cpuCore);
    return true;
}

void CaptureThread::Stop() {
    if (!m_running.load(std::memory_order_relaxed)) return;

    m_running.store(false, std::memory_order_release);

    if (m_thread.joinable()) {
        m_thread.join();
    }

    if (m_camera && m_camera->IsCapturing()) {
        m_camera->StopVideoCapture();
    }

    printf("[CaptureThread] Stopped — %llu frames captured, %llu timeouts, "
           "%d SDK drops, %llu overruns\n",
           m_stats.framesCaptured.load(),
           m_stats.timeouts.load(),
           m_stats.droppedFrames.load(),
           m_stats.bufferOverruns.load());
}

void CaptureThread::Run() {
    // Pin to dedicated CPU core for consistent latency
    if (m_cpuCore >= 0) {
        SetThreadAffinityMask(GetCurrentThread(), (1ULL << m_cpuCore));
    }

    // Elevate to time-critical priority — capture must never be preempted
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    // Get current ROI info for frame metadata
    int roiWidth = 0, roiHeight = 0, roiBin = 0;
    ASI_IMG_TYPE roiType = ASI_IMG_RAW8;
    m_camera->GetROI(roiWidth, roiHeight, roiBin, roiType);

    int frameSizeBytes = ASICamera::FrameSize(roiWidth, roiHeight, roiType);
    uint8_t bitDepth = (roiType == ASI_IMG_RAW16) ? 16 : 8;

    // Initialize FPS counter
    m_fpsStartTime = Timestamp::Now();
    m_fpsFrameCount = 0;

    printf("[CaptureThread] Capture loop running — %dx%d %s (%d bytes/frame)\n",
           roiWidth, roiHeight,
           (roiType == ASI_IMG_RAW16) ? "RAW16" : "RAW8",
           frameSizeBytes);

    // Phase 58 (review C4 / Architecture §6.3): pipeline-wedge detection.
    // 20 consecutive timeouts (~500ms of no frames at planetary exposures)
    // triggers RestartCapture (stop → 100ms USB settle → start). A removed
    // camera (immediate SDK error) backs off instead of busy-spinning this
    // TIME_CRITICAL thread, retries the restart each second, and flags
    // cameraFault for the UI until frames flow again.
    constexpr int WEDGE_TIMEOUT_COUNT = 20;
    int  consecTimeouts = 0;
    bool faultLogged = false;

    while (m_running.load(std::memory_order_relaxed)) {
        // Calculate timeout: 2x exposure + 500ms safety margin
        double expMs = m_exposureMs.load(std::memory_order_relaxed);
        int timeoutMs = static_cast<int>(expMs * 2.0) + 500;

        // Acquire a slot from the ring buffer
        FrameSlot* slot = m_ringBuffer->AcquireWriteSlot();
        if (!slot) {
            // Every slot busy (readers mid-frame). Brief yield instead of a
            // TIME_CRITICAL busy-spin; the SDK buffers the frame meanwhile.
            m_stats.bufferOverruns.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // Pull a frame from the SDK
        ASI_ERROR_CODE frameErr = ASI_SUCCESS;
        bool gotFrame = m_camera->GetVideoFrameEx(slot->data,
                                                   static_cast<int>(slot->bufferCapacity),
                                                   timeoutMs, frameErr);

        if (gotFrame) {
            consecTimeouts = 0;
            if (m_stats.cameraFault.exchange(false))
                printf("[CaptureThread] Camera recovered — frames flowing again\n");
            faultLogged = false;

            // Fill metadata
            slot->dataSize = frameSizeBytes;
            slot->timestamp = Timestamp::Now();
            slot->frameNumber = m_stats.framesCaptured.load(std::memory_order_relaxed);
            slot->width = roiWidth;
            slot->height = roiHeight;
            slot->bitDepth = bitDepth;
            slot->imgType = roiType;

            // Hand it to the recorder BEFORE committing to the ring.
            //
            // This thread owns the slot at this moment, so the sink can
            // copy from it safely; and this is the only place that sees
            // every frame. The render loop takes the LATEST frame once
            // per pass, which is right for a preview and wrong for a
            // recording -- it capped recorded fps at the loop rate and
            // silently dropped everything in between.
            if (m_frameSink) m_frameSink(slot);

            // Commit the frame to the ring buffer
            m_ringBuffer->CommitWrite(slot);
            m_stats.framesCaptured.fetch_add(1, std::memory_order_relaxed);
            m_stats.lastFrameTimestamp.store(slot->timestamp, std::memory_order_relaxed);

            // Update FPS measurement
            m_fpsFrameCount++;
            uint64_t now = slot->timestamp;
            double elapsed = Timestamp::ElapsedSec(m_fpsStartTime, now);
            if (elapsed >= FPS_UPDATE_INTERVAL_SEC) {
                double measuredFps = static_cast<double>(m_fpsFrameCount) / elapsed;
                m_stats.fps.store(measuredFps, std::memory_order_relaxed);
                m_fpsStartTime = now;
                m_fpsFrameCount = 0;
            }
        } else {
            // Frame not received — release the slot
            m_ringBuffer->ReleaseWrite(slot);

            if (frameErr == ASI_ERROR_CAMERA_REMOVED ||
                frameErr == ASI_ERROR_CAMERA_CLOSED) {
                // USB drop: the SDK call returns IMMEDIATELY, so back off
                // instead of spinning, and retry a restart each second.
                m_stats.errors.fetch_add(1, std::memory_order_relaxed);
                m_stats.cameraFault.store(true, std::memory_order_relaxed);
                if (!faultLogged) {
                    faultLogged = true;
                    printf("[CaptureThread] *** CAMERA FAULT: %s — attempting "
                           "recovery every 1s ***\n",
                           ASICamera::ErrorString(frameErr));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                if (m_running.load(std::memory_order_relaxed) &&
                    m_camera->RestartCapture()) {
                    printf("[CaptureThread] RestartCapture succeeded after fault\n");
                    m_stats.restarts.fetch_add(1, std::memory_order_relaxed);
                }
            } else {
                // Timeout — count toward §6.3 pipeline-wedge detection
                m_stats.timeouts.fetch_add(1, std::memory_order_relaxed);
                if (++consecTimeouts >= WEDGE_TIMEOUT_COUNT) {
                    consecTimeouts = 0;
                    printf("[CaptureThread] Pipeline wedge (%d consecutive "
                           "timeouts) — RestartCapture\n", WEDGE_TIMEOUT_COUNT);
                    m_stats.restarts.fetch_add(1, std::memory_order_relaxed);
                    if (!m_camera->RestartCapture())
                        m_stats.cameraFault.store(true, std::memory_order_relaxed);
                }
            }
        }

        // Check for SDK-reported dropped frames
        int drops = m_camera->GetDroppedFrames();
        m_stats.droppedFrames.store(drops, std::memory_order_relaxed);
    }
}

} // namespace raduk::core
