// ============================================================
// CentroidTracker.cpp — Planet centroid tracking
// Out-of-line definitions for CentroidTracker.h.
// ============================================================

#include "CentroidTracker.h"
#include "GuideSolve.h"   // the pixel->pulse solve, shared with phase 47
#include "CoreLog.h"

namespace raduk::core {

void CentroidTracker::SetMount(IMount* mount) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_mount = mount;
}

bool CentroidTracker::StartAutoAcquire(std::string& whyNot) {
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
    CoreLog("[CentroidTracker] Auto-acquire: searching for the planet\n");
    return true;
}

bool CentroidTracker::PromoteToAutoAcquire(std::string& whyNot) {
    if (m_stage.load() != AcquisitionStage::Searching) {
        whyNot = "not searching";
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
    CoreLog("[CentroidTracker] Search promoted to auto-acquire\n");
    return true;
}

void CentroidTracker::StartSearching() {
    if (m_stage.load() != AcquisitionStage::Off) return;
    _ResetCounters();
    _StartThread();
    m_stage.store(AcquisitionStage::Searching);
    CoreLog("[CentroidTracker] Stage -> SEARCHING\n");
}

void CentroidTracker::StartCalibration() {
    if (m_stage.load() != AcquisitionStage::Searching) return;
    if (!IsStable()) return;
    // Always a fresh calibration, never a reused one (2026-11-12). Since
    // 2026-11-07 this app guides by taking the axes from the hand
    // control, and only a calibration takes them: it measures the
    // tracking rates here and now, which change with every GoTo. A
    // reused calibration went straight to CENTERING with the hand
    // control still driving, where the loop sends nothing, so after a
    // GoTo from the map the planet sat off centre and nothing moved it
    // (tests/tracking_emulator.cpp, step 4). The stored calibration is
    // kept for click-to-recentre and the jog arrows, which use it under
    // the hand control's tracking.
    m_dwellCount = 0;
    m_stage.store(AcquisitionStage::Calibrating);
    CoreLog("[CentroidTracker] Stage -> CALIBRATING\n");
}

void CentroidTracker::StartCentering() {
    if (m_stage.load() != AcquisitionStage::Searching) return;
    if (!IsStable()) return;
    // Same reason as StartCalibration: centring needs the axes, and
    // only a calibration takes them.
    if (!m_appDriving.load()) {
        CoreLog("[CentroidTracker] StartCentering refused - the app does not hold the axes; calibrate\n");
        return;
    }
    if (!HasCalibration()) {
        CoreLog("[CentroidTracker] StartCentering refused — no calibration\n");
        return;
    }
    m_dwellCount = 0;
    m_stage.store(AcquisitionStage::Centering);
    CoreLog("[CentroidTracker] Stage -> CENTERING\n");
}

bool CentroidTracker::HasCalibration() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_cal.valid;
}

GuideCalibration CentroidTracker::Calibration() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_cal;
}

void CentroidTracker::ClearCalibration() {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_cal = GuideCalibration{};
}

void CentroidTracker::StartLocked() {
    if (m_stage.load() != AcquisitionStage::Centering) return;
    if (!IsDwellMet()) return;
    m_stage.store(AcquisitionStage::Locked);
    CoreLog("[CentroidTracker] Stage -> LOCKED\n");
}

void CentroidTracker::Reset() {
    m_stage.store(AcquisitionStage::Off);
    _StopThread();
    {
        IMount* mnt = nullptr;
        { std::lock_guard<std::mutex> lk(m_mutex); mnt = m_mount; }
        _HandBack(mnt, "acquisition reset");
    }
    _ResetCounters();
    m_driftRA.store(0.0f);
    m_driftDec.store(0.0f);
    m_suspended.store(false);
    CoreLog("[CentroidTracker] Reset -> OFF\n");
}

bool CentroidTracker::IsRunning() const {
    auto s = m_stage.load();
    return s == AcquisitionStage::Calibrating ||
           s == AcquisitionStage::Centering   ||
           s == AcquisitionStage::Locked;
}

bool CentroidTracker::SubmitCentroid(float cx, float cy, uint32_t fw, uint32_t fh) {
    float avgX = cx, avgY = cy;
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
        double sx = 0, sy = 0;
        for (const auto& q : m_samples) { sx += q.x; sy += q.y; }
        avgX = (float)(sx / m_samples.size()); avgY = (float)(sy / m_samples.size());
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
        // The averaged position, as the guider steers by: single
        // centroids jitter with seeing and broke the streak every few
        // frames, so a centred planet never reached LOCKED (2026-11-07).
        float dxPx = avgX - fw * 0.5f;
        float dyPx = avgY - fh * 0.5f;
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

void CentroidTracker::SubmitCentroidLost(bool tooFaint, int peak) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_lostTooFaint = tooFaint;
    m_lostPeak     = peak;
    m_centroidValid = false;
    // Reset counters on lost — need fresh run of consecutive frames
    m_stableCount = 0;
    m_dwellCount  = 0;
}

void CentroidTracker::Tick() {
    if (m_lostPending.exchange(false))
        if (m_onLost)  m_onLost();
    if (m_dwellPending.exchange(false))
        if (m_onDwell) m_onDwell();
    if (m_abortPending.exchange(false) && m_onAbort) {
        std::string msg;
        { std::lock_guard<std::mutex> lk(m_mutex); msg = m_abortMessage; }
        m_onAbort(msg);
    }
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
            CoreLog("[CentroidTracker] Auto-acquire: centroid stable, "
                   "calibrating\n");
            StartCalibration();
        } else if (stage == AcquisitionStage::Centering && IsDwellMet()) {
            CoreLog("[CentroidTracker] Auto-acquire: centred, locking\n");
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

std::string CentroidTracker::CentroidBlockReason() const {
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

CentroidTracker::Snapshot CentroidTracker::GetSnapshot() {
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

void CentroidTracker::_ResetCounters() {
    m_stableCount = 0;
    m_dwellCount  = 0;
    m_lostPending.store(false);
    m_dwellPending.store(false);
    std::lock_guard<std::mutex> lk(m_mutex);
    m_centroidValid    = false;
    m_centroidEverSeen = false;
    m_samples.clear();
}

void CentroidTracker::_StartThread() {
    if (m_threadRunning.load()) return;
    m_threadRunning.store(true);
    m_thread = std::thread(&CentroidTracker::Run, this);
}

void CentroidTracker::_StopThread() {
    if (!m_threadRunning.load()) return;
    m_threadRunning.store(false);
    if (m_thread.joinable()) m_thread.join();
}

bool CentroidTracker::_CalAlive() const {
    return m_threadRunning.load() &&
           m_stage.load() == AcquisitionStage::Calibrating &&
           !m_suspended.load();
}

bool CentroidTracker::_WaitCentroidAvg(int samples, float& outX, float& outY) {
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
            if (n >= samples) {
                outX = sx / n; outY = sy / n;
                CoreLog("[CAL] sample (%.1f, %.1f) from %d analyses\n", outX, outY, n);
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

bool CentroidTracker::_CalSettle(int ms) {
    for (int i = 0; i < ms / 50; i++) {
        if (!_CalAlive()) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return _CalAlive();
}

// ── The app drives the axes while it guides (2026-11-07) ───────
//
// Measured at the rig, in this order:
//  - a fixed-rate pulse REPLACES the axis's tracking; at rate 1 it is
//    slower than azimuth tracking, so East and West moved the planet
//    the same way;
//  - a variable-rate command shares its register with the hand
//    control's tracking: an altitude command sent ~100 ms after
//    tracking was re-enabled was overwritten at once (encoder moved
//    -6.3" for +26" commanded).
// So while it guides, this app switches the hand control's tracking
// OFF and drives both axes itself: each at its measured tracking rate
// plus a correction from the image, refreshed several times a second.
// Nothing can overwrite it, there are no pulses and no stops, and
// whatever ends guiding hands tracking back (_HandBack).

static double WrapDeg180(double d) {
    while (d > 180.0) d -= 360.0;
    while (d < -180.0) d += 360.0;
    return d;
}

bool CentroidTracker::_ReadAxes(IMount* mount, double& azDeg, double& altDeg,
                                std::chrono::steady_clock::time_point& t) {
    double alt = 0, az = 0;
    if (!mount->GetAltAz(alt, az)) return false;
    azDeg = az; altDeg = alt; t = std::chrono::steady_clock::now();
    return true;
}

// Both axes at these encoder rates (arcsec/s, signed as GetAltAz).
void CentroidTracker::_DriveAxes(IMount* mount, float azRate, float altRate) {
    float sAz, sAlt;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        sAz = m_axisSign[0]; sAlt = m_axisSign[1];
    }
    // Never exactly zero: a zero variable rate is a stop, and a stop
    // re-asserts the hand control's tracking (NexStarMount's sender).
    auto noZero = [](float v) { return std::fabs(v) < 0.3f ? (v < 0.0f ? -0.3f : 0.3f) : v; };
    const float c0 = noZero(azRate * sAz), c1 = noZero(altRate * sAlt);
    // Never after a hand-back: a GoTo or a stop may have taken the mount
    // a moment ago (YieldMount), and one late rate command would fight it.
    std::lock_guard<std::mutex> dk(m_driveMx);
    if (!m_appDriving.load()) return;
    mount->SlewVariable(0, c0 >= 0.0f ? 0 : 1, std::fabs(c0));
    mount->SlewVariable(1, c1 >= 0.0f ? 0 : 1, std::fabs(c1));
}

// Keep the mount tracking at the measured rates plus the integral (no
// proportional correction): what to do while the image cannot be trusted.
void CentroidTracker::_HoldRates(IMount* mount) {
    float a, b;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        a = m_baseRateAs[0] + m_integAs[0];
        b = m_baseRateAs[1] + m_integAs[1];
    }
    _DriveAxes(mount, a, b);
}

// Give tracking back to the hand control, if this app had taken it.
void CentroidTracker::_HandBack(IMount* mount, const char* why) {
    std::lock_guard<std::mutex> dk(m_driveMx);
    if (!m_appDriving.exchange(false)) return;
    if (!mount) return;
    const IMount::TrackingMode mode =
        (m_prevTrackMode == (int)IMount::OFF) ? IMount::ALT_AZ
                                              : (IMount::TrackingMode)m_prevTrackMode;
    // Stop the axes this app was driving before handing back. Turning
    // tracking on does not clear a rate command on every mount: the real
    // NexStar's hand control overwrites a guide-scale rate, but the
    // default emulator keeps it, and so may another driver. Without the
    // stop, a failed calibration left both axes slewing at tracking plus
    // the offset (about 26 and 15 "/s), the planet ran off the sensor and
    // nothing could find it again (2026-11-11, Venus in the emulator;
    // tests/rate_guide.cpp section 5).
    mount->StopSlew(0);
    mount->StopSlew(1);
    mount->SetTrackingMode(mode);
    CoreLog("[GUIDE] hand control tracking restored (%s): %s\n",
            IMount::TrackingModeName(mode), why);
}

// Kept for phase 47's click-to-recentre, which still nudges with the
// hand control tracking. While this app is driving, a nudge is an
// addition to the driven rates instead.
bool CentroidTracker::RatePulse(IMount* mount, GuideDirection dir, int ms) {
    if (!mount || ms < 10) return false;
    const int   axis = (dir == GuideDirection::East || dir == GuideDirection::West) ? 0 : 1;
    const float sgn  = (dir == GuideDirection::East || dir == GuideDirection::North) ? 1.0f : -1.0f;
    float base[2];
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_ratesValid) return false;
        base[0] = m_baseRateAs[0]; base[1] = m_baseRateAs[1];
    }
    float r[2] = { base[0], base[1] };
    r[axis] += sgn * GUIDE_OFFSET_AS;
    if (m_appDriving.load()) {
        _DriveAxes(mount, r[0], r[1]);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        _DriveAxes(mount, base[0], base[1]);
        return true;
    }
    float sgnAxis;
    { std::lock_guard<std::mutex> lk(m_mutex); sgnAxis = m_axisSign[axis]; }
    const float c = r[axis] * sgnAxis;
    if (!mount->SlewVariable(axis, c >= 0.0f ? 0 : 1, std::fabs(c))) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    mount->StopSlew(axis);
    return true;
}

void CentroidTracker::_RunCalibration(IMount* mount) {
    auto finish = [&](bool ok, const std::string& msg) {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_calMessage = msg;
        }
        if (!ok) {
            _HandBack(mount, "calibration failed");
            m_autoAdvance.store(false);
        }
        m_calSuccess.store(ok);
        m_calPending.store(true);
        AcquisitionStage expected = AcquisitionStage::Calibrating;
        m_stage.compare_exchange_strong(expected,
            ok ? AcquisitionStage::Centering : AcquisitionStage::Searching);
        CoreLog("[CentroidTracker] Calibration %s: %s\n",
               ok ? "OK" : "FAILED", msg.c_str());
    };

    CoreLog("[CentroidTracker] Calibration: app-driven axes\n");
    using clk = std::chrono::steady_clock;
    auto secs = [](clk::time_point a, clk::time_point b) {
        return std::chrono::duration<float>(b - a).count();
    };

    // 1. The hand control's tracking, measured: image drift and each
    //    axis's rate from the encoders.
    const IMount::TrackingMode prev = mount->GetTrackingMode();
    if (prev == IMount::OFF)
        return finish(false, "the mount is not tracking - turn tracking on first");
    float b0x, b0y;
    if (!_WaitCentroidAvg(CAL_SAMPLES, b0x, b0y))
        return finish(false, "no stable centroid to calibrate against");
    // 2026-11-08: calibration started with Saturn 348 px from the top of
    // the frame; its moves (~300 px) took it to 62 px, and the operator
    // had to jog to keep it, which aborted the run. Refuse up front.
    {
        uint32_t fw = 0, fh = 0;
        { std::lock_guard<std::mutex> lk(m_mutex); fw = m_frameW; fh = m_frameH; }
        const float mx = fw * CAL_EDGE_MARGIN, my = fh * CAL_EDGE_MARGIN;
        if (fw && fh && (b0x < mx || b0x > fw - mx || b0y < my || b0y > fh - my)) {
            char why[200];
            snprintf(why, sizeof(why),
                     "the planet is near the edge of the frame (%.0f, %.0f of %ux%u) and "
                     "calibration moves it a few hundred pixels - centre it roughly, then press again",
                     b0x, b0y, fw, fh);
            return finish(false, why);
        }
    }
    // Offset length scaled to the frame: at most CAL_OFFSET_FRAC of its
    // shorter side, so a 640x480 box (about 128" x 96") is not walked off
    // by 40" offsets. The full sensor keeps CAL_OFFSET_MS.
    int offMs = CAL_OFFSET_MS;
    {
        uint32_t fw = 0, fh = 0;
        { std::lock_guard<std::mutex> lk(m_mutex); fw = m_frameW; fh = m_frameH; }
        if (fw && fh) {
            const float arc = CAL_OFFSET_FRAC * (float)(std::min)(fw, fh)
                            * RigConfig::Get().plateScaleArcsecPx;
            offMs = (int)(arc / GUIDE_OFFSET_AS * 1000.0f);
            offMs = (std::max)(CAL_OFFSET_MIN_MS, (std::min)(CAL_OFFSET_MS, offMs));
        }
    }
    double az0, alt0; clk::time_point te0;
    if (!_ReadAxes(mount, az0, alt0, te0)) return finish(false, "the mount did not report its position");
    if (!_CalSettle(CAL_DRIFT_MS)) return finish(false, "aborted while measuring tracking");
    double az1, alt1; clk::time_point te1;
    if (!_ReadAxes(mount, az1, alt1, te1)) return finish(false, "the mount did not report its position");
    const float encDt = secs(te0, te1);
    const float rAz  = (float)(WrapDeg180(az1 - az0) * 3600.0 / encDt);
    const float rAlt = (float)((alt1 - alt0) * 3600.0 / encDt);
    CoreLog("[CAL] hand control tracking: az %.2f\"/s alt %.2f\"/s (encoders, %.1f s)\n",
            rAz, rAlt, encDt);
    if (std::fabs(rAz) < 0.5f && std::fabs(rAlt) < 0.5f)
        return finish(false, "the mount's axes are not moving - turn tracking on first");

    // 2. Take the axes. Both are driven for DIR_CHECK_MS in the mount's
    //    "direction 0"; the encoders say which way that is per axis.
    //    Then both straight to their tracking rates.
    m_prevTrackMode = (int)prev;
    m_appDriving.store(true);
    mount->SetTrackingMode(IMount::OFF);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_axisSign[0] = m_axisSign[1] = 1.0f;
    }
    // One offset above tracking (it was two): about 10" of image motion.
    const float s0 = std::fabs(rAz) + GUIDE_OFFSET_AS;
    const float s1 = std::fabs(rAlt) + GUIDE_OFFSET_AS;
    double c0z, c0l, c1z, c1l; clk::time_point d0, d1;
    if (!_ReadAxes(mount, c0z, c0l, d0)) return finish(false, "no mount position");
    mount->SlewVariable(0, 0, s0);
    mount->SlewVariable(1, 0, s1);
    std::this_thread::sleep_for(std::chrono::milliseconds(DIR_CHECK_MS));
    const bool got = _ReadAxes(mount, c1z, c1l, d1);
    const double movedAz  = WrapDeg180(c1z - c0z) * 3600.0;
    const double movedAlt = (c1l - c0l) * 3600.0;
    const double dt = secs(d0, d1);
    CoreLog("[CAL] direction check: az commanded %.1f\"/s moved %.1f\" (expect %.1f); "
            "alt commanded %.1f\"/s moved %.1f\" (expect %.1f)\n",
            s0, movedAz, s0 * dt, s1, movedAlt, s1 * dt);
    if (!got) return finish(false, "no mount position");
    if (std::fabs(movedAz) < 0.4 * s0 * dt || std::fabs(movedAlt) < 0.4 * s1 * dt)
        return finish(false, "an axis did not follow its rate command - check the mount");
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_axisSign[0] = movedAz  >= 0.0 ? 1.0f : -1.0f;
        m_axisSign[1] = movedAlt >= 0.0 ? 1.0f : -1.0f;
        m_baseRateAs[0] = rAz; m_baseRateAs[1] = rAlt;
        m_integAs[0] = m_integAs[1] = 0.0f;
        m_ratesValid = true;
    }
    _DriveAxes(mount, rAz, rAlt);

    // 3. Drift while this app tracks, in DRIFT_WINDOWS consecutive
    //    windows after a settle. On 2026-11-07 the image drifted 7-9"/s
    //    right after takeover with the encoders exactly on the commanded
    //    rates; the windows show whether that dies away or stays.
    if (!_CalSettle(TAKEOVER_SETTLE_MS)) return finish(false, "aborted");
    float bx, by;
    if (!_WaitCentroidAvg(CAL_SAMPLES, bx, by)) return finish(false, "target lost after taking the axes");
    clk::time_point tB = clk::now();
    float driftVx = 0.0f, driftVy = 0.0f;
    for (int w = 0; w < DRIFT_WINDOWS; ++w) {
        double az0w = 0, al0w = 0, az1w = 0, al1w = 0; clk::time_point e0, e1;
        const bool okA = _ReadAxes(mount, az0w, al0w, e0);
        if (!_CalSettle(CAL_DRIFT_MS)) return finish(false, "aborted while measuring drift");
        float nx2, ny2;
        if (!_WaitCentroidAvg(CAL_SAMPLES, nx2, ny2)) return finish(false, "target lost while measuring drift");
        const clk::time_point tN = clk::now();
        const bool okB = _ReadAxes(mount, az1w, al1w, e1);
        const float dt = secs(tB, tN);
        driftVx = (nx2 - bx) / dt; driftVy = (ny2 - by) / dt;
        const float et = secs(e0, e1);
        CoreLog("[CAL] app-tracking window %d: drift (%.1f, %.1f) px/s over %.1f s; encoders az %.2f alt %.2f \"/s (commanded %.2f %.2f)\n",
                w + 1, driftVx, driftVy, dt,
                (okA && okB) ? (float)(WrapDeg180(az1w - az0w) * 3600.0 / et) : 0.0f,
                (okA && okB) ? (float)((al1w - al0w) * 3600.0 / et) : 0.0f, rAz, rAlt);
        bx = nx2; by = ny2; tB = tN;
    }
    float b2x = bx, b2y = by;
    clk::time_point tB2 = tB;

    // 4. One axis at a time: its rate plus an offset for CAL_OFFSET_MS,
    //    then back. Drift is measured again right after the step and the
    //    mean of before and after is taken out, so a drift that is still
    //    changing slowly is not mistaken for the response.
    auto measure = [&](int axis, float fromX, float fromY, clk::time_point tFrom,
                       float& vx, float& vy, float& endX, float& endY,
                       clk::time_point& tEnd) -> bool {
        if (!_CalAlive()) return false;
        // Same direction as the axis already turns, so the motor never
        // reverses: on 2026-11-07 a +10"/s offset on altitude tracking at
        // -6.7"/s flipped the motor and backlash ate the move (1"/s of
        // 10). The response is stored per +GUIDE_OFFSET_AS all the same.
        const float base = (axis == 0) ? rAz : rAlt;
        const float o = (base >= 0.0f ? 1.0f : -1.0f) * GUIDE_OFFSET_AS;
        _DriveAxes(mount, rAz + (axis == 0 ? o : 0.0f), rAlt + (axis == 1 ? o : 0.0f));
        std::this_thread::sleep_for(std::chrono::milliseconds(offMs));
        _DriveAxes(mount, rAz, rAlt);
        if (!_CalSettle(CAL_SETTLE_MS)) return false;
        if (!_WaitCentroidAvg(CAL_SAMPLES, endX, endY)) return false;
        tEnd = clk::now();
        // Drift after the step, from here.
        if (!_CalSettle(CAL_DRIFT_MS)) return false;
        float ax2, ay2;
        if (!_WaitCentroidAvg(CAL_SAMPLES, ax2, ay2)) return false;
        const clk::time_point tA = clk::now();
        const float da = secs(tEnd, tA);
        const float afterVx = (ax2 - endX) / da, afterVy = (ay2 - endY) / da;
        const float mVx = 0.5f * (driftVx + afterVx), mVy = 0.5f * (driftVy + afterVy);
        const float t = secs(tFrom, tEnd);
        const float tx = endX - fromX - mVx * t, ty = endY - fromY - mVy * t;
        const float per = (o > 0.0f) ? 1.0f : -1.0f;
        vx = per * tx / (float)offMs;    // px per ms of a +GUIDE_OFFSET_AS offset
        vy = per * ty / (float)offMs;
        CoreLog("[CAL] %s %+.0f\"/s for %d ms: raw (%.1f, %.1f), drift before (%.1f, %.1f) after (%.1f, %.1f) px/s, "
                "response (%.1f, %.1f) px\n", axis == 0 ? "az" : "alt", o, offMs,
                endX - fromX, endY - fromY, driftVx, driftVy, afterVx, afterVy, tx, ty);
        // The next step starts from here, with this drift.
        driftVx = afterVx; driftVy = afterVy;
        endX = ax2; endY = ay2; tEnd = tA;
        return true;
    };
    float ex, ey, e2x, e2y; clk::time_point tE;
    if (!measure(0, b2x, b2y, tB2, ex, ey, e2x, e2y, tE))
        return finish(false, "azimuth measurement failed or aborted");
    float nx, ny, n2x, n2y; clk::time_point tN;
    if (!measure(1, e2x, e2y, tE, nx, ny, n2x, n2y, tN))
        return finish(false, "altitude measurement failed or aborted");

    const float eMag = std::sqrt(ex*ex + ey*ey) * offMs;
    const float nMag = std::sqrt(nx*nx + ny*ny) * offMs;
    if (eMag < 10.0f || nMag < 10.0f) {
        char why[160];
        snprintf(why, sizeof(why), "an offset barely moved the planet (az %.1f px, alt %.1f px)",
                 eMag, nMag);
        return finish(false, why);
    }
    const float sinAng = std::fabs(ex*ny - ey*nx) / (std::sqrt(ex*ex + ey*ey) * std::sqrt(nx*nx + ny*ny));
    if (sinAng < CAL_MIN_SIN_ANGLE) {
        char why[200];
        snprintf(why, sizeof(why),
                 "the two axes moved the planet only %.0f deg apart (they should be near 90)",
                 std::asin((std::min)(1.0f, sinAng)) * 57.2958f);
        return finish(false, why);
    }

    // The drift measured while this app tracked is a rate error on the
    // axes; the calibration says how much on each, so it is taken out of
    // the base rates now rather than left for the integral to find.
    {
        GuideResponse r{ ex, ey, nx, ny };
        const GuidePulses d = SolveGuidePulses(r, -driftVx, -driftVy);
        if (d.valid) {
            const float dAz  = d.eastMs  * GUIDE_OFFSET_AS / 1000.0f;
            const float dAlt = d.northMs * GUIDE_OFFSET_AS / 1000.0f;
            std::lock_guard<std::mutex> lk(m_mutex);
            m_baseRateAs[0] += dAz;
            m_baseRateAs[1] += dAlt;
            CoreLog("[CAL] drift taken out of the rates: az %+.2f alt %+.2f \"/s -> az %.2f alt %.2f\n",
                    dAz, dAlt, m_baseRateAs[0], m_baseRateAs[1]);
        }
    }

    GuideCalibration cal;
    cal.valid   = true;
    cal.exPerMs = ex; cal.eyPerMs = ey;      // px per ms of a GUIDE_OFFSET_AS offset
    cal.nxPerMs = nx; cal.nyPerMs = ny;
    cal.fourWay = false;
    cal.arcsecPerPx = RigConfig::Get().plateScaleArcsecPx;
    cal.when = clk::now();
    cal.altDeg = (float)alt1;
    { std::lock_guard<std::mutex> lk(m_mutex); m_cal = cal; }

    const float secOff = offMs / 1000.0f;
    char buf[320];
    snprintf(buf, sizeof(buf),
             "app-driven: axes %.0f deg apart; a %.0f\"/s offset moved the planet %.1f\"/s in az "
             "(expect %.1f) and %.1f\"/s in alt (expect %.1f); tracking az %.1f alt %.1f\"/s; "
             "axis signs %+.0f %+.0f",
             std::asin((std::min)(1.0f, sinAng)) * 57.2958f, GUIDE_OFFSET_AS,
             eMag * cal.arcsecPerPx / secOff, GUIDE_OFFSET_AS * std::cos((float)alt1 * 0.0174533f),
             nMag * cal.arcsecPerPx / secOff, GUIDE_OFFSET_AS, rAz, rAlt,
             m_axisSign[0], m_axisSign[1]);
    finish(true, buf);
}

void CentroidTracker::YieldMount(const char* why) {
    if (m_stage.load() == AcquisitionStage::Off) return;
    m_suspended.store(true);        // no corrections; a calibration aborts
    IMount* mnt = nullptr;
    { std::lock_guard<std::mutex> lk(m_mutex); mnt = m_mount; }
    _HandBack(mnt, why);
    m_autoAdvance.store(false);
    m_lostPending.store(true);      // the main thread resets on its next Tick
    CoreLog("[CentroidTracker] Mount yielded: %s\n", why);
}

void CentroidTracker::_AbortGuiding(IMount* mount, const std::string& why) {
    _HandBack(mount, "guiding abandoned");
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_cal = GuideCalibration{};      // it is what sent the mount the wrong way
        m_abortMessage = why;
        m_samples.clear();
    }
    m_autoAdvance.store(false);
    AcquisitionStage st = m_stage.load();
    if (st == AcquisitionStage::Centering || st == AcquisitionStage::Locked)
        m_stage.compare_exchange_strong(st, AcquisitionStage::Searching);
    m_abortPending.store(true);
    CoreLog("[CentroidTracker] Guiding abandoned: %s\n", why.c_str());
}

void CentroidTracker::Run() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    CoreLog("[CentroidTracker] Correction thread running\n");
    bool wasLost = false;
    auto lastAltPoll = std::chrono::steady_clock::now()
                     - std::chrono::seconds(60);
    float lastAltDeg = -1.0f;
    // Runaway watch (RUNAWAY_*): the closest to centre seen since this
    // run of corrections began, and how many cycles in a row the planet
    // has been well beyond it.
    AcquisitionStage watchStage = AcquisitionStage::Off;
    auto lastControl  = std::chrono::steady_clock::now();
    auto lastGuideLog = lastControl;
    auto lostSince    = lastControl;
    bool lostReported = false;
    uint32_t lastFw = 0, lastFh = 0;
    bool wasSuspended = false;
    float bestErrPx = 1e9f;
    int   overCycles = 0;

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
        if (m_suspended.load()) { wasSuspended = true; continue; }

        Snapshot s = GetSnapshot();
        if (!s.mount || !s.mount->IsConnected()) continue;

        // 2026-11-08: a hand jog during a lock moved Saturn 250 px on
        // purpose, and when the jog ended the guider read the new position
        // against its best error from before and abandoned it as a
        // runaway. The jog's stop also left that axis standing still. So
        // when a suspension ends: both axes straight back on their rates,
        // and the planet's new place is the new reference to centre from.
        if (wasSuspended) {
            wasSuspended = false;
            if (m_appDriving.load() && stage != AcquisitionStage::Calibrating) {
                _HoldRates(s.mount);
                { std::lock_guard<std::mutex> lk(m_mutex); m_samples.clear(); }
                watchStage = AcquisitionStage::Off;
                CoreLog("[GUIDE] hand jog ended: rates restored, centring from the new position\n");
                continue;
            }
        }

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

        // 2026-11-08: a format change (3840 -> 1280 -> 640 -> 3840) ended a
        // good lock -- the frame's centre and pixel coordinates moved under
        // the guider, a few frames had no centroid, and the jump read as a
        // runaway. So a frame change re-references, and while the app drives
        // the axes a loss is held for LOST_GRACE_MS at the plain tracking
        // rates before guiding gives up and hands tracking back.
        const bool frameChanged = m_frameChanged.exchange(false) ||
            (s.fw != 0 && (s.fw != lastFw || s.fh != lastFh) && lastFw != 0);
        if (s.fw != 0) { lastFw = s.fw; lastFh = s.fh; }
        if (frameChanged) {
            CoreLog("[GUIDE] the frame changed (%ux%u): holding the rates and re-referencing\n",
                    s.fw, s.fh);
            { std::lock_guard<std::mutex> lk(m_mutex); m_samples.clear(); }
            watchStage = AcquisitionStage::Off;   // runaway reference restarts
            if (m_appDriving.load()) _HoldRates(s.mount);
            continue;
        }
        if (!s.valid || timedOut) {
            const auto nowL = std::chrono::steady_clock::now();
            if (!wasLost) {
                wasLost = true;
                lostSince = nowL;
                CoreLog("[CentroidTracker] Centroid lost%s\n",
                        m_appDriving.load() ? " - holding the tracking rates" : "");
                if (m_appDriving.load()) _HoldRates(s.mount);
            }
            const bool graceOver = std::chrono::duration_cast<std::chrono::milliseconds>(
                nowL - lostSince).count() >= LOST_GRACE_MS;
            if ((!m_appDriving.load() || graceOver) && !lostReported) {
                lostReported = true;
                // Do not fly the mount blind: tracking goes back to the
                // hand control now, before the main thread resets.
                _HandBack(s.mount, "target lost");
                m_lostPending.store(true);
            }
            continue;
        }
        if (wasLost) {
            wasLost = false; lostReported = false;
            CoreLog("[CentroidTracker] Reacquired\n");
            watchStage = AcquisitionStage::Off;   // judge runaway from here
        }
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

        // 2026-11-07: centring on a bad calibration pushed the planet
        // away at the guide rate, steadily, until it left the frame.
        // Corrections that make things steadily worse are stopped.
        {
            const float errPx = std::sqrt(dxPx*dxPx + dyPx*dyPx);
            if (stage != watchStage) {
                watchStage = stage; bestErrPx = errPx; overCycles = 0;
            }
            if (errPx < bestErrPx) bestErrPx = errPx;
            if (errPx > bestErrPx + RUNAWAY_GROWTH_PX) {
                if (++overCycles >= RUNAWAY_CYCLES) {
                    char why[200];
                    snprintf(why, sizeof(why),
                             "the planet kept moving away from centre (%.0f px, best "
                             "was %.0f px) - corrections were making it worse",
                             errPx, bestErrPx);
                    _AbortGuiding(s.mount, why);
                    watchStage = AcquisitionStage::Off;
                    continue;
                }
            } else {
                overCycles = 0;
            }
        }

        // 2026-11-07: no pulses. This app drives both axes (see
        // _DriveAxes): each at its tracking rate plus a correction, the
        // image error turned into axis arcseconds by the calibration.
        // Proportional for the error now, integral for a tracking rate
        // that is a little off, both clamped.
        if (!m_appDriving.load()) continue;
        // Narrow: the old pulse deadbands (2-2.5") equal the 10 px LOCKED
        // threshold at 0.2"/px, so the guider stopped exactly at its edge.
        const float deadband = GUIDE_DEADBAND_AS;
        const float kp = (stage == AcquisitionStage::Locked) ? GUIDE_KP_LOCKED : GUIDE_KP;
        const float errAs = std::sqrt(dxPx*dxPx + dyPx*dyPx) * cal.arcsecPerPx;

        GuideResponse resp{ cal.exPerMs, cal.eyPerMs, cal.nxPerMs, cal.nyPerMs };
        resp = RescaleForAltitude(resp, cal.altDeg, lastAltDeg);
        const GuidePulses g = SolveGuidePulses(resp, -dxPx, -dyPx);
        if (!g.valid) continue;
        float aAz  = g.eastMs  * GUIDE_OFFSET_AS / 1000.0f;   // axis arcsec to centre
        float aAlt = g.northMs * GUIDE_OFFSET_AS / 1000.0f;
        if (errAs <= deadband) { aAz = 0.0f; aAlt = 0.0f; }

        const auto tNow = std::chrono::steady_clock::now();
        float dtc = std::chrono::duration<float>(tNow - lastControl).count();
        lastControl = tNow;
        if (dtc <= 0.0f || dtc > 2.0f) dtc = 0.5f;
        auto clampf = [](float v, float m) { return v > m ? m : (v < -m ? -m : v); };
        float base[2], corr[2];
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_integAs[0] = clampf(m_integAs[0] + GUIDE_KI * aAz  * dtc, GUIDE_MAX_CORR_AS * 0.5f);
            m_integAs[1] = clampf(m_integAs[1] + GUIDE_KI * aAlt * dtc, GUIDE_MAX_CORR_AS * 0.5f);
            base[0] = m_baseRateAs[0]; base[1] = m_baseRateAs[1];
            corr[0] = clampf(kp * aAz  + m_integAs[0], GUIDE_MAX_CORR_AS);
            corr[1] = clampf(kp * aAlt + m_integAs[1], GUIDE_MAX_CORR_AS);
        }
        // Never reverse an axis: a reversal is backlash, a move that does
        // not happen. On the slow side a correction stops 1"/s short of
        // zero; on the fast side it has the full range.
        for (int i = 0; i < 2; ++i) {
            if (base[i] > 1.0f && base[i] + corr[i] < 1.0f)   corr[i] = 1.0f - base[i];
            if (base[i] < -1.0f && base[i] + corr[i] > -1.0f) corr[i] = -1.0f - base[i];
        }
        if (!m_threadRunning.load() || m_suspended.load()) continue;
        _DriveAxes(s.mount, base[0] + corr[0], base[1] + corr[1]);
        if (std::chrono::duration<float>(tNow - lastGuideLog).count() >= 2.0f) {
            lastGuideLog = tNow;
            CoreLog("[GUIDE] %s err %.1f\" (%.0f, %.0f px)  axis err az %.1f alt %.1f\"  "
                    "rates az %.2f%+.2f alt %.2f%+.2f \"/s\n",
                    AcquisitionStageName(stage), errAs, dxPx, dyPx, aAz, aAlt,
                    base[0], corr[0], base[1], corr[1]);
        }
    }

    CoreLog("[CentroidTracker] Correction thread stopped\n");
}

} // namespace raduk::core
