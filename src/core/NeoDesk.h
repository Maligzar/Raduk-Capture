#pragma once
// ============================================================
//  NeoDesk -- phase 128: fetching the near-Earth objects for the map
//
//  Off the UI thread, at most every kRefreshHours or when asked:
//    1. JPL's close-approach list (NeoList.h says why that list);
//    2. the Sun's geocentric table from Horizons, for brightness;
//    3. a geocentric table for each of the nearest kMaxObjects in time.
//  Nothing about the site is sent: CAD needs no observer, and the
//  Horizons tables are from the centre of the Earth (phase 121).
//
//  Positions() then turns the tables into where each object is from the
//  site, now, and how bright -- cheap, so the map asks every few seconds.
// ============================================================

#include "../sky/NeoList.h"
#include "SatDesk.h"   // the shared HTTPS client

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace raduk::core {

struct NeoMapItem {
    std::string name;      // for the map: "2024 YR4"
    std::string target;    // for FOLLOW: what Horizons is asked for
    double cdJd = 0, distAu = 0, h = 99;
    raduk::sky::NeoView view;
};

class NeoDesk {
public:
    static constexpr double kRefreshHours = 6.0;
    static constexpr int    kMaxObjects   = 20;
    static constexpr int    kDaysBack     = 7;
    static constexpr int    kDaysAhead    = 30;
    static constexpr double kMaxDistAu    = 0.05;   // about 19 lunar distances

    ~NeoDesk() { if (m_thread.joinable()) m_thread.join(); }

    // Starts a fetch if the tables are stale, or always when forced.
    void Refresh(bool force) {
        const double now = raduk::sky::PreciseJdNow();
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (!force && m_fetchedJd > 0 && now - m_fetchedJd < kRefreshHours / 24.0) return;
        }
        if (m_busy.exchange(true)) return;
        if (m_thread.joinable()) m_thread.join();
        m_thread = std::thread([this, now]() { Fetch(now); m_busy.store(false); });
    }
    bool Busy() const { return m_busy.load(); }

    std::vector<std::string> TakeMessages() {
        std::lock_guard<std::mutex> lk(m_mutex);
        std::vector<std::string> m;
        m.swap(m_msgs);
        return m;
    }

    // Every object with a table, brighter than magLimit, as seen now.
    std::vector<NeoMapItem> Positions(double jd, const raduk::sky::LunarObserver& site,
                                      double magLimit) const {
        std::vector<NeoMapItem> out;
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_sun) return out;
        for (const auto& e : m_objects) {
            NeoMapItem it;
            it.name = e.c.des;
            it.target = raduk::sky::HorizonsCommandFor(e.c.des);
            it.cdJd = e.c.cdJd; it.distAu = e.c.distAu; it.h = e.c.h;
            it.view = raduk::sky::ViewNeo(*e.eph, *m_sun, e.c.h, jd, site);
            if (it.view.ok && it.view.mag <= magLimit) out.push_back(it);
        }
        return out;
    }

    int Fetched() const { std::lock_guard<std::mutex> lk(m_mutex); return (int)m_objects.size(); }

private:
    struct Entry {
        raduk::sky::NeoCandidate c;
        std::shared_ptr<raduk::sky::Ephemeris> eph;
    };

    void Say(const std::string& s) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_msgs.push_back("[NEO] " + s);
    }

    static bool Get(const char* host, const std::string& path, std::string& body) {
        const std::wstring wh(host, host + strlen(host));
        const std::wstring wp(path.begin(), path.end());
        return SatDesk::HttpGet(wh.c_str(), wp.c_str(), body);
    }

    void Fetch(double now) {
        using namespace raduk::sky;
        std::string body;
        if (!Get("ssd-api.jpl.nasa.gov", CadPath(now, kDaysBack, kDaysAhead, kMaxDistAu), body)) {
            Say(SatDesk::FailWhy("could not reach JPL's close-approach list") + "; keeping what the map has.");
            return;
        }
        std::vector<NeoCandidate> list = ParseCad(body);
        // One row per object, its approach nearest to now.
        std::sort(list.begin(), list.end(), [now](const NeoCandidate& a, const NeoCandidate& b) {
            return std::fabs(a.cdJd - now) < std::fabs(b.cdJd - now);
        });
        std::vector<NeoCandidate> uniq;
        for (const auto& c : list) {
            bool seen = false;
            for (const auto& u : uniq) if (u.des == c.des) { seen = true; break; }
            if (!seen) uniq.push_back(c);
            if ((int)uniq.size() >= kMaxObjects) break;
        }

        auto sun = std::make_shared<Ephemeris>();
        body.clear();
        if (!Get("ssd.jpl.nasa.gov", HorizonsVectorsPath("10", now - 0.1, now + 1.1, "1 h"), body) ||
            !(*sun = ParseHorizonsVectors(body)).ok) {
            Say(SatDesk::FailWhy("could not get the Sun's position from Horizons") + "; keeping what the map has.");
            return;
        }

        std::vector<Entry> got;
        int failed = 0;
        for (const auto& c : uniq) {
            body.clear();
            auto e = std::make_shared<Ephemeris>();
            if (Get("ssd.jpl.nasa.gov",
                    HorizonsVectorsPath(HorizonsCommandFor(c.des), now - 0.05, now + 1.05, "10 min"), body))
                *e = ParseHorizonsVectors(body);
            if (!e->ok) { ++failed; continue; }
            got.push_back({c, e});
        }
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_sun = sun;
            m_objects = std::move(got);
            m_fetchedJd = now;
        }
        char b[200];
        snprintf(b, sizeof(b), "%zu close approaches within %.2f au from %d days ago to %d ahead; "
                 "tables for %d of the nearest %zu%s.", list.size(), kMaxDistAu, kDaysBack,
                 kDaysAhead, Fetched(), uniq.size(), failed ? ", the rest unavailable" : "");
        Say(b);
    }

    mutable std::mutex m_mutex;
    std::shared_ptr<raduk::sky::Ephemeris> m_sun;
    std::vector<Entry> m_objects;
    std::vector<std::string> m_msgs;
    double m_fetchedJd = 0;
    std::atomic<bool> m_busy{false};
    std::thread m_thread;
};

} // namespace raduk::core
