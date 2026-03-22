#pragma once
// ============================================================
// PreviewRenderer.h
// Preview renderer — frame display + frame bar overlay.
// Frame control bar: thin strip on bottom edge of sensor frame.
//   - Debayer toggle: COLOR (RGGB) / RAW (grayscale Bayer)
//   - More controls to be added here later
// HandleClick(): forwards mouse clicks from Window to test
//   against frame bar buttons (returns true if consumed).
// ============================================================
#include <d2d1.h>
#include <dwrite.h>
#include <cstdint>
#include <array>
#include <chrono>
#include <functional>
#include "ASICamera2.h"
#include "../core/FrameSlot.h"
#include "../core/FrameAnalyzer.h"

namespace raduk::gui {

enum class ZoomMode    { Fit, One, Two, Four };
enum class DebayerMode { Color, Raw };  // Color=RGGB debayer, Raw=grayscale Bayer

class PreviewRenderer {
public:
    PreviewRenderer() = default;
    ~PreviewRenderer();

    bool Init(ID2D1HwndRenderTarget* renderTarget, IDWriteFactory* dwriteFactory);
    bool SetFrameSize(int width, int height);
    void Release();

    // Call order each tick:
    //   RenderFrame(...)           — BeginDraw, preview + reticle + HUD + frame bar
    //   DrawAnalysisOverlay(...)   — histogram + seeing (optional)
    //   statusBar.Draw(...)        — status bar strip
    //   EndFrame()                 — EndDraw, present
    void RenderFrame(const raduk::core::FrameSlot* frame,
                     int previewX, int previewWidth, int windowHeight,
                     int statusBarHeight,
                     double fps, uint64_t frameCount, int droppedFrames);

    void DrawAnalysisOverlay(const raduk::core::AnalysisResult& analysis,
                              int previewX, int previewWidth, int windowHeight,
                              int statusBarHeight);

    void EndFrame();

    // Forward mouse events from Window for ROI rubber-band selection
    // Returns true from HandleMouseDown if a drag was started on the image.
    bool HandleMouseDown(float x, float y);
    void HandleMouseMove(float x, float y);
    void HandleMouseUp(float x, float y);

    // Callback fired when user completes a ROI selection or clicks reset.
    void SetROIChangeCallback(std::function<void(int,int,int,int)> cb) { m_onROIChange = cb; }

    // State setters — kept so main.cpp compile doesn't break;
    // actual button visuals are now in ToolbarStrip::UpdateStates().
    void SetGoToPanelVisible(bool v) { m_goToPanelVisible = v; }
    void SetSkyMapVisible(bool v)    { m_skyMapVisible    = v; }
    void SetAcquisitionStage(int s)  { m_acqStage  = s; }
    void SetLockReady(bool r)        { m_lockReady = r; }
    void SetROIReady(bool r)         { m_roiReady  = r; }

    // Current sensor ROI — set externally when ROI changes
    void SetSensorROI(int w, int h) { m_sensorW = w; m_sensorH = h; }
    int  SensorW() const { return m_sensorW; }
    int  SensorH() const { return m_sensorH; }

    static constexpr int MAX_SENSOR_W = 3840;
    static constexpr int MAX_SENSOR_H = 2160;

    // ── Debayer control ────────────────────────────────────────
    DebayerMode GetDebayerMode() const  { return m_debayerMode; }
    void        SetDebayerMode(DebayerMode m);
    void        CycleDebayer();          // toggles Color ↔ Raw

    // ── View controls ──────────────────────────────────────────
    void ToggleReticle()        { m_showReticle = !m_showReticle; }
    bool ReticleVisible() const { return m_showReticle; }

    void CycleZoom() {
        switch (m_zoom) {
            case ZoomMode::Fit:  m_zoom = ZoomMode::One;  break;
            case ZoomMode::One:  m_zoom = ZoomMode::Two;  break;
            case ZoomMode::Two:  m_zoom = ZoomMode::Four; break;
            case ZoomMode::Four: m_zoom = ZoomMode::Fit;  break;
        }
    }
    void ZoomStep(int direction) {
        if (direction > 0) {
            if      (m_zoom == ZoomMode::Fit)  m_zoom = ZoomMode::One;
            else if (m_zoom == ZoomMode::One)  m_zoom = ZoomMode::Two;
            else if (m_zoom == ZoomMode::Two)  m_zoom = ZoomMode::Four;
        } else {
            if      (m_zoom == ZoomMode::Four) m_zoom = ZoomMode::Two;
            else if (m_zoom == ZoomMode::Two)  m_zoom = ZoomMode::One;
            else if (m_zoom == ZoomMode::One)  m_zoom = ZoomMode::Fit;
        }
    }
    ZoomMode CurrentZoom() const { return m_zoom; }
    const wchar_t* ZoomLabel() const {
        switch (m_zoom) {
            case ZoomMode::Fit:  return L"FIT";
            case ZoomMode::One:  return L"1:1";
            case ZoomMode::Two:  return L"2x";
            case ZoomMode::Four: return L"4x";
        }
        return L"FIT";
    }

    // ── ADC overlay ────────────────────────────────────────────
    // Feed current mount altitude so the ADC dispersion arrow updates live.
    void SetMountAltitude(double altDeg) { m_mountAltDeg = altDeg; }
    void SetShowADC(bool show)           { m_showADC = show; }
    bool ADCVisible() const              { return m_showADC; }
    void ToggleADC()                     { m_showADC = !m_showADC; }

private:
    void DebayerRGGB(const uint8_t* raw, uint8_t* rgb, int width, int height, int bitDepth);
    D2D1_RECT_F CalculateZoomedRect(int frameW, int frameH, int previewX, int previewW, int previewH);
    void DrawReticle(float cx, float cy, float rw, float rh);
    void DrawADCOverlay(float previewW, float previewH);
    void DrawRubberBand();
    void DrawResetPopup();

    static bool HitTest(float x, float y, const D2D1_RECT_F& r) {
        return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
    }

    ID2D1HwndRenderTarget*  m_renderTarget  = nullptr;
    IDWriteFactory*         m_dwriteFactory = nullptr;
    ID2D1Bitmap*            m_bitmap        = nullptr;   // CPU-path D2D bitmap
    ID2D1SolidColorBrush*   m_textBrush     = nullptr;
    ID2D1SolidColorBrush*   m_accentBrush   = nullptr;
    ID2D1SolidColorBrush*   m_dimBrush      = nullptr;
    ID2D1SolidColorBrush*   m_reticleBrush  = nullptr;
    ID2D1SolidColorBrush*   m_greenBrush    = nullptr;
    ID2D1SolidColorBrush*   m_amberBrush    = nullptr;
    ID2D1SolidColorBrush*   m_redBrush      = nullptr;
    ID2D1SolidColorBrush*   m_barBgBrush    = nullptr;  // frame bar background
    ID2D1SolidColorBrush*   m_barHiBrush    = nullptr;  // frame bar active button
    IDWriteTextFormat*      m_hudFont       = nullptr;
    IDWriteTextFormat*      m_titleFont     = nullptr;
    IDWriteTextFormat*      m_barFont       = nullptr;   // smaller font for frame bar

    uint8_t*                m_rgbBuffer     = nullptr;
    size_t                  m_rgbBufferSize = 0;
    int                     m_frameWidth    = 0;
    int                     m_frameHeight   = 0;

    bool                    m_showReticle   = false;
    ZoomMode                m_zoom          = ZoomMode::Fit;
    DebayerMode             m_debayerMode   = DebayerMode::Color;

    // ADC overlay
    double                  m_mountAltDeg   = -1.0;  // -1 = unknown
    bool                    m_showADC       = true;   // on by default

    // Last drawn frame rect — used for hit testing frame bar clicks
    D2D1_RECT_F             m_lastDestRect  = {};
    bool                    m_hasLastRect   = false;

    // Full preview region — frame bar spans this, not just the image rect.
    // Updated every RenderFrame call so DrawFrameBar always uses current extents.
    float                   m_lastPreviewX  = 0;
    float                   m_lastPreviewW  = 0;
    float                   m_lastPreviewH  = 0;

    // HUD status line rect — clickable to show reset popup
    D2D1_RECT_F             m_hudStatusRect  = {};
    D2D1_RECT_F             m_btnResetMax    = {};
    bool                    m_showResetPopup = false;

    bool                    m_goToPanelVisible = false;
    bool                    m_skyMapVisible    = false;
    int                     m_acqStage  = 0;
    bool                    m_lockReady = false;
    bool                    m_roiReady  = false;

    std::function<void(int,int,int,int)> m_onROIChange;

    // ROI rubber-band selection state
    enum class DragState { Idle, Dragging, Confirming };
    DragState               m_dragState     = DragState::Idle;
    float                   m_dragStartX    = 0;
    float                   m_dragStartY    = 0;
    float                   m_dragCurX      = 0;
    float                   m_dragCurY      = 0;
    int                     m_pendingROIW   = 0;
    int                     m_pendingROIH   = 0;
    int                     m_pendingROIX   = 0;
    int                     m_pendingROIY   = 0;
    std::chrono::steady_clock::time_point m_confirmTime;

    int                     m_sensorW       = MAX_SENSOR_W;
    int                     m_sensorH       = MAX_SENSOR_H;
};

} // namespace raduk::gui
