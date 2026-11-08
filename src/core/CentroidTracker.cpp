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
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_cal.valid) {
            auto ageMin = std::chrono::duration_cast<std::chrono::minutes>(
                std::chrono::steady_clock::now() - m_cal.when).count();
            if (ageMin < CAL_STALE_MIN) {
                m_dwellCount = 0;
                m_stage.store(AcquisitionStage::Centering);
                CoreLog("[CentroidTracker] Reusing calibration (%lld min old) -> CENTERING\n",
                       (long long)ageMin);
                return;
            }
            CoreLog("[CentroidTracker] Calibration stale (%lld min) — recalibrating\n",
                   (long long)ageMin);
            m_cal.valid = false;
        }
    }
    m_dwellCount = 0;
    m_stage.store(AcquisitionStage::Calibrating);
    CoreLog("[CentroidTracker] Stage -> CALIBRATING\n");
}

void CentroidTracker::StartCentering() {
    if (m_stage.load() != AcquisitionStage::Searching) return;
    if (!IsStable()) return;
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

// ── Rate-offset pulses (2026-11-07) ────────────────────────────
//
// Measured at the rig: a NexStar pulse REPLACES the axis's tracking for
// its length. At rate 1 (~3.8"/s) against ~16"/s of azimuth tracking,
// East and West both moved the planet the same way, so centring could
// never push east. So a pulse here sends the axis its OWN tracking rate
// plus or minus GUIDE_OFFSET_AS. Tracking never stops, the pulse is a
// pure offset, East is the mirror of West, and while the offset is
// smaller than the tracking rate the motor never reverses, so there is
// no backlash to take up.
//
// The tracking rates come from the mount's own encoders (GetAltAz),
// with this app's offsets taken back out, so nothing about the hand
// control's tracking model has to be assumed.

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

bool CentroidTracker::RatePulse(IMount* mount, GuideDirection dir, int ms) {
    if (!mount || ms < 10) return false;
    const int   axis = (dir == GuideDirection::East || dir == GuideDirection::West) ? 0 : 1;
    const float sgn  = (dir == GuideDirection::East || dir == GuideDirection::North) ? 1.0f : -1.0f;
    float track, axSign;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_ratesValid) return false;
        track  = m_trackRateAs[axis];
        axSign = m_axisSign[axis];
    }
    const float rate = track + sgn * GUIDE_OFFSET_AS;      // encoder arcsec/s
    const float cmd  = rate * axSign;                       // mount's own direction
    if (!mount->SlewVariable(axis, cmd >= 0.0f ? 0 : 1, std::fabs(cmd))) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    mount->StopSlew(axis);              // the sender re-asserts tracking after it
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_offsetAccumAs[axis] += sgn * GUIDE_OFFSET_AS * (float)ms / 1000.0f;
    }
    return true;
}

// Correction thread, while guiding: refresh the tracking rates from the
// encoders, taking this app's own offsets back out.
void CentroidTracker::_RefreshTrackRates(IMount* mount) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_ratesValid) return;
        if (std::chrono::duration<float>(std::chrono::steady_clock::now() - m_rateRefT).count()
            < (float)RATE_REFRESH_SEC) return;
    }
    double az = 0, alt = 0; std::chrono::steady_clock::time_point t;
    if (!_ReadAxes(mount, az, alt, t)) return;
    std::lock_guard<std::mutex> lk(m_mutex);
    const float dt = std::chrono::duration<float>(t - m_rateRefT).count();
    if (dt < 1.0f) return;
    // Encoder motion = tracking*dt + offsets (in encoder units, the
    // offsets carry the axis sign through the command).
    const float encAz  = (float)(WrapDeg180(az - m_rateRefAz) * 3600.0);
    const float encAlt = (float)((alt - m_rateRefAlt) * 3600.0);
    m_trackRateAs[0] = (encAz  - m_offsetAccumAs[0]) / dt;
    m_trackRateAs[1] = (encAlt - m_offsetAccumAs[1]) / dt;
    m_offsetAccumAs[0] = m_offsetAccumAs[1] = 0.0f;
    m_rateRefAz = az; m_rateRefAlt = alt; m_rateRefT = t;
    CoreLog("[GUIDE] tracking rates refreshed: az %.2f alt %.2f \"/s over %.0f s\n",
            m_trackRateAs[0], m_trackRateAs[1], dt);
}

void CentroidTracker::_RunCalibration(IMount* mount) {
    auto finish = [&](bool ok, const std::string& msg) {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_calMessage = msg;
        }
        if (!ok) {
            // Stops for an idle axis are not even sent (NexStarMount), so
            // this only touches an axis this calibration left moving.
            mount->StopSlew(0);
            mount->StopSlew(1);
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

    CoreLog("[CentroidTracker] Calibration: rate-offset method\n");
    using clk = std::chrono::steady_clock;
    auto secs = [](clk::time_point a, clk::time_point b) {
        return std::chrono::duration<float>(b - a).count();
    };

    // 1. Where the planet is and how the axes are moving, twice, a few
    //    seconds apart: image drift, and each axis's tracking rate.
    float b0x, b0y;
    if (!_WaitCentroidAvg(CAL_SAMPLES, b0x, b0y))
        return finish(false, "no stable centroid to calibrate against");
    double az0, alt0; clk::time_point te0;
    if (!_ReadAxes(mount, az0, alt0, te0))
        return finish(false, "the mount did not report its position");
    const clk::time_point tB0 = clk::now();
    if (!_CalSettle(CAL_DRIFT_MS)) return finish(false, "aborted while measuring drift");
    float bx, by;
    if (!_WaitCentroidAvg(CAL_SAMPLES, bx, by))
        return finish(false, "target lost while measuring drift");
    double az1, alt1; clk::time_point te1;
    if (!_ReadAxes(mount, az1, alt1, te1))
        return finish(false, "the mount did not report its position");
    clk::time_point tB1 = clk::now();
    const float driftDt = secs(tB0, tB1);
    const float driftVx = driftDt > 0.1f ? (bx - b0x) / driftDt : 0.0f;
    const float driftVy = driftDt > 0.1f ? (by - b0y) / driftDt : 0.0f;
    const float encDt = secs(te0, te1);
    if (encDt < 1.0f) return finish(false, "mount position readings too close together");
    const float rAz  = (float)(WrapDeg180(az1 - az0) * 3600.0 / encDt);
    const float rAlt = (float)((alt1 - alt0) * 3600.0 / encDt);
    CoreLog("[CAL] drift (%.1f, %.1f) px/s; tracking az %.2f\"/s alt %.2f\"/s (encoders, %.1f s)\n",
            driftVx, driftVy, rAz, rAlt, encDt);
    if (std::fabs(rAz) < 0.5f && std::fabs(rAlt) < 0.5f)
        return finish(false, "the mount is not tracking (its axes are not moving) - turn tracking on");
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_trackRateAs[0] = rAz; m_trackRateAs[1] = rAlt;
        m_axisSign[0] = m_axisSign[1] = 1.0f;
        m_ratesValid = false;
    }

    // 2. Which way each axis turns for the mount's "direction 0". The
    //    axis is driven at a known command for DIR_CHECK_MS and the
    //    encoder says which way it went.
    for (int axis = 0; axis < 2; ++axis) {
        if (!_CalAlive()) return finish(false, "aborted");
        const float speed = std::fabs(axis == 0 ? rAz : rAlt) + 2.0f * GUIDE_OFFSET_AS;
        double a0z, a0l, a1z, a1l; clk::time_point s0, s1;
        if (!_ReadAxes(mount, a0z, a0l, s0)) return finish(false, "no mount position");
        if (!mount->SlewVariable(axis, 0, speed))
            return finish(false, "the mount refused a rate command");
        std::this_thread::sleep_for(std::chrono::milliseconds(DIR_CHECK_MS));
        const bool got = _ReadAxes(mount, a1z, a1l, s1);
        mount->StopSlew(axis);
        if (!got) return finish(false, "no mount position");
        const double moved = (axis == 0 ? WrapDeg180(a1z - a0z) : (a1l - a0l)) * 3600.0;
        const double dt = secs(s0, s1);
        const double expect = speed * dt;
        CoreLog("[CAL] axis %d direction check: commanded %.1f\"/s for %.2f s, encoder moved "
                "%.1f\" (expected about %.1f\")\n", axis, speed, dt, moved, expect);
        if (std::fabs(moved) < 0.3 * expect)
            return finish(false, "an axis did not move as commanded - check the mount");
        std::lock_guard<std::mutex> lk(m_mutex);
        m_axisSign[axis] = moved >= 0.0 ? 1.0f : -1.0f;
    }
    if (!_CalSettle(CAL_SETTLE_MS)) return finish(false, "aborted");
    {
        double az2, alt2; clk::time_point te2;
        if (!_ReadAxes(mount, az2, alt2, te2)) return finish(false, "no mount position");
        std::lock_guard<std::mutex> lk(m_mutex);
        m_offsetAccumAs[0] = m_offsetAccumAs[1] = 0.0f;
        m_rateRefAz = az2; m_rateRefAlt = alt2; m_rateRefT = te2;
        m_ratesValid = true;
    }
    if (!_WaitCentroidAvg(CAL_SAMPLES, bx, by))
        return finish(false, "target lost after the direction check");
    tB1 = clk::now();

    // 3. The image's response to each axis's offset.
    auto measureAxis = [&](GuideDirection dir, float fromX, float fromY,
                           clk::time_point tFrom, float& vx, float& vy,
                           float& endX, float& endY, clk::time_point& tEnd) -> bool {
        for (int k = 0; k < CAL_MEASURE_PULSES; ++k) {
            if (!_CalAlive()) return false;
            if (!RatePulse(mount, dir, CAL_PULSE_MS)) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!_CalSettle(CAL_SETTLE_MS)) return false;
        if (!_WaitCentroidAvg(CAL_SAMPLES, endX, endY)) return false;
        tEnd = clk::now();
        const float dt = secs(tFrom, tEnd);
        const float tx = endX - fromX - driftVx * dt;
        const float ty = endY - fromY - driftVy * dt;
        vx = tx / (float)CAL_MEASURE_PULSES;
        vy = ty / (float)CAL_MEASURE_PULSES;
        CoreLog("[CAL] %s x%d offset pulses: raw (%.1f, %.1f) over %.2f s, drift part (%.1f, %.1f), "
                "response (%.1f, %.1f) px = (%.1f, %.1f) per pulse\n",
                dir == GuideDirection::East ? "Az+" : "Alt+", CAL_MEASURE_PULSES,
                endX - fromX, endY - fromY, dt, driftVx * dt, driftVy * dt, tx, ty, vx, vy);
        return true;
    };
    float ex, ey, b2x, b2y; clk::time_point tB2;
    if (!measureAxis(GuideDirection::East, bx, by, tB1, ex, ey, b2x, b2y, tB2))
        return finish(false, "azimuth measurement failed or aborted");
    float nx, ny, b3x, b3y; clk::time_point tB3;
    if (!measureAxis(GuideDirection::North, b2x, b2y, tB2, nx, ny, b3x, b3y, tB3))
        return finish(false, "altitude measurement failed or aborted");
    const float eMag = std::sqrt(ex*ex + ey*ey), nMag = std::sqrt(nx*nx + ny*ny);
    if (eMag < CAL_MIN_MOVE_PX || nMag < CAL_MIN_MOVE_PX) {
        char why[160];
        snprintf(why, sizeof(why),
                 "an offset barely moved the planet (az %.1f px, alt %.1f px per pulse)", eMag, nMag);
        return finish(false, why);
    }
    const float sinAng = std::fabs(ex*ny - ey*nx) / (eMag * nMag);
    if (sinAng < CAL_MIN_SIN_ANGLE) {
        char why[200];
        snprintf(why, sizeof(why),
                 "the two axes moved the planet only %.0f deg apart (they should be near 90)",
                 std::asin((std::min)(1.0f, sinAng)) * 57.2958f);
        return finish(false, why);
    }

    GuideCalibration cal;
    cal.valid   = true;
    cal.exPerMs = ex / (float)CAL_PULSE_MS;
    cal.eyPerMs = ey / (float)CAL_PULSE_MS;
    cal.nxPerMs = nx / (float)CAL_PULSE_MS;
    cal.nyPerMs = ny / (float)CAL_PULSE_MS;
    cal.fourWay = false;            // offsets are symmetric by construction
    cal.arcsecPerPx = RigConfig::Get().plateScaleArcsecPx;
    cal.when = clk::now();
    cal.altDeg = (float)alt1;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_cal = cal;
    }
    // On-sky check: an az offset of G moves the sky G*cos(alt), alt by G.
    const float gSkyAz  = eMag * cal.arcsecPerPx / (CAL_PULSE_MS / 1000.0f);
    const float gSkyAlt = nMag * cal.arcsecPerPx / (CAL_PULSE_MS / 1000.0f);
    char buf[320];
    snprintf(buf, sizeof(buf),
             "Az+=(%.2f,%.2f)px/s Alt+=(%.2f,%.2f)px/s, axes %.0f deg apart; a %.0f\"/s offset moved "
             "the planet %.1f\"/s in az (expect %.1f) and %.1f\"/s in alt (expect %.1f); "
             "tracking az %.1f alt %.1f\"/s",
             cal.exPerMs * 1000, cal.eyPerMs * 1000, cal.nxPerMs * 1000, cal.nyPerMs * 1000,
             std::asin((std::min)(1.0f, sinAng)) * 57.2958f, GUIDE_OFFSET_AS,
             gSkyAz, GUIDE_OFFSET_AS * std::cos((float)alt1 * 0.0174533f),
             gSkyAlt, GUIDE_OFFSET_AS, rAz, rAlt);
    finish(true, buf);
}

void CentroidTracker::_AbortGuiding(IMount* mount, const std::string& why) {
    if (mount) { mount->StopSlew(0); mount->StopSlew(1); }
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
                CoreLog("[CentroidTracker] Centroid lost\n");
                m_lostPending.store(true);
            }
            continue;
        }
        if (wasLost) { wasLost = false; CoreLog("[CentroidTracker] Reacquired\n"); }
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
        _RefreshTrackRates(s.mount);   // rate-limited inside

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

        float agg      = (stage == AcquisitionStage::Locked) ? S3_AGGRESSIVENESS  : S2_AGGRESSIVENESS;
        int   maxPulse = (stage == AcquisitionStage::Locked) ? S3_MAX_PULSE_MS    : S2_MAX_PULSE_MS;
        float deadband = (stage == AcquisitionStage::Locked) ? S3_DEADBAND_ARCSEC : S2_DEADBAND_ARCSEC;

        float errAs = std::sqrt(dxPx*dxPx + dyPx*dyPx) * cal.arcsecPerPx;
        if (errAs <= deadband) continue;

        // Alt-az: azimuth (East) pixel response scales with cos(alt);
        // rescale the measured column to the current altitude.
        //
        // The rescale and the solve both live in GuideSolve.h now,
        // because phase 47 recentres from a click using the same two
        // steps. One copy, one set of signs: a second set would be a
        // second chance to send the mount the wrong way.
        GuideResponse resp{ cal.exPerMs, cal.eyPerMs,
                            cal.nxPerMs, cal.nyPerMs };
        resp = RescaleForAltitude(resp, cal.altDeg, lastAltDeg);

        // Desired displacement is −error × aggressiveness, in pixels.
        const float ddx = -dxPx * agg, ddy = -dyPx * agg;
        float tE = 0.0f, tN = 0.0f;
        if (cal.fourWay) {
            // Each axis has a different response forwards and backwards
            // (see GuideCalibration::fourWay). Solve with each of the four
            // column pairs and keep the one whose pulse lengths both come
            // out in the direction that column was measured for.
            bool found = false;
            for (int useW = 0; useW < 2 && !found; ++useW)
                for (int useS = 0; useS < 2 && !found; ++useS) {
                    GuideResponse r{ useW ? cal.wxPerMs : cal.exPerMs,
                                     useW ? cal.wyPerMs : cal.eyPerMs,
                                     useS ? cal.sxPerMs : cal.nxPerMs,
                                     useS ? cal.syPerMs : cal.nyPerMs };
                    r = RescaleForAltitude(r, cal.altDeg, lastAltDeg);
                    const GuidePulses g = SolveGuidePulses(r, ddx, ddy);
                    if (!g.valid || g.eastMs < 0.0f || g.northMs < 0.0f) continue;
                    tE = useW ? -g.eastMs : g.eastMs;
                    tN = useS ? -g.northMs : g.northMs;
                    found = true;
                }
            if (!found) continue;
        } else {
            const GuidePulses sol = SolveGuidePulses(resp, ddx, ddy);
            if (!sol.valid) continue;
            tE = sol.eastMs; tN = sol.northMs;
        }

        bool pulsed = false;
        auto sendPulse = [&](float tMs, GuideDirection pos, GuideDirection neg,
                             const char* axis) {
            if (!m_threadRunning.load() || m_suspended.load()) return;
            float mag = std::min(std::abs(tMs), (float)maxPulse);
            int ms = (int)mag;
            if (ms < 10) return;
            GuideDirection dir = (tMs > 0) ? pos : neg;
            CoreLog("[CentroidTracker] %s pulse %dms (err %.1f\")\n", axis, ms, errAs);
            RatePulse(s.mount, dir, ms);
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

    CoreLog("[CentroidTracker] Correction thread stopped\n");
}

} // namespace raduk::core
