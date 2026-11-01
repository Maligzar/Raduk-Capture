#pragma once
// ============================================================
//  AutorunRunner — a night's runs without a hand on RECORD (phase 113)
//
//  Surface detail comes from many clean runs, each inside the
//  derotation window and each on fresh focus. This repeats that:
//  check, focus if due, record, gap, again.
//
//  It is a pure state machine for the same reason MosaicRunner is.
//  It runs for hours with nobody watching, its failures are timing
//  ones, and none of them can be rehearsed on a telescope. Tick()
//  returns an Action and the caller performs it, so every path can
//  be reached from a test with no camera and no clock.
//
//  The mount is not touched. The focuser moves only between runs.
// ============================================================

#include <cstdint>
#include <string>

namespace raduk::core {

class AutorunRunner {
public:
    enum class Phase {
        Idle,
        Waiting,    // the gap between runs, then the checks
        Focusing,
        Recording,
        Complete,   // finished as intended; Reason() says how
        Aborted,    // somebody stopped it
        Failed,     // something went wrong; Reason() says what
    };

    struct Config {
        int    runs          = 10;
        double runSec        = 0.0;    // how long one run should take
        double gapSec        = 15.0;   // AE gets this to correct
        int    refocusEvery  = 3;      // 0 never focuses
        double minAltDeg     = 20.0;

        // A target that has left the frame gets this long to come back.
        double holdLimitSec  = 120.0;

        // A sweep that never ends. Five minutes is well past the longest
        // scan the routine can run.
        double focusLimitSec = 300.0;

        // Empty runs in a row before believing it. The gate skips every
        // frame under cloud, and a file of nothing helps nobody.
        int    maxEmptyRuns  = 2;

        // Phase 114. Wait for the sky before a sweep and before a run,
        // this long at most: a run in poor seeing beats no run at all.
        bool   holdForSeeing = false;
        double seeingHoldSec = 180.0;
    };

    enum class Action { None, StartFocus, StartRun, StopRun, AbortFocus };

    // What the rig says this tick. The caller fills the last four only
    // when AboutToCheck() is true: the disk call can be slow.
    struct Inputs {
        bool     recording   = false;
        bool     focusing    = false;
        uint32_t frames      = 0;       // in the current or latest run
        bool     targetSeen  = false;
        double   altDeg      = -1.0;    // 0 or below is unknown
        bool     diskHasRoom = false;
        bool     seeingOpen  = false;
    };

    // Why a sequence may not start. nullptr means it may.
    static const char* Refusal(const Config& c) {
        if (c.runs < 1)         return "no runs asked for";
        if (c.runSec <= 0.0)    return "no run length: set a limit";
        if (c.gapSec < 0.0)     return "the gap is negative";
        if (c.refocusEvery < 0) return "the refocus interval is negative";
        return nullptr;
    }

    // The longest one run may take: half again what was expected, and a
    // minute more so a short run is not held to a few seconds.
    static double RunLimitSec(double runSec) {
        return runSec * 1.5 + 60.0;
    }

    bool Start(const Config& c, double nowSec) {
        if (const char* why = Refusal(c)) { Fail(why); return false; }
        m_cfg       = c;
        m_done      = 0;
        m_empty     = 0;
        m_emptyRow  = 0;
        m_focusBad  = 0;
        m_sinceFocus = 0;
        m_focusDue  = c.refocusEvery > 0;
        m_held      = false;
        m_seeingHeld   = false;
        m_seeingWaived = false;
        m_seeingGaveUp = 0;
        m_stopPending = false;
        m_focusStopPending = false;
        m_reason.clear();
        Enter(Phase::Waiting, nowSec);
        m_waitSec = 0.0;      // the first run has no gap before it
        return true;
    }

    // Stops wherever it is. A run in progress is closed rather than
    // abandoned, by one last StopRun from Tick.
    void Abort(const char* why) {
        if (!Running()) return;
        if (m_phase == Phase::Recording) m_stopPending = true;
        if (m_phase == Phase::Focusing)  m_focusStopPending = true;
        m_phase  = Phase::Aborted;
        m_reason = why ? why : "stopped";
    }

    // The caller could not start the run it was told to.
    void RunRefused() {
        m_phase  = Phase::Failed;
        m_reason = "the recording would not start";
    }

    // The sweep ended without finding focus. It restores where it began,
    // so the runs go on, and the summary says it happened.
    void NoteFocusFailed() { ++m_focusBad; }

    // True when Tick will run the checks, so the caller knows to fill them.
    bool AboutToCheck(double nowSec) const {
        return m_phase == Phase::Waiting && nowSec - m_entered >= m_waitSec;
    }

    Action Tick(double nowSec, const Inputs& in) {
        // Owed from an Abort or a Fail, ahead of everything else.
        if (m_stopPending) {
            m_stopPending = false;
            return Action::StopRun;
        }
        if (m_focusStopPending) {
            m_focusStopPending = false;
            return Action::AbortFocus;
        }

        const double elapsed = nowSec - m_entered;

        switch (m_phase) {
        case Phase::Waiting:
            if (elapsed < m_waitSec) break;
            return Check(nowSec, in);

        case Phase::Focusing:
            // Checked first: a focuser stuck busy is exactly what this
            // limit is for.
            if (elapsed > m_cfg.focusLimitSec) {
                Fail("the focus sweep did not finish");
                return Action::AbortFocus;
            }
            if (in.focusing) break;
            m_focusDue   = false;
            m_sinceFocus = 0;
            m_held       = false;
            Enter(Phase::Waiting, nowSec);
            m_waitSec = 0.0;
            break;

        case Phase::Recording:
            if (!in.recording) {
                RunEnded(nowSec, in.frames);
                break;
            }
            // Every stop in CaptureManager is checked when a frame
            // arrives, so a camera that died mid-run never reaches any of
            // them and the run would go on for ever.
            if (elapsed > RunLimitSec(m_cfg.runSec)) {
                Fail("a run did not end: is the camera still sending?");
                return Action::StopRun;
            }
            break;

        default:
            break;
        }
        return Action::None;
    }

    // ── Readout ────────────────────────────────────────────────
    Phase State() const { return m_phase; }
    int   Done()  const { return m_done; }
    int   Total() const { return m_cfg.runs; }
    double RunSec() const { return m_cfg.runSec; }
    int   Empty() const { return m_empty; }
    int   FocusFailures() const { return m_focusBad; }
    int   SeeingGaveUp() const { return m_seeingGaveUp; }
    // Waiting on the sky, for anything a person reads.
    bool  HoldingForSeeing() const {
        return m_phase == Phase::Waiting && m_seeingHeld;
    }
    const std::string& Reason() const { return m_reason; }
    bool Running() const {
        return m_phase == Phase::Waiting || m_phase == Phase::Focusing ||
               m_phase == Phase::Recording;
    }
    // Which run is under way or next, from 1.
    int Ordinal() const { return m_done + 1; }

private:
    Action Check(double nowSec, const Inputs& in) {
        // Below the floor it is not coming back tonight, so this is an
        // end rather than a fault. Checked ahead of the target because a
        // sinking planet may have left the frame for that reason.
        if (in.altDeg > 0.0 && in.altDeg < m_cfg.minAltDeg) {
            Finish("the target went below the altitude floor");
            return Action::None;
        }
        if (!in.diskHasRoom) {
            Fail("no room on the capture disk for another run");
            return Action::None;
        }
        if (!in.targetSeen) {
            if (!m_held) { m_held = true; m_heldSince = nowSec; }
            if (nowSec - m_heldSince > m_cfg.holdLimitSec)
                Fail("the target left the frame and did not come back");
            return Action::None;
        }
        m_held = false;

        // Before the sweep as well as the run: a V-curve fitted in bad
        // seeing is a worse focus. Giving up holds for the whole cycle,
        // or the wait would start again after the sweep.
        if (m_cfg.holdForSeeing && !m_seeingWaived) {
            if (!in.seeingOpen) {
                if (!m_seeingHeld) { m_seeingHeld = true; m_seeingSince = nowSec; }
                if (nowSec - m_seeingSince <= m_cfg.seeingHoldSec)
                    return Action::None;
                m_seeingWaived = true;
                ++m_seeingGaveUp;
            }
            m_seeingHeld = false;
        }

        if (m_focusDue) {
            Enter(Phase::Focusing, nowSec);
            return Action::StartFocus;
        }
        m_seeingWaived = false;
        Enter(Phase::Recording, nowSec);
        return Action::StartRun;
    }

    void RunEnded(double nowSec, uint32_t frames) {
        ++m_done;
        ++m_sinceFocus;
        if (frames == 0) { ++m_empty; ++m_emptyRow; }
        else             { m_emptyRow = 0; }

        if (m_emptyRow >= m_cfg.maxEmptyRuns) {
            Fail("runs are coming back empty: cloud, or the gate is shut");
            return;
        }
        if (m_done >= m_cfg.runs) {
            Finish("all runs done");
            return;
        }
        if (m_cfg.refocusEvery > 0 && m_sinceFocus >= m_cfg.refocusEvery)
            m_focusDue = true;
        Enter(Phase::Waiting, nowSec);
        m_waitSec = m_cfg.gapSec;
    }

    void Enter(Phase p, double nowSec) { m_phase = p; m_entered = nowSec; }
    void Finish(const char* why) { m_phase = Phase::Complete; m_reason = why; }
    void Fail(const char* why)   { m_phase = Phase::Failed;   m_reason = why; }

    Config m_cfg;
    Phase  m_phase      = Phase::Idle;
    double m_entered    = 0.0;
    double m_waitSec    = 0.0;
    double m_heldSince  = 0.0;
    bool   m_held       = false;
    int    m_done       = 0;
    int    m_empty      = 0;
    int    m_emptyRow   = 0;
    int    m_focusBad   = 0;
    int    m_sinceFocus = 0;
    bool   m_focusDue   = false;
    bool   m_seeingHeld   = false;
    bool   m_seeingWaived = false;
    double m_seeingSince  = 0.0;
    int    m_seeingGaveUp = 0;
    bool   m_stopPending = false;
    bool   m_focusStopPending = false;
    std::string m_reason;
};

// How long a run will take: the derotation window when there is one,
// because it is what the run is held to. Otherwise the limit that is set.
// 0 means unknown, which Refusal turns away. A frame limit needs the rate.
inline double AutorunRunSec(double derotWindowSec, bool limitByFrames,
                            uint32_t limitFrames, double limitTimeSec,
                            double fps) {
    double limit = 0.0;
    if (limitByFrames) {
        if (limitFrames > 0 && fps > 0.0) limit = limitFrames / fps;
    } else {
        limit = limitTimeSec > 0.0 ? limitTimeSec : 0.0;
    }
    if (derotWindowSec <= 0.0) return limit;
    return (limit > 0.0 && limit < derotWindowSec) ? limit : derotWindowSec;
}

// Bytes one run will write, a tenth over for the sidecars and the
// frames a rate estimate misses.
inline uint64_t AutorunRunBytes(int width, int height, int bitDepth,
                                double fps, double runSec) {
    if (width <= 0 || height <= 0 || fps <= 0.0 || runSec <= 0.0) return 0;
    const double frame = (double)width * height * (bitDepth > 8 ? 2 : 1);
    return (uint64_t)(frame * fps * runSec * 1.1);
}

// Nothing here has run through a night on a real rig. Every path is
// exercised by tests/autorun.cpp, which is a different claim.
inline constexpr bool AUTORUN_PROVEN_ON_SKY = false;

} // namespace raduk::core
