#pragma once
// ============================================================
// SerialPortScan.h — find the NexStar hand control's COM port
//
// Replaces the hardcoded "COM3" that used to be baked into the
// startup hardware check and the mount connect path. A USB-serial
// adapter's COM number is assigned by Windows and moves when the
// cable lands on a different USB port, or when another device
// (an ESP32, a GPS dongle) claims the low number first — so the
// port must be discovered, not assumed.
//
// Discovery is two steps:
//   1. Enumerate the ports that actually exist, from
//      HKLM\HARDWARE\DEVICEMAP\SERIALCOMM. This key lists only
//      PRESENT ports, so an unplugged device is never probed.
//   2. Probe each with the NexStar echo command ("Kx" -> "x#").
//      Only a real hand control answers, so a GPS dongle or a
//      dev board on a neighbouring COM number is not mistaken
//      for a mount.
//
// A port that exists but cannot be opened (ERROR_ACCESS_DENIED /
// ERROR_SHARING_VIOLATION) is reported as `busy`: that is the
// signature of CPWI already owning the mount, which the caller
// treats as "mount present, drive it over ASCOM instead".
//
// rig.ini may pin the port explicitly:
//     [Rig]
//     MountPort=COM4
// A pinned port is tried first and, if it answers, wins. If it
// does not answer the scan continues, so a stale INI degrades to
// auto-detect rather than breaking the connect.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>

namespace raduk::core {

// Result of a full scan.
struct MountPortScan {
    std::string port;        // "COM4" — empty if nothing answered
    bool        found = false;   // a hand control answered the echo
    bool        busy  = false;   // port exists but is owned by another app
    std::string busyPort;        // which port was busy (for logging)
    std::vector<std::string> probed;  // every port we looked at
};

// ── Enumerate present serial ports ─────────────────────────────
// Reads HKLM\HARDWARE\DEVICEMAP\SERIALCOMM, whose values are the
// device names and whose data are the COM port names. Ports are
// returned sorted by number so COM3 is probed before COM11.
inline std::vector<std::string> EnumerateSerialPorts() {
    std::vector<std::string> ports;
    HKEY hKey = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "HARDWARE\\DEVICEMAP\\SERIALCOMM",
                      0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        // No key at all means no serial ports are present on this machine.
        return ports;
    }

    for (DWORD i = 0; ; ++i) {
        char  name[256]  = {};
        char  data[256]  = {};
        DWORD nameLen    = sizeof(name);
        DWORD dataLen    = sizeof(data);
        DWORD type       = 0;
        LONG  r = RegEnumValueA(hKey, i, name, &nameLen, nullptr,
                                &type, (LPBYTE)data, &dataLen);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS) continue;
        if (type != REG_SZ || !data[0]) continue;
        ports.emplace_back(data);
    }
    RegCloseKey(hKey);

    // Sort numerically — "COM4" before "COM11", which a plain
    // string sort would get backwards.
    auto portNum = [](const std::string& p) -> int {
        size_t d = p.find_first_of("0123456789");
        if (d == std::string::npos) return 9999;
        return atoi(p.c_str() + d);
    };
    std::sort(ports.begin(), ports.end(),
              [&](const std::string& a, const std::string& b) {
                  return portNum(a) < portNum(b);
              });
    return ports;
}

// ── Probe one port for a NexStar hand control ──────────────────
// Sends the NexStar echo command and checks for the exact reply.
// `outBusy` is set when the port exists but another process holds
// it, which the caller reads as "CPWI has the mount".
//
// Timeouts are deliberately tight (~300ms): a scan may walk
// several ports and startup must not stall on a silent one.
inline bool ProbeNexStarPort(const std::string& comPort, bool* outBusy = nullptr) {
    if (outBusy) *outBusy = false;

    // "\\.\COMn" form — required for COM10 and above, harmless below.
    std::string path = "\\\\.\\" + comPort;
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                           0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (outBusy && (err == ERROR_ACCESS_DENIED || err == ERROR_SHARING_VIOLATION))
            *outBusy = true;
        return false;
    }

    // NexStar HC is 9600 8N1 with no handshaking. Flow control is
    // disabled explicitly: a serial cable without CTS/DSR wired
    // would otherwise stall the write until the timeout.
    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    if (GetCommState(h, &dcb)) {
        dcb.BaudRate       = 9600;
        dcb.ByteSize       = 8;
        dcb.Parity         = NOPARITY;
        dcb.StopBits       = ONESTOPBIT;
        dcb.fOutxCtsFlow   = FALSE;
        dcb.fOutxDsrFlow   = FALSE;
        dcb.fDsrSensitivity= FALSE;
        dcb.fOutX          = FALSE;
        dcb.fInX           = FALSE;
        dcb.fDtrControl    = DTR_CONTROL_DISABLE;
        dcb.fRtsControl    = RTS_CONTROL_DISABLE;
        SetCommState(h, &dcb);
    }

    COMMTIMEOUTS to = {};
    to.ReadIntervalTimeout        = 60;
    to.ReadTotalTimeoutMultiplier = 10;
    to.ReadTotalTimeoutConstant   = 300;
    to.WriteTotalTimeoutConstant  = 300;
    SetCommTimeouts(h, &to);

    // Clear any stale bytes a previous session left in the driver.
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    // Echo command: 'K' + arbitrary byte -> that byte + '#'
    const char  cmd[2] = { 'K', 'x' };
    DWORD wrote = 0;
    bool  ok = false;
    if (WriteFile(h, cmd, 2, &wrote, nullptr) && wrote == 2) {
        char  resp[2] = {};
        DWORD got = 0;
        if (ReadFile(h, resp, 2, &got, nullptr) && got == 2)
            ok = (resp[0] == 'x' && resp[1] == '#');
    }

    CloseHandle(h);
    return ok;
}

// ── Full scan ──────────────────────────────────────────────────
// `preferred` is an optional port from rig.ini, tried first.
// Returns the port that answered, or records a busy port so the
// caller can fall back to ASCOM/CPWI.
inline MountPortScan FindNexStarPort(const std::string& preferred = std::string()) {
    MountPortScan scan;

    auto tryPort = [&](const std::string& p) -> bool {
        bool busy = false;
        scan.probed.push_back(p);
        if (ProbeNexStarPort(p, &busy)) {
            scan.port  = p;
            scan.found = true;
            return true;
        }
        // Remember the first busy port — most likely the mount,
        // held by CPWI.
        if (busy && !scan.busy) {
            scan.busy     = true;
            scan.busyPort = p;
        }
        return false;
    };

    // A pinned port gets first refusal, even if SERIALCOMM does
    // not list it (some virtual-port drivers do not register there).
    if (!preferred.empty() && tryPort(preferred))
        return scan;

    for (const std::string& p : EnumerateSerialPorts()) {
        if (p == preferred) continue;  // already tried
        if (tryPort(p)) return scan;
    }
    return scan;
}

// -- Phase 66: NMEA GPS probe ---------------------------------
// A USB GPS dongle presents as a serial port, so the enumeration above
// already finds it; it just needs a different question asked. Unlike
// the NexStar probe this one is passive -- a receiver streams
// unprompted, so we only listen. Nothing is written, which matters
// when probing a port that might be someone else's device.
//
// Strictly optional. The accuracy work in Phase 66 shows Windows
// Location already clears the pointing requirement by a wide margin,
// so this exists for users who have a dongle, not because one is
// needed.

struct GpsFix {
    bool   valid  = false;
    double latDeg = 0.0;
    double lonDeg = 0.0;
    double elevM  = 0.0;
    bool   hasElev = false;
};

// Parses "ddmm.mmmm" plus a hemisphere character into signed degrees.
// NMEA packs degrees and minutes into one field, so this is not a
// plain decimal read.
inline bool ParseNmeaCoord(const std::string& field, char hemi, double& out) {
    if (field.size() < 3) return false;
    size_t dot = field.find('.');
    if (dot == std::string::npos || dot < 3) return false;
    // Degrees are everything before the last two digits of the integer
    // part; minutes are the rest.
    size_t degLen = dot - 2;
    double deg = atof(field.substr(0, degLen).c_str());
    double min = atof(field.substr(degLen).c_str());
    if (min < 0.0 || min >= 60.0) return false;
    double v = deg + min / 60.0;
    if (hemi == 'S' || hemi == 'W') v = -v;
    out = v;
    return true;
}

// Listens on one port for NMEA sentences. Returns a fix only from a
// sentence that reports a valid satellite lock.
inline bool ProbeNmeaPort(const std::string& comPort, GpsFix& fix,
                          int listenMs = 2500) {
    std::string path = "\\\\.\\" + comPort;
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ,
                           0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    if (GetCommState(h, &dcb)) {
        dcb.BaudRate = 9600;        // near-universal for NMEA dongles
        dcb.ByteSize = 8;
        dcb.Parity   = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fOutxCtsFlow = FALSE; dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl  = DTR_CONTROL_ENABLE;   // some dongles need DTR
        dcb.fRtsControl  = RTS_CONTROL_ENABLE;
        SetCommState(h, &dcb);
    }
    COMMTIMEOUTS to = {};
    to.ReadIntervalTimeout        = 100;
    to.ReadTotalTimeoutConstant   = 500;
    to.ReadTotalTimeoutMultiplier = 1;
    SetCommTimeouts(h, &to);
    PurgeComm(h, PURGE_RXCLEAR);

    std::string buf;
    bool found = false;
    DWORD start = GetTickCount();
    while (GetTickCount() - start < (DWORD)listenMs && !found) {
        char chunk[256];
        DWORD got = 0;
        if (!ReadFile(h, chunk, sizeof(chunk), &got, nullptr)) break;
        if (got == 0) continue;
        buf.append(chunk, got);
        if (buf.size() > 8192) buf.erase(0, buf.size() - 4096);

        size_t nl;
        while ((nl = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, nl);
            buf.erase(0, nl + 1);
            // Accept any talker ID: GP (GPS), GN (multi-GNSS), GL, GA.
            if (line.size() < 20 || line[0] != '$') continue;
            bool isGGA = line.compare(3, 3, "GGA") == 0;
            if (!isGGA) continue;

            std::vector<std::string> f;
            size_t pos = 0;
            while (pos <= line.size()) {
                size_t c = line.find(',', pos);
                if (c == std::string::npos) { f.push_back(line.substr(pos)); break; }
                f.push_back(line.substr(pos, c - pos));
                pos = c + 1;
            }
            // GGA: 1 time, 2 lat, 3 N/S, 4 lon, 5 E/W, 6 quality, 9 alt
            if (f.size() < 10) continue;
            if (f[6].empty() || atoi(f[6].c_str()) == 0) continue;  // no lock

            double la = 0, lo = 0;
            if (!ParseNmeaCoord(f[2], f[3].empty() ? 'N' : f[3][0], la)) continue;
            if (!ParseNmeaCoord(f[4], f[5].empty() ? 'E' : f[5][0], lo)) continue;

            fix.latDeg = la;
            fix.lonDeg = lo;
            if (!f[9].empty()) { fix.elevM = atof(f[9].c_str()); fix.hasElev = true; }
            fix.valid = true;
            found = true;
            break;
        }
    }
    CloseHandle(h);
    if (found)
        printf("[SerialPortScan] NMEA fix on %s: %.1f%c %.1f%c\n",
               comPort.c_str(),
               fix.latDeg < 0 ? -fix.latDeg : fix.latDeg, fix.latDeg < 0 ? 'S' : 'N',
               fix.lonDeg < 0 ? -fix.lonDeg : fix.lonDeg, fix.lonDeg < 0 ? 'W' : 'E');
    return found;
}

// Walks the present ports looking for a receiver. skipPort lets the
// caller exclude the mount, which is already known and must not be
// tied up.
inline bool FindGpsFix(GpsFix& fix, const std::string& skipPort = std::string()) {
    for (const std::string& p : EnumerateSerialPorts()) {
        if (!skipPort.empty() && p == skipPort) continue;
        if (ProbeNmeaPort(p, fix)) return true;
    }
    return false;
}

// Comma-joined list of probed ports, for a one-line log message.
inline std::string DescribeProbedPorts(const MountPortScan& scan) {
    if (scan.probed.empty()) return "none present";
    std::string s;
    for (size_t i = 0; i < scan.probed.size(); ++i) {
        if (i) s += ", ";
        s += scan.probed[i];
    }
    return s;
}

} // namespace raduk::core
