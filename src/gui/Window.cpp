// ============================================================
// Window.cpp — Win32 window + Direct2D for RadukCapture
// Fullscreen now just stretches the main window — panel is its
// own top-level window and needs no special handling.
// WM_MOVE / WM_SIZE fire m_onMove so docked panel can snap.
// ESC: exit fullscreen if active, emergency stop if not.
// ============================================================

#include "Window.h"
#include "Theme.h"
#include <windowsx.h>
#include <cstdio>

// Must match ControlPanel.h PANEL_WIDTH — the D2D render target is sized
// to the preview area only (clientW - PANEL_WIDTH) so it never paints over
// the WS_CHILD panel. Update here if PANEL_WIDTH changes.
// +20 accounts for WS_VSCROLL width (~17px) plus a small gap.
static constexpr int PREVIEW_PANEL_RESERVED = 580;  // 560 panel + 20 scrollbar/gap
static constexpr int TOOLBAR_HEIGHT_PX = 30;  // must match ToolbarStrip.h TOOLBAR_HEIGHT

// Panel is a fixed PREVIEW_PANEL_RESERVED px wide, docked to the right edge.
// Preview area = clientW - PREVIEW_PANEL_RESERVED.
static int CalcPreviewW(int clientW) {
    int previewW = clientW - PREVIEW_PANEL_RESERVED;
    if (previewW < 100) previewW = 100;
    return previewW;
}

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace raduk::gui {

Window::~Window() {
    ReleaseDirect2D();
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
}

bool Window::Create(const wchar_t* title, int width, int height) {
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandle(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(2, 5, 9));
    wc.lpszClassName = L"RadukCaptureWindow";
    // Load embedded icon (IDI_RADUK=101 from RadukCapture.rc)
    // Falls back to default app icon if resource not found.
    HICON hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(101));
    wc.hIcon = hIcon ? hIcon : LoadIcon(nullptr, IDI_APPLICATION);

    if (!RegisterClassExW(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            printf("[Window] RegisterClassEx failed: %lu\n", err);
            return false;
        }
    }

    // Create with CW_USEDEFAULT — let the OS pick size, then maximize.
    // Never use AdjustWindowRect: it is DPI-unaware and produces wrong chrome
    // padding at 125%/150% scaling even after SetProcessDpiAwarenessContext.
    (void)width; (void)height;
    m_hwnd = CreateWindowExW(
        0, L"RadukCaptureWindow", title,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr, nullptr, GetModuleHandle(nullptr), this);

    if (!m_hwnd) {
        printf("[Window] CreateWindowExW failed: %lu\n", GetLastError());
        return false;
    }

    // Maximize and pump the message queue so WM_SIZE fires and sets
    // m_clientWidth/m_clientHeight before InitDirect2D reads them.
    ShowWindow(m_hwnd, SW_SHOWMAXIMIZED);
    {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    // Safety net: read client rect directly — guaranteed correct now
    RECT clientRect;
    GetClientRect(m_hwnd, &clientRect);
    m_clientWidth  = clientRect.right;
    m_clientHeight = clientRect.bottom;

    // Create the preview child window — D2D renders into this, not the main window.
    // This gives us automatic clipping: D2D can never paint outside this child's bounds.
    // Positioned: x=0, y=TOOLBAR_HEIGHT_PX, width=clientW-PANEL_RESERVED, height=clientH-toolbar.
    {
        WNDCLASSEXW pwc = {};
        pwc.cbSize        = sizeof(pwc);
        pwc.style         = CS_HREDRAW | CS_VREDRAW;
        pwc.lpfnWndProc   = DefWindowProcW;
        pwc.hInstance     = GetModuleHandle(nullptr);
        pwc.hbrBackground = CreateSolidBrush(RGB(2, 5, 9));
        pwc.lpszClassName = L"RadukPreviewWindow";
        RegisterClassExW(&pwc);  // ignore ERROR_CLASS_ALREADY_EXISTS

        int previewW = CalcPreviewW(m_clientWidth);
        int previewH = m_clientHeight - TOOLBAR_HEIGHT_PX;
        m_previewHwnd = CreateWindowExW(
            0, L"RadukPreviewWindow", L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, TOOLBAR_HEIGHT_PX, previewW, previewH,
            m_hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        if (!m_previewHwnd) {
            printf("[Window] Preview child HWND failed: %lu\n", GetLastError());
            DestroyWindow(m_hwnd); m_hwnd = nullptr;
            return false;
        }
    }

    if (!InitDirect2D()) {
        printf("[Window] InitDirect2D failed\n");
        DestroyWindow(m_hwnd); m_hwnd = nullptr;
        return false;
    }

    m_savedPlacement.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(m_hwnd, &m_savedPlacement);

    printf("[Window] Created maximized: %dx%d client\n", m_clientWidth, m_clientHeight);
    return true;
}

// ── Fullscreen ─────────────────────────────────────────────────
// Stretches main window to fill the monitor. The panel window
// is completely independent and stays exactly where it is —
// in fullscreen the user can move/dock it as desired.

void Window::ToggleFullscreen() {
    if (!m_fullscreen) {
        m_savedPlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(m_hwnd, &m_savedPlacement);

        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);

        SetWindowLong(m_hwnd, GWL_STYLE,
            (GetWindowLong(m_hwnd, GWL_STYLE) & ~WS_OVERLAPPEDWINDOW) | WS_CLIPCHILDREN);
        SetWindowPos(m_hwnd, HWND_TOP,
            mi.rcMonitor.left,  mi.rcMonitor.top,
            mi.rcMonitor.right  - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        m_fullscreen = true;
        printf("[Window] Fullscreen ON\n");
    } else {
        SetWindowLong(m_hwnd, GWL_STYLE,
            GetWindowLong(m_hwnd, GWL_STYLE) | WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN);
        SetWindowPlacement(m_hwnd, &m_savedPlacement);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        m_fullscreen = false;
        printf("[Window] Fullscreen OFF\n");

        // Fire move handler so docked panel snaps back
        if (m_onMove) m_onMove();
    }
}

void Window::Run(std::function<void()> onRender) {
    MSG msg = {};
    while (true) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!m_hwnd) return;
        if (onRender && m_renderTarget) onRender();
        Sleep(1);
    }
}

bool Window::InitDirect2D() {
    HRESULT hr;
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_d2dFactory);
    if (FAILED(hr)) { printf("[Window] D2D1CreateFactory failed: 0x%lx\n", hr); return false; }

    // Get monitor DPI
    UINT dpiX = 96;
    if (m_hwnd) {
        HDC hdc = GetDC(m_hwnd);
        if (hdc) { dpiX = (UINT)GetDeviceCaps(hdc, LOGPIXELSX); ReleaseDC(m_hwnd, hdc); }
    }
    m_dpiScale = (float)dpiX / 96.0f;
    printf("[Window] Monitor DPI: %u  scale: %.2f\n", dpiX, m_dpiScale);

    // D2D renders into m_previewHwnd — the preview child window.
    // This gives automatic hardware clipping to the child bounds.
    // No toolbar translation needed: the child starts at y=TOOLBAR_HEIGHT_PX.
    RECT pr; GetClientRect(m_previewHwnd, &pr);
    D2D1_SIZE_U size = D2D1::SizeU(pr.right, pr.bottom);

    hr = m_d2dFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(m_previewHwnd, size, D2D1_PRESENT_OPTIONS_IMMEDIATELY),
        &m_renderTarget);
    if (FAILED(hr)) { printf("[Window] CreateHwndRenderTarget failed: 0x%lx\n", hr); return false; }

    // Apply DPI scale transform only — no toolbar offset needed (child handles that).
    if (m_dpiScale != 1.0f) {
        m_renderTarget->SetTransform(
            D2D1::Matrix3x2F::Scale(m_dpiScale, m_dpiScale, D2D1::Point2F(0.0f, 0.0f)));
    }

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&m_dwriteFactory));
    if (FAILED(hr)) { printf("[Window] DWriteCreateFactory failed: 0x%lx\n", hr); return false; }

    printf("[Window] Direct2D initialized (%dx%d pixels)\n", pr.right, m_clientHeight);
    return true;
}

void Window::ReleaseDirect2D() {
    if (m_dwriteFactory) { m_dwriteFactory->Release(); m_dwriteFactory = nullptr; }
    if (m_renderTarget)  { m_renderTarget->Release();  m_renderTarget  = nullptr; }
    if (m_d2dFactory)    { m_d2dFactory->Release();    m_d2dFactory    = nullptr; }
}

void Window::OnResize(int width, int height) {
    m_clientWidth  = width;
    m_clientHeight = height;
    int previewW = CalcPreviewW(width);
    int previewH = (height > TOOLBAR_HEIGHT_PX) ? height - TOOLBAR_HEIGHT_PX : height;

    // Resize the preview child window first
    if (m_previewHwnd) {
        SetWindowPos(m_previewHwnd, nullptr, 0, TOOLBAR_HEIGHT_PX,
                     previewW, previewH, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // Resize D2D render target to match
    if (m_renderTarget) {
        m_renderTarget->Resize(D2D1::SizeU(previewW, previewH));
        // Re-apply DPI scale transform (Resize resets to identity)
        if (m_dpiScale != 1.0f) {
            m_renderTarget->SetTransform(
                D2D1::Matrix3x2F::Scale(m_dpiScale, m_dpiScale, D2D1::Point2F(0.0f, 0.0f)));
        }
    }
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = static_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(msg, wParam, lParam);
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT Window::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                OnResize(LOWORD(lParam), HIWORD(lParam));
                InvalidateRect(m_hwnd, NULL, FALSE);
                if (m_onMove) m_onMove(); // keep docked panel + toolbar snapped
            }
            return 0;

        case WM_COMMAND:
            // Forward toolbar button commands to the registered handler.
            // ControlPanel WM_COMMAND is handled inside its own WndProc.
            if (m_onToolbarCommand) {
                if (m_onToolbarCommand((int)LOWORD(wParam))) return 0;
            }
            break;

        case WM_MOVE:
            if (m_onMove) m_onMove(); // keep docked panel snapped
            return 0;

        case WM_HSCROLL:
            if (m_onScroll && m_onScroll(wParam, lParam)) return 0;
            break;

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (m_onZoomWheel) m_onZoomWheel(delta > 0 ? 1 : -1);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            float x = (float)GET_X_LPARAM(lParam);
            float y = (float)GET_Y_LPARAM(lParam);
            if (m_onClick && m_onClick(x, y)) return 0;
            break;
        }

        case WM_MOUSEMOVE: {
            float x = (float)GET_X_LPARAM(lParam);
            float y = (float)GET_Y_LPARAM(lParam);
            if (m_onMouseMove) m_onMouseMove(x, y);
            break;
        }

        case WM_LBUTTONUP: {
            float x = (float)GET_X_LPARAM(lParam);
            float y = (float)GET_Y_LPARAM(lParam);
            if (m_onMouseUp) m_onMouseUp(x, y);
            break;
        }

        // WM_USER messages — used for safe cross-thread signalling
        // (e.g. WebSocket command thread → main thread)
        default:
            if (msg >= WM_USER && msg < WM_USER + 100) {
                if (m_onUserMsg) m_onUserMsg(msg);
                return 0;
            }
            break;

        case WM_KEYDOWN: {
            bool shift      = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool firstPress = ((lParam >> 30) & 1) == 0;

            switch (wParam) {
                case VK_ESCAPE:
                    if (firstPress) {
                        if (m_fullscreen) {
                            // ESC exits fullscreen first
                            if (m_onFullscreenToggle) m_onFullscreenToggle();
                        } else {
                            // ESC outside fullscreen = emergency stop
                            if (m_onEmergencyStop) m_onEmergencyStop();
                        }
                    }
                    return 0;
                case 'R':
                    if (firstPress && m_onReticleToggle) m_onReticleToggle();
                    return 0;
                case 'C':
                    if (firstPress && m_onClipToggle) m_onClipToggle();
                    return 0;
                case 'S':
                    if (firstPress && m_onSnapshot) m_onSnapshot();
                    return 0;
                case 'Q':
                    if (firstPress && m_onQualityGraphToggle) m_onQualityGraphToggle();
                    return 0;
                case 'P':
                    if (firstPress && m_onPause) m_onPause();
                    return 0;
                case 'Z':
                    if (firstPress && m_onZoomCycle) m_onZoomCycle();
                    return 0;
                case 'G':
                    if (firstPress && m_onGoToToggle) m_onGoToToggle();
                    return 0;
                case 'M':
                    if (firstPress && m_onSkyMapToggle) m_onSkyMapToggle();
                    return 0;
                case 'F':
                    if (shift) { if (m_onFocusCoarse) m_onFocusCoarse(100); }
                    else       { if (m_onFocusFine)   m_onFocusFine(10);   }
                    return 0;
                case 'T':
                    if (firstPress && m_onTrackingToggle) m_onTrackingToggle();
                    return 0;
                case VK_F11:
                    if (firstPress && m_onFullscreenToggle) m_onFullscreenToggle();
                    return 0;
                case VK_UP:
                    if (m_onJog) m_onJog(1, 0, true);
                    return 0;
                case VK_DOWN:
                    if (m_onJog) m_onJog(1, 1, true);
                    return 0;
                case VK_RIGHT:
                    if (m_onJog) m_onJog(0, 0, true);
                    return 0;
                case VK_LEFT:
                    if (m_onJog) m_onJog(0, 1, true);
                    return 0;
            }
            break;
        }

        case WM_KEYUP:
            switch (wParam) {
                case VK_UP:    if (m_onJog) m_onJog(1, 0, false); return 0;
                case VK_DOWN:  if (m_onJog) m_onJog(1, 1, false); return 0;
                case VK_RIGHT: if (m_onJog) m_onJog(0, 0, false); return 0;
                case VK_LEFT:  if (m_onJog) m_onJog(0, 1, false); return 0;
            }
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_GETMINMAXINFO: {
            // Enforce minimum window size so the panel is never clipped.
            // 600px minimum preview + 520px panel + frame borders.
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            RECT frame = {0, 0, 600 + PREVIEW_PANEL_RESERVED, 500};
            AdjustWindowRect(&frame, GetWindowLong(m_hwnd, GWL_STYLE), FALSE);
            mmi->ptMinTrackSize.x = frame.right  - frame.left;
            mmi->ptMinTrackSize.y = frame.bottom - frame.top;
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            // Fill the gap between D2D preview area and the WS_CHILD panel.
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(m_hwnd, &ps);
            RECT cr; GetClientRect(m_hwnd, &cr);
            // Preview ends at CalcPreviewW; panel starts at clientW - 560.
            // Fill the strip between them with the panel background color.
            int previewRight = CalcPreviewW(cr.right);
            RECT gapRect = { previewRight, 0, cr.right, cr.bottom };
            static HBRUSH hGapBr = CreateSolidBrush(RGB(7, 13, 22));
            FillRect(hdc, &gapRect, hGapBr);
            EndPaint(m_hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProc(m_hwnd, msg, wParam, lParam);
}

} // namespace raduk::gui
