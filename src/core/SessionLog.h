#pragma once
// ============================================================
// SessionLog.h — Timestamped session action log
// Milestone 5 — writes every significant action to a dated
// log file alongside captures. One log per session.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>   // GetComputerNameA — machine name resolved at runtime
#include <string>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace raduk::core {

class SessionLog {
public:
    SessionLog() = default;
    ~SessionLog() { Close(); }

    // Open a new log file. Path includes filename, e.g.
    // "D:\RadukCapture\logs\session_20260314_213045.log"
    // If path is empty, auto-generates name from current time.
    bool Open(const std::string& path = "") {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_file) fclose(m_file);

        std::string logPath = path;
        if (logPath.empty()) {
            time_t now = time(nullptr);
            struct tm t;
            localtime_s(&t, &now);
            char buf[256];
            sprintf_s(buf, "D:\\RadukCapture\\logs\\session_%04d%02d%02d_%02d%02d%02d.log",
                t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                t.tm_hour, t.tm_min, t.tm_sec);
            logPath = buf;
        }

        // Ensure directory exists
        CreateDirectoryA("D:\\RadukCapture\\logs", nullptr);

        m_file = fopen(logPath.c_str(), "w");
        if (!m_file) {
            // Fallback to current directory
            m_file = fopen("raduk_session.log", "w");
        }
        if (!m_file) return false;

        m_path = logPath;
        WriteHeader();
        printf("[SessionLog] Opened: %s\n", logPath.c_str());
        return true;
    }

    void Close() {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_file) {
            fprintf(m_file, "\n[%s] Session ended\n", Timestamp().c_str());
            fclose(m_file);
            m_file = nullptr;
        }
    }

    bool IsOpen() const { return m_file != nullptr; }
    const std::string& Path() const { return m_path; }

    // Write an end-of-session summary block.
    // Call just before Close() at shutdown.
    void WriteSummary(
        const std::string& target,
        uint32_t framesRecorded,
        int      framesDropped,
        double   totalElapsedSec,
        uint64_t totalBytesWritten,
        int      runCount,
        float    avgSeeingScore,
        int      eafPosition,
        float    eafTemp,
        const std::string& mountRA,
        const std::string& mountDec
    ) {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_file) return;

        time_t now = time(nullptr);
        char timebuf[64];
        struct tm t;
        localtime_s(&t, &now);
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &t);

        double elapsedMin = totalElapsedSec / 60.0;
        double gbWritten  = totalBytesWritten / 1e9;
        double dropPct    = (framesRecorded + framesDropped > 0)
                            ? 100.0 * framesDropped / (framesRecorded + framesDropped)
                            : 0.0;

        fprintf(m_file,
            "\n"
            "# ============================================================\n"
            "# SESSION SUMMARY  %s\n"
            "# ============================================================\n"
            "#   Target profile  : %s\n"
            "#   Capture runs    : %d\n"
            "#   Frames recorded : %u\n"
            "#   Frames dropped  : %d  (%.1f%%)\n"
            "#   Recording time  : %.1f min\n"
            "#   Data written    : %.2f GB\n"
            "#   Avg seeing score: %.1f / 10\n"
            "#   EAF position    : %d  (%.1f C)\n"
            "#   Mount final pos : RA %s  Dec %s\n"
            "# ============================================================\n",
            timebuf,
            target.c_str(),
            runCount,
            framesRecorded,
            framesDropped, dropPct,
            elapsedMin,
            gbWritten,
            avgSeeingScore,
            eafPosition, eafTemp,
            mountRA.c_str(), mountDec.c_str());
        fflush(m_file);
    }

    // Log a categorized event
    void Write(const char* category, const char* fmt, ...) {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_file) return;

        fprintf(m_file, "[%s] [%-12s] ", Timestamp().c_str(), category);
        va_list args;
        va_start(args, fmt);
        vfprintf(m_file, fmt, args);
        va_end(args);
        fprintf(m_file, "\n");
        fflush(m_file);
    }

    // Convenience wrappers for common categories
    void Camera(const char* fmt, ...)  { va_list a; va_start(a, fmt); WriteV("CAMERA",  fmt, a); va_end(a); }
    void Focuser(const char* fmt, ...) { va_list a; va_start(a, fmt); WriteV("FOCUSER", fmt, a); va_end(a); }
    void Mount(const char* fmt, ...)   { va_list a; va_start(a, fmt); WriteV("MOUNT",   fmt, a); va_end(a); }
    void Capture(const char* fmt, ...) { va_list a; va_start(a, fmt); WriteV("CAPTURE", fmt, a); va_end(a); }
    void Profile(const char* fmt, ...) { va_list a; va_start(a, fmt); WriteV("PROFILE", fmt, a); va_end(a); }
    void Safety(const char* fmt, ...)  { va_list a; va_start(a, fmt); WriteV("SAFETY",  fmt, a); va_end(a); }
    void System(const char* fmt, ...)  { va_list a; va_start(a, fmt); WriteV("SYSTEM",  fmt, a); va_end(a); }

private:
    void WriteV(const char* category, const char* fmt, va_list args) {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_file) return;
        fprintf(m_file, "[%s] [%-12s] ", Timestamp().c_str(), category);
        vfprintf(m_file, fmt, args);
        fprintf(m_file, "\n");
        fflush(m_file);
    }

    void WriteHeader() {
        time_t now = time(nullptr);
        char timebuf[64];
        struct tm t;
        localtime_s(&t, &now);
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &t);

        // Machine name resolved at runtime — nothing personal hardcoded
        char machine[64] = "unknown";
        DWORD mlen = (DWORD)sizeof(machine);
        GetComputerNameA(machine, &mlen);
        fprintf(m_file,
            "# ============================================================\n"
            "# RadukCapture Session Log\n"
            "# Started: %s\n"
            "# Machine: %s\n"
            "# Telescope: Celestron 8SE  Camera: ZWO ASI678MC\n"
            "# ============================================================\n\n",
            timebuf, machine);
        fflush(m_file);
    }

    std::string Timestamp() {
        time_t now = time(nullptr);
        struct tm t;
        localtime_s(&t, &now);
        char buf[32];
        sprintf_s(buf, "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
        return std::string(buf);
    }

    FILE*       m_file = nullptr;
    std::string m_path;
    std::mutex  m_mutex;
};

} // namespace raduk::core
