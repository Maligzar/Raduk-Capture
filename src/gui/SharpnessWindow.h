#pragma once
// ============================================================
// SharpnessWindow.h — Phase 42c
//
// The rolling sharpness graph as a window of its own.
//
// Not just a larger copy of the overlay. The overlay keeps 200 samples,
// about five seconds, which is the right amount for "is it sharp right
// now" while you nudge the focuser. A screen of its own can answer a
// different question — has the seeing settled, is focus drifting as the
// tube cools, was that gust or the mount — and that needs minutes, not
// seconds. So this keeps its own much longer history, fed from the same
// samples.
//
// The drawing is shared with the overlay (SharpnessDraw.h), so the
// colour bands cannot drift between the two. Those bands are the same
// judgement autofocus and the capture gate make.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <algorithm>
#include <vector>
#include "Theme.h"
#include "SharpnessDraw.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")

namespace raduk::gui {

class SharpnessWindow {
public:
    // 4000 samples. At the emulator's ~15fps that is four minutes; at a
    // real 40fps, about ninety seconds. Long enough to see a thermal
    // drift build, which five seconds cannot show however large you
    // draw it.
    static constexpr int HISTORY = 4000;

    bool Create(HWND parent, int x, int y, int w, int h) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(Theme::NV(Theme::Rgb::BG_VOID));
        wc.lpszClassName = L"RadukSharpness";
        RegisterClassExW(&wc);

        m_hwnd = CreateWindowExW(
            0, L"RadukSharpness", L"Sharpness",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX | WS_MAXIMIZEBOX,
            x, y, w, h, parent, nullptr, GetModuleHandle(nullptr), this);
        if (!m_hwnd) return false;

        BOOL dark = TRUE;
        DwmSetWindowAttribute(m_hwnd, 20, &dark, sizeof(dark));
        DwmSetWindowAttribute(m_hwnd, 19, &dark, sizeof(dark));

        m_samples.reserve(HISTORY);
        if (!InitD2D()) { DestroyWindow(m_hwnd); m_hwnd = nullptr; return false; }
        // Shown at startup, not hidden behind a shortcut. These are
        // telemetry for a screen of their own: a readout you have to
        // remember to open is a readout you will not be looking at
        // when it matters. Shift+Q closes it if the screen is wanted
        // for something else.
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        return true;
    }

    void Destroy() {
        ReleaseD2D();
        if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    }

    HWND Handle() const { return m_hwnd; }
    bool IsVisible() const { return m_hwnd && IsWindowVisible(m_hwnd); }
    void Show()   { if (m_hwnd) { ShowWindow(m_hwnd, SW_SHOW); SetForegroundWindow(m_hwnd); } }
    void Hide()   { if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE); }
    void Toggle() { if (!m_hwnd) return; IsVisible() ? Hide() : Show(); }

    // One sample per frame, from the same call that feeds the overlay.
    //
    // Kept even while hidden, deliberately: the point of a long history
    // is to look back at it, and a window that starts empty every time
    // you open it could only ever show you the present, which is what
    // the overlay already does.
    void Push(float sharpnessPct) {
        if ((int)m_samples.size() >= HISTORY)
            m_samples.erase(m_samples.begin());
        m_samples.push_back(sharpnessPct);
        if (IsVisible() && ++m_sincePaint >= PAINT_EVERY) {
            m_sincePaint = 0;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
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
            SetWindowPos(m_hwnd, HWND_TOP,
                         mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right  - mi.rcMonitor.left,
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
    // Repaint every Nth sample. At 40fps a repaint per frame would have
    // this window redrawing as hard as the preview to move the graph by
    // one bar; a third of that is still smooth and costs far less.
    static constexpr int PAINT_EVERY = 3;

    bool InitD2D() {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_factory)))
            return false;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                       __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(&m_dwrite))))
            return false;
        RECT rc; GetClientRect(m_hwnd, &rc);
        m_w = rc.right; m_h = rc.bottom;
        if (FAILED(m_factory->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(),
                D2D1::HwndRenderTargetProperties(m_hwnd, D2D1::SizeU(m_w, m_h)),
                &m_rt)))
            return false;
        CreateBrushes();
        MakeFont();
        return true;
    }

    void CreateBrushes() {
        if (!m_rt) return;
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.30f, 1.0f, 0.43f, 1.0f)), &m_green);
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(1.0f, 0.67f, 0.0f, 1.0f)), &m_amber);
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(1.0f, 0.23f, 0.36f, 1.0f)), &m_red);
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.47f, 0.53f, 0.61f, 1.0f)), &m_dim);
        m_brushGen = Theme::PaletteGeneration();
    }

    void ReleaseBrushes() {
        auto rel = [](ID2D1SolidColorBrush*& b) { if (b) { b->Release(); b = nullptr; } };
        rel(m_green); rel(m_amber); rel(m_red); rel(m_dim);
    }

    void MakeFont() {
        if (!m_dwrite) return;
        if (m_font) { m_font->Release(); m_font = nullptr; }
        const float size = (std::max)(11.0f, (std::min)(34.0f, (float)m_h / 20.0f));
        m_dwrite->CreateTextFormat(L"Space Mono", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &m_font);
    }

    void ReleaseD2D() {
        ReleaseBrushes();
        if (m_font)    { m_font->Release();    m_font = nullptr; }
        if (m_rt)      { m_rt->Release();      m_rt = nullptr; }
        if (m_dwrite)  { m_dwrite->Release();  m_dwrite = nullptr; }
        if (m_factory) { m_factory->Release(); m_factory = nullptr; }
    }

    void OnResize(int w, int h) {
        if (w <= 0 || h <= 0) return;
        m_w = w; m_h = h;
        if (m_rt) m_rt->Resize(D2D1::SizeU(w, h));
        MakeFont();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void Draw() {
        if (!m_rt || !m_font) return;
        if (m_brushGen != Theme::PaletteGeneration()) {
            ReleaseBrushes();
            CreateBrushes();
        }
        m_rt->BeginDraw();
        m_rt->Clear(Theme::NVf(D2D1::ColorF(0.016f, 0.031f, 0.059f, 1.0f)));

        sharpness::Brushes br;
        br.green = m_green; br.amber = m_amber; br.red = m_red; br.dim = m_dim;

        const float pad    = 10.0f;
        const float labelW = m_font->GetFontSize() * 6.0f;
        // Thin bars with no gap: a wide window should spend its width on
        // history rather than on spacing between bars that are one pixel
        // apart anyway.
        sharpness::Draw(m_rt, m_font, br,
                        m_samples.data(), (int)m_samples.size(),
                        pad, pad, (float)m_w - pad * 2.0f, (float)m_h - pad * 2.0f,
                        labelW, 1.0f, 0.0f, /*gridlines*/ true);

        m_rt->EndDraw();
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        SharpnessWindow* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<SharpnessWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->m_hwnd = hwnd;
        } else {
            self = reinterpret_cast<SharpnessWindow*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

        switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                BeginPaint(hwnd, &ps);
                self->Draw();
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_SIZE:
                self->OnResize(LOWORD(lp), HIWORD(lp));
                return 0;
            case WM_KEYDOWN:
                if (wp == VK_F11) { self->ToggleFullscreen(); return 0; }
                if (wp == VK_ESCAPE) {
                    if (self->m_fullscreen) self->ToggleFullscreen();
                    else                    self->Hide();
                    return 0;
                }
                break;
            case WM_ERASEBKGND:
                return 1;
            case WM_CLOSE:
                self->Hide();
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    HWND                    m_hwnd    = nullptr;
    ID2D1Factory*           m_factory = nullptr;
    IDWriteFactory*         m_dwrite  = nullptr;
    ID2D1HwndRenderTarget*  m_rt      = nullptr;
    IDWriteTextFormat*      m_font    = nullptr;
    ID2D1SolidColorBrush*   m_green = nullptr;
    ID2D1SolidColorBrush*   m_amber = nullptr;
    ID2D1SolidColorBrush*   m_red   = nullptr;
    ID2D1SolidColorBrush*   m_dim   = nullptr;
    unsigned                m_brushGen = 0;
    int                     m_w = 900, m_h = 400;
    int                     m_sincePaint = 0;
    bool                    m_fullscreen = false;
    WINDOWPLACEMENT         m_savedPlacement = {};
    std::vector<float>      m_samples;
};

} // namespace raduk::gui
