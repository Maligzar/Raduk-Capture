#pragma once
// ============================================================
// Window.h — Win32 window management for RadukCapture
//
// Architecture (post-refactor):
//   Main window  — dark background, handles keyboard + resize
//   Preview child — D2D renders here; owns mouse for ROI/zoom
//   ToolbarStrip  — WS_CHILD at y=0, h=30
//   ControlPanel  — WS_CHILD at right edge
//   StatusBar     — drawn by D2D inside preview child
//
// D2D renders into m_previewHwnd, not the main HWND.
// Zero bleed: child window clipping handles all boundaries.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <cstdint>
#include <functional>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace raduk::gui {

class Window {
public:
    Window() = default;
    ~Window();

    bool Create(const wchar_t* title, int width, int height);
    void Run(std::function<void()> onRender);

    // ── Message routing ────────────────────────────────────────
    void SetScrollHandler(std::function<bool(WPARAM, LPARAM)> handler) {
        m_onScroll = handler;
    }

    // ── Action callbacks ───────────────────────────────────────
    void SetEmergencyStopHandler(std::function<void()> h)    { m_onEmergencyStop    = h; }
    void SetReticleToggleHandler(std::function<void()> h)    { m_onReticleToggle    = h; }
    void SetZoomCycleHandler(std::function<void()> h)        { m_onZoomCycle        = h; }
    void SetGoToToggleHandler(std::function<void()> h)       { m_onGoToToggle       = h; }
    void SetSkyMapToggleHandler(std::function<void()> h)     { m_onSkyMapToggle     = h; }
    void SetZoomWheelHandler(std::function<void(int)> h)     { m_onZoomWheel        = h; }
    void SetFocusFineHandler(std::function<void(int)> h)     { m_onFocusFine        = h; }
    void SetFocusCoarseHandler(std::function<void(int)> h)   { m_onFocusCoarse      = h; }
    void SetTrackingToggleHandler(std::function<void()> h)   { m_onTrackingToggle   = h; }
    void SetFullscreenToggleHandler(std::function<void()> h) { m_onFullscreenToggle = h; }
    void SetJogHandler(std::function<void(int,int,bool)> h)  { m_onJog              = h; }
    void SetAutofocusToggleHandler(std::function<void()> h)  { m_onAutofocusToggle  = h; }
    void SetMoveHandler(std::function<void()> h)             { m_onMove             = h; }
    void SetToolbarCommandHandler(std::function<bool(int)> h){ m_onToolbarCommand   = h; }

    // Mouse events — fired from the preview child window
    void SetClickHandler(std::function<bool(float,float)> h)     { m_onClick     = h; }
    void SetMouseMoveHandler(std::function<void(float,float)> h) { m_onMouseMove = h; }
    void SetMouseUpHandler(std::function<void(float,float)> h)   { m_onMouseUp   = h; }
    void SetUserMessageHandler(std::function<void(UINT)> h)      { m_onUserMsg   = h; }

    // ── Getters ────────────────────────────────────────────────
    HWND Handle()                         const { return m_hwnd; }
    HWND PreviewHandle()                  const { return m_previewHwnd; }
    ID2D1HwndRenderTarget* RenderTarget() const { return m_renderTarget; }
    IDWriteFactory* DWriteFactory()       const { return m_dwriteFactory; }
    int  ClientWidth()                    const { return m_clientWidth; }
    int  ClientHeight()                   const { return m_clientHeight; }

    // PreviewWidth: read from D2D render target pixel size (authoritative).
    int  PreviewWidth() const {
        if (m_renderTarget) {
            D2D1_SIZE_U sz = m_renderTarget->GetPixelSize();
            if (sz.width > 0) return (int)sz.width;
        }
        return (m_clientWidth > PANEL_RESERVED) ? m_clientWidth - PANEL_RESERVED : 100;
    }

    float DpiScale()      const { return m_dpiScale; }
    bool  IsOpen()        const { return m_hwnd != nullptr; }
    bool  IsFullscreen()  const { return m_fullscreen; }

    void ToggleFullscreen();

    // Panel reserved width — must match ControlPanel.h PANEL_WIDTH + scrollbar
    static constexpr int PANEL_RESERVED = 580;

private:
    // Main window proc
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                     WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // Preview child window proc — routes mouse to callbacks
    static LRESULT CALLBACK PreviewWndProc(HWND hwnd, UINT msg,
                                            WPARAM wParam, LPARAM lParam);

    bool InitDirect2D();
    void ReleaseDirect2D();
    void OnResize(int width, int height);

    HWND                    m_hwnd         = nullptr;
    HWND                    m_previewHwnd  = nullptr;
    int                     m_clientWidth  = 0;
    int                     m_clientHeight = 0;
    float                   m_dpiScale     = 1.0f;
    bool                    m_fullscreen   = false;
    WINDOWPLACEMENT         m_savedPlacement{};

    ID2D1Factory*           m_d2dFactory   = nullptr;
    ID2D1HwndRenderTarget*  m_renderTarget = nullptr;
    IDWriteFactory*         m_dwriteFactory= nullptr;

    std::function<bool(WPARAM, LPARAM)> m_onScroll;
    std::function<void()>               m_onEmergencyStop;
    std::function<void()>               m_onReticleToggle;
    std::function<void()>               m_onZoomCycle;
    std::function<void()>               m_onGoToToggle;
    std::function<void()>               m_onSkyMapToggle;
    std::function<void(int)>            m_onZoomWheel;
    std::function<void(int)>            m_onFocusFine;
    std::function<void(int)>            m_onFocusCoarse;
    std::function<void()>               m_onTrackingToggle;
    std::function<void()>               m_onFullscreenToggle;
    std::function<void(int,int,bool)>   m_onJog;
    std::function<void()>               m_onAutofocusToggle;
    std::function<void()>               m_onMove;
    std::function<bool(int)>            m_onToolbarCommand;
    std::function<bool(float,float)>    m_onClick;
    std::function<void(float,float)>    m_onMouseMove;
    std::function<void(float,float)>    m_onMouseUp;
    std::function<void(UINT)>           m_onUserMsg;
};

} // namespace raduk::gui
