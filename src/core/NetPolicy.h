#pragma once
// ============================================================
//  NetPolicy -- Phase 151.0: may the app reach the internet now?
//
//  The owner's rule 7: nothing ever needs the internet. Every fetch the
//  app makes from the internet (JPL Horizons and SBDB, CelesTrak, IMCCE,
//  the time server) asks here first, so that with no network:
//    - nothing waits on a lookup or a connect that cannot succeed;
//    - each fetcher keeps what it has cached and says it is offline,
//      rather than reporting an error with no way forward.
//
//  Two ways to be offline:
//    - configured: rig.ini Offline=1. Nothing on the internet is tried.
//    - detected: an internet host would not resolve. Further
//      internet fetches are skipped for kBackoffMs, then one is tried
//      again, so a network that comes back is used within ten minutes
//      and a night at a dark site does not retry every few seconds.
//      A server's own trouble (refusing, a 404, a 500, a slow reply) is
//      not the network being down and does not count.
//
//  Hosts on this PC or the LAN (Leoben, the dashboard, Ollama, a local
//  time server) are never gated: IsLocalHost.
//
//  Pure: no Windows, a caller-supplied clock, so it is tested with g++.
// ============================================================

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <set>
#include <string>

namespace raduk::core {

class NetPolicy {
public:
    static constexpr int64_t kBackoffMs = 10 * 60 * 1000;

    // The one the app uses. Tests make their own.
    static NetPolicy& Get() { static NetPolicy p; return p; }

    void SetConfigured(bool offline) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_configured = offline;
    }
    bool Configured() const { std::lock_guard<std::mutex> lk(m_mutex); return m_configured; }

    // May an internet fetch be tried at nowMs (any monotonic clock)?
    bool AllowInternet(int64_t nowMs) const {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_configured) return false;
        return !(m_downSinceMs >= 0 && nowMs - m_downSinceMs < kBackoffMs);
    }

    // The network looked down: an internet host did not resolve.
    // Starts (or restarts) the back-off from nowMs.
    void NoteNetworkDown(int64_t nowMs, const std::string& host) {
        std::lock_guard<std::mutex> lk(m_mutex);
        // A new spell (none running, or the last one over) names this host.
        if (m_downSinceMs < 0 || nowMs - m_downSinceMs >= kBackoffMs) m_firstHost = host;
        m_downSinceMs = nowMs;
        m_skippedLogged.clear();
    }

    // A fetch reached its server: the network is up.
    void NoteSuccess() {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_downSinceMs = -1;
        m_skippedLogged.clear();
    }

    bool DetectedDown(int64_t nowMs) const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_downSinceMs >= 0 && nowMs - m_downSinceMs < kBackoffMs;
    }

    // Why fetches are being skipped, for the log and the fetchers'
    // messages. Empty when they are not.
    std::string Why(int64_t nowMs) const {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_configured) return "offline (rig.ini Offline=1)";
        if (m_downSinceMs >= 0 && nowMs - m_downSinceMs < kBackoffMs) {
            const int64_t left = kBackoffMs - (nowMs - m_downSinceMs);
            const int mins = (int)((left + 59999) / 60000);
            char b[160];
            std::snprintf(b, sizeof(b), "no network (%s could not be found); next try in %d min",
                          m_firstHost.c_str(), mins);
            return b;
        }
        return std::string();
    }

    // True the first time a host is skipped in this offline spell, so the
    // log says so once per host, not once per fetch.
    bool FirstSkipOf(const std::string& host) {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_skippedLogged.insert(host).second;
    }

    // This PC or the LAN, never gated: localhost, a name with no dot
    // (a LAN machine such as "elminster"), *.local / *.lan / *.home.arpa,
    // and the private and loopback IPv4 ranges.
    static bool IsLocalHost(std::string h) {
        for (auto& c : h) c = (char)std::tolower((unsigned char)c);
        while (!h.empty() && h.back() == '.') h.pop_back();
        if (h.empty()) return false;
        if (h == "localhost" || h == "::1") return true;
        if (h.find('.') == std::string::npos && h.find(':') == std::string::npos) return true;
        auto ends = [&](const char* s) {
            const std::string t(s);
            return h.size() > t.size() && h.compare(h.size() - t.size(), t.size(), t) == 0;
        };
        if (ends(".local") || ends(".lan") || ends(".home.arpa") || ends(".localdomain")) return true;
        unsigned a, b, c, d; char tail;
        if (std::sscanf(h.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) == 4 &&
            a < 256 && b < 256 && c < 256 && d < 256) {
            if (a == 10 || a == 127) return true;
            if (a == 172 && b >= 16 && b <= 31) return true;
            if (a == 192 && b == 168) return true;
            if (a == 169 && b == 254) return true;
        }
        return false;
    }

private:
    mutable std::mutex m_mutex;
    bool        m_configured  = false;
    int64_t     m_downSinceMs = -1;
    std::string m_firstHost;
    std::set<std::string> m_skippedLogged;
};

} // namespace raduk::core
