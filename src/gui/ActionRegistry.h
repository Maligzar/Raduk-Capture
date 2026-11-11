#pragma once
// ============================================================
// ActionRegistry.h -- phase 132.7: every keyboard command, by name
//
// The command palette (Ctrl+K) finds a command by typing part of what
// it does. This is its list and its ranking, and nothing else: pure, no
// Windows, so tests/command_palette.cpp can pin both.
//
// ── One source of behaviour ─────────────────────────────────
//
// An action here is not a second implementation of anything. It names
// a key the main window already answers -- R, Shift+H, Ctrl+Shift+M --
// and the palette presses it through Window::RunKey, which runs the same
// case in HandleKeyDown that the keyboard does. So an action cannot do
// something its shortcut does not, and the shortcut shown beside it is
// true by construction. tests/command_palette.cpp checks every action's
// key against Window.cpp's own cases, so a key that is moved there and
// not here fails the suite rather than the palette.
//
// The arrow-key jogs are not here: a jog starts on key-down and stops
// on key-up, and a palette has no key-up to give.
//
// ── Guarded actions ─────────────────────────────────────────
//
// Anything that moves the telescope or the focuser, or starts an
// unattended sequence, is `guarded`: the palette asks for Enter a
// second time before running it. Typing three letters and Enter must
// not be able to slew the mount by accident.
// ============================================================

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace raduk::gui::palette {

struct Action {
    const char* id;
    const char* label;      // what it does, as a person would say it
    const char* keywords;   // other words someone might type
    const char* shortcut;   // as shown, e.g. "Ctrl+Shift+M"
    // How the palette runs it: a key for HandleKeyDown, or a character
    // for HandleChar (the ?, /, O, U and Shift+A commands live there).
    unsigned    vk;         // virtual-key code, 0 when `ch` is used
    bool        shift;
    bool        ctrl;
    wchar_t     ch;         // character, 0 when `vk` is used
    bool        guarded;
};

// VK codes spelled out so this header stays free of windows.h.
namespace vk {
    inline constexpr unsigned SPACE = 0x20, F11 = 0x7A;
}

// The table. Labels are the action, not the window's name alone, so
// "histogram" and "show the histogram" both find it.
inline const std::vector<Action>& All() {
    static const std::vector<Action> a = {
        // Safety first, so an empty query shows it at the top.
        {"stop",        "Emergency stop: mount, tracking, autofocus, recording", "halt abort esc escape space", "Space", vk::SPACE, false, false, 0, false},

        // Windows
        {"goto",        "GoTo window",              "go to target object catalogue search", "G",            'G', false, false, 0, false},
        {"skymap",      "Sky map",                  "chart planisphere map",                "M",            'M', false, false, 0, false},
        {"skymap2",     "Second sky map",           "chart map control screen",             "Shift+M",      'M', true,  false, 0, false},
        {"histogram",   "Histogram window",         "levels exposure clipping",             "Shift+H",      'H', true,  false, 0, false},
        {"sharpness",   "Sharpness window",         "seeing quality focus graph",           "Shift+Q",      'Q', true,  false, 0, false},
        {"temperature", "Temperature window",       "sensor focuser heat dew",              "Shift+T",      'T', true,  false, 0, false},
        {"status",      "Status window",            "sky altitude airmass weather",         "Shift+D",      'D', true,  false, 0, false},
        {"adc",         "ADC alignment window",     "atmospheric dispersion corrector prism", "Shift+A",    0,   false, false, L'A', false},
        {"objectpanel", "Object panel",             "info facts audience about",            "Shift+I",      'I', true,  false, 0, false},
        {"objectfull",  "Object panel fullscreen",  "info audience screen",                 "Ctrl+Shift+I", 'I', true,  true,  0, false},
        {"shareview",   "Share view",               "audience screen public caption",       "Shift+V",      'V', true,  false, 0, false},
        {"sharefull",   "Share view fullscreen",    "audience screen public",               "Ctrl+Shift+V", 'V', true,  true,  0, false},
        {"help",        "Help",                     "keys shortcuts manual",                "?",            0,   false, false, L'?', false},
        {"assistant",   "Assistant",                "ai chat ask",                          "/",            0,   false, false, L'/', false},
        {"fullscreen",  "Fullscreen preview",       "maximise borderless",                  "F11",          vk::F11, false, false, 0, false},

        // The view
        {"night",       "Night vision",             "red dark mode",                        "N",            'N', false, false, 0, false},
        {"reticle",     "Reticle",                  "crosshair centre overlay",             "R",            'R', false, false, 0, false},
        {"clip",        "Clipping warning",         "overexposed saturated highlights",     "C",            'C', false, false, 0, false},
        {"quality",     "Quality graph",            "seeing sharpness overlay",             "Q",            'Q', false, false, 0, false},
        {"zoom",        "Zoom: next step",          "magnify enlarge",                      "Z",            'Z', false, false, 0, false},
        {"stretch",     "Display stretch",          "levels brighten contrast",             "L",            'L', false, false, 0, false},
        {"autostretch", "Display stretch from the planet", "auto levels brighten view dim planet", "Shift+L",  'L', true,  false, 0, false},
        {"channel",     "Colour channel: next",     "red green blue mono rgb",              "H",            'H', false, false, 0, false},
        {"fx",          "Display effect: next",     "filter enhance sharpen",               "B",            'B', false, false, 0, false},
        {"pixel",       "Pixel readout",            "value adu inspect",                    "X",            'X', false, false, 0, false},
        {"ghost",       "Ghost frame",              "overlay compare previous",             "O",            0,   false, false, L'o', false},
        {"clearview",   "Clear the view",           "hide overlays clean",                  "U",            0,   false, false, L'u', false},
        {"rgbalign",    "RGB align",                "colour channels offset dispersion",    "V",            'V', false, false, 0, false},
        {"advisory",    "Advisories",               "watch hints suggestions",              "W",            'W', false, false, 0, false},

        // Capture
        {"snapshot",    "Snapshot",                 "still image save frame png",           "S",            'S', false, false, 0, false},
        {"framing",     "Framing shot",             "boosted snapshot bright",              "Shift+S",      'S', true,  false, 0, false},
        {"pause",       "Pause or resume recording", "hold continue",                       "P",            'P', false, false, 0, false},
        {"stack",       "Stack the last recording", "autostakkert process",                 "K",            'K', false, false, 0, false},
        {"openlast",    "Open the last recording",  "file explorer show",                   "Shift+K",      'K', true,  false, 0, false},
        {"phemu",       "Jupiter's moons: mutual events", "phemu imcce eclipse occultation io europa ganymede callisto", "J", 'J', false, false, 0, false},

        // Things that move hardware, or start a sequence: guarded.
        {"autofocus",   "Autofocus: start or stop", "focus sweep hfd",                      "A",            'A', false, false, 0, true},
        {"focusin",     "Focuser: fine step",       "focus nudge 10",                       "F",            'F', false, false, 0, true},
        {"focusout",    "Focuser: coarse step",     "focus nudge 100",                      "Shift+F",      'F', true,  false, 0, true},
        {"tracking",    "Tracking on or off",       "sidereal follow mount",                "T",            'T', false, false, 0, true},
        {"recall",      "Recall a capture's framing", "goto past capture return",           "Shift+G",      'G', true,  false, 0, true},
        {"recalloff",   "Recall the target's place in that frame", "offset nudge centre",   "Ctrl+Shift+G", 'G', true,  true,  0, true},
        {"mosaic",      "Mosaic: start or stop",    "panels tiles lunar sequence",          "Ctrl+Shift+M", 'M', true,  true,  0, true},
        {"phemurec",    "Record the next mutual event, or cancel it", "phemu imcce record light curve clock", "Shift+J", 'J', true, false, 0, true},
    };
    return a;
}

// ── Ranking ─────────────────────────────────────────────────
//
// The query's characters must all appear, in order, in the label or the
// keywords (a subsequence, case aside). Better: each run of characters
// that matches in a row, a match at the start of a word, and a match in
// the label rather than the keywords. An empty query keeps the table's
// own order. Ties keep the table's order too, so the list is stable as
// someone types.

inline char Lower(char c) { return (char)std::tolower((unsigned char)c); }

// The score of `q` against `text`, or -1 when it does not match.
inline int MatchScore(const std::string& q, const char* text) {
    if (q.empty()) return 0;
    if (!text) return -1;
    int score = 0, run = 0;
    size_t qi = 0;
    char prev = ' ';
    for (const char* p = text; *p && qi < q.size(); ++p) {
        const char c = *p;
        if (Lower(c) == Lower(q[qi])) {
            const bool wordStart = !std::isalnum((unsigned char)prev);
            score += 1 + run * 3 + (wordStart ? 6 : 0) + (p == text ? 4 : 0);
            ++run;
            ++qi;
        } else {
            run = 0;
        }
        prev = c;
    }
    return qi == q.size() ? score : -1;
}

struct Ranked { size_t index; int score; };

// Matching actions, best first. `list` is any table, so the test can
// time a thousand entries.
inline std::vector<Ranked> Rank(const std::string& query, const std::vector<Action>& list) {
    std::string q;
    for (char c : query) if (c != ' ') q += c;   // spaces are for the eye
    std::vector<Ranked> out;
    out.reserve(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        int s = MatchScore(q, list[i].label);
        if (s >= 0) s += 10;                   // the label is what it IS
        const int k = MatchScore(q, list[i].keywords);
        if (k > s) s = k;
        if (s >= 0) out.push_back({i, s});
    }
    // Stable, so ties keep table order.
    std::stable_sort(out.begin(), out.end(),
                     [](const Ranked& a, const Ranked& b) { return a.score > b.score; });
    return out;
}

} // namespace raduk::gui::palette
