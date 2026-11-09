#pragma once
// ============================================================
// CommandPalette.h -- phase 132.7: Ctrl+K, type what you want
//
// A popup over the main window: type part of a command's name, Up and
// Down to choose, Enter to run it. The list and ranking are
// ActionRegistry.h; running one is the caller's (main.cpp presses the
// action's key through Window::RunKey). This file is the window only.
//
// ── Keys ────────────────────────────────────────────────────
//
// The popup answers WM_GETDLGCODE with DLGC_WANTALLKEYS. That matters:
// Window::RouteThreadKey hands every key aimed at any of this process's
// windows to the main window's shortcuts unless the focused window
// claims it, and without the claim typing "snap" would take a snapshot.
//
// Esc closes the palette and does nothing else. In the main window Esc
// is the emergency stop, which also ends a recording, aborts autofocus
// and resets the guider; closing a palette in the middle of a run must
// not do that. The stop is one more Esc away: the palette closes the
// moment it loses focus, and both STOP buttons are untouched. The
// palette also offers "Emergency stop" as its first entry.
//
// ── Guarded actions ─────────────────────────────────────────
//
// An action that moves the telescope or focuser, or starts a sequence,
// takes Enter twice: the first arms it and the footer says what the
// second will do. Anything else typed, or a different row, disarms it.
//
// ── Look ────────────────────────────────────────────────────
//
// GDI, painted whole into a memory DC from the tokens through
// Theme::Brush and Theme::NV, so night vision reaches every pixel. The
// selected row is the lists' own (132.6): BORDER with the signal
// colour's bar at its left. Shortcuts sit in a hairline badge in the
// data face. Nothing animates.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "ActionRegistry.h"
#include "Theme.h"

namespace raduk::gui {

class CommandPalette {
public:
    using RunFn = std::function<void(const palette::Action&)>;

    static constexpr int kRows = 10;

    ~CommandPalette() {
        if (m_hwnd) DestroyWindow(m_hwnd);
        FreeFonts();
    }

    bool IsOpen() const { return m_hwnd && IsWindowVisible(m_hwnd); }

    // Opens over `owner` with an empty query; if already open, closes
    // (Ctrl+K again is the way out as well as in).
    void Toggle(HWND owner, RunFn run) {
        if (IsOpen()) { Close(); return; }
        m_owner = owner;
        m_run = std::move(run);
        if (!m_hwnd && !Create()) return;
        m_query.clear();
        m_armed = -1;
        Refilter();
        Place();
        ShowWindow(m_hwnd, SW_SHOW);
        SetForegroundWindow(m_hwnd);
        SetFocus(m_hwnd);
    }

    void Close() {
        if (!IsOpen()) return;
        ShowWindow(m_hwnd, SW_HIDE);
        if (m_owner) { SetForegroundWindow(m_owner); SetFocus(m_owner); }
    }

private:
    HWND   m_hwnd  = nullptr;
    HWND   m_owner = nullptr;
    RunFn  m_run;
    std::string m_query;
    std::vector<palette::Ranked> m_hits;
    int    m_sel   = 0;
    int    m_top   = 0;       // first visible hit
    int    m_armed = -1;      // table index of a guarded action awaiting Enter
    float  m_scale = 1.0f;
    HFONT  m_fQuery = nullptr, m_fRow = nullptr, m_fBadge = nullptr, m_fFoot = nullptr;

    int S(int px) const { return (int)(px * m_scale + 0.5f); }
    int QueryH() const  { return S(44); }
    int RowH() const    { return S(30); }
    int FootH() const   { return S(26); }
    int Width() const   { return S(560); }
    int Height() const  { return QueryH() + RowH() * kRows + FootH(); }

    bool Create() {
        static bool registered = false;
        const HINSTANCE inst = GetModuleHandleW(nullptr);
        if (!registered) {
            WNDCLASSEXW wc = {};
            wc.cbSize        = sizeof(wc);
            wc.lpfnWndProc   = Proc;
            wc.hInstance     = inst;
            wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
            wc.lpszClassName = L"RadukCommandPalette";
            if (!RegisterClassExW(&wc)) return false;
            registered = true;
        }
        m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"RadukCommandPalette", L"Commands",
                                 WS_POPUP, 0, 0, 10, 10, m_owner, nullptr, inst, this);
        return m_hwnd != nullptr;
    }

    void FreeFonts() {
        for (HFONT* f : {&m_fQuery, &m_fRow, &m_fBadge, &m_fFoot})
            if (*f) { DeleteObject(*f); *f = nullptr; }
    }

    void MakeFonts() {
        FreeFonts();
        auto mk = [&](int px, int weight, const wchar_t* face) {
            return CreateFontW(-S(px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, face);
        };
        m_fQuery = mk(Theme::Type::TITLE,  FW_NORMAL, Theme::FONT_LABEL);
        m_fRow   = mk(Theme::Type::STRONG, FW_NORMAL, Theme::FONT_LABEL);
        m_fBadge = mk(Theme::Type::MICRO,  FW_NORMAL, Theme::FONT_DATA);
        m_fFoot  = mk(Theme::Type::BODY,   FW_NORMAL, Theme::FONT_LABEL);
    }

    // Centred over the owner's monitor, a fifth of the way down: where
    // the eye already is, clear of the preview's centre.
    void Place() {
        const UINT dpi = m_owner ? GetDpiForWindow(m_owner) : 96;
        const float scale = (dpi ? dpi : 96) / 96.0f;
        if (scale != m_scale || !m_fRow) { m_scale = scale; MakeFonts(); }
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(MonitorFromWindow(m_owner ? m_owner : m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        const RECT& w = mi.rcWork;
        const int x = w.left + ((w.right - w.left) - Width()) / 2;
        const int y = w.top + (w.bottom - w.top) / 5;
        SetWindowPos(m_hwnd, HWND_TOP, x, y, Width(), Height(), SWP_NOACTIVATE);
    }

    void Refilter() {
        m_hits = palette::Rank(m_query, palette::All());
        m_sel = 0;
        m_top = 0;
        m_armed = -1;
        if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void Move(int d) {
        if (m_hits.empty()) return;
        m_sel = std::max(0, std::min((int)m_hits.size() - 1, m_sel + d));
        if (m_sel < m_top) m_top = m_sel;
        if (m_sel >= m_top + kRows) m_top = m_sel - kRows + 1;
        m_armed = -1;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void Activate() {
        if (m_hits.empty()) return;
        const int idx = (int)m_hits[m_sel].index;
        const palette::Action& a = palette::All()[idx];
        if (a.guarded && m_armed != idx) {
            m_armed = idx;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        Close();
        if (m_run) m_run(a);
    }

    void Paint(HDC target) {
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bmp = CreateCompatibleBitmap(target, rc.right, rc.bottom);
        HGDIOBJ oldBmp = SelectObject(dc, bmp);
        SetBkMode(dc, TRANSPARENT);

        FillRect(dc, &rc, Theme::Brush(Theme::Rgb::PANEL));

        // The query line: a prompt in the signal colour, then what has
        // been typed, or what to do when nothing has.
        const int pad = S(14);
        RECT q = { pad, 0, rc.right - pad, QueryH() };
        HGDIOBJ oldF = SelectObject(dc, m_fQuery);
        SetTextColor(dc, Theme::NV(Theme::Rgb::ACCENT));
        DrawTextW(dc, L"›", 1, &q, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        q.left += S(20);
        if (m_query.empty()) {
            SetTextColor(dc, Theme::NV(Theme::Rgb::TEXT_DIM));
            DrawTextW(dc, L"Type a command", -1, &q, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        } else {
            const std::wstring wq(m_query.begin(), m_query.end());
            SetTextColor(dc, Theme::NV(Theme::Rgb::TEXT));
            DrawTextW(dc, (wq + L"│").c_str(), -1, &q,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        RECT hair = { 0, QueryH() - 1, rc.right, QueryH() };
        FillRect(dc, &hair, Theme::Brush(Theme::Rgb::BORDER));

        // The rows.
        const auto& all = palette::All();
        for (int r = 0; r < kRows; ++r) {
            const int h = m_top + r;
            if (h >= (int)m_hits.size()) break;
            const palette::Action& a = all[m_hits[h].index];
            RECT row = { 0, QueryH() + r * RowH(), rc.right, QueryH() + (r + 1) * RowH() };
            if (h == m_sel) {
                FillRect(dc, &row, Theme::Brush(Theme::Rgb::BORDER));
                RECT bar = { row.left, row.top, row.left + S(2), row.bottom };
                FillRect(dc, &bar, Theme::Brush(Theme::Rgb::ACCENT));
            }

            // The shortcut, in a hairline badge at the right.
            SelectObject(dc, m_fBadge);
            const std::wstring sc(a.shortcut, a.shortcut + strlen(a.shortcut));
            SIZE ts = {};
            GetTextExtentPoint32W(dc, sc.c_str(), (int)sc.size(), &ts);
            const int bw = ts.cx + S(12), bh = S(18);
            RECT badge = { row.right - pad - bw, row.top + (RowH() - bh) / 2,
                           row.right - pad, row.top + (RowH() - bh) / 2 + bh };
            FrameRect(dc, &badge, Theme::Brush(h == m_sel ? Theme::Rgb::TEXT_DIM : Theme::Rgb::BORDER));
            SetTextColor(dc, Theme::NV(Theme::Rgb::TEXT_DIM));
            DrawTextW(dc, sc.c_str(), (int)sc.size(), &badge,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // The label, and for a guarded action a note of what it moves.
            SelectObject(dc, m_fRow);
            RECT lab = { pad, row.top, badge.left - S(10), row.bottom };
            const std::wstring wl(a.label, a.label + strlen(a.label));
            SetTextColor(dc, Theme::NV(Theme::Rgb::TEXT));
            DrawTextW(dc, wl.c_str(), (int)wl.size(), &lab,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            if (a.guarded) {
                SIZE ls = {};
                GetTextExtentPoint32W(dc, wl.c_str(), (int)wl.size(), &ls);
                RECT note = { lab.left + ls.cx + S(10), row.top, lab.right, row.bottom };
                if (note.right - note.left > S(40)) {
                    SelectObject(dc, m_fBadge);
                    SetTextColor(dc, Theme::NV(Theme::Rgb::TEXT_DIM));
                    DrawTextW(dc, L"MOVES HARDWARE", -1, &note,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                }
            }
        }
        if (m_hits.empty()) {
            SelectObject(dc, m_fRow);
            RECT none = { pad, QueryH(), rc.right - pad, QueryH() + RowH() };
            SetTextColor(dc, Theme::NV(Theme::Rgb::TEXT_DIM));
            DrawTextW(dc, L"Nothing matches", -1, &none, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        // The footer: the keys, or what a second Enter will do. Caution
        // is said in words and in the caution token, never in hue alone.
        RECT foot = { 0, rc.bottom - FootH(), rc.right, rc.bottom };
        RECT fl = { foot.left, foot.top, foot.right, foot.top + 1 };
        FillRect(dc, &fl, Theme::Brush(Theme::Rgb::BORDER));
        SelectObject(dc, m_fFoot);
        RECT ft = { pad, foot.top, foot.right - pad, foot.bottom };
        if (m_armed >= 0) {
            const std::string l = std::string("Enter again to run: ") + all[m_armed].label;
            const std::wstring wl(l.begin(), l.end());
            SetTextColor(dc, Theme::NV(Theme::Rgb::AMBER));
            DrawTextW(dc, wl.c_str(), -1, &ft, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else {
            SetTextColor(dc, Theme::NV(Theme::Rgb::TEXT_DIM));
            DrawTextW(dc, L"↑↓ choose    Enter run    Esc close", -1, &ft,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        FrameRect(dc, &rc, Theme::Brush(Theme::Rgb::BORDER));
        SelectObject(dc, oldF);
        BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBmp);
        DeleteObject(bmp);
        DeleteDC(dc);
    }

    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        CommandPalette* self = nullptr;
        if (m == WM_NCCREATE) {
            self = static_cast<CommandPalette*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        } else {
            self = reinterpret_cast<CommandPalette*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(h, m, w, l);

        switch (m) {
            case WM_GETDLGCODE:
                // Every key is the palette's; see the header.
                return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
            case WM_ACTIVATE:
                if (LOWORD(w) == WA_INACTIVE) ShowWindow(h, SW_HIDE);
                return 0;
            case WM_KEYDOWN:
                switch (w) {
                    case VK_ESCAPE: self->Close(); return 0;
                    case VK_RETURN: self->Activate(); return 0;
                    case VK_UP:     self->Move(-1); return 0;
                    case VK_DOWN:   self->Move(1); return 0;
                    case VK_PRIOR:  self->Move(-kRows); return 0;
                    case VK_NEXT:   self->Move(kRows); return 0;
                    case VK_BACK:
                        if (!self->m_query.empty()) { self->m_query.pop_back(); self->Refilter(); }
                        return 0;
                    case 'K':
                        if (GetKeyState(VK_CONTROL) & 0x8000) { self->Close(); return 0; }
                        break;
                }
                return 0;
            case WM_CHAR:
                // Printable ASCII only: the labels are ASCII, and Ctrl
                // chords arrive here as control characters.
                if (w >= 0x20 && w < 0x7F && self->m_query.size() < 48) {
                    self->m_query += (char)w;
                    self->Refilter();
                }
                return 0;
            case WM_LBUTTONDOWN: {
                const int y = (short)HIWORD(l) - self->QueryH();
                if (y >= 0) {
                    const int hit = self->m_top + y / self->RowH();
                    if (hit < (int)self->m_hits.size()) {
                        if (hit != self->m_sel) { self->m_sel = hit; self->m_armed = -1; }
                        self->Activate();
                    }
                }
                return 0;
            }
            case WM_MOUSEWHEEL:
                self->Move(GET_WHEEL_DELTA_WPARAM(w) > 0 ? -1 : 1);
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC dc = BeginPaint(h, &ps);
                self->Paint(dc);
                EndPaint(h, &ps);
                return 0;
            }
            case WM_NCDESTROY:
                SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                self->m_hwnd = nullptr;
                break;
        }
        return DefWindowProcW(h, m, w, l);
    }
};

} // namespace raduk::gui
