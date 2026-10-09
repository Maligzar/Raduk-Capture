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
    bool         m_cacheValid     = false;
};

} // namespace raduk::core
