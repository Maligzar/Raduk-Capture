// ============================================================
// PreviewRenderer.cpp
// M10: GPU debayer + frame control bar overlay.
// ROI rubber-band: drag on preview image → select new ROI.
//   HandleMouseDown/Move/Up forwarded from Window WM_ messages.
//   Auto-applies after 2s confirmation delay.
// ============================================================
#include "PreviewRenderer.h"
#include "Theme.h"
#include "core/ADCAssistant.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <wincodec.h>   // WIC — PNG encoder
#pragma comment(lib, "windowscodecs.lib")

namespace raduk::gui {

PreviewRenderer::~PreviewRenderer() { Release(); }

bool PreviewRenderer::Init(ID2D1HwndRenderTarget* renderTarget,
                            IDWriteFactory* dwriteFactory) {
    m_renderTarget  = renderTarget;
    m_dwriteFactory = dwriteFactory;
    if (!m_renderTarget || !m_dwriteFactory) return false;

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
    hr = m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.30f, 1.0f, 0.43f, 0.85f), &m_greenBrush);
    if (FAILED(hr)) return false;
    hr = m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.0f, 0.67f, 0.0f, 0.85f), &m_amberBrush);
    if (FAILED(hr)) return false;
    hr = m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.0f, 0.23f, 0.36f, 0.85f), &m_redBrush);
    if (FAILED(hr)) return false;
    // Frame bar brushes
    hr = m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.04f, 0.09f, 0.14f, 0.88f), &m_barBgBrush);  // dark translucent
    if (FAILED(hr)) return false;
    hr = m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.0f, 0.898f, 1.0f, 0.15f), &m_barHiBrush);   // cyan highlight
    if (FAILED(hr)) return false;

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

    // Auto-apply after 2 seconds in Confirming state
    if (m_dragState == DragState::Confirming) {
        auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - m_confirmTime).count();
        if (elapsed >= 2.0 && m_onROIChange && m_pendingROIW > 0) {
            m_dragState = DragState::Idle;
            m_showResetPopup = false;
            m_onROIChange(m_pendingROIW, m_pendingROIH, m_pendingROIX, m_pendingROIY);
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

void PreviewRenderer::RenderFrame(const raduk::core::FrameSlot* frame,
                                   int previewX, int previewWidth, int windowHeight,
                                   int statusBarHeight,
                                   double fps, uint64_t frameCount, int droppedFrames) {
    if (!m_renderTarget) return;

    int previewHeight = windowHeight - statusBarHeight;
    float px = (float)previewX;
    float pw = (float)previewWidth;
    float ph = (float)previewHeight;

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

    if (hasFrame) {
        D2D1_RECT_F destRect = CalculateZoomedRect(
            frame->width, frame->height, previewX, previewWidth, previewHeight);

        // ── Draw sensor image ──────────────────────────────────
        if (m_debayerMode == DebayerMode::Color) {
            DebayerRGGB(frame->data, m_rgbBuffer,
                        frame->width, frame->height, frame->bitDepth);
            size_t n = (size_t)frame->width * frame->height;
            std::vector<uint8_t> rgba(n * 4);

            // Clip flash: alternate red / original every 8 frames
            bool flashOn = m_showClip && ((m_clipFlash / 8) % 2 == 0);
            ++m_clipFlash;

            for (size_t i = 0; i < n; i++) {
                uint8_t r = m_rgbBuffer[i*3+0];
                uint8_t g = m_rgbBuffer[i*3+1];
                uint8_t b = m_rgbBuffer[i*3+2];
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
                constexpr float PLATE_SCALE = 0.203f; // arcsec/px
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

            if (m_bitmap) {
                D2D1_RECT_U srcRect = D2D1::RectU(0, 0, frame->width, frame->height);
                m_bitmap->CopyFromMemory(&srcRect, rgba.data(), frame->width * 4);
                m_renderTarget->DrawBitmap(m_bitmap, destRect, 1.0f,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
        } else { // DebayerMode::Raw — grayscale Bayer
            size_t n = (size_t)frame->width * frame->height;
            std::vector<uint8_t> rgba(n * 4);
            const uint8_t* src = frame->data;
            bool flashOn = m_showClip && ((m_clipFlash / 8) % 2 == 0);
            for (size_t i = 0; i < n; i++) {
                uint8_t v = (frame->bitDepth == 16)
                    ? (uint8_t)(reinterpret_cast<const uint16_t*>(src)[i] >> 4)
                    : src[i];
                if (flashOn && v == 255) {
                    rgba[i*4+0] = 220; rgba[i*4+1] = 10; rgba[i*4+2] = 10;
                } else {
                    rgba[i*4+0] = rgba[i*4+1] = rgba[i*4+2] = v;
                }
                rgba[i*4+3] = 255;
            }
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

    // ADC overlay — dispersion arrow + severity label
    if (m_showADC && m_mountAltDeg > 0.0)
        DrawADCOverlay(pw, ph);

    // Quality bar graph — per-frame sharpness history (Phase 25a)
    if (m_showQualityGraph && m_qualCount > 0)
        DrawQualityGraph(px, pw, ph);

    // HUD
    if (m_hudFont && m_titleFont) {
        wchar_t buf[256];
        const wchar_t* debayerTag =
            m_debayerMode == DebayerMode::Color ? L"COLOR" : L"RAW";

        swprintf(buf, 256, L"RADUKCAPTURE  ASI678MC  %.1f FPS  [%s]", fps, debayerTag);
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

        // RGB Align HUD — show shift value when enabled
        if (m_rgbAlignEnabled && m_amberBrush) {
            wchar_t rgbBuf[32];
            swprintf(rgbBuf, 32, L"RGB ALIGN  %+d px", m_rgbAlignShift);
            D2D1_RECT_F rgbRect = D2D1::RectF(px+pw-160, 74, px+pw-8, 88);
            m_hudFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            m_renderTarget->DrawText(rgbBuf, (UINT32)wcslen(rgbBuf),
                                     m_hudFont, rgbRect, m_amberBrush);
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

// ── DrawAnalysisOverlay ───────────────────────────────────────
// No-op — histogram drawn in ControlPanel::PaintHistogram (GDI WM_PAINT).

void PreviewRenderer::DrawAnalysisOverlay(const raduk::core::AnalysisResult& /*analysis*/,
                                           int /*previewX*/, int /*previewWidth*/,
                                           int /*windowHeight*/, int /*statusBarHeight*/) {
    // No-op: histogram now drawn in ControlPanel::PaintHistogram (GDI WM_PAINT).
}

// ── DrawPixelReadout ──────────────────────────────────────────
// Maps current hover/locked screen position to frame pixel coords,
// reads RGB from m_rgbBuffer, shows tooltip + crosshair.
// Tooltip follows cursor when unlocked, stays fixed when locked.

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
    const float BAR_W     = 1.5f;    // each bar width
    const float GAP       = 0.5f;    // gap between bars

    // Position: bottom-left, above status line
    float gx = px + MARGIN;
    float gy = ph - 24.0f - GRAPH_H - 8.0f;  // just above HUD status

    // Semi-transparent background
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

    // ── Draw bars oldest → newest ──────────────────────────────
    int   n       = m_qualCount;
    int   maxBars = (int)((GRAPH_W - LABEL_W) / (BAR_W + GAP));
    int   start   = (n > maxBars) ? n - maxBars : 0;
    float peak    = 0.0f;
    float sum     = 0.0f;

    for (int i = start; i < n; i++) {
        int   idx  = (m_qualHead - n + i + QUAL_HISTORY) % QUAL_HISTORY;
        float val  = m_qualHistory[idx];
        if (val > peak) peak = val;
        sum += val;

        float barH = (val / 100.0f) * GRAPH_H;
        float bx   = gx + (float)(i - start) * (BAR_W + GAP);
        float by   = gy + GRAPH_H - barH;

        // Color: green >= 60, amber >= 30, red < 30
        ID2D1SolidColorBrush* barBrush =
            (val >= 60.0f) ? m_greenBrush :
            (val >= 30.0f) ? m_amberBrush : m_redBrush;
        if (barBrush)
            m_renderTarget->FillRectangle(
                D2D1::RectF(bx, by, bx + BAR_W, gy + GRAPH_H), barBrush);
    }

    // ── Baseline ──────────────────────────────────────────────
    if (m_dimBrush)
        m_renderTarget->DrawLine(
            D2D1::Point2F(gx, gy + GRAPH_H),
            D2D1::Point2F(gx + GRAPH_W - LABEL_W, gy + GRAPH_H),
            m_dimBrush, 0.5f);

    // ── Label area: SHARP + peak + mean ──────────────────────
    if (!m_hudFont) return;
    float lx = gx + GRAPH_W - LABEL_W + 4;
    float mean = (n > 0) ? sum / n : 0.0f;

    ID2D1SolidColorBrush* labelBrush =
        (peak >= 60.0f) ? m_greenBrush :
        (peak >= 30.0f) ? m_amberBrush : m_redBrush;
    if (!labelBrush) return;

    m_hudFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    // "SHARP" header
    m_renderTarget->DrawText(L"SHARP", 5, m_hudFont,
        D2D1::RectF(lx, gy, lx + LABEL_W, gy + 14), m_dimBrush ? m_dimBrush : labelBrush);

    // Peak value
    wchar_t buf[24];
    swprintf(buf, 24, L"PK %.0f%%", peak);
    m_renderTarget->DrawText(buf, (UINT32)wcslen(buf), m_hudFont,
        D2D1::RectF(lx, gy + 14, lx + LABEL_W, gy + 28), labelBrush);

    // Mean value
    swprintf(buf, 24, L"AV %.0f%%", mean);
    m_renderTarget->DrawText(buf, (UINT32)wcslen(buf), m_hudFont,
        D2D1::RectF(lx, gy + 28, lx + LABEL_W, gy + 42), m_dimBrush ? m_dimBrush : labelBrush);
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
    float smaller = (rw < rh) ? rw : rh;
    float gap = smaller * 0.018f;
    float r   = smaller * 0.10f;
    float left = cx-rw/2.0f, right=cx+rw/2.0f, top=cy-rh/2.0f, bot=cy+rh/2.0f;
    m_renderTarget->DrawLine(D2D1::Point2F(left, cy),    D2D1::Point2F(cx-gap, cy),   m_reticleBrush, 1.5f);
    m_renderTarget->DrawLine(D2D1::Point2F(cx+gap, cy),  D2D1::Point2F(right, cy),    m_reticleBrush, 1.5f);
    m_renderTarget->DrawLine(D2D1::Point2F(cx, top),     D2D1::Point2F(cx, cy-gap),   m_reticleBrush, 1.5f);
    m_renderTarget->DrawLine(D2D1::Point2F(cx, cy+gap),  D2D1::Point2F(cx, bot),      m_reticleBrush, 1.5f);
    m_renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy), r, r),            m_reticleBrush, 1.5f);
    m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy), 2.5f, 2.5f),     m_reticleBrush);
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
    if (m_textBrush)   { m_textBrush->Release();   m_textBrush   = nullptr; }
    if (m_accentBrush) { m_accentBrush->Release(); m_accentBrush = nullptr; }
    if (m_dimBrush)    { m_dimBrush->Release();    m_dimBrush    = nullptr; }
    if (m_reticleBrush){ m_reticleBrush->Release();m_reticleBrush= nullptr; }
    if (m_greenBrush)  { m_greenBrush->Release();  m_greenBrush  = nullptr; }
    if (m_amberBrush)  { m_amberBrush->Release();  m_amberBrush  = nullptr; }
    if (m_redBrush)    { m_redBrush->Release();    m_redBrush    = nullptr; }
    if (m_barBgBrush)  { m_barBgBrush->Release();  m_barBgBrush  = nullptr; }
    if (m_barHiBrush)  { m_barHiBrush->Release();  m_barHiBrush  = nullptr; }
    if (m_hudFont)     { m_hudFont->Release();     m_hudFont     = nullptr; }
    if (m_titleFont)   { m_titleFont->Release();   m_titleFont   = nullptr; }
    if (m_barFont)     { m_barFont->Release();     m_barFont     = nullptr; }
    delete[] m_rgbBuffer;
    m_rgbBuffer = nullptr; m_rgbBufferSize = 0;
    m_hasLastRect = false;
}

// ── DebayerRGGB (CPU fallback) ────────────────────────────────

void PreviewRenderer::DebayerRGGB(const uint8_t* raw, uint8_t* rgb,
                                   int width, int height, int bitDepth) {
    if (bitDepth == 16) {
        const uint16_t* r16 = reinterpret_cast<const uint16_t*>(raw);
        for (int y=0; y<height-1; y+=2) for (int x=0; x<width-1; x+=2) {
            uint8_t r8=(uint8_t)(r16[y*width+x]>>4);
            uint8_t g8=(uint8_t)(((r16[y*width+x+1]+r16[(y+1)*width+x])/2)>>4);
            uint8_t b8=(uint8_t)(r16[(y+1)*width+x+1]>>4);
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
