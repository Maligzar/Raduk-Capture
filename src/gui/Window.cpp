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
#include <dwmapi.h>
#include <cstdio>
#pragma comment(lib, "dwmapi.lib")

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

    // Dark title bar so it matches the app and the title text stays readable
    // (on light Windows themes the caption was dark-on-light and unreadable).
    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 on current Win10/11; 19 on older
    // builds — set both, DWM ignores the one it doesn't know.
    {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(m_hwnd, 20, &dark, sizeof(dark));
        DwmSetWindowAttribute(m_hwnd, 19, &dark, sizeof(dark));
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
        // Phase 61: was DefWindowProcW, which silently ate every mouse
        // click on the image -- the rubber-band ROI and the pixel-readout
        // click were fully implemented but never received an event.
        pwc.lpfnWndProc   = PreviewProc;
        pwc.hInstance     = GetModuleHandle(nullptr);
        pwc.hbrBackground = CreateSolidBrush(RGB(2, 5, 9));
        pwc.lpszClassName = L"RadukPreviewWindow";
        RegisterClassExW(&pwc);  // ignore ERROR_CLASS_ALREADY_EXISTS

        int previewW = CalcPreviewW(m_clientWidth);
        int previewH = m_clientHeight - TOOLBAR_HEIGHT_PX;
        m_previewHwnd = CreateWindowExW(
            0, L"RadukPreviewWindow", L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, TOOLBAR_HEIGHT_PX + m_notifBarH, previewW, previewH - m_notifBarH,
            m_hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        // Back-pointer so the child proc can reach this instance.
        if (m_previewHwnd)
            SetWindowLongPtrW(m_previewHwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(this));
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
            // Phase 60: route hotkeys before dispatch so they work
            // whichever of our windows or controls holds focus. A
            // consumed key is not dispatched. TranslateMessage still
            // runs for keys we do not take, so the WM_CHAR hotkeys
            // ('?', '/', 'o') arrive on a later pass and route then.
            if (RouteThreadKey(msg)) continue;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!m_hwnd) return;
        if (onRender && m_renderTarget) onRender();
        Sleep(1);
    }
}

// -- Phase 60: thread-wide key routing --------------------------------
// Maps an arrow key to the jog axis/direction pair the mount API uses.
// Axis 0 is RA/azimuth, axis 1 is Dec/altitude; direction 0 is the
// positive sense. These are the same pairings the on-screen D-pad sends.
static bool ArrowAxisDir(WPARAM vk, int& axis, int& dir) {
    switch (vk) {
        case VK_UP:    axis = 1; dir = 0; return true;
        case VK_DOWN:  axis = 1; dir = 1; return true;
        case VK_RIGHT: axis = 0; dir = 0; return true;
        case VK_LEFT:  axis = 0; dir = 1; return true;
        default:       return false;
    }
}

void Window::StartArrowJog(int axis, int dir) {
    if (!m_onJog) return;
    // WM_KEYDOWN auto-repeats at the system repeat rate while a key is
    // held. Each SlewFixed is an 8-byte NexStar 'P' command on a
    // 9600-baud link shared with the position poll, and the mount is
    // already moving, so every repeat is pure congestion. The held flag
    // drops them -- and also de-duplicates a press that both the router
    // and this window's proc could otherwise see.
    if (m_arrowJogHeld[axis][dir]) return;
    m_arrowJogHeld[axis][dir] = true;
    m_onJog(axis, dir, true);
}

// Returns true when a jog was actually running in this direction, so
// callers can tell "we owned this key" from "nothing to stop".
bool Window::StopArrowJog(int axis, int dir) {
    if (!m_arrowJogHeld[axis][dir]) return false;
    m_arrowJogHeld[axis][dir] = false;
    if (m_onJog) m_onJog(axis, dir, false);
    return true;
}

void Window::StopAllArrowJogs() {
    for (int axis = 0; axis < 2; axis++)
        for (int dir = 0; dir < 2; dir++)
            StopArrowJog(axis, dir);
}

bool Window::HandleKeyDown(WPARAM wParam, LPARAM lParam) {
    bool shift      = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool firstPress = ((lParam >> 30) & 1) == 0;
    (void)firstPress;  // used by the cases below

    int axis = 0, dir = 0;
    if (ArrowAxisDir(wParam, axis, dir)) {
        StartArrowJog(axis, dir);
        return true;
    }

    switch (wParam) {
        case VK_ESCAPE:
            // Phase 71 (review): ESC always stops the mount, including
            // in fullscreen. It used to exit fullscreen *instead*, which
            // meant that while fullscreen there was no emergency stop
            // key at all -- and fullscreen is exactly when someone is
            // watching the preview rather than the panel. An emergency
            // stop that is conditional on a display mode is not one.
            // F11 toggles fullscreen and is unaffected.
            if (firstPress && m_onEmergencyStop) m_onEmergencyStop();
            return true;

        case VK_SPACE:
            // Documented in the help as the emergency stop but never
            // bound to anything. Someone reaching for the key the
            // manual names, with the mount running away, got nothing.
            if (firstPress && m_onEmergencyStop) m_onEmergencyStop();
            return true;

        case 'A':
            // Likewise documented, and SetAutofocusToggleHandler was
            // registered in main, but no case ever invoked it -- the
            // handler had been dead since it was written.
            if (firstPress && m_onAutofocusToggle) m_onAutofocusToggle();
            return true;
        case 'R':
            if (firstPress && m_onReticleToggle) m_onReticleToggle();
            return true;
        case 'C':
            if (firstPress && m_onClipToggle) m_onClipToggle();
            return true;
        case 'S':
            // Phase 39: Shift+S is the framing shot — a boosted
            // snapshot, so it sits on the snapshot key rather than
            // taking a new letter. The spec asked for F, but F has
            // stepped the focuser since long before this existed and
            // is used far more often.
            if (shift) { if (firstPress && m_onFramingShot) m_onFramingShot(); }
            else       { if (firstPress && m_onSnapshot)    m_onSnapshot();    }
            return true;
        case 'Q':
            if (firstPress && m_onQualityGraphToggle) m_onQualityGraphToggle();
            return true;
        case 'P':
            if (firstPress && m_onPause) m_onPause();
            return true;
        case 'V':
            if (firstPress && m_onRGBAlign) m_onRGBAlign();
            return true;
        case 'X':
            if (firstPress && m_onPixelReadout) m_onPixelReadout();
            return true;
        case 'B':
            if (firstPress && m_onFXCycle) m_onFXCycle();
            return true;
        case 'K':   // stacK the last recording in AutoStakkert
            if (firstPress && m_onStackLast) m_onStackLast();
            return true;
        case 'N':
            if (firstPress && m_onNightVision) m_onNightVision();
            return true;
        case 'H':
            if (firstPress && m_onChannelCycle) m_onChannelCycle();
            return true;
        case 'Z':
            if (firstPress && m_onZoomCycle) m_onZoomCycle();
            return true;
        case 'G':
            if (firstPress && m_onGoToToggle) m_onGoToToggle();
            return true;
        case 'M':
            if (firstPress && m_onSkyMapToggle) m_onSkyMapToggle();
            return true;
        case 'F':
            if (shift) { if (m_onFocusCoarse) m_onFocusCoarse(100); }
            else       { if (m_onFocusFine)   m_onFocusFine(10);   }
            return true;
        case 'T':
            if (firstPress && m_onTrackingToggle) m_onTrackingToggle();
            return true;
        case VK_F11:
            if (firstPress && m_onFullscreenToggle) m_onFullscreenToggle();
            return true;
    }
    return false;
}

bool Window::HandleKeyUp(WPARAM wParam) {
    int axis = 0, dir = 0;
    if (!ArrowAxisDir(wParam, axis, dir)) return false;
    return StopArrowJog(axis, dir);
}

bool Window::HandleChar(WPARAM wParam) {
    // ? key -- toggle help window (Shift+/ on US keyboard)
    if (wParam == L'?' && m_onHelpToggle) { m_onHelpToggle(); return true; }
    // / key -- toggle AI chat panel
    if (wParam == L'/' && m_onAIToggle)   { m_onAIToggle();   return true; }
    // O key -- toggle ghost frame overlay
    if ((wParam == L'o' || wParam == L'O') && m_onGhostToggle) {
        m_onGhostToggle();
        return true;
    }
    return false;
}

// Called from the message pump for every key message before dispatch.
// Returns true when the key was consumed and must not be dispatched.
// -- Phase 61: preview-child mouse routing ----------------------------
// Mouse messages go to the window UNDER THE CURSOR, which is the preview
// child, so the main window's WM_LBUTTONDOWN never fired over the image.
// Forward the three events the preview renderer needs.
//
// Coordinates: the renderer hit-tests against m_lastDestRect, which lives
// in the pre-transform space of the render target on this child. Child
// client pixels therefore divide by the DPI scale -- the same convention
// the existing SetPixelHover call already used. At 96 DPI this is the
// identity, so it is a no-op on a standard display.
void Window::PreviewMouseCoords(LPARAM lParam, float& x, float& y) const {
    float s = (m_dpiScale > 0.0f) ? m_dpiScale : 1.0f;
    x = (float)GET_X_LPARAM(lParam) / s;
    y = (float)GET_Y_LPARAM(lParam) / s;
}

LRESULT CALLBACK Window::PreviewProc(HWND hwnd, UINT msg,
                                     WPARAM wParam, LPARAM lParam) {
    Window* self = reinterpret_cast<Window*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    float x = 0.0f, y = 0.0f;
    switch (msg) {
        case WM_LBUTTONDOWN:
            self->PreviewMouseCoords(lParam, x, y);
            // Capture so a drag leaving the child still completes -- an ROI
            // box dragged past the edge would otherwise never apply.
            SetCapture(hwnd);
            if (self->m_onPixelClick) self->m_onPixelClick(x, y);
            if (self->m_onClick) self->m_onClick(x, y);
            return 0;

        case WM_MOUSEMOVE:
            self->PreviewMouseCoords(lParam, x, y);
            if (self->m_onMouseMove) self->m_onMouseMove(x, y);
            return 0;

        case WM_LBUTTONUP:
            self->PreviewMouseCoords(lParam, x, y);
            ReleaseCapture();
            if (self->m_onMouseUp) self->m_onMouseUp(x, y);
            return 0;

        case WM_CAPTURECHANGED:
            // Capture stolen mid-drag (alt-tab, a popup, the system menu)
            // means WM_LBUTTONUP never arrives, leaving the renderer in
            // DragState::Dragging with a rubber band painted over the
            // image. End the drag. Coordinates are deliberately off-frame
            // so the selection is rejected rather than applied.
            if (self->m_onMouseUp) self->m_onMouseUp(-1.0f, -1.0f);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool Window::RouteThreadKey(const MSG& msg) {
    if (!m_hwnd || !msg.hwnd) return false;
    if (msg.message != WM_KEYDOWN && msg.message != WM_KEYUP &&
        msg.message != WM_CHAR)
        return false;

    // Messages already aimed at this window reach HandleMessage through
    // normal dispatch -- routing them here too would fire a hotkey twice.
    if (msg.hwnd == m_hwnd) return false;

    // Only windows of this process. PeekMessage should never hand us
    // anything else, but driving a telescope is not worth the assumption.
    DWORD pid = 0;
    GetWindowThreadProcessId(msg.hwnd, &pid);
    if (pid != GetCurrentProcessId()) return false;

    int axis = 0, dir = 0;
    const bool isArrow = (msg.message != WM_CHAR) &&
                         ArrowAxisDir(msg.wParam, axis, dir);

    // A key-up ending a jog we started must always get through, or the
    // mount keeps slewing. StopArrowJog reports whether this direction
    // was really held, so a stray key-up is handed back to the control
    // (an edit box moving its caret) instead of being swallowed.
    if (msg.message == WM_KEYUP && isArrow)
        return StopArrowJog(axis, dir);

    // Leave the key with the focused control when it legitimately wants
    // it. Edit boxes and list boxes answer WM_GETDLGCODE themselves, so
    // the caret keeps working in the chat input and the coordinate
    // fields, and the GoTo / align lists keep arrow navigation, without
    // this code hardcoding any class names.
    LRESULT dlgCode = SendMessageW(msg.hwnd, WM_GETDLGCODE, msg.wParam,
                                   reinterpret_cast<LPARAM>(&msg));
    if (dlgCode & (DLGC_WANTALLKEYS | DLGC_WANTMESSAGE)) return false;
    if (isArrow) {
        if (dlgCode & DLGC_WANTARROWS) return false;
    } else if (dlgCode & (DLGC_WANTCHARS | DLGC_HASSETSEL)) {
        // Anything accepting text keeps its letters, so 'S' does not fire
        // a snapshot mid-sentence.
        return false;
    }

    if (msg.message == WM_KEYDOWN) return HandleKeyDown(msg.wParam, msg.lParam);
    if (msg.message == WM_KEYUP)   return HandleKeyUp(msg.wParam);
    return HandleChar(msg.wParam);
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
    int topOffset = TOOLBAR_HEIGHT_PX + m_notifBarH;
    int previewH  = (height > topOffset) ? height - topOffset : height;

    // Resize the preview child window first
    if (m_previewHwnd) {
        SetWindowPos(m_previewHwnd, nullptr, 0, topOffset,
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
            if (m_onPixelClick) m_onPixelClick(x, y);
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

        // Key handling lives in HandleKeyDown/Up/Char (Phase 60). Both
        // this proc and the message-pump router call them, so a hotkey
        // behaves the same whether this window has focus or one of the
        // floating panels does.
        case WM_KEYDOWN:
            if (HandleKeyDown(wParam, lParam)) return 0;
            break;

        case WM_KEYUP:
            if (HandleKeyUp(wParam)) return 0;
            break;

        case WM_CHAR:
            if (HandleChar(wParam)) return 0;
            break;

        case WM_KILLFOCUS:
            // Failsafe: an arrow jog held while focus leaves this window
            // never sees its WM_KEYUP here. Modal dialogs run their own
            // message pump, so RouteThreadKey never sees their keys --
            // this is the bracket for that case.
            StopAllArrowJogs();
            break;

        case WM_ACTIVATEAPP:
            // Failsafe: with keys routed thread-wide a jog can be started
            // while a floating window holds focus, so this window's
            // WM_KILLFOCUS no longer brackets every jog. Leaving the
            // application entirely means the matching WM_KEYUP goes
            // elsewhere -- stop now rather than wait out the watchdog.
            if (wParam == FALSE) StopAllArrowJogs();
            break;


        case WM_CLOSE:
            if (m_onCloseRequest) {
                m_onCloseRequest(); // handler decides whether to destroy
                return 0;          // suppress default close behaviour
            }
            // No handler — fall through to default (DestroyWindow → WM_DESTROY)
            DestroyWindow(m_hwnd);
            return 0;

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
