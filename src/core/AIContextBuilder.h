#pragma once
// ============================================================
// AIContextBuilder.h — Phase 50: AI System Prompt Builder
//
// Assembles the system prompt injected before every AI query.
// Five context blocks, refreshed on each message:
//
//  [IDENTITY]        — RadukScope persona + site info
//  [APP REFERENCE]   — Full RadukCapture-Help.md (cached)
//  [SESSION STATE]   — Live camera/mount/format/seeing snapshot
//  [TONIGHT'S SKY]   — Planner data from GoToPanel
//  [CAPTURES TONIGHT]— SER file listing from output dir
//  [OBJECT KNOWLEDGE]— SQLite stub (empty until 50a)
// ============================================================

#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstdio>
#include "ObjectKnowledgeDB.h"
#include "RigConfig.h"

namespace raduk::core {

// ── Session state snapshot ─────────────────────────────────────
// Filled by main.cpp each render tick and passed to Build().
struct AISessionState {
    // Time
    std::string localTime;      // "2026-03-22 21:45 MST"
    std::string lstStr;         // "08h 32m"

    // Target
    std::string targetName;     // "Jupiter"
    std::string profileName;    // "Jupiter"

    // Mount
    std::string mountRA;        // "11h 52m"
    std::string mountDec;       // "+1° 14'"
    float       mountAlt = 0;
    float       mountAz  = 0;
    bool        tracking = false;

    // Camera
    int   gain       = 0;
    float exposureMs = 0;
    float fps        = 0;
    float seeingScore= 0;   // 0–10

    // Phase 86: capture format. The toolbar can change these mid-session
    // now, and without them the assistant could not answer "am I at full
    // frame", "why is this file so big", or spot that someone is still
    // cropped from an earlier [ROI] and should press FULL.
    int   roiWidth    = 0;
    int   roiHeight   = 0;
    int   bitDepth    = 8;
    int   sensorWidth = 0;   // for recognising a crop
    int   sensorHeight= 0;
    std::string limitType;   // "frames" or "time"
    int   limitValue  = 0;

    // Focuser
    int   eafPos     = 0;
    float eafTempC   = 0;

    // Capture
    bool     recording     = false;
    uint32_t framesRecorded= 0;
    uint32_t framesDropped = 0;
    int      bufferUsed    = 0;
    int      bufferTotal   = 0;

    // Output directory (for captures scan)
    std::string outputDir;
};

class AIContextBuilder {
public:
    AIContextBuilder() = default;

    // Call once at startup — caches Help.md content.
    void LoadHelpFile(const std::wstring& exeDir) {
        std::wstring path = exeDir + L"\\RadukCapture-Help.md";
        HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, 0, nullptr);
        if (hf == INVALID_HANDLE_VALUE) {
            path = L"RadukCapture-Help.md";
            hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, 0, nullptr);
        }
        if (hf != INVALID_HANDLE_VALUE) {
            DWORD sz = GetFileSize(hf, nullptr);
            if (sz > 0 && sz < 512 * 1024) {
                std::vector<char> buf(sz + 1, 0);
                DWORD read = 0;
                ReadFile(hf, buf.data(), sz, &read, nullptr);
                m_helpText = std::string(buf.data(), read);
            }
            CloseHandle(hf);
            printf("[AIContextBuilder] Help.md loaded (%zu bytes)\n",
                   m_helpText.size());
        } else {
            m_helpText = "(Help file not found — limited app reference available)";
            printf("[AIContextBuilder] Help.md not found — using stub\n");
        }
    }

    void SetKnowledgeDB(ObjectKnowledgeDB* db) { m_db = db; }

    // Build the full system prompt. Called before every AI query.
    // PlannerVec must have elements with: name, type, mag, altNow,
    // peakAlt, minsLeft, windowStr fields.
    template<typename PlannerVec>
    std::string Build(const AISessionState& s, const PlannerVec& planner,
                      const std::string& userQuery) {
        std::ostringstream ss;

        // ── [IDENTITY] ────────────────────────────────────────
        ss << "[IDENTITY]\n"
           << "You are RadukScope, an AI assistant embedded in the RadukCapture "
              "astrophotography application.\n"
           << "You are running locally on the capture PC. Observing site: "
           << RigConfig::Get().siteLatDeg << ", "
           << RigConfig::Get().siteLonDeg << " (configured in rig.ini).\n"
           << "Equipment: Celestron 8SE SCT (f/10, 2032mm FL), ZWO ASI678MC "
              "(Sony IMX678, 3840x2160, 2.9um), ZWO EAF, NexStar mount.\n"
           << "Plate scale: 0.203 arcsec/pixel. HCG threshold: gain 182.\n"
           // The filter is removable hardware the app cannot detect, and
           // it moves the atmospheric dispersion figure by up to 4x. An
           // assistant answering "do I need the ADC tonight" has to know
           // which band is in front of the sensor.
           << "Filter fitted: " << RigConfig::Get().filterName << " ("
           << (int)RigConfig::Get().filterLowNm << "-"
           << (int)RigConfig::Get().filterHighNm
           << "nm). This is set in rig.ini / SETUP, not detected - if the "
              "user says they changed filters, tell them to update it, "
              "because every dispersion figure depends on it.\n"
           << "Answer concisely and practically. If unsure, say so. "
              "Never invent equipment specs or sky positions.\n\n";

        // ── [APP REFERENCE] ───────────────────────────────────
        if (!m_helpText.empty()) {
            ss << "[APP REFERENCE]\n" << m_helpText << "\n\n";
        }

        // ── [SESSION STATE] ───────────────────────────────────
        ss << "[SESSION STATE]\n";
        ss << "Time: " << s.localTime << "  |  LST: " << s.lstStr << "\n";
        ss << "Target: " << (s.targetName.empty() ? "none" : s.targetName)
           << "  |  Profile: " << (s.profileName.empty() ? "none" : s.profileName) << "\n";
        ss << "Mount: RA " << s.mountRA << "  Dec " << s.mountDec
           << "  Alt " << std::fixed << std::setprecision(1) << s.mountAlt
           << "°  Az "  << s.mountAz << "°"
           << "  Tracking: " << (s.tracking ? "ON" : "OFF") << "\n";
        ss << "Camera: Gain " << s.gain
           << "  Exp " << s.exposureMs << "ms"
           << "  FPS " << s.fps
           << "  Seeing " << s.seeingScore << "/10\n";
        // Format, and whether it is a crop. Said in words rather than
        // left as two numbers to compare, because the question people
        // actually ask is "why has my field gone narrow".
        if (s.roiWidth > 0 && s.roiHeight > 0) {
            ss << "Format: " << s.roiWidth << "x" << s.roiHeight << "  "
               << s.bitDepth << "-bit";
            if (s.sensorWidth > 0 && s.sensorHeight > 0 &&
                (s.roiWidth < s.sensorWidth || s.roiHeight < s.sensorHeight)) {
                ss << "  (CROPPED from the full " << s.sensorWidth << "x"
                   << s.sensorHeight << " sensor - the FULL button on the top "
                   << "bar restores the whole field)";
            } else {
                ss << "  (full sensor)";
            }
            ss << "\n";
        }
        if (!s.limitType.empty() && s.limitValue > 0) {
            ss << "Capture limit: " << s.limitValue
               << (s.limitType == "time" ? " seconds" : " frames") << "\n";
        }
        ss << "EAF: " << s.eafPos << "  Temp: " << s.eafTempC << "°C\n";
        ss << "Recording: " << (s.recording ? "YES" : "NO");
        if (s.recording) {
            ss << "  Frames: " << s.framesRecorded
               << "  Buffer: " << s.bufferUsed << "/" << s.bufferTotal
               << "  Drops: " << s.framesDropped;
        }
        ss << "\n\n";

        // ── [TONIGHT'S SKY / PLANNER] ─────────────────────────
        ss << "[TONIGHT'S SKY — imaging windows, sorted by time remaining]\n";
        int count = 0;
        for (const auto& e : planner) {
            if (count >= 25) break; // cap at 25 objects
            // Skip very faint or Sun
            if (e.type == "Sun") continue;
            if (e.mag > 9.5f)   continue;

            ss << std::left << std::setw(18) << e.name
               << "  Alt " << std::setw(5) << std::right
               << (int)e.altNow << "°";

            if (e.minsLeft > 0) {
                ss << "  Left " << (e.minsLeft / 60) << "h"
                   << std::setw(2) << std::setfill('0')
                   << (e.minsLeft % 60) << "m" << std::setfill(' ');
                ss << "  Win " << e.windowStr;
            } else {
                ss << "  (not above 30° tonight)";
            }
            ss << "  Peak " << (int)e.peakAlt << "°"
               << "  Mag " << std::fixed << std::setprecision(1) << e.mag;

            // Mark current target
            if (!s.targetName.empty() && e.name.find(s.targetName) != std::string::npos)
                ss << "  ← CURRENT TARGET";

            ss << "\n";
            count++;
        }
        ss << "\n";

        // ── [CAPTURES TONIGHT] ────────────────────────────────
        ss << "[CAPTURES TONIGHT]\n";
        auto captures = ScanCapturesDir(s.outputDir);
        if (captures.empty()) {
            ss << "(no captures yet tonight)\n";
        } else {
            for (const auto& c : captures)
                ss << c << "\n";
        }
        ss << "\n";

        // ── [OBJECT KNOWLEDGE] ────────────────────────────────
        if (m_db && m_db->IsAvailable()) {
            // Extract object name from the query, then look it up
            std::string objName = m_db->ExtractObjectName(userQuery);
            std::string objKnowledge;
            if (!objName.empty())
                objKnowledge = m_db->Query(objName);
            if (!objKnowledge.empty()) {
                ss << "[OBJECT KNOWLEDGE]\n" << objKnowledge << "\n\n";
            }
        }

        return ss.str();
    }

private:
    // Scan output directory for tonight's SER files.
    // "Tonight" = files modified in the last 12 hours.
    std::vector<std::string> ScanCapturesDir(const std::string& dir) const {
        std::vector<std::string> results;
        if (dir.empty()) return results;

        std::string pattern = dir + "\\*.ser";
        WIN32_FIND_DATAA fd;
        HANDLE hf = FindFirstFileA(pattern.c_str(), &fd);
        if (hf == INVALID_HANDLE_VALUE) return results;

        FILETIME now; GetSystemTimeAsFileTime(&now);
        ULONGLONG nowULL = ((ULONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime;
        ULONGLONG twelveHours = 12ULL * 3600 * 10000000; // 12h in 100ns units

        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            ULONGLONG fileTime = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32)
                               | fd.ftLastWriteTime.dwLowDateTime;
            if (nowULL - fileTime > twelveHours) continue; // older than 12h

            // Format size
            ULONGLONG bytes = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            char sizeStr[16];
            if (bytes >= 1024*1024*1024)
                snprintf(sizeStr, 16, "%.1f GB", bytes / (1024.0*1024.0*1024.0));
            else
                snprintf(sizeStr, 16, "%.0f MB", bytes / (1024.0*1024.0));

            results.push_back(std::string(fd.cFileName) + "  (" + sizeStr + ")");
        } while (FindNextFileA(hf, &fd) && results.size() < 20);

        FindClose(hf);
        return results;
    }

    std::string        m_helpText;
    ObjectKnowledgeDB* m_db = nullptr;
};

} // namespace raduk::core
