// ============================================================
// NexStarMount.cpp — Celestron NexStar serial mount controller
// Section 3.6 of Architecture Doc
// ============================================================

#include "NexStarMount.h"
#include "CoreLog.h"
#include "RigConfig.h"
#include <ctime>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <thread>
#include <chrono>

namespace raduk::core {

// ── Connection ─────────────────────────────────────────────────

bool NexStarMount::Connect(const std::string& comPort, int baudRate) {
    if (m_connected) {
        CoreLog("[NexStarMount] Already connected\n");
        return false;
    }

    std::string portPath = "\\\\.\\" + comPort;
    m_serial = CreateFileA(
        portPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, 0, nullptr);

    if (m_serial == INVALID_HANDLE_VALUE) {
        CoreLog("[NexStarMount] Failed to open %s: %lu\n", comPort.c_str(), GetLastError());
        return false;
    }

    // Configure serial: 9600 8N1
    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    GetCommState(m_serial, &dcb);
    dcb.BaudRate = baudRate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    SetCommState(m_serial, &dcb);

    // Timeouts — tuned for NexStar HC at 9600 baud
    // At 9600 baud, one byte takes ~1ms. We allow 50ms per byte (generous)
    // and 500ms total per command. This means a dead/wrong port returns in
    // <500ms instead of blocking for ReadTotalTimeoutConstant × bytes.
    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout         = 50;   // max ms between consecutive bytes
    timeouts.ReadTotalTimeoutMultiplier  = 5;    // ms per byte
    timeouts.ReadTotalTimeoutConstant    = 500;  // ms base overhead per read call
    timeouts.WriteTotalTimeoutConstant   = 500;
    SetCommTimeouts(m_serial, &timeouts);

    m_comPort = comPort;
    m_connected = true;

    // Verify connection
    int major = 0, minor = 0;
    if (GetVersion(major, minor)) {
        CoreLog("[NexStarMount] Connected on %s — HC version %d.%d\n",
               comPort.c_str(), major, minor);
    } else {
        char resp[4] = {}; int len = 0;
        if (SendCommand("K", 1, resp, 2, len) && resp[0] == 'K') {
            CoreLog("[NexStarMount] Connected on %s — echo OK (no version)\n",
                   comPort.c_str());
        } else {
            CoreLog("[NexStarMount] Connected on %s — WARNING: no response to echo\n"
                   "  Mount may be off or HC not ready. Commands will be attempted.\n",
                   comPort.c_str());
        }
    }

    // Phase 62: push site and clock before anything asks the mount to
    // track. An alt-az mount derives its altitude/azimuth rates from
    // latitude and local time, so without these SetTrackingMode is
    // asking it to solve an underdetermined problem and it declines.
    // Phase 64: only when the mount has no alignment to lose. Setting
    // time or location on an aligned Celestron can invalidate its
    // model, and the app reconnects far more often than the mount is
    // re-aligned -- pushing unconditionally cost a hard-won alignment
    // across an app restart on 2026-10-08.
    m_alignedCache.store(QueryAligned());
    if (m_alignedCache.load()) {
        CoreLog("[NexStarMount] Mount reports ALIGNED - leaving its site and "
               "clock untouched\n");
    } else {
        CoreLog("[NexStarMount] Mount reports NOT aligned - pushing site and "
               "clock\n");
        PushSiteAndTime();
    }

    // Motion command sender (see the header): before anything can jog.
    for (auto& a : m_axisCmd) a = AxisCmd{};
    m_cmdRunning.store(true);
    m_cmdThread = std::thread(&NexStarMount::CommandLoop, this);

    // Start background poll thread — keeps position cache fresh
    // without ever blocking the main/UI thread on serial I/O
    m_pollRunning.store(true);
    m_pollThread = std::thread(&NexStarMount::PollLoop, this);

    // Start heartbeat watchdog thread
    // Seeds the heartbeat so it doesn't fire immediately on connect
    Heartbeat();
    m_watchdogFired.store(false);
    m_watchdogPaused.store(false);
    m_watchdogRunning.store(true);
    m_watchdogThread = std::thread(&NexStarMount::WatchdogLoop, this);

    return true;
}

void NexStarMount::Disconnect() {
    if (!m_connected) return;

    // Leave the mount stopped -- but STILL TRACKING. This used to be an
    // emergency stop, whose rate-0 stops switch tracking off on both axes:
    // every close of the app (and there were many on 2026-11-07) left the
    // scope standing still while the sky moved, and the planet was gone
    // when it reopened. An axis this app is moving is stopped through the
    // sender, which turns tracking back on; the emergency stop is only for
    // a stop that cannot be confirmed.
    {
        bool anyMoving = false;
        {
            std::lock_guard<std::mutex> lk(m_cmdMutex);
            for (const auto& a : m_axisCmd)
                if (a.moving || a.want != a.sent) anyMoving = true;
        }
        if (anyMoving && m_cmdRunning.load()) {
            const char s0[8] = { 'P', 2, 16, 36, 0, 0, 0, 0 };
            const char s1[8] = { 'P', 2, 17, 36, 0, 0, 0, 0 };
            const uint64_t q0 = QueueMotion(0, s0, true);
            const uint64_t q1 = QueueMotion(1, s1, true);
            const bool ok = WaitMotionSent(0, q0, 1500) && WaitMotionSent(1, q1, 1500);
            CoreLog("[NexStarMount] Disconnect: motion stopped %s\n",
                    ok ? "(tracking kept)" : "NOT confirmed - emergency stop");
            if (!ok) EmergencyStop();
        } else {
            CoreLog("[NexStarMount] Disconnect: nothing moving, tracking left as it is\n");
        }
    }

    // Stop watchdog first (it uses serial via EmergencyStop)
    m_watchdogRunning.store(false);
    if (m_watchdogThread.joinable()) m_watchdogThread.join();

    // Stop poll thread
    m_pollRunning.store(false);
    if (m_pollThread.joinable()) m_pollThread.join();

    // Stop the motion sender. EmergencyStop above dropped anything
    // still queued, so nothing can start a motor after it.
    m_cmdRunning.store(false);
    m_cmdCv.notify_all();
    m_sentCv.notify_all();
    if (m_cmdThread.joinable()) m_cmdThread.join();

    // Close the handle under the serial mutex — a UI/correction thread
    // already inside SendCommand could otherwise race ReadFile against
    // CloseHandle (review T18).
    m_connected = false;
    {
        std::lock_guard<std::mutex> lk(m_serialMutex);
        CloseHandle(m_serial);
        m_serial = INVALID_HANDLE_VALUE;
    }
    CoreLog("[NexStarMount] Disconnected from %s\n", m_comPort.c_str());
}

// ── Low-level serial I/O ───────────────────────────────────────

bool NexStarMount::SendCommand(const char* cmd, int cmdLen,
                                char* response, int maxResponseLen, int& bytesRead) {
    std::lock_guard<std::mutex> lock(m_serialMutex);
    if (!m_connected) return false;

    // Consume acks from earlier fire-and-forget commands (waits for any
    // still in flight), THEN flush — a stray '#' arriving after the purge
    // would otherwise be parsed as this command's reply.
    DrainPendingAcks();
    PurgeComm(m_serial, PURGE_RXCLEAR);

    DWORD written = 0;
    if (!WriteFile(m_serial, cmd, cmdLen, &written, nullptr) || (int)written != cmdLen) {
        return false;
    }

    // Read until '#' terminator
    bytesRead = 0;
    while (bytesRead < maxResponseLen) {
        DWORD got = 0;
        char c;
        if (!ReadFile(m_serial, &c, 1, &got, nullptr) || got == 0) {
            return false;  // Timeout
        }
        response[bytesRead++] = c;
        if (c == '#') break;
    }

    return (bytesRead > 0 && response[bytesRead - 1] == '#');
}

// Fire-and-forget: write command, don't wait for '#' response.
// Used for slew commands where we don't need the acknowledgement
// and blocking the main thread for 500ms is unacceptable.
// The HC still sends a '#' ack later; m_pendingAcks counts them so the
// next synchronous command can drain them BEFORE parsing its own reply —
// an ack arriving after a PurgeComm otherwise corrupts that parse
// (intermittent poll failures / wrong IsSlewing, review T18).
bool NexStarMount::SendCommandNoWait(const char* cmd, int cmdLen) {
    std::lock_guard<std::mutex> lock(m_serialMutex);
    if (!m_connected) {
        CoreLog("[NexStarMount] SendCommandNoWait: not connected\n");
        return false;
    }
    // No PurgeComm here. It used to clear the receive buffer before
    // every jog, which threw away the '#' acks of earlier fire-and-forget
    // commands while m_pendingAcks went on counting them, so the next
    // synchronous command sat in DrainPendingAcks for a full 500 ms read
    // timeout waiting for an ack that had been deleted -- holding the
    // serial lock, and with it the UI thread's next jog, the whole time.
    // Junk without a '#' is bounded by the drain and purged by
    // SendCommand after it.
    DWORD written = 0;
    BOOL ok = WriteFile(m_serial, cmd, cmdLen, &written, nullptr);
    if (!ok || (int)written != cmdLen) {
        CoreLog("[NexStarMount] WriteFile failed: ok=%d written=%lu expected=%d err=%lu\n",
               (int)ok, written, cmdLen, GetLastError());
        return false;
    }
    m_pendingAcks.fetch_add(1);
    return true;
}

// Drain outstanding '#' acks from fire-and-forget commands.
// Called with m_serialMutex HELD, before writing a synchronous command.
// Buffered acks read back instantly; an ack still in flight is waited for
// (bounded by the port's read timeout), so it can't land mid-reply and
// corrupt the next parse.
void NexStarMount::DrainPendingAcks() {
    int pending = m_pendingAcks.exchange(0);
    if (pending <= 0) return;
    int acksSeen = 0;
    int maxBytes = pending * 8 + 8; // hard bound against junk streams
    while (acksSeen < pending && maxBytes-- > 0) {
        char c = 0;
        DWORD got = 0;
        if (!ReadFile(m_serial, &c, 1, &got, nullptr) || got == 0) break;
        if (c == '#') acksSeen++;
    }
}

// ── Position readout ───────────────────────────────────────────

bool NexStarMount::GetRADec(double& raHours, double& decDegrees) {
    // Returns cached value from poll thread — instant, never blocks UI
    std::lock_guard<std::mutex> lk(m_cacheMutex);
    if (!m_cacheValid) return false;
    raHours    = m_cachedRA;
    decDegrees = m_cachedDec;
    return true;
}
bool NexStarMount::GetAltAz(double& altDegrees, double& azDegrees) {
    char resp[20] = {};
    int len = 0;
    if (!SendCommand("z", 1, resp, 18, len)) return false;
    if (len < 17) return false;
    char azHex[9] = {}, altHex[9] = {};
    memcpy(azHex,  resp,     8);
    memcpy(altHex, resp + 9, 8);
    azDegrees  = DecodePosition(azHex)  * 360.0;
    altDegrees = DecodePosition(altHex) * 360.0;
    if (altDegrees > 180.0) altDegrees -= 360.0;
    return true;
}

// ── GoTo ───────────────────────────────────────────────────────

bool NexStarMount::GoToRADec(double raHours, double decDegrees) {
    // 'r' + RA(8hex) + ',' + DEC(8hex)
    char cmd[19];
    cmd[0] = 'r';

    double raFrac = raHours / 24.0;
    double decFrac = decDegrees / 360.0;
    if (decFrac < 0) decFrac += 1.0;

    EncodePosition(raFrac, cmd + 1);
    cmd[9] = ',';
    EncodePosition(decFrac, cmd + 10);

    char resp[4] = {};
    int len = 0;
    const bool ok = SendCommand(cmd, 18, resp, 2, len);
    if (ok) {
        // Slewing from now on, until a query made AFTER this says not:
        // the next one is held off 500 ms so the mount has begun.
        const long long now = SteadyMs();
        m_slewingCache.store(true);
        m_lastSlewQueryMs.store(now);
        m_slewInterestUntilMs.store(now + 5000);
    }
    return ok;
}

long long NexStarMount::SteadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Cached; see the header. Asking keeps the 500 ms refresh going.
bool NexStarMount::IsSlewing() {
    m_slewInterestUntilMs.store(SteadyMs() + 5000);
    return m_slewingCache.load();
}

bool NexStarMount::QuerySlewing(bool& slewing) {
    char resp[4] = {};
    int len = 0;
    if (!SendCommand("L", 1, resp, 2, len)) return false;
    slewing = (resp[0] == '1');
    return true;
}

void NexStarMount::CancelGoTo() {
    char resp[4] = {};
    int len = 0;
    SendCommand("M", 1, resp, 2, len);
}

// ── Tracking ───────────────────────────────────────────────────

int NexStarMount::ReadTrackingModeRaw() {
    char resp[4] = {};
    int  len = 0;
    if (!SendCommand("t", 1, resp, 2, len) || len < 1) return -1;
    return (int)(unsigned char)resp[0];
}

bool NexStarMount::SetTrackingMode(TrackingMode mode) {
    char cmd[2] = {'T', static_cast<char>(mode)};
    char resp[4] = {};
    int len = 0;
    if (!SendCommand(cmd, 2, resp, 2, len)) {
        CoreLog("[NexStarMount] Tracking %s: no response to command\n",
               TrackingModeName(mode));
        return false;
    }

    // Phase 64: the '#' acknowledgement means the command was RECEIVED,
    // not that the mount is driving. An unaligned mount ACKs a 'T'
    // happily and then sits still -- which is exactly how the UI came
    // to report "tracking ALT-AZ" on a mount that never moved. Read the
    // mode back and report what the mount is actually doing.
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    int actual = ReadTrackingModeRaw();
    if (actual < 0) {
        CoreLog("[NexStarMount] Tracking %s: ACKed but readback failed\n",
               TrackingModeName(mode));
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(m_cacheMutex);
        m_cachedTracking = static_cast<TrackingMode>(actual);
    }

    bool ok = (actual == (int)mode);
    CoreLog("[NexStarMount] Tracking %s requested, mount reports %s : %s\n",
           TrackingModeName(mode),
           TrackingModeName(static_cast<TrackingMode>(actual)),
           ok ? "confirmed" : "NOT RUNNING (mount likely unaligned)");
    return ok;
}

NexStarMount::TrackingMode NexStarMount::GetTrackingMode() {
    std::lock_guard<std::mutex> lk(m_cacheMutex);
    return m_cachedTracking;
}

// ── Slewing ────────────────────────────────────────────────────

bool NexStarMount::SlewFixed(int axis, int direction, int rate) {
    char cmd[8];
    cmd[0] = 'P';
    cmd[1] = 2;
    cmd[2] = (axis == 0) ? 16 : 17;
    cmd[3] = (direction == 0) ? 36 : 37;
    cmd[4] = static_cast<char>(rate);
    cmd[5] = 0; cmd[6] = 0; cmd[7] = 0;

    CoreLog("[NexStarMount] SlewFixed axis=%d dir=%d rate=%d\n", axis, direction, rate);
    // Queued for the sender, which waits for the '#'. Never blocks here.
    if (!m_connected || !m_cmdRunning.load()) return false;
    QueueMotion(axis == 0 ? 0 : 1, cmd, rate == 0);
    return true;
}

// ── The motion sender ──────────────────────────────────────────

uint64_t NexStarMount::QueueMotion(int axis, const char cmd[8], bool isStop) {
    std::lock_guard<std::mutex> lk(m_cmdMutex);
    AxisCmd& a = m_axisCmd[axis];
    // A stop for an axis this app is not moving, with nothing queued, is
    // not sent at all: on this mount ANY rate-0 stop switches that axis's
    // tracking off (2026-11-07), so a "just in case" stop is not harmless.
    if (isStop && !a.moving && a.want == a.sent) return a.sent;
    memcpy(a.cmd, cmd, 8);
    a.isStop = isStop;
    a.failures = 0;
    const uint64_t seq = ++a.want;
    m_cmdCv.notify_all();
    return seq;
}

bool NexStarMount::WaitMotionSent(int axis, uint64_t seq, int timeoutMs) {
    std::unique_lock<std::mutex> lk(m_cmdMutex);
    return m_sentCv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] {
        // Superseded counts as done: a later command for this axis has
        // been acknowledged, so this one no longer describes the mount.
        return m_axisCmd[axis].sent >= seq || !m_cmdRunning.load();
    }) && m_axisCmd[axis].sent >= seq;
}

void NexStarMount::CommandLoop() {
    CoreLog("[NexStarMount] Motion sender started\n");
    while (m_cmdRunning.load()) {
        int axis = -1; char cmd[8]; uint64_t seq = 0; bool isStop = false;
        {
            std::unique_lock<std::mutex> lk(m_cmdMutex);
            m_cmdCv.wait_for(lk, std::chrono::milliseconds(100), [&] {
                return !m_cmdRunning.load() ||
                       m_axisCmd[0].want != m_axisCmd[0].sent ||
                       m_axisCmd[1].want != m_axisCmd[1].sent;
            });
            if (!m_cmdRunning.load()) break;
            // Stops first: if both axes have work, the one stopping goes
            // ahead of the one starting.
            for (int pass = 0; pass < 2 && axis < 0; ++pass)
                for (int i = 0; i < 2; ++i) {
                    const AxisCmd& a = m_axisCmd[i];
                    if (a.want != a.sent && (pass == 1 || a.isStop)) { axis = i; break; }
                }
            if (axis < 0) continue;
            memcpy(cmd, m_axisCmd[axis].cmd, 8);
            seq = m_axisCmd[axis].want;
            isStop = m_axisCmd[axis].isStop;
        }

        char resp[4] = {}; int len = 0;
        const long long t0 = SteadyMs();
        const bool ok = SendCommand(cmd, 8, resp, 2, len);   // waits for '#'
        // Every motion command and how long the hand control took to
        // acknowledge it: the record that was missing on 2026-11-07.
        CoreLog("[MOUNT-IO] axis %d %s (P %d %d %d %d) %s in %lld ms\n",
                axis, isStop ? "stop" : "move",
                (int)(unsigned char)cmd[1], (int)(unsigned char)cmd[3],
                (int)(unsigned char)cmd[4], (int)(unsigned char)cmd[5],
                ok ? "acked" : "NO ACK", SteadyMs() - t0);

        bool emergency = false;
        bool reassertTracking = false;
        {
            std::lock_guard<std::mutex> lk(m_cmdMutex);
            AxisCmd& a = m_axisCmd[axis];
            if (ok) {
                if (isStop) reassertTracking = true;   // every stop, see below
                a.moving = !isStop;
                if (a.sent < seq) a.sent = seq;
                a.failures = 0;
                m_sentCv.notify_all();
            } else if (a.want == seq) {
                // A move that may have reached the motor although its ack
                // did not come back: assume it did, so a later stop is sent.
                if (!isStop) a.moving = true;
                // Left queued, so it is retried. A start that will not go
                // is given up after three tries; a stop never is, and three
                // failures escalate.
                if (++a.failures >= 3) {
                    if (a.isStop) emergency = true;
                    CoreLog("[NexStarMount] Axis %d %s not acknowledged after 3 tries%s\n",
                           axis, a.isStop ? "STOP" : "move",
                           a.isStop ? " - EMERGENCY STOP" : " - dropped");
                    a.sent = a.want;
                    m_sentCv.notify_all();
                }
            }
        }
        // 2026-11-07, measured at the rig: a 'P' rate-0 stop goes to the
        // motor board and zeroes the axis, tracking included, and the hand
        // control took 10 s or more to resume it (camera drift went from
        // 0.9"/s to 13.5"/s after one 250 ms jog). So calibration measured
        // with tracking off, and centring -- a pulse every half second --
        // never let it resume, which is how the planet ran away. After a
        // stop that ends a move, tracking is switched straight back on in
        // the mode the mount was in.
        if (reassertTracking && !emergency) {
            TrackingMode mode = OFF; bool known = false;
            {
                std::lock_guard<std::mutex> lk(m_cacheMutex);
                mode = m_cachedTracking; known = m_trackingValid;
            }
            if (known && mode != OFF) {
                const char t[2] = { 'T', (char)mode };
                char r[4] = {}; int rl = 0;
                const long long t1 = SteadyMs();
                const bool tok = SendCommand(t, 2, r, 2, rl);
                CoreLog("[MOUNT-IO] tracking %s re-asserted after axis %d stop: %s in %lld ms\n",
                        TrackingModeName(mode), axis, tok ? "acked" : "NO ACK",
                        SteadyMs() - t1);
            } else {
                CoreLog("[MOUNT-IO] axis %d stopped; tracking mode %s, not re-asserted\n",
                        axis, known ? "OFF" : "unknown");
            }
        }
        if (emergency) EmergencyStop();
        else if (!ok) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    CoreLog("[NexStarMount] Motion sender stopped\n");
}

// ── Variable rate ──────────────────────────────────────────────
//
// P 3 <16|17> <6|7> <hi> <lo> 0 0, against SlewFixed's
// P 2 <16|17> <36|37> <rate> 0 0 0. Same shape, different command
// byte, and the rate becomes two bytes of quarter-arcseconds per
// second instead of one preset index.
//
// Direction uses 6 and 7 where the fixed form uses 36 and 37, which
// is the protocol's own inconsistency rather than a typo here.
bool NexStarMount::SlewVariable(int axis, int direction, double arcsecPerSec) {
    const nexstar::VariableRate r = nexstar::EncodeVariableRate(arcsecPerSec);

    char cmd[8];
    cmd[0] = 'P';
    cmd[1] = 3;
    cmd[2] = (axis == 0) ? 16 : 17;
    cmd[3] = (direction == 0) ? 6 : 7;
    cmd[4] = static_cast<char>(r.hi);
    cmd[5] = static_cast<char>(r.lo);
    cmd[6] = 0; cmd[7] = 0;

    CoreLog("[NexStarMount] SlewVariable axis=%d dir=%d %.2f\"/s "
           "(sent %.2f, bytes %u/%u)\n",
           axis, direction, arcsecPerSec, r.actualArcsecPerSec,
           (unsigned)r.hi, (unsigned)r.lo);
    // Queued for the sender, like SlewFixed. A zero rate is a stop.
    if (!m_connected || !m_cmdRunning.load()) return false;
    QueueMotion(axis == 0 ? 0 : 1, cmd, r.hi == 0 && r.lo == 0);
    return true;
}

void NexStarMount::StopSlew(int axis) {
    CoreLog("[NexStarMount] StopSlew axis=%d\n", axis);
    SlewFixed(axis, 0, 0);
}

// ── Sync ───────────────────────────────────────────────────────

bool NexStarMount::SyncRADec(double raHours, double decDegrees) {
    char cmd[19];
    cmd[0] = 's';

    double raFrac = raHours / 24.0;
    double decFrac = decDegrees / 360.0;
    if (decFrac < 0) decFrac += 1.0;

    EncodePosition(raFrac, cmd + 1);
    cmd[9] = ',';
    EncodePosition(decFrac, cmd + 10);

    char resp[4] = {};
    int len = 0;
    return SendCommand(cmd, 18, resp, 2, len);
}

// ── Info ───────────────────────────────────────────────────────

bool NexStarMount::GetVersion(int& major, int& minor) {
    char resp[4] = {};
    int len = 0;
    if (!SendCommand("V", 1, resp, 3, len)) return false;
    if (len < 3) return false;
    major = (unsigned char)resp[0];
    minor = (unsigned char)resp[1];
    return true;
}

// -- Phase 62: tell the mount where and when it is --------------------
// Without these an alt-az mount has no way to turn sidereal motion into
// altitude/azimuth rates, so it declines to track no matter how often
// SetTrackingMode is sent. The site lived in RigConfig and was never
// pushed; the clock was never pushed at all.

bool NexStarMount::SendCommandFixed(const char* cmd, int cmdLen,
                                    unsigned char* response, int expectBytes) {
    std::lock_guard<std::mutex> lock(m_serialMutex);
    if (!m_connected) return false;

    DrainPendingAcks();
    PurgeComm(m_serial, PURGE_RXCLEAR);

    DWORD written = 0;
    if (!WriteFile(m_serial, cmd, cmdLen, &written, nullptr) ||
        (int)written != cmdLen)
        return false;

    // Exactly expectBytes of payload, then the '#'. Reading "until #"
    // would stop early whenever a payload byte happens to be 0x23.
    for (int i = 0; i < expectBytes; i++) {
        DWORD got = 0;
        char  c   = 0;
        if (!ReadFile(m_serial, &c, 1, &got, nullptr) || got == 0) return false;
        response[i] = (unsigned char)c;
    }
    DWORD got = 0;
    char  term = 0;
    if (!ReadFile(m_serial, &term, 1, &got, nullptr) || got == 0) return false;
    return term == '#';
}

bool NexStarMount::GetSiteLocation(double& latDeg, double& lonDeg) {
    unsigned char r[8] = {};
    if (!SendCommandFixed("w", 1, r, 8)) {
        CoreLog("[NexStarMount] Site read: no reply\n");
        return false;
    }

    auto dms = [](unsigned char d, unsigned char m, unsigned char sec, bool neg) {
        double v = (double)d + (double)m / 60.0 + (double)sec / 3600.0;
        return neg ? -v : v;
    };
    double la = dms(r[0], r[1], r[2], r[3] != 0);   // r[3]: 0 = north
    double lo = dms(r[4], r[5], r[6], r[7] != 0);   // r[7]: 0 = east

    // Reject nonsense rather than adopting it. Minutes and seconds above
    // 59 mean the reply was misframed, not that the mount is somewhere
    // unusual.
    if (r[1] > 59 || r[2] > 59 || r[5] > 59 || r[6] > 59 ||
        la < -90.0 || la > 90.0 || lo < -180.0 || lo > 180.0) {
        CoreLog("[NexStarMount] Site read: implausible reply, ignoring\n");
        return false;
    }
    // A mount that has never been given a site reports 0,0. That is a
    // real place, but as a mount default it means "unset" -- adopting it
    // is the very mistake this work exists to prevent.
    if (la == 0.0 && lo == 0.0) {
        CoreLog("[NexStarMount] Site read: mount reports 0,0 (never set)\n");
        return false;
    }

    latDeg = la;
    lonDeg = lo;
    CoreLog("[NexStarMount] Site read from mount: %.1f%c %.1f%c\n",
           la < 0 ? -la : la, la < 0 ? 'S' : 'N',
           lo < 0 ? -lo : lo, lo < 0 ? 'W' : 'E');
    return true;
}

bool NexStarMount::SetSiteLocation(double latDeg, double lonDeg) {
    // NexStar 'W': 8 bytes, deg/min/sec per axis plus a hemisphere flag.
    // Sign is carried by the flag, not the magnitude, so send absolutes.
    auto split = [](double deg, int& d, int& m, int& s) {
        double a = (deg < 0.0) ? -deg : deg;
        d = (int)a;
        double remMin = (a - d) * 60.0;
        m = (int)remMin;
        s = (int)((remMin - m) * 60.0 + 0.5);
        if (s >= 60) { s -= 60; m++; }      // carry from rounding
        if (m >= 60) { m -= 60; d++; }
    };

    int latD, latM, latS, lonD, lonM, lonS;
    split(latDeg, latD, latM, latS);
    split(lonDeg, lonD, lonM, lonS);

    char cmd[9];
    cmd[0] = 'W';
    cmd[1] = (char)latD;
    cmd[2] = (char)latM;
    cmd[3] = (char)latS;
    cmd[4] = (char)(latDeg < 0.0 ? 1 : 0);   // 0 = north, 1 = south
    cmd[5] = (char)lonD;
    cmd[6] = (char)lonM;
    cmd[7] = (char)lonS;
    cmd[8] = (char)(lonDeg < 0.0 ? 1 : 0);   // 0 = east, 1 = west

    char resp[4] = {}; int len = 0;
    bool ok = SendCommand(cmd, 9, resp, 2, len);
    CoreLog("[NexStarMount] Site -> %d%c%02d'%02d\" %s, %d%c%02d'%02d\" %s : %s\n",
           latD, 0xB0, latM, latS, (latDeg < 0.0 ? "S" : "N"),
           lonD, 0xB0, lonM, lonS, (lonDeg < 0.0 ? "W" : "E"),
           ok ? "accepted" : "REFUSED");
    return ok;
}

bool NexStarMount::SetDateTime(int hour, int minute, int second,
                               int month, int day, int year,
                               int utcOffsetHours, bool dst) {
    // NexStar 'H': 8 bytes. The UTC offset is a signed byte sent as
    // two's complement, and the year is the last two digits.
    char cmd[9];
    cmd[0] = 'H';
    cmd[1] = (char)hour;
    cmd[2] = (char)minute;
    cmd[3] = (char)second;
    cmd[4] = (char)month;
    cmd[5] = (char)day;
    cmd[6] = (char)(year % 100);
    cmd[7] = (char)(signed char)utcOffsetHours;
    cmd[8] = (char)(dst ? 1 : 0);

    char resp[4] = {}; int len = 0;
    bool ok = SendCommand(cmd, 9, resp, 2, len);
    CoreLog("[NexStarMount] Clock -> %04d-%02d-%02d %02d:%02d:%02d UTC%+d dst=%d : %s\n",
           year, month, day, hour, minute, second,
           utcOffsetHours, dst ? 1 : 0, ok ? "accepted" : "REFUSED");
    return ok;
}

bool NexStarMount::PushSiteAndTime() {
    const auto& cfg = RigConfig::Get();

    // Local wall clock plus the offset the OS reports, so a DST change
    // or a trip to another timezone needs no edit. _get_timezone gives
    // seconds WEST of UTC, hence the negation.
    time_t now = time(nullptr);
    struct tm lt {};
    localtime_s(&lt, &now);

    // Explicit, though localtime_s above already initialises it:
    // _get_timezone otherwise returns the CRT default, not the OS zone.
    _tzset();
    long tzSec = 0;
    _get_timezone(&tzSec);
    int  utcOffset = (int)(-tzSec / 3600);
    bool dst       = (lt.tm_isdst > 0);
    // NOTE: the 'H' command carries the STANDARD timezone offset and a
    // separate DST flag -- the mount adds the hour itself. Adding it here
    // as well double-counted it and put the mount an hour ahead in summer.
    // Harmless at this site (Arizona keeps standard time year round),
    // which is exactly why it would have gone unnoticed.

    // Phase 66: never push a site we do not have. 0,0 is a real place
    // in the Gulf of Guinea, so sending it is not a harmless default --
    // it actively breaks the alt/az rates, the sky map and the ADC
    // model, and looks like a configured site while doing so. Leaving
    // the mount's own site alone is strictly better than overwriting it
    // with a plausible-looking lie. The clock is independent of the
    // site and still worth setting.
    // Phase 66: the relationship is bidirectional now. Until this, the
    // app could only write, so a mount that already knew where it was --
    // entered by hand on the keypad, or from a SkySync GPS -- had that
    // knowledge ignored and then overwritten.
    bool okSite = true;
    double mLat = 0.0, mLon = 0.0;
    bool mountHasSite = GetSiteLocation(mLat, mLon);

    if (!cfg.SiteIsSet() && mountHasSite) {
        // The mount is the only one who knows. Take it.
        raduk::core::RigConfig::Get().AdoptSite(mLat, mLon, raduk::core::RigConfig::UNKNOWN_ELEV, "mount");
    } else if (cfg.SiteIsSet() && mountHasSite) {
        // Both know. Agreement is reassurance; disagreement is a real
        // finding, and better surfaced before a session than after.
        double km = cfg.DistanceKmTo(mLat, mLon);
        if (km > 25.0) {
            CoreLog("[NexStarMount] WARNING: mount site differs from rig.ini "
                   "by %.0f km - one of them is wrong\n", km);
        } else {
            CoreLog("[NexStarMount] Mount site agrees with rig.ini (%.1f km)\n", km);
        }
        okSite = SetSiteLocation(cfg.siteLatDeg, cfg.siteLonDeg);
    } else if (cfg.SiteIsSet()) {
        okSite = SetSiteLocation(cfg.siteLatDeg, cfg.siteLonDeg);
    } else {
        CoreLog("[NexStarMount] No site known to either the app or the mount "
               "- alt/az tracking cannot be computed\n");
    }
    bool okTime = SetDateTime(lt.tm_hour, lt.tm_min, lt.tm_sec,
                              lt.tm_mon + 1, lt.tm_mday, lt.tm_year + 1900,
                              utcOffset, dst);
    if (!okSite || !okTime)
        CoreLog("[NexStarMount] Site/clock push incomplete -- alt-az tracking "
               "may be refused\n");
    return okSite && okTime;
}

// False for "no reply" as well as for "not aligned"; `answered` says
// which. Only an answer may change the cached state -- a busy hand
// control timing out once must not read as a lost alignment (it did,
// 2026-11-07, refusing a recall seconds after a GoTo had worked).
bool NexStarMount::QueryAligned(bool* answered) {
    char resp[4] = {};
    int len = 0;
    const bool ok = SendCommand("J", 1, resp, 2, len) && len >= 2;
    if (answered) *answered = ok;
    return ok && (resp[0] == 1);
}

// ── Encoding/Decoding ──────────────────────────────────────────

void NexStarMount::EncodePosition(double fraction, char* out) {
    // Convert fraction (0.0–1.0) to 32-bit hex
    uint32_t val = static_cast<uint32_t>(fraction * 4294967296.0);
    sprintf(out, "%08X", val);
}

double NexStarMount::DecodePosition(const char* hex) {
    uint32_t val = 0;
    sscanf(hex, "%8X", &val);
    return static_cast<double>(val) / 4294967296.0;
}

// ── Guide pulses (Phase 5 fallback path) ──────────────────────
// The NexStar 'P' passthrough has no portable timed-pulse form: the AUX
// guide command (msgId 0x26) only exists on newer MC firmware, and the
// previous implementation here sent MC_MOVE_POS/NEG with msgLen=1, which
// never forwarded the duration bytes at all (pulses were no-ops at best).
// Pulses are therefore software-timed: start a fixed-rate slew at the
// slowest rate, hold for durationMs, then stop. BLOCKS the calling thread
// for the duration — only the centroid-tracker correction thread calls this.

bool NexStarMount::PulseGuide(GuideDirection dir, int durationMs) {
    if (!m_connected) return false;

    // Rate 1 ≈ 0.5x sidereal (~7.5 arcsec/s) on NexStar hand controllers.
    // CentroidTracker's MS_PER_ARCSEC pulse sizing assumes roughly this rate.
    // 2026-11-07: rate 3, not 1. A fixed-rate pulse REPLACES the axis's
    // tracking for its duration rather than adding to it, and at rate 1
    // (~12"/s measured) the pulse and the alt-az tracking rate (up to
    // ~15"/s) were the same size: East and West responses were not
    // mirror images and centring drove the planet away. Fast, short pulses
    // make tracking a small correction to the response.
    // Rate 1 measured ~3.8"/s against ~16"/s of azimuth tracking (2026-11-07):
    // a pulse REPLACES tracking, so at rate 1 East and West both moved the
    // planet the same way. Rate 3, with pulses long enough to start the
    // motor (120 ms did not; 400 ms is the next try).
    constexpr int GUIDE_RATE = 1;   // centring uses CentroidTracker::RatePulse now
    constexpr int MAX_PULSE_MS = 1000; // hard safety cap

    if (durationMs < 10) return true;          // below mechanical resolution
    if (durationMs > MAX_PULSE_MS) durationMs = MAX_PULSE_MS;

    int axis      = (dir == GuideDirection::East || dir == GuideDirection::West) ? 0 : 1;
    int direction = (dir == GuideDirection::East || dir == GuideDirection::North) ? 0 : 1;

    if (!m_cmdRunning.load()) return false;
    const char start[8] = { 'P', 2, (char)(axis == 0 ? 16 : 17),
                            (char)(direction == 0 ? 36 : 37),
                            (char)GUIDE_RATE, 0, 0, 0 };
    const char stop[8]  = { 'P', 2, (char)(axis == 0 ? 16 : 17), 36, 0, 0, 0, 0 };

    // The pulse is timed from the START'S ACKNOWLEDGEMENT, not from when
    // it was written: until the '#' comes back the motor has not begun.
    const uint64_t s1 = QueueMotion(axis, start, false);
    if (!WaitMotionSent(axis, s1, 1500)) {
        // Not acknowledged: make sure it is not left running anyway.
        const uint64_t s0 = QueueMotion(axis, stop, true);
        WaitMotionSent(axis, s0, 2000);
        CoreLog("[NexStarMount] PulseGuide: start not acknowledged\n");
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));

    // The stop MUST reach the mount -- a started-but-unstopped pulse is a
    // runaway, and on 2026-11-07 exactly that drove the mount off at the
    // guide rate. The sender resends an unacknowledged stop and emergency-
    // stops after three failures; this waits for it to be confirmed.
    const uint64_t s2 = QueueMotion(axis, stop, true);
    if (WaitMotionSent(axis, s2, 2500)) return true;
    CoreLog("[NexStarMount] PulseGuide: stop not confirmed — EMERGENCY STOP\n");
    EmergencyStop();
    return false;
}

// ── Background poll thread ─────────────────────────────────────
// Runs every 2 seconds. Reads RA/DEC and tracking mode from HC,
// stores in cache. Main thread reads from cache — never blocks.

void NexStarMount::PollLoop() {
    CoreLog("[NexStarMount] Poll thread started\n");
    int consecutiveFailures = 0;

    while (m_pollRunning.load()) {
        // Sleep 4 seconds between polls (interruptible in 100ms chunks)
        // Longer interval reduces serial bus contention with PulseGuide commands
        // from the centroid tracker. Position cache is still fresh enough for UI.
        //
        // While someone is following a GoTo (IsSlewing asked within the
        // last 5 s), the slewing state is refreshed every 500 ms in the
        // gaps, so the UI never makes that round-trip itself.
        for (int i = 0; i < 40 && m_pollRunning.load(); i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const long long now = SteadyMs();
            if (now < m_slewInterestUntilMs.load() &&
                now - m_lastSlewQueryMs.load() >= 500) {
                m_lastSlewQueryMs.store(now);
                bool sl = false;
                if (QuerySlewing(sl)) m_slewingCache.store(sl);
            }
        }

        if (!m_pollRunning.load()) break;

        // Read RA/DEC
        char resp[20] = {};
        int len = 0;
        bool ok = false;
        {
            std::lock_guard<std::mutex> lk(m_serialMutex);
            if (!m_connected) break;
            DrainPendingAcks();
            PurgeComm(m_serial, PURGE_RXCLEAR);
            DWORD written = 0;
            char cmd = 'e';
            if (WriteFile(m_serial, &cmd, 1, &written, nullptr) && written == 1) {
                // Read response with timeout already set on port
                while (len < 18) {
                    DWORD got = 0;
                    char c;
                    if (!ReadFile(m_serial, &c, 1, &got, nullptr) || got == 0) break;
                    resp[len++] = c;
                    if (c == '#') { ok = true; break; }
                }
            }
        }

        if (ok && len >= 17) {
            char raHex[9] = {}, decHex[9] = {};
            memcpy(raHex, resp, 8);
            memcpy(decHex, resp + 9, 8);
            double ra  = DecodePosition(raHex) * 24.0;
            double dec = DecodePosition(decHex) * 360.0;
            if (dec > 180.0) dec -= 360.0;

            // Read tracking mode.
            //
            // Phase 69: this defaulted its local to OFF, so a failed read
            // was stored and displayed as "tracking off" -- identical to
            // the mount genuinely not driving. That ambiguity is the last
            // instance of the pattern that cost the 2026-10-08 session:
            // reporting a value when what happened was a failure to
            // obtain one. Track whether the read actually succeeded and
            // leave the previous mode in place when it did not, rather
            // than inventing OFF.
            char tresp[4] = {}; int tlen = 0;
            TrackingMode mode = OFF;
            bool modeRead = false;
            {
                std::lock_guard<std::mutex> lk(m_serialMutex);
                if (m_connected) {
                    char tcmd = 't';
                    DWORD tw = 0;
                    if (WriteFile(m_serial, &tcmd, 1, &tw, nullptr) && tw == 1) {
                        // The mode byte AND its '#': a '#' left in the
                        // buffer would be counted by a later drain as a
                        // jog's ack, and the real ack then misparsed.
                        DWORD got = 0;
                        if (ReadFile(m_serial, tresp, 2, &got, nullptr) && got >= 1) {
                            mode     = static_cast<TrackingMode>((unsigned char)tresp[0]);
                            modeRead = true;
                        }
                    }
                }
            }

            std::lock_guard<std::mutex> lk(m_cacheMutex);
            m_cachedRA       = ra;
            m_cachedDec      = dec;
            if (modeRead) {
                m_cachedTracking      = mode;
                m_trackingValid       = true;
                m_trackingReadFails   = 0;
            } else if (++m_trackingReadFails >= TRACKING_READ_FAIL_LIMIT) {
                // Sustained failure is real information: the link is up
                // enough to return a position but the mode query keeps
                // failing. Mark it unknown rather than asserting a state.
                m_trackingValid = false;
                if (m_trackingReadFails == TRACKING_READ_FAIL_LIMIT)
                    CoreLog("[NexStarMount] Tracking mode unreadable after %d "
                           "attempts - reporting UNKNOWN, not OFF\n",
                           TRACKING_READ_FAIL_LIMIT);
            }
            m_cacheValid     = true;
            consecutiveFailures = 0;
        }
        if (ok && len >= 17) {
            // Alignment rarely changes; one 'J' a cycle keeps the cached
            // answer honest without the UI ever asking. Outside the
            // cache lock above, so the two mutexes are never nested.
            bool answered = false;
            const bool aligned = QueryAligned(&answered);
            if (answered) m_alignedCache.store(aligned);
        } else {
            consecutiveFailures++;
            if (consecutiveFailures == 1)
                CoreLog("[NexStarMount] Poll: no response from HC (mount off or busy)\n");
            else if (consecutiveFailures % 15 == 0)
                CoreLog("[NexStarMount] Poll: still no response (%d failures)\n",
                       consecutiveFailures);
        }
    }
    CoreLog("[NexStarMount] Poll thread stopped\n");
}

// ── SAFETY: Emergency stop ─────────────────────────────────────
// Sends stop commands on both axes + cancel GoTo.
// Thread-safe — can be called from watchdog, crash handler, or UI.
// Uses raw serial writes without blocking on response.

void NexStarMount::EmergencyStop() {
    if (!m_connected || m_serial == INVALID_HANDLE_VALUE) return;

    CoreLog("[NexStarMount] *** EMERGENCY STOP ***\n");

    // Nothing still queued may start a motor after this. try_lock, not
    // lock: this also runs from the crash handler, which may have
    // interrupted a thread holding the queue's lock.
    if (m_cmdMutex.try_lock()) {
        for (auto& a : m_axisCmd) a.sent = a.want;
        m_cmdMutex.unlock();
        m_sentCv.notify_all();
    }

    // Stop axis 0 (RA/Az) — rate 0
    {
        char cmd[8] = { 'P', 2, 16, 36, 0, 0, 0, 0 };
        std::lock_guard<std::mutex> lk(m_serialMutex);
        DWORD w = 0;
        WriteFile(m_serial, cmd, 8, &w, nullptr);
        m_pendingAcks.fetch_add(1);
    }
    // Stop axis 1 (Dec/Alt) — rate 0
    {
        char cmd[8] = { 'P', 2, 17, 36, 0, 0, 0, 0 };
        std::lock_guard<std::mutex> lk(m_serialMutex);
        DWORD w = 0;
        WriteFile(m_serial, cmd, 8, &w, nullptr);
        m_pendingAcks.fetch_add(1);
    }
    // Cancel GoTo ('M' command)
    {
        char cmd[1] = { 'M' };
        std::lock_guard<std::mutex> lk(m_serialMutex);
        DWORD w = 0;
        WriteFile(m_serial, cmd, 1, &w, nullptr);
        m_pendingAcks.fetch_add(1);
    }

    CoreLog("[NexStarMount] Emergency stop sent on both axes\n");
}

// ── SAFETY: Watchdog thread ────────────────────────────────────
// Checks heartbeat every 500ms.
// If heartbeat goes stale (> WATCHDOG_TIMEOUT_MS), fires emergency stop.

void NexStarMount::WatchdogLoop() {
    CoreLog("[NexStarMount] Watchdog started (timeout=%dms)\n",
           WATCHDOG_TIMEOUT_MS);

    while (m_watchdogRunning.load()) {
        // Check every 500ms — fine enough resolution for a 3s timeout
        for (int i = 0; i < 5 && m_watchdogRunning.load(); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (!m_watchdogRunning.load()) break;
        if (m_watchdogPaused.load()) continue;
        if (!m_connected) continue;

        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        auto last = m_lastHeartbeat.load();
        auto elapsedMs = (now - last) / 1'000'000LL; // nanoseconds → ms

        if (last == 0) continue; // heartbeat never seeded

        if (elapsedMs > WATCHDOG_TIMEOUT_MS) {
            if (!m_watchdogFired.load()) {
                CoreLog("[NexStarMount] *** WATCHDOG FIRED — heartbeat lost for %lldms ***\n",
                       elapsedMs);
                CoreLog("[NexStarMount] App may have frozen or crashed — stopping mount\n");
                m_watchdogFired.store(true);
                EmergencyStop();
            }
            // Keep checking — if app recovers and calls Heartbeat(),
            // the watchdog resets on next cycle
        } else {
            // Heartbeat OK — reset fired flag if it was set
            if (m_watchdogFired.load()) {
                CoreLog("[NexStarMount] Watchdog: heartbeat restored — watchdog reset\n");
                m_watchdogFired.store(false);
            }
        }
    }

    CoreLog("[NexStarMount] Watchdog stopped\n");
}

} // namespace raduk::core

