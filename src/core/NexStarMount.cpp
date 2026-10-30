// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// ============================================================
// NexStarMount.cpp — Celestron NexStar serial mount controller
// Section 3.6 of Architecture Doc
// ============================================================

#include "NexStarMount.h"
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
        printf("[NexStarMount] Already connected\n");
        return false;
    }

    std::string portPath = "\\\\.\\" + comPort;
    m_serial = CreateFileA(
        portPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, 0, nullptr);

    if (m_serial == INVALID_HANDLE_VALUE) {
        printf("[NexStarMount] Failed to open %s: %lu\n", comPort.c_str(), GetLastError());
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
        printf("[NexStarMount] Connected on %s — HC version %d.%d\n",
               comPort.c_str(), major, minor);
    } else {
        char resp[4] = {}; int len = 0;
        if (SendCommand("K", 1, resp, 2, len) && resp[0] == 'K') {
            printf("[NexStarMount] Connected on %s — echo OK (no version)\n",
                   comPort.c_str());
        } else {
            printf("[NexStarMount] Connected on %s — WARNING: no response to echo\n"
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
    if (IsAligned()) {
        printf("[NexStarMount] Mount reports ALIGNED - leaving its site and "
               "clock untouched\n");
    } else {
        printf("[NexStarMount] Mount reports NOT aligned - pushing site and "
               "clock\n");
        PushSiteAndTime();
    }

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

    // Emergency stop before disconnecting — always leave mount stopped
    printf("[NexStarMount] Disconnect: sending emergency stop\n");
    EmergencyStop();

    // Stop watchdog first (it uses serial via EmergencyStop)
    m_watchdogRunning.store(false);
    if (m_watchdogThread.joinable()) m_watchdogThread.join();

    // Stop poll thread
    m_pollRunning.store(false);
    if (m_pollThread.joinable()) m_pollThread.join();

    // Close the handle under the serial mutex — a UI/correction thread
    // already inside SendCommand could otherwise race ReadFile against
    // CloseHandle (review T18).
    m_connected = false;
    {
        std::lock_guard<std::mutex> lk(m_serialMutex);
        CloseHandle(m_serial);
        m_serial = INVALID_HANDLE_VALUE;
    }
    printf("[NexStarMount] Disconnected from %s\n", m_comPort.c_str());
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
        printf("[NexStarMount] SendCommandNoWait: not connected\n");
        return false;
    }
    PurgeComm(m_serial, PURGE_RXCLEAR);
    DWORD written = 0;
    BOOL ok = WriteFile(m_serial, cmd, cmdLen, &written, nullptr);
    if (!ok || (int)written != cmdLen) {
        printf("[NexStarMount] WriteFile failed: ok=%d written=%lu expected=%d err=%lu\n",
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
    return SendCommand(cmd, 18, resp, 2, len);
}

bool NexStarMount::IsSlewing() {
    char resp[4] = {};
    int len = 0;
    if (!SendCommand("L", 1, resp, 2, len)) return false;
    return (resp[0] == '1');
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
        printf("[NexStarMount] Tracking %s: no response to command\n",
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
        printf("[NexStarMount] Tracking %s: ACKed but readback failed\n",
               TrackingModeName(mode));
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(m_cacheMutex);
        m_cachedTracking = static_cast<TrackingMode>(actual);
    }

    bool ok = (actual == (int)mode);
    printf("[NexStarMount] Tracking %s requested, mount reports %s : %s\n",
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

    printf("[NexStarMount] SlewFixed axis=%d dir=%d rate=%d\n", axis, direction, rate);
    // Fire-and-forget — don't block waiting for '#' acknowledgement
    bool ok = SendCommandNoWait(cmd, 8);
    if (!ok) printf("[NexStarMount] SlewFixed write FAILED\n");
    return ok;
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

    printf("[NexStarMount] SlewVariable axis=%d dir=%d %.2f\"/s "
           "(sent %.2f, bytes %u/%u)\n",
           axis, direction, arcsecPerSec, r.actualArcsecPerSec,
           (unsigned)r.hi, (unsigned)r.lo);
    // Fire-and-forget, like SlewFixed: the mount is already moving and
    // a blocking read here would stall the jog.
    bool ok = SendCommandNoWait(cmd, 8);
    if (!ok) printf("[NexStarMount] SlewVariable write FAILED\n");
    return ok;
}

void NexStarMount::StopSlew(int axis) {
    printf("[NexStarMount] StopSlew axis=%d\n", axis);
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
        printf("[NexStarMount] Site read: no reply\n");
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
        printf("[NexStarMount] Site read: implausible reply, ignoring\n");
        return false;
    }
    // A mount that has never been given a site reports 0,0. That is a
    // real place, but as a mount default it means "unset" -- adopting it
    // is the very mistake this work exists to prevent.
    if (la == 0.0 && lo == 0.0) {
        printf("[NexStarMount] Site read: mount reports 0,0 (never set)\n");
        return false;
    }

    latDeg = la;
    lonDeg = lo;
    printf("[NexStarMount] Site read from mount: %.1f%c %.1f%c\n",
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
    printf("[NexStarMount] Site -> %d%c%02d'%02d\" %s, %d%c%02d'%02d\" %s : %s\n",
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
    printf("[NexStarMount] Clock -> %04d-%02d-%02d %02d:%02d:%02d UTC%+d dst=%d : %s\n",
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
            printf("[NexStarMount] WARNING: mount site differs from rig.ini "
                   "by %.0f km - one of them is wrong\n", km);
        } else {
            printf("[NexStarMount] Mount site agrees with rig.ini (%.1f km)\n", km);
        }
        okSite = SetSiteLocation(cfg.siteLatDeg, cfg.siteLonDeg);
    } else if (cfg.SiteIsSet()) {
        okSite = SetSiteLocation(cfg.siteLatDeg, cfg.siteLonDeg);
    } else {
        printf("[NexStarMount] No site known to either the app or the mount "
               "- alt/az tracking cannot be computed\n");
    }
    bool okTime = SetDateTime(lt.tm_hour, lt.tm_min, lt.tm_sec,
                              lt.tm_mon + 1, lt.tm_mday, lt.tm_year + 1900,
                              utcOffset, dst);
    if (!okSite || !okTime)
        printf("[NexStarMount] Site/clock push incomplete -- alt-az tracking "
               "may be refused\n");
    return okSite && okTime;
}

bool NexStarMount::IsAligned() {
    char resp[4] = {};
    int len = 0;
    if (!SendCommand("J", 1, resp, 2, len)) return false;
    return (resp[0] == 1);
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
    constexpr int GUIDE_RATE = 1;
    constexpr int MAX_PULSE_MS = 1000; // hard safety cap

    if (durationMs < 10) return true;          // below mechanical resolution
    if (durationMs > MAX_PULSE_MS) durationMs = MAX_PULSE_MS;

    int axis      = (dir == GuideDirection::East || dir == GuideDirection::West) ? 0 : 1;
    int direction = (dir == GuideDirection::East || dir == GuideDirection::North) ? 0 : 1;

    if (!SlewFixed(axis, direction, GUIDE_RATE)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));

    // The stop MUST reach the mount — a started-but-unstopped pulse is a
    // runaway. Retry the stop, and escalate to EmergencyStop if it won't send.
    for (int attempt = 0; attempt < 3; attempt++) {
        if (SlewFixed(axis, 0, 0)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    printf("[NexStarMount] PulseGuide: stop failed to send — EMERGENCY STOP\n");
    EmergencyStop();
    return false;
}

// ── Background poll thread ─────────────────────────────────────
// Runs every 2 seconds. Reads RA/DEC and tracking mode from HC,
// stores in cache. Main thread reads from cache — never blocks.

void NexStarMount::PollLoop() {
    printf("[NexStarMount] Poll thread started\n");
    int consecutiveFailures = 0;

    while (m_pollRunning.load()) {
        // Sleep 4 seconds between polls (interruptible in 100ms chunks)
        // Longer interval reduces serial bus contention with PulseGuide commands
        // from the centroid tracker. Position cache is still fresh enough for UI.
        for (int i = 0; i < 40 && m_pollRunning.load(); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

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
                        DWORD got = 0;
                        if (ReadFile(m_serial, tresp, 1, &got, nullptr) && got == 1) {
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
                    printf("[NexStarMount] Tracking mode unreadable after %d "
                           "attempts - reporting UNKNOWN, not OFF\n",
                           TRACKING_READ_FAIL_LIMIT);
            }
            m_cacheValid     = true;
            consecutiveFailures = 0;
        } else {
            consecutiveFailures++;
            if (consecutiveFailures == 1)
                printf("[NexStarMount] Poll: no response from HC (mount off or busy)\n");
            else if (consecutiveFailures % 15 == 0)
                printf("[NexStarMount] Poll: still no response (%d failures)\n",
                       consecutiveFailures);
        }
    }
    printf("[NexStarMount] Poll thread stopped\n");
}

// ── SAFETY: Emergency stop ─────────────────────────────────────
// Sends stop commands on both axes + cancel GoTo.
// Thread-safe — can be called from watchdog, crash handler, or UI.
// Uses raw serial writes without blocking on response.

void NexStarMount::EmergencyStop() {
    if (!m_connected || m_serial == INVALID_HANDLE_VALUE) return;

    printf("[NexStarMount] *** EMERGENCY STOP ***\n");

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

    printf("[NexStarMount] Emergency stop sent on both axes\n");
}

// ── SAFETY: Watchdog thread ────────────────────────────────────
// Checks heartbeat every 500ms.
// If heartbeat goes stale (> WATCHDOG_TIMEOUT_MS), fires emergency stop.

void NexStarMount::WatchdogLoop() {
    printf("[NexStarMount] Watchdog started (timeout=%dms)\n",
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
                printf("[NexStarMount] *** WATCHDOG FIRED — heartbeat lost for %lldms ***\n",
                       elapsedMs);
                printf("[NexStarMount] App may have frozen or crashed — stopping mount\n");
                m_watchdogFired.store(true);
                EmergencyStop();
            }
            // Keep checking — if app recovers and calls Heartbeat(),
            // the watchdog resets on next cycle
        } else {
            // Heartbeat OK — reset fired flag if it was set
            if (m_watchdogFired.load()) {
                printf("[NexStarMount] Watchdog: heartbeat restored — watchdog reset\n");
                m_watchdogFired.store(false);
            }
        }
    }

    printf("[NexStarMount] Watchdog stopped\n");
}

} // namespace raduk::core

