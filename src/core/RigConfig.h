#pragma once
// ============================================================
// RigConfig.h — Phase 55: per-rig configuration
//
// Single source of truth for the constants that were previously
// scattered as literals (review T17): observing site, nominal
// plate scale, and guide rate. A Barlow / bin / site change is
// now one INI edit instead of a source hunt — and the tracking
// chain (pulse sizing, deadbands, ASTAP FOV hint) stays in sync.
//
// Persisted as rig.ini next to the executable, same Win32
// GetPrivateProfileString mechanism ProfileManager uses. A
// missing file yields the defaults below (Anthem AZ, 8SE +
// ASI678MC at f/10, NexStar guide rate 1 ≈ 0.5x sidereal).
//
// NOTE: the nominal plate scale seeds UI overlays and solver
// hints only — the CentroidTracker's correction loop uses the
// MEASURED Phase 51 calibration, never this value.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <cstdio>

namespace raduk::core {

struct RigConfig {
    // Observing site
    double siteLatDeg = 33.86;    // +N
    double siteLonDeg = -112.09;  // +E (negative = west)

    // Optics / sensor (nominal)
    float  plateScaleArcsecPx = 0.203f;  // 8SE 2032mm + 2.0um pixels

    // Mount guide rate behind PulseGuide, on-sky arcsec/second
    float  guideRateArcsecPerSec = 7.5f;

    // Singleton — loads rig.ini on first access
    static RigConfig& Get() {
        static RigConfig cfg = LoadFromDisk();
        return cfg;
    }

    void Save() const {
        std::string p = IniPath();
        auto wf = [&](const char* key, double v) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.6f", v);
            WritePrivateProfileStringA("Rig", key, buf, p.c_str());
        };
        wf("SiteLatDeg", siteLatDeg);
        wf("SiteLonDeg", siteLonDeg);
        wf("PlateScaleArcsecPx", plateScaleArcsecPx);
        wf("GuideRateArcsecPerSec", guideRateArcsecPerSec);
        printf("[RigConfig] Saved %s\n", p.c_str());
    }

private:
    static std::string IniPath() {
        char exe[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        std::string p(exe);
        size_t slash = p.find_last_of("\\/");
        if (slash != std::string::npos) p.resize(slash + 1);
        return p + "rig.ini";
    }

    static RigConfig LoadFromDisk() {
        RigConfig cfg;
        std::string p = IniPath();
        auto rf = [&](const char* key, double def) -> double {
            char buf[64] = {};
            GetPrivateProfileStringA("Rig", key, "", buf, sizeof(buf), p.c_str());
            if (!buf[0]) return def;
            char* end = nullptr;
            double v = strtod(buf, &end);
            return (end && end != buf) ? v : def;
        };
        cfg.siteLatDeg            = rf("SiteLatDeg", cfg.siteLatDeg);
        cfg.siteLonDeg            = rf("SiteLonDeg", cfg.siteLonDeg);
        cfg.plateScaleArcsecPx    = (float)rf("PlateScaleArcsecPx", cfg.plateScaleArcsecPx);
        cfg.guideRateArcsecPerSec = (float)rf("GuideRateArcsecPerSec", cfg.guideRateArcsecPerSec);

        // Sanity clamps — a corrupted INI must not produce absurd pulse
        // sizing or site math
        if (cfg.siteLatDeg < -90 || cfg.siteLatDeg > 90)    cfg.siteLatDeg = 33.86;
        if (cfg.siteLonDeg < -180 || cfg.siteLonDeg > 180)  cfg.siteLonDeg = -112.09;
        if (cfg.plateScaleArcsecPx < 0.01f || cfg.plateScaleArcsecPx > 20.0f)
            cfg.plateScaleArcsecPx = 0.203f;
        if (cfg.guideRateArcsecPerSec < 0.5f || cfg.guideRateArcsecPerSec > 60.0f)
            cfg.guideRateArcsecPerSec = 7.5f;

        printf("[RigConfig] site=(%.4f,%.4f) scale=%.3f\"/px guide=%.1f\"/s (%s)\n",
               cfg.siteLatDeg, cfg.siteLonDeg,
               cfg.plateScaleArcsecPx, cfg.guideRateArcsecPerSec, p.c_str());
        return cfg;
    }
};

} // namespace raduk::core
