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
// missing file yields the defaults below (site 0,0 — set your
// own coordinates in rig.ini; optics default to 8SE + ASI678MC
// at f/10, NexStar guide rate 1 ≈ 0.5x sidereal).
//
// NOTE: the nominal plate scale seeds UI overlays and solver
// hints only — the CentroidTracker's correction loop uses the
// MEASURED Phase 51 calibration, never this value.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX  // keep windows.h min/max macros away from std::min/max
#endif
#include <windows.h>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cmath>

namespace raduk::core {

struct RigConfig {
    // Observing site
    // 0,0 by default — deliberately not a real location. Set your
    // observing site in rig.ini (alt/az math and the planner need it).
    double siteLatDeg = 0.0;    // +N
    double siteLonDeg = 0.0;    // +E (negative = west)

    // Phase 66: 0,0 is both the default AND a legal coordinate (it is a
    // real spot in the Gulf of Guinea), so the two were indistinguishable
    // and an absent rig.ini looked exactly like a deliberate site on the
    // equator. Nothing could tell "unset" from "set to zero", and a wrong
    // site was pushed to the mount in silence. Track it explicitly.
    bool        siteValid  = false;
    // How the site was obtained: manual | windows | mount | gps.
    // An explicit "manual" entry must never be replaced by an estimate.
    std::string siteSource;
    // Metres above sea level. Feeds the ADC pressure model, which
    // otherwise derives pressure from a constant tied to one town.
    double      siteElevationM = 0.0;

    static constexpr double UNKNOWN_ELEV = -1.0e9;

    bool SiteIsSet() const { return siteValid; }

    // Known filters and the band each passes, nm. Edges are the
    // practical limits of the filter AND the sensor together - the
    // IMX678 still responds past 1000nm but weakly enough that it
    // contributes little to where the dispersion extremes sit.
    struct FilterPreset { const char* name; float lowNm; float highNm; };
    static const FilterPreset* Presets(int& count) {
        static const FilterPreset kP[] = {
            { "UV/IR cut",     400.0f,  700.0f },
            { "None",          380.0f, 1000.0f },
            { "IR pass 685",   685.0f, 1000.0f },
            { "Methane 889",   880.0f,  900.0f },
        };
        count = (int)(sizeof(kP)/sizeof(kP[0]));
        return kP;
    }
    // Set the band from a preset name. Returns false and leaves the
    // band alone if the name is unknown, so a hand-edited rig.ini
    // with custom edges is not silently overwritten.
    bool ApplyFilterPreset(const std::string& name) {
        int n = 0; const FilterPreset* p = Presets(n);
        for (int i = 0; i < n; i++) {
            if (_stricmp(name.c_str(), p[i].name) == 0) {
                filterName   = p[i].name;
                filterLowNm  = p[i].lowNm;
                filterHighNm = p[i].highNm;
                return true;
            }
        }
        return false;
    }

    // Phase 66: accept a site derived at runtime (mount, Windows
    // Location, GPS). Refuses to overwrite one the user set by hand --
    // a surveyed coordinate must not be replaced by a WiFi estimate.
    // Returns true if the value was taken. Persists so the next launch
    // starts from it rather than rediscovering.
    bool AdoptSite(double latDeg, double lonDeg, double elevM,
                   const char* source) {
        if (siteValid && siteSource == "manual") {
            printf("[RigConfig] Keeping the manual site; ignoring %s\n", source);
            return false;
        }
        if (latDeg < -90 || latDeg > 90 || lonDeg < -180 || lonDeg > 180)
            return false;
        siteLatDeg     = latDeg;
        siteLonDeg     = lonDeg;
        // Pass UNKNOWN_ELEV when the source has no vertical fix. A
        // WiFi position reports altitude 0.0, and treating that as sea
        // level would overwrite a correct elevation and skew the ADC
        // pressure model.
        if (elevM != UNKNOWN_ELEV && elevM > -500.0 && elevM < 9000.0)
            siteElevationM = elevM;
        siteSource     = source;
        siteValid      = true;
        printf("[RigConfig] Site adopted from %s: %.1f%c %.1f%c\n", source,
               latDeg < 0 ? -latDeg : latDeg, latDeg < 0 ? 'S' : 'N',
               lonDeg < 0 ? -lonDeg : lonDeg, lonDeg < 0 ? 'W' : 'E');
        Save();
        return true;
    }

    // Great-circle distance in km to another site — used to decide
    // whether two sources genuinely disagree or merely differ in
    // precision.
    double DistanceKmTo(double latDeg, double lonDeg) const {
        constexpr double R = 6371.0, D2R = 3.14159265358979 / 180.0;
        double dLat = (latDeg - siteLatDeg) * D2R;
        double dLon = (lonDeg - siteLonDeg) * D2R;
        double a = std::sin(dLat/2) * std::sin(dLat/2)
                 + std::cos(siteLatDeg * D2R) * std::cos(latDeg * D2R)
                 * std::sin(dLon/2) * std::sin(dLon/2);
        return 2.0 * R * std::asin(std::sqrt(a < 0 ? 0 : (a > 1 ? 1 : a)));
    }

    // Optics / sensor (nominal)
    float  plateScaleArcsecPx = 0.203f;  // 8SE 2032mm + 2.0um pixels
    // Phase 108: sets the resolution a run can lose to rotation.
    float  apertureMm = 203.0f;
    // Phase 77: hand each finished recording to AutoStakkert
    // automatically. Off by default - stacking is minutes of CPU and
    // should be opted into, not sprung on someone mid-session.
    // ── Optical bandpass (Phase 82) ───────────────────────────
    //
    // Which filter is physically on the camera, and the band it
    // passes. This is not bookkeeping: ADCAssistant computes
    // atmospheric dispersion between the band edges, and it used to
    // hardcode 450-700nm - which IS a UV/IR cut passband. So the
    // dispersion model silently assumed the filter was fitted. Take
    // it off and the IMX678 sees past 1000nm, the real extremes are
    // much further apart, and the ADC advice is understated in the
    // unsafe direction.
    //
    // Persisted, unlike the readiness check's manual ticks. A dust
    // cap being off last night says nothing about tonight; a filter
    // stays on the camera until it is deliberately changed.
    std::string filterName   = "UV/IR cut";
    float       filterLowNm  = 400.0f;
    float       filterHighNm = 700.0f;

    // "ser" or "fits". SER is the default: it is what the stacking
    // chain reads.
    std::string captureFormat = "ser";

    // Phase 107: move the ROI to hold a locked target.
    bool        roiFollow = false;

    // Phase 108: stop at the derotation window instead of only warning,
    // and rename finished Lights for WinJUPOS.
    bool        derotationStop = false;
    bool        winJuposNames  = true;

    bool        autoStackOnStop = false;
    std::string autoStakkertPath;       // blank = probe the usual places

    // Mount guide rate behind PulseGuide, on-sky arcsec/second
    float  guideRateArcsecPerSec = 7.5f;

    // Optional pin for the NexStar hand control's serial port, e.g.
    // "COM4". Empty (the default) means auto-detect: SerialPortScan
    // probes the ports Windows reports as present and keeps the one
    // that answers the NexStar echo. Set this only to override a
    // scan that picks the wrong device; a stale value falls back to
    // auto-detect rather than failing the connect.
    // Phase 67: degrees of cumulative azimuth travel before the cord-wrap
    // warning fires. Depends on cable length and how they are dressed, so
    // it is per-rig rather than a constant.
    // Phase 67 step 4: bearing FROM the mount TO where the observer
    // stands, degrees true. One number, because everything the align
    // helper says is relative to it -- and it is a question answerable
    // in the dark, unlike "which way is north".
    // -1 means unasked; 0 is a legal bearing (due north).
    double observerBearingDeg = -1.0;
    bool ObserverBearingSet() const { return observerBearingDeg >= 0.0; }
    // Set by "Don't show again" on the align helper.
    bool showAlignHelper = true;
    // Phase 68: the readiness dialog appears only on a failure. This
    // turns even that off, because a tool that cannot be silenced is
    // one people learn to resent.
    bool showReadinessCheck = true;

    // Phase 26 step 2: night vision survives a restart. Its own spec
    // asked for this and it was never stored, so the one setting whose
    // whole purpose is protecting dark adaptation had to be switched on
    // again every time the app was reopened -- in the dark, which is
    // exactly when a bright screen does the damage.
    bool nightVision = false;
    // Preview overlay telemetry (histogram / temperature / sharpness
    // drawn on the image). Remembered, because on a multi-screen desk
    // it gets turned off once and should stay off.
    bool overlayTelemetry = true;

    // Is an atmospheric dispersion corrector actually in the imaging
    // train? Default NO, because most trains do not have one and a
    // readout about a part you do not own is noise at best and, in the
    // case of the alignment tool, a number inviting you to adjust
    // something that is not there. Ticked on the readiness dialog when
    // one is fitted. Everything ADC -- the preview arrow, the FOCUSER
    // readout and the Shift+A alignment window -- keys off this.
    bool adcFitted = false;

    // Phase 12b: unprompted advice from the local LLM. Off by default --
    // it costs GPU time and not everyone wants a running commentary.
    bool advisoryEnabled = false;
    int  advisoryIntervalS = 180;

    // ── Last session's camera state ───────────────────────────
    // Phase 88. Startup used to write hardcoded values over whatever
    // the rig was last doing: gain 100, 8 ms, full frame, 8-bit. A
    // night's tuning survived only if it had been saved into a profile
    // first, and the capture format did not survive at all. These hold
    // the live state instead, so a relaunch comes back where you left.
    //
    // -1 / 0 means "never stored", and then the old defaults apply, so
    // a first run and an upgrade both behave as before.
    int   lastRoiWidth   = 0;
    int   lastRoiHeight  = 0;
    int   lastBitDepth   = 0;
    int   lastGain       = -1;
    float lastExposureMs = -1.0f;
    int   lastGamma      = -1;
    int   lastWbRed      = -1;
    int   lastWbBlue     = -1;
    int   lastUsb        = -1;
    bool  HasLastCamera() const { return lastGain >= 0; }
    bool  HasLastFormat() const { return lastRoiWidth > 0 && lastRoiHeight > 0; }

    // Phase 67 step 6: an obstruction sector in azimuth - a house, a
    // wall, a tree. Degrees true, running CLOCKWISE from start to end.
    // Equal values mean no arc, which is the default: most sites do
    // not need one and an accidental sector would be worse than none.
    double keepOutStartDeg = 0.0;
    double keepOutEndDeg   = 0.0;
    bool KeepOutSet() const { return keepOutStartDeg != keepOutEndDeg; }

    double wrapLimitDeg = 360.0;

    std::string mountPort;

    // Pin an exact ASCOM telescope ProgID, skipping discovery.
    // Empty means discover, which is what it has always done.
    //
    // This exists because discovery refuses a set of drivers by
    // name, and two of the reasons are about this machine rather
    // than about the drivers: ASIMount grabs COM3 here, DeviceHub
    // was misconfigured here. Both are right for this rig and
    // neither is a fact about ASCOM. Naming a ProgID is not
    // discovery, so the refusal list does not apply to it: set
    // this to ASCOM.Simulator.Telescope to exercise the generic
    // path with no mount on the desk, or to ASIMount on a rig that
    // actually has one.
    std::string ascomProgId;

    // P5.6: the WebSocket server's shared secret. 64 hex characters,
    // generated on first run by main.cpp. Deliberately NOT written by
    // Save(): it has one writer, SaveWsToken(), so saving the rest of
    // the rig can never change or clear it. Never log it.
    std::string wsToken;

    void SaveRoiFollow() const {
        WritePrivateProfileStringA("Rig", "RoiFollow", roiFollow ? "1" : "0",
                                   IniPath().c_str());
    }

    void SaveDerotationStop() const {
        WritePrivateProfileStringA("Rig", "DerotationStop",
                                   derotationStop ? "1" : "0",
                                   IniPath().c_str());
    }

    void SaveCaptureFormat() const {
        WritePrivateProfileStringA("Rig", "CaptureFormat", captureFormat.c_str(),
                                   IniPath().c_str());
    }

    void SaveWsToken() const {
        WritePrivateProfileStringA("Rig", "WsToken", wsToken.c_str(), IniPath().c_str());
    }

    // The home network (owner's decision, 2026-10-25). Hand-set in
    // rig.ini and, like WsToken, never written by Save().
    //   WsLan=1      listen on the home network, not only this PC.
    //                RADUK_WS_LAN=1 in the environment still does it too.
    //   WsLanOpen=1  a client on a PRIVATE network address needs no
    //                token. Loopback and public addresses still do --
    //                ws::PeerExemptFromToken is the whole rule.
    bool wsLan     = false;
    bool wsLanOpen = false;

    // Leoben, the owner's assistant on another PC, as a source of
    // weather and sky conditions. Empty URL = off. The token is
    // Leoben's LEOBEN_API_TOKEN; never log it.
    //   LeobenUrl=http://elminster:5000
    //   LeobenToken=<Leoben's API token>
    std::string leobenUrl;
    std::string leobenToken;

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
        auto ws = [&](const char* key, const std::string& v) {
            WritePrivateProfileStringA("Rig", key, v.c_str(), p.c_str());
        };
        wf("SiteLatDeg", siteLatDeg);
        wf("SiteLonDeg", siteLonDeg);
        wf("PlateScaleArcsecPx", plateScaleArcsecPx);
        ws("FilterName", filterName);
        wf("FilterLowNm",  filterLowNm);
        wf("FilterHighNm", filterHighNm);
        ws("CaptureFormat", captureFormat);
        ws("RoiFollow", roiFollow ? "1" : "0");
        ws("DerotationStop", derotationStop ? "1" : "0");
        ws("WinJuposNames", winJuposNames ? "1" : "0");
        wf("ApertureMm", apertureMm);
        ws("AutoStackOnStop", autoStackOnStop ? "1" : "0");
        ws("AutoStakkertPath", autoStakkertPath);
        wf("GuideRateArcsecPerSec", guideRateArcsecPerSec);
        wf("SiteElevationM", siteElevationM);
        wf("WrapLimitDeg", wrapLimitDeg);
        wf("KeepOutStartDeg", keepOutStartDeg);
        wf("KeepOutEndDeg",   keepOutEndDeg);
        wf("ObserverBearingDeg", observerBearingDeg);
        wf("ShowAlignHelper", showAlignHelper ? 1.0 : 0.0);
        wf("ShowReadinessCheck", showReadinessCheck ? 1.0 : 0.0);
        wf("NightVision", nightVision ? 1.0 : 0.0);
        wf("OverlayTelemetry", overlayTelemetry ? 1.0 : 0.0);
        wf("ADCFitted", adcFitted ? 1.0 : 0.0);
        wf("AdvisoryEnabled",   advisoryEnabled ? 1.0 : 0.0);
        wf("AdvisoryIntervalS", (double)advisoryIntervalS);
        wf("LastRoiWidth",   (double)lastRoiWidth);
        wf("LastRoiHeight",  (double)lastRoiHeight);
        wf("LastBitDepth",   (double)lastBitDepth);
        wf("LastGain",       (double)lastGain);
        wf("LastExposureMs", (double)lastExposureMs);
        wf("LastGamma",      (double)lastGamma);
        wf("LastWbRed",      (double)lastWbRed);
        wf("LastWbBlue",     (double)lastWbBlue);
        wf("LastUsb",        (double)lastUsb);
        WritePrivateProfileStringA("Rig", "SiteSource",
                                   siteSource.empty() ? "manual" : siteSource.c_str(),
                                   p.c_str());
        WritePrivateProfileStringA("Rig", "MountPort",
                                   mountPort.c_str(), p.c_str());
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
        auto rs = [&](const char* key, const std::string& def) -> std::string {
            char buf[512] = {};
            GetPrivateProfileStringA("Rig", key, "", buf, sizeof(buf), p.c_str());
            return buf[0] ? std::string(buf) : def;
        };
        auto rf = [&](const char* key, double def) -> double {
            char buf[64] = {};
            GetPrivateProfileStringA("Rig", key, "", buf, sizeof(buf), p.c_str());
            if (!buf[0]) return def;
            char* end = nullptr;
            double v = strtod(buf, &end);
            return (end && end != buf) ? v : def;
        };
        // Phase 66: presence, not value. A key that is absent means the
        // site is unknown; a key reading 0.0 means someone chose the
        // equator. Conflating them is what let a wrong site reach the
        // mount silently.
        auto present = [&](const char* key) -> bool {
            char buf[64] = {};
            GetPrivateProfileStringA("Rig", key, "", buf, sizeof(buf), p.c_str());
            return buf[0] != 0;
        };
        bool haveLat = present("SiteLatDeg");
        bool haveLon = present("SiteLonDeg");

        cfg.siteLatDeg            = rf("SiteLatDeg", cfg.siteLatDeg);
        cfg.siteLonDeg            = rf("SiteLonDeg", cfg.siteLonDeg);
        cfg.siteValid             = haveLat && haveLon;
        cfg.siteElevationM        = rf("SiteElevationM", cfg.siteElevationM);
        cfg.wrapLimitDeg          = rf("WrapLimitDeg", cfg.wrapLimitDeg);
        cfg.keepOutStartDeg       = rf("KeepOutStartDeg", cfg.keepOutStartDeg);
        cfg.keepOutEndDeg         = rf("KeepOutEndDeg",   cfg.keepOutEndDeg);
        cfg.observerBearingDeg    = rf("ObserverBearingDeg", cfg.observerBearingDeg);
        cfg.showAlignHelper       = rf("ShowAlignHelper", 1.0) != 0.0;
        cfg.showReadinessCheck    = rf("ShowReadinessCheck", 1.0) != 0.0;
        cfg.nightVision           = rf("NightVision", 0.0) != 0.0;
        cfg.overlayTelemetry      = rf("OverlayTelemetry", 1.0) != 0.0;
        cfg.adcFitted             = rf("ADCFitted", 0.0) != 0.0;
        cfg.advisoryEnabled       = rf("AdvisoryEnabled", 0.0) != 0.0;
        cfg.advisoryIntervalS     = (int)rf("AdvisoryIntervalS", 180.0);
        if (cfg.advisoryIntervalS < 30 || cfg.advisoryIntervalS > 3600)
            cfg.advisoryIntervalS = 180;
        cfg.lastRoiWidth          = (int)rf("LastRoiWidth",   0.0);
        cfg.lastRoiHeight         = (int)rf("LastRoiHeight",  0.0);
        cfg.lastBitDepth          = (int)rf("LastBitDepth",   0.0);
        cfg.lastGain              = (int)rf("LastGain",      -1.0);
        cfg.lastExposureMs        = (float)rf("LastExposureMs", -1.0);
        cfg.lastGamma             = (int)rf("LastGamma",     -1.0);
        cfg.lastWbRed             = (int)rf("LastWbRed",     -1.0);
        cfg.lastWbBlue            = (int)rf("LastWbBlue",    -1.0);
        cfg.lastUsb               = (int)rf("LastUsb",       -1.0);

        // A stored camera value outside its usable range must not be
        // applied. The emulator discards USB bandwidth and answers 0 for
        // it, so without this a session saved on the bench would restore
        // bandwidth 0 to the real camera and stop it streaming. Each
        // field is checked on its own: one bad number should not discard
        // the rest of a good session.
        auto bad = [](int v, int lo, int hi) { return v < lo || v > hi; };
        if (bad(cfg.lastGain,   0,  600))  cfg.lastGain   = -1;
        if (bad(cfg.lastGamma,  1,  100))  cfg.lastGamma  = -1;
        if (bad(cfg.lastWbRed,  1,   99))  cfg.lastWbRed  = -1;
        if (bad(cfg.lastWbBlue, 1,   99))  cfg.lastWbBlue = -1;
        if (bad(cfg.lastUsb,   40,  100))  cfg.lastUsb    = -1;
        if (cfg.lastExposureMs <= 0.0f || cfg.lastExposureMs > 600000.0f)
            cfg.lastExposureMs = -1.0f;
        // The format is checked against the real sensor by the caller,
        // which reads back what the camera granted; only nonsense is
        // rejected here.
        if (cfg.lastRoiWidth  < 8 || cfg.lastRoiWidth  > 16384 ||
            cfg.lastRoiHeight < 2 || cfg.lastRoiHeight > 16384) {
            cfg.lastRoiWidth = cfg.lastRoiHeight = 0;
        }
        if (cfg.lastBitDepth != 8 && cfg.lastBitDepth != 16)
            cfg.lastBitDepth = 0;
        {
            char sbuf[32] = {};
            GetPrivateProfileStringA("Rig", "SiteSource", "", sbuf, sizeof(sbuf), p.c_str());
            cfg.siteSource = sbuf[0] ? sbuf : (cfg.siteValid ? "manual" : "");
        }
        cfg.plateScaleArcsecPx    = (float)rf("PlateScaleArcsecPx", cfg.plateScaleArcsecPx);
        cfg.filterName            = rs("FilterName", cfg.filterName);
        cfg.filterLowNm           = (float)rf("FilterLowNm",  cfg.filterLowNm);
        cfg.filterHighNm          = (float)rf("FilterHighNm", cfg.filterHighNm);
        cfg.captureFormat         = rs("CaptureFormat", cfg.captureFormat);
        cfg.roiFollow             = rs("RoiFollow", cfg.roiFollow ? "1" : "0") == "1";
        cfg.derotationStop        = rs("DerotationStop", cfg.derotationStop ? "1" : "0") == "1";
        cfg.winJuposNames         = rs("WinJuposNames", cfg.winJuposNames ? "1" : "0") == "1";
        cfg.apertureMm            = (float)rf("ApertureMm", cfg.apertureMm);
        cfg.autoStackOnStop       = rs("AutoStackOnStop", cfg.autoStackOnStop ? "1" : "0") == "1";
        cfg.autoStakkertPath      = rs("AutoStakkertPath", cfg.autoStakkertPath);
        cfg.guideRateArcsecPerSec = (float)rf("GuideRateArcsecPerSec", cfg.guideRateArcsecPerSec);

        // Optional explicit mount port. Read as a raw string: blank
        // means auto-detect, which is the normal case.
        {
            char pbuf[32] = {};
            GetPrivateProfileStringA("Rig", "MountPort", "", pbuf, sizeof(pbuf), p.c_str());
            cfg.mountPort = pbuf;
        cfg.ascomProgId = rs("AscomProgId", "");
        }
        cfg.wsToken = rs("WsToken", "");
        cfg.wsLan       = rs("WsLan", "0") == "1";
        cfg.wsLanOpen   = rs("WsLanOpen", "0") == "1";
        cfg.leobenUrl   = rs("LeobenUrl", "");
        cfg.leobenToken = rs("LeobenToken", "");

        // Sanity clamps — a corrupted INI must not produce absurd pulse
        // sizing or site math
        // An out-of-range site is corrupt, not zero. Clamping it to 0,0
        // would manufacture exactly the plausible-looking wrong site this
        // phase exists to prevent.
        if (cfg.siteLatDeg < -90 || cfg.siteLatDeg > 90 ||
            cfg.siteLonDeg < -180 || cfg.siteLonDeg > 180) {
            printf("[RigConfig] Site out of range (%.4f,%.4f) - treating as unset\n",
                   cfg.siteLatDeg, cfg.siteLonDeg);
            cfg.siteLatDeg = 0.0;
            cfg.siteLonDeg = 0.0;
            cfg.siteValid  = false;
        }
        if (cfg.plateScaleArcsecPx < 0.01f || cfg.plateScaleArcsecPx > 20.0f)
            cfg.plateScaleArcsecPx = 0.203f;
        if (cfg.guideRateArcsecPerSec < 0.5f || cfg.guideRateArcsecPerSec > 60.0f)
            cfg.guideRateArcsecPerSec = 7.5f;
        // A nonsense band would feed the Edlen expansion values outside
        // its domain and produce a dispersion figure that looks
        // authoritative and is meaningless. Fall back to the UV/IR cut
        // passband, which is what the model assumed before it was
        // configurable at all.
        if (cfg.filterLowNm  < 300.0f || cfg.filterLowNm  > 1200.0f ||
            cfg.filterHighNm < 300.0f || cfg.filterHighNm > 1200.0f ||
            cfg.filterHighNm <= cfg.filterLowNm + 10.0f) {
            cfg.filterName   = "UV/IR cut";
            cfg.filterLowNm  = 400.0f;
            cfg.filterHighNm = 700.0f;
        }

        // Phase 66: cross-check longitude against the OS timezone. The UTC
        // offset implies a longitude band (offset x 15 deg); real borders
        // are irregular, so the tolerance is deliberately loose and only a
        // gross mismatch trips it. This is the check that would have caught
        // a 0,0 site instantly: UTC-7 implies about -105 deg, so 0.0 sits
        // 105 deg away while a genuine -112.09 is only 7 deg away.
        if (cfg.siteValid) {
            // _get_timezone returns the CRT's default (PST) until the
            // timezone is initialised, so a process that has not yet
            // called a localtime function reads the wrong meridian.
            // Observed: this machine is US Mountain Standard Time
            // (UTC-7) but reported UTC-8 without this call.
            _tzset();
            long tzSec = 0;
            _get_timezone(&tzSec);
            double tzCentreLon = -(double)tzSec / 3600.0 * 15.0;
            double delta = cfg.siteLonDeg - tzCentreLon;
            while (delta >  180.0) delta -= 360.0;
            while (delta < -180.0) delta += 360.0;
            if (delta < 0) delta = -delta;
            if (delta > 20.0) {
                printf("[RigConfig] WARNING: longitude %.2f is %.0f deg from the "
                       "timezone centre %.0f - the site or the clock is wrong\n",
                       cfg.siteLonDeg, delta, tzCentreLon);
            }
        }

        // Report coarsely. Full precision in a startup log ends up in
        // screenshots; one decimal is about 11 km, which names a town
        // rather than a house, and is plenty to confirm the right site.
        if (cfg.siteValid) {
            printf("[RigConfig] site=%.1f%c %.1f%c (%s) elev=%.0fm\n",
                   cfg.siteLatDeg < 0 ? -cfg.siteLatDeg : cfg.siteLatDeg,
                   cfg.siteLatDeg < 0 ? 'S' : 'N',
                   cfg.siteLonDeg < 0 ? -cfg.siteLonDeg : cfg.siteLonDeg,
                   cfg.siteLonDeg < 0 ? 'W' : 'E',
                   cfg.siteSource.empty() ? "manual" : cfg.siteSource.c_str(),
                   cfg.siteElevationM);
        } else {
            printf("[RigConfig] SITE NOT SET - alt/az tracking, the sky map and "
                   "the ADC model all need one. Set SiteLatDeg and SiteLonDeg "
                   "in %s\n", p.c_str());
        }
        printf("[RigConfig] scale=%.3f\"/px guide=%.1f\"/s port=%s (%s)\n",
               cfg.plateScaleArcsecPx, cfg.guideRateArcsecPerSec,
               cfg.mountPort.empty() ? "auto" : cfg.mountPort.c_str(), p.c_str());
        return cfg;
    }
};

} // namespace raduk::core
