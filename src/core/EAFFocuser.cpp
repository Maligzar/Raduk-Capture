// ============================================================
// EAFFocuser.cpp — ZWO EAF SDK Wrapper for RadukCapture
// ============================================================

#include "EAFFocuser.h"
#include <cstdio> 

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
        printf("[EAFFocuser] Already open\n");
        return false;
    }

    int id = 0;
    EAF_ERROR_CODE err = EAFGetID(index, &id);
    if (err != EAF_SUCCESS) {
        printf("[EAFFocuser] GetID(%d) failed: %d\n", index, (int)err);
        return false;
    }

    err = EAFOpen(id);
    if (err != EAF_SUCCESS) {
        printf("[EAFFocuser] Open(%d) failed: %d\n", id, (int)err);
        return false;
    }

    m_focuserId = id;

    err = EAFGetProperty(id, &m_info);
    if (err != EAF_SUCCESS) {
        printf("[EAFFocuser] GetProperty failed: %d\n", (int)err);
        EAFClose(id);
        m_focuserId = -1;
        return false;
    }

    m_isOpen = true;
    m_fwBacklashAtOpen = GetBacklash();
    printf("[EAFFocuser] Opened: id=%d, maxStep=%d, firmware backlash=%d\n",
           id, m_info.MaxStep, m_fwBacklashAtOpen);
    return true;
}

void EAFFocuser::Close() {
    if (!m_isOpen) return;

    if (m_fwBacklashAtOpen >= 0 && GetBacklash() != m_fwBacklashAtOpen)
        EAFSetBacklash(m_focuserId, m_fwBacklashAtOpen);
    EAFClose(m_focuserId);
    printf("[EAFFocuser] Closed: id=%d\n", m_focuserId);
    
    m_focuserId = -1;
    m_isOpen = false;
}

// ── Motion ─────────────────────────────────────────────────────────────

bool EAFFocuser::MoveTo(int position) {
    if (!m_isOpen) return false;
    const int from = GetPosition();
    const int leg  = m_approach.Begin(from, position);
    EAF_ERROR_CODE err = EAFMove(m_focuserId, leg);
    if (err != EAF_SUCCESS) {
        m_approach.Cancel();
        printf("[EAFFocuser] MoveTo(%d) failed: %d\n", leg, (int)err);
        return false;
    }
    return true;
}

bool EAFFocuser::MoveToDirect(int position) {
    if (!m_isOpen) return false;
    m_approach.Direct(GetPosition(), position);
    EAF_ERROR_CODE err = EAFMove(m_focuserId, position);
    if (err != EAF_SUCCESS) {
        printf("[EAFFocuser] MoveTo(%d) failed: %d\n", position, (int)err);
        return false;
    }
    return true;
}

bool EAFFocuser::MoveRelative(int steps) {
    if (!m_isOpen) return false;
    int currentPos = GetPosition();
    return MoveTo(currentPos + steps);
}

bool EAFFocuser::Stop() {
    if (!m_isOpen) return false;
    m_approach.Cancel();
    return EAFStop(m_focuserId) == EAF_SUCCESS;
}

bool EAFFocuser::IsMoving() const {
    if (!m_isOpen) return false;
    
    // Fix for C2664: Using native 'bool' to match the SDK signature
    // signature: EAF_ERROR_CODE EAFIsMoving(int, bool*, bool*)
    bool bMoving = false;
    bool bHandControl = false;
    
    EAFIsMoving(m_focuserId, &bMoving, &bHandControl);
    if (bMoving) return true;
    // Phase 139.2: the first leg is done; command the second.
    const int next = m_approach.NextLeg(GetPosition());
    if (next >= 0) {
        if (EAFMove(m_focuserId, next) != EAF_SUCCESS) return false;
        return true;
    }
    return false;
}

int EAFFocuser::GetPosition() const {
    if (!m_isOpen) return 0;
    int pos = 0;
    EAFGetPosition(m_focuserId, &pos);
    return pos;
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