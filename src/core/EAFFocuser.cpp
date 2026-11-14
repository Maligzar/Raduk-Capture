// ============================================================
// EAFFocuser.cpp — ZWO EAF SDK Wrapper for RadukCapture
// ============================================================

#include "EAFFocuser.h"
#include <cstdio> 
#include "CoreLog.h"

// Manual definitions for ZWO EAF types
#ifndef EAF_BOOL
    typedef int EAF_BOOL;
#endif

#ifndef EAF_FALSE
    #define EAF_FALSE 0
#endif

#ifndef EAF_TRUE
    #define EAF_TRUE 1
#endif

namespace raduk::core {

// ── Lifecycle ──────────────────────────────────────────────────────────

bool EAFFocuser::Open(int index) {
    if (m_isOpen) {
        raduk::core::CoreLog("[EAFFocuser] Already open\n");
        return false;
    }

    int id = 0;
    EAF_ERROR_CODE err = EAFGetID(index, &id);
    if (err != EAF_SUCCESS) {
        raduk::core::CoreLog("[EAFFocuser] GetID(%d) failed: %d\n", index, (int)err);
        return false;
    }

    err = EAFOpen(id);
    if (err != EAF_SUCCESS) {
        raduk::core::CoreLog("[EAFFocuser] Open(%d) failed: %d\n", id, (int)err);
        return false;
    }

    m_focuserId = id;

    err = EAFGetProperty(id, &m_info);
    if (err != EAF_SUCCESS) {
        raduk::core::CoreLog("[EAFFocuser] GetProperty failed: %d\n", (int)err);
        EAFClose(id);
        m_focuserId = -1;
        return false;
    }

    m_isOpen = true;
    // Phase 161.0a: a focuser whose position cannot be read is not open.
    int at = 0;
    if (!TryGetPosition(at)) {
        raduk::core::CoreLog("[EAFFocuser] position could not be read on open\n");
        m_isOpen = false;
        EAFClose(id);
        m_focuserId = -1;
        return false;
    }
    m_fwBacklashAtOpen = GetBacklash();
    raduk::core::CoreLog("[EAFFocuser] Opened: id=%d, maxStep=%d, firmware backlash=%d\n",
           id, m_info.MaxStep, m_fwBacklashAtOpen);
    return true;
}

void EAFFocuser::Close() {
    if (!m_isOpen) return;

    if (m_fwBacklashAtOpen >= 0 && GetBacklash() != m_fwBacklashAtOpen)
        EAFSetBacklash(m_focuserId, m_fwBacklashAtOpen);
    EAFClose(m_focuserId);
    raduk::core::CoreLog("[EAFFocuser] Closed: id=%d\n", m_focuserId);
    
    m_focuserId = -1;
    m_isOpen = false;
}

// ── Motion ─────────────────────────────────────────────────────────────

// Phase 161.0a: every move comes through here. It starts only from a
// position that was read just now, inside the focuser's range, and no
// further than the travel guard unless `pastGuard` overrides it.
bool EAFFocuser::Command(int position, bool oneSided, bool pastGuard) {
    if (!m_isOpen) { m_ledger.Refuse(MoveRefusal::NotOpen, 0, position, 0); return false; }
    int from = 0;
    const bool known = TryGetPosition(from);
    const int  guard = m_maxTravel.load();
    const MoveRefusal r = CheckMove(known, from, position, m_info.MaxStep, guard, pastGuard);
    if (r != MoveRefusal::None) {
        m_ledger.Refuse(r, from, position, guard);
        raduk::core::CoreLog("[EAFFocuser] move to %d refused: %s\n", position,
                             m_ledger.LastRefusalText().c_str());
        return false;
    }
    int leg = position;
    if (oneSided) leg = m_approach.Begin(from, position);
    else          m_approach.Direct(from, position);
    EAF_ERROR_CODE err = EAFMove(m_focuserId, leg);
    if (err != EAF_SUCCESS) {
        m_approach.Cancel();
        m_ledger.Idle();
        m_ledger.Refuse(MoveRefusal::Device, from, position, guard);
        raduk::core::CoreLog("[EAFFocuser] EAFMove(%d) failed: %d\n", leg, (int)err);
        return false;
    }
    m_ledger.Commanded(position, leg);
    return true;
}

bool EAFFocuser::MoveTo(int position)       { return Command(position, true,  false); }
bool EAFFocuser::MoveToFar(int position)    { return Command(position, true,  true);  }
bool EAFFocuser::MoveToDirect(int position) { return Command(position, false, false); }
bool EAFFocuser::MoveToDirectFar(int position) { return Command(position, false, true); }

bool EAFFocuser::MoveRelative(int steps) {
    if (!m_isOpen) { m_ledger.Refuse(MoveRefusal::NotOpen, 0, 0, 0); return false; }
    // While a move is in flight a step adds to where that move ends, not
    // to wherever the motor is (which may be the overshoot leg).
    int target = m_ledger.Target();
    if (target >= 0 && !IsMoving()) target = -1;   // it has finished
    int position = 0;
    if (target < 0 && !TryGetPosition(position)) {
        m_ledger.Refuse(MoveRefusal::PositionUnknown, 0, 0, 0);
        raduk::core::CoreLog("[EAFFocuser] relative move of %d refused: %s\n", steps,
                             m_ledger.LastRefusalText().c_str());
        return false;
    }
    return MoveTo(RelativeBase(target, position) + steps);
}

bool EAFFocuser::Stop() {
    if (!m_isOpen) return false;
    m_approach.Cancel();
    m_ledger.Idle();
    return EAFStop(m_focuserId) == EAF_SUCCESS;
}

bool EAFFocuser::IsMoving() const {
    if (!m_isOpen) return false;

    // signature: EAF_ERROR_CODE EAFIsMoving(int, bool*, bool*)
    bool bMoving = false;
    bool bHandControl = false;

    // Phase 161.0a: a query that failed is not "stopped". Unknown counts
    // as moving; a caller waiting on it stalls out and stops the motor.
    if (EAFIsMoving(m_focuserId, &bMoving, &bHandControl) != EAF_SUCCESS) return true;
    if (bMoving) return true;
    int at = 0;
    if (!TryGetPosition(at)) return true;
    // "Not moving" short of the leg: the motor may not have started.
    switch (m_ledger.StoppedAt(at, MoveLedger::Clock::now())) {
        case MoveLedger::LegState::Waiting: return true;
        case MoveLedger::LegState::Short: {
            const int target = m_ledger.Target();
            m_approach.Cancel();
            m_ledger.Idle();
            m_ledger.Refuse(MoveRefusal::StoppedShort, at, target, 0);
            raduk::core::CoreLog("[EAFFocuser] %s\n", m_ledger.LastRefusalText().c_str());
            return false;
        }
        case MoveLedger::LegState::Arrived: break;
    }
    // Phase 139.2: the first leg is done; command the second.
    const int next = m_approach.NextLeg(at);
    if (next >= 0) {
        if (EAFMove(m_focuserId, next) != EAF_SUCCESS) {
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

bool EAFFocuser::TryGetPosition(int& position) const {
    if (!m_isOpen) return false;
    int pos = 0;
    if (EAFGetPosition(m_focuserId, &pos) != EAF_SUCCESS) {
        m_ledger.ReadFailed();
        return false;
    }
    m_ledger.Good(pos);
    position = pos;
    return true;
}

// Never an invented number: after a failed read, the last good one.
int EAFFocuser::GetPosition() const {
    int pos = 0;
    if (TryGetPosition(pos)) return pos;
    return m_ledger.Last();
}

// ── Configuration & Sensors ───────────────────────────────────────────

float EAFFocuser::GetTemperature() const {
    if (!m_isOpen) return 0.0f;
    float temp = 0.0f;
    EAFGetTemp(m_focuserId, &temp);
    return temp;
}

void EAFFocuser::SetReverse(bool reversed) {
    if (!m_isOpen) return;
    EAFSetReverse(m_focuserId, reversed ? EAF_TRUE : EAF_FALSE);
}

void EAFFocuser::SetBacklash(int steps) {
    if (!m_isOpen) return;
    EAFSetBacklash(m_focuserId, steps);
}

int EAFFocuser::GetBacklash() const {
    if (!m_isOpen) return -1;
    int v = -1;
    if (EAFGetBacklash(m_focuserId, &v) != EAF_SUCCESS) return -1;
    return v;
}

} // namespace raduk::core