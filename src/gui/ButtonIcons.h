#pragma once
// ============================================================
// ButtonIcons.h — Phase 119.4: which label gets which icon
//
// By label, not by control id, because labels change at run time:
// "ZOOM: FIT" becomes "ZOOM: 2X" and must keep its icon. Pure, and
// checked by tests/window_chrome.cpp.
//
// Glyphs are Segoe MDL2 Assets, which ships with Windows, and each was
// looked at before it went in. An icon that does not say what the
// button does stays out: DARK and the browse button keep their words,
// because the nearest glyphs were a power symbol and a door. MOUNT CTRL
// shares the Mount tab's target. The label stays beside the icon throughout, since at 2am
// a word is still faster than a picture.
// ============================================================

#include <cwchar>

namespace raduk::gui {

struct ButtonIcon { const wchar_t* prefix; wchar_t glyph; };

inline constexpr ButtonIcon kButtonIcons[] = {
    {L"CAMERA",      L'\xE722'},
    {L"FOCUS",       L'\xE8B3'},
    {L"MOUNT",       L'\xE81D'},
    {L"OUTPUT",      L'\xE74E'},
    {L"ZOOM",        L'\xE8A3'},
    {L"FULLSCR",     L'\xE740'},
    {L"RGB ALIGN",   L'\xE7FD'},
    {L"[FX]",        L'\xE790'},
    {L"[N]",         L'\xE708'},
    {L"[H]",         L'\xE81E'},
    {L"FLAT",        L'\xE706'},
    {L"CLEAR",       L'\xE894'},
    {L"STOP",        L'\xE71A'},
    {L"GO TO",       L'\xE707'},
    {L"GOTO",        L'\xE707'},
    {L"CABLES",      L'\xE71B'},
    {L"SETUP",       L'\xE713'},
    {L"READY",       L'\xE73E'},
    {L"POINT SOLVE", L'\xE81D'},
    {L"LOAD",        L'\xE8E5'},
    {L"SAVE",        L'\xE74E'},
    {L"EDIT",        L'\xE70F'},
};

// 0 for a label with no icon.
inline wchar_t IconForLabel(const wchar_t* label) {
    if (!label) return 0;
    for (const ButtonIcon& i : kButtonIcons)
        if (wcsncmp(label, i.prefix, wcslen(i.prefix)) == 0) return i.glyph;
    return 0;
}

} // namespace raduk::gui
