#pragma once
// ============================================================
// PointingModel.h — Pointing error database for RadukCapture
//
// Records the difference between where the mount THINKS it is
// (NexStar reported RA/Dec) and where it ACTUALLY is (ASTAP
// plate-solve result). Accumulated over a session and across
// nights, this model pre-corrects GoTo slews so the target
// lands in the FOV without needing the CENTERING stage.
//
// Storage: CSV file at D:\RadukCapture\data\pointing_model.csv
// Format:  timestamp, az, alt, ra_expected, dec_expected,
//          ra_actual, dec_actual, error_arcsec, temp_c,
//          seeing_score, object_name
//
// No external dependencies — plain file I/O only.
// Thread-safe: all writes serialised via mutex.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <cstdio>
#include "core/Paths.h"
#include <cmath>
#include <algorithm>
#include <ctime>

namespace raduk::sky {

struct PointingEntry {
    // When
    std::string timestamp;      // ISO 8601 local time

    // Where the mount was pointing (Az/Alt for spatial queries)
    double az_deg    = 0.0;
    double alt_deg   = 0.0;

    // The error vector
    double ra_expected  = 0.0;  // mount-reported RA (hours)
    double dec_expected = 0.0;  // mount-reported Dec (degrees)
    double ra_actual    = 0.0;  // plate-solved RA (hours)
    double dec_actual   = 0.0;  // plate-solved Dec (degrees)
    double error_arcsec = 0.0;  // angular separation, arcseconds

    // Conditions
    float  temp_c       = 0.0f;
    float  seeing_score = 0.0f;
    std::string object_name;
};

// Compute angular separation in arcseconds between two RA/Dec positions
inline double AngularSeparationArcsec(double ra1_h, double dec1_deg,
                                       double ra2_h, double dec2_deg) {
    const double DEG = 3.14159265358979323846 / 180.0;
    double ra1  = ra1_h  * 15.0 * DEG;
    double ra2  = ra2_h  * 15.0 * DEG;
    double dec1 = dec1_deg * DEG;
    double dec2 = dec2_deg * DEG;
    double cosd = sin(dec1)*sin(dec2) + cos(dec1)*cos(dec2)*cos(ra1-ra2);
    if (cosd >  1.0) cosd =  1.0;
    if (cosd < -1.0) cosd = -1.0;
    return acos(cosd) / DEG * 3600.0;
}

class PointingModel {
public:
    explicit PointingModel(const std::string& dataDir = raduk::core::Paths::Data())
        : m_dataDir(dataDir) {
        // Ensure data directory exists
        std::wstring wdir(dataDir.begin(), dataDir.end());
        CreateDirectoryW(wdir.c_str(), nullptr);
        m_csvPath = dataDir + "\\pointing_model.csv";
        LoadFromCSV();
    }

    // ── Record a new pointing error ────────────────────────────
    // Call this from the ASTAP solve callback (worker thread safe).
    // ra_expected/dec_expected = mount's reported position
    // ra_actual/dec_actual     = plate-solved position
    void Record(double ra_expected_h, double dec_expected_deg,
                double ra_actual_h,   double dec_actual_deg,
                double az_deg, double alt_deg,
                float temp_c, float seeing_score,
                const std::string& objectName) {
        PointingEntry e;
        e.timestamp    = CurrentTimestamp();
        e.az_deg       = az_deg;
        e.alt_deg      = alt_deg;
        e.ra_expected  = ra_expected_h;
        e.dec_expected = dec_expected_deg;
        e.ra_actual    = ra_actual_h;
        e.dec_actual   = dec_actual_deg;
        e.error_arcsec = AngularSeparationArcsec(ra_expected_h, dec_expected_deg,
                                                  ra_actual_h,   dec_actual_deg);
        e.temp_c       = temp_c;
        e.seeing_score = seeing_score;
        e.object_name  = objectName;

        printf("[PointingModel] Recorded: %s  error=%.1f\"  az=%.1f alt=%.1f\n",
               objectName.c_str(), e.error_arcsec, az_deg, alt_deg);

        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_entries.push_back(e);
            AppendCSV(e);
        }
    }

    // ── Session statistics ─────────────────────────────────────
    int   EntryCount()     const { std::lock_guard<std::mutex> lk(m_mutex); return (int)m_entries.size(); }
    int   SessionCount()   const { std::lock_guard<std::mutex> lk(m_mutex); return m_sessionCount; }

    // Mean pointing error across all loaded entries (arcseconds)
    double MeanError() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_entries.empty()) return 0.0;
        double sum = 0.0;
        for (auto& e : m_entries) sum += e.error_arcsec;
        return sum / m_entries.size();
    }

    // Best pointing error seen this session
    double BestSessionError() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_sessionEntries.empty()) return 0.0;
        double best = 1e9;
        for (auto& e : m_sessionEntries) if (e.error_arcsec < best) best = e.error_arcsec;
        return best;
    }

    // Last recorded error (arcseconds)
    double LastError() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_entries.empty()) return 0.0;
        return m_entries.back().error_arcsec;
    }

    // Last recorded object name
    std::string LastObject() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_entries.empty()) return "";
        return m_entries.back().object_name;
    }

    // Number of entries within RADIUS_DEG of az/alt — for GoTo pre-correction
    int NearbyCount(double az_deg, double alt_deg, double radiusDeg = 15.0) const {
        std::lock_guard<std::mutex> lk(m_mutex);
        int n = 0;
        for (auto& e : m_entries) {
            double daz  = e.az_deg  - az_deg;
            double dalt = e.alt_deg - alt_deg;
            if (sqrt(daz*daz + dalt*dalt) <= radiusDeg) n++;
        }
        return n;
    }

    // Estimated correction for a given Az/Alt using nearest neighbours
    // Returns false if not enough data (< 2 nearby points)
    bool GetCorrection(double az_deg, double alt_deg,
                       double& dRA_h, double& dDec_deg,
                       double radiusDeg = 20.0) const {
        std::lock_guard<std::mutex> lk(m_mutex);
        // Collect nearby entries
        std::vector<const PointingEntry*> nearby;
        for (auto& e : m_entries) {
            double daz  = e.az_deg  - az_deg;
            double dalt = e.alt_deg - alt_deg;
            if (sqrt(daz*daz + dalt*dalt) <= radiusDeg)
                nearby.push_back(&e);
        }
        if (nearby.size() < 2) return false;

        // Weighted mean correction — closer points get more weight
        double sumW = 0.0, sumDRA = 0.0, sumDDec = 0.0;
        for (auto* e : nearby) {
            double daz  = e->az_deg  - az_deg;
            double dalt = e->alt_deg - alt_deg;
            double dist = sqrt(daz*daz + dalt*dalt);
            if (dist < 0.001) dist = 0.001;
            double w = 1.0 / (dist * dist);  // inverse-distance-squared weighting
            sumW    += w;
            sumDRA  += w * (e->ra_actual  - e->ra_expected);
            sumDDec += w * (e->dec_actual - e->dec_expected);
        }
        dRA_h   = sumDRA  / sumW;
        dDec_deg = sumDDec / sumW;
        return true;
    }

    const std::string& CSVPath() const { return m_csvPath; }

private:
    void LoadFromCSV() {
        std::ifstream f(m_csvPath);
        if (!f.is_open()) {
            // First run — write header
            std::ofstream hdr(m_csvPath);
            if (hdr.is_open())
                hdr << "timestamp,az_deg,alt_deg,ra_expected_h,dec_expected_deg,"
                       "ra_actual_h,dec_actual_deg,error_arcsec,temp_c,"
                       "seeing_score,object_name\n";
            return;
        }

        std::string line;
        std::getline(f, line); // skip header
        int loaded = 0;
        while (std::getline(f, line)) {
            PointingEntry e;
            if (ParseCSVLine(line, e)) {
                m_entries.push_back(e);
                loaded++;
            }
        }
        printf("[PointingModel] Loaded %d historical entries from %s\n",
               loaded, m_csvPath.c_str());
    }

    void AppendCSV(const PointingEntry& e) {
        std::ofstream f(m_csvPath, std::ios::app);
        if (!f.is_open()) return;
        char line[512];
        sprintf_s(line,
            "%s,%.3f,%.3f,%.6f,%.6f,%.6f,%.6f,%.1f,%.1f,%.1f,%s\n",
            e.timestamp.c_str(),
            e.az_deg, e.alt_deg,
            e.ra_expected, e.dec_expected,
            e.ra_actual,   e.dec_actual,
            e.error_arcsec, e.temp_c, e.seeing_score,
            e.object_name.c_str());
        f << line;
        m_sessionCount++;
    }

    bool ParseCSVLine(const std::string& line, PointingEntry& e) {
        // Simple CSV parse (no quoted fields needed — no commas in values)
        const char* p = line.c_str();
        auto nextField = [&]() -> std::string {
            const char* start = p;
            while (*p && *p != ',') p++;
            std::string s(start, p);
            if (*p == ',') p++;
            return s;
        };
        try {
            e.timestamp    = nextField();
            e.az_deg       = std::stod(nextField());
            e.alt_deg      = std::stod(nextField());
            e.ra_expected  = std::stod(nextField());
            e.dec_expected = std::stod(nextField());
            e.ra_actual    = std::stod(nextField());
            e.dec_actual   = std::stod(nextField());
            e.error_arcsec = std::stod(nextField());
            e.temp_c       = std::stof(nextField());
            e.seeing_score = std::stof(nextField());
            e.object_name  = nextField();
            return !e.timestamp.empty();
        } catch (...) { return false; }
    }

    static std::string CurrentTimestamp() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char buf[32];
        sprintf_s(buf, "%04d-%02d-%02dT%02d:%02d:%02d",
                  st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond);
        return buf;
    }

    std::string              m_dataDir;
    std::string              m_csvPath;
    mutable std::mutex       m_mutex;
    std::vector<PointingEntry> m_entries;          // all loaded entries
    std::vector<PointingEntry> m_sessionEntries;   // this session only
    int                      m_sessionCount = 0;
};

} // namespace raduk::sky
