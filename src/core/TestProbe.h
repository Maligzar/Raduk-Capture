#pragma once
// ============================================================
//  TestProbe -- phase 125
//
//  Lets a scenario (Scenario.h) see what the operator sees: notices,
//  status lines and button labels, as text. Nothing is listening in an
//  ordinary run, and then each call is one null check.
//
//  Lines are tagged by where they came from:
//      [NOTICE] the notification bar
//      [STATUS] a panel's status line
//      [BUTTON] a toolbar or rail button's label, "RUNS -> #1"
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <functional>
#include <string>

namespace raduk::core {

inline std::function<void(const std::string&)>& ProbeHook() {
    static std::function<void(const std::string&)> hook;
    return hook;
}

inline bool ProbeOn() { return (bool)ProbeHook(); }

inline void Probe(const std::string& line) {
    if (auto& h = ProbeHook()) h(line);
}

inline void ProbeW(const char* tag, const std::wstring& text) {
    auto& h = ProbeHook();
    if (!h) return;
    std::string u;
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
                                      nullptr, 0, nullptr, nullptr);
    if (n > 0) {
        u.resize((size_t)n);
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &u[0], n,
                            nullptr, nullptr);
    }
    h(std::string(tag) + " " + u);
}

} // namespace raduk::core
