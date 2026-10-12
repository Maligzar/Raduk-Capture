#pragma once
// ============================================================
// StatusBar.h — Bottom status strip for RadukCapture
// Milestone 5 — drawn by Direct2D, sits below the preview.
// Shows: FPS, drops, sensor temp, resolution, EAF pos+temp,
//        mount RA/DEC + tracking, active profile name.
// ============================================================

#include "Theme.h"
#include <d2d1.h>
#include <dwrite.h>
#include <cstdint>
#include <string>
#include <cstdio>
#include "../core/DeviceFactory.h"

namespace raduk::gui {

static constexpr int STATUS_BAR_HEIGHT = 32;

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
            Theme::NVf(D2D1::ColorF(0.043f, 0.082f, 0.122f)), &m_bgBrush);   // --panel color
        if (FAILED(hr)) return false;

        hr = m_rt->CreateSolidColorBrush(
            Theme::NVf(D2D1::ColorF(0.043f, 0.137f, 0.208f)), &m_borderBrush); // --border color
        if (FAILED(hr)) return false;

        hr = m_rt->CreateSolidColorBrush(
            Theme::NVf(D2D1::ColorF(0.0f, 0.898f, 1.0f)), &m_accentBrush);    // --accent cyan
        if (FAILED(hr)) return false;

        hr = m_rt->CreateSolidColorBrush(
            Theme::NVf(D2D1::ColorF(0.722f, 0.831f, 0.910f)), &m_textBrush);  // --text
        if (FAILED(hr)) return false;

        hr = m_rt->CreateSolidColorBrush(
            Theme::NVf(D2D1::ColorF(0.550f, 0.686f, 0.745f)), &m_dimBrush);   // --dim #8CAFBE
        if (FAILED(hr)) return false;

        hr = m_rt->CreateSolidColorBrush(
            Theme::NVf(D2D1::ColorF(0.302f, 1.0f, 0.569f)), &m_greenBrush);   // --green #4DFF91
        if (FAILED(hr)) return false;

        hr = m_rt->CreateSolidColorBrush(
            Theme::NVf(D2D1::ColorF(1.0f, 0.725f, 0.0f)), &m_amberBrush);     // --amber #FFB900
        if (FAILED(hr)) return false;

        hr = m_dw->CreateTextFormat(
            L"Space Mono", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            10.0f, L"en-us", &m_font);
        if (FAILED(hr)) return false;

        m_font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
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

        // ── Live indicator ─────────────────────────────────────
        auto dot = m_emulator
            ? Theme::NVf(D2D1::ColorF(1.0f, 0.667f, 0.0f))   // amber = emulator
            : Theme::NVf(D2D1::ColorF(0.302f, 1.0f, 0.569f)); // green = live hardware
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
        DrawText(buf, x, y, 35.0f, h, modeBrush);
        x += 38.0f;

        DrawSep(x, y, h); x += 18.0f;

        // ── FPS ────────────────────────────────────────────────
        ID2D1SolidColorBrush* fpsBrush = (m_fps > 30) ? m_accentBrush : m_amberBrush;
        swprintf(buf, 32, L"%.1f FPS", m_fps);
        DrawText(buf, x, y, 70.0f, h, fpsBrush);
        x += 74.0f;

        // ── Drops ──────────────────────────────────────────────
        ID2D1SolidColorBrush* dropBrush = (m_drops == 0) ? m_dimBrush : m_amberBrush;
        swprintf(buf, 32, L"DROPS: %d", m_drops);
        DrawText(buf, x, y, 80.0f, h, dropBrush);
        x += 84.0f;

        DrawSep(x, y, h); x += 18.0f;

        // ── Resolution ─────────────────────────────────────────
        const wchar_t* fmt = (m_bitDepth == 16) ? L"%dx%d RAW16" : L"%dx%d RAW8";
        swprintf(buf, 64, fmt, m_roiW, m_roiH);
        DrawText(buf, x, y, 110.0f, h, m_textBrush);
        x += 114.0f;

        // ── Sensor temp ────────────────────────────────────────
        swprintf(buf, 32, L"%.1f\xB0""C", m_sensorTemp);
        DrawText(buf, x, y, 55.0f, h, m_dimBrush);
        x += 59.0f;

        DrawSep(x, y, h); x += 18.0f;

        // ── EAF ────────────────────────────────────────────────
        if (m_eafPos >= 0) {
            ID2D1SolidColorBrush* eafBrush = m_eafMoving ? m_amberBrush : m_textBrush;
            swprintf(buf, 64, L"EAF: %d  %.1f\xB0""C%s",
                m_eafPos, m_eafTemp, m_eafMoving ? L" [MOVING]" : L"");
            DrawText(buf, x, y, 160.0f, h, eafBrush);
            x += 164.0f;

            DrawSep(x, y, h); x += 18.0f;
        }

        // ── Mount ──────────────────────────────────────────────
        if (m_mountConn) {
            swprintf(buf, 128, L"RA: %hs  DEC: %hs  TRK: %hs",
                m_ra.c_str(), m_dec.c_str(), m_tracking.c_str());
            DrawText(buf, x, y, 320.0f, h, m_textBrush);
            x += 324.0f;

            DrawSep(x, y, h); x += 18.0f;
        }

        // ── Profile (right-aligned) ────────────────────────────
        if (!m_profileName.empty()) {
            swprintf(buf, 64, L"[%hs]", m_profileName.c_str());
            float pw = 100.0f;
            DrawText(buf, w - pw - 8.0f, y, pw, h, m_accentBrush);
        }
    }

private:
    void DrawText(const wchar_t* text, float x, float y,
                  float w, float h, ID2D1SolidColorBrush* brush) {
        D2D1_RECT_F r = D2D1::RectF(x, y, x + w, y + h);
        m_rt->DrawText(text, (UINT32)wcslen(text), m_font, r, brush);
    }

    void DrawSep(float x, float y, float h) {
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
};

} // namespace raduk::gui
