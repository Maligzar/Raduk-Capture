#pragma once
// ============================================================
// CentroidTracker.h — Phase 6.5: Three-Stage Centroid Acquisition
//
// Replaces the single PLANET LOCK toggle with an explicit
// three-stage acquisition workflow:
//
//   Stage 1 — SEARCHING
//     Full frame, no corrections.  FrameAnalyzer hunts for the
//     brightest object.  Requires STABLE_FRAMES (5) consecutive
//     valid centroids before the LOCK button activates.
//
//   Stage 2 — CENTERING
//     Slow proportional corrections pull planet toward centre.
//     Aggressiveness 20%, max pulse 200ms, 2Hz.
//     Dwell condition: centroid within CENTRE_THRESHOLD_PX (10px)
//     for DWELL_FRAMES (8) consecutive frames → ROI button activates.
//
//   Stage 3 — LOCKED
//     Caller switches sensor ROI to 1280x960 centred on planet.
//     Fine guiding: aggressiveness 50%, max pulse 500ms, 4Hz.
//
// THREAD SAFETY:
//   All shared state passes through a single m_mutex.
//   Correction thread reads only via Snapshot.
//   Stage transitions always happen on the main thread.
//
// SAFETY:
//   Centroid lost at any stage → m_lostPending → main calls Reset()
//   Mount jog → SuspendCorrections() / ResumeCorrections()
//   ESC → Reset()
// ============================================================

#include "IMount.h"
#include <windows.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace raduk::core {

// ── Acquisition stage ─────────────────────────────────────────
enum class AcquisitionStage {
    Off,        // nothing active
    Searching,  // centroid detection on, no corrections
    Centering,  // slow corrections pulling planet to centre
    Locked      // fine guiding active; ROI optionally cropped
};

inline const char* AcquisitionStageName(AcquisitionStage s) {
    switch (s) {
        case AcquisitionStage::Off:       return "OFF";
        case AcquisitionStage::Searching: return "SEARCHING";
        case AcquisitionStage::Centering: return "CENTERING";
        case AcquisitionStage::Locked:    return "LOCKED";
    }
    return "OFF";
}

class CentroidTracker {
public:
    static constexpr float PLATE_SCALE_ARCSEC_PX = 0.203f;

    // Stage 2 — Centering
    static constexpr float S2_AGGRESSIVENESS  = 0.20f;
    static constexpr int   S2_MAX_PULSE_MS    = 200;
    static constexpr float S2_DEADBAND_ARCSEC = 1.0f;
    static constexpr int   S2_RATE_MS         = 500;  // 2Hz

    // Stage 3 — Locked
    static constexpr float S3_AGGRESSIVENESS  = 0.50f;
    static constexpr int   S3_MAX_PULSE_MS    = 500;
    static constexpr float S3_DEADBAND_ARCSEC = 2.0f;
    static constexpr int   S3_RATE_MS         = 250;  // 4Hz

    // Transition thresholds
    static constexpr int   STABLE_FRAMES       = 5;
    static constexpr float CENTRE_THRESHOLD_PX = 10.0f;
    static constexpr int   DWELL_FRAMES        = 8;
    static constexpr int   CENTROID_TIMEOUT_MS = 2000;

    CentroidTracker() = default;
    ~CentroidTracker() { Reset(); }
    CentroidTracker(const CentroidTracker&) = delete;
    CentroidTracker& operator=(const CentroidTracker&) = delete;

    // ── Setup ─────────────────────────────────────────────────
    void SetMount(IMount* mount) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_mount = mount;
    }

    void SetLostCallback(std::function<void()> cb)            { m_onLost  = std::move(cb); }
    void SetDwellCallback(std::function<void()> cb)           { m_onDwell = std::move(cb); }
    void SetDriftCallback(std::function<void(float,float)> cb){ m_onDrift = std::move(cb); }

    // ── Stage control — main thread only ─────────────────────
    AcquisitionStage Stage() const { return m_stage.load(); }

    void StartSearching() {
        if (m_stage.load() != AcquisitionStage::Off) return;
        _ResetCounters();
        _StartThread();
        m_stage.store(AcquisitionStage::Searching);
        printf("[CentroidTracker] Stage -> SEARCHING\n");
    }

    // Only callable once IsStable() is true
    void StartCentering() {
        if (m_stage.load() != AcquisitionStage::Searching) return;
        if (!IsStable()) return;
        m_dwellCount = 0;
        m_stage.store(AcquisitionStage::Centering);
        printf("[CentroidTracker] Stage -> CENTERING\n");
    }

    // Only callable once IsDwellMet() is true
    void StartLocked() {
        if (m_stage.load() != AcquisitionStage::Centering) return;
        if (!IsDwellMet()) return;
        m_stage.store(AcquisitionStage::Locked);
        printf("[CentroidTracker] Stage -> LOCKED\n");
    }

    // Reset all stages back to Off — safe from any state
    void Reset() {
        m_stage.store(AcquisitionStage::Off);
        _StopThread();
        _ResetCounters();
        m_driftRA.store(0.0f);
        m_driftDec.store(0.0f);
        m_suspended.store(false);
        printf("[CentroidTracker] Reset -> OFF\n");
    }

    // ── State queries ─────────────────────────────────────────
    bool IsStable()   const { return m_stableCount >= STABLE_FRAMES;  }
    bool IsDwellMet() const { return m_dwellCount  >= DWELL_FRAMES;   }
    int  StableCount() const { return m_stableCount; }
    int  DwellCount()  const { return m_dwellCount;  }

    // Legacy: code that checked IsRunning() gets true during Centering or Locked
    bool IsRunning() const {
        auto s = m_stage.load();
        return s == AcquisitionStage::Centering || s == AcquisitionStage::Locked;
    }

    // ── Jog suspend ───────────────────────────────────────────
    void SuspendCorrections() { m_suspended.store(true);  }
    void ResumeCorrections()  { m_suspended.store(false); }

    // ── Feed from main thread each frame ─────────────────────
    // Returns true when a stage-transition threshold is first met.
    // Stage 1: returns true at exactly STABLE_FRAMES consecutive.
    // Stage 2: returns true at exactly DWELL_FRAMES consecutive in-threshold.
    bool SubmitCentroid(float cx, float cy, uint32_t fw, uint32_t fh) {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_centroidX        = cx;
            m_centroidY        = cy;
            m_frameW           = fw;
            m_frameH           = fh;
            m_centroidValid    = true;
            m_centroidEverSeen = true;
            m_lastCentroidTime = std::chrono::steady_clock::now();
        }

        auto stage = m_stage.load();

        if (stage == AcquisitionStage::Searching) {
            if (m_stableCount < STABLE_FRAMES) m_stableCount++;
            return (m_stableCount == STABLE_FRAMES);
        }

        if (stage == AcquisitionStage::Centering) {
            float dxPx = cx - fw * 0.5f;
            float dyPx = cy - fh * 0.5f;
            float distPx = std::sqrt(dxPx*dxPx + dyPx*dyPx);
            if (distPx <= CENTRE_THRESHOLD_PX) {
                if (m_dwellCount < DWELL_FRAMES) m_dwellCount++;
                return (m_dwellCount == DWELL_FRAMES);
            } else {
                m_dwellCount = 0;
            }
        }

        return false;
    }

    void SubmitCentroidLost() {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_centroidValid = false;
        // Reset counters on lost — need fresh run of consecutive frames
        m_stableCount = 0;
        m_dwellCount  = 0;
    }

    // ── Main loop dispatch (~500ms) ───────────────────────────
    void Tick() {
        if (m_lostPending.exchange(false))
            if (m_onLost)  m_onLost();
        if (m_dwellPending.exchange(false))
            if (m_onDwell) m_onDwell();
        auto stage = m_stage.load();
        if ((stage == AcquisitionStage::Centering ||
             stage == AcquisitionStage::Locked) && m_onDrift)
            m_onDrift(m_driftRA.load(), m_driftDec.load());
    }

    float DriftRA()  const { return m_driftRA.load();  }
    float DriftDec() const { return m_driftDec.load(); }

    // Last known centroid position — safe to read from main thread
    // Used by ROI crop callback before lastAnalysis is in scope
    float LastCentroidX()     const { std::lock_guard<std::mutex> lk(m_mutex); return m_centroidX; }
    float LastCentroidY()     const { std::lock_guard<std::mutex> lk(m_mutex); return m_centroidY; }
    bool  LastCentroidValid() const { std::lock_guard<std::mutex> lk(m_mutex); return m_centroidValid; }

private:
    struct Snapshot {
        float    cx = 0, cy = 0;
        uint32_t fw = 0, fh = 0;
        bool     valid = false, everSeen = false;
        std::chrono::steady_clock::time_point lastTime;
        IMount*  mount = nullptr;
    };

    Snapshot GetSnapshot() {
        std::lock_guard<std::mutex> lk(m_mutex);
        return { m_centroidX, m_centroidY, m_frameW, m_frameH,
                 m_centroidValid, m_centroidEverSeen,
                 m_lastCentroidTime, m_mount };
    }

    void _ResetCounters() {
        m_stableCount = 0;
        m_dwellCount  = 0;
        m_lostPending.store(false);
        m_dwellPending.store(false);
        std::lock_guard<std::mutex> lk(m_mutex);
        m_centroidValid    = false;
        m_centroidEverSeen = false;
    }

    void _StartThread() {
        if (m_threadRunning.load()) return;
        m_threadRunning.store(true);
        m_thread = std::thread(&CentroidTracker::Run, this);
    }

    void _StopThread() {
        if (!m_threadRunning.load()) return;
        m_threadRunning.store(false);
        if (m_thread.joinable()) m_thread.join();
    }

    void Run() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        printf("[CentroidTracker] Correction thread running\n");
        bool wasLost = false;

        while (m_threadRunning.load()) {
            auto stage = m_stage.load();

            // Sleep at stage-appropriate rate
            int sleepMs = (stage == AcquisitionStage::Locked) ? S3_RATE_MS : S2_RATE_MS;
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
            if (!m_threadRunning.load()) break;

            // No corrections in Searching or Off
            if (stage == AcquisitionStage::Searching ||
                stage == AcquisitionStage::Off) continue;

            if (m_suspended.load()) continue;

            Snapshot s = GetSnapshot();
            if (!s.mount || !s.mount->IsConnected()) continue;

            // Centroid timeout
            bool timedOut = !s.everSeen;
            if (s.everSeen) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - s.lastTime).count();
                timedOut = (elapsed > CENTROID_TIMEOUT_MS);
            }

            if (!s.valid || timedOut) {
                if (!wasLost) {
                    wasLost = true;
                    printf("[CentroidTracker] Centroid lost\n");
                    m_lostPending.store(true);
                }
                continue;
            }
            if (wasLost) { wasLost = false; printf("[CentroidTracker] Reacquired\n"); }
            if (s.fw == 0 || s.fh == 0) continue;

            float dxPx = s.cx - s.fw * 0.5f;
            float dyPx = s.cy - s.fh * 0.5f;
            float dRA  = dxPx * PLATE_SCALE_ARCSEC_PX;
            float dDec = dyPx * PLATE_SCALE_ARCSEC_PX;
            m_driftRA.store(dRA);
            m_driftDec.store(dDec);

            float agg      = (stage == AcquisitionStage::Locked) ? S3_AGGRESSIVENESS  : S2_AGGRESSIVENESS;
            int   maxPulse = (stage == AcquisitionStage::Locked) ? S3_MAX_PULSE_MS    : S2_MAX_PULSE_MS;
            float deadband = (stage == AcquisitionStage::Locked) ? S3_DEADBAND_ARCSEC : S2_DEADBAND_ARCSEC;

            constexpr float MS_PER_ARCSEC = 100.0f;

            auto sendPulse = [&](float drift, GuideDirection pos, GuideDirection neg,
                                 const char* axis) {
                float absd = std::abs(drift);
                if (absd <= deadband) return;
                if (!m_threadRunning.load()) return;
                int ms = (int)std::min(absd * agg * MS_PER_ARCSEC, (float)maxPulse);
                if (ms < 10) return;
                GuideDirection dir = (drift > 0) ? pos : neg;
                printf("[CentroidTracker] %s %.1f\" -> %dms\n", axis, drift, ms);
                s.mount->PulseGuide(dir, ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(ms + 50));
            };

            sendPulse(dRA,  GuideDirection::East,  GuideDirection::West,  "RA ");
            sendPulse(dDec, GuideDirection::North, GuideDirection::South, "Dec");
        }

        printf("[CentroidTracker] Correction thread stopped\n");
    }

    // ── Stage ─────────────────────────────────────────────────
    std::atomic<AcquisitionStage> m_stage{AcquisitionStage::Off};

    // ── Shared state — guarded by m_mutex ─────────────────────
    mutable std::mutex m_mutex;
    IMount*    m_mount            = nullptr;
    float      m_centroidX        = 0.0f;
    float      m_centroidY        = 0.0f;
    uint32_t   m_frameW           = 0;
    uint32_t   m_frameH           = 0;
    bool       m_centroidValid    = false;
    bool       m_centroidEverSeen = false;
    std::chrono::steady_clock::time_point m_lastCentroidTime;

    // ── Main-thread counters (no mutex — main thread only) ────
    int m_stableCount = 0;
    int m_dwellCount  = 0;

    // ── Thread ─────────────────────────────────────────────────
    std::thread       m_thread;
    std::atomic<bool> m_threadRunning{false};

    // ── Atomics ────────────────────────────────────────────────
    std::atomic<bool>            m_suspended{false};
    std::atomic<bool>            m_lostPending{false};
    std::atomic<bool>            m_dwellPending{false};
    std::atomic<float>           m_driftRA{0.0f};
    std::atomic<float>           m_driftDec{0.0f};

    // ── Main-thread callbacks ──────────────────────────────────
    std::function<void()>            m_onLost;
    std::function<void()>            m_onDwell;
    std::function<void(float,float)> m_onDrift;
};

} // namespace raduk::core
