#pragma once
// ============================================================
// ProfileManager.h — Load/save target profiles as INI files
// Milestone 5 — pure Win32 INI via GetPrivateProfileString /
// WritePrivateProfileString. No external dependencies.
// ============================================================

#include "TargetProfile.h"
#include <string>
#include <vector>
#include <windows.h>
#include <cstdio>

namespace raduk::profiles {

class ProfileManager {
public:
    ProfileManager() = default;

    // Initialize — sets the profiles directory and loads all profiles.
    // Creates default profiles if directory is empty.
    void Init(const std::string& profilesDir = "D:\\RadukCapture\\profiles") {
        m_dir = profilesDir;
        CreateDirectoryA(m_dir.c_str(), nullptr);
        LoadAll();
    }

    // All loaded profiles (const)
    const std::vector<TargetProfile>& Profiles() const { return m_profiles; }
    // Mutable access by index — for writing notes/fields back before save
    TargetProfile* ProfileAt(int index) {
        if (index < 0 || index >= (int)m_profiles.size()) return nullptr;
        return &m_profiles[index];
    }

    // Find by name (case-insensitive). Returns nullptr if not found.
    const TargetProfile* Find(const std::string& name) const {
        for (const auto& p : m_profiles) {
            if (_stricmp(p.name.c_str(), name.c_str()) == 0)
                return &p;
        }
        return nullptr;
    }

    // Index of currently active profile (-1 if none)
    int ActiveIndex() const { return m_activeIndex; }

    // Set active profile by index
    void SetActive(int index) {
        if (index >= 0 && index < (int)m_profiles.size())
            m_activeIndex = index;
    }

    const TargetProfile* Active() const {
        if (m_activeIndex < 0 || m_activeIndex >= (int)m_profiles.size())
            return nullptr;
        return &m_profiles[m_activeIndex];
    }

    // Save a profile to its INI file
    bool Save(const TargetProfile& profile) {
        std::string path = m_dir + "\\" + profile.filename;
        const char* p = path.c_str();

        auto WS = [&](const char* sec, const char* key, const std::string& val) {
            WritePrivateProfileStringA(sec, key, val.c_str(), p);
        };
        auto WI = [&](const char* sec, const char* key, int val) {
            char buf[32]; sprintf_s(buf, "%d", val);
            WritePrivateProfileStringA(sec, key, buf, p);
        };
        auto WF = [&](const char* sec, const char* key, double val) {
            char buf[32]; sprintf_s(buf, "%.4f", val);
            WritePrivateProfileStringA(sec, key, buf, p);
        };
        auto WB = [&](const char* sec, const char* key, bool val) {
            WritePrivateProfileStringA(sec, key, val ? "true" : "false", p);
        };

        WS("profile", "name", profile.name);

        WI("camera",  "gain",          profile.gain);
        WF("camera",  "exposure_ms",   profile.exposureMs);
        WI("camera",  "gamma",         profile.gamma);
        WI("camera",  "wb_red",        profile.wbRed);
        WI("camera",  "wb_blue",       profile.wbBlue);
        WI("camera",  "usb_bandwidth", profile.usbBandwidth);
        WB("camera",  "high_speed",    profile.highSpeed);

        WI("roi",     "width",         profile.roiWidth);
        WI("roi",     "height",        profile.roiHeight);
        WI("roi",     "binning",       profile.binning);
        WI("roi",     "img_type",      (int)profile.imgType);

        WI("focuser", "position",      profile.focusPosition);
        WI("focuser", "step_coarse",   profile.stepCoarse);
        WI("focuser", "step_fine",     profile.stepFine);

        WS("mount",   "tracking_mode", profile.trackingMode);
        WI("mount",   "slew_rate",     profile.slewRate);
        WS("mount",   "goto_target",   profile.gotoTarget);

        WS("capture", "format",        profile.captureFormat);
        WS("capture", "limit_type",    profile.limitType);
        WI("capture", "limit_value",   profile.limitValue);
        WS("capture", "file_prefix",   profile.filePrefix);

        WS("notes",   "text",          profile.notes);

        printf("[ProfileManager] Saved: %s\n", path.c_str());
        return true;
    }

    // Load a single profile from an INI file path
    bool Load(const std::string& path, TargetProfile& out) {
        const char* p = path.c_str();

        auto RI = [&](const char* sec, const char* key, int def) -> int {
            return (int)GetPrivateProfileIntA(sec, key, def, p);
        };
        auto RS = [&](const char* sec, const char* key, const char* def) -> std::string {
            char buf[512]; buf[0] = 0;
            GetPrivateProfileStringA(sec, key, def, buf, sizeof(buf), p);
            return std::string(buf);
        };
        auto RF = [&](const char* sec, const char* key, double def) -> double {
            char buf[32]; sprintf_s(buf, "%.4f", def);
            char result[32];
            GetPrivateProfileStringA(sec, key, buf, result, sizeof(result), p);
            return atof(result);
        };
        auto RB = [&](const char* sec, const char* key, bool def) -> bool {
            std::string v = RS(sec, key, def ? "true" : "false");
            return (v == "true" || v == "1" || v == "yes");
        };

        out.name         = RS("profile", "name", "Unknown");

        out.gain         = RI("camera",  "gain",          100);
        out.exposureMs   = RF("camera",  "exposure_ms",   8.0);
        out.gamma        = RI("camera",  "gamma",         50);
        out.wbRed        = RI("camera",  "wb_red",        52);
        out.wbBlue       = RI("camera",  "wb_blue",       47);
        out.usbBandwidth = RI("camera",  "usb_bandwidth", 80);
        out.highSpeed    = RB("camera",  "high_speed",    true);

        out.roiWidth     = RI("roi",     "width",         1280);
        out.roiHeight    = RI("roi",     "height",        960);
        out.binning      = RI("roi",     "binning",       1);
        out.imgType      = (ASI_IMG_TYPE)RI("roi", "img_type", (int)ASI_IMG_RAW8);

        out.focusPosition = RI("focuser", "position",    50000);
        out.stepCoarse    = RI("focuser", "step_coarse", 100);
        out.stepFine      = RI("focuser", "step_fine",   10);

        out.trackingMode = RS("mount",   "tracking_mode", "alt_az");
        out.slewRate     = RI("mount",   "slew_rate",     9);
        out.gotoTarget   = RS("mount",   "goto_target",   "");

        out.captureFormat = RS("capture", "format",      "SER");
        out.limitType     = RS("capture", "limit_type",  "frames");
        out.limitValue    = RI("capture", "limit_value", 3000);
        out.filePrefix    = RS("capture", "file_prefix", "");

        out.notes = RS("notes", "text", "");

        // Extract filename from path
        size_t slash = path.find_last_of("\\/");
        out.filename = (slash != std::string::npos) ? path.substr(slash + 1) : path;

        return true;
    }

    // Reload all profiles from disk
    void LoadAll() {
        m_profiles.clear();

        // Standard profiles — loaded in order shown in ControlPanel dropdown
        static const char* defaults[] = {
            "jupiter.ini", "saturn.ini",      "mars.ini",
            "venus.ini",   "moon.ini",        "dso.ini",
            "star.ini",    "planet_focus.ini"
        };

        static TargetProfile (*factories[])() = {
            DefaultJupiter, DefaultSaturn,      DefaultMars,
            DefaultVenus,   DefaultMoon,        DefaultDSO,
            DefaultStar,    DefaultPlanetFocus
        };

        for (int i = 0; i < 8; i++) {
            std::string path = m_dir + "\\" + defaults[i];
            TargetProfile profile = factories[i](); // start with defaults

            // If file exists, load it (overrides defaults)
            FILE* f = nullptr;
            fopen_s(&f, path.c_str(), "r");
            if (f) {
                fclose(f);
                Load(path, profile);
            } else {
                // File doesn't exist — write the defaults
                profile.filename = defaults[i];
                Save(profile);
            }

            m_profiles.push_back(profile);
        }

        printf("[ProfileManager] Loaded %d profiles from %s\n",
               (int)m_profiles.size(), m_dir.c_str());
    }

private:
    std::string              m_dir;
    std::vector<TargetProfile> m_profiles;
    int                      m_activeIndex = 0; // Default to Jupiter
};

} // namespace raduk::profiles
