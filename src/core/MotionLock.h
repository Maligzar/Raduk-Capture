#pragma once
// ============================================================
// MotionLock.h — Phase 122: track what moves, from the image alone
//
// Phases 116 to 121 fly a prediction and let the image correct it. A
// rocket in flight has no prediction at all, and neither does anything
// whose prediction has gone wrong. So the image becomes the prediction:
// the operator brings the object into frame, and this measures where it
// is on the sky, fits its motion, and hands SatTracker a sky position
// and rates exactly as SGP4 and Horizons do. Every stop the tracker has
// still applies.
//
// ── Where the object is ─────────────────────────────────────
//
// The mount reports where it points, and the centroid says how far
// from there the object sits. Phase 51's calibration turns pixels into
// axis degrees, rotation and mirror included, which SatImageLock
// already relies on. Mount plus offset is the object, in alt-az,
// independent of where the mount happens to be heading.
//
// The mount as it pointed WHEN THE FRAME WAS TAKEN, not now. A centroid
// is a few tenths of a second old by the time it arrives, and the mount
// has moved in that time; adding the old offset to the new position put
// the object behind itself by the sky rate times the delay. The test
// measured 0.1 degrees at two ticks, which is off the sensor. So the
// mount's readings are kept, and each fix is paired with the one from
// its own moment.
//
// ── How it moves ────────────────────────────────────────────
//
// An alpha-beta-gamma filter on each axis: position, rate and
// acceleration. A rocket's apparent rate grows through a pass, and a
// filter with no acceleration term lags it in exactly the way that
// walks the object off a 7' frame. Fixes arrive about ten times a
// second. With 3 px of seeing noise on every centroid, these gains were
// the only set tried that kept a rocket's opening on the sensor at three
// ticks of delay; softer ones settled as well and opened worse.
//
// ── When it is lost ─────────────────────────────────────────
//
// It coasts on the fitted motion for kCoastSec, then says so, and the
// caller stops. Coasting longer would fly the mount on a guess.
// ============================================================

#include <cmath>
#include <deque>

#include "GuideSolve.h"
#include "SatTracker.h"

namespace raduk::core {

// Where in the sky a pixel offset from the frame's centre is, as axis
// degrees from where the mount points. Positive azimuth is east of the
// mount's heading, positive altitude above it, as the rates are.
inline bool PixelOffsetToAxisDeg(const GuideResponse& resp, float altCalDeg,
                                 float rateArcsecPerSec, double ddx, double ddy,
                                 double altNowDeg, double& dAz, double& dAlt) {
    const GuideResponse r = RescaleForAltitude(resp, altCalDeg, (float)altNowDeg);
    // The pulses that would bring the object to the centre are the axis
    // move from the mount to the object.
    const GuidePulses p = SolveGuidePulses(r, (float)ddx, (float)ddy);
    if (!p.valid) return false;
    const double degPerMs = rateArcsecPerSec / 1000.0 / 3600.0;
    dAz  = p.eastMs  * degPerMs;
    dAlt = p.northMs * degPerMs;
    return true;
}

// One axis of position, rate and acceleration.
class AxisFilter {
public:
    void Reset() { m_n = 0; }
    int  Count() const { return m_n; }

    void Update(double t, double z) {
        if (m_n == 0) { m_x = z; m_v = 0; m_a = 0; m_t = t; m_n = 1; return; }
        const double dt = t - m_t;
        if (dt <= 1e-4) return;
        if (m_n == 1) {
            m_v = (z - m_x) / dt;
            m_x = z; m_t = t; m_n = 2;
            return;
        }
        const double xp = m_x + m_v * dt + 0.5 * m_a * dt * dt;
        const double vp = m_v + m_a * dt;
        const double r = z - xp;
        m_x = xp + kAlpha * r;
        m_v = vp + kBeta * r / dt;
        m_a = m_a + kGamma * 2.0 * r / (dt * dt);
        m_t = t;
        ++m_n;
    }

    double Position(double t) const {
        const double dt = t - m_t;
        return m_x + m_v * dt + 0.5 * m_a * dt * dt;
    }
    double Rate(double t) const { return m_v + m_a * (t - m_t); }

    static constexpr double kAlpha = 0.8;
    static constexpr double kBeta  = 0.55;
    static constexpr double kGamma = 0.15;

private:
    double m_x = 0, m_v = 0, m_a = 0, m_t = 0;
    int    m_n = 0;
};

class MotionLock {
public:
    static constexpr double kCoastSec = 2.0;
    static constexpr int    kReadyFixes = 3;

    void Configure(const GuideResponse& r, float altCalDeg, float rateArcsecPerSec) {
        m_resp = r; m_altCal = altCalDeg; m_rate = rateArcsecPerSec;
        const GuidePulses t = SolveGuidePulses(r, 1.0f, 0.0f);
        m_ok = t.valid && rateArcsecPerSec > 0.0f;
        Reset();
    }
    bool Calibrated() const { return m_ok; }

    void Reset() {
        m_az.Reset(); m_alt.Reset();
        m_lastFix = -1.0e9;
        m_unwrap = 0.0; m_prevAz = 0.0; m_haveAz = false;
        m_hist.clear();
    }

    // One tick: the mount's reading now and, when there is one, a
    // centroid from a frame taken at frameTime.
    void Update(double now, double mountAlt, double mountAz,
                bool haveFix, double frameTime, float x, float y, int w, int h) {
        m_hist.push_back({now, mountAlt, mountAz});
        while (m_hist.size() > 2 && m_hist.front().t < now - 5.0) m_hist.pop_front();
        if (!m_ok || !haveFix || w <= 0 || h <= 0) return;
        double fAlt = mountAlt, fAz = mountAz;
        MountAt(frameTime, fAlt, fAz);
        double dAz = 0.0, dAlt = 0.0;
        if (!PixelOffsetToAxisDeg(m_resp, m_altCal, m_rate, w * 0.5 - x, h * 0.5 - y,
                                  fAlt, dAz, dAlt)) return;
        // Unwrapped, so passing north does not look like a 360 degree jump.
        double az = fAz + dAz;
        if (m_haveAz) {
            double d = az - m_prevAz;
            if (d > 180.0) m_unwrap -= 360.0;
            if (d < -180.0) m_unwrap += 360.0;
        }
        m_prevAz = az; m_haveAz = true;
        m_az.Update(frameTime, az + m_unwrap);
        m_alt.Update(frameTime, fAlt + dAlt);
        m_lastFix = now;
    }

    bool Ready() const { return m_az.Count() >= kReadyFixes; }
    bool Lost(double now) const { return now - m_lastFix > kCoastSec; }
    bool Coasting(double now) const { return now - m_lastFix > 0.5 && !Lost(now); }

    SatTracker::Sky SkyAt(double t) const {
        SatTracker::Sky s;
        if (!Ready()) return s;
        double az = std::fmod(m_az.Position(t), 360.0);
        if (az < 0.0) az += 360.0;
        s.ok = true;
        s.azDeg = az;
        s.altDeg = m_alt.Position(t);
        s.azRate = m_az.Rate(t);
        s.altRate = m_alt.Rate(t);
        return s;
    }

private:
    struct Reading { double t, alt, az; };

    // Linear between the readings either side; the nearest end outside.
    void MountAt(double t, double& alt, double& az) const {
        if (m_hist.empty()) return;
        if (t <= m_hist.front().t) { alt = m_hist.front().alt; az = m_hist.front().az; return; }
        for (size_t i = 1; i < m_hist.size(); ++i) {
            const Reading& a = m_hist[i - 1];
            const Reading& b = m_hist[i];
            if (t > b.t) continue;
            const double k = (b.t > a.t) ? (t - a.t) / (b.t - a.t) : 0.0;
            double dz = b.az - a.az;
            if (dz > 180.0) dz -= 360.0;
            if (dz < -180.0) dz += 360.0;
            alt = a.alt + (b.alt - a.alt) * k;
            az  = a.az + dz * k;
            return;
        }
        alt = m_hist.back().alt; az = m_hist.back().az;
    }

    std::deque<Reading> m_hist;
    GuideResponse m_resp;
    float  m_altCal = -1.0f, m_rate = 0.0f;
    bool   m_ok = false, m_haveAz = false;
    AxisFilter m_az, m_alt;
    double m_lastFix = -1.0e9, m_unwrap = 0.0, m_prevAz = 0.0;
};

// Against a simulated sky, mount and rotated, mirrored camera. Whether it
// holds a real rocket is the sky's question.
inline constexpr bool MOTION_LOCK_PROVEN_ON_SKY = false;

} // namespace raduk::core
