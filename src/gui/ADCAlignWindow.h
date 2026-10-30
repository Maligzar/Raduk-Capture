// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// ============================================================
//  ADCAlignWindow — dialling in an atmospheric dispersion corrector
//
//  The ADC advisor (FOCUSER panel, and the arrow on the preview) tells
//  you how much dispersion the atmosphere is producing at the current
//  altitude. It is a prediction from a model and it never looks at the
//  image, so it cannot tell you whether the corrector you fitted is
//  actually doing its job.
//
//  This window shows the dispersion MEASURED in the live frame, which
//  is what is left after the corrector has done whatever it is
//  currently doing. Turn the prisms, watch the residual fall, stop when
//  it stops falling.
//
//  The arithmetic lives in core/ADCAlignment.h, driven by synthetic
//  frames in tests/adc_alignment.cpp. This file is only the display.
// ============================================================

#pragma once

#include <windows.h>
#include <dwmapi.h>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <vector>
#include "Theme.h"
#include "core/ADCAlignment.h"

#pragma comment(lib, "dwmapi.lib")

namespace raduk::gui {

class ADCAlignWindow {
public:
    struct Data {
        raduk::core::DispersionMeasurement measured;  // smoothed, from the live frame
        float predictedArcsec = 0.0f;   // what ADCAssistant expects at this altitude
        float altitudeDeg     = 0.0f;
        int   samples         = 0;      // frames in the rolling window
        bool  frameAvailable  = false;
    };

    bool Create(HWND parent, int x, int y, int w, int h) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"RadukADCAlign";
        RegisterClassExW(&wc);

        m_hwnd = CreateWindowExW(
            0, L"RadukADCAlign", L"ADC Alignment",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX | WS_MAXIMIZEBOX,
            x, y, w, h, parent, nullptr, GetModuleHandle(nullptr), this);
        if (!m_hwnd) return false;

        BOOL dark = TRUE;
        DwmSetWindowAttribute(m_hwnd, 20, &dark, sizeof(dark));
        DwmSetWindowAttribute(m_hwnd, 19, &dark, sizeof(dark));

        // Unlike the other telemetry windows this one starts hidden.
        // It is a job you do once at the start of a session, not
        // something you watch all night, and a screen is worth more to
        // anything you do watch.
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
        if (d.measured.valid) {
            m_trend.push_back(d.measured.magArcsec);
            if ((int)m_trend.size() > TREND_MAX) m_trend.erase(m_trend.begin());
        }
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
            m_fullscreen = false;
        }
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

private:
    static constexpr int TREND_MAX = 300;

    // Below this the residual is smaller than the seeing it was
    // measured through, and further dialling is chasing noise.
    static constexpr float ALIGNED_ARCSEC = 0.15f;

    static HFONT MakeFont(int px, bool bold, const char* face) {
        return CreateFontA(-px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                           FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
    }

    static void Fill(HDC hdc, int x, int y, int w, int h, COLORREF c) {
        RECT r = { x, y, x + w, y + h };
        FillRect(hdc, &r, Theme::Brush(c));
    }

    static void TextAt(HDC hdc, int x, int y, int w, int h, const char* s,
                       COLORREF clr, int px, bool bold, const char* face,
                       UINT align) {
        HFONT f = MakeFont(px, bold, face);
        HGDIOBJ old = SelectObject(hdc, f);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, clr);
        RECT r = { x, y, x + w, y + h };
        DrawTextA(hdc, s, -1, &r, align | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, old);
        DeleteObject(f);
    }

    static void Line(HDC hdc, int x0, int y0, int x1, int y1,
                     COLORREF c, int width) {
        HPEN p = CreatePen(PS_SOLID, width, c);
        HGDIOBJ old = SelectObject(hdc, p);
        MoveToEx(hdc, x0, y0, nullptr);
        LineTo(hdc, x1, y1);
        SelectObject(hdc, old);
        DeleteObject(p);
    }

    // ── The dial ────────────────────────────────────────────────
    //
    // Which way the residual points, against the frame. Zenith is up
    // the frame in an unrotated alt-az view and dispersion runs along
    // the zenith line, so a correctly ROTATED corrector leaves a
    // residual at 0 or 180 and a residual off that axis means the ADC
    // body itself is turned wrong -- a different adjustment from the
    // prism angle, and one no single number can distinguish.
    void DrawDial(HDC hdc, int cx, int cy, int radius) const {
        const COLORREF dim  = Theme::NV(RGB(50, 64, 82));
        const COLORREF axis = Theme::NV(RGB(90, 110, 134));

        HPEN   pen   = CreatePen(PS_SOLID, 1, dim);
        HGDIOBJ oldP = SelectObject(hdc, pen);
        HGDIOBJ oldB = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Ellipse(hdc, cx - radius, cy - radius, cx + radius, cy + radius);
        SelectObject(hdc, oldB);
        SelectObject(hdc, oldP);
        DeleteObject(pen);

        // The zenith axis, which is the axis the residual should lie on.
        Line(hdc, cx, cy - radius, cx, cy + radius, axis, 1);
        const int zPx = (std::max)(9, (std::min)(16, radius / 6));
        const int zW  = zPx * 8;     // fits "ZENITH" at any of those sizes
        TextAt(hdc, cx - zW / 2, cy - radius - zPx - 6, zW, zPx + 4, "ZENITH",
               axis, zPx, false, "Barlow Condensed", DT_CENTER);

        if (!m_data.measured.valid) return;

        // Scale the needle against the predicted dispersion, so the
        // needle shrinks as the corrector takes effect. Without a
        // prediction (no altitude yet) fall back to a fixed 2 arcsec.
        const float full = (m_data.predictedArcsec > 0.05f)
                         ? m_data.predictedArcsec : 2.0f;
        float frac = m_data.measured.magArcsec / full;
        if (frac > 1.0f) frac = 1.0f;
        const int len = (int)(radius * frac);
        if (len < 2) return;

        const double rad = m_data.measured.angleDeg * 3.14159265358979 / 180.0;
        const int ex = cx + (int)(len * std::sin(rad));
        const int ey = cy - (int)(len * std::cos(rad));
        Line(hdc, cx, cy, ex, ey, ResidualColour(), 3);
    }

    COLORREF ResidualColour() const {
        // No reading is not a good reading. Green here means the
        // residual is below the seeing, and a placeholder painted green
        // says the corrector is dialled in when nothing has been
        // measured at all.
        if (!m_data.measured.valid) return Theme::NV(RGB(90, 110, 134));
        const float a = m_data.measured.magArcsec;
        if (a <= ALIGNED_ARCSEC)  return Theme::NV(RGB(0, 200, 110));
        if (a <= 0.40f)           return Theme::NV(RGB(255, 170, 0));
        return Theme::NV(RGB(255, 60, 90));
    }

    // Residual over the last few minutes. A corrector is dialled by
    // small nudges, and whether the last nudge helped is a question
    // about the trend, not about the current number -- seeing moves the
    // current number by more than a good nudge does.
    void DrawTrend(HDC hdc, int x, int y, int w, int h) const {
        Fill(hdc, x, y, w, h, Theme::NV(RGB(4, 10, 18)));
        if (m_trend.size() < 2) return;

        float peak = 0.0f;
        for (float v : m_trend) peak = (std::max)(peak, v);
        if (peak < 0.3f) peak = 0.3f;

        // The aligned threshold as a reference line, so "flat near the
        // bottom" can be told from "flat but still half an arcsec out".
        const int ty = y + h - (int)(h * (ALIGNED_ARCSEC / peak));
        if (ty > y && ty < y + h)
            Line(hdc, x, ty, x + w, ty, Theme::NV(RGB(50, 64, 82)), 1);

        const int n = (int)m_trend.size();
        int prevX = 0, prevY = 0;
        for (int i = 0; i < n; i++) {
            const int px = x + (int)((float)i / (n - 1) * (w - 1));
            const int py = y + h - 1 - (int)((m_trend[i] / peak) * (h - 2));
            if (i > 0) Line(hdc, prevX, prevY, px, py,
                            Theme::NV(RGB(0, 229, 255)), 1);
            prevX = px; prevY = py;
        }

        char lbl[32];
        snprintf(lbl, sizeof(lbl), "%.2f\"", peak);
        TextAt(hdc, x + 4, y + 2, 60, 14, lbl,
               Theme::NV(RGB(90, 110, 134)), 11, false, "Space Mono", DT_LEFT);
    }

    // What to actually do with your hands.
    void Guidance(char* out, size_t n) const {
        if (!m_data.frameAvailable) {
            strcpy_s(out, n, "No frame - start the camera");
            return;
        }
        if (!m_data.measured.valid) {
            strcpy_s(out, n,
                     "No target - centre a bright planet or star and zoom in");
            return;
        }
        if (m_data.samples < 10) {
            strcpy_s(out, n, "Collecting - hold still");
            return;
        }

        const float a = m_data.measured.magArcsec;
        if (a <= ALIGNED_ARCSEC) {
            strcpy_s(out, n, "ALIGNED - residual is below the seeing");
            return;
        }

        // How far off the zenith axis the residual lies. Off-axis means
        // the ADC BODY is rotated wrong, which no amount of prism angle
        // will fix, so say that instead of "turn the prisms".
        float offAxis = m_data.measured.angleDeg;
        if (offAxis > 180.0f) offAxis = 360.0f - offAxis;   // fold to 0..180
        const float fromAxis = (std::min)(offAxis, 180.0f - offAxis);

        if (fromAxis > 30.0f) {
            snprintf(out, n,
                     "Residual %.0f deg off the zenith axis - rotate the ADC body",
                     fromAxis);
            return;
        }

        // On-axis: the sense tells you which way the prisms are wrong.
        // Blue toward zenith (near 0) is the uncorrected sense, so more
        // correction is needed. Blue away from zenith (near 180) means
        // the corrector has gone past it.
        const bool overcorrected = (m_data.measured.angleDeg > 90.0f &&
                                    m_data.measured.angleDeg < 270.0f);
        snprintf(out, n, "%s - %.2f\" remaining",
                 overcorrected ? "OVERCORRECTED, back the prisms off"
                               : "Spread the prisms further apart",
                 a);
    }

    void Draw(HDC hdc, const RECT& cr) {
        const int W = cr.right, H = cr.bottom;
        Fill(hdc, 0, 0, W, H, RGB(4, 8, 15));
        if (W < 200 || H < 160) return;

        const int pad = (std::max)(8, W / 50);
        const COLORREF label = Theme::NV(RGB(120, 136, 156));
        const COLORREF value = Theme::NV(RGB(200, 232, 240));

        // ── Left: the number being dialled to zero ──────────────
        // 62%, not 55%. The guidance line and the predicted line are
        // whole sentences, and at 55% both were being clipped mid-word
        // -- "Spread the prisms further apar" is worse than no advice.
        const int colW = (W - pad * 3) * 62 / 100;
        const int bigPx = (std::min)((std::max)(28, H / 5), colW / 5);

        char buf[160];
        int y = pad;

        TextAt(hdc, pad, y, colW, bigPx / 2, "MEASURED RESIDUAL",
               label, (std::max)(10, bigPx / 4), true, "Barlow Condensed", DT_LEFT);
        y += bigPx / 2 + 2;

        if (m_data.measured.valid)
            snprintf(buf, sizeof(buf), "%.2f\"", m_data.measured.magArcsec);
        else
            snprintf(buf, sizeof(buf), "--");
        TextAt(hdc, pad, y, colW, bigPx, buf, ResidualColour(),
               bigPx, true, "Space Mono", DT_LEFT);
        y += bigPx + 4;

        if (m_data.measured.valid) {
            snprintf(buf, sizeof(buf), "%.2f px at %.0f deg,  %d samples",
                     m_data.measured.magPx, m_data.measured.angleDeg,
                     m_data.samples);
            TextAt(hdc, pad, y, colW, bigPx / 3, buf, label,
                   (std::max)(10, bigPx / 4), false, "Space Mono", DT_LEFT);
        }
        y += bigPx / 3 + pad;

        // ── Predicted, and what fraction is left ────────────────
        //
        // The residual alone does not say whether the corrector is
        // working: at 70 degrees altitude there is barely any
        // dispersion to correct, so a small residual proves nothing.
        // Against the prediction it becomes a percentage, and a
        // percentage is comparable between one night and the next.
        TextAt(hdc, pad, y, colW, bigPx / 3, "PREDICTED, UNCORRECTED",
               label, (std::max)(10, bigPx / 4), true, "Barlow Condensed", DT_LEFT);
        y += bigPx / 3 + 2;

        if (m_data.predictedArcsec > 0.0f) {
            const float pct = m_data.measured.valid
                            ? m_data.measured.magArcsec / m_data.predictedArcsec * 100.0f
                            : 0.0f;
            if (m_data.measured.valid)
                snprintf(buf, sizeof(buf), "%.2f\" at alt %.0f deg  -  %.0f%% left",
                         m_data.predictedArcsec, m_data.altitudeDeg, pct);
            else
                snprintf(buf, sizeof(buf), "%.2f\" at alt %.0f deg",
                         m_data.predictedArcsec, m_data.altitudeDeg);
        } else {
            snprintf(buf, sizeof(buf), "unknown - mount has not reported altitude");
        }
        TextAt(hdc, pad, y, colW, bigPx / 2, buf, value,
               (std::max)(10, bigPx / 4), false, "Space Mono", DT_LEFT);
        y += bigPx / 2 + pad;

        // ── What to do ──────────────────────────────────────────
        Guidance(buf, sizeof(buf));
        const int guideH = (std::max)(22, bigPx / 2);
        Fill(hdc, pad, y, colW, guideH, Theme::NV(RGB(8, 16, 26)));
        TextAt(hdc, pad + 6, y, colW - 12, guideH, buf,
               m_data.measured.valid ? ResidualColour() : label,
               (std::max)(11, bigPx / 4), true, "Barlow Condensed", DT_LEFT);
        y += guideH + pad;

        // ── Trend ───────────────────────────────────────────────
        const int trendH = (std::max)(40, H - y - pad);
        if (trendH > 24) {
            TextAt(hdc, pad, y - 14, colW, 14, "RESIDUAL OVER TIME",
                   label, 11, true, "Barlow Condensed", DT_LEFT);
            DrawTrend(hdc, pad, y, colW, trendH);
        }

        // ── Right: the dial ─────────────────────────────────────
        const int dialX = pad * 2 + colW;
        const int dialW = W - dialX - pad;
        const int radius = (std::min)(dialW, H - pad * 2) / 2 - 18;
        if (radius > 24)
            DrawDial(hdc, dialX + dialW / 2, pad + 20 + radius, radius);
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        ADCAlignWindow* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<ADCAlignWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->m_hwnd = hwnd;
        } else {
            self = reinterpret_cast<ADCAlignWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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
            case WM_ERASEBKGND: return 1;
            case WM_SIZE: InvalidateRect(hwnd, nullptr, FALSE); return 0;
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

    HWND               m_hwnd = nullptr;
    Data               m_data;
    std::vector<float> m_trend;
    bool               m_fullscreen = false;
    WINDOWPLACEMENT    m_savedPlacement = {};
};

} // namespace raduk::gui
