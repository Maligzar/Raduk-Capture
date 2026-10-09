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
#include "RigConfig.h"
#include <windows.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <deque>
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
    // Nominal plate scale — display/seed only; corrections use the measured
    // Phase 51 calibration. Kept in sync with RigConfig at construction.
    static constexpr float PLATE_SCALE_ARCSEC_PX = 0.203f;

    // Stage 2 — Centering
    // Deadband must stay below CENTRE_THRESHOLD_PX on the real rig
    // (10px ≈ 2" at 0.203"/px) or dwell could never be reached.
    static constexpr float S2_AGGRESSIVENESS  = 0.20f;
    static constexpr int   S2_MAX_PULSE_MS    = 200;
    static constexpr float S2_DEADBAND_ARCSEC = 2.0f;
    static constexpr int   S2_RATE_MS         = 500;

    // Stage 3 — Locked
    static constexpr float S3_AGGRESSIVENESS  = 0.50f;
    static constexpr int   S3_MAX_PULSE_MS    = 500;
    static constexpr float S3_DEADBAND_ARCSEC = 2.5f;
    static constexpr int   S3_RATE_MS         = 250;

    // Phase 53 — correction dynamics.
    // Corrections act on the centroid AVERAGED over AVG_WINDOW_MS, not the
    // latest sample: raw planetary centroids jitter 2-5" with seeing, and a
    // proportional loop fed raw samples chases that noise. The deadbands
    // above apply to the averaged position.
    // Note on cadence: S2/S3_RATE_MS set the loop wake-up rate, but on the
    // NexStar backend PulseGuide blocks for the pulse duration, so a Locked
    // cycle that fires both axes takes up to ~S3_RATE_MS + 2×(maxPulse+50)ms
    // (~1.35s worst case) — the effective correction rate is ~1-4Hz, not a
    // fixed 4Hz.
    static constexpr int   AVG_WINDOW_MS   = 1000;
    static constexpr int   AVG_MIN_SAMPLES = 2;

    // Transition thresholds
    // Mirrors FrameAnalyzer::CENTROID_MIN_PEAK (20/255) as a percentage.
    static constexpr int   CENTROID_MIN_PEAK_PCT = 8;
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
    // sidereal). Only feeds the cosmetic arcsec/px estimate. Overridden by
    // RigConfig (Phase 55) at calibration time.
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
    // Phase 65: fired from Tick() when auto-acquire reaches Locked, so
    // the UI can say guiding is live. Not fired for a manual StartLocked
    // via the ROI button, which logs for itself.
    void SetAutoLockedCallback(std::function<void()> cb) { m_onAutoLocked = cb; }

    void SetCalibrationCallback(std::function<void(bool, const std::string&)> cb) {
        m_onCalibration = std::move(cb);
    }

    // ── Stage control — main thread only ─────────────────────
    AcquisitionStage Stage() const { return m_stage.load(); }

    // -- Phase 65: one-button acquisition -----------------------
    // Point, centre, track. Previously this took three button presses
    // (CENTROID, then LOCK once five centroids had accumulated, then
    // ROI once the dwell was met) and each silently did nothing when
    // its precondition was unmet, so a refusal was indistinguishable
    // from a dead button.
    //
    // Returns false with a reason the caller can show. The tracking
    // check is the important one: with the mount not driving, the
    // planet crosses the 10 px dwell window in 0.14 s, so Centering
    // can never converge and calibration measures about four times
    // more drift than pulse response. Every stage downstream fails,
    // and none of them can say why.
    bool StartAutoAcquire(std::string& whyNot) {
        if (m_stage.load() != AcquisitionStage::Off) {
            whyNot = "acquisition already running";
            return false;
        }
        IMount* mnt = nullptr;
        { std::lock_guard<std::mutex> lk(m_mutex); mnt = m_mount; }
        if (!mnt || !mnt->IsConnected()) {
            whyNot = "mount not connected";
            return false;
        }
        if (mnt->GetTrackingMode() == IMount::OFF) {
            whyNot = "mount is not tracking - align it on the hand control "
                     "first (Align > Quick Align needs no stars)";
            return false;
        }
        m_autoAdvance.store(true);
        StartSearching();
        printf("[CentroidTracker] Auto-acquire: searching for the planet\n");
        return true;
    }

    bool AutoAdvancing() const { return m_autoAdvance.load(); }

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
            auto now = std::chrono::steady_clock::now();
            m_centroidX        = cx;
            m_centroidY        = cy;
            m_frameW           = fw;
            m_frameH           = fh;
            m_centroidValid    = true;
            m_centroidEverSeen = true;
            m_lastCentroidTime = now;
            // Phase 53: rolling window for the averaged correction input
            m_samples.push_back({cx, cy, now});
            while (!m_samples.empty() &&
                   std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - m_samples.front().t).count() > AVG_WINDOW_MS)
                m_samples.pop_front();
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

    // Phase 69: tooFaint distinguishes "the planet is dimmer than the
    // centroid gate" from "there is nothing in frame". They need
    // opposite responses -- raise exposure versus go and find it --
    // and acquisition used to stall identically for both, silently.
    void SubmitCentroidLost(bool tooFaint = false, int peak = -1) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_lostTooFaint = tooFaint;
        m_lostPeak     = peak;
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

        // Phase 65: drive the sequence to completion without further
        // button presses. Done here on the main thread, where the other
        // stage transitions already live.
        if (m_autoAdvance.load()) {
            if (stage == AcquisitionStage::Searching && IsStable()) {
                printf("[CentroidTracker] Auto-acquire: centroid stable, "
                       "calibrating\n");
                StartCalibration();
            } else if (stage == AcquisitionStage::Centering && IsDwellMet()) {
                printf("[CentroidTracker] Auto-acquire: centred, locking\n");
                StartLocked();
                // Stop here rather than cropping the sensor. The ROI crop
                // changes capture resolution, which is the user's call --
                // auto-acquire's job is to centre and guide.
                m_autoAdvance.store(false);
                if (m_onAutoLocked) m_onAutoLocked();
            } else if (stage == AcquisitionStage::Off) {
                m_autoAdvance.store(false);   // Reset() ends the sequence
            }
            stage = m_stage.load();
        }

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

    // Phase 69: why the last frame produced no centroid, phrased for
    // the user. Empty when there is nothing useful to add.
    std::string CentroidBlockReason() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_centroidValid) return std::string();
        if (m_lostTooFaint) {
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "planet too faint to detect (frame peak %d%%, needs "
                     "above %d%%) - raise exposure or gain",
                     m_lostPeak >= 0 ? (m_lostPeak * 100 / 255) : 0,
                     CENTROID_MIN_PEAK_PCT);
            return std::string(buf);
        }
        return "no planet detected in frame";
    }

private:
    struct Snapshot {
        float    cx = 0, cy = 0;       // latest sample
        float    avgX = 0, avgY = 0;   // Phase 53: window average
        int      nAvg = 0;
        uint32_t fw = 0, fh = 0;
        bool     valid = false, everSeen = false;
        std::chrono::steady_clock::time_point lastTime;
        IMount*  mount = nullptr;
    };

    Snapshot GetSnapshot() {
        std::lock_guard<std::mutex> lk(m_mutex);
        Snapshot s;
        s.cx = m_centroidX;   s.cy = m_centroidY;
        s.fw = m_frameW;      s.fh = m_frameH;
        s.valid = m_centroidValid;
        s.everSeen = m_centroidEverSeen;
        s.lastTime = m_lastCentroidTime;
        s.mount = m_mount;
        if (!m_samples.empty()) {
            double sx = 0, sy = 0;
            for (const auto& p : m_samples) { sx += p.x; sy += p.y; }
            s.avgX = (float)(sx / m_samples.size());
            s.avgY = (float)(sy / m_samples.size());
            s.nAvg = (int)m_samples.size();
        }
        return s;
    }

    void _ResetCounters() {
        m_stableCount = 0;
        m_dwellCount  = 0;
        m_lostPending.store(false);
        m_dwellPending.store(false);
        std::lock_guard<std::mutex> lk(m_mutex);
        m_centroidValid    = false;
        m_centroidEverSeen = false;
        m_samples.clear();
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

        // Phase 65: one pulse per axis, and the displacement is KEPT.
        //
        // This used to pulse out, measure, then pulse back to the start --
        // four pulses and four settles for two measurements, with half the
        // motion deliberately undone. The return served "so the two axis
        // measurements share a similar working point (and the planet stays
        // in frame)", but a CAL_PULSE_MS pulse at the nominal guide rate
        // moves the planet about 30 px inside a 3840 px frame, so staying
        // in frame was never at risk, and each measurement is a delta that
        // does not care where it started.
        //
        // Dropping the return halves calibration (two pulses, two settles)
        // and leaves the planet displaced by at most two pulse lengths --
        // which Centering, entered immediately afterwards, corrects as part
        // of its normal work. Calibration stops being a detour.
        auto measureAxis = [&](GuideDirection dir,
                               float fromX, float fromY,
                               float& vx, float& vy,
                               float& endX, float& endY) -> bool {
            if (!_CalAlive()) return false;
            if (!mount->PulseGuide(dir, CAL_PULSE_MS)) return false;
            if (!_CalSettle(CAL_SETTLE_MS)) return false;
            if (!_WaitCentroidAvg(CAL_SAMPLES, endX, endY)) return false;
            vx = endX - fromX; vy = endY - fromY;
            return true;
        };

        float ex, ey, b2x, b2y;
        if (!measureAxis(GuideDirection::East, bx, by, ex, ey, b2x, b2y))
            return finish(false, "East pulse measurement failed or aborted");
        float eMag = std::sqrt(ex*ex + ey*ey);
        if (eMag < CAL_MIN_MOVE_PX)
            return finish(false, "East response too small — check guide rate/target");

        float nx, ny, b3x, b3y;
        if (!measureAxis(GuideDirection::North, b2x, b2y, nx, ny, b3x, b3y))
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
        // Phase 65: the plate scale is KNOWN from the optics
        // (206.265 x pixel_um / focal_mm) and sits in RigConfig. The guide
        // rate is the genuinely unknown quantity -- PulseGuide just asserts
        // NexStar fixed rate 1 is about 7.5"/s and nothing has ever checked.
        //
        // This previously assumed the rate and derived the scale, which is
        // backwards: it pinned a known number to an unverified one, so every
        // arcsecond the user reads scaled with that assumption. Take the
        // scale as given and derive the rate instead, then log it -- a
        // measured rate far from the assumed value immediately explains any
        // mis-sized pulse, instead of hiding inside the displayed figures.
        //
        // The correction solve is unaffected either way: it works in the
        // pixel domain from the measured px/ms matrix, where the rate
        // cancels out.
        cal.arcsecPerPx = RigConfig::Get().plateScaleArcsecPx;
        {
            float assumed  = RigConfig::Get().guideRateArcsecPerSec;
            float measured = (eMag * cal.arcsecPerPx)
                           / (CAL_PULSE_MS / 1000.0f);   // arcsec per second
            printf("[CentroidTracker] Guide rate: assumed %.2f\"/s, "
                   "measured %.2f\"/s (%.0f%% of assumed)\n",
                   assumed, measured,
                   assumed > 0.01f ? (measured / assumed * 100.0f) : 0.0f);
            if (assumed > 0.01f &&
                (measured < assumed * 0.5f || measured > assumed * 2.0f)) {
                printf("[CentroidTracker] WARNING: measured guide rate is far "
                       "from rig.ini GuideRateArcsecPerSec -- pulse sizing and "
                       "the drift readout will be off until it is corrected\n");
            }
        }
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

            // Phase 53: correct on the window-averaged centroid, not the
            // raw sample — and only once enough analyses have accumulated.
            if (s.nAvg < AVG_MIN_SAMPLES) continue;

            // ASCOM PulseGuide is asynchronous: don't stack a new correction
            // on top of one still executing. (NexStar pulses block in
            // PulseGuide itself; its IsPulseGuiding is always false.)
            if (s.mount->IsPulseGuiding()) continue;

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

            float dxPx = s.avgX - s.fw * 0.5f;
            float dyPx = s.avgY - s.fh * 0.5f;
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

            bool pulsed = false;
            auto sendPulse = [&](float tMs, GuideDirection pos, GuideDirection neg,
                                 const char* axis) {
                if (!m_threadRunning.load() || m_suspended.load()) return;
                float mag = std::min(std::abs(tMs), (float)maxPulse);
                int ms = (int)mag;
                if (ms < 10) return;
                GuideDirection dir = (tMs > 0) ? pos : neg;
                printf("[CentroidTracker] %s pulse %dms (err %.1f\")\n", axis, ms, errAs);
                s.mount->PulseGuide(dir, ms);
                pulsed = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            };

            sendPulse(tE, GuideDirection::East,  GuideDirection::West,  "E/W");
            sendPulse(tN, GuideDirection::North, GuideDirection::South, "N/S");

            // The averaging window now contains pre-move samples; clear it so
            // the next correction is computed purely from post-move analyses.
            if (pulsed) {
                std::lock_guard<std::mutex> lk(m_mutex);
                m_samples.clear();
            }
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
    bool       m_lostTooFaint     = false;  // last loss was a dim frame
    int        m_lostPeak         = -1;     // frame peak at that moment
    bool       m_centroidEverSeen = false;
    std::chrono::steady_clock::time_point m_lastCentroidTime;

    // Phase 53: rolling centroid window for averaged corrections
    struct CentroidSample { float x, y; std::chrono::steady_clock::time_point t; };
    std::deque<CentroidSample> m_samples;

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
    std::function<void()>        m_onAutoLocked;
    std::atomic<bool>            m_autoAdvance{false};
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
