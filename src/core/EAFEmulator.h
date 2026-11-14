#pragma once
// ============================================================
// EAFEmulator.h — Software emulator for ZWO EAF focuser
// Implements the same public interface as EAFFocuser.
// Simulates motor movement at realistic step rate.
// Enabled when RADUK_EMULATOR is defined in CMakeLists.txt.
// ============================================================

#ifdef RADUK_EMULATOR
#  include "EAF_focuser_stub.h"
#else
#  include "EAF_focuser.h"
#endif
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include "IFocuser.h"
#include "FocusDirection.h"
#include "CoreLog.h"

namespace raduk::core {

class EAFFocuser : public IFocuser {
public:
    EAFFocuser()  = default;
    ~EAFFocuser() { Close(); }

    EAFFocuser(const EAFFocuser&) = delete;
    EAFFocuser& operator=(const EAFFocuser&) = delete;

    // ── Lifecycle ──────────────────────────────────────────────

    static int GetCount() {
        raduk::core::CoreLog("[EAFEmulator] GetCount: 1 emulated focuser\n");
        return 1;
    }

    bool Open(int /*index*/) override {
        if (m_isOpen) return false;
        // Start where this rig's focuser actually lives. The 8SE with the
        // EAF comes to focus around 150-175k - a measured run landed at
        // 168187 - so the old 50000 was nowhere near reality and made
        // every emulated focus figure meaningless to compare against a
        // real night's log.
        m_position = 166500;
        m_ledger.Good(166500);
        m_excursionFrom = 166500; m_excursion = 0;
        m_target   = 166500;
        m_optic    = 166500.0;
        m_isOpen   = true;
        m_moving   = false;
        // Start the motor simulation thread
        m_motorThread = std::thread(&EAFFocuser::MotorLoop, this);
        raduk::core::CoreLog("[EAFEmulator] Opened: id=0, pos=%d, maxStep=600000\n", m_position.load());
        return true;
    }

    void Close() override {
        if (!m_isOpen) return;
        m_isOpen = false;
        m_moving = false;
        if (m_motorThread.joinable()) m_motorThread.join();
        raduk::core::CoreLog("[EAFEmulator] Closed\n");
    }

    bool IsOpen() const override { return m_isOpen; }

    // ── Motion ─────────────────────────────────────────────────
    // Phase 161.0a: the same checked path as the EAF wrapper
    // (FocuserGuard.h), so a test of one is a test of the rule.

    bool MoveTo(int position) override       { return Command(position, true,  false); }
    bool MoveToFar(int position) override    { return Command(position, true,  true);  }
    bool MoveToDirect(int position) override { return Command(position, false, false); }
    bool MoveToDirectFar(int position) override { return Command(position, false, true); }

    void SetMaxTravel(int steps) override { m_maxTravel.store(steps < 0 ? 0 : steps); }
    int  MaxTravel() const override       { return m_maxTravel.load(); }
    MoveRefusal LastRefusalKind() const override { return m_ledger.LastRefusal(); }
    std::string LastRefusal() const override     { return m_ledger.LastRefusalText(); }

    void SetApproachBacklash(int steps) override { m_approach.SetBacklash(steps); }
    void SetApproachDirection(int dir) override  { m_approach.SetDirection(dir); }
    int  ApproachBacklash() const override       { return m_approach.Backlash(); }

    // Phase 139: the knob's slack, simulated. The mirror (what the
    // camera emulator focuses on) trails the step count by up to half of
    // it either way, so the same step number reached from opposite
    // directions is two focus positions, as the owner saw on the rig.
    // 0 (the default) is a perfect focuser.
    void SetSimulatedSlack(int steps) { m_slackHalf = (std::max)(0, steps) / 2; }
    int  GetOpticalPosition() const   { return (int)std::lround(m_optic.load()); }

    // ── Faults (phase 161.0a; phase 157's focuser injection) ───
    // The next n position reads fail, as a USB hiccup does.
    void FailReads(int n)        { m_failReads.store(n < 0 ? 0 : n); }
    // The next n move commands are refused by the "device".
    void RefuseMoves(int n)      { m_refuseMoves.store(n < 0 ? 0 : n); }
    // The motor does not turn while this is on, and says it is moving.
    void SetStalled(bool on)     { m_stalled.store(on); }
    // After a move command the motor waits this long before it starts,
    // and reports "not moving" meanwhile.
    void SetMovingLagMs(int ms)  { m_lagMs.store(ms < 0 ? 0 : ms); }
    // The motor stops this many steps short of every target (a slipping
    // coupling), and reports "not moving" there.
    void SetStopsShortBy(int steps) { m_shortBy.store(steps < 0 ? 0 : steps); }
    // The furthest the motor has been from where it was when this was
    // last reset: what a test asserts a fault could not exceed.
    void ResetExcursion()        { m_excursionFrom.store(m_position.load()); m_excursion.store(0); }
    int  MaxExcursion() const    { return m_excursion.load(); }

private:
    bool Command(int position, bool oneSided, bool pastGuard) {
        if (!m_isOpen) { m_ledger.Refuse(MoveRefusal::NotOpen, 0, position, 0); return false; }
        int from = 0;
        const bool known = TryGetPosition(from);
        const int  guard = m_maxTravel.load();
        const MoveRefusal r = CheckMove(known, from, position, GetMaxPosition(), guard, pastGuard);
        if (r != MoveRefusal::None) {
            m_ledger.Refuse(r, from, position, guard);
            raduk::core::CoreLog("[EAFEmulator] move to %d refused: %s\n", position,
                                 m_ledger.LastRefusalText().c_str());
            return false;
        }
        int leg = position;
        if (oneSided) leg = m_approach.Begin(from, position);
        else          m_approach.Direct(from, position);
        if (!Leg(leg)) {
            m_approach.Cancel();
            m_ledger.Idle();
            m_ledger.Refuse(MoveRefusal::Device, from, position, guard);
            return false;
        }
        m_ledger.Commanded(position, leg);
        return true;
    }

    // The "device": one motor command.
    bool Leg(int position) {
        int left = m_refuseMoves.load();
        while (left > 0 && !m_refuseMoves.compare_exchange_weak(left, left - 1)) {}
        if (left > 0) {
            raduk::core::CoreLog("[EAFEmulator] MoveTo %d REFUSED (injected)\n", position);
            return false;
        }
        std::lock_guard<std::mutex> lk(m_mutex);
        const int by = m_shortBy.load(), pos = m_position.load();
        m_target = position;
        if (by > 0 && std::abs(position - pos) > by) m_target = position + (position > pos ? -by : by);
        m_startAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_lagMs.load());
        m_moving = (m_lagMs.load() == 0) && (m_target != pos);
        raduk::core::CoreLog("[EAFEmulator] MoveTo %d (from %d)\n", position, pos);
        return true;
    }
public:

    bool MoveRelative(int steps) override {
        if (!m_isOpen) { m_ledger.Refuse(MoveRefusal::NotOpen, 0, 0, 0); return false; }
        int target = m_ledger.Target();
        if (target >= 0 && !IsMoving()) target = -1;   // it has finished
        int position = 0;
        if (target < 0 && !TryGetPosition(position)) {
            m_ledger.Refuse(MoveRefusal::PositionUnknown, 0, 0, 0);
            raduk::core::CoreLog("[EAFEmulator] relative move of %d refused: %s\n", steps,
                                 m_ledger.LastRefusalText().c_str());
            return false;
        }
        return MoveTo(RelativeBase(target, position) + steps);
    }

    bool Stop() override {
        if (!m_isOpen) return false;
        m_approach.Cancel();
        m_ledger.Idle();
        std::lock_guard<std::mutex> lk(m_mutex);
        m_target = m_position.load();  // Cancel move — target = current
        m_moving = false;
        raduk::core::CoreLog("[EAFEmulator] Stop at %d\n", m_position.load());
        return true;
    }

    bool IsMoving() const override {
        if (!m_isOpen) return false;
        if (m_stalled.load()) {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (m_target != m_position.load()) return true;
        }
        if (m_moving.load()) return true;
        int at = 0;
        if (!TryGetPosition(at)) return true;   // unknown counts as moving
        switch (m_ledger.StoppedAt(at, MoveLedger::Clock::now())) {
            case MoveLedger::LegState::Waiting: return true;
            case MoveLedger::LegState::Short: {
                const int target = m_ledger.Target();
                m_approach.Cancel();
                m_ledger.Idle();
                m_ledger.Refuse(MoveRefusal::StoppedShort, at, target, 0);
                raduk::core::CoreLog("[EAFEmulator] %s\n", m_ledger.LastRefusalText().c_str());
                return false;
            }
            case MoveLedger::LegState::Arrived: break;
        }
        // Phase 139.2: the first leg is done; command the second.
        const int next = m_approach.NextLeg(at);
        if (next >= 0) {
            if (!const_cast<EAFFocuser*>(this)->Leg(next)) {
                m_approach.Cancel();
                m_ledger.Idle();
                m_ledger.Refuse(MoveRefusal::Device, at, next, 0);
                return false;
            }
            m_ledger.Leg(next);
            return true;
        }
        m_ledger.Idle();
        return false;
    }

    bool TryGetPosition(int& position) const override {
        if (!m_isOpen) return false;
        int left = m_failReads.load();
        while (left > 0 && !m_failReads.compare_exchange_weak(left, left - 1)) {}
        if (left > 0) { m_ledger.ReadFailed(); return false; }
        position = m_position.load();
        m_ledger.Good(position);
        return true;
    }

    // Never an invented number: after a failed read, the last good one.
    int GetPosition() const override {
        int pos = 0;
        if (TryGetPosition(pos)) return pos;
        return m_ledger.Last();
    }

    int GetMaxPosition() const override { return 600000; }

    // ── Configuration ──────────────────────────────────────────

    void SetReverse(bool reversed) override {
        m_reversed = reversed;
        raduk::core::CoreLog("[EAFEmulator] Reverse: %s\n", reversed ? "on" : "off");
    }

    void SetBacklash(int steps) override {
        m_backlash = steps;
        raduk::core::CoreLog("[EAFEmulator] Backlash: %d steps\n", steps);
    }
    int GetBacklash() const override { return m_backlash; }

    // ── Temperature ────────────────────────────────────────────

    float GetTemperature() const override {
        // Stable temperature with minor noise — realistic for a focuser
        float noise = ((m_tempTick.load() % 10) - 5) * 0.02f;
        return 18.3f + noise;
    }

    // ── Info ───────────────────────────────────────────────────

    int FocuserId() const override { return 0; }

private:
    // Motor simulation: moves position toward target at ~1000 steps/sec
    void MotorLoop() {
        const int STEPS_PER_TICK = 10;   // steps moved per millisecond tick
        while (m_isOpen) {
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                int pos = m_position.load();
                if (pos != m_target && !m_stalled.load() &&
                    std::chrono::steady_clock::now() >= m_startAt) {
                    int delta = m_target - pos;
                    int step = std::min(std::abs(delta), STEPS_PER_TICK);
                    if (delta < 0) step = -step;
                    m_position.store(pos + step);
                    const double p = pos + step, o = m_optic.load();
                    if (p > o + m_slackHalf) m_optic.store(p - m_slackHalf);
                    if (p < o - m_slackHalf) m_optic.store(p + m_slackHalf);
                    m_moving = (m_position.load() != m_target);
                    const int ex = std::abs(m_position.load() - m_excursionFrom.load());
                    if (ex > m_excursion.load()) m_excursion.store(ex);
                } else if (pos == m_target) {
                    m_moving = false;
                }
            }
            m_tempTick++;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    bool                m_isOpen   = false;
    std::atomic<int>    m_position{166500};
    int                 m_target   = 166500;
    std::atomic<bool>   m_moving{false};
    bool                m_reversed = false;
    int                 m_backlash = 0;
    mutable ApproachSequencer m_approach;
    std::atomic<double> m_optic{166500.0};
    int                 m_slackHalf = 0;
    mutable std::mutex  m_mutex;
    // Phase 161.0a.
    mutable MoveLedger  m_ledger;
    std::atomic<int>    m_maxTravel{kDefaultMaxTravelSteps};
    mutable std::atomic<int> m_failReads{0};
    std::atomic<int>    m_refuseMoves{0};
    std::atomic<bool>   m_stalled{false};
    std::atomic<int>    m_lagMs{0};
    std::atomic<int>    m_shortBy{0};
    std::atomic<int>    m_excursionFrom{166500};
    std::atomic<int>    m_excursion{0};
    std::chrono::steady_clock::time_point m_startAt{};
    std::thread         m_motorThread;
    std::atomic<int>    m_tempTick{0};
};

} // namespace raduk::core
