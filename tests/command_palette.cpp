// ============================================================
//  Phase 132.7: the command palette's list and ranking
//
//  1. Every action is a key the main window really answers: its case
//     exists in Window.cpp's HandleKeyDown (or its character in
//     HandleChar), and a Shift or Ctrl action's case reads Shift or
//     Ctrl. A key moved in Window.cpp and not here fails here.
//  2. The shortcut shown is the key pressed.
//  3. Anything that moves hardware or starts a sequence is guarded.
//  4. The ranking finds the obvious thing first.
//  5. A thousand entries rank in well under a millisecond.
//  6. Shift+A no longer starts autofocus (the ADC window's key).
// ============================================================

#include "gui/ActionRegistry.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

using namespace raduk::gui::palette;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-60s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static std::string Root() {
    std::string f = __FILE__;
    for (char& c : f) if (c == '\\') c = '/';
    const size_t at = f.rfind("/tests/");
    return at == std::string::npos ? std::string(".") : f.substr(0, at);
}
static std::string Read(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The body of `case <label>:` inside `fn`'s text, up to the next case.
static std::string CaseBody(const std::string& fn, const std::string& label) {
    const std::string key = "case " + label + ":";
    const size_t at = fn.find(key);
    if (at == std::string::npos) return "";
    size_t end = fn.find("\n        case ", at + key.size());
    if (end == std::string::npos) end = fn.size();
    return fn.substr(at, end - at);
}

static std::string Function(const std::string& src, const std::string& sig) {
    const size_t at = src.find(sig);
    if (at == std::string::npos) return "";
    const size_t end = src.find("\n}\n", at);
    return src.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

static std::string ShortcutFor(const Action& a) {
    if (a.ch) {
        if (a.ch == L'A') return "Shift+A";
        if (a.ch >= L'a' && a.ch <= L'z') return std::string(1, (char)(a.ch - 32));
        return std::string(1, (char)a.ch);
    }
    std::string k;
    if (a.vk == vk::SPACE) k = "Space";
    else if (a.vk == vk::F11) k = "F11";
    else k = std::string(1, (char)a.vk);
    return std::string(a.ctrl ? "Ctrl+" : "") + (a.shift ? "Shift+" : "") + k;
}

int main() {
    printf("\n=== Phase 132.7: command palette ===\n");
    const auto& all = All();

    printf("\n1-2. Every action is a real key, shown truly\n");
    {
        const std::string src = Read(Root() + "/src/gui/Window.cpp");
        const std::string down = Function(src, "bool Window::HandleKeyDown(");
        const std::string chr  = Function(src, "bool Window::HandleChar(");
        Check(!down.empty() && !chr.empty(), "found HandleKeyDown and HandleChar");
        std::set<std::string> ids;
        int bad = 0;
        for (const Action& a : all) {
            if (!ids.insert(a.id).second) { ++bad; Check(false, std::string("duplicate id ") + a.id); }
            if (ShortcutFor(a) != a.shortcut) {
                ++bad;
                Check(false, std::string(a.id) + ": shown shortcut is the key pressed",
                      std::string(a.shortcut) + " vs " + ShortcutFor(a));
            }
            if (a.ch) {
                const std::string lit = std::string("L'") + (char)a.ch + "'";
                if (chr.find(lit) == std::string::npos) {
                    ++bad; Check(false, std::string(a.id) + ": HandleChar answers " + lit);
                }
                continue;
            }
            const std::string label = a.vk == vk::SPACE ? "VK_SPACE"
                                    : a.vk == vk::F11   ? "VK_F11"
                                    : std::string("'") + (char)a.vk + "'";
            const std::string body = CaseBody(down, label);
            if (body.empty()) { ++bad; Check(false, std::string(a.id) + ": HandleKeyDown has " + label); continue; }
            if (a.shift && body.find("shift") == std::string::npos) {
                ++bad; Check(false, std::string(a.id) + ": its case reads Shift");
            }
            if (a.ctrl && body.find("ModCtrl()") == std::string::npos) {
                ++bad; Check(false, std::string(a.id) + ": its case reads Ctrl");
            }
        }
        Check(bad == 0, "all " + std::to_string(all.size()) + " actions match Window.cpp");
        Check(all.size() >= 35, "the palette covers the keyboard", std::to_string(all.size()) + " actions");
    }

    printf("\n3. Guarded\n");
    {
        const std::set<std::string> mustGuard = {"autofocus", "focusin", "focusout", "tracking",
                                                 "recall", "recalloff", "mosaic"};
        int n = 0;
        bool ok = true;
        for (const Action& a : all) {
            const bool must = mustGuard.count(a.id) > 0;
            if (a.guarded) ++n;
            if (must != a.guarded) { ok = false; Check(false, std::string(a.id) + " guard"); }
        }
        Check(ok && n == (int)mustGuard.size(),
              "everything that moves hardware or starts a sequence asks twice");
        Check(std::string(all[0].id) == "stop" && !all[0].guarded,
              "the emergency stop is first, and never asks twice");
    }

    printf("\n4. Ranking\n");
    {
        auto first = [&](const char* q) {
            const auto r = Rank(q, all);
            return r.empty() ? std::string("(none)") : std::string(all[r[0].index].id);
        };
        Check(first("") == "stop", "an empty query keeps the table's order");
        Check(Rank("", all).size() == all.size(), "and shows everything");
        Check(first("hist") == "histogram", "hist -> histogram", first("hist"));
        Check(first("snap") == "snapshot", "snap -> snapshot", first("snap"));
        Check(first("night") == "night", "night -> night vision", first("night"));
        Check(first("stop") == "stop", "stop -> emergency stop", first("stop"));
        Check(first("sky map") == "skymap", "sky map -> sky map", first("sky map"));
        Check(first("crosshair") == "reticle", "a keyword finds it: crosshair -> reticle",
              first("crosshair"));
        Check(first("GOTO") == "goto", "case does not matter", first("GOTO"));
        Check(Rank("qqqzzz", all).empty(), "nonsense matches nothing");
        Check(MatchScore("hs", "Histogram") > 0 && MatchScore("sh", "Histogram") < 0,
              "the characters must come in order");
    }

    printf("\n5. Speed\n");
    {
        std::vector<Action> big;
        while (big.size() < 1000) for (const Action& a : all) if (big.size() < 1000) big.push_back(a);
        const auto t0 = std::chrono::steady_clock::now();
        size_t found = 0;
        const int reps = 200;
        for (int i = 0; i < reps; ++i) found += Rank(i % 2 ? "sky" : "focus st", big).size();
        const double us = std::chrono::duration<double, std::micro>(
                              std::chrono::steady_clock::now() - t0).count() / reps;
        char d[64];
        snprintf(d, sizeof d, "%.0f us per query", us);
        Check(found > 0 && us < 1000.0, "1,000 entries rank in under 1 ms", d);
    }

    printf("\n6. Shift+A is the ADC window, not autofocus\n");
    {
        const std::string src = Read(Root() + "/src/gui/Window.cpp");
        const std::string body = CaseBody(Function(src, "bool Window::HandleKeyDown("), "'A'");
        const size_t guard = body.find("if (shift) return false;");
        const size_t af = body.find("m_onAutofocusToggle()");
        Check(guard != std::string::npos && af != std::string::npos && guard < af,
              "case 'A' hands Shift+A on before it can start a sweep");
    }

    printf("\n%s\n", g_fail ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return g_fail ? 1 : 0;
}
