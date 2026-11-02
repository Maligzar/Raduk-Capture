#pragma once
// ============================================================
// WindowChrome.h — Phase 119.3: the app's own title bar and frame
//
// One call in a window's proc replaces its Windows caption. The window
// keeps its real styles, so Windows still gives it rounded corners, a
// shadow, Aero Snap, Alt+Space and the maximise animation; it just stops
// reserving space at the top. That space becomes client area, the window
// paints the bar itself, and Filter tells Windows what each part of it
// is. Reporting the maximise button as HTMAXBUTTON is what keeps Windows
// 11's Snap Layouts on hover.
//
// To adopt it: call Attach after creating the window, Filter first in
// the proc, start drawing at CaptionHeight(), and call Draw last.
// Direct2D only for now. The GDI windows need a GDI painter when they
// are migrated.
//
// Paint reads from shape and weight, not hue: the active window has a
// lit rule and a stronger title, which is still true in night vision.
// Close turns the danger colour on hover because it is the one thing in
// the frame that does something final.
// ============================================================

#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>

#include "BundledFonts.h"
#include "ChromeLayout.h"
#include "Theme.h"

namespace raduk::gui {

class WindowChrome {
public:
    void Attach(HWND hwnd, const chrome::Flags& f, COLORREF accent = Theme::Rgb::ACCENT) {
        m_hwnd = hwnd;
        m_flags = f;
        m_accent = accent;
        m_metrics = chrome::ForDpi((int)GetDpiForWindow(hwnd));
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
                     SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    void Release() {
        if (m_title)  { m_title->Release();  m_title  = nullptr; }
        if (m_glyphs) { m_glyphs->Release(); m_glyphs = nullptr; }
    }

    // Fullscreen takes WS_CAPTION away, and the bar goes with it.
    bool Showing() const {
        return m_hwnd && (GetWindowLongW(m_hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
    }
    int CaptionHeight() const { return Showing() ? m_metrics.captionH : 0; }

    // True when handled, with the result in out.
    bool Filter(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& out) {
        if (hwnd != m_hwnd || !Showing()) return false;
        switch (msg) {
        case WM_NCCALCSIZE: {
            if (!wp) return false;
            auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
            const LONG top = p->rgrc[0].top;
            // Windows keeps its sides and bottom; only the top is ours.
            out = DefWindowProcW(hwnd, msg, wp, lp);
            p->rgrc[0].top = top;
            // A maximised window hangs its frame off the screen.
            if (IsZoomed(hwnd)) {
                const UINT dpi = GetDpiForWindow(hwnd);
                p->rgrc[0].top += GetSystemMetricsForDpi(SM_CYFRAME, dpi) +
                                  GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            }
            return true;
        }
        case WM_NCHITTEST: {
            const LRESULT def = DefWindowProcW(hwnd, msg, wp, lp);
            if (def != HTCLIENT) { out = def; return true; }
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            out = ToHit(ZoneAt(pt.x, pt.y));
            return true;
        }
        case WM_NCMOUSEMOVE: {
            SetHover(FromHit(wp));
            TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE | TME_NONCLIENT, hwnd, 0};
            TrackMouseEvent(&t);
            return false;
        }
        case WM_NCMOUSELEAVE:
        case WM_MOUSELEAVE:
            m_pressed = chrome::Zone::Client;
            SetHover(chrome::Zone::Client);
            return false;
        case WM_MOUSEMOVE:
            SetHover(chrome::Zone::Client);
            return false;
        case WM_NCLBUTTONDOWN:
            // Swallowed: left to Windows it paints a classic button.
            if (IsButton(FromHit(wp))) {
                m_pressed = FromHit(wp);
                Redraw();
                out = 0;
                return true;
            }
            return false;
        case WM_NCLBUTTONUP: {
            const chrome::Zone z = FromHit(wp);
            const bool fire = IsButton(z) && z == m_pressed;
            m_pressed = chrome::Zone::Client;
            Redraw();
            if (!fire) return false;
            WPARAM cmd = SC_CLOSE;
            if (z == chrome::Zone::Minimize) cmd = SC_MINIMIZE;
            if (z == chrome::Zone::Maximize)
                cmd = IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE;
            PostMessageW(hwnd, WM_SYSCOMMAND, cmd, 0);
            out = 0;
            return true;
        }
        case WM_NCACTIVATE:
            m_active = wp != FALSE;
            Redraw();
            // -1 tells Windows not to paint its own caption over ours.
            out = DefWindowProcW(hwnd, msg, wp, -1);
            return true;
        case WM_DPICHANGED:
            m_metrics = chrome::ForDpi(HIWORD(wp));
            Release();
            return false;
        default:
            return false;
        }
    }

    // Last in the window's Draw, inside BeginDraw. clientW in the render
    // target's pixels, which at this app's 96 dpi targets are also its
    // units. Does nothing while the bar is hidden.
    void Draw(ID2D1RenderTarget* rt, IDWriteFactory* dw, int clientW) {
        if (!Showing()) return;
        MakeFormats(dw);
        ID2D1SolidColorBrush* b = nullptr;
        if (FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &b))) return;
        auto paint = [&](COLORREF c) {
            b->SetColor(Theme::NVf(D2D1::ColorF(GetRValue(c) / 255.0f,
                                                GetGValue(c) / 255.0f,
                                                GetBValue(c) / 255.0f)));
            return b;
        };
        const float w = (float)clientW, h = (float)m_metrics.captionH;
        rt->FillRectangle(D2D1::RectF(0, 0, w, h), paint(Theme::Rgb::PANEL));
        rt->FillRectangle(D2D1::RectF(0, h - 1, w, h), paint(Theme::Rgb::BORDER));

        // The accent rule, lit only on the active window.
        const float bar = (float)((m_metrics.inset + 2) / 4);
        if (m_active)
            rt->FillRectangle(D2D1::RectF(0, h * 0.25f, bar, h * 0.75f), paint(m_accent));

        wchar_t title[128] = {};
        const int n = GetWindowTextW(m_hwnd, title, 128);
        for (int i = 0; i < n; ++i) title[i] = towupper(title[i]);
        if (m_title && n > 0) {
            const float tr = (float)chrome::TitleRight(clientW, m_metrics, m_flags);
            rt->DrawText(title, (UINT32)n, m_title,
                         D2D1::RectF((float)m_metrics.inset, 0, tr, h),
                         paint(m_active ? Theme::Rgb::TEXT : Theme::Rgb::TEXT_DIM),
                         D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }

        const chrome::Buttons bs = chrome::LayoutButtons(clientW, m_metrics, m_flags);
        const wchar_t maxGlyph = IsZoomed(m_hwnd) ? L'\xE923' : L'\xE922';
        if (m_flags.minimize) Button(rt, paint, bs.min, chrome::Zone::Minimize, L'\xE921');
        if (m_flags.maximize) Button(rt, paint, bs.max, chrome::Zone::Maximize, maxGlyph);
        Button(rt, paint, bs.close, chrome::Zone::Close, L'\xE8BB');
        b->Release();
    }

private:
    template <class Paint>
    void Button(ID2D1RenderTarget* rt, Paint& paint, const chrome::Box& box,
                chrome::Zone z, wchar_t glyph) {
        const D2D1_RECT_F r = D2D1::RectF((float)box.left, (float)box.top,
                                          (float)box.right, (float)box.bottom - 1);
        const bool hot = m_hover == z, down = m_pressed == z;
        if (hot || down)
            rt->FillRectangle(r, paint(z == chrome::Zone::Close ? Theme::Rgb::ACCENT4
                                                                : Theme::Rgb::BORDER));
        if (m_glyphs)
            rt->DrawText(&glyph, 1, m_glyphs, r,
                         paint((m_active || hot) ? Theme::Rgb::TEXT : Theme::Rgb::TEXT_DIM));
    }

    void MakeFormats(IDWriteFactory* dw) {
        if (!m_title) {
            dw->CreateTextFormat(Fonts::FamilyFor(Theme::FONT_LABEL), Fonts::CollectionFor(Theme::FONT_LABEL),
                DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, m_metrics.captionH * 0.47f, L"en-us", &m_title);
            if (m_title) {
                m_title->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                m_title->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            }
        }
        if (!m_glyphs) {
            // Segoe MDL2 Assets ships with Windows 10 and 11: the same
            // shapes Windows' own buttons use, at any size.
            dw->CreateTextFormat(L"Segoe MDL2 Assets", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                m_metrics.captionH * 0.3f, L"en-us", &m_glyphs);
            if (m_glyphs) {
                m_glyphs->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                m_glyphs->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
        }
    }

    chrome::Zone ZoneAt(int x, int y) const {
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        return chrome::HitTest(x, y, rc.right, m_metrics, m_flags, IsZoomed(m_hwnd) != FALSE);
    }

    static bool IsButton(chrome::Zone z) {
        return z == chrome::Zone::Minimize || z == chrome::Zone::Maximize ||
               z == chrome::Zone::Close;
    }

    static LRESULT ToHit(chrome::Zone z) {
        switch (z) {
        case chrome::Zone::Caption:  return HTCAPTION;
        case chrome::Zone::Minimize: return HTMINBUTTON;
        case chrome::Zone::Maximize: return HTMAXBUTTON;
        case chrome::Zone::Close:    return HTCLOSE;
        case chrome::Zone::Top:      return HTTOP;
        case chrome::Zone::TopLeft:  return HTTOPLEFT;
        case chrome::Zone::TopRight: return HTTOPRIGHT;
        default:                     return HTCLIENT;
        }
    }

    static chrome::Zone FromHit(WPARAM hit) {
        switch (hit) {
        case HTMINBUTTON: return chrome::Zone::Minimize;
        case HTMAXBUTTON: return chrome::Zone::Maximize;
        case HTCLOSE:     return chrome::Zone::Close;
        default:          return chrome::Zone::Client;
        }
    }

    void SetHover(chrome::Zone z) {
        if (z == m_hover) return;
        m_hover = z;
        Redraw();
    }

    void Redraw() {
        RECT r = {0, 0, 0, m_metrics.captionH};
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        r.right = rc.right;
        InvalidateRect(m_hwnd, &r, FALSE);
    }

    HWND           m_hwnd = nullptr;
    chrome::Flags  m_flags;
    chrome::Metrics m_metrics;
    chrome::Zone   m_hover = chrome::Zone::Client;
    chrome::Zone   m_pressed = chrome::Zone::Client;
    COLORREF       m_accent = Theme::Rgb::ACCENT;
    bool           m_active = true;
    IDWriteTextFormat* m_title = nullptr;
    IDWriteTextFormat* m_glyphs = nullptr;
};

} // namespace raduk::gui
