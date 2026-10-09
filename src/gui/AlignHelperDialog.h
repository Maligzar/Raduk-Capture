#pragma once
// ============================================================
// AlignHelperDialog.h — Phase 67 step 4
//
// Shown when the mount reports itself unaligned: Quick Align needs
// the tube levelled and pointed north, and north is an abstraction
// at midnight in a front yard.
//
// Asks one question the first time — which way the scope lies from
// where you stand — then translates "point it north" into the
// landmarks in front of you. The answer is saved, so it is asked
// once per rig rather than once per session.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <string>
#include "core/RigConfig.h"
#include "core/AlignGuide.h"

namespace raduk::gui {

// Modal. Returns true if the user asked not to be shown it again.
inline bool ShowQuickAlignHelper(HWND owner, HWND panel) {
    auto& rig = raduk::core::RigConfig::Get();

    static bool registered = false;
    static const wchar_t* kClass = L"RadukAlignHelper";
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = DefWindowProcW;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(7, 13, 22));
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        registered = true;
    }

    const int DW = 500, DH = 340;
    RECT wr = {};
    if (owner) GetWindowRect(owner, &wr);
    int dx = wr.left + ((wr.right - wr.left) - DW) / 2;
    int dy = wr.top  + ((wr.bottom - wr.top) - DH) / 2;
    if (dx < 0 || dx > 10000) dx = 220;
    if (dy < 0 || dy > 10000) dy = 160;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass,
                               L"Mount is not aligned",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                               dx, dy, DW, DH,
                               owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!dlg) return false;

    enum { ID_DIR = 3001, ID_GUIDE, ID_DONE, ID_NEVER };

    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                  int x, int y, int w, int h, int id) -> HWND {
        return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                               x, y, w, h, dlg, (HMENU)(INT_PTR)id,
                               GetModuleHandleW(nullptr), nullptr);
    };

    int y = 12;
    mk(L"STATIC",
       L"The mount reports that it is not aligned, so it will not track. "
       L"Run Align > Quick Align on the hand control - it needs no stars, "
       L"only a level tube pointed north.",
       0, 14, y, DW - 34, 50, -1);
    y += 54;

    mk(L"STATIC", L"Standing where you normally sit, the scope is to my:",
       0, 14, y + 4, 300, 20, -1);
    HWND combo = CreateWindowExW(0, L"COMBOBOX", L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                 CBS_DROPDOWNLIST,
                                 320, y, 150, 220, dlg,
                                 (HMENU)(INT_PTR)ID_DIR,
                                 GetModuleHandleW(nullptr), nullptr);
    // The scope's direction from the observer is the REVERSE of the
    // stored bearing, which runs mount -> observer. Asking it the way
    // round the user experiences it avoids making them do that flip.
    const wchar_t* dirs[] = {L"north", L"north-east", L"east", L"south-east",
                             L"south", L"south-west", L"west", L"north-west"};
    for (auto d : dirs) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)d);
    int preset = 4;   // scope to the south is the common desk layout
    if (rig.ObserverBearingSet()) {
        double scopeFromObs = rig.observerBearingDeg + 180.0;
        if (scopeFromObs >= 360.0) scopeFromObs -= 360.0;
        preset = ((int)((scopeFromObs + 22.5) / 45.0)) % 8;
    }
    SendMessageW(combo, CB_SETCURSEL, preset, 0);
    y += 34;

    HWND guide = mk(L"STATIC", L"", 0, 14, y, DW - 34, 110, ID_GUIDE);
    y += 114;

    mk(L"BUTTON", L"Got it", BS_DEFPUSHBUTTON, DW - 230, y, 100, 26, ID_DONE);
    mk(L"BUTTON", L"Don't show again", BS_PUSHBUTTON, DW - 122, y, 110, 26, ID_NEVER);

    struct State { bool done = false; bool never = false; HWND guide = nullptr;
                   HWND combo = nullptr; };
    State st; st.guide = guide; st.combo = combo;
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&st);

    auto refresh = [&]() {
        int sel = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (sel < 0) sel = 4;
        double scopeFromObs = sel * 45.0;
        // Stored bearing is mount -> observer, the reverse.
        double bearing = scopeFromObs + 180.0;
        if (bearing >= 360.0) bearing -= 360.0;

        auto g = raduk::core::BuildQuickAlignGuidance(bearing);
        std::string text = "North is " + g.northPhrase + ".\r\n\r\n"
                         + g.action + "\r\n\r\n" + g.note;
        wchar_t w[512] = {};
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, w, 511);
        SetWindowTextW(guide, w);

        rig.observerBearingDeg = bearing;
    };
    refresh();

    struct Sub {
        static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp,
                                     UINT_PTR, DWORD_PTR ref) {
            auto* s = (State*)GetWindowLongPtrW(h, GWLP_USERDATA);
            auto* fn = (std::function<void()>*)ref;
            if (!s) return DefSubclassProc(h, m, wp, lp);
            if (m == WM_COMMAND) {
                int id = LOWORD(wp);
                if (id == ID_DIR && HIWORD(wp) == CBN_SELCHANGE) {
                    if (fn) (*fn)();        // live update as they choose
                    return 0;
                }
                if (id == ID_DONE)  { s->done = true; return 0; }
                if (id == ID_NEVER) { s->never = true; s->done = true; return 0; }
            }
            if (m == WM_CLOSE) { s->done = true; return 0; }
            return DefSubclassProc(h, m, wp, lp);
        }
    };
    std::function<void()> refreshFn = refresh;
    SetWindowSubclass(dlg, Sub::Proc, 1, (DWORD_PTR)&refreshFn);

    if (owner) EnableWindow(owner, FALSE);
    if (panel) EnableWindow(panel, FALSE);
    SetForegroundWindow(dlg);
    SetFocus(combo);

    MSG msg = {};
    while (!st.done) {
        if (!GetMessageW(&msg, nullptr, 0, 0)) break;
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (owner) EnableWindow(owner, TRUE);
    if (panel) EnableWindow(panel, TRUE);
    DestroyWindow(dlg);
    if (owner) SetForegroundWindow(owner);

    rig.Save();   // keep the bearing; it is asked once per rig
    return st.never;
}

} // namespace raduk::gui
