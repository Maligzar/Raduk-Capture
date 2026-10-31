// ============================================================
//  MosaicRunner — driving the mount through the panels
//
//  Phase 97.5, the larger half of roadmap row 34. 97 worked out where
//  the panels go and 97.4 told the operator whether the mosaic would
//  work; neither moved the mount. This does.
//
//  ── The design point the whole thing turns on ──────────────
//
//  Phase 97 measured that a lunar mosaic of EIGHTEEN panels takes 8.3
//  minutes, over which the Moon moves 272 arcsec against the stars --
//  three times the whole overlap -- and concluded that the mosaic
//  does not close. That conclusion was about a mosaic laid out in
//  FIXED offsets from a position taken once at the start, which is
//  what a plan on its own can be.
//
//  Execution is not obliged to work that way, and this one does not.
//  Tick() takes the target's position NOW, every call, and the panel
//  it points at is computed against that. So the eighteenth panel is
//  aimed at where the Moon is when the eighteenth panel is captured,
//  not where it was eight minutes earlier, and the cumulative drift
//  stops existing. What is left is the drift WITHIN one panel:
//
//      20 s of capture x 0.549 arcsec/s = 11 arcsec = 54 px,
//      against a 20% overlap of 768 px -- seven per cent of it.
//
//  The stacker removes that; it is the same intra-run drift any
//  single lunar capture already has.
//
//  **So per-panel recomputation is the fix, and the tracking rate
//  97 added is the refinement.** Taking the live position is not a
//  parameter the caller may forget: it is the shape of Tick().
//
//  ── Why this file touches no hardware ──────────────────────
//
//  A mosaic is a multi-minute unattended sequence on a real telescope
//  with a real camera, and it is the first thing in this application
//  that moves the mount repeatedly without anyone's hand on it. The
//  failure modes are timing ones -- a slew that never starts, a slew
//  that never ends, an abort arriving mid-capture -- and none of them
//  can be rehearsed against hardware from here.
//
//  So the runner is a pure state machine: Tick() returns an Action
//  and the caller performs it. Every path through it is reachable
//  from a test with no mount, no camera and no clock.
// ============================================================

#pragma once

#include "MosaicPlan.h"

#include <cmath>
#include <string>

namespace raduk::sky {

class MosaicRunner {
public:
    enum class Phase {
        Idle,        // nothing running
        Slewing,     // waiting for the mount to arrive
        Settling,    // arrived; letting the tube stop ringing
        Capturing,   // the panel is being recorded
        Complete,    // every panel captured
        Aborted,     // the operator stopped it
        Failed,      // something went wrong; Reason() says what
    };

    struct Config {
        MosaicPlan  plan;
        FieldOfView fov;
        double targetArcmin   = 0.0;
        double captureSec     = 20.0;
        double settleSec      =  8.0;

        // A slew that never ends. An 8SE crossing ten arcmin takes a
        // few seconds; ninety is not a tolerance, it is a sign the
        // mount has stopped answering.
        double slewTimeoutSec = 90.0;

        // A slew that never STARTS, which is the subtler one. Issue a
        // GoTo and ask "still slewing?" immediately and the honest
        // answer is often no, because the mount has not begun. Accept
        // that and the runner marches through eighteen panels without
        // moving, capturing the same patch of Moon every time -- a
        // failure that produces a complete-looking mosaic of one
        // panel repeated. So a slew is not allowed to be finished
        // until it has had this long to start.
        double slewMinSec     =  1.5;

        // Panels holding no part of the disc. At the full sensor
        // there are none; at a 640x480 crop there are 56 of 432, and
        // every one skipped is time not spent.
        bool   skipEmptyPanels = true;
    };

    struct Action {
        enum Kind { None, Slew, BeginCapture, EndCapture };
        Kind   kind    = None;
        double raHours = 0.0;
        double decDeg  = 0.0;
        int    panel   = -1;   // index into the plan's grid
        int    ordinal = -1;   // 1..Total(), for anything a person reads
    };

    // ── Control ────────────────────────────────────────────────
    void Configure(const Config& c) { m_cfg = c; BuildOrder(); }

    void Start(double nowSec) {
        if (m_order.empty()) { Fail("nothing to capture"); return; }
        m_at      = 0;
        m_phase   = Phase::Slewing;
        m_entered = nowSec;
        m_started = nowSec;
        m_sawSlew = false;
        m_issued  = false;
        m_reason.clear();
        m_captured = 0;
    }

    // Stops wherever it is. A capture in progress is ENDED rather than
    // abandoned: the caller gets one last EndCapture from Tick so the
    // file is closed, because a half-written SER is worse than a short
    // one.
    void Abort(const char* why) {
        if (m_phase == Phase::Capturing) m_endPending = true;
        m_phase  = Phase::Aborted;
        m_reason = why ? why : "stopped";
    }

    // ── The loop ───────────────────────────────────────────────
    //
    // targetRaHours / targetDecDeg are where the target is RIGHT NOW.
    // Recomputed by the caller every tick from the ephemeris, which is
    // the whole point (see the header comment).
    //
    // mountSlewing is the mount's own answer, not an assumption.
    Action Tick(double nowSec, double targetRaHours, double targetDecDeg,
                bool mountSlewing) {
        Action a;

        // An abort that interrupted a capture still owes one
        // EndCapture, and it is owed before anything else.
        if (m_endPending) {
            m_endPending = false;
            a.kind = Action::EndCapture;
            a.panel = CurrentPanel();
            a.ordinal = m_at + 1;
            return a;
        }
        if (m_phase == Phase::Idle || m_phase == Phase::Complete ||
            m_phase == Phase::Aborted || m_phase == Phase::Failed)
            return a;

        const double in = nowSec - m_entered;

        switch (m_phase) {
        case Phase::Slewing: {
            if (!m_issued) {
                m_issued = true;
                a = SlewAction(targetRaHours, targetDecDeg);
                return a;
            }
            // The timeout is checked FIRST, and unconditionally.
            //
            // The first draft checked it only on the not-slewing path,
            // because the slewing path did `break` straight out. A
            // mount that reports Slewing forever -- which is exactly
            // what a stuck or disconnected one does -- therefore never
            // timed out and the run hung silently. That is the one
            // failure the timeout exists for, and it was the one it
            // could not catch.
            if (in > m_cfg.slewTimeoutSec) {
                Fail(m_sawSlew ? "slew did not finish"
                               : "slew did not start");
                break;
            }
            if (mountSlewing) { m_sawSlew = true; break; }
            // Not slewing. Believe it only once the mount has had long
            // enough to start, or once we have actually seen it move.
            if (m_sawSlew || in >= m_cfg.slewMinSec) {
                m_phase = Phase::Settling;
                m_entered = nowSec;
            }
            break;
        }
        case Phase::Settling:
            if (in >= m_cfg.settleSec) {
                m_phase = Phase::Capturing;
                m_entered = nowSec;
                a.kind = Action::BeginCapture;
                a.panel = CurrentPanel();
                a.ordinal = m_at + 1;
                return a;
            }
            break;

        case Phase::Capturing:
            if (in >= m_cfg.captureSec) {
                a.kind = Action::EndCapture;
                a.panel = CurrentPanel();
                a.ordinal = m_at + 1;
                ++m_captured;
                Advance(nowSec);
                return a;
            }
            break;

        default: break;
        }
        return a;
    }

    // ── Readout ────────────────────────────────────────────────
    Phase       State()      const { return m_phase; }
    int         Total()      const { return (int)m_order.size(); }
    int         Captured()   const { return m_captured; }
    int         Ordinal()    const { return m_at + 1; }
    const std::string& Reason() const { return m_reason; }
    bool Running() const {
        return m_phase == Phase::Slewing || m_phase == Phase::Settling ||
               m_phase == Phase::Capturing;
    }

    // Seconds still to go, from where it is. Each remaining panel
    // costs a settle and a capture; the one in hand costs whatever is
    // left of it. The slew itself is not counted because its duration
    // is the mount's business and not known here -- so this is a floor,
    // and is described as one wherever it is shown.
    double SecondsRemaining(double nowSec) const {
        if (!Running()) return 0.0;
        const int left = Total() - m_at - 1;
        double s = left * (m_cfg.settleSec + m_cfg.captureSec);
        const double in = nowSec - m_entered;
        if (m_phase == Phase::Settling)
            s += (m_cfg.settleSec - in) + m_cfg.captureSec;
        else if (m_phase == Phase::Capturing)
            s += (m_cfg.captureSec - in);
        else
            s += m_cfg.settleSec + m_cfg.captureSec;
        return s < 0.0 ? 0.0 : s;
    }

    // Where a given step points, for a dry run or a preview. Same
    // arithmetic Tick uses, so a preview cannot disagree with what
    // actually happens.
    Action StepAt(int step, double targetRaHours, double targetDecDeg) const {
        Action a;
        if (step < 0 || step >= (int)m_order.size()) return a;
        const Panel q = PanelAt(m_cfg.plan, m_order[(size_t)step]);
        const MountOffset off = RaDecOffset(q.eastArcsec, q.northArcsec,
                                            targetDecDeg);
        a.kind    = Action::Slew;
        a.panel   = m_order[(size_t)step];
        a.ordinal = step + 1;
        // RA in hours: arcsec -> degrees -> hours.
        a.raHours = targetRaHours + off.raArcsec / 3600.0 / 15.0;
        a.decDeg  = targetDecDeg  + off.decArcsec / 3600.0;
        return a;
    }

private:
    void BuildOrder() {
        m_order.clear();
        m_phase = Phase::Idle;
        m_at = 0;
        m_captured = 0;

        // A configuration with no geometry is not a one-panel mosaic,
        // it is a mistake. The first draft let a default-constructed
        // Config through because a default MosaicPlan is 1x1 and the
        // skip test is disabled when targetArcmin is zero -- so Start
        // happily began slewing somewhere on behalf of a target with
        // no size, in a frame with no size.
        if (m_cfg.fov.widthArcmin  <= 0.0 ||
            m_cfg.fov.heightArcmin <= 0.0 ||
            m_cfg.targetArcmin     <= 0.0) return;

        const int n = m_cfg.plan.cols * m_cfg.plan.rows;
        for (int i = 0; i < n; ++i) {
            if (m_cfg.skipEmptyPanels && m_cfg.targetArcmin > 0.0 &&
                !PanelTouchesDisc(m_cfg.plan, PanelAt(m_cfg.plan, i),
                                  m_cfg.fov, m_cfg.targetArcmin))
                continue;
            m_order.push_back(i);
        }
    }

    int CurrentPanel() const {
        return (m_at >= 0 && m_at < (int)m_order.size())
             ? m_order[(size_t)m_at] : -1;
    }

    Action SlewAction(double ra, double dec) const {
        Action a = StepAt(m_at, ra, dec);
        return a;
    }

    void Advance(double nowSec) {
        ++m_at;
        if (m_at >= (int)m_order.size()) {
            m_phase = Phase::Complete;
            return;
        }
        m_phase   = Phase::Slewing;
        m_entered = nowSec;
        m_sawSlew = false;
        m_issued  = false;
    }

    void Fail(const char* why) {
        m_phase  = Phase::Failed;
        m_reason = why ? why : "failed";
    }

    Config m_cfg;
    std::vector<int> m_order;
    Phase  m_phase    = Phase::Idle;
    int    m_at       = 0;
    int    m_captured = 0;
    double m_entered  = 0.0;
    double m_started  = 0.0;
    bool   m_sawSlew  = false;
    bool   m_issued   = false;
    bool   m_endPending = false;
    std::string m_reason;
};

// Nothing here has driven a real mount. Every state and every
// transition is exercised by tests/mosaic_run.cpp against a fake
// clock and a fake mount, which is a different claim.
inline constexpr bool MOSAIC_RUN_PROVEN_ON_SKY = false;

} // namespace raduk::sky
