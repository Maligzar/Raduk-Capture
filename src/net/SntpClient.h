#pragma once
// ============================================================
//  SntpClient.h -- phase 141.1: the clock check, over the network
//
//  Asks a time server four times over UDP 123, off the UI thread, and
//  keeps the sample with the shortest round trip (Sntp.h does the
//  arithmetic). The local times are read from GetSystemTimeAsFileTime,
//  the same clock CaptureWriter stamps every frame from, so the offset
//  is the offset of the frame times.
//
//  The server is TimeServer in rig.ini, pool.ntp.org by default. Only
//  a 48-byte SNTP request is sent; nothing about the site or the rig.
// ============================================================

#include "Sntp.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../core/CoreLog.h"
#include "../core/NetPolicy.h"   // phase 151.0
#include "../core/RigConfig.h"

#pragma comment(lib, "ws2_32.lib")

namespace raduk::net {

// UTC seconds since 1970 from the system clock the frames use.
inline double PcClockUnixNow() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    // 100 ns ticks since 1601-01-01; 11644473600 s to 1970.
    return (double)u.QuadPart / 1e7 - 11644473600.0;
}

class ClockCheck {
public:
    ~ClockCheck() { if (m_thread.joinable()) m_thread.join(); }

    // Starts a check unless one is running. Returns false if one was.
    bool Start(const std::string& server) {
        if (m_busy.exchange(true)) return false;
        if (m_thread.joinable()) m_thread.join();
        m_done.store(false);
        m_thread = std::thread([this, server]() {
            const sntp::Sample s = Query(server);
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                m_result = s;
                m_checkedUnix = PcClockUnixNow();
            }
            m_done.store(true);
            m_busy.store(false);
        });
        return true;
    }

    bool Busy() const { return m_busy.load(); }
    bool Done() const { return m_done.load(); }
    sntp::Sample Result() const { std::lock_guard<std::mutex> lk(m_mutex); return m_result; }
    double CheckedUnix() const { std::lock_guard<std::mutex> lk(m_mutex); return m_checkedUnix; }

private:
    static sntp::Sample Query(const std::string& server) {
        sntp::Sample fail;
        // Phase 151.0: an internet time server is not tried while offline
        // (rig.ini Offline=1, or no network a moment ago). The record then
        // says the clock was not confirmed, and why. A LAN server is.
        auto& net = raduk::core::NetPolicy::Get();
        net.SetConfigured(raduk::core::RigConfig::Get().offline != 0);
        const bool local = raduk::core::NetPolicy::IsLocalHost(server);
        const int64_t now = (int64_t)GetTickCount64();
        if (!local && !net.AllowInternet(now)) {
            fail.why = net.Why(now);
            if (net.FirstSkipOf(server))
                raduk::core::CoreLog("[NET] skipped %s: %s\n", server.c_str(), fail.why.c_str());
            return fail;
        }
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { fail.why = "Winsock did not start"; return fail; }
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        addrinfo* res = nullptr;
        if (getaddrinfo(server.c_str(), "123", &hints, &res) != 0 || !res) {
            WSACleanup();
            fail.why = "could not find " + server;
            if (!local) {
                net.NoteNetworkDown(now, server);
                raduk::core::CoreLog("[NET] %s could not be found: %s\n", server.c_str(), net.Why(now).c_str());
            }
            return fail;
        }
        std::vector<sntp::Sample> samples;
        for (int i = 0; i < 4; ++i) {
            SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
            if (s == INVALID_SOCKET) { sntp::Sample x; x.why = "no socket"; samples.push_back(x); continue; }
            DWORD timeoutMs = 1500;
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));
            const double t1 = PcClockUnixNow();
            const std::vector<uint8_t> q = sntp::Request(t1);
            sntp::Sample x;
            if (sendto(s, (const char*)q.data(), (int)q.size(), 0, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
                x.why = "could not send to " + server;
            } else {
                uint8_t r[128];
                const int n = recv(s, (char*)r, sizeof(r), 0);
                const double t4 = PcClockUnixNow();
                if (n <= 0) x.why = server + " did not answer (UDP 123 blocked?)";
                else x = sntp::Read(r, (size_t)n, t1, t4);
            }
            closesocket(s);
            samples.push_back(x);
            Sleep(250);   // spaced, so a busy moment on the network does not set all four
        }
        freeaddrinfo(res);
        WSACleanup();
        const sntp::Sample best = sntp::Best(samples);
        if (best.ok && !local) net.NoteSuccess();
        return best;
    }

    mutable std::mutex m_mutex;
    sntp::Sample m_result;
    double m_checkedUnix = 0;
    std::atomic<bool> m_busy{false};
    std::atomic<bool> m_done{false};
    std::thread m_thread;
};

}  // namespace raduk::net
