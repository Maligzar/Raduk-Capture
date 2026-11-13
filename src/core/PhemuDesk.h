#pragma once
// ============================================================
//  PhemuDesk -- phase 141.0: the season's Galilean mutual events,
//  fetched from IMCCE and judged for this site
//
//  Off the UI thread: the predictions from today to the end of the
//  2026-27 season are asked of IMCCE's Opale service in 60-day pieces,
//  and only the mutual events between the four large moons are kept
//  (MutualEvents.h). They go to a cache beside the other data, so the
//  list works with no network once fetched. IMCCE's predictions do not
//  change from week to week, so the cache is refreshed only when it is
//  a month old or missing, or when asked.
//
//  The cache is IMCCE's material, kept for this machine's use. It is
//  not something to commit or pass on: IMCCE's terms do not allow its
//  material to be reproduced without permission.
//
//  Nothing about the site is sent. Opale is asked for geocentric
//  predictions; whether Jupiter is up and the Sun down is worked out
//  here, from the app's own ephemeris.
// ============================================================

#include "../sky/MutualEvents.h"
#include "../sky/Ephemeris.h"      // J2000ToDate, LocalHorizon, SunDirectionOfDate
#include "../sky/SkyObject.h"      // kPlanets, PlanetRADec
#include "Paths.h"
#include "SatDesk.h"               // the shared HTTPS client

#include <atomic>
#include <cmath>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace raduk::core {

class PhemuDesk {
public:
    // The season IMCCE gives: May 2026 to August 2027.
    static constexpr int    kSeasonEndYear = 2027, kSeasonEndMonth = 8, kSeasonEndDay = 31;
    static constexpr int    kChunkDays = 60;
    static constexpr double kRefreshDays = 30.0;

    ~PhemuDesk() { if (m_thread.joinable()) m_thread.join(); }

    static std::string CachePath() { return Paths::Root() + "\\phemu_jupiter_2026_27.txt"; }

    // Reads the cache if there is one. Cheap; call once at start.
    void Load() {
        std::ifstream in(CachePath());
        if (!in) return;
        std::vector<raduk::sky::phemu::Event> evs;
        double fetchedJd = 0;
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("# fetched_jd ", 0) == 0) { fetchedJd = atof(line.c_str() + 13); continue; }
            raduk::sky::phemu::Event e;
            if (raduk::sky::phemu::ParseCacheLine(line, e)) evs.push_back(e);
        }
        std::lock_guard<std::mutex> lk(m_mutex);
        m_events = std::move(evs);
        m_fetchedJd = fetchedJd;
    }

    // Starts a fetch if the cache is missing or a month old, or always
    // when forced. Past the end of the season it does nothing.
    void Refresh(bool force) {
        const double now = raduk::sky::PreciseJdNow();
        if (now > SeasonEndJd()) return;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (!force && m_fetchedJd > 0 && now - m_fetchedJd < kRefreshDays) return;
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

    int Count() const { std::lock_guard<std::mutex> lk(m_mutex); return (int)m_events.size(); }

    // The events with a recording window in [fromJd, toJd], judged for
    // the site. With no site set, every event is returned unjudged and
    // siteKnown says so.
    std::vector<raduk::sky::phemu::Pick> Picks(double fromJd, double toJd,
                                               const raduk::sky::LunarObserver& site,
                                               bool* siteKnown = nullptr) const {
        std::vector<raduk::sky::phemu::Event> evs;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            evs = m_events;
        }
        if (siteKnown) *siteKnown = site.set;
        raduk::sky::phemu::SkyFn sky = [site](double jd) { return SkyAtSite(jd, site); };
        return raduk::sky::phemu::Tonight(evs, fromJd, toJd, sky, {}, !site.set);
    }

    // Jupiter's altitude and the Sun's at the site, from the app's own
    // ephemeris. Good to a fraction of a degree, which is all a 15-degree
    // limit needs.
    static raduk::sky::phemu::SkyAt SkyAtSite(double jdUt, const raduk::sky::LunarObserver& o) {
        using namespace raduk::sky;
        phemu::SkyAt s;
        if (!o.set) return s;
        const PlanetElements* jup = nullptr;
        for (const auto& p : kPlanets) if (std::string(p.name) == "Jupiter") { jup = &p; break; }
        if (!jup) return s;
        double raH = 0, decD = 0;
        if (!PlanetRADec(*jup, jdUt, raH, decD)) return s;
        const double ra = raH * 15.0 * 3.14159265358979323846 / 180.0;
        const double de = decD * 3.14159265358979323846 / 180.0;
        LVec v{std::cos(de) * std::cos(ra), std::cos(de) * std::sin(ra), std::sin(de)};
        v = J2000ToDate(v, JdTdFromUt(jdUt));
        double az = 0;
        LocalHorizon(v, jdUt, o, s.jupiterAltDeg, az);
        LocalHorizon(SunDirectionOfDate(jdUt), jdUt, o, s.sunAltDeg, az);
        s.ok = true;
        return s;
    }

private:
    static double SeasonEndJd() {
        char b[40];
        snprintf(b, sizeof(b), "%04d-%02d-%02dT23:59:59.000", kSeasonEndYear, kSeasonEndMonth, kSeasonEndDay);
        return raduk::sky::phemu::JdFromIsoUtc(b);
    }

    void Say(const std::string& s) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_msgs.push_back("[PHEMU] " + s);
    }

    static bool Get(const char* host, const std::string& path, std::string& body) {
        const std::wstring wh(host, host + strlen(host));
        const std::wstring wp(path.begin(), path.end());
        return SatDesk::HttpGet(wh.c_str(), wp.c_str(), body);
    }

    void Fetch(double now) {
        using namespace raduk::sky::phemu;
        std::vector<Event> all;
        const double end = SeasonEndJd();
        // Start at today's UTC midnight, then walk the season in pieces.
        double day = std::floor(now - 0.5) + 0.5;
        int pieces = 0;
        while (day <= end) {
            const std::string iso = IsoFromJd(day, 0);   // "YYYY-MM-DD 00:00:00"
            const int y = atoi(iso.substr(0, 4).c_str());
            const int m = atoi(iso.substr(5, 2).c_str());
            const int d = atoi(iso.substr(8, 2).c_str());
            const int nbd = (int)(std::min)((double)kChunkDays, std::ceil(end - day));
            if (nbd <= 0) break;
            std::string body, why;
            if (!Get("opale.imcce.fr", OpalePath(y, m, d, nbd), body)) {
                Say(SatDesk::FailWhy("could not reach IMCCE's prediction service") + "; keeping what is cached.");
                return;
            }
            std::vector<Event> part;
            if (!ParseOpale(body, part, &why)) {
                Say("IMCCE's reply could not be used (" + why + "); keeping what is cached.");
                return;
            }
            all.insert(all.end(), part.begin(), part.end());
            day += nbd;
            ++pieces;
        }
        // Two pieces can share an event at their join; keep one.
        std::sort(all.begin(), all.end(),
                  [](const Event& a, const Event& b) { return a.greatestJd < b.greatestJd; });
        std::vector<Event> uniq;
        for (const Event& e : all) {
            if (!uniq.empty() && uniq.back().by == e.by && uniq.back().of == e.of &&
                uniq.back().kind == e.kind && std::fabs(uniq.back().greatestJd - e.greatestJd) < 1.0 / 1440.0)
                continue;
            uniq.push_back(e);
        }

        std::ofstream out(CachePath(), std::ios::trunc);
        if (out) {
            out << "# Galilean mutual events, 2026-27 season (IMCCE PHEMU)\n"
                << "# Source: IMCCE Opale, opale.imcce.fr/api/v1/phenomena/satellites/599\n"
                << "# IMCCE's predictions, kept for this machine's use. Do not commit or pass on.\n"
                << "# begin_jd greatest_jd end_jd (UTC)  type  subtype  by  of  duration_min"
                   "  drop_pct  impact  limb_radii  sep_arcsec\n";
            char b[64];
            snprintf(b, sizeof(b), "# fetched_jd %.6f\n", now);
            out << b;
            for (const Event& e : uniq) out << CacheLine(e) << "\n";
        }
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_events = uniq;
            m_fetchedJd = now;
        }
        char b[200];
        snprintf(b, sizeof(b), "%zu Galilean mutual events from now to the season's end, in %d "
                 "requests to IMCCE%s.", uniq.size(), pieces, out ? "" : " (the cache could not be written)");
        Say(b);
    }

    mutable std::mutex m_mutex;
    std::vector<raduk::sky::phemu::Event> m_events;
    std::vector<std::string> m_msgs;
    double m_fetchedJd = 0;
    std::atomic<bool> m_busy{false};
    std::thread m_thread;
};

}  // namespace raduk::core
