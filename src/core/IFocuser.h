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

    // ── Readout ────────────────────────────────────────────────
    virtual int   GetPosition() const = 0;
    virtual int   GetMaxPosition() const = 0;
    virtual float GetTemperature() const = 0;

    // ── Configuration ──────────────────────────────────────────
    virtual void SetReverse(bool reversed) = 0;
    virtual void SetBacklash(int steps) = 0;

    // ── Identity ───────────────────────────────────────────────
    virtual int FocuserId() const = 0;
};

} // namespace raduk::core
