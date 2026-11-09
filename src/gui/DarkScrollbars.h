#pragma once
// ============================================================
// DarkScrollbars.h -- phase 132.6b: Windows' own dark scrollbars
//
// A system scrollbar cannot be told a colour. It is classic light grey
// on a de-themed control, the lightest thing on a night screen. Windows
// has a dark scrollbar, but only behind two undocumented uxtheme entry
// points, by ordinal, that Explorer, Notepad++ and Windows Terminal all
// call (inferred from their published sources, not from Microsoft):
//
//   135  SetPreferredAppMode(AllowDark)    Windows 10 1903 (18362) on
//        AllowDarkModeForApp(true)         1809 (17763) only
//   133  AllowDarkModeForWindow(hwnd, true)
//
// then SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr). Before 1809
// none of it exists, so nothing is called and the bars stay as they
// were. Ordinals are looked up, never assumed; a missing one is a no-op.
//
// ── What is themed ──────────────────────────────────────────
//
// Only where the theme can change nothing but the scrollbar:
//
//   - list views. The theme would also restyle their rows (hover,
//     selection, focus ring); StripDraw.h paints every row itself, so
//     those never show.
//   - this app's own window classes that scroll (the control panel's
//     host), whose client area the app paints anyway.
//
// NOT edit boxes or list boxes: a themed one stops honouring the app's
// WM_CTLCOLOR* brushes (ControlPanel.cpp found that for edits), which
// would turn a dark box white. Their bars stay classic for now.
//
// ── Night ───────────────────────────────────────────────────
//
// Dark grey is still not red. No system scrollbar can be; that needs a
// bar the app draws itself. But dark grey gives off a small fraction of
// the light the classic bar did. rig.ini DarkScrollbars=0 turns all of
// this off.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>

#include <cwchar>

#pragma comment(lib, "uxtheme.lib")

namespace raduk::gui::darkbars {

// Which entry point 135 is on this build. Pure, so it is tested.
enum class AppMode { None, AllowDarkModeForApp, SetPreferredAppMode };
inline constexpr AppMode AppModeFor(DWORD build) {
    return build >= 18362 ? AppMode::SetPreferredAppMode
         : build >= 17763 ? AppMode::AllowDarkModeForApp
         : AppMode::None;
}

// The classes left alone: stock controls whose own drawing the theme
// would change (see above), and the ones with no scrollbar of their own.
inline bool IsSkippedClass(const wchar_t* cls) {
    static const wchar_t* const skip[] = {
        L"Edit", L"ListBox", L"ComboBox", L"ComboLBox", L"Button", L"Static",
        L"SysTabControl32", L"SysHeader32", L"msctls_trackbar32", L"msctls_updown32",
        L"msctls_progress32", L"RichEdit20W", L"RICHEDIT50W", L"SysTreeView32",
    };
    for (const wchar_t* s : skip) if (_wcsicmp(cls, s) == 0) return true;
    return false;
}

namespace detail {
inline DWORD Build() {
    using Fn = void(WINAPI*)(DWORD*, DWORD*, DWORD*);
    static DWORD build = [] {
        DWORD maj = 0, min = 0, b = 0;
        if (HMODULE nt = GetModuleHandleW(L"ntdll.dll"))
            if (auto f = reinterpret_cast<Fn>(GetProcAddress(nt, "RtlGetNtVersionNumbers")))
                f(&maj, &min, &b);
        return maj >= 10 ? (b & 0x0FFFFFFF) : 0u;
    }();
    return build;
}
inline HMODULE UxTheme() {
    static HMODULE m = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return m;
}
using AllowForWindowFn = BOOL(WINAPI*)(HWND, BOOL);
inline AllowForWindowFn AllowForWindow() {
    static AllowForWindowFn f = (AppModeFor(Build()) != AppMode::None && UxTheme())
        ? reinterpret_cast<AllowForWindowFn>(GetProcAddress(UxTheme(), MAKEINTRESOURCEA(133)))
        : nullptr;
    return f;
}
inline bool g_on = false;
}

// Once, at startup. False when this Windows has no dark scrollbars or
// the switch is off, and then nothing else here does anything.
inline bool Enable(bool wanted) {
    detail::g_on = false;
    if (!wanted) return false;
    const AppMode mode = AppModeFor(detail::Build());
    HMODULE ux = detail::UxTheme();
    if (mode == AppMode::None || !ux || !detail::AllowForWindow()) return false;
    FARPROC p135 = GetProcAddress(ux, MAKEINTRESOURCEA(135));
    if (!p135) return false;
    if (mode == AppMode::SetPreferredAppMode)
        reinterpret_cast<int(WINAPI*)(int)>(p135)(1);          // AllowDark
    else
        reinterpret_cast<BOOL(WINAPI*)(BOOL)>(p135)(TRUE);
    detail::g_on = true;
    return true;
}

inline bool On() { return detail::g_on; }

inline void Apply(HWND h) {
    if (!detail::g_on || !h) return;
    detail::AllowForWindow()(h, TRUE);
    SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
}

// Every scrolling window under `top` that can take the theme safely.
// Call after Theme::StyleStockControls, which de-themes the list views:
// this puts the dark theme back on them, and only them.
inline void Style(HWND top) {
    if (!detail::g_on || !top) return;
    EnumChildWindows(top, [](HWND c, LPARAM) -> BOOL {
        wchar_t cls[64] = {};
        if (GetClassNameW(c, cls, 64) <= 0) return TRUE;
        if (_wcsicmp(cls, WC_LISTVIEWW) == 0) { Apply(c); return TRUE; }
        if (IsSkippedClass(cls)) return TRUE;
        const LONG_PTR st = GetWindowLongPtrW(c, GWL_STYLE);
        if (st & (WS_VSCROLL | WS_HSCROLL)) Apply(c);
        return TRUE;
    }, 0);
}

} // namespace raduk::gui::darkbars
