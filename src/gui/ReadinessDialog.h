#pragma once
// ============================================================
// ReadinessDialog.h — Phase 68 steps 3-5
//
// Shown only when something is actually wrong. A list that appears
// every launch trains people to dismiss it unread, after which it
// costs attention and provides none.
//
// Rows refresh on a timer, so this is a working surface rather than
// a snapshot: stand at the mount, run Quick Align on the hand
// control, and watch the row turn green without restarting anything.
// That is the whole reason it is worth building as a dialog instead
// of a log line.
//
// Continue is always available. This is advice, not a gate - daytime
// bench work must not be trapped behind a checklist about the sky.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Theme.h"
#include "core/RigConfig.h"
#include <windows.h>
#include <commctrl.h>
#include <functional>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include "core/Readiness.h"

namespace raduk::gui {

// reevaluate is called on a timer to rebuild the report. Returns true
// if the user ticked every manual item before continuing, which the
// caller may use to reset the cord-wrap count.
// ── Dimming the other screens ─────────────────────────────────
//
// A borderless layered window over every monitor EXCEPT the one the
// dialog is on, so the readiness list is the only lit thing on a
// five-screen desk and cannot be missed or worked around by looking
// somewhere else. This is the safety part of a safety gate.
//
// WS_EX_NOACTIVATE matters: these must never take focus, or tabbing
// around the dialog would land on a black rectangle. WS_EX_TOOLWINDOW
// keeps them out of Alt-Tab. They are TOPMOST, so the dialog is raised
// above them after it is created.
//
// Deliberately NOT done by disabling input globally. The app keeps
// running and keeps drawing underneath -- a capture in progress is not
// interrupted by someone opening the checklist, and the mount can still
// be stopped. The gate is about attention, not about freezing the
// program; a modal that froze the UI cost most of an afternoon in
// Phase 84 and is not a mistake worth repeating.
namespace detail {

struct ScreenDim {
    std::vector<HWND> wins;

    void Create(HMONITOR skip) {
        static bool reg = false;
        static const wchar_t* kCls = L"RadukScreenDim";
        if (!reg) {
            WNDCLASSEXW wc = {};
            wc.cbSize        = sizeof(wc);
            wc.lpfnWndProc   = DefWindowProcW;
            wc.hInstance     = GetModuleHandleW(nullptr);
            wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
            wc.lpszClassName = kCls;
            RegisterClassExW(&wc);
            reg = true;
        }

        struct Ctx { ScreenDim* self; HMONITOR skip; const wchar_t* cls; };
        Ctx ctx{ this, skip, kCls };

        EnumDisplayMonitors(nullptr, nullptr,
            [](HMONITOR hm, HDC, LPRECT, LPARAM lp) -> BOOL {
                auto* c = reinterpret_cast<Ctx*>(lp);
                if (hm == c->skip) return TRUE;
                MONITORINFO mi = { sizeof(mi) };
                if (!GetMonitorInfo(hm, &mi)) return TRUE;
                HWND h = CreateWindowExW(
                    WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
                    WS_EX_TOOLWINDOW,
                    c->cls, L"", WS_POPUP,
                    mi.rcMonitor.left, mi.rcMonitor.top,
                    mi.rcMonitor.right  - mi.rcMonitor.left,
                    mi.rcMonitor.bottom - mi.rcMonitor.top,
                    nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
                if (!h) return TRUE;
                // Dark enough to read as "not this one", light enough
                // that you can still see a slew is happening behind it.
                SetLayeredWindowAttributes(h, 0, 170, LWA_ALPHA);
                ShowWindow(h, SW_SHOWNOACTIVATE);
                c->self->wins.push_back(h);
                return TRUE;
            }, reinterpret_cast<LPARAM>(&ctx));
    }

    void Destroy() {
        for (HWND h : wins) if (h) DestroyWindow(h);
        wins.clear();
    }
};

} // namespace detail

inline bool ShowReadinessDialog(
        HWND owner, HWND panel,
        std::function<raduk::core::ReadyReport()> reevaluate,
        std::function<void(const std::string&)> openHelp = nullptr) {

    using namespace raduk::core;

    static bool registered = false;
    static const wchar_t* kClass = L"RadukReadiness";
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = DefWindowProcW;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(Theme::NV(Theme::Rgb::DEEP));
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        registered = true;
    }

    ReadyReport report = reevaluate();

    // Count the rows a human has to answer before sizing, because the
    // list height is whatever is left after them.
    int manualCount = 0;
    for (const auto& it : report.items)
        if (it.state == ReadyState::Manual) ++manualCount;

    // Sized from the screen it will appear on, not fixed at 620x470.
    // This is the one dialog that must be impossible to overlook, and
    // on a 1080p monitor the old size was a sixth of the width.
    HMONITOR hmon = owner ? MonitorFromWindow(owner, MONITOR_DEFAULTTOPRIMARY)
                          : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hmon, &mi);
    const int monW = mi.rcWork.right  - mi.rcWork.left;
    const int monH = mi.rcWork.bottom - mi.rcWork.top;

    const int DW = (std::max)(620, (std::min)(1100, (int)(monW * 0.55)));
    const int DH = (std::max)(470, (std::min)(900,  (int)(monH * 0.70)));

    // Centred on that monitor rather than on the owner window: the
    // owner may be maximised across a different screen entirely.
    int dx = mi.rcWork.left + (monW - DW) / 2;
    int dy = mi.rcWork.top  + (monH - DH) / 2;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass,
                               L"Before you start",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                               dx, dy, DW, DH,
                               owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!dlg) return false;

    enum { ID_LIST = 4001, ID_CONTINUE, ID_RECHECK, ID_HELP, ID_ADC, ID_TICK0 };
    const UINT_PTR TIMER_ID = 1;

    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                  int x, int y, int w, int h, int id) -> HWND {
        return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                               x, y, w, h, dlg, (HMENU)(INT_PTR)id,
                               GetModuleHandleW(nullptr), nullptr);
    };

    mk(L"STATIC",
       L"Some things are not ready. Rows update by themselves - fix one at "
       L"the mount and watch it turn green, no restart needed.",
       0, 14, 10, DW - 34, 34, -1);

    // Automatic rows in a read-only edit, so long explanations wrap and
    // Whatever is left between the title and the tick rows. Fixed at
    // 250 before, which left a growing band of nothing once the dialog
    // could be larger than 470 tall.
    const int LIST_H = (std::max)(160,
        (DH - 72) - 16 - (manualCount * 22) - 24 - 48 - 8);

    // the whole thing stays selectable for copying into a bug report.
    HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                14, 48, DW - 34, LIST_H, dlg,
                                (HMENU)(INT_PTR)ID_LIST,
                                GetModuleHandleW(nullptr), nullptr);

    // Manual items get real checkboxes: they are the only rows a human
    // can actually answer.
    std::vector<HWND> ticks;
    int ty = 48 + LIST_H + 10;
    int manualIdx = 0;
    for (const auto& it : report.items) {
        if (it.state != ReadyState::Manual) continue;
        std::string label = it.name + " - " + it.detail;
        wchar_t w[256] = {};
        MultiByteToWideChar(CP_UTF8, 0, label.c_str(), -1, w, 255);
        ticks.push_back(mk(L"BUTTON", w, BS_AUTOCHECKBOX, 16, ty, DW - 44, 20,
                           ID_TICK0 + manualIdx));
        ty += 22;
        manualIdx++;
    }

    // ── A statement about the rig, not about tonight ────────────
    //
    // The ticks above are per-night confirmations: dust cap off, tube
    // cooled, cables dressed. This one is different in kind. It says
    // what is bolted to the imaging train, it is remembered between
    // sessions, and it never blocks Continue -- not owning a corrector
    // is not a readiness failure.
    //
    // It lives here anyway because this is the one screen that already
    // asks about the physical train, and because an ADC readout with no
    // ADC in the train is worse than no readout: the alignment window
    // would show a residual and invite you to adjust a part that is not
    // there.
    HWND adcTick = mk(L"BUTTON",
        L"ADC fitted to the imaging train  -  enables the ADC arrow, the "
        L"FOCUSER readout and the Shift+A alignment window",
        BS_AUTOCHECKBOX, 16, ty, DW - 44, 20, ID_ADC);
    if (adcTick)
        SendMessageW(adcTick, BM_SETCHECK,
                     raduk::core::RigConfig::Get().adcFitted ? BST_CHECKED
                                                             : BST_UNCHECKED,
                     0);
    ty += 24;

    // Opens the Help section for the first failing row. Naming a
    // heading is only half the job if the reader then has to find it.
    if (openHelp)
        mk(L"BUTTON", L"Explain this", BS_PUSHBUTTON, 14, DH - 72, 110, 26,
           ID_HELP);
    mk(L"BUTTON", L"Re-check now", BS_PUSHBUTTON, DW - 260, DH - 72, 110, 26,
       ID_RECHECK);
    mk(L"BUTTON", L"Continue", BS_DEFPUSHBUTTON, DW - 140, DH - 72, 110, 26,
       ID_CONTINUE);

    struct State {
        bool done = false;
        HWND list = nullptr;
        std::function<ReadyReport()>* reeval = nullptr;
        std::function<void()> onHelp;
    };
    State st; st.list = list; st.reeval = &reevaluate;
    if (openHelp) {
        st.onHelp = [&openHelp, &reevaluate]() {
            ReadyReport cur = reevaluate();
            for (const auto& it : cur.items)
                if (it.state == ReadyState::Fail && !it.helpAnchor.empty()) {
                    openHelp(it.helpAnchor);
                    return;
                }
            for (const auto& it : cur.items)
                if (it.state != ReadyState::Pass && !it.helpAnchor.empty()) {
                    openHelp(it.helpAnchor);
                    return;
                }
        };
    }
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&st);

    auto render = [](HWND listCtl, const ReadyReport& rep) {
        std::string body;
        // Failures first: the thing stopping you should not be third.
        for (int pass = 0; pass < 3; pass++) {
            for (const auto& it : rep.items) {
                bool want = (pass == 0 && it.state == ReadyState::Fail)
                         || (pass == 1 && (it.state == ReadyState::Warn ||
                                           it.state == ReadyState::Unknown))
                         || (pass == 2 && it.state == ReadyState::Pass);
                if (!want) continue;
                body += std::string("[") + ReadyStateName(it.state) + "]  "
                      + it.name + "  -  " + it.detail + "\r\n";
                if (!it.helpAnchor.empty() && it.state != ReadyState::Pass)
                    body += std::string("          Help: ") + it.helpAnchor + "\r\n";
            }
        }
        std::wstring w(body.begin(), body.end());
        SetWindowTextW(listCtl, w.c_str());
    };
    render(list, report);

    struct Sub {
        static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp,
                                     UINT_PTR, DWORD_PTR ref) {
            auto* s = (State*)GetWindowLongPtrW(h, GWLP_USERDATA);
            auto* rf = (std::function<void()>*)ref;
            if (!s) return DefSubclassProc(h, m, wp, lp);
            if (m == WM_TIMER) { if (rf) (*rf)(); return 0; }
            if (m == WM_COMMAND) {
                int id = LOWORD(wp);
                if (id == ID_RECHECK)  { if (rf) (*rf)(); return 0; }
                if (id == ID_HELP)     { if (s->onHelp) s->onHelp(); return 0; }
                if (id == ID_ADC) {
                    // Written the moment it is toggled, not on
                    // Continue. Escape and the close box both end this
                    // dialog too, and a setting that only persists down
                    // one of three exits is one that sometimes silently
                    // does not.
                    auto& rg = raduk::core::RigConfig::Get();
                    rg.adcFitted = (SendMessageW((HWND)lp, BM_GETCHECK, 0, 0)
                                    == BST_CHECKED);
                    rg.Save();
                    return 0;
                }
                if (id == ID_CONTINUE) { s->done = true; return 0; }
            }
            if (m == WM_CLOSE) { s->done = true; return 0; }
            return DefSubclassProc(h, m, wp, lp);
        }
    };

    std::function<void()> refresh = [&]() {
        ReadyReport fresh = reevaluate();
        render(list, fresh);
    };
    SetWindowSubclass(dlg, Sub::Proc, 1, (DWORD_PTR)&refresh);
    SetTimer(dlg, TIMER_ID, 2000, nullptr);   // poll while it is open

    if (owner) EnableWindow(owner, FALSE);
    if (panel) EnableWindow(panel, FALSE);

    // Grey out every screen except this one, then put the dialog above
    // the dimmers -- they are TOPMOST, so without this it would be
    // behind its own overlay.
    detail::ScreenDim dim;
    dim.Create(hmon);
    SetWindowPos(dlg, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(dlg);
    SetFocus(dlg);

    MSG msg = {};
    while (!st.done) {
        if (!GetMessageW(&msg, nullptr, 0, 0)) break;
        // Escape closes the gate without ticking anything, so it
        // returns false and the manual items stay unconfirmed. A
        // keyboard way out matters in the dark; silently counting it
        // as confirmation would not.
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            st.done = true;
            continue;
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    bool allTicked = !ticks.empty();
    for (HWND t : ticks)
        if (SendMessageW(t, BM_GETCHECK, 0, 0) != BST_CHECKED) allTicked = false;

    KillTimer(dlg, TIMER_ID);
    dim.Destroy();
    if (owner) EnableWindow(owner, TRUE);
    if (panel) EnableWindow(panel, TRUE);
    DestroyWindow(dlg);
    if (owner) SetForegroundWindow(owner);
    return allTicked;
}

} // namespace raduk::gui
