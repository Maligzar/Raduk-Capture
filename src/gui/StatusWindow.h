#pragma once
// ============================================================
// StatusWindow.h — Phase 42f
//
// HDD / BUF / FPS / LOST, in a window of its own.
//
// It used to be a section of the control panel, and it was the last
// thing left there that the panel painted ITSELF rather than building
// out of child controls. That turned out to be three bugs wearing one
// coat:
//
//   * It sat on a different plane. Child controls are clipped and
//     composited over the parent; the status strip was painted onto the
//     parent's own DC, underneath them. Every collapse toggle runs
//     RebuildControls, which destroys and recreates every child, and
//     the parent's already-painted strip stayed visible in the space
//     the children vacated. That was the "bleeding into everything
//     else".
//
//   * Its header was drawn by DrawSectionHeader in GDI while every
//     other header is a child control, so collapsed it did not line up
//     with the rest and its font was different.
//
//   * Its repaint invalidated the whole panel twice a second, and an
//     older invalidation aimed at the since-departed histogram used
//     unscrolled coordinates, so once the panel scrolled the clip
//     region and the drawing were in different places.
//
// None of that is worth repairing in place. The readout belongs beside
// the other telemetry, on the screen with the charts.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <algorithm>
#include <cstdio>
#include "Theme.h"

#pragma comment(lib, "dwmapi.lib")

namespace raduk::gui {

class StatusWindow {
public:
    struct Data {
        float fps          = 0.0f;
        int   framesLost   = 0;
        float bufferPct    = 0.0f;
        int   bufferFrames = 0;
        int   bufferTotal  = 512;
        float diskUsedGB   = 0.0f;
        bool  recording    = false;
    };

    bool Create(HWND parent, int x, int y, int w, int h) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;              // painted whole in WM_PAINT
        wc.lpszClassName = L"RadukStatus";
        RegisterClassExW(&wc);

        m_hwnd = CreateWindowExW(
            0, L"RadukStatus", L"Status",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX | WS_MAXIMIZEBOX,
            x, y, w, h, parent, nullptr, GetModuleHandle(nullptr), this);
        if (!m_hwnd) return false;

        BOOL dark = TRUE;
        DwmSetWindowAttribute(m_hwnd, 20, &dark, sizeof(dark));
        DwmSetWindowAttribute(m_hwnd, 19, &dark, sizeof(dark));
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        return true;
    }

    void Destroy() { if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; } }

    HWND Handle() const { return m_hwnd; }
    bool IsVisible() const { return m_hwnd && IsWindowVisible(m_hwnd); }
    void Show()   { if (m_hwnd) { ShowWindow(m_hwnd, SW_SHOW); SetForegroundWindow(m_hwnd); } }
    void Hide()   { if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE); }
    void Toggle() { if (!m_hwnd) return; IsVisible() ? Hide() : Show(); }

    void Update(const Data& d) {
        m_data = d;
        if (IsVisible()) InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void ToggleFullscreen() {
        if (!m_hwnd) return;
        if (!m_fullscreen) {
            m_savedPlacement.length = sizeof(WINDOWPLACEMENT);
            GetWindowPlacement(m_hwnd, &m_savedPlacement);
            MONITORINFO mi = { sizeof(mi) };
            if (!GetMonitorInfo(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi))
                return;
            SetWindowLongW(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(m_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            m_fullscreen = true;
        } else {
            SetWindowLongW(m_hwnd, GWL_STYLE,
                           WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX |
                           WS_MAXIMIZEBOX | WS_VISIBLE);
            SetWindowPlacement(m_hwnd, &m_savedPlacement);
            SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            m_fullscreen = false;
        }
        SetFocus(m_hwnd);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

private:
    static HFONT MakeFont(int px, bool bold, const char* face) {
        return CreateFontA(-px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                           FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
    }

    static void Fill(HDC hdc, int x, int y, int w, int h, COLORREF c) {
        RECT r = { x, y, x + w, y + h };
        HBRUSH b = Theme::Brush(c);
        FillRect(hdc, &r, b);
    }

    // A labelled progress bar. Same shape as the one the control panel
    // drew, scaled to whatever size this window is given.
    static void Bar(HDC hdc, int x, int y, int w, int h, float pct,
                    COLORREF fill, COLORREF bg, const char* label,
                    COLORREF labelClr, int fontPx) {
        Fill(hdc, x, y, w, h, bg);
        if (pct > 0.0f) {
            if (pct > 100.0f) pct = 100.0f;
            Fill(hdc, x, y, (int)(w * pct / 100.0f), h, fill);
        }
        if (label && *label) {
            HFONT f = MakeFont(fontPx, false, "Space Mono");
            HGDIOBJ old = SelectObject(hdc, f);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, labelClr);
            RECT tr = { x + 4, y, x + w - 4, y + h };
            DrawTextA(hdc, label, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, old);
            DeleteObject(f);
        }
    }

    void Draw(HDC hdc, const RECT& cr) {
        const int W = cr.right, H = cr.bottom;
        Fill(hdc, 0, 0, W, H, RGB(4, 8, 15));
        if (W < 120 || H < 50) return;

        // Everything scales off the window, because this gets a screen
        // of its own and a fixed 20px bar would waste it.
        const int pad   = (std::max)(8,  W / 60);
        const int rowH  = (std::max)(18, (H - pad * 3) / 2);
        const int fontPx= (std::max)(11, (std::min)(30, rowH / 2));
        const int lblW  = fontPx * 4;
        const int rightW= fontPx * 8;
        const int barW  = W - pad * 2 - lblW - rightW - 8;
        if (barW < 40) return;

        HFONT lf = MakeFont(fontPx, true,  "Barlow Condensed");
        HFONT vf = MakeFont(fontPx, false, "Space Mono");
        SetBkMode(hdc, TRANSPARENT);

        struct Row {
            const char* label;
            float       pct;
            COLORREF    fill;
            char        barText[40];
            const char* rightLabel;
            char        rightValue[24];
            COLORREF    rightClr;
        } rows[2] = {};

        // HDD + FPS
        rows[0].label = "HDD";
        rows[0].pct   = m_data.recording
                      ? (std::min)(100.0f, m_data.diskUsedGB / 500.0f * 100.0f)
                      : 0.0f;
        rows[0].fill  = Theme::NV(RGB(0, 200, 110));
        sprintf_s(rows[0].barText, "%.3f GB", m_data.diskUsedGB);
        rows[0].rightLabel = "FPS";
        sprintf_s(rows[0].rightValue, "%.1f", m_data.fps);
        rows[0].rightClr = Theme::NV(RGB(0, 229, 255));

        // BUF + LOST
        rows[1].label = "BUF";
        rows[1].pct   = m_data.bufferPct;
        rows[1].fill  = (m_data.bufferPct > 80.0f) ? Theme::NV(RGB(255, 60, 90))
                      : (m_data.bufferPct > 50.0f) ? Theme::NV(RGB(255, 170, 0))
                                                   : Theme::NV(RGB(0, 200, 110));
        sprintf_s(rows[1].barText, "%d / %d slots",
                  m_data.bufferFrames,
                  m_data.bufferTotal > 0 ? m_data.bufferTotal : 512);
        rows[1].rightLabel = "LOST";
        sprintf_s(rows[1].rightValue, "%d", m_data.framesLost);
        rows[1].rightClr = m_data.framesLost > 0 ? Theme::NV(RGB(255, 60, 90))
                                                 : Theme::NV(RGB(120, 136, 156));

        int y = pad;
        for (const Row& r : rows) {
            HGDIOBJ old = SelectObject(hdc, lf);
            SetTextColor(hdc, Theme::NV(RGB(120, 136, 156)));
            RECT lr = { pad, y, pad + lblW, y + rowH };
            DrawTextA(hdc, r.label, -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, old);

            const int bx = pad + lblW;
            Bar(hdc, bx, y, barW, rowH, r.pct, r.fill, Theme::NV(RGB(4, 10, 18)),
                r.barText, Theme::NV(RGB(200, 232, 240)), fontPx);

            old = SelectObject(hdc, lf);
            SetTextColor(hdc, Theme::NV(RGB(120, 136, 156)));
            RECT rl = { bx + barW + 6, y, bx + barW + 6 + rightW / 2, y + rowH };
            DrawTextA(hdc, r.rightLabel, -1, &rl, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, old);

            old = SelectObject(hdc, vf);
            SetTextColor(hdc, r.rightClr);
            RECT rv = { bx + barW + 6 + rightW / 2, y, W - pad, y + rowH };
            DrawTextA(hdc, r.rightValue, -1, &rv, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, old);

            y += rowH + pad;
        }

        DeleteObject(lf);
        DeleteObject(vf);
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        StatusWindow* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<StatusWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->m_hwnd = hwnd;
        } else {
            self = reinterpret_cast<StatusWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

        switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT cr; GetClientRect(hwnd, &cr);
                self->Draw(hdc, cr);
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_SIZE:
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case WM_KEYDOWN:
                if (wp == VK_F11) { self->ToggleFullscreen(); return 0; }
                if (wp == VK_ESCAPE) {
                    if (self->m_fullscreen) self->ToggleFullscreen();
                    else                    self->Hide();
                    return 0;
                }
                break;
            case WM_CLOSE:
                self->Hide();
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    HWND            m_hwnd = nullptr;
    Data            m_data;
    bool            m_fullscreen = false;
    WINDOWPLACEMENT m_savedPlacement = {};
};

} // namespace raduk::gui
