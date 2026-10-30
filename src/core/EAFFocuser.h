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
#include "IFocuser.h"

namespace raduk::core {

class EAFFocuser : public IFocuser {
public:
    EAFFocuser() = default;
    ~EAFFocuser() { Close(); }

    EAFFocuser(const EAFFocuser&) = delete;
    EAFFocuser& operator=(const EAFFocuser&) = delete;

    static int GetCount() { return EAFGetNum(); }

    bool Open(int index) override;
    void Close() override;
    bool IsOpen() const override { return m_isOpen; }

    bool MoveTo(int position) override;
    bool MoveRelative(int steps) override;
    bool Stop() override;
    bool IsMoving() const override;
    int  GetPosition() const override;
    int  GetMaxPosition() const override { return m_info.MaxStep; }

    void SetReverse(bool reversed) override;
    void SetBacklash(int steps) override;

    float GetTemperature() const override;
    int FocuserId() const override { return m_focuserId; }

private:
    int         m_focuserId = -1;
    EAF_INFO    m_info{};
    bool        m_isOpen = false;
};

} // namespace raduk::core

#endif // !RADUK_EMULATOR
