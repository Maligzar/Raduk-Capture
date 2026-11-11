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
// Draw is for Direct2D windows and DrawGdi for the rest. Both paint the
// same bar from the same geometry. A GDI window with controls has it
// easier still: Install hooks the window, so its proc needs no change.
//
// Paint reads from shape and weight, not hue: the active window has a
// lit rule and a stronger title, which is still true in night vision.
// Close turns the danger colour on hover because it is the one thing in
// the frame that does something final.
// ============================================================

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <d2d1.h>
#include <dwrite.h>

#include "BundledFonts.h"
#include "ChromeLayout.h"
#include "PanelDock.h"
#include "Theme.h"

#pragma comment(lib, "comctl32.lib")

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

    // How far a window's contents must sit below the top.
    static int CaptionFor(HWND h) {
        if (!h || (GetWindowLongW(h, GWL_STYLE) & WS_CAPTION) != WS_CAPTION) return 0;
        return chrome::ForDpi((int)GetDpiForWindow(h)).captionH;
    }

    struct InstallOptions {
        // For a window laid out once, at fixed positions: everything in
        // it moves below the bar and the window grows to fit. A window
        // that lays out on WM_SIZE adds CaptionFor itself.
        bool  shiftChildren = false;
        // For the dialogs, which never set one and so showed the system
        // default.
        HFONT font = nullptr;
    };

    // Owns itself, and goes when the window does.
    static void Install(HWND h, const chrome::Flags& f, const InstallOptions& o) {
        if (!h) return;
        auto* c = new WindowChrome;
        RECT before = {};
        GetClientRect(h, &before);
        SetWindowSubclass(h, HookProc, 1, reinterpret_cast<DWORD_PTR>(c));
        c->Attach(h, f);
        if (o.font)
            EnumChildWindows(h, [](HWND k, LPARAM lp) -> BOOL {
                SendMessageW(k, WM_SETFONT, (WPARAM)lp, TRUE);
                return TRUE;
            }, (LPARAM)o.font);
        if (!o.shiftChildren) return;

        const int cap = c->CaptionHeight();
        RECT after = {}, wr = {};
        GetClientRect(h, &after);
        GetWindowRect(h, &wr);
        const int extra = before.bottom + cap - after.bottom;
        SetWindowPos(h, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top + extra,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        struct Ctx { HWND parent; int dy; } ctx = {h, cap};
        EnumChildWindows(h, [](HWND k, LPARAM lp) -> BOOL {
            const Ctx* x = reinterpret_cast<const Ctx*>(lp);
            if (GetParent(k) != x->parent) return TRUE;
            RECT r;
            GetWindowRect(k, &r);
            MapWindowPoints(nullptr, x->parent, reinterpret_cast<POINT*>(&r), 2);
            SetWindowPos(k, nullptr, r.left, r.top + x->dy, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return TRUE;
        }, (LPARAM)&ctx);
    }

    void Release() {
        if (m_title)  { m_title->Release();  m_title  = nullptr; }
        if (m_glyphs) { m_glyphs->Release(); m_glyphs = nullptr; }
        if (m_gdiTitle)  { DeleteObject(m_gdiTitle);  m_gdiTitle  = nullptr; }
        if (m_gdiGlyphs) { DeleteObject(m_gdiGlyphs); m_gdiGlyphs = nullptr; }
    }

    // Fullscreen takes WS_CAPTION away, and the bar goes with it.
    bool Showing() const {
        return m_hwnd && (GetWindowLongW(m_hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
    }
    int CaptionHeight() const { return Showing() ? m_metrics.captionH : 0; }

    // True when handled, with the result in out.
    bool Filter(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& out) {
        // Phase 134.3: F11 on a panel in a group fills the monitor with the
        // group, and F11 again restores it. Checked BEFORE the caption
        // test, because a member in group fullscreen has no caption.
        //
        // The main loop routes keys thread-wide (Window::RouteThreadKey),
        // so F11 only reaches a panel that claims it through
        // WM_GETDLGCODE, as the sky map learned. A panel claims it only
        // when it is in a group or in group fullscreen; a lone panel's F11
        // goes where it always went. Esc is never claimed: it is the
        // emergency stop and must reach the mount from every window.
        if (hwnd == m_hwnd && panel_dock::IsRegistered(hwnd)) {
            // 134.4a: Dock / Undock on the window menu (right-click the
            // title, or Alt+Space). The menu is only prepared here; it
            // goes on to Windows to be shown.
            if (msg == WM_INITMENUPOPUP && HIWORD(lp))
                panel_dock::PrepareMenu(hwnd, reinterpret_cast<HMENU>(wp));
            if (msg == WM_SYSCOMMAND) {
                const UINT cmd = (UINT)(wp & 0xFFF0);
                if (cmd == panel_dock::kDockCmd) { panel_dock::ToggleDock(hwnd); out = 0; return true; }
                // A docked panel is sized by the dock; maximising it would
                // tear it out of its slot.
                if (cmd == SC_MAXIMIZE && panel_dock::IsDocked(hwnd)) { out = 0; return true; }
            }
            // F11 on a docked panel fills the monitor with the dock.
            if (panel_dock::IsDocked(hwnd)) {
                if (msg == WM_GETDLGCODE && lp && wp == VK_F11) { out = DLGC_WANTMESSAGE; return true; }
                if (msg == WM_KEYDOWN && wp == VK_F11) { panel_dock::DockFullscreen(); out = 0; return true; }
            }
        }
        if (hwnd == m_hwnd && panel_dock::IsRegistered(hwnd) && !panel_dock::IsDocked(hwnd)) {
            const bool grouped = panel_dock::InGroupFull(hwnd) ||
                                 (Showing() && panel_dock::GroupOf(hwnd).size() >= 2);
            if (msg == WM_GETDLGCODE && lp && wp == VK_F11 && grouped) {
                out = DLGC_WANTMESSAGE;
                return true;
            }
            if (msg == WM_KEYDOWN && wp == VK_F11 && grouped) {
                if (panel_dock::InGroupFull(hwnd)) panel_dock::ExitGroupFull();
                else panel_dock::EnterGroupFull(hwnd);
                out = 0;
                return true;
            }
        }
        if (hwnd != m_hwnd || !Showing()) return false;
        // Phase 134.1: the panels that dock snap while dragged or sized,
        // and none can be made smaller than 240 x 120. The proposed
        // rectangle is adjusted in place and the message answered here,
        // TRUE as Windows documents for both. No panel handles these
        // three itself (checked 2026-11-11), so nothing is skipped.
        if (panel_dock::IsRegistered(hwnd)) {
            if (msg == WM_GETMINMAXINFO && lp) {
                auto* mm = reinterpret_cast<MINMAXINFO*>(lp);   // already holds the defaults
                mm->ptMinTrackSize.x = panel_dock::Scaled(hwnd, dock::kMinW);
                mm->ptMinTrackSize.y = panel_dock::Scaled(hwnd, dock::kMinH);
                out = 0;
                return true;
            }
            // 134.2: a drag's snapshot is taken at its start and dropped at
            // its end; both messages carry on to the window afterwards.
            if (panel_dock::Enabled() && msg == WM_ENTERSIZEMOVE) panel_dock::BeginDrag(hwnd);
            if (msg == WM_EXITSIZEMOVE) panel_dock::EndDrag();
            if ((panel_dock::Enabled() || panel_dock::IsDocked(hwnd)) && msg == WM_MOVING) {
                panel_dock::DragMove(hwnd, reinterpret_cast<RECT*>(lp));
                out = TRUE;
                return true;
            }
            if (panel_dock::Enabled() && msg == WM_SIZING) {
                panel_dock::DragSize(hwnd, reinterpret_cast<RECT*>(lp), dock::EdgesFromSizing((int)wp));
                out = TRUE;
                return true;
            }
        }
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

    // The GDI twin of Draw, called from WM_PAINT.
    void DrawGdi(HDC hdc, int clientW) {
        if (!Showing()) return;
        MakeGdiFonts();
        const int h = m_metrics.captionH;
        RECT bar = {0, 0, clientW, h};
        FillRect(hdc, &bar, Theme::Brush(Theme::Rgb::PANEL));
        RECT line = {0, h - 1, clientW, h};
        FillRect(hdc, &line, Theme::Brush(Theme::Rgb::BORDER));
        if (m_active) {
            const int w = (m_metrics.inset + 2) / 4;
            RECT rule = {0, h / 4, w, h * 3 / 4};
            FillRect(hdc, &rule, Theme::Brush(m_accent));
        }

        SetBkMode(hdc, TRANSPARENT);
        HGDIOBJ old = SelectObject(hdc, m_gdiTitle);
        wchar_t title[128] = {};
        const int n = GetWindowTextW(m_hwnd, title, 128);
        for (int i = 0; i < n; ++i) title[i] = towupper(title[i]);
        SetTextColor(hdc, Theme::NV(m_active ? Theme::Rgb::TEXT : Theme::Rgb::TEXT_DIM));
        RECT tr = {m_metrics.inset, 0, chrome::TitleRight(clientW, m_metrics, m_flags), h};
        DrawTextW(hdc, title, n, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        const chrome::Buttons bs = chrome::LayoutButtons(clientW, m_metrics, m_flags);
        SelectObject(hdc, m_gdiGlyphs);
        const wchar_t maxGlyph = IsZoomed(m_hwnd) ? L'\xE923' : L'\xE922';
        if (m_flags.minimize) ButtonGdi(hdc, bs.min, chrome::Zone::Minimize, L'\xE921');
        if (m_flags.maximize) ButtonGdi(hdc, bs.max, chrome::Zone::Maximize, maxGlyph);
        ButtonGdi(hdc, bs.close, chrome::Zone::Close, L'\xE8BB');
        SelectObject(hdc, old);
    }

private:
    static LRESULT CALLBACK HookProc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                     UINT_PTR, DWORD_PTR data) {
        auto* c = reinterpret_cast<WindowChrome*>(data);
        LRESULT out = 0;
        if (c->Filter(h, msg, wp, lp, out)) return out;
        if (msg == WM_PAINT) {
            const LRESULT r = DefSubclassProc(h, msg, wp, lp);
            RECT cr;
            GetClientRect(h, &cr);
            HDC dc = GetDC(h);
            c->DrawGdi(dc, cr.right);
            ReleaseDC(h, dc);
            return r;
        }
        if (msg == WM_NCDESTROY) {
            RemoveWindowSubclass(h, HookProc, 1);
            c->Release();
            delete c;
        }
        return DefSubclassProc(h, msg, wp, lp);
    }

    void ButtonGdi(HDC hdc, const chrome::Box& box, chrome::Zone z, wchar_t glyph) {
        RECT r = {box.left, box.top, box.right, box.bottom - 1};
        const bool hot = m_hover == z, down = m_pressed == z;
        if (hot || down)
            FillRect(hdc, &r, Theme::Brush(z == chrome::Zone::Close ? Theme::Rgb::ACCENT4
                                                                    : Theme::Rgb::BORDER));
        SetTextColor(hdc, Theme::NV((m_active || hot) ? Theme::Rgb::TEXT : Theme::Rgb::TEXT_DIM));
        DrawTextW(hdc, &glyph, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    void MakeGdiFonts() {
        if (m_gdiTitle) return;
        const int tp = (int)(m_metrics.captionH * 0.47f);
        const int gp = (int)(m_metrics.captionH * 0.3f);
        m_gdiTitle = CreateFontW(-tp, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, Theme::FONT_LABEL);
        m_gdiGlyphs = CreateFontW(-gp, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
    }

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
    HFONT m_gdiTitle = nullptr, m_gdiGlyphs = nullptr;
};

} // namespace raduk::gui
