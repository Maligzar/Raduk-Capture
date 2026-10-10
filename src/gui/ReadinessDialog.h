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
#include <windows.h>
#include <commctrl.h>
#include <functional>
#include <cstdio>
#include <string>
#include <vector>
#include "core/Readiness.h"

namespace raduk::gui {

// reevaluate is called on a timer to rebuild the report. Returns true
// if the user ticked every manual item before continuing, which the
// caller may use to reset the cord-wrap count.
inline bool ShowReadinessDialog(
        HWND owner, HWND panel,
        std::function<raduk::core::ReadyReport()> reevaluate) {

    using namespace raduk::core;

    static bool registered = false;
    static const wchar_t* kClass = L"RadukReadiness";
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

    ReadyReport report = reevaluate();

    const int DW = 620, DH = 470;
    RECT wr = {};
    if (owner) GetWindowRect(owner, &wr);
    int dx = wr.left + ((wr.right - wr.left) - DW) / 2;
    int dy = wr.top  + ((wr.bottom - wr.top) - DH) / 2;
    if (dx < 0 || dx > 10000) dx = 200;
    if (dy < 0 || dy > 10000) dy = 120;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass,
                               L"Before you start",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                               dx, dy, DW, DH,
                               owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!dlg) return false;

    enum { ID_LIST = 4001, ID_CONTINUE, ID_RECHECK, ID_TICK0 };
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
    // the whole thing stays selectable for copying into a bug report.
    HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                14, 48, DW - 34, 250, dlg,
                                (HMENU)(INT_PTR)ID_LIST,
                                GetModuleHandleW(nullptr), nullptr);

    // Manual items get real checkboxes: they are the only rows a human
    // can actually answer.
    std::vector<HWND> ticks;
    int ty = 306;
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

    mk(L"BUTTON", L"Re-check now", BS_PUSHBUTTON, DW - 260, DH - 72, 110, 26,
       ID_RECHECK);
    mk(L"BUTTON", L"Continue", BS_DEFPUSHBUTTON, DW - 140, DH - 72, 110, 26,
       ID_CONTINUE);

    struct State {
        bool done = false;
        HWND list = nullptr;
        std::function<ReadyReport()>* reeval = nullptr;
    };
    State st; st.list = list; st.reeval = &reevaluate;
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
    SetForegroundWindow(dlg);

    MSG msg = {};
    while (!st.done) {
        if (!GetMessageW(&msg, nullptr, 0, 0)) break;
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    bool allTicked = !ticks.empty();
    for (HWND t : ticks)
        if (SendMessageW(t, BM_GETCHECK, 0, 0) != BST_CHECKED) allTicked = false;

    KillTimer(dlg, TIMER_ID);
    if (owner) EnableWindow(owner, TRUE);
    if (panel) EnableWindow(panel, TRUE);
    DestroyWindow(dlg);
    if (owner) SetForegroundWindow(owner);
    return allTicked;
}

} // namespace raduk::gui
