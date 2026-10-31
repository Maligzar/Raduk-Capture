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
    // Phase 99: this was a THIRD hardcoded plate scale, read by
    // nothing. The rig's real one is RigConfig::plateScaleArcsecPx,
    // loaded from rig.ini, and the emulator deliberately runs 0.223 so
    // phase 51's guide calibration has a real error to measure. A
    // tracker that ever started using this constant would silently
    // ignore both.
    //
    // This project has been bitten by a stale literal twice already --
    // the emulator's blur per step, derived for a plate scale that
    // then moved, and the plate scale itself. Left as a pointer to
    // the real one rather than a fourth number to go stale.
    //   plate scale: raduk::core::RigConfig::Get().plateScaleArcsecPx

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
    void SetMount(IMount* mount);

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
    bool StartAutoAcquire(std::string& whyNot);

    bool AutoAdvancing() const { return m_autoAdvance.load(); }

    void StartSearching();

    // LOCK: only callable once IsStable() is true. Runs calibration first
    // (Phase 51) unless a fresh calibration already exists, in which case
    // it advances straight to Centering.
    void StartCalibration();

    // Direct entry to Centering — only valid with a measured calibration.
    // (The Calibrating stage transitions here itself on success.)
    void StartCentering();

    bool HasCalibration() const;
    GuideCalibration Calibration() const;
    void ClearCalibration();

    // Only callable once IsDwellMet() is true
    void StartLocked();

    // Reset all stages back to Off — safe from any state
    void Reset();

    // ── State queries ─────────────────────────────────────────
    bool IsStable()   const { return m_stableCount >= STABLE_FRAMES;  }
    bool IsDwellMet() const { return m_dwellCount  >= DWELL_FRAMES;   }
    int  StableCount() const { return m_stableCount; }
    int  DwellCount()  const { return m_dwellCount;  }

    // Legacy: code that checked IsRunning() gets true whenever the tracker
    // may be pulsing the mount (Calibrating, Centering or Locked).
    bool IsRunning() const;

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
    bool SubmitCentroid(float cx, float cy, uint32_t fw, uint32_t fh);

    // Phase 69: tooFaint distinguishes "the planet is dimmer than the
    // centroid gate" from "there is nothing in frame". They need
    // opposite responses -- raise exposure versus go and find it --
    // and acquisition used to stall identically for both, silently.
    void SubmitCentroidLost(bool tooFaint = false, int peak = -1);

    // ── Main loop dispatch (~500ms) ───────────────────────────
    void Tick();

    float DriftRA()  const { return m_driftRA.load();  }
    float DriftDec() const { return m_driftDec.load(); }

    // Last known centroid position — safe to read from main thread
    // Used by ROI crop callback before lastAnalysis is in scope
    float LastCentroidX()     const { std::lock_guard<std::mutex> lk(m_mutex); return m_centroidX; }
    float LastCentroidY()     const { std::lock_guard<std::mutex> lk(m_mutex); return m_centroidY; }
    bool  LastCentroidValid() const { std::lock_guard<std::mutex> lk(m_mutex); return m_centroidValid; }

    // Phase 69: why the last frame produced no centroid, phrased for
    // the user. Empty when there is nothing useful to add.
    std::string CentroidBlockReason() const;

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

    Snapshot GetSnapshot();

    void _ResetCounters();

    void _StartThread();

    void _StopThread();

    // ── Phase 51: calibration helpers (correction thread only) ──

    // True while the calibration may keep running.
    bool _CalAlive() const;

    // Average `samples` DISTINCT analyzer results (lastTime must change
    // between samples). Fails on timeout, loss, or calibration abort.
    bool _WaitCentroidAvg(int samples, float& outX, float& outY);

    // Interruptible settle sleep.
    bool _CalSettle(int ms);

    // Full calibration sequence. Runs on the correction thread while
    // stage == Calibrating. Transitions the stage itself:
    // success → Centering, failure/abort → Searching.
    void _RunCalibration(IMount* mount);

    void Run();

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
