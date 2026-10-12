#pragma once
// ============================================================
// HistogramWindow.h — Phase 42b
//
// The per-channel histogram as a window of its own, for a monitor of
// its own.
//
// The overlay on the preview had to be small and had to sit on empty
// sky, which capped how much of the distribution it could show and
// meant it competed with the image for space. With five screens there
// is no reason for either constraint: a spare monitor can hold rows a
// third of a screen tall, where the shape of each channel is readable
// at a glance from across the room.
//
// The drawing is shared with the overlay (HistogramDraw.h) rather than
// copied, so the log scale and the clip colouring cannot drift apart.
// What this adds is the window, its render target, and a font that
// grows with it.
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
#include <cstdio>
#include "Theme.h"
#include "HistogramDraw.h"
#include "../core/FrameAnalyzer.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "dwrite.lib")

namespace raduk::gui {

class HistogramWindow {
public:
    bool Create(HWND parent, int x, int y, int w, int h) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(Theme::NV(RGB(4, 8, 15)));
        wc.lpszClassName = L"RadukHistogram";
        RegisterClassExW(&wc);

        // Same flags as the sky map, and for the same reason: a maximise
        // box and no tool-window bit, so it can fill whatever screen it
        // is dragged to and appears in Alt-Tab.
        m_hwnd = CreateWindowExW(
            0, L"RadukHistogram", L"Histogram",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX | WS_MAXIMIZEBOX,
            x, y, w, h, parent, nullptr, GetModuleHandle(nullptr), this);
        if (!m_hwnd) return false;

        // Dark caption, matching the main window. 20 on current Win10/11,
        // 19 on older builds; setting both is what Window.cpp does.
        {
            BOOL dark = TRUE;
            DwmSetWindowAttribute(m_hwnd, 20, &dark, sizeof(dark));
            DwmSetWindowAttribute(m_hwnd, 19, &dark, sizeof(dark));
        }

        if (!InitD2D()) { DestroyWindow(m_hwnd); m_hwnd = nullptr; return false; }
        ShowWindow(m_hwnd, SW_HIDE);   // opened with Shift+H
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

    // Latest frame analysis. Called every render tick, so it only keeps
    // the data and asks for a repaint -- and only while visible, since a
    // hidden window redrawing at 30fps is pure waste.
    void Update(const raduk::core::AnalysisResult& a) {
        m_analysis = a;
        m_valid    = a.valid;
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
        // Through NVf, so night vision reaches this window too.
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(1.0f, 0.31f, 0.31f, 1.0f)), &m_br.rB);
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.31f, 0.86f, 0.31f, 1.0f)), &m_br.gB);
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.31f, 0.55f, 1.0f, 1.0f)), &m_br.bB);
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.02f, 0.04f, 0.07f, 1.0f)), &m_br.bedB);
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.85f, 0.88f, 0.92f, 1.0f)), &m_br.meanB);
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.47f, 0.53f, 0.61f, 1.0f)), &m_br.dimB);
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(1.0f, 0.67f, 0.0f, 1.0f)), &m_br.amberB);
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(1.0f, 0.23f, 0.36f, 1.0f)), &m_br.redB);
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.30f, 1.0f, 0.43f, 1.0f)), &m_br.greenB);
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.02f, 0.03f, 0.06f, 1.0f)), &m_bgBrush);
        m_brushGen = Theme::PaletteGeneration();
    }

    void ReleaseBrushes() {
        auto rel = [](ID2D1SolidColorBrush*& b) { if (b) { b->Release(); b = nullptr; } };
        rel(m_br.rB); rel(m_br.gB); rel(m_br.bB); rel(m_br.bedB); rel(m_br.meanB);
        rel(m_br.dimB); rel(m_br.amberB); rel(m_br.redB); rel(m_br.greenB);
        rel(m_bgBrush);
    }

    // The font is remade on every resize because this window's whole
    // point is that it can be large. A fixed 11px label on a 1080p
    // screen would waste the space the window exists to use.
    void MakeFont() {
        if (!m_dwrite) return;
        if (m_font) { m_font->Release(); m_font = nullptr; }
        const float size = (std::max)(11.0f, (std::min)(42.0f, (float)m_h / 22.0f));
        m_dwrite->CreateTextFormat(L"Space Mono", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &m_font);
        if (m_font) m_font->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
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
        // Night vision rebuilds the palette; these are COM objects, so
        // tinting the colour alone would leave them holding the old one.
        if (m_brushGen != Theme::PaletteGeneration()) {
            ReleaseBrushes();
            CreateBrushes();
        }
        m_rt->BeginDraw();
        m_rt->Clear(Theme::NVf(D2D1::ColorF(0.016f, 0.031f, 0.059f, 1.0f)));

        const float w = (float)m_w, h = (float)m_h;
        // Three rows plus a line for the seeing score; the rows take
        // what is left after a margin.
        const float rowH = (std::max)(16.0f, (h - 16.0f - h * 0.10f) / 3.0f);

        histogram::Brushes br;
        br.r = m_br.rB; br.g = m_br.gB; br.b = m_br.bB;
        br.bed = m_br.bedB; br.mean = m_br.meanB; br.dim = m_br.dimB;
        br.amber = m_br.amberB; br.red = m_br.redB; br.green = m_br.greenB;

        // Text columns sized from the font, not the row height: the
        // letter is one glyph and "100.0%" is six, whatever the rows do.
        const float fs   = m_font->GetFontSize();
        const float labW = fs * 1.3f;
        const float pctW = fs * 4.2f;
        histogram::Draw(m_rt, m_font, br, m_analysis, m_valid,
                        0.0f, 0.0f, w, h, rowH, labW, pctW);

        m_rt->EndDraw();
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        HistogramWindow* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<HistogramWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->m_hwnd = hwnd;
        } else {
            self = reinterpret_cast<HistogramWindow*>(
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
                return 1;           // D2D clears; let GDI skip it
            case WM_CLOSE:
                self->Hide();       // hide, never destroy
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    struct Br {
        ID2D1SolidColorBrush* rB = nullptr;
        ID2D1SolidColorBrush* gB = nullptr;
        ID2D1SolidColorBrush* bB = nullptr;
        ID2D1SolidColorBrush* bedB = nullptr;
        ID2D1SolidColorBrush* meanB = nullptr;
        ID2D1SolidColorBrush* dimB = nullptr;
        ID2D1SolidColorBrush* amberB = nullptr;
        ID2D1SolidColorBrush* redB = nullptr;
        ID2D1SolidColorBrush* greenB = nullptr;
    } m_br;

    HWND                    m_hwnd    = nullptr;
    ID2D1Factory*           m_factory = nullptr;
    IDWriteFactory*         m_dwrite  = nullptr;
    ID2D1HwndRenderTarget*  m_rt      = nullptr;
    IDWriteTextFormat*      m_font    = nullptr;
    ID2D1SolidColorBrush*   m_bgBrush = nullptr;
    unsigned                m_brushGen = 0;
    int                     m_w = 600, m_h = 400;
    bool                    m_fullscreen = false;
    WINDOWPLACEMENT         m_savedPlacement = {};

    raduk::core::AnalysisResult m_analysis;
    bool                        m_valid = false;
};

} // namespace raduk::gui
