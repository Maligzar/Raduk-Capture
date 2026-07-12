#pragma once
// ============================================================
// IMount.h — Abstract mount interface for RadukCapture
//
// Both ASCOMMount and NexStarMount implement this interface.
// The rest of the codebase programs against IMount* only —
// swapping the backend is a single-line change in DeviceFactory.
//
// PulseGuide is defined here (Phase 5) — implemented by
// ASCOMMount via ITelescope.PulseGuide(), stubbed by emulator.
// ============================================================

#include <string>

namespace raduk::core {

// Guide directions — match ASCOM ITelescopeV3 GuideDirections enum
enum class GuideDirection {
    North = 0,
    South = 1,
    East  = 2,
    West  = 3
};

class IMount {
public:
    virtual ~IMount() = default;

    // ── Connection ─────────────────────────────────────────────
    // For ASCOM: progID is the driver ProgID (e.g. "CPWI.Telescope")
    // For NexStar: progID is the COM port (e.g. "COM3")
    virtual bool Connect(const std::string& progID, int baudRate = 9600) = 0;
    virtual void Disconnect() = 0;
    virtual bool IsConnected() const = 0;

    // ── Position readout ───────────────────────────────────────
    virtual bool GetRADec(double& raHours, double& decDegrees) = 0;
    virtual bool GetAltAz(double& altDegrees, double& azDegrees) = 0;

    // ── GoTo ───────────────────────────────────────────────────
    virtual bool GoToRADec(double raHours, double decDegrees) = 0;
    virtual bool IsSlewing() = 0;
    virtual void CancelGoTo() = 0;

    // ── Tracking ───────────────────────────────────────────────
    enum TrackingMode { OFF = 0, ALT_AZ = 1, EQ_NORTH = 2, EQ_SOUTH = 3 };
    virtual bool         SetTrackingMode(TrackingMode mode) = 0;
    virtual TrackingMode GetTrackingMode() = 0;

    // ── Jog slewing ────────────────────────────────────────────
    virtual bool SlewFixed(int axis, int direction, int rate) = 0;
    virtual void StopSlew(int axis) = 0;

    // ── Sync ───────────────────────────────────────────────────
    virtual bool SyncRADec(double raHours, double decDegrees) = 0;

    // ── Alignment ──────────────────────────────────────────────
    virtual bool IsAligned() = 0;

    // ── Guide pulses (Phase 5 centroid tracking) ───────────────
    // Send a correction pulse in the given direction for durationMs milliseconds.
    // ASCOMMount: maps to ITelescope.PulseGuide() (returns immediately;
    //             check IsPulseGuiding() before sending the next pulse).
    // NexStarMount: software-timed slew — BLOCKS the calling thread for
    //               durationMs. Only call from the correction thread.
    // Emulator: instantaneous position nudge.
    virtual bool PulseGuide(GuideDirection dir, int durationMs) = 0;
    virtual bool IsPulseGuiding() = 0;

    // ── SAFETY: watchdog heartbeat ─────────────────────────────
    // Called by the main render loop every frame. NexStarMount uses it to
    // feed its runaway-mount watchdog; other backends ignore it. Virtual so
    // callers never need to downcast (a static_cast to NexStarMount here
    // previously corrupted ASCOMMount instances on the CPWI path).
    virtual void Heartbeat() {}

    // ── Info ───────────────────────────────────────────────────
    virtual bool GetVersion(int& major, int& minor) = 0;
    virtual const std::string& ConnectionId() const = 0;

    // ── Static helpers (same for all implementations) ──────────
    static std::string FormatRA(double raHours) {
        if (raHours < 0) raHours += 24.0;
        int h = (int)raHours;
        int m = (int)((raHours - h) * 60.0);
        int s = (int)(((raHours - h) * 60.0 - m) * 60.0);
        char buf[32];
        sprintf_s(buf, "%02dh %02dm %02ds", h, m, s);
        return std::string(buf);
    }

    static std::string FormatDec(double decDegrees) {
        char sign = (decDegrees >= 0) ? '+' : '-';
        double abs_dec = (decDegrees < 0) ? -decDegrees : decDegrees;
        int d = (int)abs_dec;
        int m = (int)((abs_dec - d) * 60.0);
        int s = (int)(((abs_dec - d) * 60.0 - m) * 60.0);
        char buf[32];
        sprintf_s(buf, "%c%02d\xB0 %02d' %02d\"", sign, d, m, s);
        return std::string(buf);
    }

    static const char* TrackingModeName(TrackingMode mode) {
        switch (mode) {
            case OFF:      return "OFF";
            case ALT_AZ:   return "ALT-AZ";
            case EQ_NORTH: return "EQ-NORTH";
            case EQ_SOUTH: return "EQ-SOUTH";
            default:       return "UNKNOWN";
        }
    }
};

} // namespace raduk::core
