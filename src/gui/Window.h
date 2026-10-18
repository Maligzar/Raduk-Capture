#pragma once
// ============================================================
// Window.h — Win32 window management for RadukCapture
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
    // NOTE: WM_COMMAND and WM_HSCROLL from the panel's own WndProc
    // are handled inside ControlPanel directly — no forwarding needed.
    void SetScrollHandler(std::function<bool(WPARAM, LPARAM)> handler) {
        m_onScroll = handler;
    }

    // ── Action callbacks ───────────────────────────────────────
    void SetEmergencyStopHandler(std::function<void()> h)    { m_onEmergencyStop    = h; }
    void SetReticleToggleHandler(std::function<void()> h)    { m_onReticleToggle    = h; }
    void SetClipToggleHandler(std::function<void()> h)       { m_onClipToggle       = h; }
    void SetSnapshotHandler(std::function<void()> h)         { m_onSnapshot         = h; }
    void SetQualityGraphToggleHandler(std::function<void()> h){ m_onQualityGraphToggle = h; }
    void SetPauseHandler(std::function<void()> h)             { m_onPause              = h; }
    void SetRGBAlignHandler(std::function<void()> h)          { m_onRGBAlign           = h; }
    void SetSkyMap2ToggleHandler(std::function<void()> h)     { m_onSkyMap2Toggle      = h; }  // Shift+M
    void SetObjectPanelToggleHandler(std::function<void()> h) { m_onObjectPanelToggle  = h; }  // Shift+I
    void SetObjectPanelFullscreenHandler(std::function<void()> h) { m_onObjectPanelFullscreen = h; }
    void SetShareViewToggleHandler(std::function<void()> h)   { m_onShareViewToggle    = h; }  // Shift+V
    void SetShareViewFullscreenHandler(std::function<void()> h){ m_onShareViewFullscreen = h; } // Ctrl+Shift+V
    void SetPixelReadoutHandler(std::function<void()> h)      { m_onPixelReadout        = h; }
    void SetFXCycleHandler(std::function<void()> h)           { m_onFXCycle             = h; }
    void SetStackLastHandler(std::function<void()> h)         { m_onStackLast           = h; }
    void SetNightVisionHandler(std::function<void()> h)       { m_onNightVision         = h; }
    void SetChannelCycleHandler(std::function<void()> h)      { m_onChannelCycle        = h; }  // H key
    void SetFramingShotHandler(std::function<void()> h)       { m_onFramingShot        = h; }  // Shift+S
    void SetAdvisoryToggleHandler(std::function<void()> h)    { m_onAdvisoryToggle     = h; }
    void SetStretchToggleHandler(std::function<void()> h)     { m_onStretchToggle     = h; }  // L key
    // Phase 45: reticle transform. Ctrl+wheel sizes, Shift+wheel
    // rotates, Ctrl+drag moves. Deltas in window pixels; the handler
    // converts to fractions of the frame.
    void SetReticleSizeHandler(std::function<void(int)> h)    { m_onReticleSize   = h; }
    void SetReticleRotateHandler(std::function<void(int)> h)  { m_onReticleRotate = h; }
    void SetReticleDragHandler(std::function<void(float,float)> h) { m_onReticleDrag = h; }
    // Fires once when the gesture finishes. Reporting during the drag
    // would mean a notification per mouse-move message; reporting at the
    // end is both quieter and the only point at which the result is
    // final enough to be worth stating.
    void SetReticleDragEndHandler(std::function<void()> h) { m_onReticleDragEnd = h; }
    void SetHelpToggleHandler(std::function<void()> h)        { m_onHelpToggle          = h; }  // ? key
    void SetAIToggleHandler(std::function<void()> h)          { m_onAIToggle            = h; }  // / key
    void SetGhostToggleHandler(std::function<void()> h)       { m_onGhostToggle         = h; }  // O key

    // Called when user clicks X or presses Alt+F4.
    // Handler should show confirm dialog if needed, then call DestroyWindow(Handle())
    // to proceed. If handler is not set, default WM_CLOSE behaviour (destroy) applies.
    void SetCloseRequestHandler(std::function<void()> h)      { m_onCloseRequest        = h; }

    // Called by main when notification bar shows/hides to reflow preview child
    void SetNotifBarHeight(int h) {
        if (h == m_notifBarH) return;
        m_notifBarH = h;
        // Trigger a layout update by firing OnResize with current dimensions
        OnResize(m_clientWidth, m_clientHeight);
    }
    int NotifBarHeight() const { return m_notifBarH; }
    void SetMouseMoveHandler(std::function<void(float,float)> h) { m_onMouseMove        = h; }
    void SetPixelClickHandler(std::function<void(float,float)> h){ m_onPixelClick        = h; }
    void SetZoomCycleHandler(std::function<void()> h)        { m_onZoomCycle        = h; }
    void SetGoToToggleHandler(std::function<void()> h)       { m_onGoToToggle       = h; }
    void SetSkyMapToggleHandler(std::function<void()> h)     { m_onSkyMapToggle     = h; }
    void SetZoomWheelHandler(std::function<void(int)> h)     { m_onZoomWheel        = h; }
    void SetFocusFineHandler(std::function<void(int)> h)     { m_onFocusFine        = h; }
    void SetFocusCoarseHandler(std::function<void(int)> h)   { m_onFocusCoarse      = h; }
    void SetTrackingToggleHandler(std::function<void()> h)   { m_onTrackingToggle   = h; }
    void SetFullscreenToggleHandler(std::function<void()> h) { m_onFullscreenToggle = h; }
    // Ctrl+Shift+Home — gather every window back onto the primary.
    void SetGatherWindowsHandler(std::function<void()> h) { m_onGatherWindows = h; }
    // Ctrl+Shift+End — record where the windows are now as the layout.
    void SetSaveLayoutHandler(std::function<void()> h) { m_onSaveLayout = h; }
    // Screens came or went -- a monitor woke, slept, was unplugged, or a
    // driver reset the topology.
    void SetDisplayChangeHandler(std::function<void()> h) { m_onDisplayChange = h; }
    // Shift+H — the stand-alone histogram window.
    void SetHistogramToggleHandler(std::function<void()> h) { m_onHistogramToggle = h; }
    // Shift+Q — the stand-alone sharpness window.
    void SetSharpnessToggleHandler(std::function<void()> h) { m_onSharpnessToggle = h; }
    // Shift+T — the stand-alone temperature window.
    void SetTemperatureToggleHandler(std::function<void()> h) { m_onTemperatureToggle = h; }
    // U — everything drawn over the viewfinder except the reticle.
    void SetOverlayToggleHandler(std::function<void()> h) { m_onOverlayToggle = h; }
    // Shift+A — the ADC alignment window.
    void SetADCAlignHandler(std::function<void()> h) { m_onADCAlign = h; }
    // Shift+D — the stand-alone status window (disk, buffer, FPS, lost).
    void SetStatusToggleHandler(std::function<void()> h) { m_onStatusToggle = h; }
    void SetJogHandler(std::function<void(int,int,bool)> h)  { m_onJog              = h; }
    void SetAutofocusToggleHandler(std::function<void()> h)  { m_onAutofocusToggle  = h; }

    // Called when main window moves or resizes — panel uses this to stay docked
    void SetMoveHandler(std::function<void()> h) { m_onMove = h; }
    // Toolbar button commands — return true to consume
    void SetToolbarCommandHandler(std::function<bool(int)> h) { m_onToolbarCommand = h; }

    // Called on WM_LBUTTONDOWN — x/y in client coords. Return true to consume.
    void SetClickHandler(std::function<bool(float,float)> h)        { m_onClick     = h; }
    // Called on WM_LBUTTONUP
    void SetMouseUpHandler(std::function<void(float,float)> h)      { m_onMouseUp   = h; }
    // Called on WM_USER + n — for cross-thread signalling (e.g. from WS command thread)
    void SetUserMessageHandler(std::function<void(UINT)> h)         { m_onUserMsg   = h; }

    // ── Getters ────────────────────────────────────────────────
    HWND Handle()                         const { return m_hwnd; }
    HWND PreviewHandle()                  const { return m_previewHwnd; }
    ID2D1HwndRenderTarget* RenderTarget() const { return m_renderTarget; }
    IDWriteFactory* DWriteFactory()       const { return m_dwriteFactory; }
    int  ClientWidth()                    const { return m_clientWidth; }
    int  ClientHeight()                   const { return m_clientHeight; }
    // Authoritative preview width — reads directly from the D2D render target size.
    // Always in sync with what D2D actually renders into, regardless of DPI scaling
    // or any race between WM_SIZE and the render loop.
    int  PreviewWidth() const {
        if (m_renderTarget) {
            D2D1_SIZE_U sz = m_renderTarget->GetPixelSize();
            if (sz.width > 0) return (int)sz.width;
        }
        int pw = m_clientWidth - 580;  // render target reservation
        return pw > 100 ? pw : 100;
    }
    // Phase 45: the reticle drag works in fractions of the preview, so
    // it needs the preview's height as well as its width.
    int  PreviewHeight() const {
        if (m_renderTarget) {
            D2D1_SIZE_U sz = m_renderTarget->GetPixelSize();
            if (sz.height > 0) return (int)sz.height;
        }
        int ph = m_clientHeight - 30 - 30;   // toolbar + status bar
        return ph > 100 ? ph : 100;
    }
    float DpiScale() const { return m_dpiScale; }
    bool IsOpen()                         const { return m_hwnd != nullptr; }
    bool IsFullscreen()                   const { return m_fullscreen; }

    // Fullscreen: stretches main window to cover the monitor.
    // Panel window is unaffected — stays wherever it is.
    void ToggleFullscreen();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // -- Phase 60: thread-wide key routing ----------------------
    // Every hotkey used to live in this window's WM_KEYDOWN, so it only
    // fired while this window held focus. Clicking a panel text field, or
    // opening Help / the AI chat / GoTo / Object Info / the align list,
    // moves focus to another of our windows and the keys went dead until
    // the main window was re-activated. RouteThreadKey is called from the
    // message pump before dispatch, so the keys work whichever of our
    // windows or controls has focus; HandleMessage calls the same
    // handlers, so behaviour is identical on both paths.
    // -- Phase 61: preview-child mouse routing -----------------
    // The preview is a child HWND covering the whole image area. Its
    // class proc was DefWindowProcW, so every click on the image was
    // discarded and the rubber-band ROI and pixel-readout click never
    // received a single event. (Scroll-zoom worked because
    // WM_MOUSEWHEEL goes to the FOCUSED window, not the one under the
    // cursor -- a different routing rule, hence the different outcome.)
    static LRESULT CALLBACK PreviewProc(HWND hwnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam);
    // Child-client pixels -> the units PreviewRenderer hit-tests in.
    void PreviewMouseCoords(LPARAM lParam, float& x, float& y) const;

    bool RouteThreadKey(const MSG& msg);
    bool HandleKeyDown(WPARAM wParam, LPARAM lParam);
    bool HandleKeyUp(WPARAM wParam);
    bool HandleChar(WPARAM wParam);

    // Arrow jogs are tracked per axis+direction so diagonals work and a
    // stop is only ever sent for a direction actually held.
    void StartArrowJog(int axis, int dir);
    bool StopArrowJog(int axis, int dir);
    void StopAllArrowJogs();

    bool InitDirect2D();
    void ReleaseDirect2D();
    void OnResize(int width, int height);

    HWND                    m_hwnd         = nullptr;
    HWND                    m_previewHwnd  = nullptr;  // D2D renders into this child
    int                     m_clientWidth  = 0;
    int                     m_clientHeight = 0;
    float                   m_dpiScale     = 1.0f;
    bool                    m_fullscreen   = false;
    bool                    m_arrowJogHeld[2][2] = {}; // [axis][dir] keyboard jogs in progress
    WINDOWPLACEMENT         m_savedPlacement{};

    ID2D1Factory*           m_d2dFactory   = nullptr;
    ID2D1HwndRenderTarget*  m_renderTarget = nullptr;
    IDWriteFactory*         m_dwriteFactory= nullptr;

    std::function<bool(WPARAM, LPARAM)> m_onScroll;
    std::function<void()>               m_onEmergencyStop;
    std::function<void()>               m_onReticleToggle;
    std::function<void()>               m_onClipToggle;
    std::function<void()>               m_onSnapshot;
    std::function<void()>               m_onQualityGraphToggle;
    std::function<void()>               m_onPause;
    std::function<void()>               m_onRGBAlign;
    std::function<void()>               m_onSkyMap2Toggle;
    std::function<void()>               m_onObjectPanelToggle;
    std::function<void()>               m_onObjectPanelFullscreen;
    std::function<void()>               m_onShareViewToggle;
    std::function<void()>               m_onShareViewFullscreen;
    std::function<void()>               m_onPixelReadout;
    std::function<void()>               m_onFXCycle;
    std::function<void()>               m_onStackLast;
    std::function<void()>               m_onNightVision;
    std::function<void()>               m_onChannelCycle;
    std::function<void()>               m_onFramingShot;
    std::function<void()>               m_onAdvisoryToggle;
    std::function<void()>               m_onStretchToggle;
    std::function<void(int)>            m_onReticleSize;
    std::function<void(int)>            m_onReticleRotate;
    std::function<void(float,float)>    m_onReticleDrag;
    std::function<void()>               m_onReticleDragEnd;
    bool  m_reticleDragging = false;
    float m_reticleLastX = 0.0f, m_reticleLastY = 0.0f;
    std::function<void()>               m_onHelpToggle;
    std::function<void()>               m_onAIToggle;
    std::function<void()>               m_onGhostToggle;
    std::function<void()>               m_onCloseRequest;
    int                                 m_notifBarH = 0;
    std::function<void(float,float)>    m_onPixelClick;
    std::function<void()>               m_onZoomCycle;
    std::function<void()>               m_onGoToToggle;
    std::function<void()>               m_onSkyMapToggle;
    std::function<void(int)>            m_onZoomWheel;
    std::function<void(int)>            m_onFocusFine;
    std::function<void(int)>            m_onFocusCoarse;
    std::function<void()>               m_onTrackingToggle;
    std::function<void()>               m_onFullscreenToggle;
    std::function<void()>               m_onGatherWindows;
    std::function<void()>               m_onSaveLayout;
    std::function<void()>               m_onDisplayChange;
    std::function<void()>               m_onHistogramToggle;
    std::function<void()>               m_onSharpnessToggle;
    std::function<void()>               m_onTemperatureToggle;
    std::function<void()>               m_onOverlayToggle;
    std::function<void()>               m_onADCAlign;
    std::function<void()>               m_onStatusToggle;
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
