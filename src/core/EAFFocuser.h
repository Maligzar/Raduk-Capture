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
#include <atomic>
#include <vector>
#include "IFocuser.h"
#include "FocusDirection.h"

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
    bool MoveToDirect(int position) override;
    bool MoveToFar(int position) override;
    bool MoveToDirectFar(int position) override;
    void SetMaxTravel(int steps) override { m_maxTravel.store(steps < 0 ? 0 : steps); }
    int  MaxTravel() const override       { return m_maxTravel.load(); }
    MoveRefusal LastRefusalKind() const override { return m_ledger.LastRefusal(); }
    std::string LastRefusal() const override     { return m_ledger.LastRefusalText(); }
    void SetApproachBacklash(int steps) override { m_approach.SetBacklash(steps); }
    void SetApproachDirection(int dir) override  { m_approach.SetDirection(dir); }
    int  ApproachBacklash() const override       { return m_approach.Backlash(); }
    bool MoveRelative(int steps) override;
    bool Stop() override;
    bool IsMoving() const override;
    bool TryGetPosition(int& position) const override;
    int  GetPosition() const override;
    int  GetMaxPosition() const override { return m_info.MaxStep; }

    void SetReverse(bool reversed) override;
    void SetBacklash(int steps) override;
    int  GetBacklash() const override;

    float GetTemperature() const override;
    int FocuserId() const override { return m_focuserId; }

private:
    int         m_focuserId = -1;
    EAF_INFO    m_info{};
    bool        m_isOpen = false;
    // Phase 139.2: the second leg of a one-sided move.
    mutable ApproachSequencer m_approach;
    // Phase 161.0a: one checked path for every move.
    bool Command(int position, bool oneSided, bool pastGuard);
    mutable MoveLedger  m_ledger;
    std::atomic<int>    m_maxTravel{kDefaultMaxTravelSteps};
    // The firmware's backlash when opened, put back on Close so other
    // software finds the EAF as it was.
    int         m_fwBacklashAtOpen = -1;
};

} // namespace raduk::core

#endif // !RADUK_EMULATOR
