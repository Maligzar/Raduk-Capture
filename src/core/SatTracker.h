// ============================================================
//  SatTracker — driving the mount along a pass
//
//  Phase 116, the high-rate half. Everything else that moves this
//  mount either asks it to go somewhere and waits, or nudges it. This
//  is the first thing that FLIES it: both axes under continuous rate
//  command, at up to 3 deg/s, on a telescope in the dark, possibly
//  with nobody's hand near it. So what matters most here is how it
//  stops.
//
//  ── Pure, for the reason MosaicRunner is ────────────────────
//
//  Tick() takes what the sky and the mount say and returns what to do.
//  There is no clock, no mount and no thread here, so every way of
//  going wrong can be rehearsed by a test: a mount that stops
//  answering, axes wired the wrong way round, a stall, a slew that
//  never ends.
//
//  ── The control law ─────────────────────────────────────────
//
//  The rate the pass needs, plus a proportion of the measured error.
//  The feed-forward carries nearly all of it; at 1 deg/s the mount is
//  going to be most of a degree behind in one second without it, and
//  no gain a 5 Hz loop can use would close that. Each axis is then
//  clamped to the mount's top rate and to an acceleration a heavy tube
//  can follow, because stepping an 8SE to 1 deg/s is a good way to
//  skip a gear.
//
//  ── What stops it ───────────────────────────────────────────
//
//  - The end of the window. A normal finish stops both axes.
//  - Leaving the altitude band, measured by the MOUNT, not predicted.
//    A floor and a ceiling hold even if the pass is wrong.
//  - An error past hardErrDeg. This is the guard for the case that
//    cannot be tested indoors: if an axis runs backwards on the real
//    mount, the error grows at twice the sky rate and this catches it
//    within a few seconds, at any tracking state.
//  - An error that stays past lostErrDeg when the mount is not
//    saturated. It is stalled, or slipping.
//  - No position from the mount for staleSec.
//  - Abort, which is what Escape and any jog come to.
//
//  Every path that ends a run emits Stop, and a capture in progress
//  gets its EndCapture in the same breath.
// ============================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <string>

namespace raduk::core {

class SatTracker {
public:
    enum class Phase {
        Idle,
        Slewing,     // GoTo to where the window begins
        Holding,     // arrived; waiting for it to rise
        Acquiring,   // moving, and closing on the satellite
        Tracking,    // on it, and recording
        Complete,
        Aborted,
        Failed,
    };

    struct Config {
        double startSec = 0.0, endSec = 0.0;
        double maxRateDeg  = 3.0;     // deg/s, either axis
        double maxAccelDeg = 2.0;     // deg/s^2
        double kp          = 0.8;     // 1/s
        double minAltDeg   = 15.0;
        double maxAltDeg   = 80.0;
        double altMarginDeg = 3.0;    // past the band before it stops
        double acquireDeg  = 0.25;    // close enough to call it on
        double acquireHoldSec = 1.0;
        double acquireLimitSec = 30.0;
        double lostErrDeg  = 0.75;   // ten times the frame's half-height
        double lostHoldSec = 3.0;
        double hardErrDeg  = 15.0;
        double staleSec    = 2.0;
        double slewTimeoutSec = 120.0;
        double slewMinSec  = 1.5;
        // Phase 122: the object is already in frame, so there is nowhere
        // to slew to: start holding, and move at startSec.
        bool   skipSlew    = false;
    };

    struct Sky {
        bool   ok = false;
        double altDeg = 0.0, azDeg = 0.0;
        double altRate = 0.0, azRate = 0.0;    // deg/s
    };

    struct Output {
        bool   goTo = false;           // to the window's start
        bool   trackingOff = false;    // before the first rate
        bool   rates = false;
        bool   stop = false;           // both axes, and restore tracking
        bool   beginCapture = false;
        bool   endCapture = false;
        double azRate = 0.0, altRate = 0.0;    // deg/s, signed
    };

    void Configure(const Config& c) { m_cfg = c; m_phase = Phase::Idle; }

    void Start(double nowSec) {
        m_phase = m_cfg.skipSlew ? Phase::Holding : Phase::Slewing;
        m_entered = nowSec;
        m_issued = false;
        m_sawSlew = false;
        m_cmdAz = m_cmdAlt = 0.0;
        m_lastTick = nowSec;
        m_lastGood = nowSec;
        m_capturing = false;
        m_nearSince = -1.0;
        m_lostSince = -1.0;
        m_abort = false;
        m_fault = false;
        m_reason.clear();
        m_errDeg = 0.0;
        m_peakErrDeg = 0.0;
        m_saturated = false;
        m_wasOnTarget = false;
    }

    void Abort(const char* why) {
        if (!Running()) return;
        m_abort = true;
        m_reason = why ? why : "stopped";
    }

    Output Tick(double now, const Sky& sky, bool mountOk,
                double mountAlt, double mountAz, bool mountSlewing) {
        Output o;
        if (!Running()) return o;

        if (m_abort) { Finish(o, Phase::Aborted, nullptr); return o; }
        if (mountOk) m_lastGood = now;
        const double in = now - m_entered;

        switch (m_phase) {
        case Phase::Slewing:
            if (!m_issued) { m_issued = true; o.goTo = true; return o; }
            if (now >= m_cfg.endSec) {
                FinishHarmless(o, "the pass ended before the slew did");
                break;
            }
            if (in > m_cfg.slewTimeoutSec) {
                Finish(o, Phase::Failed, m_sawSlew ? "slew did not finish"
                                                   : "slew did not start");
                break;
            }
            if (mountSlewing) { m_sawSlew = true; break; }
            if (m_sawSlew || in >= m_cfg.slewMinSec) Enter(Phase::Holding, now);
            break;

        case Phase::Holding:
            if (now < m_cfg.startSec) break;
            if (now >= m_cfg.endSec) {
                FinishHarmless(o, "the pass was over");
                break;
            }
            o.trackingOff = true;
            m_lastTick = now;
            m_lastGood = now;
            Enter(Phase::Acquiring, now);
            break;

        case Phase::Acquiring:
        case Phase::Tracking:
            Fly(o, now, sky, mountOk, mountAlt, mountAz);
            break;

        default: break;
        }
        return o;
    }

    Phase State() const { return m_phase; }
    bool Running() const {
        return m_phase == Phase::Slewing || m_phase == Phase::Holding ||
               m_phase == Phase::Acquiring || m_phase == Phase::Tracking;
    }
    const std::string& Reason() const { return m_reason; }

    // Phase 118. A failure that says something about the telescope, as
    // against one that only cost this pass. A night plan stops on the
    // first kind: the next pass would meet the same fault, unattended.
    bool TelescopeFault() const { return m_fault; }
    double ErrorDeg() const { return m_errDeg; }         // on the sky
    double PeakErrorDeg() const { return m_peakErrDeg; } // once on target
    // Phase 127: whether it ever got there. A run stopped while slewing or
    // holding reported "worst error 0.00 deg", which read as perfect.
    bool   WasOnTarget()  const { return m_wasOnTarget; }
    bool Saturated() const { return m_saturated; }

private:
    void Enter(Phase p, double now) { m_phase = p; m_entered = now; }

    void Finish(Output& o, Phase p, const char* why) {
        // Stop first in every case. A run that has ended is not a state
        // the axes are allowed to find out about later.
        o.stop = true;
        o.rates = false;
        o.azRate = o.altRate = 0.0;
        if (m_capturing) { o.endCapture = true; m_capturing = false; }
        m_phase = p;
        if (why) m_reason = why;
        m_fault = p == Phase::Failed && !m_harmless;
    }

    void FinishHarmless(Output& o, const char* why) {
        m_harmless = true;
        Finish(o, Phase::Failed, why);
        m_harmless = false;
    }

    static double Wrap(double d) {
        d = std::fmod(d, 360.0);
        if (d > 180.0)  d -= 360.0;
        if (d < -180.0) d += 360.0;
        return d;
    }

    static double Clamp(double v, double lim) {
        return v > lim ? lim : (v < -lim ? -lim : v);
    }

    double Limit(double want, double last, double dt) {
        const double a = m_cfg.maxAccelDeg * dt;
        const double r = last + Clamp(want - last, a);
        return Clamp(r, m_cfg.maxRateDeg);
    }

    void Fly(Output& o, double now, const Sky& sky, bool mountOk,
             double mountAlt, double mountAz) {
        if (now >= m_cfg.endSec) { Finish(o, Phase::Complete, nullptr); return; }

        if (!sky.ok) {
            Finish(o, Phase::Failed, "the prediction stopped answering");
            return;
        }
        if (!mountOk) {
            if (now - m_lastGood > m_cfg.staleSec)
                Finish(o, Phase::Failed, "the mount stopped answering");
            return;              // last rates stand until then
        }
        if (mountAlt < m_cfg.minAltDeg - m_cfg.altMarginDeg ||
            mountAlt > m_cfg.maxAltDeg + m_cfg.altMarginDeg) {
            Finish(o, Phase::Failed, "the mount left the safe altitude band");
            return;
        }

        const double errAlt = sky.altDeg - mountAlt;
        const double errAz  = Wrap(sky.azDeg - mountAz);
        const double cosAlt = std::cos(mountAlt * 0.017453292519943295);
        m_errDeg = std::hypot(errAz * cosAlt, errAlt);

        if (m_errDeg > m_cfg.hardErrDeg) {
            Finish(o, Phase::Failed,
                   "the mount is moving away from the satellite: check the "
                   "axis directions");
            return;
        }

        const double wantAz  = sky.azRate  + m_cfg.kp * errAz;
        const double wantAlt = sky.altRate + m_cfg.kp * errAlt;
        m_saturated = std::fabs(wantAz)  >= m_cfg.maxRateDeg ||
                      std::fabs(wantAlt) >= m_cfg.maxRateDeg;

        double dt = now - m_lastTick;
        if (dt <= 0.0) dt = 0.001;
        if (dt > 1.0)  dt = 1.0;
        m_lastTick = now;
        m_cmdAz  = Limit(wantAz,  m_cmdAz,  dt);
        m_cmdAlt = Limit(wantAlt, m_cmdAlt, dt);
        o.rates = true;
        o.azRate = m_cmdAz;
        o.altRate = m_cmdAlt;

        if (m_phase == Phase::Acquiring) {
            if (m_errDeg <= m_cfg.acquireDeg) {
                if (m_nearSince < 0.0) m_nearSince = now;
                if (now - m_nearSince >= m_cfg.acquireHoldSec) {
                    Enter(Phase::Tracking, now);
                    m_wasOnTarget = true;
                    o.beginCapture = true;
                    m_capturing = true;
                    m_lostSince = -1.0;
                }
            } else {
                m_nearSince = -1.0;
                if (!m_saturated &&
                    now - m_entered > m_cfg.acquireLimitSec) {
                    FinishHarmless(o, "never caught the satellite");
                }
            }
            return;
        }

        if (m_errDeg > m_peakErrDeg) m_peakErrDeg = m_errDeg;
        // Falling behind through the keyhole is expected and it ends by
        // itself. Falling behind at a rate the mount has is a fault.
        if (m_errDeg > m_cfg.lostErrDeg && !m_saturated) {
            if (m_lostSince < 0.0) m_lostSince = now;
            if (now - m_lostSince >= m_cfg.lostHoldSec)
                Finish(o, Phase::Failed,
                       "the mount fell behind at a rate it should hold");
        } else {
            m_lostSince = -1.0;
        }
    }

    Config m_cfg;
    Phase  m_phase = Phase::Idle;
    double m_entered = 0.0, m_lastTick = 0.0, m_lastGood = 0.0;
    double m_nearSince = -1.0, m_lostSince = -1.0;
    double m_cmdAz = 0.0, m_cmdAlt = 0.0;
    double m_errDeg = 0.0, m_peakErrDeg = 0.0;
    bool   m_wasOnTarget = false;
    bool   m_issued = false, m_sawSlew = false;
    bool   m_capturing = false, m_abort = false, m_saturated = false;
    bool   m_fault = false, m_harmless = false;
    std::string m_reason;
};

// Exercised against a simulated mount with latency and a rate and
// acceleration limit. The real tube, its backlash and its gears are
// clear-night item 39.
inline constexpr bool SAT_TRACKING_PROVEN_ON_SKY = false;

} // namespace raduk::core
