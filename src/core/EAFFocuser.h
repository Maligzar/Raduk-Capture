#pragma once
// ============================================================
// EAFFocuser.h — ZWO EAF SDK Wrapper for RadukCapture
// Section 3.5 of Architecture Doc
// ============================================================

#include "EAF_focuser.h"
#include <vector>

namespace raduk::core {

class EAFFocuser {
public:
    EAFFocuser() = default;
    ~EAFFocuser() { Close(); }

    // Non-copyable
    EAFFocuser(const EAFFocuser&) = delete;
    EAFFocuser& operator=(const EAFFocuser&) = delete;

    // ── Lifecycle ──────────────────────────────────────────────

    static int GetCount() { return EAFGetNum(); }

    bool Open(int index);
    void Close();
    bool IsOpen() const { return m_isOpen; }

    // ── Motion ─────────────────────────────────────────────────

    bool MoveTo(int position);
    bool MoveRelative(int steps);
    bool Stop();
    bool IsMoving() const;
    int  GetPosition() const;
    int  GetMaxPosition() const { return m_info.MaxStep; }

    // ── Configuration ──────────────────────────────────────────

    void SetReverse(bool reversed);
    void SetBacklash(int steps);

    // ── Temperature ────────────────────────────────────────────

    float GetTemperature() const;

    // ── Info ───────────────────────────────────────────────────

    int FocuserId() const { return m_focuserId; }

private:
    int         m_focuserId = -1;
    EAF_INFO    m_info{};
    bool        m_isOpen = false;
};

} // namespace raduk::core
