#pragma once
// ============================================================
// TemperatureWindow.h — Phase 42d
//
// Camera and focuser temperature over a long window.
//
// The preview panel keeps 120 samples at roughly 2Hz, about a minute.
// That is enough to see that the sensors are reading, and not nearly
// enough to see the thing temperature actually matters for: the slow
// drift as the tube cools, which pulls focus with it over an hour and
// is the reason a focus set at the start of a session is wrong by the
// end. This keeps 7200 samples -- an hour at the same rate -- so the
// drift is a shape on a chart rather than something inferred from two
// readings taken twenty minutes apart.
//
// Drawing is shared with the panel (TemperatureDraw.h).
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
#include "TemperatureDraw.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")

namespace raduk::gui {

class TemperatureWindow {
public:
    // At the readout loop's ~2Hz this is an hour of history.
    static constexpr int HISTORY = 7200;

    bool Create(HWND parent, int x, int y, int w, int h) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(Theme::NV(Theme::Rgb::BG_VOID));
        wc.lpszClassName = L"RadukTemperature";
        RegisterClassExW(&wc);

        m_hwnd = CreateWindowExW(
            0, L"RadukTemperature", L"Temperature",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX | WS_MAXIMIZEBOX,
            x, y, w, h, parent, nullptr, GetModuleHandle(nullptr), this);
        if (!m_hwnd) return false;

        BOOL dark = TRUE;
        DwmSetWindowAttribute(m_hwnd, 20, &dark, sizeof(dark));
        DwmSetWindowAttribute(m_hwnd, 19, &dark, sizeof(dark));

        m_samples.reserve(HISTORY);
        if (!InitD2D()) { DestroyWindow(m_hwnd); m_hwnd = nullptr; return false; }
        // Open on its screen, without stealing focus from the main window.
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

    // From the readout loop, ~2Hz. Kept whether visible or not: an hour
    // of history is only useful if it was being collected before you
    // thought to look at it.
    void Push(float camC, float focC) {
        m_camNow = camC; m_focNow = focC;
        if ((int)m_samples.size() >= HISTORY)
            m_samples.erase(m_samples.begin());
        m_samples.push_back({ camC, focC });
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
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.30f, 1.0f, 0.43f, 1.0f)), &m_cam);
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(1.0f, 0.67f, 0.0f, 1.0f)), &m_foc);
        m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(0.47f, 0.53f, 0.61f, 1.0f)), &m_dim);
        m_brushGen = Theme::PaletteGeneration();
    }

    void ReleaseBrushes() {
        auto rel = [](ID2D1SolidColorBrush*& b) { if (b) { b->Release(); b = nullptr; } };
        rel(m_cam); rel(m_foc); rel(m_dim);
    }

    void MakeFont() {
        if (!m_dwrite) return;
        if (m_font) { m_font->Release(); m_font = nullptr; }
        const float size = (std::max)(11.0f, (std::min)(30.0f, (float)m_h / 14.0f));
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
        if (m_brushGen != Theme::PaletteGeneration()) {
            ReleaseBrushes();
            CreateBrushes();
        }
        m_rt->BeginDraw();
        m_rt->Clear(Theme::NVf(D2D1::ColorF(0.016f, 0.031f, 0.059f, 1.0f)));

        temperature::Brushes br;
        br.cam = m_cam; br.foc = m_foc; br.dim = m_dim;

        const float pad    = 12.0f;
        const float labelH = m_font->GetFontSize() * 1.6f;
        // The x axis spans only as much history as has been collected,
        // so the trace grows from the left instead of a short run being
        // stretched across the whole width and looking like an hour of
        // data that is not there yet.
        const int   span   = (std::max)((int)m_samples.size(), 2);

        temperature::Draw(m_rt, m_font, br,
                          m_samples.data(), (int)m_samples.size(), span,
                          m_camNow, m_focNow,
                          pad, pad, (float)m_w - pad * 2.0f,
                          (float)m_h - pad * 2.0f, labelH, 1.5f);

        m_rt->EndDraw();
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        TemperatureWindow* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<TemperatureWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->m_hwnd = hwnd;
        } else {
            self = reinterpret_cast<TemperatureWindow*>(
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
    ID2D1SolidColorBrush*   m_cam = nullptr;
    ID2D1SolidColorBrush*   m_foc = nullptr;
    ID2D1SolidColorBrush*   m_dim = nullptr;
    unsigned                m_brushGen = 0;
    int                     m_w = 900, m_h = 400;
    bool                    m_fullscreen = false;
    WINDOWPLACEMENT         m_savedPlacement = {};
    float                   m_camNow = 0.0f, m_focNow = 0.0f;
    std::vector<temperature::Sample> m_samples;
};

} // namespace raduk::gui
