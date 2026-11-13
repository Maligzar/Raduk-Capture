#pragma once
// ============================================================
//  CoreLog -- core code's messages, into the app's log
//
//  The mount and the centroid tracker reported everything through
//  printf, which in this GUI app goes to a console window and nowhere
//  else. On 2026-11-07 the mount controls failed twice at the rig and
//  the log held no trace of a timeout, a lost stop or an emergency stop,
//  so the failure could not be diagnosed. CoreLog formats like printf,
//  stamps the local time to the millisecond, and hands the line to whatever
//  main.cpp installs (the log file). With nothing installed -- the bench
//  tests -- it falls back to stdout.
// ============================================================

#include <cstdarg>
#include <cstdio>
#include <functional>
#include <string>
#include <chrono>
#include <ctime>
// Phase 155: no <windows.h>. Every core header now logs through here, and
// pulling the Windows macros into headers that never had them (and into
// the g++ bench tests) is a cost with no benefit: the standard library
// gives the same local time to the millisecond.

namespace raduk::core {

inline std::function<void(const char*)>& CoreLogHook() {
    static std::function<void(const char*)> hook;
    return hook;
}

inline void CoreLog(const char* fmt, ...) {
    char body[1024];
    va_list a; va_start(a, fmt); vsnprintf(body, sizeof(body), fmt, a); va_end(a);
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    const int ms = (int)(std::chrono::duration_cast<std::chrono::milliseconds>(
                             now.time_since_epoch()).count() % 1000);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &tt);
#else
    localtime_r(&tt, &lt);
#endif
    char line[1100];
    snprintf(line, sizeof(line), "%02d:%02d:%02d.%03d %s",
             lt.tm_hour, lt.tm_min, lt.tm_sec, ms, body);
    if (auto& h = CoreLogHook()) h(line);
    else { fputs(line, stdout); fflush(stdout); }
}

} // namespace raduk::core
