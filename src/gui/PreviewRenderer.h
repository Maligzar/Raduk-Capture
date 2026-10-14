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
#include <string>
#include <vector>
#include <deque>
#include <functional>
#include "../core/FrameSlot.h"
#include "core/CalibrationFrames.h"
#include "../core/FrameAnalyzer.h"
#include "../core/QuickLookStacker.h"

namespace raduk::gui {

enum class ZoomMode    { Fit, One, Two, Four };
enum class DebayerMode { Color, Raw };  // Color=RGGB debayer, Raw=grayscale Bayer

// Phase 25 — Display FX mode
enum class DisplayFX {
    Normal,    // no modification
    Boost2x,   // multiply channels ×2, clamp 255
    Boost4x,   // multiply channels ×4, clamp 255
    // Phase 74: quality-gated, centroid-aligned stacks of the last N
    // ADMITTED frames. Previously these were an unaligned mean over
    // every recent frame, which smeared the planet by the 10-25 px of
    // seeing jitter and made the result softer than a single frame.
    Stack3,    // best-of-recent, 3 admitted frames, aligned
    Stack5,    // best-of-recent, 5 admitted frames, aligned
    Stack10,   // best-of-recent, 10 admitted frames, aligned
};

class PreviewRenderer {
public:
    PreviewRenderer() = default;
    ~PreviewRenderer();

    bool Init(ID2D1HwndRenderTarget* renderTarget, IDWriteFactory* dwriteFactory);
    bool SetFrameSize(int width, int height);
    void Release();

    // Call order each tick:
    //   RenderFrame(...)           — BeginDraw, preview + reticle + HUD + frame bar
    //   DrawSidePanels(...)        — histogram + temperature panels
    //   statusBar.Draw(...)        — status bar strip
    //   EndFrame()                 — EndDraw, present
    void RenderFrame(const raduk::core::FrameSlot* frame,
                     int previewX, int previewWidth, int windowHeight,
                     int statusBarHeight,
                     double fps, uint64_t frameCount, int droppedFrames);

    // Permanent side panels: histogram to the left of the image, the
    // temperature chart to the right. Both used to be collapsible
    // sections in the control panel, where they competed for vertical
    // room with everything else and started collapsed -- so the two
    // readouts you want to glance at continuously were the two you had
    // to open first. The dark sky either side of a planet is the one
    // part of this window with space going spare, so they live there
    // now and are always on.
    //
    // Drawn even when the analysis is stale: the temperature trace is
    // meaningful with no frame at all, and a panel that vanishes
    // whenever the planet drifts out of the centroid is worse than one
    // showing its last reading.
    void DrawSidePanels(const raduk::core::AnalysisResult& analysis,
                        bool analysisValid,
                        int previewX, int previewWidth, int windowHeight,
                        int statusBarHeight);

    struct TempSample { float camC; float focC; };
    static constexpr int TEMP_HISTORY = 120;

    // Temperature sample, from the readout loop (~500ms) -- the same
    // source that used to feed the control panel's chart.
    void PushTempSample(float camC, float focC) {
        m_tempCamC = camC; m_tempFocC = focC; m_hasTempData = true;
        m_tempHistory.push_back({camC, focC});
        if ((int)m_tempHistory.size() > TEMP_HISTORY)
            m_tempHistory.pop_front();
    }

    void EndFrame();

    // Forward mouse events from Window for ROI rubber-band selection
    // Returns true from HandleMouseDown if a drag was started on the image.
    bool HandleMouseDown(float x, float y);
    void HandleMouseMove(float x, float y);
    void HandleMouseUp(float x, float y);

    // Callback fired when user completes a ROI selection or clicks reset.
    void SetROIChangeCallback(std::function<void(int,int,int,int)> cb) { m_onROIChange = cb; }

    // Phase 56 (review G2): the rubber-band 2s auto-apply only FLAGS the
    // ROI change during rendering. The main loop must call this after
    // ReleaseRead() — the callback reallocates the frame ring, which is
    // unsafe while a FrameSlot* from GetLatestFrame() is still held.
    void FireDeferredROIApply() {
        if (!m_roiAutoApplyPending) return;
        m_roiAutoApplyPending = false;
        if (m_onROIChange && m_pendingROIW > 0)
            m_onROIChange(m_pendingROIW, m_pendingROIH, m_pendingROIX, m_pendingROIY);
    }

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

    // ── Phases 27 & 28: dark and flat correction ──────────────
    // The renderer borrows the masters; it does not own them. Applied
    // to the PREVIEW only: frame->data is left untouched, so the
    // recording and the sharpness metric still see raw sensor data.
    // ── Phase 37: display stretch ─────────────────────────────
    // A transfer function on the DISPLAY only: out = (v-black)*255/
    // (white-black). Nothing recorded changes. This is how you see a
    // faint target without touching gain or exposure, which would.
    void SetStretch(int black, int white) {
        // Kept at least 4 apart. A zero or inverted window would divide
        // by zero or solarise the image, and someone dragging two
        // sliders will hit both.
        if (black < 0)   black = 0;
        if (white > 255) white = 255;
        if (white < black + 4) white = black + 4;
        if (white > 255) { white = 255; black = 251; }
        m_black = black; m_white = white;
    }
    void SetStretchEnabled(bool on) { m_stretchOn = on; }
    bool StretchEnabled() const     { return m_stretchOn; }
    int  StretchBlack()   const     { return m_black; }
    int  StretchWhite()   const     { return m_white; }
    // True only when it is on AND actually changing anything, so a
    // 0-255 window does not claim to be stretching.
    bool StretchActive() const {
        return m_stretchOn && (m_black > 0 || m_white < 255);
    }

    void SetCalibration(const raduk::core::CalibrationFrames* c) { m_cal = c; }
    void SetCalibrationEnabled(bool on) { m_calEnabled = on; }
    bool CalibrationEnabled() const     { return m_calEnabled; }
    // True only if a master was actually applied to the last frame
    // drawn -- enabled but mismatched reports false, because the
    // picture on screen is uncorrected and saying otherwise would be
    // the lie this flag exists to prevent.
    bool CalibrationActive() const      { return m_calApplied; }
    void        SetDebayerMode(DebayerMode m);
    void        CycleDebayer();          // toggles Color ↔ Raw

    // ── View controls ──────────────────────────────────────────
    // Phase 45: shapes, not just on/off. Crosshair is the original;
    // Rings exists for collimation, where three concentric circles
    // show whether the out-of-focus donut is centred; Box frames a
    // crop before committing to one.
    enum class ReticleShape { Crosshair = 0, Circle, Rings, Box };

    // One control rather than two. R used to toggle visibility and
    // there was nowhere to choose a shape; it now steps
    // Off -> Crosshair -> Circle -> Rings -> Box -> Off, so the first
    // press still just turns the reticle on, as it always did.
    void ToggleReticle() {
        if (!m_showReticle) { m_showReticle = true;
                              m_reticleShape = ReticleShape::Crosshair; return; }
        switch (m_reticleShape) {
            case ReticleShape::Crosshair: m_reticleShape = ReticleShape::Circle; break;
            case ReticleShape::Circle:    m_reticleShape = ReticleShape::Rings;  break;
            case ReticleShape::Rings:     m_reticleShape = ReticleShape::Box;    break;
            case ReticleShape::Box:       m_showReticle  = false;                break;
        }
    }
    bool ReticleVisible() const { return m_showReticle; }
    ReticleShape GetReticleShape() const { return m_reticleShape; }
    const wchar_t* ReticleLabel() const {
        if (!m_showReticle) return L"RETICLE";
        switch (m_reticleShape) {
            case ReticleShape::Circle: return L"RETICLE CIRCLE";
            case ReticleShape::Rings:  return L"RETICLE RINGS";
            case ReticleShape::Box:    return L"RETICLE BOX";
            default:                   return L"RETICLE CROSS";
        }
    }
    // Shape size as a fraction of the shorter preview edge. The
    // crosshair spans the frame and ignores this.
    // ── Phase 45: move, size and rotate ──────────────────────
    // Offsets are FRACTIONS of the preview, not pixels, so the reticle
    // stays where it was put when the window is resized or the zoom
    // changes. Pixels would drift every time the layout moved.
    void NudgeReticle(float dxFrac, float dyFrac) {
        m_reticleDX += dxFrac;
        m_reticleDY += dyFrac;
        // Half a frame in any direction. Further and it is off screen,
        // where it cannot be seen or dragged back.
        auto clamp = [](float v) { return v < -0.5f ? -0.5f
                                        : (v > 0.5f ? 0.5f : v); };
        m_reticleDX = clamp(m_reticleDX);
        m_reticleDY = clamp(m_reticleDY);
    }
    void SetReticleOffset(float dxFrac, float dyFrac) {
        m_reticleDX = 0.0f; m_reticleDY = 0.0f;
        NudgeReticle(dxFrac, dyFrac);
    }
    float ReticleDX() const { return m_reticleDX; }
    float ReticleDY() const { return m_reticleDY; }

    // Rotation only means anything for the shapes that are not radially
    // symmetric. A rotated circle, or three concentric rings, is the
    // same picture -- so the angle is kept but reported as inapplicable,
    // rather than silently accepted and visibly doing nothing.
    bool ReticleRotatable() const {
        return m_reticleShape == ReticleShape::Box
            || m_reticleShape == ReticleShape::Crosshair;
    }
    void RotateReticle(float degrees) {
        m_reticleRot += degrees;
        while (m_reticleRot >= 360.0f) m_reticleRot -= 360.0f;
        while (m_reticleRot <    0.0f) m_reticleRot += 360.0f;
    }
    float ReticleRotation() const { return m_reticleRot; }

    // Back to frame centre, original size, no rotation.
    void ResetReticleTransform() {
        m_reticleDX = m_reticleDY = 0.0f;
        m_reticleRot = 0.0f;
        m_reticleScale = 0.35f;
    }

    void SetReticleScale(float f) {
        if (f < 0.05f) f = 0.05f;
        if (f > 0.95f) f = 0.95f;
        m_reticleScale = f;
    }
    float ReticleScale() const { return m_reticleScale; }

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

    // ── Clipping warning overlay ───────────────────────────────
    // Flash overexposed pixels red in the bitmap. Feed clipPct from
    // FrameAnalyzer each frame. Toggle via CLIP toolbar button or key.
    void SetShowClip(bool show) { m_showClip = show; }
    bool ClipVisible()    const { return m_showClip; }
    void ToggleClip()           { m_showClip = !m_showClip; }
    void SetClipPct(float pct)  { m_clipPct = pct; }

    // ── Quality bar graph — Phase 25a ─────────────────────────
    // Rolling per-frame sharpness history drawn as a vertical bar
    // chart in the bottom-left of the preview. Toggle via Q key.
    // Everything drawn over the viewfinder, except the reticle.
    //
    // Covers the histogram/temperature side panels, the sharpness
    // graph, the ADC arrow, the caption, the zoom hint, the mode
    // labels, the pixel readout and the LIVE status line. Off leaves
    // the image and the reticle and nothing else.
    //
    // The reticle is deliberately exempt: it is the one overlay you
    // look THROUGH the image with rather than read off it, and clearing
    // the view to judge a target is exactly when you still want it.
    //
    // It started narrower -- just the readouts that had been duplicated
    // onto their own screens -- which is why the saved setting is still
    // called OverlayTelemetry in rig.ini. Renaming the key would lose
    // everyone's saved state to buy a tidier word.
    void SetShowOverlays(bool on) { m_showOverlays = on; }
    bool OverlaysVisible()  const { return m_showOverlays; }
    void ToggleOverlays()         { m_showOverlays = !m_showOverlays; }

    void SetShowQualityGraph(bool show) { m_showQualityGraph = show; }
    bool QualityGraphVisible()    const { return m_showQualityGraph; }
    void ToggleQualityGraph()           { m_showQualityGraph = !m_showQualityGraph; }
    // Push a new sharpness sample (0-100). Call each frame.
    void PushQualitySample(float sharpnessPct) {
        m_qualHistory[m_qualHead] = sharpnessPct;
        m_qualHead = (m_qualHead + 1) % QUAL_HISTORY;
        if (m_qualCount < QUAL_HISTORY) ++m_qualCount;
    }
    bool HasFrame() const { return m_rgbBuffer && m_frameWidth > 0 && m_frameHeight > 0; }
    // Save the current debayered RGB frame as PNG using WIC.
    bool SaveSnapshotPNG(const std::wstring& path);

    // Show "SAVED: filename" notification in HUD for ~3 seconds.
    void SetSnapshotNotification(const std::wstring& msg) {
        m_snapMsg   = msg;
        m_snapTimer = 180;
    }

    // ── RGB Align — Phase 23 ──────────────────────────────────
    // Shift blue channel UP by computed pixel offset to compensate
    // atmospheric dispersion. Auto value from ADCAssistant altitude.
    // Manual slider adds ±20px fine-tune on top. Display-only.
    void SetRGBAlignEnabled(bool e)    { m_rgbAlignEnabled = e; }
    bool RGBAlignEnabled()       const { return m_rgbAlignEnabled; }
    void ToggleRGBAlign()              { m_rgbAlignEnabled = !m_rgbAlignEnabled; }
    void SetRGBAlignManual(int px)     { m_rgbAlignManual = px; }
    int  RGBAlignManual()        const { return m_rgbAlignManual; }
    int  RGBAlignShift()         const { return m_rgbAlignShift; }

    // ── Pixel Value Readout — Phase 24 ───────────────────────
    // Hover: live tooltip at cursor. Click: lock to pixel. Click again: unlock.
    // Disabled by default — toggle with X key or panel button.
    void SetPixelReadoutEnabled(bool e) { m_pixelReadout = e; }
    bool PixelReadoutEnabled()    const { return m_pixelReadout; }
    void TogglePixelReadout()           { m_pixelReadout = !m_pixelReadout;
                                          if (!m_pixelReadout) m_pixelLocked = false; }
    // Feed mouse position (D2D coords, already DPI-corrected) each WM_MOUSEMOVE
    void SetPixelHover(float sx, float sy) {
        if (!m_pixelLocked) { m_pixelHoverX = sx; m_pixelHoverY = sy; }
    }
    // Left-click: toggle lock on current hover position
    void ClickPixelLock(float sx, float sy) {
        if (!m_pixelReadout) return;
        if (m_pixelLocked) { m_pixelLocked = false; }
        else { m_pixelHoverX = sx; m_pixelHoverY = sy; m_pixelLocked = true; }
    }

    // ── Display FX — Phase 25 ─────────────────────────────────
    // Boost or stack frames for display only. SER data unaffected.
    DisplayFX GetDisplayFX()       const { return m_displayFX; }
    void      SetDisplayFX(DisplayFX fx);   // defined in .cpp — handles alloc/free
    void      CycleDisplayFX() {
        // Normal → Boost2x → Boost4x → Stack3 → Stack5 → Stack10 → Normal
        switch (m_displayFX) {
            case DisplayFX::Normal:   SetDisplayFX(DisplayFX::Boost2x);  break;
            case DisplayFX::Boost2x:  SetDisplayFX(DisplayFX::Boost4x);  break;
            case DisplayFX::Boost4x:  SetDisplayFX(DisplayFX::Stack3);   break;
            case DisplayFX::Stack3:   SetDisplayFX(DisplayFX::Stack5);   break;
            case DisplayFX::Stack5:   SetDisplayFX(DisplayFX::Stack10);  break;
            case DisplayFX::Stack10:  SetDisplayFX(DisplayFX::Normal);   break;
        }
    }
    const wchar_t* DisplayFXLabel() const {
        switch (m_displayFX) {
            case DisplayFX::Boost2x:  return L"BOOST ×2";
            case DisplayFX::Boost4x:  return L"BOOST ×4";
            case DisplayFX::Stack3:   return L"STACK 3 BEST";
            case DisplayFX::Stack5:   return L"STACK 5 BEST";
            case DisplayFX::Stack10:  return L"STACK 10 BEST";
            default:                  return L"NORMAL";
        }
    }

    // Phase 74: quick-look stacker readout, for the HUD and the log.
    float       QuickLookLive()   const { return m_qlLive;  }
    float       QuickLookStack()  const { return m_qlStack; }
    int         QuickLookDepth()  const { return m_fxFilled; }
    bool        QuickLookActive() const { return m_fxN > 0; }
    bool        QuickLookLearning() const { return m_ql.Learning(); }
    int         QuickLookAdmitted() const { return m_ql.Admitted(); }
    const char* QuickLookReason() const { return m_qlReason; }

    // Phase 26: Night Vision Mode — red tint for dark adaptation
    void SetNightVision(bool on)   { m_nightVision = on; }
    bool NightVisionOn()     const { return m_nightVision; }
    void ToggleNightVision()       { m_nightVision = !m_nightVision; }

    // Phase 29: Ghost Frame Overlay
    // CaptureGhost() snaps the current RGB frame as reference.
    // ToggleGhost() captures on first press, clears on second (and any time
    // GhostEnabled() is true), matching the [GH] button cycle.
    void CaptureGhost() {
        if (!m_rgbBuffer || m_frameWidth == 0 || m_frameHeight == 0) return;
        size_t bytes = (size_t)m_frameWidth * m_frameHeight * 3;
        if (m_ghostFrameSize != bytes) {
            delete[] m_ghostFrame;
            m_ghostFrame     = new uint8_t[bytes];
            m_ghostFrameSize = bytes;
        }
        memcpy(m_ghostFrame, m_rgbBuffer, bytes);
        m_ghostW       = m_frameWidth;
        m_ghostH       = m_frameHeight;
        m_ghostEnabled = true;
    }
    void ClearGhost() {
        m_ghostEnabled = false;
    }
    void ToggleGhost() {
        if (m_ghostEnabled) ClearGhost();
        else                CaptureGhost();
    }
    bool GhostEnabled() const { return m_ghostEnabled; }

    // Phase 38: Single-Channel Display Mode — R / G / B / All
    enum class ChannelMode { All = 0, R, G, B };
    ChannelMode GetChannelMode()   const { return m_channelMode; }
    void CycleChannelMode() {
        switch (m_channelMode) {
            case ChannelMode::All: m_channelMode = ChannelMode::R; break;
            case ChannelMode::R:   m_channelMode = ChannelMode::G; break;
            case ChannelMode::G:   m_channelMode = ChannelMode::B; break;
            case ChannelMode::B:   m_channelMode = ChannelMode::All; break;
        }
    }
    const wchar_t* ChannelLabel() const {
        switch (m_channelMode) {
            case ChannelMode::R:  return L"CH: RED";
            case ChannelMode::G:  return L"CH: GREEN";
            case ChannelMode::B:  return L"CH: BLUE";
            default:              return L"CH: ALL";
        }
    }

private:
    void DebayerRGGB(const uint8_t* raw, uint8_t* rgb, int width, int height, int bitDepth);
    D2D1_RECT_F CalculateZoomedRect(int frameW, int frameH, int previewX, int previewW, int previewH);
    // Brush lifecycle, separate from Init/Release so a palette change
    // does not tear down the fonts and frame buffers as well.
    bool CreateBrushes();
    void ReleaseBrushes();
    unsigned m_brushPaletteGen = 0;

    void DrawReticle(float cx, float cy, float rw, float rh);
    void DrawADCOverlay(float previewW, float previewH);
    void DrawQualityGraph(float px, float pw, float ph);
    void DrawHistogramPanel(const raduk::core::AnalysisResult& a, bool valid,
                            float x, float y, float w, float h);
    void DrawTempPanel(float x, float y, float w, float h);
    void FillPanelBg(float x, float y, float w, float h);
    void DrawPixelReadout(float px, float pw, float ph);
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
    // Side-panel channel colours, cached rather than created per frame:
    // the histogram redraws three rows of bars at up to 30fps.
    ID2D1SolidColorBrush*   m_histRBrush    = nullptr;
    ID2D1SolidColorBrush*   m_histGBrush    = nullptr;
    ID2D1SolidColorBrush*   m_histBBrush    = nullptr;
    ID2D1SolidColorBrush*   m_panelBgBrush  = nullptr;  // translucent backing
    IDWriteTextFormat*      m_hudFont       = nullptr;
    IDWriteTextFormat*      m_titleFont     = nullptr;
    IDWriteTextFormat*      m_barFont       = nullptr;   // smaller font for frame bar

    std::deque<TempSample> m_tempHistory;
    float m_tempCamC    = 0.0f;
    float m_tempFocC    = 0.0f;
    bool  m_hasTempData = false;

    uint8_t*                m_rgbBuffer     = nullptr;
    size_t                  m_rgbBufferSize = 0;
    int                     m_frameWidth    = 0;
    int                     m_frameHeight   = 0;

    bool                    m_showReticle   = false;
    ReticleShape            m_reticleShape  = ReticleShape::Crosshair;
    float                   m_reticleScale  = 0.35f;
    float                   m_reticleDX     = 0.0f;   // fraction of width
    float                   m_reticleDY     = 0.0f;   // fraction of height
    float                   m_reticleRot    = 0.0f;   // degrees
    ZoomMode                m_zoom          = ZoomMode::Fit;
    DebayerMode             m_debayerMode   = DebayerMode::Color;

    // ADC overlay
    double                  m_mountAltDeg   = -1.0;
    bool                    m_showADC       = true;

    // Clipping warning
    bool                    m_showClip      = false;
    float                   m_clipPct       = 0.0f;
    int                     m_clipFlash     = 0;

    // Snapshot notification (Phase 22)
    std::wstring            m_snapMsg;
    int                     m_snapTimer     = 0;

    // RGB Align (Phase 23)
    bool                    m_rgbAlignEnabled = false;
    int                     m_rgbAlignManual  = 0;     // user fine-tune ±20px
    int                     m_rgbAlignShift   = 0;     // last applied shift (for HUD)

    // Pixel Value Readout (Phase 24)
    bool                    m_pixelReadout  = false;
    bool                    m_pixelLocked   = false;
    float                   m_pixelHoverX   = 0.0f;
    float                   m_pixelHoverY   = 0.0f;

    // Display FX (Phase 25)
    DisplayFX               m_displayFX     = DisplayFX::Normal;
    // Stack accumulator — uint32 per channel to avoid overflow
    static constexpr int    FX_STACK_MAX    = 10;
    uint8_t*                m_fxFrames[FX_STACK_MAX] = {}; // circular history of RGB frames
    uint32_t*               m_fxAccum       = nullptr;     // running sum (R,G,B per pixel)
    int                     m_fxHead        = 0;           // next slot to write
    int                     m_fxFilled      = 0;           // how many slots valid
    int                     m_fxN           = 0;           // current stack depth (3/5/10)
    // Phase 74 quick-look stacker. The stacker owns no pixels; these
    // buffers and their teardown stay here, in SetFrameSize.
    raduk::core::QuickLookStacker m_ql;
    std::vector<uint8_t>    m_fxStackBuf;                  // last composed stack
    float                   m_qlLive        = 0.0f;        // live frame score
    float                   m_qlStack       = 0.0f;        // composed stack score
    const char*             m_qlReason      = "";          // why a frame was skipped

    // Night Vision Mode (Phase 26)
    bool                    m_nightVision   = false;
    const raduk::core::CalibrationFrames* m_cal = nullptr;
    bool                    m_calEnabled    = true;
    bool                    m_stretchOn     = false;
    int                     m_black         = 0;
    int                     m_white         = 255;
    bool                    m_calApplied    = false;
    std::vector<uint8_t>    m_calBuf;

    // Single-Channel Display Mode (Phase 38)
    ChannelMode             m_channelMode   = ChannelMode::All;

    // Ghost Frame Overlay (Phase 29)
    // Stores a snapshot of m_rgbBuffer at the moment CaptureGhost() is called.
    // Blended at 50% opacity over the live frame for framing reference.
    uint8_t*                m_ghostFrame    = nullptr;
    size_t                  m_ghostFrameSize= 0;
    int                     m_ghostW        = 0;
    int                     m_ghostH        = 0;
    bool                    m_ghostEnabled  = false;

    // Quality bar graph (Phase 25a)
    static constexpr int    QUAL_HISTORY    = 200; // ~5s at 40fps
    float                   m_qualHistory[200] = {};
    int                     m_qualHead      = 0;
    int                     m_qualCount     = 0;
    bool                    m_showOverlays = true;
    bool                    m_showQualityGraph = true; // on by default

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
    // v1.1.1: last-frame hold — preview redraws the newest frame between
    // camera deliveries instead of flashing NO SIGNAL (see RenderFrame)
    static constexpr int    HOLD_TIMEOUT_MS = 2000;
    std::vector<uint8_t>    m_holdBuf;
    int                     m_holdW = 0, m_holdH = 0;
    uint8_t                 m_holdDepth = 8;
    std::chrono::steady_clock::time_point m_holdTime{};

    bool                    m_roiAutoApplyPending = false; // Phase 56 (G2)
    int                     m_pendingROIW   = 0;
    int                     m_pendingROIH   = 0;
    int                     m_pendingROIX   = 0;
    int                     m_pendingROIY   = 0;
    std::chrono::steady_clock::time_point m_confirmTime;

    int                     m_sensorW       = MAX_SENSOR_W;
    int                     m_sensorH       = MAX_SENSOR_H;
};

} // namespace raduk::gui
