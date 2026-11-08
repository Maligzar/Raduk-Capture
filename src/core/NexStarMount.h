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
#include <condition_variable>
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

    // 2026-11-07, the viewfinder froze at times while slewing. The UI
    // thread asked the mount these two over serial: IsAligned on every
    // GoTo-panel tick (~2 s), IsSlewing on every tick of a GoTo. Each
    // waits its turn on a 9600-baud link shared with the position poll
    // and the alt/az reader, and while the mount slews fast the hand
    // control answers slowly or not at all -- 500 ms per timed-out read.
    // The preview stopped drawing for as long as that took.
    //
    // Both now answer from a cache the poll thread keeps. IsAligned is
    // refreshed every poll cycle. IsSlewing is refreshed every 500 ms
    // for a few seconds after anyone asks, so a GoTo is followed at the
    // same cadence as before; a successful GoTo marks the cache slewing
    // at once, so a stale "not slewing" cannot announce it complete.
    bool IsAligned() override { return m_alignedCache.load(); }
    bool IsSlewing() override;

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

    // Serial round-trips behind the cached IsAligned / IsSlewing. Poll
    // thread (and Connect, before it starts) only.
    bool QueryAligned(bool* answered = nullptr);
    bool QuerySlewing(bool& slewing);
    std::atomic<bool>      m_alignedCache{false};
    std::atomic<bool>      m_slewingCache{false};
    std::atomic<long long> m_slewInterestUntilMs{0};  // steady ms
    std::atomic<long long> m_lastSlewQueryMs{0};
    static long long SteadyMs();

    // ── Motion commands, one at a time (2026-11-07) ────────────
    //
    // Every 'P' motion command (jog start, jog stop, guide pulse start
    // and stop) used to be written fire-and-forget, never waiting for
    // the hand control's '#'. The hand control takes one command at a
    // time; fast jogging fed it ~28 commands in 2 s and it stopped
    // responding, and a guide pulse's STOP that arrived while it was
    // still busy was lost -- the axis then ran on at guide rate
    // (~14"/s), which poisoned calibration and drove centring away.
    // That was the "sometimes it works and sometimes it does not".
    //
    // Now callers only record what each axis should be doing; one
    // sender thread passes it on, waiting for each '#' before the next.
    // A request superseded before it was sent is dropped (twenty taps
    // become a few commands, and the last -- usually a stop -- is the
    // one that goes). A stop that is not acknowledged is resent, and
    // after three failures the mount is emergency-stopped. The UI never
    // waits; PulseGuide waits for its own start and stop to be acked.
    struct AxisCmd {
        char     cmd[8]{};
        uint64_t want = 0;      // sequence of the latest request
        uint64_t sent = 0;      // sequence the mount has acknowledged
        bool     isStop = true;
        int      failures = 0;
        bool     moving = false;  // last ACKED command on this axis was a move
    };
    uint64_t QueueMotion(int axis, const char cmd[8], bool isStop);
    bool     WaitMotionSent(int axis, uint64_t seq, int timeoutMs);
    void     CommandLoop();
    std::mutex              m_cmdMutex;
    std::condition_variable m_cmdCv;     // work queued / stopping
    std::condition_variable m_sentCv;    // a command was acknowledged
    AxisCmd                 m_axisCmd[2];
    std::thread             m_cmdThread;
    std::atomic<bool>       m_cmdRunning{false};
};

} // namespace raduk::core
