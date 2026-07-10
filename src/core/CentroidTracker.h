#pragma once
// ============================================================
// CentroidTracker.h — Phase 6.5: Three-Stage Centroid Acquisition
//                     Phase 51: Guide Calibration & Axis Transform
//
// Acquisition workflow:
//
//   Stage 1 — SEARCHING
//     Full frame, no corrections.  FrameAnalyzer hunts for the
//     brightest object.  Requires STABLE_FRAMES (5) consecutive
//     valid centroids before the LOCK button activates.
//
//   Stage 1.5 — CALIBRATING (Phase 51)
//     Entered by LOCK.  PHD2-style: pulse East, measure the centroid
//     displacement vector; pulse back; pulse North, measure; pulse
//     back.  Derives the 2x2 pixel→pulse transform (and an effective
//     arcsec/px) from the measured response instead of assuming the
//     camera orientation, plate scale, or guide rate.  On success
//     auto-advances to CENTERING; on failure returns to SEARCHING.
//     A previous calibration younger than CAL_STALE_MIN is reused
//     (alt-az field rotation slowly invalidates the transform, so
//     calibration deliberately does NOT persist across sessions).
//
//   Stage 2 — CENTERING
//     Slow corrections pull planet toward centre using the measured
//     transform.  Dwell condition: centroid within CENTRE_THRESHOLD_PX
//     (10px) for DWELL_FRAMES (8) consecutive frames → ROI activates.
//
//   Stage 3 — LOCKED
//     Caller switches sensor ROI to 1280x960 centred on planet.
//     Fine guiding, same transform, higher aggressiveness.
//
// Corrections are solved purely in the pixel domain:
//   pulse durations (tE, tN) satisfy  M · (tE, tN) = −error_px · agg
// where M's columns are the measured East/North responses in px/ms.
// A negative duration flips to the opposite direction (West/South).
// On alt-az mounts the East (azimuth) column is rescaled by
// cos(alt_now)/cos(alt_cal) as altitude changes.
//
// THREAD SAFETY:
//   All shared state passes through a single m_mutex.
//   Correction thread reads only via Snapshot.
//   Stage transitions to/from Off happen on the main thread; the
//   Calibrating→Centering / Calibrating→Searching transitions happen
//   on the correction thread (main thread only observes them).
//
// SAFETY:
//   Centroid lost at any stage → m_lostPending → main calls Reset()
//   Mount jog → SuspendCorrections() (aborts an active calibration)
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
#include <string>
#include <algorithm>

namespace raduk::core {

// ── Acquisition stage ─────────────────────────────────────────
enum class AcquisitionStage {
    Off,         // nothing active
    Searching,   // centroid detection on, no corrections
    Calibrating, // measuring pulse→pixel response (Phase 51)
    Centering,   // slow corrections pulling planet to centre
    Locked       // fine guiding active; ROI optionally cropped
};

inline const char* AcquisitionStageName(AcquisitionStage s) {
    switch (s) {
        case AcquisitionStage::Off:         return "OFF";
        case AcquisitionStage::Searching:   return "SEARCHING";
        case AcquisitionStage::Calibrating: return "CALIBRATING";
        case AcquisitionStage::Centering:   return "CENTERING";
        case AcquisitionStage::Locked:      return "LOCKED";
    }
    return "OFF";
}

// ── Phase 51: measured guide calibration ──────────────────────
struct GuideCalibration {
    bool  valid = false;
    // Measured centroid response, px per ms of pulse
    float exPerMs = 0.0f, eyPerMs = 0.0f;  // East pulse
    float nxPerMs = 0.0f, nyPerMs = 0.0f;  // North pulse
    // Effective image scale derived from the East response and the
    // nominal guide rate — used only for deadbands and drift display;
    // corrections themselves stay in the pixel domain.
    float arcsecPerPx = 0.0f;
    // Altitude at calibration time (<0 = unknown) — used to rescale the
    // azimuth response by cos(alt_now)/cos(alt_cal) on alt-az mounts.
    float altDeg = -1.0f;
    std::chrono::steady_clock::time_point when{};
};

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

    // Phase 51 — calibration parameters
    static constexpr int   CAL_PULSE_MS        = 800;   // per calibration pulse
    static constexpr int   CAL_SAMPLES         = 4;     // centroids averaged per measurement
    static constexpr int   CAL_SETTLE_MS       = 700;   // post-pulse settle before measuring
    static constexpr int   CAL_STEP_TIMEOUT_MS = 8000;  // max wait for samples per step
    static constexpr float CAL_MIN_MOVE_PX     = 2.5f;  // min measurable pulse response
    static constexpr float CAL_MIN_SIN_ANGLE   = 0.25f; // axes must be >~15 deg apart
    static constexpr int   CAL_STALE_MIN       = 15;    // alt-az field rotation ages it out
    // Nominal on-sky guide rate behind PulseGuide (NexStar rate 1 ≈ 0.5x
    // sidereal). Only feeds the cosmetic arcsec/px estimate.
    static constexpr float GUIDE_RATE_AS_PER_S = 7.5f;

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
    // Fired from Tick() (main thread) when a calibration attempt finishes:
    // (success, human-readable summary/reason).
    void SetCalibrationCallback(std::function<void(bool, const std::string&)> cb) {
        m_onCalibration = std::move(cb);
    }

    // ── Stage control — main thread only ─────────────────────
    AcquisitionStage Stage() const { return m_stage.load(); }

    void StartSearching() {
        if (m_stage.load() != AcquisitionStage::Off) return;
        _ResetCounters();
        _StartThread();
        m_stage.store(AcquisitionStage::Searching);
        printf("[CentroidTracker] Stage -> SEARCHING\n");
    }

    // LOCK: only callable once IsStable() is true. Runs calibration first
    // (Phase 51) unless a fresh calibration already exists, in which case
    // it advances straight to Centering.
    void StartCalibration() {
        if (m_stage.load() != AcquisitionStage::Searching) return;
        if (!IsStable()) return;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (m_cal.valid) {
                auto ageMin = std::chrono::duration_cast<std::chrono::minutes>(
                    std::chrono::steady_clock::now() - m_cal.when).count();
                if (ageMin < CAL_STALE_MIN) {
                    m_dwellCount = 0;
                    m_stage.store(AcquisitionStage::Centering);
                    printf("[CentroidTracker] Reusing calibration (%lld min old) -> CENTERING\n",
                           (long long)ageMin);
                    return;
                }
                printf("[CentroidTracker] Calibration stale (%lld min) — recalibrating\n",
                       (long long)ageMin);
                m_cal.valid = false;
            }
        }
        m_dwellCount = 0;
        m_stage.store(AcquisitionStage::Calibrating);
        printf("[CentroidTracker] Stage -> CALIBRATING\n");
    }

    // Direct entry to Centering — only valid with a measured calibration.
    // (The Calibrating stage transitions here itself on success.)
    void StartCentering() {
        if (m_stage.load() != AcquisitionStage::Searching) return;
        if (!IsStable()) return;
        if (!HasCalibration()) {
            printf("[CentroidTracker] StartCentering refused — no calibration\n");
            return;
        }
        m_dwellCount = 0;
        m_stage.store(AcquisitionStage::Centering);
        printf("[CentroidTracker] Stage -> CENTERING\n");
    }

    bool HasCalibration() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_cal.valid;
    }
    GuideCalibration Calibration() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_cal;
    }
    void ClearCalibration() {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_cal = GuideCalibration{};
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

    // Legacy: code that checked IsRunning() gets true whenever the tracker
    // may be pulsing the mount (Calibrating, Centering or Locked).
    bool IsRunning() const {
        auto s = m_stage.load();
        return s == AcquisitionStage::Calibrating ||
               s == AcquisitionStage::Centering   ||
               s == AcquisitionStage::Locked;
    }

    // ── Jog suspend ───────────────────────────────────────────
    void SuspendCorrections() { m_suspended.store(true);  }
    void ResumeCorrections()  { m_suspended.store(false); }

    // ── Feed from main thread, ONCE PER NEW ANALYSIS RESULT ───
    // Do not re-submit a stale result: every call refreshes the
    // centroid-loss timeout, so re-submitting defeats the 2s safety
    // cutoff and inflates the stable/dwell counters.
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
            if (m_stableCount < STABLE_FRAMES) {
                m_stableCount++;
                return (m_stableCount == STABLE_FRAMES); // true exactly once
            }
            return false;
        }

        if (stage == AcquisitionStage::Centering) {
            float dxPx = cx - fw * 0.5f;
            float dyPx = cy - fh * 0.5f;
            float distPx = std::sqrt(dxPx*dxPx + dyPx*dyPx);
            if (distPx <= CENTRE_THRESHOLD_PX) {
                if (m_dwellCount < DWELL_FRAMES) {
                    m_dwellCount++;
                    if (m_dwellCount == DWELL_FRAMES) {
                        m_dwellPending.store(true); // Tick() fires m_onDwell
                        return true;                // true exactly once
                    }
                }
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
        if (m_calPending.exchange(false)) {
            if (m_onCalibration) {
                std::string msg;
                { std::lock_guard<std::mutex> lk(m_mutex); msg = m_calMessage; }
                m_onCalibration(m_calSuccess.load(), msg);
            }
        }
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

    // ── Phase 51: calibration helpers (correction thread only) ──

    // True while the calibration may keep running.
    bool _CalAlive() const {
        return m_threadRunning.load() &&
               m_stage.load() == AcquisitionStage::Calibrating &&
               !m_suspended.load();
    }

    // Average `samples` DISTINCT analyzer results (lastTime must change
    // between samples). Fails on timeout, loss, or calibration abort.
    bool _WaitCentroidAvg(int samples, float& outX, float& outY) {
        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds(CAL_STEP_TIMEOUT_MS);
        std::chrono::steady_clock::time_point lastSeen{};
        float sx = 0, sy = 0; int n = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            if (!_CalAlive()) return false;
            Snapshot s = GetSnapshot();
            if (s.valid && s.everSeen && s.lastTime != lastSeen) {
                lastSeen = s.lastTime;
                sx += s.cx; sy += s.cy; n++;
                if (n >= samples) { outX = sx / n; outY = sy / n; return true; }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return false;
    }

    // Interruptible settle sleep.
    bool _CalSettle(int ms) {
        for (int i = 0; i < ms / 50; i++) {
            if (!_CalAlive()) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return _CalAlive();
    }

    // Full calibration sequence. Runs on the correction thread while
    // stage == Calibrating. Transitions the stage itself:
    // success → Centering, failure/abort → Searching.
    void _RunCalibration(IMount* mount) {
        auto finish = [&](bool ok, const std::string& msg) {
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                m_calMessage = msg;
            }
            m_calSuccess.store(ok);
            m_calPending.store(true);
            // Only move the stage if we're still Calibrating (Reset wins)
            AcquisitionStage expected = AcquisitionStage::Calibrating;
            m_stage.compare_exchange_strong(expected,
                ok ? AcquisitionStage::Centering : AcquisitionStage::Searching);
            printf("[CentroidTracker] Calibration %s: %s\n",
                   ok ? "OK" : "FAILED", msg.c_str());
        };

        printf("[CentroidTracker] Calibration: measuring pulse response\n");

        float bx, by;
        if (!_WaitCentroidAvg(CAL_SAMPLES, bx, by))
            return finish(false, "no stable centroid to calibrate against");

        // A pulse + settle + measurement for one axis; returns response vector.
        auto measureAxis = [&](GuideDirection dir, GuideDirection back,
                               float fromX, float fromY,
                               float& vx, float& vy, float& endX, float& endY) -> bool {
            if (!_CalAlive()) return false;
            if (!mount->PulseGuide(dir, CAL_PULSE_MS)) return false;
            if (!_CalSettle(CAL_SETTLE_MS)) return false;
            float mx, my;
            if (!_WaitCentroidAvg(CAL_SAMPLES, mx, my)) return false;
            vx = mx - fromX; vy = my - fromY;
            // Pulse back toward the start so the two axis measurements share
            // a similar working point (and the planet stays in frame).
            if (!mount->PulseGuide(back, CAL_PULSE_MS)) return false;
            if (!_CalSettle(CAL_SETTLE_MS)) return false;
            if (!_WaitCentroidAvg(CAL_SAMPLES, endX, endY)) return false;
            return true;
        };

        float ex, ey, b2x, b2y;
        if (!measureAxis(GuideDirection::East, GuideDirection::West,
                         bx, by, ex, ey, b2x, b2y))
            return finish(false, "East pulse measurement failed or aborted");
        float eMag = std::sqrt(ex*ex + ey*ey);
        if (eMag < CAL_MIN_MOVE_PX)
            return finish(false, "East response too small — check guide rate/target");

        float nx, ny, b3x, b3y;
        if (!measureAxis(GuideDirection::North, GuideDirection::South,
                         b2x, b2y, nx, ny, b3x, b3y))
            return finish(false, "North pulse measurement failed or aborted");
        float nMag = std::sqrt(nx*nx + ny*ny);
        if (nMag < CAL_MIN_MOVE_PX)
            return finish(false, "North response too small — check guide rate/target");

        // Axes must be usefully independent: |sin(angle between them)|
        float cross = ex*ny - ey*nx;
        if (std::abs(cross) / (eMag * nMag) < CAL_MIN_SIN_ANGLE)
            return finish(false, "axis responses nearly parallel — bad calibration");

        GuideCalibration cal;
        cal.valid   = true;
        cal.exPerMs = ex / (float)CAL_PULSE_MS;
        cal.eyPerMs = ey / (float)CAL_PULSE_MS;
        cal.nxPerMs = nx / (float)CAL_PULSE_MS;
        cal.nyPerMs = ny / (float)CAL_PULSE_MS;
        cal.arcsecPerPx = (GUIDE_RATE_AS_PER_S * CAL_PULSE_MS / 1000.0f) / eMag;
        cal.when    = std::chrono::steady_clock::now();
        {
            double alt = 0, az = 0;
            if (mount->GetAltAz(alt, az)) cal.altDeg = (float)alt;
        }
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_cal = cal;
        }

        char buf[160];
        snprintf(buf, sizeof(buf),
                 "E=(%.2f,%.2f)px/s N=(%.2f,%.2f)px/s scale=%.2f\"/px alt=%.0f",
                 cal.exPerMs * 1000, cal.eyPerMs * 1000,
                 cal.nxPerMs * 1000, cal.nyPerMs * 1000,
                 cal.arcsecPerPx, cal.altDeg);
        finish(true, buf);
    }

    void Run() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        printf("[CentroidTracker] Correction thread running\n");
        bool wasLost = false;
        auto lastAltPoll = std::chrono::steady_clock::now()
                         - std::chrono::seconds(60);
        float lastAltDeg = -1.0f;

        while (m_threadRunning.load()) {
            auto stage = m_stage.load();

            // Sleep at stage-appropriate rate
            int sleepMs = (stage == AcquisitionStage::Locked) ? S3_RATE_MS : S2_RATE_MS;
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
            if (!m_threadRunning.load()) break;
            stage = m_stage.load();

            // No corrections in Searching or Off
            if (stage == AcquisitionStage::Searching ||
                stage == AcquisitionStage::Off) continue;

            // A jog mid-calibration aborts it (via _CalAlive) rather than
            // pausing — the measurement would be poisoned anyway.
            if (m_suspended.load()) continue;

            Snapshot s = GetSnapshot();
            if (!s.mount || !s.mount->IsConnected()) continue;

            // Phase 51: run the calibration sequence in-line
            if (stage == AcquisitionStage::Calibrating) {
                _RunCalibration(s.mount);
                continue;
            }

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

            GuideCalibration cal;
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                cal = m_cal;
            }
            if (!cal.valid) continue; // Centering/Locked require calibration

            // Occasionally refresh altitude for the cos(alt) azimuth rescale
            auto now = std::chrono::steady_clock::now();
            if (cal.altDeg >= 0 &&
                std::chrono::duration_cast<std::chrono::seconds>(now - lastAltPoll).count() >= 30) {
                lastAltPoll = now;
                double alt = 0, az = 0;
                if (s.mount->GetAltAz(alt, az)) lastAltDeg = (float)alt;
            }

            float dxPx = s.cx - s.fw * 0.5f;
            float dyPx = s.cy - s.fh * 0.5f;
            m_driftRA.store(dxPx * cal.arcsecPerPx);
            m_driftDec.store(dyPx * cal.arcsecPerPx);

            float agg      = (stage == AcquisitionStage::Locked) ? S3_AGGRESSIVENESS  : S2_AGGRESSIVENESS;
            int   maxPulse = (stage == AcquisitionStage::Locked) ? S3_MAX_PULSE_MS    : S2_MAX_PULSE_MS;
            float deadband = (stage == AcquisitionStage::Locked) ? S3_DEADBAND_ARCSEC : S2_DEADBAND_ARCSEC;

            float errAs = std::sqrt(dxPx*dxPx + dyPx*dyPx) * cal.arcsecPerPx;
            if (errAs <= deadband) continue;

            // Alt-az: azimuth (East) pixel response scales with cos(alt);
            // rescale the measured column to the current altitude.
            float exMs = cal.exPerMs, eyMs = cal.eyPerMs;
            if (cal.altDeg >= 0 && lastAltDeg >= 0) {
                float cCal = std::cos(cal.altDeg * 0.01745329f);
                float cNow = std::cos(lastAltDeg * 0.01745329f);
                if (cCal > 0.05f && cNow > 0.05f) {
                    float k = cNow / cCal;
                    exMs *= k; eyMs *= k;
                }
            }

            // Solve M·(tE,tN) = desired displacement (−error × agg), px.
            float det = exMs * cal.nyPerMs - cal.nxPerMs * eyMs;
            float eMag2 = exMs*exMs + eyMs*eyMs;
            float nMag2 = cal.nxPerMs*cal.nxPerMs + cal.nyPerMs*cal.nyPerMs;
            if (std::abs(det) < 1e-8f || eMag2 <= 0 || nMag2 <= 0) continue;

            float ddx = -dxPx * agg, ddy = -dyPx * agg;
            float tE = ( cal.nyPerMs * ddx - cal.nxPerMs * ddy) / det;
            float tN = (-eyMs        * ddx + exMs        * ddy) / det;

            auto sendPulse = [&](float tMs, GuideDirection pos, GuideDirection neg,
                                 const char* axis) {
                if (!m_threadRunning.load() || m_suspended.load()) return;
                float mag = std::min(std::abs(tMs), (float)maxPulse);
                int ms = (int)mag;
                if (ms < 10) return;
                GuideDirection dir = (tMs > 0) ? pos : neg;
                printf("[CentroidTracker] %s pulse %dms (err %.1f\")\n", axis, ms, errAs);
                s.mount->PulseGuide(dir, ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            };

            sendPulse(tE, GuideDirection::East,  GuideDirection::West,  "E/W");
            sendPulse(tN, GuideDirection::North, GuideDirection::South, "N/S");
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
    std::atomic<bool>            m_calPending{false};
    std::atomic<bool>            m_calSuccess{false};
    std::atomic<float>           m_driftRA{0.0f};
    std::atomic<float>           m_driftDec{0.0f};

    // ── Phase 51 calibration state — guarded by m_mutex ───────
    GuideCalibration m_cal;
    std::string      m_calMessage;

    // ── Main-thread callbacks ──────────────────────────────────
    std::function<void()>            m_onLost;
    std::function<void()>            m_onDwell;
    std::function<void(float,float)> m_onDrift;
    std::function<void(bool, const std::string&)> m_onCalibration;
};

} // namespace raduk::core
