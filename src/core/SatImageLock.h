#pragma once
// ============================================================
// SatImageLock.h — Phase 117: steer a satellite pass from the image
//
// Phase 116 flies on prediction alone, and a prediction cannot hold a
// 13' x 7' frame: a day-old element set is 4' to 8' out, nearly all of
// it along track, and a second of PC clock is 28'. So once the satellite
// is on the sensor, the sensor should take over.
//
// ── A correction, never a controller ────────────────────────
//
// SatTracker keeps flying the prediction, with every stop it has. This
// only moves the target it is aiming at, by a bias that is clamped to a
// degree. So an image gone wrong can point the mount a degree away from
// the prediction and no further, and the worst outcome of a bad centroid
// is a lost satellite, not a runaway.
//
// ── Which way is which ──────────────────────────────────────
//
// Phase 51's guide calibration has measured how the image responds to
// an East and a North pulse, which holds the camera's rotation and the
// mirror flip. Nothing is assumed. On NexStar those pulses are axis 0
// and axis 1 in direction 0, which are exactly a positive azimuth and
// altitude rate here, so a solved pulse is a solved axis move. On an
// alt-az mount the sensor is fixed to the axes, so there is no field
// rotation to chase, only the cos(altitude) GuideSolve already rescales.
// No calibration, no lock: the pass flies on prediction and says so.
//
// ── Trust ───────────────────────────────────────────────────
//
// A centroid is believed when the next one agrees with it. A hot spot,
// a plane or the limb of the Moon arrives as one frame's jump, and
// acting on it would throw the telescope at it.
//
// The fix comes off the capture thread and is a few tenths of a second
// old by the time it is used, and the loop is an integrator with that
// delay in it. The first gain here was one half: the test saw it ring at
// two ticks late and diverge at three, and a camera, a 5 Hz loop and a
// serial link can easily be three. At 0.2 it still rang at five and
// diverged at seven. 0.12 settles at seven ticks and takes about eight
// seconds to centre, against an along-track error that barely changes
// over a pass.
// ============================================================

#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>

#include "GuideSolve.h"
#include "RoiFollower.h"

namespace raduk::core {

class SatImageLock {
public:
    static constexpr double kGain       = 0.12;
    static constexpr double kMaxBiasDeg = 1.0;
    static constexpr double kDeadbandPx = 4.0;
    static constexpr double kLostSec    = 1.0;

    void Configure(const GuideResponse& r, float altCalDeg, float rateArcsecPerSec) {
        m_resp = r;
        m_altCal = altCalDeg;
        m_rate = rateArcsecPerSec;
        const GuidePulses t = SolveGuidePulses(r, 1.0f, 0.0f);
        m_ok = t.valid && rateArcsecPerSec > 0.0f;
        Reset();
    }

    void Reset() {
        m_biasAz = m_biasAlt = 0.0;
        m_lastFix = -1.0e9;
        m_havePrev = false;
        m_rejected = 0;
    }

    bool Calibrated() const { return m_ok; }

    // One centroid, or none. seq moves when a new one exists.
    void Update(double now, bool haveFix, float x, float y,
                int width, int height, float altNowDeg) {
        if (!m_ok || !haveFix || width <= 0 || height <= 0) {
            return;
        }
        const double limit = (width < height ? width : height) * 0.25;
        const bool agrees = m_havePrev &&
            std::hypot((double)x - m_prevX, (double)y - m_prevY) <= limit;
        m_prevX = x; m_prevY = y; m_havePrev = true;
        if (!agrees) { ++m_rejected; return; }
        m_lastFix = now;

        const double ddx = width  * 0.5 - x;
        const double ddy = height * 0.5 - y;
        if (std::hypot(ddx, ddy) < kDeadbandPx) return;

        const GuideResponse r = RescaleForAltitude(m_resp, m_altCal, altNowDeg);
        const GuidePulses p = SolveGuidePulses(r, (float)ddx, (float)ddy);
        if (!p.valid) return;
        const double degPerMs = m_rate / 1000.0 / 3600.0;
        m_biasAz  = Clamp(m_biasAz  + kGain * p.eastMs  * degPerMs);
        m_biasAlt = Clamp(m_biasAlt + kGain * p.northMs * degPerMs);
    }

    double BiasAzDeg()  const { return m_biasAz; }
    double BiasAltDeg() const { return m_biasAlt; }
    bool Locked(double now) const { return now - m_lastFix < kLostSec; }
    int  Rejected() const { return m_rejected; }

private:
    static double Clamp(double v) {
        return v > kMaxBiasDeg ? kMaxBiasDeg : (v < -kMaxBiasDeg ? -kMaxBiasDeg : v);
    }

    GuideResponse m_resp;
    float  m_altCal = -1.0f, m_rate = 0.0f;
    bool   m_ok = false, m_havePrev = false;
    float  m_prevX = 0.0f, m_prevY = 0.0f;
    double m_biasAz = 0.0, m_biasAlt = 0.0, m_lastFix = -1.0e9;
    int    m_rejected = 0;
};

// Between the capture thread and the pass thread. Disarmed, it costs
// one atomic read a frame. Armed, it looks ten times a second and not at
// every frame: a full-sensor centroid is two million reads even at this
// stride, and the capture thread has a recording to feed.
class SatFrameTap {
public:
    static constexpr int kStride = 2;
    static constexpr int kPeriodMs = 100;

    void Arm(bool on) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_armed.store(on, std::memory_order_relaxed);
        m_fresh = false;
    }

    void OnFrame(const uint8_t* data, int width, int height, int bitDepth) {
        if (!m_armed.load(std::memory_order_relaxed)) return;
        const auto now = std::chrono::steady_clock::now();
        m_frameAt = now;
        if (now - m_last < std::chrono::milliseconds(kPeriodMs)) return;
        m_last = now;
        float x = 0.0f, y = 0.0f;
        const bool ok = FastCentroid(data, width, height, bitDepth, kStride, x, y);
        std::lock_guard<std::mutex> lk(m_mutex);
        m_fresh = ok;
        m_x = x; m_y = y; m_w = width; m_h = height;
        m_fixAt = m_frameAt;
    }

    // Phase 122: the same, and how long ago its frame arrived.
    bool TakeTimed(float& x, float& y, int& w, int& h, double& ageSec) {
        std::chrono::steady_clock::time_point at;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (!m_fresh) return false;
            m_fresh = false;
            x = m_x; y = m_y; w = m_w; h = m_h;
            at = m_fixAt;
        }
        ageSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - at).count();
        return true;
    }

    // True once for each new centroid.
    bool Take(float& x, float& y, int& w, int& h) {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_fresh) return false;
        m_fresh = false;
        x = m_x; y = m_y; w = m_w; h = m_h;
        return true;
    }

private:
    std::atomic<bool> m_armed{false};
    std::chrono::steady_clock::time_point m_last{}, m_frameAt{}, m_fixAt{};
    std::mutex m_mutex;
    bool  m_fresh = false;
    float m_x = 0.0f, m_y = 0.0f;
    int   m_w = 0, m_h = 0;
};

// Against a simulated sensor and mount with a rotated, mirrored camera.
// Whether FastCentroid finds a real satellite on a real frame, and
// whether the guide calibration holds at these rates, is checklist 40.
inline constexpr bool SAT_IMAGE_LOCK_PROVEN_ON_SKY = false;

} // namespace raduk::core
