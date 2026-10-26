// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once
// ============================================================
// NexStarMount.h — Celestron NexStar serial mount controller
// Implements IMount — direct serial fallback when CPWI is off.
//
// THREADING MODEL:
// A background poll thread reads position + tracking mode every
// 4 seconds. Main thread reads from atomic cache — never blocks.
// Slew/stop commands are fire-and-forget (no response wait); their
// deferred '#' acks are counted and drained before the next
// synchronous command so they can't corrupt reply parsing.
// This keeps the UI fully responsive even when the HC is slow.
//
// SAFETY — HEARTBEAT WATCHDOG:
// A second background thread monitors a heartbeat timestamp.
// The main loop must call Heartbeat() at least once per
// WATCHDOG_TIMEOUT_MS (8000ms). If the heartbeat stops
// (app freeze, crash, deadlock), the watchdog fires:
//   1. Sends StopSlew on both axes
//   2. Sends CancelGoTo
//   3. Logs the emergency stop
// The watchdog is the last line of defense against a runaway mount.
//
// CRASH HANDLER:
// An SEH/atexit handler registered at startup sends an emergency
// stop on process exit — catches crashes that bypass normal shutdown.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdint>
#include "IMount.h"

namespace raduk::core {

// ── The variable-rate command's wire encoding ─────────────────
//
// NexStar's P 3 <axis> <dir> <hi> <lo> carries the rate as
// arcseconds per second TIMES FOUR, split big-endian across two
// bytes. Out here as a free function so it can be checked against
// hand-worked values without a mount, a serial port or a sky --
// getting this wrong points the telescope somewhere at a speed
// nobody asked for, and it is exactly the kind of arithmetic that
// looks right and is not.
namespace nexstar {

struct VariableRate { unsigned char hi, lo; double actualArcsecPerSec; };

inline VariableRate EncodeVariableRate(double arcsecPerSec) {
    if (arcsecPerSec < 0.0) arcsecPerSec = -arcsecPerSec;

    // Quarter-arcseconds per second, which is the unit on the wire.
    //
    // CLAMPED AS A DOUBLE, BEFORE THE CONVERSION. Clamping afterwards
    // is what this did first, and `long` is 32 bits on this compiler:
    // a preposterous rate overflowed the cast, the result was
    // undefined, and in practice it came out ZERO -- so an absurd
    // request encoded as "stop" and the clamp never ran. A different
    // value could as easily have wrapped to something fast. Converting
    // a double that is already in range cannot do either.
    double quarters = arcsecPerSec * 4.0 + 0.5;
    if (!(quarters > 0.0))   quarters = 0.0;      // also catches NaN
    if (quarters > 65535.0)  quarters = 65535.0;  // two bytes is the limit
    long q = (long)quarters;
    VariableRate r;
    r.hi = (unsigned char)((q >> 8) & 0xFF);
    r.lo = (unsigned char)(q & 0xFF);
    // What the mount will ACTUALLY do, after the quantisation. The
    // caller may want to log or display this rather than what it
    // asked for.
    r.actualArcsecPerSec = q / 4.0;
    return r;
}

// One sidereal second of arc, for callers expressing rates the way
// astronomers do.
inline constexpr double SIDEREAL_ARCSEC_PER_SEC = 15.041067;

} // namespace nexstar

class NexStarMount : public IMount {
public:
    // Watchdog timeout — if Heartbeat() not called within this window,
    // mount is emergency-stopped. 8s allows two max guide pulses
    // (~550ms each) plus D2D render headroom while still catching
    // real freezes.
    static constexpr int WATCHDOG_TIMEOUT_MS = 8000;

    NexStarMount() = default;
    ~NexStarMount() override { Disconnect(); }

    NexStarMount(const NexStarMount&) = delete;
    NexStarMount& operator=(const NexStarMount&) = delete;

    // ── Connection ─────────────────────────────────────────────
    bool Connect(const std::string& comPort, int baudRate = 9600) override;
    void Disconnect() override;
    bool IsConnected() const override { return m_connected; }

    // ── Position readout (returns cached value — never blocks) ──
    bool GetRADec(double& raHours, double& decDegrees) override;
    bool GetAltAz(double& altDegrees, double& azDegrees) override;

    // ── GoTo ───────────────────────────────────────────────────
    bool GoToRADec(double raHours, double decDegrees) override;
    bool IsSlewing() override;
    void CancelGoTo() override;

    // ── Tracking ───────────────────────────────────────────────
    bool         SetTrackingMode(TrackingMode mode) override;
    TrackingMode GetTrackingMode() override;

    // ── Slewing (fire-and-forget — never blocks) ───────────────
    bool SlewFixed(int axis, int direction, int rate) override;
    bool SlewVariable(int axis, int direction, double arcsecPerSec) override;
    bool SupportsVariableRate() const override { return true; }
    void StopSlew(int axis) override;

    // ── Sync ───────────────────────────────────────────────────
    bool SyncRADec(double raHours, double decDegrees) override;

    // ── Alignment ──────────────────────────────────────────────
    // -- Phase 64: verified tracking --------------------------
    // Reads the mount's actual tracking mode ('t'), uncached.
    // Returns -1 if the mount did not answer, so a comms failure is
    // distinguishable from a genuine OFF -- the poll loop conflates
    // the two by defaulting its local to OFF.
    int ReadTrackingModeRaw();

    // Phase 69: true when the cached tracking mode came from a reply
    // the mount actually sent. False means unknown -- distinct from
    // OFF, which the poll loop used to report for both.
    bool TrackingModeKnown() const override {
        std::lock_guard<std::mutex> lk(m_cacheMutex);
        return m_trackingValid;
    }

    // -- Phase 62: site and clock -------------------------------
    // An alt-az mount cannot derive tracking rates without knowing
    // where and when it is: converting sidereal motion into altitude
    // and azimuth rates needs latitude and local time. The app knew
    // the site (RigConfig) but never told the mount, so every attempt
    // to enable tracking was asking it to solve an underdetermined
    // problem. Both are pushed automatically on Connect().
    //
    // Return the mount's acknowledgement so a refusal is visible
    // rather than assumed -- the failure mode this whole area suffers
    // from is commands whose results are discarded.
    bool SetSiteLocation(double latDeg, double lonDeg);   // +N, +E
    // Reads the site the mount already holds. An aligned hand control
    // has one, often entered carefully or supplied by a SkySync GPS,
    // and until now the app could only ever write. Returns false if
    // the mount does not answer or reports an implausible site.
    bool GetSiteLocation(double& latDeg, double& lonDeg);
    bool SetDateTime(int hour, int minute, int second,
                     int month, int day, int year,
                     int utcOffsetHours, bool dst);
    // Pushes RigConfig's site and the PC clock in one call.
    bool PushSiteAndTime();

    bool IsAligned() override;

    // ── Guide pulses (Phase 5) ─────────────────────────────────
    bool PulseGuide(GuideDirection dir, int durationMs) override;
    bool IsPulseGuiding() override { return false; }

    // ── Info ───────────────────────────────────────────────────
    bool GetVersion(int& major, int& minor) override;
    const std::string& ConnectionId() const override { return m_comPort; }

    // ── SAFETY: Heartbeat ──────────────────────────────────────
    // Call this from the main loop every frame (~30-100x/sec).
    // The watchdog thread checks this timestamp every 500ms.
    // If more than WATCHDOG_TIMEOUT_MS passes without a heartbeat,
    // the mount is emergency-stopped on both axes.
    void Heartbeat() override {
        m_lastHeartbeat.store(
            std::chrono::steady_clock::now().time_since_epoch().count());
    }

    // Pause/resume watchdog — call Pause before intentional long
    // operations (plate solving, file save). Always resume after.
    void WatchdogPause()  { m_watchdogPaused.store(true);  }
    void WatchdogResume() {
        Heartbeat();  // reset timer before resuming
        m_watchdogPaused.store(false);
    }

    // True if the watchdog has fired since last connect
    bool WatchdogFired() const { return m_watchdogFired.load(); }

    // Emergency stop — both axes, cancel GoTo. Callable from any thread.
    void EmergencyStop();

private:
    bool SendCommand(const char* cmd, int cmdLen,
                     char* response, int maxResponseLen, int& bytesRead);
    // Phase 66: fixed-length reply reader. SendCommand stops at the
    // first '#', which is right for ASCII replies but wrong for the
    // binary ones: 'w' (site) and 'h' (clock) return eight raw bytes
    // and any of them can legitimately equal 0x23 -- a latitude of
    // 33 deg 35' would truncate mid-reply and decode as nonsense.
    // This reads exactly expectBytes, then the terminator.
    bool SendCommandFixed(const char* cmd, int cmdLen,
                          unsigned char* response, int expectBytes);
    bool SendCommandNoWait(const char* cmd, int cmdLen);
    void DrainPendingAcks(); // call with m_serialMutex held

    static void   EncodePosition(double fraction, char* out);
    static double DecodePosition(const char* hex);

    void PollLoop();
    void WatchdogLoop();

    HANDLE       m_serial    = INVALID_HANDLE_VALUE;
    bool         m_connected = false;
    std::string  m_comPort;
    std::mutex   m_serialMutex;
    // '#' acks owed by fire-and-forget commands, drained before the next
    // synchronous command parses its reply
    std::atomic<int> m_pendingAcks{0};

    // Poll thread
    std::thread       m_pollThread;
    std::atomic<bool> m_pollRunning{false};

    // Watchdog thread
    std::thread       m_watchdogThread;
    std::atomic<bool> m_watchdogRunning{false};
    std::atomic<bool> m_watchdogPaused{false};
    std::atomic<bool> m_watchdogFired{false};
    std::atomic<long long> m_lastHeartbeat{0};

    // Cached position
    mutable std::mutex m_cacheMutex;
    double       m_cachedRA       = 0.0;
    double       m_cachedDec      = 0.0;
    TrackingMode m_cachedTracking = OFF;
    // Consecutive failed 't' reads tolerated before the mode is
    // declared unknown. A single dropped reply is noise; a run of them
    // is information.
    static constexpr int TRACKING_READ_FAIL_LIMIT = 3;
    bool         m_trackingValid     = false;
    int          m_trackingReadFails = 0;
    bool         m_cacheValid     = false;
};

} // namespace raduk::core
