#pragma once
// ============================================================
// IFocuser.h - Abstract focuser interface for RadukCapture
//
// Both the ZWO EAF wrapper and the emulator implement this.
//
// Until P5.2 there was no interface here and none was needed,
// because there was no choice to express: EAFEmulator.h defines a
// class literally named EAFFocuser, and DeviceFactory.h includes
// exactly one of the two headers. The emulator impersonates the
// hardware class by name collision, which works, costs nothing
// at runtime, and has two consequences worth being explicit
// about.
//
// The first is that a second focuser brand cannot be added at
// all. Not "is awkward to add": there is nowhere to put it.
//
// The second is subtler and is the reason this interface earns
// its place today, before any second brand exists. Nothing
// checked that the two classes agreed. They are compiled on
// different days by different build configurations and never in
// the same translation unit, so a signature could drift on one
// side and only the other configuration would notice, at the
// worst possible moment. Both now inherit this, so the compiler
// checks the agreement on every build of either.
//
// What is deliberately NOT here:
//
//   GetCount() stays static on both classes. It answers "how
//   many of these exist" before any instance does, which is not
//   a question an instance can be asked, and C++ has no virtual
//   statics. DeviceFactory still picks which GetCount() is
//   compiled.
//
//   The emulator's extras are not promoted. A caller that wants
//   them knows it has an emulator.
// ============================================================

#include <string>
#include "FocuserGuard.h"

namespace raduk::core {

class IFocuser {
public:
    virtual ~IFocuser() = default;

    // ── Connection ─────────────────────────────────────────────
    virtual bool Open(int index) = 0;
    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;

    // ── Motion ─────────────────────────────────────────────────
    // MoveTo is absolute, MoveRelative is signed steps. Both
    // return whether the command was accepted, not whether the
    // move finished: IsMoving() answers that.
    virtual bool MoveTo(int position) = 0;
    virtual bool MoveRelative(int steps) = 0;
    virtual bool Stop() = 0;
    virtual bool IsMoving() const = 0;
    // Phase 139.2: MoveTo and MoveRelative end every move travelling
    // outward (rising numbers) by at least the approach backlash, so a
    // step number is one focus position however it was reached; a move
    // that would end inward goes past and comes back, and IsMoving()
    // stays true until the second leg is done. MoveToDirect is one leg,
    // for autofocus's sweep, which measures both directions on purpose.
    virtual bool MoveToDirect(int position) = 0;
    // Phase 161.0a: no move starts from a position that could not be
    // read, goes outside the focuser's range, or travels further than
    // the guard (rig.ini FocuserMaxTravelSteps; 0: off) in one command.
    // A refused move returns false and LastRefusal() says why.
    // MoveToFar is MoveTo with the travel guard overridden, for a caller
    // that has asked the operator twice. MoveRelative adds to the
    // commanded target while a move is in flight.
    virtual bool MoveToFar(int position) = 0;
    // MoveToDirect with the travel guard overridden, for autofocus alone:
    // it holds every target within the guard of where the sweep STARTED,
    // which is the bound that matters, so a hop from one side of that
    // span to the other is not refused part way through.
    virtual bool MoveToDirectFar(int position) = 0;
    virtual void SetMaxTravel(int steps) = 0;
    virtual int  MaxTravel() const = 0;
    virtual MoveRefusal LastRefusalKind() const = 0;
    virtual std::string LastRefusal() const = 0;
    virtual void SetApproachBacklash(int steps) = 0;   // 0: off
    virtual int  ApproachBacklash() const = 0;
    // Phase 145.4: which way that final travel goes, +1 outward (rising
    // numbers, the default) or -1 inward (rig.ini FocuserFinalDirection).
    virtual void SetApproachDirection(int dir) = 0;

    // ── Readout ────────────────────────────────────────────────
    // Phase 161.0a: TryGetPosition is false when the read failed.
    // GetPosition never invents a number: after a failed read it returns
    // the last good one. Anything that MOVES from a position, or saves
    // one, uses TryGetPosition.
    virtual bool  TryGetPosition(int& position) const = 0;
    virtual int   GetPosition() const = 0;
    virtual int   GetMaxPosition() const = 0;
    virtual float GetTemperature() const = 0;

    // ── Configuration ──────────────────────────────────────────
    virtual void SetReverse(bool reversed) = 0;
    virtual void SetBacklash(int steps) = 0;   // the EAF firmware's own
    virtual int  GetBacklash() const = 0;       // compensation, 0-255

    // ── Identity ───────────────────────────────────────────────
    virtual int FocuserId() const = 0;
};

} // namespace raduk::core
