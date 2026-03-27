#pragma once
// ============================================================
// EAFFocuser.h — ZWO EAF SDK Wrapper for RadukCapture
// In RADUK_EMULATOR builds this file only provides the stub types.
// EAFEmulator.h provides the EAFFocuser class instead.
// ============================================================

#ifdef RADUK_EMULATOR
#  include "EAF_focuser_stub.h"
// EAFEmulator.h defines raduk::core::EAFFocuser — do not redefine here.
#else
#  include "EAF_focuser.h"
#include <vector>

namespace raduk::core {

class EAFFocuser {
public:
    EAFFocuser() = default;
    ~EAFFocuser() { Close(); }

    EAFFocuser(const EAFFocuser&) = delete;
    EAFFocuser& operator=(const EAFFocuser&) = delete;

    static int GetCount() { return EAFGetNum(); }

    bool Open(int index);
    void Close();
    bool IsOpen() const { return m_isOpen; }

    bool MoveTo(int position);
    bool MoveRelative(int steps);
    bool Stop();
    bool IsMoving() const;
    int  GetPosition() const;
    int  GetMaxPosition() const { return m_info.MaxStep; }

    void SetReverse(bool reversed);
    void SetBacklash(int steps);

    float GetTemperature() const;
    int FocuserId() const { return m_focuserId; }

private:
    int         m_focuserId = -1;
    EAF_INFO    m_info{};
    bool        m_isOpen = false;
};

} // namespace raduk::core

#endif // !RADUK_EMULATOR
