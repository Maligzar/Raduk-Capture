// ============================================================
//  ConfirmToast — a large, self-dismissing confirmation
//
//  For actions whose whole purpose is to change something on disk, and
//  which currently say so only in a log file nobody has open. Saving
//  the window layout is the case this was built for: Ctrl+Shift+End
//  wrote the file and gave no sign it had, so the only way to find out
//  whether the arrangement had been captured was to restart the app and
//  see.
//
//  Deliberately big and deliberately slow to leave. This is read from
//  across a room, at night, by someone who has just pressed a key with
//  one hand while holding a focuser with the other.
//
//  Non-modal and WS_EX_NOACTIVATE: it must never steal focus from
//  whatever was being done, and it must never need dismissing.
// ============================================================

#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include "Theme.h"

namespace raduk::gui {

class ConfirmToast {
public:
    // ok decides the accent colour: this is used for failures too, and
    // a red banner saying the file could not be written is worth more
    // than the green one saying it could.
    static void Show(HWND owner, const std::wstring& title,
                     const std::vector<std::wstring>& lines,
                     bool ok, UINT dwellMs = 5000) {
        auto* st = new State{ title, lines, ok };

        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc = {};
            wc.cbSize        = sizeof(wc);
            wc.lpfnWndProc   = Proc;
            wc.hInstance     = GetModuleHandleW(nullptr);
            wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = nullptr;
            wc.lpszClassName = L"RadukConfirmToast";
            RegisterClassExW(&wc);
            registered = true;
        }

        // Centred on the screen the owner is on, not the primary. On a
        // five-screen desk the primary may not be the one being looked
        // at, and a confirmation nobody sees is not one.
        MONITORINFO mi = { sizeof(mi) };
        HMONITOR hm = MonitorFromWindow(owner ? owner : GetForegroundWindow(),
                                        MONITOR_DEFAULTTOPRIMARY);
        if (!GetMonitorInfo(hm, &mi)) return;

        const int mw = mi.rcWork.right - mi.rcWork.left;
        const int mh = mi.rcWork.bottom - mi.rcWork.top;
        const int w  = (std::max)(420, (std::min)(900, (int)(mw * 0.42)));
        const int h  = (std::max)(170, (std::min)(380, (int)(mh * 0.26)));
        const int x  = mi.rcWork.left + (mw - w) / 2;
        const int y  = mi.rcWork.top  + (mh - h) / 3;   // a third down reads
                                                        // better than centred

        HWND hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"RadukConfirmToast", L"", WS_POPUP,
            x, y, w, h, owner, nullptr, GetModuleHandleW(nullptr), st);
        if (!hwnd) { delete st; return; }

        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        SetTimer(hwnd, 1, dwellMs, nullptr);
    }

private:
    struct State {
        std::wstring title;
        std::vector<std::wstring> lines;
        bool ok;
    };

    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
        State* st = reinterpret_cast<State*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(h, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }
        switch (m) {
            case WM_TIMER:
                KillTimer(h, 1);
                DestroyWindow(h);
                return 0;

            // Clicking it dismisses early. Someone who has read it
            // should not have to wait out the rest of the dwell.
            case WM_LBUTTONDOWN:
                DestroyWindow(h);
                return 0;

            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC dc = BeginPaint(h, &ps);
                RECT rc; GetClientRect(h, &rc);
                if (st) Paint(dc, rc, *st);
                EndPaint(h, &ps);
                return 0;
            }
            case WM_ERASEBKGND: return 1;
            case WM_DESTROY:
                delete st;
                SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                return 0;
        }
        return DefWindowProcW(h, m, wp, lp);
    }

    static void Paint(HDC dc, const RECT& rc, const State& st) {
        const COLORREF accent = st.ok ? Theme::NV(Theme::Rgb::ACCENT3)
                                      : Theme::NV(Theme::Rgb::ACCENT4);

        HBRUSH bg = CreateSolidBrush(Theme::NV(Theme::Rgb::DEEP));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);

        // A thick accent bar down the left edge rather than a border all
        // round: it reads as a status colour at a glance without the
        // window looking like a dialog that wants answering.
        RECT bar = { 0, 0, (LONG)((std::max)(6, (int)(rc.right / 90))), rc.bottom };
        HBRUSH ab = CreateSolidBrush(accent);
        FillRect(dc, &bar, ab);
        DeleteObject(ab);

        HPEN   pen = CreatePen(PS_SOLID, 1, Theme::NV(RGB(40, 54, 72)));
        HGDIOBJ op = SelectObject(dc, pen);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, 0, 0, rc.right, rc.bottom);
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(pen);

        SetBkMode(dc, TRANSPARENT);
        const int pad = (std::max)(18, (int)rc.right / 24);
        int y = pad;

        const int titlePx = (std::max)(22, (std::min)(46, (int)rc.bottom / 5));
        HFONT tf = CreateFontW(-titlePx, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH, L"Barlow Condensed");
        HGDIOBJ of = SelectObject(dc, tf);
        SetTextColor(dc, accent);
        RECT tr = { pad, y, (LONG)rc.right - pad, (LONG)(y + titlePx + 6) };
        DrawTextW(dc, st.title.c_str(), -1, &tr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, of);
        DeleteObject(tf);
        y += titlePx + 10;

        const int bodyPx = (std::max)(13, (std::min)(24, titlePx * 5 / 9));
        HFONT bf = CreateFontW(-bodyPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH, L"Space Mono");
        of = SelectObject(dc, bf);
        SetTextColor(dc, Theme::NV(Theme::Rgb::TEXT));
        for (const std::wstring& l : st.lines) {
            if (y + bodyPx > (int)rc.bottom - pad / 2) break;
            RECT lr = { pad, y, (LONG)rc.right - pad, (LONG)(y + bodyPx + 6) };
            DrawTextW(dc, l.c_str(), -1, &lr,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            y += bodyPx + 6;
        }
        SelectObject(dc, of);
        DeleteObject(bf);
    }
};

} // namespace raduk::gui
