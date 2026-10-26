// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// ============================================================
// CentroidTracker.cpp — Planet centroid tracking
// Out-of-line definitions for CentroidTracker.h.
// ============================================================

#include "CentroidTracker.h"
#include "GuideSolve.h"   // the pixel->pulse solve, shared with phase 47

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
    printf("[CentroidTracker] Auto-acquire: searching for the planet\n");
    return true;
}

void CentroidTracker::StartSearching() {
    if (m_stage.load() != AcquisitionStage::Off) return;
    _ResetCounters();
    _StartThread();
    m_stage.store(AcquisitionStage::Searching);
    printf("[CentroidTracker] Stage -> SEARCHING\n");
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

void CentroidTracker::StartCentering() {
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
    printf("[CentroidTracker] Stage -> LOCKED\n");
}

void CentroidTracker::Reset() {
    m_stage.store(AcquisitionStage::Off);
    _StopThread();
    _ResetCounters();
    m_driftRA.store(0.0f);
    m_driftDec.store(0.0f);
    m_suspended.store(false);
    printf("[CentroidTracker] Reset -> OFF\n");
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
            if (n >= samples) { outX = sx / n; outY = sy / n; return true; }
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

void CentroidTracker::_RunCalibration(IMount* mount) {
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
    float measuredRate = 0.0f, assumedRate = 0.0f;
    cal.arcsecPerPx = RigConfig::Get().plateScaleArcsecPx;
    {
        float assumed  = RigConfig::Get().guideRateArcsecPerSec;
        float measured = (eMag * cal.arcsecPerPx)
                       / (CAL_PULSE_MS / 1000.0f);   // arcsec per second
        // Carried into the calibration message rather than printed.
        // printf reaches nothing in a GUI build, so the one diagnostic
        // that says rig.ini's guide rate is wrong -- which throws off
        // pulse sizing and the drift readout -- was invisible to the
        // only person who could act on it.
        measuredRate = measured;
        assumedRate  = assumed;
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

    // The guide rate belongs here, not in a printf: a measurement that
    // disagrees with rig.ini is the most useful thing this routine
    // learns, and it is only actionable if it is seen.
    const bool rateOff = assumedRate > 0.01f &&
                         (measuredRate < assumedRate * 0.5f ||
                          measuredRate > assumedRate * 2.0f);
    char buf[256];
    snprintf(buf, sizeof(buf),
             "E=(%.2f,%.2f)px/s N=(%.2f,%.2f)px/s scale=%.2f\"/px alt=%.0f"
             "  guide rate measured %.2f\"/s vs rig.ini %.2f\"/s%s",
             cal.exPerMs * 1000, cal.eyPerMs * 1000,
             cal.nxPerMs * 1000, cal.nyPerMs * 1000,
             cal.arcsecPerPx, cal.altDeg,
             measuredRate, assumedRate,
             rateOff ? " <- WRONG, set GuideRateArcsecPerSec" : "");
    finish(true, buf);
}

void CentroidTracker::Run() {
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
        const GuidePulses sol = SolveGuidePulses(resp, ddx, ddy);
        if (!sol.valid) continue;
        const float tE = sol.eastMs, tN = sol.northMs;

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

} // namespace raduk::core
