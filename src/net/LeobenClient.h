// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once
// ============================================================
// LeobenClient.h -- fetches GET /weather/current from Leoben
//
// One background thread. It asks once at start, then every 10 minutes
// -- Leoben itself refreshes current conditions every 10 -- and, after
// a failure, every 2 until Leoben answers again. Elminster is a gaming
// PC: it is off, asleep or busy as often as not, so failure is the
// ordinary case here and is never more than a status line.
//
// Cost on the home network: one request of a few hundred bytes and a
// reply of a few KB every 10 minutes. Nothing a game would notice.
//
// The token is sent as "Authorization: Bearer", Leoben's own scheme
// (its middleware/auth.py), and is never logged. Parsing is
// LeobenWeather.h's and is tested there; this file only moves bytes.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "LeobenWeather.h"

#pragma comment(lib, "winhttp.lib")

namespace raduk::net {

class LeobenClient {
public:
    enum class State { Off, Waiting, Ok, Unreachable, Refused, NoData };

    struct Status {
        State            state = State::Off;
        LeobenConditions last;          // last GOOD reading, kept across failures
        int              httpStatus = 0;
        int64_t          lastAttemptUnix = 0;
    };

    using LogFn = std::function<void(const std::string&)>;

    ~LeobenClient() { Stop(); }

    // Does nothing, and reports Off, when the URL is empty or not one
    // ParseLeobenUrl accepts -- the log says which.
    void Start(const std::string& url, const std::string& token, LogFn log) {
        Stop();
        m_log = std::move(log);
        if (url.empty()) { Set(State::Off, 0); return; }
        if (!ParseLeobenUrl(url, m_ep)) {
            if (m_log) m_log("[LEOBEN] LeobenUrl in rig.ini is not http://host:port - weather from Leoben is off");
            Set(State::Off, 0);
            return;
        }
        m_token = token;
        if (m_token.empty() && m_log)
            m_log("[LEOBEN] No LeobenToken in rig.ini - Leoben will refuse the request unless it runs without auth");
        Set(State::Waiting, 0);
        m_stop = false;
        m_thread = std::thread([this] { Run(); });
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lk(m_waitMu);
            m_stop = true;
        }
        m_cv.notify_all();
        if (m_thread.joinable()) m_thread.join();
    }

    Status Get() const {
        std::lock_guard<std::mutex> lk(m_mu);
        return m_status;
    }

    static const char* StateText(State s) {
        switch (s) {
        case State::Off:         return "off";
        case State::Waiting:     return "waiting for Leoben";
        case State::Ok:          return "ok";
        case State::Unreachable: return "Leoben not reachable";
        case State::Refused:     return "Leoben refused the token";
        case State::NoData:      return "Leoben has no weather yet";
        }
        return "?";
    }

private:
    static constexpr int kPollOkSec   = 10 * 60;
    static constexpr int kPollFailSec = 2 * 60;

    void Set(State s, int http) {
        std::lock_guard<std::mutex> lk(m_mu);
        m_status.state = s;
        m_status.httpStatus = http;
        m_status.lastAttemptUnix = (int64_t)std::time(nullptr);
    }

    void Run() {
        State lastLogged = State::Off;
        for (;;) {
            int http = 0;
            std::string body;
            const bool got = Fetch(http, body);
            State s;
            LeobenConditions c;
            if (!got)                          s = State::Unreachable;
            else if (http == 401 || http == 403) s = State::Refused;
            else if (http == 200 && ParseLeobenCurrent(body, c)) s = State::Ok;
            else                               s = State::NoData;

            {
                std::lock_guard<std::mutex> lk(m_mu);
                m_status.state = s;
                m_status.httpStatus = http;
                m_status.lastAttemptUnix = (int64_t)std::time(nullptr);
                if (s == State::Ok) {
                    c.fetchedUnix = m_status.lastAttemptUnix;
                    m_status.last = c;
                }
            }
            // Log changes of state, not every poll: a PC that is off all
            // night would otherwise write the same line 200 times.
            if (s != lastLogged && m_log) {
                std::string m = std::string("[LEOBEN] ") + StateText(s);
                if (http) m += " (HTTP " + std::to_string(http) + ")";
                m_log(m);
                lastLogged = s;
            }

            std::unique_lock<std::mutex> lk(m_waitMu);
            const int wait = (s == State::Ok) ? kPollOkSec : kPollFailSec;
            if (m_cv.wait_for(lk, std::chrono::seconds(wait), [this] { return m_stop.load(); }))
                return;
        }
    }

    bool Fetch(int& http, std::string& body) {
        http = 0; body.clear();
        HINTERNET hs = WinHttpOpen(L"RadukCapture/2", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hs) return false;
        WinHttpSetTimeouts(hs, 5000, 5000, 5000, 5000);
        HINTERNET hc = WinHttpConnect(hs, m_ep.host.c_str(), (INTERNET_PORT)m_ep.port, 0);
        HINTERNET hr = hc ? WinHttpOpenRequest(hc, L"GET", L"/weather/current", nullptr,
                                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0)
                          : nullptr;
        bool ok = false;
        if (hr) {
            std::wstring hdr = L"Accept: application/json\r\n";
            if (!m_token.empty())
                hdr += L"Authorization: Bearer " + std::wstring(m_token.begin(), m_token.end()) + L"\r\n";
            if (WinHttpSendRequest(hr, hdr.c_str(), (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hr, nullptr)) {
                DWORD code = 0, sz = sizeof(code);
                WinHttpQueryHeaders(hr, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
                http = (int)code;
                for (;;) {
                    DWORD avail = 0;
                    if (!WinHttpQueryDataAvailable(hr, &avail) || avail == 0) break;
                    // A weather snapshot is a few KB. Anything past 256 KB
                    // is not one, and is not read into memory.
                    if (body.size() + avail > 256 * 1024) { body.clear(); break; }
                    std::string chunk(avail, '\0');
                    DWORD got = 0;
                    if (!WinHttpReadData(hr, chunk.data(), avail, &got) || got == 0) break;
                    body.append(chunk.data(), got);
                }
                ok = true;
            }
        }
        if (hr) WinHttpCloseHandle(hr);
        if (hc) WinHttpCloseHandle(hc);
        WinHttpCloseHandle(hs);
        return ok;
    }

    LeobenEndpoint          m_ep;
    std::string             m_token;
    LogFn                   m_log;
    std::thread             m_thread;
    std::atomic<bool>       m_stop{false};
    std::mutex              m_waitMu;
    std::condition_variable m_cv;
    mutable std::mutex      m_mu;
    Status                  m_status;
};

} // namespace raduk::net
