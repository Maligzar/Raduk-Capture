#pragma once
// ============================================================
// StatusBar.h — Bottom status strip for RadukCapture
// Milestone 5 — drawn by Direct2D, sits below the preview.
// Shows: FPS, drops, sensor temp, resolution, EAF pos+temp,
//        mount RA/DEC + tracking, active profile name.
// ============================================================

#include "BundledFonts.h"
#include "ToolbarLayout.h"
#include "Theme.h"
#include <d2d1.h>
#include <dwrite.h>
#include <cstdint>
#include <string>
#include <cstdio>
#include "../core/DeviceFactory.h"

namespace raduk::gui {

static constexpr int STATUS_BAR_HEIGHT = TOOLBAR_HEIGHT;

// Font size for the bottom bar, and the factor every hardcoded field
// width is multiplied by. The two have to move together: Space Mono is
// monospace, so a glyph at 13px is 1.3x the width of one at 10px, and
// raising the size alone would clip every field to its old box.
//
// Measured at 13px, maximised: the run ends at 1247 of the 1360 DIPs
// the bar gets (previewW = 1920 - PANEL_WIDTH), with every field
// present including the mount. That is the normal case and it fits.
//
// It does not fit at every window size, which is the point of the
// right limit in DrawText below. Restored to 1440 wide the bar has
// about 880 DIPs, and the run wants ~1050 with the mount connected --
// so the field widths cannot be trusted to fit and something has to
// give way gracefully rather than overprint.
static constexpr float STATUS_FONT_PX = 14.0f;
static constexpr float STATUS_W_SCALE = STATUS_FONT_PX / 10.0f;

class StatusBar {
public:
    StatusBar() = default;
    ~StatusBar() { Release(); }

    bool Init(ID2D1HwndRenderTarget* rt, IDWriteFactory* dw) {
        m_rt = rt;
        m_dw = dw;
        m_paletteGen = Theme::PaletteGeneration();
        if (!m_rt || !m_dw) return false;

        HRESULT hr;
        hr = m_rt->CreateSolidColorBrush(
            Theme::Color(Theme::PANEL), &m_bgBrush);
        if (FAILED(hr)) return false;

        hr = m_rt->CreateSolidColorBrush(
            Theme::Color(Theme::BORDER), &m_borderBrush);
        if (FAILED(hr)) return false;

        hr = m_rt->CreateSolidColorBrush(
            Theme::Color(Theme::ACCENT), &m_accentBrush);
        if (FAILED(hr)) return false;

        hr = m_rt->CreateSolidColorBrush(
            Theme::Color(Theme::TEXT), &m_textBrush);
        if (FAILED(hr)) return false;

        hr = m_rt->CreateSolidColorBrush(
            Theme::Color(Theme::TEXT_DIM), &m_dimBrush);
        if (FAILED(hr)) return false;

        hr = m_rt->CreateSolidColorBrush(
            Theme::Color(Theme::GREEN), &m_greenBrush);
        if (FAILED(hr)) return false;

        hr = m_rt->CreateSolidColorBrush(
            Theme::Color(Theme::AMBER), &m_amberBrush);
        if (FAILED(hr)) return false;

        hr = m_dw->CreateTextFormat(
            raduk::gui::Fonts::FamilyFor(L"Space Mono"), raduk::gui::Fonts::CollectionFor(L"Space Mono"),
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            STATUS_FONT_PX, L"en-us", &m_font);
        if (FAILED(hr)) return false;

        m_font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        // A strip is one line. Wrapped, the end of the mount field
        // dropped below it and out of the window.
        m_font->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        m_font->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        printf("[StatusBar] Initialized\n");
        return true;
    }

    void Release() {
        if (m_bgBrush)     { m_bgBrush->Release();     m_bgBrush = nullptr; }
        if (m_borderBrush) { m_borderBrush->Release(); m_borderBrush = nullptr; }
        if (m_accentBrush) { m_accentBrush->Release(); m_accentBrush = nullptr; }
        if (m_textBrush)   { m_textBrush->Release();   m_textBrush = nullptr; }
        if (m_dimBrush)    { m_dimBrush->Release();    m_dimBrush = nullptr; }
        if (m_greenBrush)  { m_greenBrush->Release();  m_greenBrush = nullptr; }
        if (m_amberBrush)  { m_amberBrush->Release();  m_amberBrush = nullptr; }
        if (m_font)        { m_font->Release();         m_font = nullptr; }
    }

    // Update live data (call from UpdateReadouts, ~2Hz)
    void SetCameraStats(double fps, int drops, float sensorTemp,
                        int roiW, int roiH, int bitDepth) {
        m_fps        = fps;
        m_drops      = drops;
        m_sensorTemp = sensorTemp;
        m_roiW = roiW; m_roiH = roiH;
        m_bitDepth   = bitDepth;
    }

    void SetFocuserStats(int position, float temp, bool isMoving) {
        m_eafPos     = position;
        m_eafTemp    = temp;
        m_eafMoving  = isMoving;
    }

    void SetMountStats(const std::string& ra, const std::string& dec,
                       const std::string& tracking, bool connected) {
        m_ra         = ra;
        m_dec        = dec;
        m_tracking   = tracking;
        m_mountConn  = connected;
    }

    void SetProfile(const std::string& name) {
        m_profileName = name;
    }

    void SetEmulatorMode(bool emulator) {
        m_emulator = emulator;
    }

    // Draw the status bar — call inside BeginDraw/EndDraw
    // y = top of status bar in window coords (windowHeight - STATUS_BAR_HEIGHT)
    void Draw(int windowWidth, int windowHeight) {
        // Phase 26: D2D brushes are COM objects built once in Init, so a
        // palette change has to rebuild them -- tinting the colour alone
        // would leave every existing brush holding the old one. Rebuilt
        // here rather than from the toggle so this class does not need
        // to be told about the mode at all.
        if (m_rt && m_dw && m_paletteGen != Theme::PaletteGeneration()) {
            ID2D1HwndRenderTarget* rt = m_rt;
            IDWriteFactory*        dw = m_dw;
            Release();
            Init(rt, dw);
        }
        if (!m_rt || !m_font) return;

        float y = (float)(windowHeight - STATUS_BAR_HEIGHT);
        float w = (float)windowWidth;

        // Background
        D2D1_RECT_F bg = D2D1::RectF(0, y, w, (float)windowHeight);
        m_rt->FillRectangle(bg, m_bgBrush);

        // Top border line
        m_rt->DrawLine(
            D2D1::Point2F(0, y),
            D2D1::Point2F(w, y),
            m_borderBrush, 1.0f);

        // Build status segments and draw left-to-right with separators
        float x = 8.0f;
        float h = (float)STATUS_BAR_HEIGHT;

        // Reserve the right-aligned profile label's space BEFORE the
        // left-to-right run, and stop the run there.
        //
        // Every field width below is a hardcoded constant scaled by the
        // font size, so the run's total length is fixed while the space
        // it has to fit in is not: the bar is previewW wide, which
        // changes with the window and the control panel. At 13px the run
        // ends about where the profile label begins, and on anything
        // narrower than this screen it would simply draw over it --
        // RA/DEC and the profile name interleaved into unreadable mush.
        // Clipping to a limit degrades instead: the last field that
        // fits gets what room is left and loses its tail, which for the
        // mount field is well past the end of the text anyway.
        const float profW = 100.0f * STATUS_W_SCALE;
        m_rightLimit = m_profileName.empty() ? w - 8.0f
                                             : w - profW - 16.0f;

        // ── Live indicator ─────────────────────────────────────
        auto dot = m_emulator
            ? Theme::Color(Theme::AMBER)   // caution = emulator
            : Theme::Color(Theme::GREEN);  // live = real hardware
        ID2D1SolidColorBrush* dotBrush = m_emulator ? m_amberBrush : m_greenBrush;

        // Draw filled circle as live dot
        D2D1_ELLIPSE ellipse = D2D1::Ellipse(
            D2D1::Point2F(x + 5, y + h / 2.0f), 4.0f, 4.0f);
        m_rt->FillEllipse(ellipse, dotBrush);
        x += 14.0f;

        wchar_t buf[512];
        const wchar_t* SEP = L"  |  ";

        // ── Mode label ─────────────────────────────────────────
        const wchar_t* modeLabel = m_emulator ? L"EMU" : L"LIVE";
        ID2D1SolidColorBrush* modeBrush = m_emulator ? m_amberBrush : m_greenBrush;
        swprintf(buf, 8, L"%s", modeLabel);
        DrawText(buf, x, y, 35.0f * STATUS_W_SCALE, h, modeBrush);
        x += 38.0f  * STATUS_W_SCALE;

        DrawSep(x, y, h); x += 9.0f * STATUS_W_SCALE;

        // ── FPS ────────────────────────────────────────────────
        ID2D1SolidColorBrush* fpsBrush = (m_fps > 30) ? m_accentBrush : m_amberBrush;
        swprintf(buf, 32, L"%.1f FPS", m_fps);
        DrawText(buf, x, y, 62.0f * STATUS_W_SCALE, h, fpsBrush);
        x += 66.0f  * STATUS_W_SCALE;

        // ── Drops ──────────────────────────────────────────────
        ID2D1SolidColorBrush* dropBrush = (m_drops == 0) ? m_dimBrush : m_amberBrush;
        swprintf(buf, 32, L"DROPS: %d", m_drops);
        DrawText(buf, x, y, 70.0f * STATUS_W_SCALE, h, dropBrush);
        x += 74.0f  * STATUS_W_SCALE;

        DrawSep(x, y, h); x += 9.0f * STATUS_W_SCALE;

        // ── Resolution ─────────────────────────────────────────
        const wchar_t* fmt = (m_bitDepth == 16) ? L"%dx%d RAW16" : L"%dx%d RAW8";
        swprintf(buf, 64, fmt, m_roiW, m_roiH);
        DrawText(buf, x, y, 96.0f * STATUS_W_SCALE, h, m_textBrush);
        x += 100.0f * STATUS_W_SCALE;

        // ── Sensor temp ────────────────────────────────────────
        swprintf(buf, 32, L"%.1f\xB0""C", m_sensorTemp);
        DrawText(buf, x, y, 55.0f * STATUS_W_SCALE, h, m_dimBrush);
        x += 59.0f  * STATUS_W_SCALE;

        DrawSep(x, y, h); x += 9.0f * STATUS_W_SCALE;

        // ── EAF ────────────────────────────────────────────────
        if (m_eafPos >= 0) {
            ID2D1SolidColorBrush* eafBrush = m_eafMoving ? m_amberBrush : m_textBrush;
            swprintf(buf, 64, L"EAF: %d  %.1f\xB0""C%s",
                m_eafPos, m_eafTemp, m_eafMoving ? L" [MOVING]" : L"");
            DrawText(buf, x, y, 146.0f * STATUS_W_SCALE, h, eafBrush);
            x += 150.0f * STATUS_W_SCALE;

            DrawSep(x, y, h); x += 9.0f * STATUS_W_SCALE;
        }

        // ── Mount ──────────────────────────────────────────────
        if (m_mountConn) {
            swprintf(buf, 128, L"RA: %hs  DEC: %hs  %hs",
                m_ra.c_str(), m_dec.c_str(), m_tracking.c_str());
            DrawText(buf, x, y, 290.0f * STATUS_W_SCALE, h, m_textBrush);
            x += 294.0f * STATUS_W_SCALE;

            DrawSep(x, y, h); x += 9.0f * STATUS_W_SCALE;
        }

        // ── Profile (right-aligned) ────────────────────────────
        if (!m_profileName.empty()) {
            swprintf(buf, 64, L"[%hs]", m_profileName.c_str());
            m_rightLimit = w;            // this one IS the right edge
            DrawText(buf, w - profW - 8.0f, y, profW, h, m_accentBrush);
        }
    }

private:
    void DrawText(const wchar_t* text, float x, float y,
                  float w, float h, ID2D1SolidColorBrush* brush) {
        if (x >= m_rightLimit) return;                  // no room left
        if (x + w > m_rightLimit) w = m_rightLimit - x; // clip the tail
        D2D1_RECT_F r = D2D1::RectF(x, y, x + w, y + h);
        m_rt->DrawText(text, (UINT32)wcslen(text), m_font, r, brush);
    }

    void DrawSep(float x, float y, float h) {
        if (x + 4 >= m_rightLimit) return;  // a separator with nothing after it
        m_rt->DrawLine(
            D2D1::Point2F(x + 4, y + 6),
            D2D1::Point2F(x + 4, y + h - 6),
            m_borderBrush, 1.0f);
    }

    ID2D1HwndRenderTarget*  m_rt           = nullptr;
    IDWriteFactory*         m_dw           = nullptr;
    unsigned                m_paletteGen   = 0;
    ID2D1SolidColorBrush*   m_bgBrush      = nullptr;
    ID2D1SolidColorBrush*   m_borderBrush  = nullptr;
    ID2D1SolidColorBrush*   m_accentBrush  = nullptr;
    ID2D1SolidColorBrush*   m_textBrush    = nullptr;
    ID2D1SolidColorBrush*   m_dimBrush     = nullptr;
    ID2D1SolidColorBrush*   m_greenBrush   = nullptr;
    ID2D1SolidColorBrush*   m_amberBrush   = nullptr;
    IDWriteTextFormat*      m_font         = nullptr;

    // Live data
    double      m_fps         = 0.0;
    int         m_drops       = 0;
    float       m_sensorTemp  = 0.0f;
    int         m_roiW = 1280, m_roiH = 960;
    int         m_bitDepth    = 8;
    int         m_eafPos      = -1;
    float       m_eafTemp     = 0.0f;
    bool        m_eafMoving   = false;
    std::string m_ra, m_dec, m_tracking;
    bool        m_mountConn   = false;
    std::string m_profileName;
    bool        m_emulator    = false;
    // Right edge the left-to-right run may not cross. Set per Draw, since
    // it depends on the bar width and whether a profile label is shown.
    float       m_rightLimit  = 1e9f;
};

// Phase 120: the strip across the top, in a window of its own. It used
// to be drawn into the preview's render target every frame, in the one
// path phases 91 and 93 worked to empty, for figures that change a few
// times a second.
class StatusStripWindow {
public:
    bool Create(HWND parent) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = DefWindowProcW;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = L"RadukStatusStrip";
        RegisterClassExW(&wc);
        m_hwnd = CreateWindowExW(0, L"RadukStatusStrip", L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            TOOLBAR_RAIL_W, 0, 100, STATUS_BAR_HEIGHT, parent, nullptr,
            GetModuleHandle(nullptr), nullptr);
        if (!m_hwnd) return false;
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_factory)))
            return false;
        return SUCCEEDED(m_factory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(m_hwnd, D2D1::SizeU(100, STATUS_BAR_HEIGHT)),
            &m_rt));
    }

    ID2D1HwndRenderTarget* Target() const { return m_rt; }

    void Place(int width) {
        if (!m_hwnd || width <= 0) return;
        m_w = width;
        SetWindowPos(m_hwnd, nullptr, TOOLBAR_RAIL_W, 0, width, STATUS_BAR_HEIGHT,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_rt) m_rt->Resize(D2D1::SizeU(width, STATUS_BAR_HEIGHT));
    }

    void Render(StatusBar& bar) {
        if (!m_rt) return;
        m_rt->BeginDraw();
        bar.Draw(m_w, STATUS_BAR_HEIGHT);
        m_rt->EndDraw();
    }

    void Release() {
        if (m_rt)      { m_rt->Release();      m_rt = nullptr; }
        if (m_factory) { m_factory->Release(); m_factory = nullptr; }
    }

private:
    HWND m_hwnd = nullptr;
    ID2D1Factory* m_factory = nullptr;
    ID2D1HwndRenderTarget* m_rt = nullptr;
    int m_w = 100;
};

} // namespace raduk::gui
