#pragma once
// ============================================================
//  JupiterDesk -- phase 137: the next few days of moon and shadow
//  events, fetched from JPL Horizons and cached
//
//  Off the UI thread, like PhemuDesk. For each Galilean moon: Horizons'
//  visibility codes (transits, occultations, eclipses) at 2-minute
//  steps, and jovicentric vectors of the moon, the Sun and the Earth at
//  10-minute steps for the shadow transits (JupiterEvents.h). Three
//  days ahead, refreshed when the cache no longer covers the next two.
//
//  Nothing about the site is sent: the requests are geocentric, and
//  whether Jupiter is up is judged here. Horizons' output is NASA/JPL
//  data and may be kept.
// ============================================================

#include "../sky/JupiterEvents.h"
#include "Paths.h"
#include "SatDesk.h"   // the shared HTTPS client

#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace raduk::core {

class JupiterDesk {
public:
    static constexpr double kAheadDays = 3.0, kMinCoverDays = 2.0;

    ~JupiterDesk() { if (m_thread.joinable()) m_thread.join(); }

    static std::string CachePath() { return Paths::Root() + "\\jupiter_events.txt"; }

    static std::string CacheLine(const raduk::sky::jupiter::Event& e) {
        char b[120];
        snprintf(b, sizeof(b), "%d %d %.6f %.6f %d %d", e.moon, (int)e.kind, e.startJd, e.endJd,
                 e.startKnown ? 1 : 0, e.endKnown ? 1 : 0);
        return b;
    }
    static bool ParseCacheLine(const std::string& l, raduk::sky::jupiter::Event& e) {
        int k = 0, sk = 1, ek = 1;
        if (l.empty() || l[0] == '#') return false;
        if (sscanf(l.c_str(), "%d %d %lf %lf %d %d", &e.moon, &k, &e.startJd, &e.endJd, &sk, &ek) != 6) return false;
        if (e.moon < 1 || e.moon > 4 || k < 0 || k > 3) return false;
        e.kind = (raduk::sky::jupiter::Kind)k; e.startKnown = sk != 0; e.endKnown = ek != 0;
        return true;
    }

    void Load() {
        std::ifstream in(CachePath());
        if (!in) return;
        std::vector<raduk::sky::jupiter::Event> evs;
        double coverTo = 0;
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("# cover_to_jd ", 0) == 0) { coverTo = atof(line.c_str() + 14); continue; }
            raduk::sky::jupiter::Event e;
            if (ParseCacheLine(line, e)) evs.push_back(e);
        }
        std::lock_guard<std::mutex> lk(m_mutex);
        m_events = std::move(evs);
        m_coverToJd = coverTo;
    }

    void Refresh(bool force) {
        const double now = raduk::sky::PreciseJdNow();
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (!force && m_coverToJd - now > kMinCoverDays) return;
        }
        if (m_busy.exchange(true)) return;
        if (m_thread.joinable()) m_thread.join();
        m_thread = std::thread([this, now]() { Fetch(now); m_busy.store(false); });
    }
    bool Busy() const { return m_busy.load(); }
    double CoverToJd() const { std::lock_guard<std::mutex> lk(m_mutex); return m_coverToJd; }

    std::vector<std::string> TakeMessages() {
        std::lock_guard<std::mutex> lk(m_mutex);
        std::vector<std::string> m; m.swap(m_msgs); return m;
    }

    // Events overlapping [fromJd, toJd], in time order.
    std::vector<raduk::sky::jupiter::Event> Events(double fromJd, double toJd) const {
        std::vector<raduk::sky::jupiter::Event> out;
        std::lock_guard<std::mutex> lk(m_mutex);
        for (const auto& e : m_events)
            if (e.endJd >= fromJd && e.startJd <= toJd) out.push_back(e);
        std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.startJd < b.startJd; });
        return out;
    }

private:
    void Say(const std::string& s) { std::lock_guard<std::mutex> lk(m_mutex); m_msgs.push_back("[JUPITER] " + s); }

    static bool Get(const std::string& path, std::string& body) {
        const std::wstring wp(path.begin(), path.end());
        return SatDesk::HttpGet(L"ssd.jpl.nasa.gov", wp.c_str(), body);
    }

    void Fetch(double now) {
        using namespace raduk::sky::jupiter;
        const double from = now - 2.0 / 24.0, to = now + kAheadDays;
        std::vector<Event> all;
        std::string body;
        if (!Get(JovicentricVectorsPath("10", from, to), body)) { Say(SatDesk::FailWhy("could not reach JPL Horizons") + "; keeping what is cached."); return; }
        const auto sun = raduk::sky::ParseHorizonsVectors(body);
        if (!Get(JovicentricVectorsPath("399", from, to), body)) { Say(SatDesk::FailWhy("could not reach JPL Horizons") + "; keeping what is cached."); return; }
        const auto earth = raduk::sky::ParseHorizonsVectors(body);
        if (sun.pts.size() < 2 || earth.pts.size() < 2) { Say("Horizons sent no usable vectors (" + sun.why + earth.why + ")."); return; }
        for (int moon = 1; moon <= 4; ++moon) {
            if (!Get(VisibilityPath(moon, from, to), body)) { Say(SatDesk::FailWhy("could not reach JPL Horizons") + "; keeping what is cached."); return; }
            const auto ev = EventsFromVisibility(moon, ParseVisibility(body));
            all.insert(all.end(), ev.begin(), ev.end());
            if (!Get(JovicentricVectorsPath("50" + std::to_string(moon), from, to), body)) { Say(SatDesk::FailWhy("could not reach JPL Horizons") + "; keeping what is cached."); return; }
            const auto mv = raduk::sky::ParseHorizonsVectors(body);
            const auto sh = EventsFromVectors(moon, mv, sun, earth, true, false);
            all.insert(all.end(), sh.begin(), sh.end());
        }
        std::sort(all.begin(), all.end(), [](const Event& a, const Event& b) { return a.startJd < b.startJd; });
        std::ofstream out(CachePath(), std::ios::trunc);
        if (out) {
            out << "# Jupiter moon and shadow events, from JPL Horizons (phase 137)\n"
                << "# moon kind(0 transit 1 shadow 2 occultation 3 eclipse) start_jd end_jd start_known end_known (UT)\n";
            char b[64]; snprintf(b, sizeof(b), "# cover_to_jd %.6f\n", to); out << b;
            for (const auto& e : all) out << CacheLine(e) << "\n";
        }
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_events = all;
            m_coverToJd = to;
        }
        char b[160];
        snprintf(b, sizeof(b), "%zu moon and shadow events for the next %.0f days, from JPL Horizons.", all.size(), kAheadDays);
        Say(b);
    }

    mutable std::mutex m_mutex;
    std::vector<raduk::sky::jupiter::Event> m_events;
    std::vector<std::string> m_msgs;
    double m_coverToJd = 0;
    std::atomic<bool> m_busy{false};
    std::thread m_thread;
};

}  // namespace raduk::core
