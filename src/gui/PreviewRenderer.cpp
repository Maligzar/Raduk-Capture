// ============================================================
// PreviewRenderer.cpp
// M10: GPU debayer + frame control bar overlay.
// ROI rubber-band: drag on preview image → select new ROI.
//   HandleMouseDown/Move/Up forwarded from Window WM_ messages.
//   Auto-applies after 2s confirmation delay.
// ============================================================
#include "PreviewRenderer.h"
#include "Theme.h"
#include "HistogramDraw.h"
#include "SharpnessDraw.h"
#include "TemperatureDraw.h"
#include "Version.h"
#include "core/ADCAssistant.h"
#include "core/RigConfig.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <wincodec.h>   // WIC — PNG encoder
#pragma comment(lib, "windowscodecs.lib")

namespace raduk::gui {

PreviewRenderer::~PreviewRenderer() { Release(); }

// Phase 26 / follow-up: brush creation split out of Init so the
// palette can be changed without tearing down the fonts and the
// frame buffers, which Release() also frees. The colours come from
// Theme, which tints them, so recreating is all that is needed.
bool PreviewRenderer::CreateBrushes() {
    HRESULT hr;
    hr = m_renderTarget->CreateSolidColorBrush(Theme::Color(Theme::TEXT),     &m_textBrush);
    if (FAILED(hr)) return false;
    hr = m_renderTarget->CreateSolidColorBrush(Theme::Color(Theme::ACCENT),   &m_accentBrush);
    if (FAILED(hr)) return false;
    hr = m_renderTarget->CreateSolidColorBrush(Theme::Color(Theme::TEXT_DIM), &m_dimBrush);
    if (FAILED(hr)) return false;
    hr = m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.0f, 0.1f, 0.1f, 1.0f), &m_reticleBrush);
    if (FAILED(hr)) return false;
    // These three go through NVf as of the side panels. They were raw
    // literals, so night vision left them green/amber at full strength.
    // That was survivable while they only tinted the quality graph, and
    // not once the temperature chart moved onto the preview: CAM and
    // SEEING then sat on an otherwise all-red screen at the eyepiece,
    // which is the one thing night vision exists to prevent.
    hr = m_renderTarget->CreateSolidColorBrush(
        Theme::NVf(D2D1::ColorF(0.30f, 1.0f, 0.43f, 0.85f)), &m_greenBrush);
    if (FAILED(hr)) return false;
    hr = m_renderTarget->CreateSolidColorBrush(
        Theme::NVf(D2D1::ColorF(1.0f, 0.67f, 0.0f, 0.85f)), &m_amberBrush);
    if (FAILED(hr)) return false;
    hr = m_renderTarget->CreateSolidColorBrush(
        Theme::NVf(D2D1::ColorF(1.0f, 0.23f, 0.36f, 0.85f)), &m_redBrush);
    if (FAILED(hr)) return false;
    // Frame bar brushes
    hr = m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.04f, 0.09f, 0.14f, 0.88f), &m_barBgBrush);  // dark translucent
    if (FAILED(hr)) return false;
    hr = m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.0f, 0.898f, 1.0f, 0.15f), &m_barHiBrush);   // cyan highlight
    if (FAILED(hr)) return false;
    // Side panels. Through Theme::NVf so night vision reaches them --
    // the raw ColorF brushes above predate that and stay as they were.
    hr = m_renderTarget->CreateSolidColorBrush(
        Theme::NVf(D2D1::ColorF(1.0f, 0.31f, 0.31f, 0.90f)), &m_histRBrush);
    if (FAILED(hr)) return false;
    hr = m_renderTarget->CreateSolidColorBrush(
        Theme::NVf(D2D1::ColorF(0.31f, 0.86f, 0.31f, 0.90f)), &m_histGBrush);
    if (FAILED(hr)) return false;
    hr = m_renderTarget->CreateSolidColorBrush(
        Theme::NVf(D2D1::ColorF(0.31f, 0.55f, 1.0f, 0.90f)), &m_histBBrush);
    if (FAILED(hr)) return false;
    hr = m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.55f), &m_panelBgBrush);
    if (FAILED(hr)) return false;
    m_brushPaletteGen = Theme::PaletteGeneration();
    return true;
}

void PreviewRenderer::ReleaseBrushes() {
    if (m_textBrush) { m_textBrush->Release(); m_textBrush = nullptr; }
    if (m_accentBrush) { m_accentBrush->Release(); m_accentBrush = nullptr; }
    if (m_dimBrush) { m_dimBrush->Release(); m_dimBrush = nullptr; }
    if (m_reticleBrush) { m_reticleBrush->Release(); m_reticleBrush = nullptr; }
    if (m_greenBrush) { m_greenBrush->Release(); m_greenBrush = nullptr; }
    if (m_amberBrush) { m_amberBrush->Release(); m_amberBrush = nullptr; }
    if (m_redBrush) { m_redBrush->Release(); m_redBrush = nullptr; }
    if (m_barBgBrush) { m_barBgBrush->Release(); m_barBgBrush = nullptr; }
    if (m_barHiBrush) { m_barHiBrush->Release(); m_barHiBrush = nullptr; }
    if (m_histRBrush) { m_histRBrush->Release(); m_histRBrush = nullptr; }
    if (m_histGBrush) { m_histGBrush->Release(); m_histGBrush = nullptr; }
    if (m_histBBrush) { m_histBBrush->Release(); m_histBBrush = nullptr; }
    if (m_panelBgBrush) { m_panelBgBrush->Release(); m_panelBgBrush = nullptr; }
}

bool PreviewRenderer::Init(ID2D1HwndRenderTarget* renderTarget,
                            IDWriteFactory* dwriteFactory) {
    m_renderTarget  = renderTarget;
    m_dwriteFactory = dwriteFactory;
    if (!m_renderTarget || !m_dwriteFactory) return false;

    if (!CreateBrushes()) return false;

    HRESULT hr;

    hr = m_dwriteFactory->CreateTextFormat(
        Theme::FONT_DATA, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-us", &m_hudFont);
    if (FAILED(hr)) return false;
    hr = m_dwriteFactory->CreateTextFormat(
        Theme::FONT_BANNER, nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"en-us", &m_titleFont);
    if (FAILED(hr)) return false;
    // Smaller font for frame bar buttons
    hr = m_dwriteFactory->CreateTextFormat(
        Theme::FONT_DATA, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-us", &m_barFont);
    if (FAILED(hr)) return false;

    printf("[PreviewRenderer] Initialized\n");

    // Color debayer (CPU RGGB) is the only debayer mode.
    // GPU path removed — caused NO SIGNAL on some driver/ROI configurations.
    m_debayerMode = DebayerMode::Color;
    printf("[PreviewRenderer] Color debayer (CPU RGGB) active\n");

    return true;
}

bool PreviewRenderer::SetFrameSize(int width, int height) {
    // Phase 29: If frame dimensions change, the ghost is stale — clear it
    if (width != m_frameWidth || height != m_frameHeight)
        m_ghostEnabled = false;

    m_frameWidth = width; m_frameHeight = height;

    // CPU-path RGB buffer (always maintained as fallback)
    size_t newSize = static_cast<size_t>(width) * height * 3;
    if (newSize != m_rgbBufferSize) {
        delete[] m_rgbBuffer;
        m_rgbBuffer = new (std::nothrow) uint8_t[newSize];
        if (!m_rgbBuffer) { m_rgbBufferSize = 0; return false; }
        m_rgbBufferSize = newSize;
        memset(m_rgbBuffer, 0, newSize);
    }

    // CPU-path D2D bitmap — only needed when debayer mode is CPU or Off
    if (m_bitmap) { m_bitmap->Release(); m_bitmap = nullptr; }
    {
        D2D1_SIZE_U bmpSize = D2D1::SizeU(width, height);
        D2D1_BITMAP_PROPERTIES bmpProps = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        HRESULT hr = m_renderTarget->CreateBitmap(bmpSize, bmpProps, &m_bitmap);
        if (FAILED(hr)) {
            printf("[PreviewRenderer] CreateBitmap (CPU) failed: 0x%lx\n", hr);
            return false;
        }
    }

    printf("[PreviewRenderer] Frame %dx%d  debayer: %s\n",
        width, height,
        m_debayerMode == DebayerMode::Color ? "COLOR" : "RAW");

    // Phase 25 / Phase 56 (review G1): the stack buffers were allocated for
    // the OLD frame size — memset with the new size overflowed the heap when
    // the ROI grew. Reallocate them at the new dimensions instead.
    if (m_fxAccum || m_fxFrames[0]) {
        for (int i = 0; i < FX_STACK_MAX; i++) {
            delete[] m_fxFrames[i];
            m_fxFrames[i] = nullptr;
        }
        delete[] m_fxAccum;
        m_fxAccum  = nullptr;
        m_fxHead = m_fxFilled = 0;
        // Phase 74: the stacker caches a reference position and a score
        // history in the OLD geometry; both are meaningless at a new
        // frame size, and a stale reference would shift every frame.
        m_ql.Reset();
        m_fxStackBuf.clear();
        m_qlLive = m_qlStack = 0.0f; m_qlReason = "";
        if (m_fxN > 0) { // stack mode still active — rebuild at new size
            size_t frameBytes = (size_t)width * height * 3;
            for (int i = 0; i < m_fxN; i++)
                m_fxFrames[i] = new (std::nothrow) uint8_t[frameBytes]();
            m_fxAccum = new (std::nothrow) uint32_t[(size_t)width * height * 3]();
        }
    }
    return true;
}

// ── Debayer mode control ───────────────────────────────────────

void PreviewRenderer::SetDebayerMode(DebayerMode m) {
    m_debayerMode = m;
    printf("[PreviewRenderer] Debayer mode: %s\n",
        m == DebayerMode::Color ? "COLOR" : "RAW");
}

void PreviewRenderer::CycleDebayer() {
    // Simple toggle: Color ↔ Raw
    SetDebayerMode(m_debayerMode == DebayerMode::Color
                   ? DebayerMode::Raw
                   : DebayerMode::Color);
}

// ── Mouse / drag handling ──────────────────────────────────────

// HandleMouseDown: called from WM_LBUTTONDOWN.
// Handles ROI rubber-band drag and reset popup clicks.
// Frame bar buttons removed — now native Win32 in ToolbarStrip.
bool PreviewRenderer::HandleMouseDown(float x, float y) {
    if (!m_hasLastRect) return false;

    // Start ROI drag if inside frame
    if (HitTest(x, y, m_lastDestRect)) {
        m_dragState  = DragState::Dragging;
        m_dragStartX = x;
        m_dragStartY = y;
        m_dragCurX   = x;
        m_dragCurY   = y;
        m_pendingROIW = 0;
        m_pendingROIH = 0;
        m_showResetPopup = false;
        return true;
    }

    // HUD status line (resolution readout) — click to show reset popup
    if (HitTest(x, y, m_hudStatusRect)) {
        m_showResetPopup = !m_showResetPopup;
        m_dragState = DragState::Idle;
        return true;
    }

    // Reset popup button — fire full-sensor reset
    if (m_showResetPopup && HitTest(x, y, m_btnResetMax)) {
        m_showResetPopup = false;
        m_dragState = DragState::Idle;
        if (m_onROIChange)
            m_onROIChange(MAX_SENSOR_W, MAX_SENSOR_H, 0, 0);
        return true;
    }

    // Clicking anywhere else while reset popup is visible — dismiss it
    if (m_showResetPopup) {
        m_showResetPopup = false;
        return true;
    }

    if (m_dragState == DragState::Confirming) {
        m_dragState = DragState::Idle;
    }
    return false;
}

void PreviewRenderer::HandleMouseMove(float x, float y) {
    if (m_dragState != DragState::Dragging) return;
    m_dragCurX = x;
    m_dragCurY = y;
}

void PreviewRenderer::HandleMouseUp(float x, float y) {
    if (m_dragState != DragState::Dragging) return;

    m_dragCurX = x;
    m_dragCurY = y;

    if (!m_hasLastRect) { m_dragState = DragState::Idle; return; }

    // ── Convert screen drag rect → sensor pixel rect ──────────
    // destRect is the rendered frame on screen.
    // Sensor is m_sensorW × m_sensorH at the current ROI.
    float dW = m_lastDestRect.right  - m_lastDestRect.left;
    float dH = m_lastDestRect.bottom - m_lastDestRect.top;
    if (dW < 1.0f || dH < 1.0f) { m_dragState = DragState::Idle; return; }

    // Clamp drag rect to destRect bounds
    float x0 = (m_dragStartX < m_dragCurX) ? m_dragStartX : m_dragCurX;
    float y0 = (m_dragStartY < m_dragCurY) ? m_dragStartY : m_dragCurY;
    float x1 = (m_dragStartX > m_dragCurX) ? m_dragStartX : m_dragCurX;
    float y1 = (m_dragStartY > m_dragCurY) ? m_dragStartY : m_dragCurY;

    x0 = (x0 < m_lastDestRect.left)   ? m_lastDestRect.left   : x0;
    y0 = (y0 < m_lastDestRect.top)    ? m_lastDestRect.top    : y0;
    x1 = (x1 > m_lastDestRect.right)  ? m_lastDestRect.right  : x1;
    y1 = (y1 > m_lastDestRect.bottom) ? m_lastDestRect.bottom : y1;

    // Reject tiny drags (< 20px in either dimension) — these are accidental
    // clicks or spurious mouse events, not intentional ROI selections.
    if ((x1 - x0) < 20.0f || (y1 - y0) < 20.0f) {
        m_dragState = DragState::Idle;
        return;
    }

    // Screen selection width/height as fraction of displayed frame
    float fracW = (x1 - x0) / dW;
    float fracH = (y1 - y0) / dH;

    // Map to sensor pixels
    float rawW = fracW * (float)m_sensorW;
    float rawH = fracH * (float)m_sensorH;

    // Snap to ASI constraints: width multiple of 8, height multiple of 2
    int snapW = ((int)rawW / 8) * 8;
    int snapH = ((int)rawH / 2) * 2;

    // Enforce minimum size
    snapW = (snapW < 64)  ? 64  : snapW;
    snapH = (snapH < 64)  ? 64  : snapH;

    // Enforce maximum (full sensor)
    snapW = (snapW > MAX_SENSOR_W) ? MAX_SENSOR_W : snapW;
    snapH = (snapH > MAX_SENSOR_H) ? MAX_SENSOR_H : snapH;

    // Reject if essentially the same as current ROI (< 5% different)
    float diffW = (float)(m_sensorW - snapW) / m_sensorW;
    float diffH = (float)(m_sensorH - snapH) / m_sensorH;
    if (diffW < 0.05f && diffH < 0.05f) {
        m_dragState = DragState::Idle;
        return;
    }

    m_pendingROIW  = snapW;
    m_pendingROIH  = snapH;

    // ── Compute centred start position ─────────────────────────
    // Find the centre of the drag selection in sensor coordinates.
    // The drag was made within destRect which maps to current ROI.
    // We need to express this in full-sensor coordinates.
    // Since SetROI without SetStartPos centres on the full sensor,
    // the current ROI top-left in sensor space is:
    //   roiOriginX = (MAX_SENSOR_W - m_sensorW) / 2
    //   roiOriginY = (MAX_SENSOR_H - m_sensorH) / 2
    // (This is what the ASI SDK does when no start pos is set.)
    // But after the first SetStartPos, we track it explicitly via
    // m_sensorOriginX/Y — we'll default to centred for now.
    float selCentreX = (x0 + x1) * 0.5f;
    float selCentreY = (y0 + y1) * 0.5f;
    float normX = (selCentreX - m_lastDestRect.left) / dW;  // 0..1 across current ROI
    float normY = (selCentreY - m_lastDestRect.top)  / dH;

    // Centre of selection in full-sensor pixel space
    float sensorCX = normX * (float)m_sensorW;
    float sensorCY = normY * (float)m_sensorH;

    // Desired top-left of new ROI on current ROI (then offset to full sensor below)
    int startX = (int)(sensorCX - snapW * 0.5f);
    int startY = (int)(sensorCY - snapH * 0.5f);

    // Clamp so ROI stays within current sensor ROI dimensions
    if (startX < 0) startX = 0;
    if (startY < 0) startY = 0;
    if (startX + snapW > m_sensorW) startX = m_sensorW - snapW;
    if (startY + snapH > m_sensorH) startY = m_sensorH - snapH;

    // Snap to ASI constraints
    startX = (startX / 8) * 8;
    startY = (startY / 2) * 2;

    m_pendingROIX  = startX;
    m_pendingROIY  = startY;
    m_dragState    = DragState::Confirming;
    m_confirmTime  = std::chrono::steady_clock::now();

    printf("[ROI] Selection: %dx%d → snapped %dx%d at sensor (%d,%d)\n",
           (int)rawW, (int)rawH, snapW, snapH, startX, startY);
}

// ── DrawRubberBand ─────────────────────────────────────────────
// Drawn over the preview image during drag and confirming states.

void PreviewRenderer::DrawRubberBand() {
    if (!m_renderTarget || !m_hasLastRect) return;
    if (m_dragState == DragState::Idle) return;

    // Auto-apply after 2 seconds in Confirming state.
    // Phase 56 (review G2): do NOT invoke the ROI callback from inside the
    // render pass — it reallocates the frame ring while the render loop
    // still holds a FrameSlot* from GetLatestFrame(), so the subsequent
    // ReleaseRead() touched freed memory. Flag it instead; the main loop
    // fires it via FireDeferredROIApply() after releasing the frame.
    if (m_dragState == DragState::Confirming) {
        auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - m_confirmTime).count();
        if (elapsed >= 2.0 && m_onROIChange && m_pendingROIW > 0) {
            m_dragState = DragState::Idle;
            m_showResetPopup = false;
            m_roiAutoApplyPending = true;
            return;
        }
    }

    // Compute rubber-band screen rect (clamped to destRect)
    float x0 = (m_dragStartX < m_dragCurX) ? m_dragStartX : m_dragCurX;
    float y0 = (m_dragStartY < m_dragCurY) ? m_dragStartY : m_dragCurY;
    float x1 = (m_dragStartX > m_dragCurX) ? m_dragStartX : m_dragCurX;
    float y1 = (m_dragStartY > m_dragCurY) ? m_dragStartY : m_dragCurY;

    x0 = (x0 < m_lastDestRect.left)   ? m_lastDestRect.left   : x0;
    y0 = (y0 < m_lastDestRect.top)    ? m_lastDestRect.top    : y0;
    x1 = (x1 > m_lastDestRect.right)  ? m_lastDestRect.right  : x1;
    y1 = (y1 > m_lastDestRect.bottom) ? m_lastDestRect.bottom : y1;

    D2D1_RECT_F selRect = D2D1::RectF(x0, y0, x1, y1);
    float selW = x1 - x0, selH = y1 - y0;
    if (selW < 2.0f || selH < 2.0f) return;

    // Dimming overlay outside selection
    ID2D1SolidColorBrush* dimBrush = nullptr;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0,0,0,0.45f), &dimBrush);
    if (dimBrush) {
        // Top strip
        m_renderTarget->FillRectangle(
            D2D1::RectF(m_lastDestRect.left, m_lastDestRect.top,
                         m_lastDestRect.right, y0), dimBrush);
        // Bottom strip
        m_renderTarget->FillRectangle(
            D2D1::RectF(m_lastDestRect.left, y1,
                         m_lastDestRect.right, m_lastDestRect.bottom), dimBrush);
        // Left strip
        m_renderTarget->FillRectangle(
            D2D1::RectF(m_lastDestRect.left, y0, x0, y1), dimBrush);
        // Right strip
        m_renderTarget->FillRectangle(
            D2D1::RectF(x1, y0, m_lastDestRect.right, y1), dimBrush);
        dimBrush->Release();
    }

    // Selection border — cyan dashed look (two overlapping rects)
    if (m_accentBrush) {
        m_renderTarget->DrawRectangle(selRect, m_accentBrush, 1.5f);
        // Corner handles
        const float hSz = 6.0f;
        float corners[4][2] = {{x0,y0},{x1,y0},{x0,y1},{x1,y1}};
        for (auto& c : corners) {
            m_renderTarget->FillRectangle(
                D2D1::RectF(c[0]-hSz/2, c[1]-hSz/2, c[0]+hSz/2, c[1]+hSz/2),
                m_accentBrush);
        }
    }

    // Live size readout centred in the selection
    if (m_hudFont) {
        wchar_t buf[64];
        int dispW = 0, dispH = 0;

        if (m_dragState == DragState::Confirming && m_pendingROIW > 0) {
            // Show snapped size with countdown
            double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - m_confirmTime).count();
            int countdown = (int)(2.0 - elapsed) + 1;
            swprintf(buf, 64, L"%d × %d  (applying in %ds)",
                     m_pendingROIW, m_pendingROIH, countdown);
        } else {
            // Live size during drag — calculate from current cursor
            float dW = m_lastDestRect.right - m_lastDestRect.left;
            float dH = m_lastDestRect.bottom - m_lastDestRect.top;
            if (dW > 0 && dH > 0) {
                dispW = ((int)((selW / dW) * m_sensorW) / 8) * 8;
                dispH = ((int)((selH / dH) * m_sensorH) / 2) * 2;
                dispW = (dispW < 64) ? 64 : dispW;
                dispH = (dispH < 64) ? 64 : dispH;
                swprintf(buf, 64, L"%d × %d", dispW, dispH);
            } else {
                buf[0] = 0;
            }
        }

        if (buf[0]) {
            // Background pill for readability
            D2D1_RECT_F labelRect = D2D1::RectF(
                x0 + selW/2.0f - 70.0f, y0 + selH/2.0f - 10.0f,
                x0 + selW/2.0f + 70.0f, y0 + selH/2.0f + 10.0f);

            ID2D1SolidColorBrush* bgBrush = nullptr;
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0,0,0,0.75f), &bgBrush);
            if (bgBrush) {
                m_renderTarget->FillRectangle(labelRect, bgBrush);
                bgBrush->Release();
            }

            m_hudFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            ID2D1SolidColorBrush* textBrush =
                (m_dragState == DragState::Confirming) ? m_amberBrush : m_accentBrush;
            m_renderTarget->DrawText(buf, (UINT32)wcslen(buf),
                                      m_hudFont, labelRect, textBrush);
        }
    }
}

// ── EndFrame ──────────────────────────────────────────────────

void PreviewRenderer::EndFrame() {
    if (!m_renderTarget) return;
    HRESULT hr = m_renderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
        printf("[PreviewRenderer] Render target lost\n");
}

// ── RenderFrame ───────────────────────────────────────────────

// ── Phase 74: shifted copy for the quick-look stacker ─────────
// Writes src into dst displaced by (dx, dy) buffer pixels, zero-filling
// the margin that shifts in. Row-wise memcpy over the overlapping span,
// so there are no per-pixel branches. dx/dy are always even, which keeps
// the debayer 2x2 block phase intact - an odd shift would only move that
// phase and add nothing, since each block is four identical values.
static void ShiftCopyRGB(uint8_t* dst, const uint8_t* src,
                         int w, int h, int dx, int dy) {
    const size_t rowBytes = (size_t)w * 3;
    memset(dst, 0, rowBytes * (size_t)h);
    // dst(x,y) = src(x-dx, y-dy); clamp to where a source exists.
    const int y0 = dy > 0 ? dy : 0;
    const int y1 = dy > 0 ? h  : h + dy;   // exclusive
    const int x0 = dx > 0 ? dx : 0;
    const int x1 = dx > 0 ? w  : w + dx;
    if (y1 <= y0 || x1 <= x0) return;
    const size_t span = (size_t)(x1 - x0) * 3;
    for (int y = y0; y < y1; y++) {
        const uint8_t* s = src + ((size_t)(y - dy) * w + (size_t)(x0 - dx)) * 3;
        uint8_t*       d = dst + ((size_t)y * w + (size_t)x0) * 3;
        memcpy(d, s, span);
    }
}

void PreviewRenderer::RenderFrame(const raduk::core::FrameSlot* frame,
                                   int previewX, int previewWidth, int windowHeight,
                                   int statusBarHeight,
                                   double fps, uint64_t frameCount, int droppedFrames) {
    if (!m_renderTarget) return;

    // v1.1.1: hold the newest frame between camera frames. Each frame is
    // delivered exactly once (SDK semantics), so whenever the camera rate
    // drops below the ~60Hz render loop most render ticks arrive with
    // frame == null — and the preview flashed NO SIGNAL between frames
    // (visible at any exposure > ~16ms; the old always-a-frame emulator
    // masked it). Redraw the held copy instead; NO SIGNAL only appears
    // after HOLD_TIMEOUT_MS with no frames at all (real signal loss).
    raduk::core::FrameSlot holdShim;
    // Phase 74: whether THIS tick carries a genuinely new camera frame.
    // Everything below also runs on held redraws, so anything that
    // accumulates has to ask first.
    bool isNewFrame = false;
    if (frame && frame->data && frame->width > 0 && frame->height > 0) {
        isNewFrame = true;
        size_t bytes = (size_t)frame->width * frame->height
                     * (frame->bitDepth == 16 ? 2 : 1);
        m_holdBuf.assign(frame->data, frame->data + bytes);
        m_holdW     = (int)frame->width;
        m_holdH     = (int)frame->height;
        m_holdDepth = frame->bitDepth;
        m_holdTime  = std::chrono::steady_clock::now();
    } else if (!m_holdBuf.empty()) {
        auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_holdTime).count();
        if (ageMs < HOLD_TIMEOUT_MS) {
            holdShim.data     = m_holdBuf.data();
            holdShim.dataSize = m_holdBuf.size();
            holdShim.width    = (uint32_t)m_holdW;
            holdShim.height   = (uint32_t)m_holdH;
            holdShim.bitDepth = (uint8_t)m_holdDepth;
            frame = &holdShim;
        }
    }

    int previewHeight = windowHeight - statusBarHeight;
    float px = (float)previewX;
    float pw = (float)previewWidth;
    float ph = (float)previewHeight;

    // Phase 26 follow-up: the brushes hold tinted colours taken when
    // they were created, so a palette change has to rebuild them. This
    // is why switching night vision on mid-session used to leave the
    // overlays in day colours until a restart; StatusBar already did
    // this and the preview did not.
    if (m_brushPaletteGen != Theme::PaletteGeneration()) {
        ReleaseBrushes();
        CreateBrushes();
    }
    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(Theme::Color(Theme::BG_VOID));

    // No PushAxisAlignedClip needed — D2D is attached to the preview child HWND
    // which is automatically hardware-clipped to its own bounds.

    bool hasFrame = (frame && frame->data && m_rgbBuffer
                     && frame->width > 0 && frame->height > 0);

    float topMargin = 36.0f, bottomMargin = 30.0f;
    float availW = pw, availH = ph - topMargin - bottomMargin;
    float reticleCX = px + availW / 2.0f;
    float reticleCY = topMargin + availH / 2.0f;
    float reticleW  = availW, reticleH = availH;

    // Phases 27 & 28: calibrate into a scratch buffer, once, before
    // either debayer path reads the frame. frame->data is deliberately
    // NOT modified -- the recording and the sharpness metric read the
    // same pointer and must keep seeing raw sensor data.
    const uint8_t* rawSrc = frame ? frame->data : nullptr;
    m_calApplied = false;
    if (hasFrame && m_calEnabled && m_cal) {
        const size_t bytes = (size_t)frame->width * frame->height
                           * (frame->bitDepth == 16 ? 2 : 1);
        if (m_calBuf.size() != bytes) m_calBuf.resize(bytes);
        if (m_cal->Apply(frame->data, m_calBuf.data(),
                         (int)frame->width, (int)frame->height,
                         frame->bitDepth)) {
            rawSrc       = m_calBuf.data();
            m_calApplied = true;
        }
    }

    if (hasFrame) {
        D2D1_RECT_F destRect = CalculateZoomedRect(
            frame->width, frame->height, previewX, previewWidth, previewHeight);

        // ── Draw sensor image ──────────────────────────────────
        if (m_debayerMode == DebayerMode::Color) {
            DebayerRGGB(rawSrc, m_rgbBuffer,
                        frame->width, frame->height, frame->bitDepth);

            // ── Phase 25: Display FX ──────────────────────────
            const uint8_t* fxSrc = m_rgbBuffer; // default: unmodified
            size_t n = (size_t)frame->width * frame->height;

            if (m_displayFX == DisplayFX::Boost2x ||
                m_displayFX == DisplayFX::Boost4x) {
                int mult = (m_displayFX == DisplayFX::Boost2x) ? 2 : 4;
                // Apply in-place to a temp copy so m_rgbBuffer stays raw for snapshot
                static thread_local std::vector<uint8_t> boostBuf;
                boostBuf.resize(n * 3);
                for (size_t i = 0; i < n * 3; i++) {
                    int v = (int)m_rgbBuffer[i] * mult;
                    boostBuf[i] = (uint8_t)(v > 255 ? 255 : v);
                }
                fxSrc = boostBuf.data();
            } else if ((m_displayFX == DisplayFX::Stack3  ||
                        m_displayFX == DisplayFX::Stack5  ||
                        m_displayFX == DisplayFX::Stack10) &&
                       m_fxAccum && m_fxFrames[0] && m_fxN > 0) {
                // Quality-gated, centroid-aligned stack of the last N
                // ADMITTED frames.
                //
                // Offer() only on a new frame. RenderFrame also runs on
                // held redraws between camera frames, and the old code
                // re-accumulated the held copy on every tick - so at any
                // exposure longer than a render tick "STACK 10f" was a few
                // distinct frames counted many times over.
                if (isNewFrame) {
                    auto v = m_ql.Offer(m_rgbBuffer,
                                        (int)frame->width, (int)frame->height);
                    m_qlLive   = v.score;
                    m_qlReason = v.reason;
                    if (v.admit) {
                        int oldest = m_fxHead % m_fxN;
                        uint8_t* oldSlot = m_fxFrames[oldest];
                        if (m_fxFilled >= m_fxN) {
                            for (size_t i = 0; i < n * 3; i++)
                                m_fxAccum[i] -= oldSlot[i];
                        }
                        // The slot stores the SHIFTED bytes, which is what
                        // keeps subtract-oldest above consistent with the
                        // accumulator. Storing the unshifted frame and
                        // shifting on the way in would desynchronise them.
                        ShiftCopyRGB(oldSlot, m_rgbBuffer,
                                     (int)frame->width, (int)frame->height,
                                     v.dx, v.dy);
                        for (size_t i = 0; i < n * 3; i++)
                            m_fxAccum[i] += oldSlot[i];
                        m_fxHead = (m_fxHead + 1) % m_fxN;
                        if (m_fxFilled < m_fxN) m_fxFilled++;

                        m_fxStackBuf.resize(n * 3);
                        uint32_t div = (uint32_t)m_fxFilled;
                        for (size_t i = 0; i < n * 3; i++)
                            m_fxStackBuf[i] = (uint8_t)(m_fxAccum[i] / div);
                        // Same metric, same window size, same feature: that
                        // is what makes the stack number comparable with the
                        // live one rather than merely adjacent to it.
                        m_qlStack = raduk::core::QuickLookStacker::ScoreWindow(
                            m_fxStackBuf.data(), (int)frame->width,
                            (int)frame->height, m_ql.RefX(), m_ql.RefY());
                    }
                }
                // Hold the last composed stack on skipped frames and held
                // redraws, so the display does not flicker back to raw.
                if (m_fxFilled > 0 && m_fxStackBuf.size() == n * 3)
                    fxSrc = m_fxStackBuf.data();
            }
            // ── Phase 29: Ghost Frame Overlay ─────────────────
            // Blend stored ghost at 50% over fxSrc into a temp buffer.
            // Only active if ghost enabled and dimensions match.
            static thread_local std::vector<uint8_t> ghostBuf;
            if (m_ghostEnabled && m_ghostFrame &&
                m_ghostW == frame->width && m_ghostH == frame->height) {
                ghostBuf.resize(n * 3);
                for (size_t i = 0; i < n * 3; i++) {
                    // 50% live + 50% ghost
                    ghostBuf[i] = (uint8_t)(((int)fxSrc[i] + (int)m_ghostFrame[i]) >> 1);
                }
                fxSrc = ghostBuf.data();
            }
            // ── End Ghost ─────────────────────────────────────

            std::vector<uint8_t> rgba(n * 4);

            // Clip flash: alternate red / original every 8 frames
            bool flashOn = m_showClip && ((m_clipFlash / 8) % 2 == 0);
            ++m_clipFlash;

            // Phase 37: the stretch as a 256-entry table, built once per
            // frame instead of two multiplies and a clamp on every one of
            // several million subpixels.
            const bool stretchOn = StretchActive();
            uint8_t lut[256];
            if (stretchOn) {
                const int lo = m_black, hi = m_white;
                const int span = (hi > lo) ? (hi - lo) : 1;   // SetStretch
                for (int v = 0; v < 256; v++) {               // keeps hi>lo
                    int o = (v - lo) * 255 / span;
                    lut[v] = (uint8_t)(o < 0 ? 0 : (o > 255 ? 255 : o));
                }
            }

            for (size_t i = 0; i < n; i++) {
                uint8_t r = fxSrc[i*3+0];
                uint8_t g = fxSrc[i*3+1];
                uint8_t b = fxSrc[i*3+2];
                // Phase 37: display stretch, FIRST. It is a transfer
                // function on the real pixel values, so it has to run
                // before night vision collapses them to luminance and
                // before a single channel is picked out -- otherwise it
                // would be stretching the output of those, not the image.
                if (stretchOn) {
                    r = lut[r]; g = lut[g]; b = lut[b];
                }
                // Phase 26: Night Vision — Rec.709 luminance → red channel only
                if (m_nightVision) {
                    uint8_t lum = (uint8_t)(0.2126f*r + 0.7152f*g + 0.0722f*b);
                    r = lum;
                    g = (uint8_t)(lum * 0.10f);
                    b = 0;
                }
                // Phase 38: Single-Channel Display Mode
                if (m_channelMode != ChannelMode::All) {
                    uint8_t ch = (m_channelMode == ChannelMode::R) ? r
                               : (m_channelMode == ChannelMode::G) ? g : b;
                    r = ch; g = ch; b = ch;
                }
                if (flashOn && (r == 255 || g == 255 || b == 255)) {
                    rgba[i*4+0] = 220; rgba[i*4+1] = 10; rgba[i*4+2] = 10;
                } else {
                    rgba[i*4+0] = r; rgba[i*4+1] = g; rgba[i*4+2] = b;
                }
                rgba[i*4+3] = 255;
            }

            // ── RGB Align (Phase 23) ──────────────────────────
            // Compute pixel shift from ADC dispersion + manual offset.
            // Plate scale: 0.203 arcsec/px for 8SE+ASI678MC at f/10.
            // Blue displaced toward zenith (UP) by shift pixels.
            // Red displaced DOWN by shift/2 pixels (smaller correction).
            if (m_rgbAlignEnabled && frame->width > 0 && frame->height > 0) {
                float dispArcsec = 0.0f;
                if (m_mountAltDeg > 0.0) {
                    auto adc = raduk::core::ADCAssistant::Compute(m_mountAltDeg);
                    dispArcsec = adc.separationArcsec;
                }
                const float PLATE_SCALE =
                    raduk::core::RigConfig::Get().plateScaleArcsecPx; // arcsec/px
                int autoShift = (dispArcsec > 0.0f)
                    ? (int)roundf(dispArcsec / PLATE_SCALE) : 0;
                int shift = autoShift + m_rgbAlignManual;
                shift = (shift > 40) ? 40 : (shift < -40) ? -40 : shift;
                m_rgbAlignShift = shift;

                if (shift != 0) {
                    int W = frame->width, H = frame->height;
                    // Shift blue UP by |shift| (positive shift = dispersion correction)
                    int bShift = shift;    // blue up
                    int rShift = -shift/2; // red down by half (partial correction)

                    std::vector<uint8_t> aligned(rgba);
                    for (int y = 0; y < H; y++) {
                        for (int x = 0; x < W; x++) {
                            size_t dst = (size_t)(y * W + x) * 4;
                            // Blue channel from (y + bShift) row
                            int srcY_b = y + bShift;
                            if (srcY_b >= 0 && srcY_b < H) {
                                size_t src = (size_t)(srcY_b * W + x) * 4;
                                aligned[dst+2] = rgba[src+2];
                            } else {
                                aligned[dst+2] = 0;
                            }
                            // Red channel from (y + rShift) row
                            int srcY_r = y + rShift;
                            if (srcY_r >= 0 && srcY_r < H && rShift != 0) {
                                size_t src = (size_t)(srcY_r * W + x) * 4;
                                aligned[dst+0] = rgba[src+0];
                            }
                        }
                    }
                    rgba = std::move(aligned);
                }
            } else {
                m_rgbAlignShift = 0;
            }

            // Phase 89: the share view gets the same finished bytes,
            // before they go anywhere else. It skips most frames itself.
            if (m_presentSink)
                m_presentSink(rgba.data(), (int)frame->width, (int)frame->height);
            if (m_bitmap) {
                D2D1_RECT_U srcRect = D2D1::RectU(0, 0, frame->width, frame->height);
                m_bitmap->CopyFromMemory(&srcRect, rgba.data(), frame->width * 4);
                m_renderTarget->DrawBitmap(m_bitmap, destRect, 1.0f,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
        } else { // DebayerMode::Raw — grayscale Bayer
            size_t n = (size_t)frame->width * frame->height;
            std::vector<uint8_t> rgba(n * 4);
            const uint8_t* src = rawSrc;
            bool flashOn = m_showClip && ((m_clipFlash / 8) % 2 == 0);
            // ASI RAW16 is left-aligned in the 16-bit word (full 0-65535
            // range), so reducing to 8 bits needs >> 8. The previous >> 4
            // left 0-4095, which the uint8_t cast wrapped (4096 -> 0), so
            // every pixel brighter than 4095 came out dark. FrameAnalyzer
            // fixed this same bug; the renderer never got the fix.
            for (size_t i = 0; i < n; i++) {
                uint8_t v = (frame->bitDepth == 16)
                    ? (uint8_t)(reinterpret_cast<const uint16_t*>(src)[i] >> 8)
                    : src[i];
                if (flashOn && v == 255) {
                    rgba[i*4+0] = 220; rgba[i*4+1] = 10; rgba[i*4+2] = 10;
                } else {
                    rgba[i*4+0] = rgba[i*4+1] = rgba[i*4+2] = v;
                }
                rgba[i*4+3] = 255;
            }
            // Phase 89: the share view gets the same finished bytes,
            // before they go anywhere else. It skips most frames itself.
            if (m_presentSink)
                m_presentSink(rgba.data(), (int)frame->width, (int)frame->height);
            if (m_bitmap) {
                D2D1_RECT_U srcRect = D2D1::RectU(0, 0, frame->width, frame->height);
                m_bitmap->CopyFromMemory(&srcRect, rgba.data(), frame->width * 4);
                m_renderTarget->DrawBitmap(m_bitmap, destRect, 1.0f,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
        }

        // ── Frame control bar (outside clip — drawn after PopClip below) ──
        // Store destRect for bar drawing and hit testing
        m_lastDestRect = destRect;
        m_hasLastRect  = true;
        // Also store full preview extents so DrawFrameBar spans the whole
        // preview region, not just the image rect width.
        m_lastPreviewX = px;
        m_lastPreviewW = pw;
        m_lastPreviewH = ph;
        // Reticle centre from frame rect
        reticleCX = (destRect.left + destRect.right)  / 2.0f;
        reticleCY = (destRect.top  + destRect.bottom) / 2.0f;
        reticleW  = destRect.right  - destRect.left;
        reticleH  = destRect.bottom - destRect.top;
    } else {
        m_hasLastRect = false;
        m_lastPreviewX = px;
        m_lastPreviewW = pw;
        m_lastPreviewH = ph;
        if (m_titleFont && m_dimBrush) {
            D2D1_RECT_F r = D2D1::RectF(px, ph/2.0f-20, px+pw, ph/2.0f+20);
            m_titleFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_renderTarget->DrawText(L"NO SIGNAL", 9, m_titleFont, r, m_dimBrush);
        }
    }

    // Reticle
    if (m_showReticle && m_reticleBrush)
        DrawReticle(reticleCX, reticleCY, reticleW, reticleH);

    // ADC overlay — dispersion arrow + severity label.
    //
    // Also gated on there BEING a corrector in the train. The arrow
    // says how far a corrector would have to work; with no corrector
    // fitted it is a readout about a part you do not own, and the
    // owner asked for all of it to stay dark until one is.
    if (m_showOverlays && m_showADC && m_mountAltDeg > 0.0 &&
        raduk::core::RigConfig::Get().adcFitted)
        DrawADCOverlay(pw, ph);

    // Quality bar graph — per-frame sharpness history (Phase 25a)
    if (m_showOverlays && m_showQualityGraph && m_qualCount > 0)
        DrawQualityGraph(px, pw, ph);

    // HUD — caption, zoom hint, mode labels, pixel readout and the
    // LIVE status line, all inside this one block.
    //
    // Gated as a unit by U. Everything drawn over the image goes except
    // the reticle, which is the one overlay you are using the image
    // THROUGH rather than reading off it -- hiding it while centring a
    // target would defeat the point of being able to clear the view.
    if (m_showOverlays && m_hudFont && m_titleFont) {
        wchar_t buf[256];
        const wchar_t* debayerTag =
            m_debayerMode == DebayerMode::Color ? L"COLOR" : L"RAW";

        // Version in the always-visible banner (I control this color, unlike
        // the OS title bar) so a stale build is obvious at a glance.
        swprintf(buf, 256, L"RADUKCAPTURE v%s  ASI678MC  %.1f FPS  [%s]",
                 RADUK_VERSION_W, fps, debayerTag);
        D2D1_RECT_F titleRect = D2D1::RectF(px+8, 6, px+pw-200, 26);
        m_titleFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_renderTarget->DrawText(buf, (UINT32)wcslen(buf),
                                  m_titleFont, titleRect, m_accentBrush);

        swprintf(buf, 32, L"ZOOM: %s", ZoomLabel());
        D2D1_RECT_F zoomRect = D2D1::RectF(px+pw-140, 6, px+pw-8, 24);
        m_hudFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        m_renderTarget->DrawText(buf, (UINT32)wcslen(buf), m_hudFont, zoomRect,
            m_zoom == ZoomMode::Fit ? m_dimBrush : m_accentBrush);

        D2D1_RECT_F hintRect = D2D1::RectF(px+pw-140, 26, px+pw-8, 40);
        m_hudFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        m_renderTarget->DrawText(L"scroll = zoom", 13, m_hudFont, hintRect, m_dimBrush);

        if (m_showReticle) {
            D2D1_RECT_F rRect = D2D1::RectF(px+pw-140, 42, px+pw-8, 56);
            m_hudFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            m_renderTarget->DrawText(L"[+] RETICLE ON", 14,
                                      m_hudFont, rRect, m_reticleBrush);
        }

        // Clipping warning — shown whenever clip > 0, flash state indicates severity
        if (m_showClip && m_clipPct > 0.05f && m_redBrush) {
            wchar_t clipBuf[32];
            swprintf(clipBuf, 32, L"CLIP %.1f%%", m_clipPct);
            bool labelFlash = (m_clipFlash / 8) % 2 == 0;
            ID2D1SolidColorBrush* clipBrush = labelFlash ? m_redBrush : m_amberBrush;
            D2D1_RECT_F clipRect = D2D1::RectF(px+pw-140, 58, px+pw-8, 72);
            m_hudFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            m_renderTarget->DrawText(clipBuf, (UINT32)wcslen(clipBuf),
                                     m_hudFont, clipRect, clipBrush);
        }

        // RGB Align HUD — shown when enabled
        if (m_rgbAlignEnabled && m_amberBrush) {
            wchar_t rgbBuf[32];
            swprintf(rgbBuf, 32, L"RGB ALIGN  %+d px", m_rgbAlignShift);
            D2D1_RECT_F rgbRect = D2D1::RectF(px+pw-160, 74, px+pw-8, 88);
            m_hudFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            m_renderTarget->DrawText(rgbBuf, (UINT32)wcslen(rgbBuf),
                                     m_hudFont, rgbRect, m_amberBrush);
        }

        // FX mode HUD — shown when not Normal
        if (m_displayFX != DisplayFX::Normal && m_amberBrush) {
            const wchar_t* fxLbl = DisplayFXLabel();
            D2D1_RECT_F fxRect = D2D1::RectF(px+pw-120, 90, px+pw-8, 104);
            m_hudFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            m_renderTarget->DrawText(fxLbl, (UINT32)wcslen(fxLbl),
                                     m_hudFont, fxRect, m_amberBrush);
        }

        // Phase 74: quick-look stacker readout. Two numbers from the
        // SAME metric over the same window, so they can be compared:
        // the live frame and the aligned stack of the best recent ones.
        // Gated on the stacker having actually been FED, not merely on a
        // stack mode being selected. The stacker only runs in the Color
        // debayer branch, so in RAW mode this used to sit there reading
        // "LIVE 0.00 learning..." next to an FX label saying
        // "STACK 10 BEST" while nothing was being stacked at all.
        if (m_fxN > 0 && m_amberBrush &&
            m_debayerMode == DebayerMode::Color) {
            wchar_t qb[96];
            if (m_ql.Learning()) {
                swprintf(qb, 96, L"LIVE %.2f  learning...", m_qlLive);
            } else {
                swprintf(qb, 96, L"LIVE %.2f  STACK %.2f  (%d)",
                         m_qlLive, m_qlStack, m_fxFilled);
            }
            D2D1_RECT_F qRect = D2D1::RectF(px+pw-220, 154, px+pw-8, 168);
            m_hudFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            m_renderTarget->DrawText(qb, (UINT32)wcslen(qb),
                                     m_hudFont, qRect, m_amberBrush);
        }

        // Phase 26: Night Vision HUD label
        if (m_nightVision && m_redBrush) {
            D2D1_RECT_F nvRect = D2D1::RectF(px+pw-120, 106, px+pw-8, 120);
            m_hudFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            m_renderTarget->DrawText(L"NIGHT VISION", 12, m_hudFont, nvRect, m_redBrush);
        }

        // Phase 38: Single-Channel HUD label
        if (m_channelMode != ChannelMode::All && m_accentBrush) {
            const wchar_t* chLbl = ChannelLabel();
            D2D1_RECT_F chRect = D2D1::RectF(px+pw-120, 122, px+pw-8, 136);
            m_hudFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            m_renderTarget->DrawText(chLbl, (UINT32)wcslen(chLbl), m_hudFont, chRect, m_accentBrush);
        }

        // Phase 29: Ghost Frame HUD label
        if (m_ghostEnabled && m_amberBrush) {
            D2D1_RECT_F ghRect = D2D1::RectF(px+pw-120, 138, px+pw-8, 152);
            m_hudFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            m_renderTarget->DrawText(L"GHOST", 5, m_hudFont, ghRect, m_amberBrush);
        }

        // Pixel Value Readout (Phase 24)
        if (m_pixelReadout && m_rgbBuffer && m_frameWidth > 0 && m_hasLastRect) {
            DrawPixelReadout(px, pw, ph);
        }

        if (hasFrame && frame) {
            swprintf(buf, 256, L"LIVE  %ux%u  %s  %.1f FPS  FRAMES: %llu  DROPS: %d",
                frame->width, frame->height,
                (frame->bitDepth == 16) ? L"RAW16" : L"RAW8",
                fps, frameCount, droppedFrames);
        } else {
            swprintf(buf, 64, L"WAITING FOR FRAMES...");
        }
        float statusY = ph - 24.0f;
        // Store rect for hit testing — user can click it to get reset popup
        m_hudStatusRect = D2D1::RectF(px+8, statusY, px+300, statusY+18);
        D2D1_RECT_F statusRect = D2D1::RectF(px+8, statusY, px+pw-8, statusY+18);
        m_hudFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        // Highlight the clickable portion (resolution text) slightly brighter
        ID2D1SolidColorBrush* statusBrush = hasFrame ? m_accentBrush : m_dimBrush;
        m_renderTarget->DrawText(buf, (UINT32)wcslen(buf), m_hudFont, statusRect,
            statusBrush);
        // Subtle underline on the resolution hit area to hint it's clickable
        if (hasFrame && m_dimBrush) {
            m_renderTarget->DrawLine(
                D2D1::Point2F(px+8, statusY+17),
                D2D1::Point2F(px+8 + (float)wcslen(buf)*7.2f, statusY+17),
                m_dimBrush, 0.5f);
        }
    }

    // Rubber-band ROI selection overlay
    DrawRubberBand();

    // Snapshot notification (Phase 22) — "SAVED: filename" for ~3s
    if (m_snapTimer > 0 && m_hudFont && m_greenBrush) {
        --m_snapTimer;
        D2D1_RECT_F nr = D2D1::RectF(px + pw/2.0f - 200, ph/2.0f - 14,
                                      px + pw/2.0f + 200, ph/2.0f + 14);
        m_hudFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_renderTarget->DrawText(m_snapMsg.c_str(),
                                 (UINT32)m_snapMsg.size(),
                                 m_hudFont, nr, m_greenBrush);
    }

    // Reset popup
    if (m_showResetPopup) DrawResetPopup();

    // Frame bar buttons removed — now Win32 native buttons in ToolbarStrip above preview.
}

// ── DrawResetPopup ─────────────────────────────────────────────
// Small popup anchored above the HUD status line.
// Appears when user clicks the resolution readout.
// Contains a single "RESET TO 3840×2160" button.

void PreviewRenderer::DrawResetPopup() {
    if (!m_renderTarget || !m_barFont || !m_barBgBrush || !m_hudFont) return;

    // Anchor popup just above the HUD status rect
    const float popW   = 180.0f;
    const float popH   = 48.0f;
    const float popX   = m_hudStatusRect.left;
    const float popY   = m_hudStatusRect.top - popH - 4.0f;

    D2D1_RECT_F popRect = D2D1::RectF(popX, popY, popX + popW, popY + popH);

    // Background
    ID2D1SolidColorBrush* bgBrush = nullptr;
    m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.04f, 0.09f, 0.15f, 0.96f), &bgBrush);
    if (bgBrush) {
        m_renderTarget->FillRectangle(popRect, bgBrush);
        bgBrush->Release();
    }

    // Border
    if (m_accentBrush)
        m_renderTarget->DrawRectangle(popRect, m_accentBrush, 1.0f);

    // Title label
    if (m_barFont && m_dimBrush) {
        D2D1_RECT_F titleR = D2D1::RectF(popX+6, popY+4, popX+popW-6, popY+18);
        m_barFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_renderTarget->DrawText(L"ROI PRESETS", 11, m_barFont, titleR, m_dimBrush);
    }

    // Reset button
    const float btnX = popX + 6.0f;
    const float btnY = popY + 20.0f;
    const float btnW = popW - 12.0f;
    const float btnH = 22.0f;
    m_btnResetMax = D2D1::RectF(btnX, btnY, btnX + btnW, btnY + btnH);

    // Button highlight background
    if (m_barHiBrush)
        m_renderTarget->FillRectangle(m_btnResetMax, m_barHiBrush);

    // Button border
    if (m_accentBrush)
        m_renderTarget->DrawRectangle(m_btnResetMax, m_accentBrush, 1.0f);

    // Button label
    if (m_barFont && m_accentBrush) {
        m_barFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_renderTarget->DrawText(
            L"RESET  3840 \u00d7 2160", 19,
            m_barFont, m_btnResetMax, m_accentBrush);
    }
}

// ── Side panels ───────────────────────────────────────────────────
// Histogram left, temperature right, both vertically centred on the
// dark sky either side of the target.
//
// Centred rather than cornered because every corner is already taken:
// the caption and zoom hint sit top-left and top-right, the quality
// graph and the ADC readout bottom-left and bottom-right. The middle
// of each edge is the only part of the frame nothing else claims, and
// on a planetary target it is empty sky.

void PreviewRenderer::FillPanelBg(float x, float y, float w, float h) {
    if (!m_panelBgBrush) return;
    m_renderTarget->FillRectangle(D2D1::RectF(x, y, x + w, y + h), m_panelBgBrush);
    if (m_dimBrush)
        m_renderTarget->DrawRectangle(D2D1::RectF(x, y, x + w, y + h), m_dimBrush, 0.5f);
}

void PreviewRenderer::DrawSidePanels(const raduk::core::AnalysisResult& analysis,
                                     bool analysisValid,
                                     int previewX, int previewWidth,
                                     int windowHeight, int statusBarHeight) {
    if (!m_renderTarget || !m_hudFont) return;
    if (!m_showOverlays) return;

    const float px = (float)previewX;
    const float pw = (float)previewWidth;
    const float ph = (float)(windowHeight - statusBarHeight);

    const float PANEL_W = 268.0f;
    const float HIST_H  = 104.0f;
    const float TEMP_H  = 116.0f;
    const float MARGIN  = 12.0f;
    const float BOTTOM_PAD = 108.0f;  // clearance above the sharpness graph / ADC box
    const float TOP_LIMIT  = 100.0f;  // never ride up into the caption

    // Give up rather than overprint the image when there is no room to
    // either side -- at a small window or a heavy zoom the two panels
    // would meet in the middle and cover the target itself.
    if (pw < PANEL_W * 2.0f + MARGIN * 4.0f + 160.0f) return;
    if (ph < TEMP_H + BOTTOM_PAD + TOP_LIMIT) return;

    // Both panels sit low and share a bottom edge, rather than being
    // vertically centred as they were through v1.2.5.
    //
    // Centred put them level with the target. A planet is centred too
    // -- that is what the reticle is for -- so at any real zoom the
    // panels were the two things closest to the thing being judged, one
    // either side of it. Low and bottom-aligned keeps the whole middle
    // band of the image clear, and reads as one row of instruments
    // instead of two floating boxes.
    //
    // BOTTOM_PAD clears what already lives along the bottom: the
    // sharpness graph starts at ph-80 on the left and the ADC box at
    // ph-100 on the right, so ph-108 is below neither.
    const float bottom = ph - BOTTOM_PAD;
    float histY = bottom - HIST_H;
    float tempY = bottom - TEMP_H;

    // At an unusually short window, lowering them would push the panels
    // up into the caption instead. Stop at TOP_LIMIT and let them sit
    // higher rather than overprint it.
    if (histY < TOP_LIMIT) histY = TOP_LIMIT;
    if (tempY < TOP_LIMIT) tempY = TOP_LIMIT;

    DrawHistogramPanel(analysis, analysisValid,
                       px + MARGIN, histY, PANEL_W, HIST_H);
    DrawTempPanel(px + pw - PANEL_W - MARGIN, tempY, PANEL_W, TEMP_H);
}

void PreviewRenderer::DrawHistogramPanel(const raduk::core::AnalysisResult& a,
                                         bool valid,
                                         float x, float y, float w, float h) {
    FillPanelBg(x, y, w, h);

    // The drawing itself lives in HistogramDraw.h, shared with the
    // stand-alone histogram window. It used to be inline here, and the
    // parts worth not duplicating are the parts that took the longest to
    // get right: the log scale, the skipped sub-pixel bins, the clip
    // colouring. ROWH 24 reproduces exactly what this overlay drew
    // before the extraction.
    // The overlay's original numbers, passed explicitly so the
      // extraction cannot change how it looks.
    const float ROWH = 24.0f, LABW = 12.0f, PCTW = 44.0f;

    histogram::Brushes br;
    br.r     = m_histRBrush;
    br.g     = m_histGBrush;
    br.b     = m_histBBrush;
    br.bed   = m_panelBgBrush;
    br.mean  = m_textBrush;
    br.dim   = m_dimBrush;
    br.amber = m_amberBrush;
    br.red   = m_redBrush;
    br.green = m_greenBrush;

    histogram::Draw(m_renderTarget, m_hudFont, br, a, valid,
                    x, y, w, h, ROWH, LABW, PCTW);
}

void PreviewRenderer::DrawTempPanel(float x, float y, float w, float h) {
    FillPanelBg(x, y, w, h);

    // Drawing shared with the stand-alone temperature window
    // (TemperatureDraw.h). The 2 degree minimum span lives there, and
    // has to behave identically in both or the two displays disagree
    // about whether the temperature is doing anything.
    const float PAD    = 6.0f;
    const float LABELH = 18.0f;

    temperature::Brushes br;
    br.cam = m_greenBrush;
    br.foc = m_amberBrush;
    br.dim = m_dimBrush;

    // The overlay's own 120-sample history, converted for the shared
    // painter. Small enough that copying it per frame costs nothing.
    temperature::Sample flat[TEMP_HISTORY];
    const int n = (int)m_tempHistory.size();
    for (int i = 0; i < n; i++)
        flat[i] = { m_tempHistory[i].camC, m_tempHistory[i].focC };

    temperature::Draw(m_renderTarget, m_hudFont, br, flat, n, TEMP_HISTORY,
                      m_tempCamC, m_tempFocC,
                      x + PAD, y + PAD, w - PAD * 2.0f, h - PAD * 2.0f,
                      LABELH, 1.0f);
}

void PreviewRenderer::DrawPixelReadout(float px, float pw, float ph) {
    if (!m_renderTarget || !m_hudFont) return;

    float sx = m_pixelHoverX;
    float sy = m_pixelHoverY;

    // Map screen pos → frame pixel (accounting for zoom/letterbox)
    float dW = m_lastDestRect.right  - m_lastDestRect.left;
    float dH = m_lastDestRect.bottom - m_lastDestRect.top;
    if (dW <= 0 || dH <= 0) return;

    float normX = (sx - m_lastDestRect.left) / dW;
    float normY = (sy - m_lastDestRect.top)  / dH;
    if (normX < 0 || normX > 1 || normY < 0 || normY > 1) return;

    int fx = (int)(normX * m_frameWidth);
    int fy = (int)(normY * m_frameHeight);
    fx = fx < 0 ? 0 : (fx >= m_frameWidth  ? m_frameWidth  - 1 : fx);
    fy = fy < 0 ? 0 : (fy >= m_frameHeight ? m_frameHeight - 1 : fy);

    size_t idx = (size_t)fy * m_frameWidth + fx;
    uint8_t r = m_rgbBuffer[idx*3+0];
    uint8_t g = m_rgbBuffer[idx*3+1];
    uint8_t b = m_rgbBuffer[idx*3+2];

    // ── Small crosshair at hover position ────────────────────
    const float CH = 8.0f;
    ID2D1SolidColorBrush* xBrush = m_pixelLocked ? m_greenBrush : m_dimBrush;
    if (xBrush) {
        m_renderTarget->DrawLine(D2D1::Point2F(sx - CH, sy), D2D1::Point2F(sx + CH, sy),
                                 xBrush, 1.0f);
        m_renderTarget->DrawLine(D2D1::Point2F(sx, sy - CH), D2D1::Point2F(sx, sy + CH),
                                 xBrush, 1.0f);
    }

    // ── Tooltip box ───────────────────────────────────────────
    wchar_t line1[48], line2[32];
    swprintf(line1, 48, L"R:%3d  G:%3d  B:%3d", r, g, b);
    swprintf(line2, 32, L"[%d, %d]", fx, fy);

    const float TW = 130.0f, TH = 36.0f, TPAD = 4.0f;
    // Position tooltip: prefer right of cursor, flip left near edge
    float tx = sx + 10.0f;
    float ty = sy - TH - 4.0f;
    if (tx + TW > px + pw - 8) tx = sx - TW - 10.0f;
    if (ty < 4.0f) ty = sy + 10.0f;

    // Background
    {
        ID2D1SolidColorBrush* bgBr = nullptr;
        m_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.02f, 0.06f, 0.12f, 0.88f), &bgBr);
        if (bgBr) {
            m_renderTarget->FillRectangle(
                D2D1::RectF(tx, ty, tx+TW, ty+TH), bgBr);
            bgBr->Release();
        }
    }
    // Border
    if (xBrush)
        m_renderTarget->DrawRectangle(
            D2D1::RectF(tx, ty, tx+TW, ty+TH), xBrush, 0.5f);

    // Text
    m_hudFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    ID2D1SolidColorBrush* txtBrush = m_pixelLocked ? m_greenBrush : m_amberBrush;
    if (!txtBrush) txtBrush = m_dimBrush;
    if (txtBrush) {
        m_renderTarget->DrawText(line1, (UINT32)wcslen(line1), m_hudFont,
            D2D1::RectF(tx+TPAD, ty+2, tx+TW-TPAD, ty+TH/2+2), txtBrush);
        m_renderTarget->DrawText(line2, (UINT32)wcslen(line2), m_hudFont,
            D2D1::RectF(tx+TPAD, ty+TH/2+2, tx+TW-TPAD, ty+TH), m_dimBrush ? m_dimBrush : txtBrush);
    }

    // Lock indicator
    if (m_pixelLocked && m_greenBrush) {
        D2D1_RECT_F lr = D2D1::RectF(px+pw-80, 90, px+pw-8, 104);
        m_hudFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        m_renderTarget->DrawText(L"PIXEL LOCK", 10, m_hudFont, lr, m_greenBrush);
    }
}

// ── DrawQualityGraph ──────────────────────────────────────────
// Renders a rolling per-frame sharpness bar graph in the bottom-
// left of the preview, above the HUD status line.
//
// Layout: fixed-width strip, bars fill left-to-right oldest→newest.
// Bar height proportional to sharpnessPct (0-100).
// Color: green (sharp) → amber (ok) → red (soft) by value.
// "SHARP" label + peak value shown to the right of the bars.

void PreviewRenderer::DrawQualityGraph(float px, float pw, float ph) {
    if (!m_renderTarget) return;

    const float GRAPH_W   = 220.0f;  // total graph width
    const float GRAPH_H   = 48.0f;   // bar chart height
    const float MARGIN    = 10.0f;   // left margin from preview edge
    const float LABEL_W   = 60.0f;   // label area to the right of bars
    const float BAR_W     = 1.5f;
    const float GAP       = 0.5f;

    // Bottom-left, above the HUD status line.
    const float gx = px + MARGIN;
    const float gy = ph - 24.0f - GRAPH_H - 8.0f;

    // Semi-transparent backing.
    {
        ID2D1SolidColorBrush* bgBrush = nullptr;
        m_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.45f), &bgBrush);
        if (bgBrush) {
            m_renderTarget->FillRectangle(
                D2D1::RectF(gx - 4, gy - 4,
                            gx + GRAPH_W + LABEL_W + 8, gy + GRAPH_H + 4),
                bgBrush);
            bgBrush->Release();
        }
    }

    // The ring buffer is flattened oldest-first for the shared painter,
    // which takes a plain span so the two callers can keep different
    // amounts of history.
    float flat[QUAL_HISTORY];
    const int n = m_qualCount;
    for (int i = 0; i < n; i++)
        flat[i] = m_qualHistory[(m_qualHead - n + i + QUAL_HISTORY) % QUAL_HISTORY];

    sharpness::Brushes br;
    br.green = m_greenBrush;
    br.amber = m_amberBrush;
    br.red   = m_redBrush;
    br.dim   = m_dimBrush;

    // No gridlines: across a 48px strip they would be noise.
    sharpness::Draw(m_renderTarget, m_hudFont, br, flat, n,
                    gx, gy, GRAPH_W + LABEL_W, GRAPH_H,
                    LABEL_W, BAR_W, GAP, /*gridlines*/ false);
}

void PreviewRenderer::SetDisplayFX(DisplayFX fx) {
    if (fx == m_displayFX) return;

    // Free existing stack buffers if switching away from stack mode
    auto isStack = [](DisplayFX f) {
        return f == DisplayFX::Stack3 || f == DisplayFX::Stack5 || f == DisplayFX::Stack10;
    };

    if (isStack(m_displayFX) && !isStack(fx)) {
        // Leaving stack mode — free all
        for (int i = 0; i < FX_STACK_MAX; i++) {
            delete[] m_fxFrames[i];
            m_fxFrames[i] = nullptr;
        }
        delete[] m_fxAccum;
        m_fxAccum  = nullptr;
        m_fxHead   = 0;
        m_fxFilled = 0;
        m_fxN      = 0;
    }

    m_displayFX = fx;

    // Phase 74: the stacker caches a reference position and a score
    // history; both belong to the mode being left. Cold start refills
    // in under a second, so resetting costs nothing.
    m_ql.Reset();
    m_fxStackBuf.clear();
    m_qlLive = m_qlStack = 0.0f; m_qlReason = "";

    if (isStack(fx) && m_frameWidth > 0 && m_frameHeight > 0) {
        m_fxN = (fx == DisplayFX::Stack3) ? 3 :
                (fx == DisplayFX::Stack5) ? 5 : 10;
        size_t frameBytes = (size_t)m_frameWidth * m_frameHeight * 3;
        size_t accumBytes = (size_t)m_frameWidth * m_frameHeight * 3 * sizeof(uint32_t);
        // Allocate history slots and clear accumulator
        for (int i = 0; i < m_fxN; i++) {
            if (!m_fxFrames[i]) {
                m_fxFrames[i] = new uint8_t[frameBytes]();
            }
        }
        if (!m_fxAccum) {
            m_fxAccum = new uint32_t[m_frameWidth * m_frameHeight * 3]();
        } else {
            // Switching depth (3 -> 5 -> 10) REUSES the accumulator but
            // resets m_fxFilled below, so the old sums would be divided
            // by a small count and the display saturated white until the
            // ring refilled. Zero it with the ring.
            memset(m_fxAccum, 0,
                   (size_t)m_frameWidth * m_frameHeight * 3 * sizeof(uint32_t));
        }
        m_fxHead   = 0;
        m_fxFilled = 0;
    }

    printf("[FX] Mode: %ls\n", DisplayFXLabel());
}

// ── SaveSnapshotPNG ───────────────────────────────────────────
// Encodes the current m_rgbBuffer to PNG via WIC.
// m_rgbBuffer is RGB 24bpp; WIC needs a pixel format — we use
// GUID_WICPixelFormat24bppRGB which maps directly.

bool PreviewRenderer::SaveSnapshotPNG(const std::wstring& path) {
    if (!m_rgbBuffer || m_frameWidth <= 0 || m_frameHeight <= 0) {
        printf("[Snapshot] No frame data available\n");
        return false;
    }

    IWICImagingFactory* factory = nullptr;
    IWICStream*         stream  = nullptr;
    IWICBitmapEncoder*  encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    bool ok = false;

    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { printf("[Snapshot] WIC factory failed: 0x%lx\n", hr); goto cleanup; }

    hr = factory->CreateStream(&stream);
    if (FAILED(hr)) goto cleanup;

    hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) { printf("[Snapshot] Cannot open file for write\n"); goto cleanup; }

    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) goto cleanup;

    hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (FAILED(hr)) goto cleanup;

    hr = encoder->CreateNewFrame(&frame, nullptr);
    if (FAILED(hr)) goto cleanup;

    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) goto cleanup;

    hr = frame->SetSize((UINT)m_frameWidth, (UINT)m_frameHeight);
    if (FAILED(hr)) goto cleanup;

    {
        WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppRGB;
        hr = frame->SetPixelFormat(&fmt);
        if (FAILED(hr)) goto cleanup;

        UINT stride = (UINT)m_frameWidth * 3;
        UINT bufSize = stride * (UINT)m_frameHeight;
        hr = frame->WritePixels((UINT)m_frameHeight, stride, bufSize, m_rgbBuffer);
        if (FAILED(hr)) { printf("[Snapshot] WritePixels failed: 0x%lx\n", hr); goto cleanup; }
    }

    hr = frame->Commit();
    if (FAILED(hr)) goto cleanup;

    hr = encoder->Commit();
    if (FAILED(hr)) goto cleanup;

    ok = true;
    printf("[Snapshot] Saved: %ls  (%dx%d)\n", path.c_str(), m_frameWidth, m_frameHeight);

cleanup:
    if (frame)   frame->Release();
    if (encoder) encoder->Release();
    if (stream)  stream->Release();
    if (factory) factory->Release();
    return ok;
}

// ── DrawReticle ───────────────────────────────────────────────

void PreviewRenderer::DrawReticle(float cx, float cy, float rw, float rh) {
    if (!m_reticleBrush || rw < 10 || rh < 10) return;

    // Phase 45: offset and rotation. The offset is a FRACTION of the
    // frame so it survives a window resize; rotation goes through the
    // render target transform, which turns the shape about its own
    // centre rather than about the frame centre.
    cx += m_reticleDX * rw;
    cy += m_reticleDY * rh;
    D2D1_MATRIX_3X2_F saved = D2D1::Matrix3x2F::Identity();
    const bool rotated = (m_reticleRot != 0.0f) && ReticleRotatable();
    if (rotated) {
        m_renderTarget->GetTransform(&saved);
        m_renderTarget->SetTransform(
            D2D1::Matrix3x2F::Rotation(m_reticleRot, D2D1::Point2F(cx, cy))
            * saved);
    }
    // Put the transform back on every exit path, including the early
    // returns inside the shape cases.
    struct Restore {
        ID2D1RenderTarget* rt; D2D1_MATRIX_3X2_F m; bool on;
        ~Restore() { if (on) rt->SetTransform(m); }
    } restore{ m_renderTarget, saved, rotated };
    const float smaller = (rw < rh) ? rw : rh;
    const float gap  = smaller * 0.018f;
    const float base = smaller * 0.5f * m_reticleScale;
    const float left = cx-rw/2.0f, right = cx+rw/2.0f;
    const float top  = cy-rh/2.0f, bot   = cy+rh/2.0f;

    // Every shape keeps the centre dot. It is the only part that marks
    // the exact pixel, and it is what the planet gets nudged onto.
    auto centreDot = [&] {
        m_renderTarget->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(cx, cy), 2.5f, 2.5f), m_reticleBrush);
    };

    switch (m_reticleShape) {
    case ReticleShape::Circle:
        m_renderTarget->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F(cx, cy), base, base), m_reticleBrush, 1.5f);
        centreDot();
        break;

    case ReticleShape::Rings:
        // Three concentric rings for collimation: a defocused star
        // shows as a donut, and whether that donut sits centred in
        // these is the thing being judged.
        for (int i = 1; i <= 3; i++) {
            const float r = base * (float)i / 3.0f;
            m_renderTarget->DrawEllipse(
                D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), m_reticleBrush, 1.2f);
        }
        centreDot();
        break;

    case ReticleShape::Box: {
        // Corner brackets rather than a full rectangle, so the frame
        // edge does not hide the detail just inside it.
        const float h = base, t = base * 0.28f;
        const D2D1_POINT_2F c[4] = {
            { cx-h, cy-h }, { cx+h, cy-h }, { cx+h, cy+h }, { cx-h, cy+h }
        };
        const float sx[4] = {  1.0f, -1.0f, -1.0f,  1.0f };
        const float sy[4] = {  1.0f,  1.0f, -1.0f, -1.0f };
        for (int i = 0; i < 4; i++) {
            m_renderTarget->DrawLine(c[i],
                D2D1::Point2F(c[i].x + sx[i]*t, c[i].y), m_reticleBrush, 1.5f);
            m_renderTarget->DrawLine(c[i],
                D2D1::Point2F(c[i].x, c[i].y + sy[i]*t), m_reticleBrush, 1.5f);
        }
        centreDot();
        break;
    }

    case ReticleShape::Crosshair:
    default: {
        // The original: full-frame cross with a gap at the centre so
        // the target itself is never covered, plus a small ring.
        const float r = smaller * 0.10f;
        m_renderTarget->DrawLine(D2D1::Point2F(left, cy),   D2D1::Point2F(cx-gap, cy), m_reticleBrush, 1.5f);
        m_renderTarget->DrawLine(D2D1::Point2F(cx+gap, cy), D2D1::Point2F(right, cy),  m_reticleBrush, 1.5f);
        m_renderTarget->DrawLine(D2D1::Point2F(cx, top),    D2D1::Point2F(cx, cy-gap), m_reticleBrush, 1.5f);
        m_renderTarget->DrawLine(D2D1::Point2F(cx, cy+gap), D2D1::Point2F(cx, bot),    m_reticleBrush, 1.5f);
        m_renderTarget->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), m_reticleBrush, 1.5f);
        centreDot();
        break;
    }
    }
}

// ── DrawADCOverlay ─────────────────────────────────────────────
// Draws a dispersion indicator in the bottom-right of the preview.
// Shows: severity color, arrow pointing in dispersion direction
// (always toward zenith = upward in alt-az), and arcsec value.
// Only visible when m_mountAltDeg > 0 and separation is meaningful.

void PreviewRenderer::DrawADCOverlay(float previewW, float previewH) {
    if (!m_renderTarget || !m_hudFont) return;

    using namespace raduk::core;
    ADCResult adc = ADCAssistant::Compute(m_mountAltDeg);

    // Don't clutter the view if dispersion is negligible
    if (adc.separationArcsec < 0.15f) return;

    // Choose color by severity
    ID2D1SolidColorBrush* brush = nullptr;
    if (adc.adcRequired)         brush = m_redBrush;
    else if (adc.adcRecommended) brush = m_amberBrush;
    else                         brush = m_dimBrush;
    if (!brush) return;

    // ── Panel position: bottom-right corner ───────────────────
    const float PAD   = 14.0f;
    const float BOXW  = 114.0f;
    const float BOXH  = 56.0f;
    // previewH is the preview child window height — status bar is separate,
    // so just clear the bottom edge with PAD
    float bx = previewW - BOXW - PAD;
    float by = previewH - BOXH - PAD - 30.0f; // above the HUD status line

    // Clamp to visible area
    if (bx < 4.0f) bx = 4.0f;
    if (by < 4.0f) by = 4.0f;

    // Semi-transparent background box
    ID2D1SolidColorBrush* bgBrush = nullptr;
    m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.55f), &bgBrush);
    if (bgBrush) {
        m_renderTarget->FillRectangle(
            D2D1::RectF(bx, by, bx+BOXW, by+BOXH), bgBrush);
        bgBrush->Release();
    }

    // ── "ADC" label top line ──────────────────────────────────
    {
        wchar_t buf[32];
        swprintf(buf, 32, L"ADC  %.2f\"", adc.separationArcsec);
        D2D1_RECT_F tr = D2D1::RectF(bx+4, by+3, bx+BOXW-4, by+19);
        m_hudFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_renderTarget->DrawText(buf, (UINT32)wcslen(buf), m_hudFont, tr, brush);
    }

    // ── Severity label ────────────────────────────────────────
    {
        wchar_t buf[24];
        swprintf(buf, 24, L"%hs", adc.SeverityLabel());
        D2D1_RECT_F sr = D2D1::RectF(bx+4, by+18, bx+BOXW-4, by+33);
        m_renderTarget->DrawText(buf, (UINT32)wcslen(buf), m_hudFont, sr, brush);
    }

    // ── Dispersion arrow ──────────────────────────────────────
    // Arrow points UP (toward zenith) — blue is displaced upward
    // relative to red. Length scaled to severity (4–20px).
    float arrowLen = 6.0f + adc.separationArcsec * 2.5f;
    if (arrowLen > 22.0f) arrowLen = 22.0f;

    float ax = bx + BOXW - 18.0f;  // arrow x center
    float ay = by + BOXH/2.0f + 4; // arrow y center

    // Shaft (upward)
    m_renderTarget->DrawLine(
        D2D1::Point2F(ax, ay + arrowLen/2),
        D2D1::Point2F(ax, ay - arrowLen/2),
        brush, 1.5f);
    // Arrowhead
    m_renderTarget->DrawLine(
        D2D1::Point2F(ax, ay - arrowLen/2),
        D2D1::Point2F(ax - 4, ay - arrowLen/2 + 5),
        brush, 1.5f);
    m_renderTarget->DrawLine(
        D2D1::Point2F(ax, ay - arrowLen/2),
        D2D1::Point2F(ax + 4, ay - arrowLen/2 + 5),
        brush, 1.5f);

    // "B" label — kept inside the box top area, not above the box
    {
        float lblY = by + 3.0f;  // top of box + small margin
        D2D1_RECT_F br = D2D1::RectF(ax-8, lblY, ax+8, lblY+12);
        m_renderTarget->DrawText(L"B", 1, m_hudFont, br, m_accentBrush);
    }
}

// ── CalculateZoomedRect ───────────────────────────────────────

D2D1_RECT_F PreviewRenderer::CalculateZoomedRect(int frameW, int frameH,
                                                   int previewX, int previewW, int previewH) {
    float topM=36.0f, botM=30.0f;
    float availW=(float)previewW, availH=(float)previewH-topM-botM;
    float cx=(float)previewX+availW/2.0f, cy=topM+availH/2.0f;
    float destW, destH;
    switch (m_zoom) {
        case ZoomMode::One:  destW=(float)frameW;      destH=(float)frameH;      break;
        case ZoomMode::Two:  destW=(float)frameW*2.0f; destH=(float)frameH*2.0f; break;
        case ZoomMode::Four: destW=(float)frameW*4.0f; destH=(float)frameH*4.0f; break;
        default: {
            float fa=(float)frameW/(float)frameH, ra=availW/availH;
            if (fa>ra) { destW=availW; destH=availW/fa; }
            else       { destH=availH; destW=availH*fa; }
            break;
        }
    }
    return D2D1::RectF(cx-destW/2.0f, cy-destH/2.0f, cx+destW/2.0f, cy+destH/2.0f);
}

// ── Release ───────────────────────────────────────────────────

void PreviewRenderer::Release() {
    if (m_bitmap)      { m_bitmap->Release();      m_bitmap      = nullptr; }
    ReleaseBrushes();
    if (m_hudFont)     { m_hudFont->Release();     m_hudFont     = nullptr; }
    if (m_titleFont)   { m_titleFont->Release();   m_titleFont   = nullptr; }
    if (m_barFont)     { m_barFont->Release();     m_barFont     = nullptr; }
    delete[] m_rgbBuffer;
    m_rgbBuffer = nullptr; m_rgbBufferSize = 0;
    // Phase 25: free FX stack buffers
    for (int i = 0; i < FX_STACK_MAX; i++) {
        delete[] m_fxFrames[i];
        m_fxFrames[i] = nullptr;
    }
    delete[] m_fxAccum;
    m_fxAccum = nullptr;
    // Phase 29: free ghost frame buffer
    delete[] m_ghostFrame;
    m_ghostFrame = nullptr; m_ghostFrameSize = 0;
    m_hasLastRect = false;
}

// ── DebayerRGGB (CPU fallback) ────────────────────────────────

void PreviewRenderer::DebayerRGGB(const uint8_t* raw, uint8_t* rgb,
                                   int width, int height, int bitDepth) {
    if (bitDepth == 16) {
        const uint16_t* r16 = reinterpret_cast<const uint16_t*>(raw);
        for (int y=0; y<height-1; y+=2) for (int x=0; x<width-1; x+=2) {
            // >> 8, not >> 4 - see the RAW16 note in RenderFrame. A >> 4
            // here wrapped every value above 4095 to near-black, which
            // also corrupts anything reading m_rgbBuffer downstream.
            uint8_t r8=(uint8_t)(r16[y*width+x]>>8);
            uint8_t g8=(uint8_t)(((r16[y*width+x+1]+r16[(y+1)*width+x])/2)>>8);
            uint8_t b8=(uint8_t)(r16[(y+1)*width+x+1]>>8);
            int i00=(y*width+x)*3,i01=(y*width+x+1)*3,i10=((y+1)*width+x)*3,i11=((y+1)*width+x+1)*3;
            rgb[i00]=rgb[i01]=rgb[i10]=rgb[i11]=r8;
            rgb[i00+1]=rgb[i01+1]=rgb[i10+1]=rgb[i11+1]=g8;
            rgb[i00+2]=rgb[i01+2]=rgb[i10+2]=rgb[i11+2]=b8;
        }
    } else {
        for (int y=0; y<height-1; y+=2) for (int x=0; x<width-1; x+=2) {
            uint8_t r=raw[y*width+x];
            uint8_t g=(uint8_t)((raw[y*width+x+1]+raw[(y+1)*width+x])/2);
            uint8_t b=raw[(y+1)*width+x+1];
            int i00=(y*width+x)*3,i01=(y*width+x+1)*3,i10=((y+1)*width+x)*3,i11=((y+1)*width+x+1)*3;
            rgb[i00]=rgb[i01]=rgb[i10]=rgb[i11]=r;
            rgb[i00+1]=rgb[i01+1]=rgb[i10+1]=rgb[i11+1]=g;
            rgb[i00+2]=rgb[i01+2]=rgb[i10+2]=rgb[i11+2]=b;
        }
    }
}

} // namespace raduk::gui
