#pragma once
// ============================================================
//  FocuserGuard.h -- phase 161.0a: a focuser move that cannot start
//  from a number nobody measured
//
//  From the first-principles review of 2026-11-14. Before this, a failed
//  position read came back as 0, and that 0 was the base of a relative
//  step, of autofocus's sweep and of the position a profile saved: one
//  USB hiccup could send the 8SE's mirror 168,000 steps toward the end
//  of its travel. Nothing checked that a move was accepted, or that the
//  focuser stopped where it was sent.
//
//  Here, all pure (no SDK, no Windows), shared by the EAF wrapper and
//  the emulator so the two cannot drift:
//    CheckMove        may this move start? Not from an unknown position,
//                     not outside the focuser's range, and not further
//                     than the travel guard unless the caller says so.
//    RelativeBase     a relative step adds to the commanded target while
//                     a move is in flight, not to wherever the motor is.
//    MoveLedger       the last good position, the commanded target, the
//                     leg in flight, and why the last move was refused.
//    FarMoveConfirm   a move past the guard asks for a second press.
//
//  Gated by tests/focuser_safety.cpp.
// ============================================================

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace raduk::core {

// One command may travel this far unless the caller overrides it: about
// two turns of the 8SE's knob at the EAF's 5,760 steps a turn. rig.ini
// FocuserMaxTravelSteps; 0 turns the guard off.
// UNVERIFIED(item 59): the travel guard is wide enough for a night's focus moves and autofocus on the rig
inline constexpr int kDefaultMaxTravelSteps = 12000;

// A move has arrived when the focuser has stopped within this of where
// it was sent.
inline constexpr int kArrivalToleranceSteps = 2;

// A focuser that reports "not moving" short of its target is given this
// long (the motor may not have started yet) before the move is called
// short and given up.
inline constexpr int kShortOfTargetGraceMs = 2000;

enum class MoveRefusal {
    None,
    NotOpen,
    PositionUnknown,   // the position could not be read: no base to move from
    OutOfRange,        // below 0 or beyond the focuser's own maximum
    TooFar,            // further than the travel guard, and not overridden
    Device,            // the focuser itself refused the command
    StoppedShort,      // it stopped, but not where it was sent
};

inline const char* DescribeRefusal(MoveRefusal r) {
    switch (r) {
        case MoveRefusal::None:            return "";
        case MoveRefusal::NotOpen:         return "no focuser connected";
        case MoveRefusal::PositionUnknown: return "the focuser's position could not be read";
        case MoveRefusal::OutOfRange:      return "the target is outside the focuser's range";
        case MoveRefusal::TooFar:          return "the move is longer than FocuserMaxTravelSteps";
        case MoveRefusal::Device:          return "the focuser refused the command";
        case MoveRefusal::StoppedShort:    return "the focuser stopped short of its target";
    }
    return "?";
}

// May a move from `from` to `to` start? maxStep <= 0: no upper limit
// known. maxTravel <= 0: the guard is off. pastGuard: the caller overrides the
// guard (and nothing else).
inline MoveRefusal CheckMove(bool positionKnown, int from, int to,
                             int maxStep, int maxTravel, bool pastGuard) {
    if (!positionKnown) return MoveRefusal::PositionUnknown;
    if (to < 0 || (maxStep > 0 && to > maxStep)) return MoveRefusal::OutOfRange;
    if (!pastGuard && maxTravel > 0 &&
        std::llabs((long long)to - (long long)from) > (long long)maxTravel)
        return MoveRefusal::TooFar;
    return MoveRefusal::None;
}

// What a relative step adds to: the commanded target while a move is in
// flight (commandedTarget >= 0), else the position.
inline int RelativeBase(int commandedTarget, int position) {
    return commandedTarget >= 0 ? commandedTarget : position;
}

// Thread-safe: autofocus's thread and the UI both use the focuser.
class MoveLedger {
public:
    using Clock = std::chrono::steady_clock;

    // ── The position ──────────────────────────────────────────
    void Good(int pos) {
        std::lock_guard<std::mutex> lk(m_mx);
        m_last = pos; m_known = true; m_lastReadOk = true;
    }
    void ReadFailed() { std::lock_guard<std::mutex> lk(m_mx); m_lastReadOk = false; ++m_readFailures; }
    bool Known() const      { std::lock_guard<std::mutex> lk(m_mx); return m_known; }
    bool LastReadOk() const { std::lock_guard<std::mutex> lk(m_mx); return m_lastReadOk; }
    int  Last() const       { std::lock_guard<std::mutex> lk(m_mx); return m_last; }
    int  ReadFailures() const { std::lock_guard<std::mutex> lk(m_mx); return m_readFailures; }

    // ── The move in flight ────────────────────────────────────
    // `target` is where the whole move ends; `leg` what the motor was
    // just told (they differ on the first leg of a one-sided move).
    void Commanded(int target, int leg) {
        std::lock_guard<std::mutex> lk(m_mx);
        m_target = target; m_leg = leg; m_shortSince = Clock::time_point{};
        m_refusal = MoveRefusal::None; m_refusalText.clear();
    }
    void Leg(int leg) {
        std::lock_guard<std::mutex> lk(m_mx);
        m_leg = leg; m_shortSince = Clock::time_point{};
    }
    void Idle() {
        std::lock_guard<std::mutex> lk(m_mx);
        m_target = -1; m_leg = -1; m_shortSince = Clock::time_point{};
    }
    int Target() const { std::lock_guard<std::mutex> lk(m_mx); return m_target; }

    enum class LegState { Arrived, Waiting, Short };
    // The motor reports "not moving" at `at`. Arrived: within tolerance
    // of the leg. Waiting: short of it, inside the grace. Short: still
    // short after the grace, so the move is over and did not get there.
    LegState StoppedAt(int at, Clock::time_point now) {
        std::lock_guard<std::mutex> lk(m_mx);
        if (m_leg < 0 || std::abs(at - m_leg) <= kArrivalToleranceSteps) {
            m_shortSince = Clock::time_point{};
            return LegState::Arrived;
        }
        if (m_shortSince == Clock::time_point{}) { m_shortSince = now; return LegState::Waiting; }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_shortSince).count();
        return ms >= kShortOfTargetGraceMs ? LegState::Short : LegState::Waiting;
    }

    // ── Why the last move did not happen ──────────────────────
    MoveRefusal Refuse(MoveRefusal r, int from, int to, int maxTravel) {
        std::lock_guard<std::mutex> lk(m_mx);
        m_refusal = r;
        char b[200];
        switch (r) {
            case MoveRefusal::TooFar:
                std::snprintf(b, sizeof(b), "%d to %d is %lld steps, more than FocuserMaxTravelSteps (%d)",
                              from, to, std::llabs((long long)to - (long long)from), maxTravel);
                break;
            case MoveRefusal::OutOfRange:
                std::snprintf(b, sizeof(b), "%d is outside the focuser's range", to);
                break;
            case MoveRefusal::StoppedShort:
                std::snprintf(b, sizeof(b), "the focuser stopped at %d, not %d", from, to);
                break;
            default:
                std::snprintf(b, sizeof(b), "%s", DescribeRefusal(r));
        }
        m_refusalText = b;
        return r;
    }
    MoveRefusal LastRefusal() const { std::lock_guard<std::mutex> lk(m_mx); return m_refusal; }
    std::string LastRefusalText() const { std::lock_guard<std::mutex> lk(m_mx); return m_refusalText; }

private:
    mutable std::mutex m_mx;
    bool  m_known = false, m_lastReadOk = false;
    int   m_last = 0, m_readFailures = 0;
    int   m_target = -1, m_leg = -1;
    Clock::time_point m_shortSince{};
    MoveRefusal m_refusal = MoveRefusal::None;
    std::string m_refusalText;
};

// A move past the travel guard asks for a second press: the same target
// asked for again within the window. Single-threaded (the UI's).
class FarMoveConfirm {
public:
    static constexpr double kWindowSec = 5.0;
    bool Confirmed(int target, double nowSec) {
        if (m_armed && target == m_target && nowSec <= m_until) { m_armed = false; return true; }
        m_armed = true; m_target = target; m_until = nowSec + kWindowSec;
        return false;
    }
private:
    bool   m_armed = false;
    int    m_target = 0;
    double m_until = 0.0;
};

}  // namespace raduk::core
