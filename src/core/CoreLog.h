#pragma once
// ============================================================
//  CoreLog -- core code's messages, into the app's log
//
//  The mount and the centroid tracker reported everything through
//  printf, which in this GUI app goes to a console window and nowhere
//  else. On 2026-11-07 the mount controls failed twice at the rig and
//  the log held no trace of a timeout, a lost stop or an emergency stop,
//  so the failure could not be diagnosed. CoreLog formats like printf,
//  stamps the time to the millisecond, and hands the line to whatever
//  main.cpp installs (the log file). With nothing installed -- the bench
//  tests -- it falls back to stdout.
// ============================================================

#include <cstdarg>
#include <cstdio>
#include <functional>
#include <string>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace raduk::core {

inline std::function<void(const char*)>& CoreLogHook() {
    static std::function<void(const char*)> hook;
    return hook;
}

inline void CoreLog(const char* fmt, ...) {
    char body[1024];
    va_list a; va_start(a, fmt); vsnprintf(body, sizeof(body), fmt, a); va_end(a);
    SYSTEMTIME t; GetLocalTime(&t);
    char line[1100];
    snprintf(line, sizeof(line), "%02d:%02d:%02d.%03d %s",
             t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, body);
    if (auto& h = CoreLogHook()) h(line);
    else { fputs(line, stdout); fflush(stdout); }
}

} // namespace raduk::core
