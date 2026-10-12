// ============================================================
// ControlPanel.cpp — Persistent floating panel for RadukCapture
// Condensed layout: fits all sections (VIEW/PROFILE/CAMERA/
// FOCUSER/MOUNT/CAPTURE) in ~680px without scrolling.
// Two-column camera sliders, tighter row heights throughout.
// ============================================================

#include "ControlPanel.h"
#include "Theme.h"
#include "core/RigConfig.h"
#include <windowsx.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <commctrl.h>
#include <shlobj.h>
#include <uxtheme.h>

#pragma comment(lib, "uxtheme.lib")

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

static constexpr wchar_t PANEL_CLASS_NAME[] = L"RadukControlPanel";

namespace raduk::gui {

ControlPanel::~ControlPanel() {
    if (m_panelHwnd) { DestroyWindow(m_panelHwnd); m_panelHwnd = nullptr; }
}

// ── Panel WndProc ──────────────────────────────────────────────

LRESULT CALLBACK ControlPanel::PanelWndProc(HWND hwnd, UINT msg,
                                              WPARAM wParam, LPARAM lParam) {
    ControlPanel* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = static_cast<ControlPanel*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<ControlPanel*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProc(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_MOUSEACTIVATE:
            // Don't activate the panel window (keeps main window focused for
            // keyboard shortcuts), but DO allow the click to pass through to
            // child controls so buttons visually depress and sliders drag.
            return MA_NOACTIVATE;

        case WM_PARENTNOTIFY:
            // When a child edit box is clicked, explicitly give it focus
            if (LOWORD(wParam) == WM_LBUTTONDOWN) {
                HWND child = (HWND)lParam;
                if (child) {
                    wchar_t cls[32];
                    GetClassNameW(child, cls, 32);
                    if (wcscmp(cls, L"Edit") == 0) {
                        SetForegroundWindow(hwnd);
                        SetFocus(child);
                    }
                }
            }
            break;

        // Phase 71 (review): the arrow-forwarding that used to live here
        // is gone. Phase 60 routes keys from the message pump before
        // dispatch, so arrows destined for this panel are consumed and
        // this proc never sees them -- the forward had become
        // unreachable. Left in place it would have been a second path
        // to the jog handler waiting to double-fire the moment the
        // routing rules changed.

        case WM_SIZE: {
            if (!self || !self->m_panelHwnd) break;
            // Guard: if RebuildControls hasn't run yet, m_contentBottom is 0
            // and the scroll calc would be nonsense. Skip until first build done.
            if (self->m_contentBottom < 1) break;
            int panelH = HIWORD(lParam);
            if (panelH < 1) break;
            int totalH = self->m_contentBottom + self->GdiStripHeight() + 8;
            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask  = SIF_RANGE | SIF_PAGE;
            si.nMin   = 0;
            si.nMax   = totalH;
            si.nPage  = (UINT)panelH;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            ShowScrollBar(hwnd, SB_VERT, totalH > panelH);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }

        case WM_VSCROLL: {
            if (!self) break;
            SCROLLINFO si = {}; si.cbSize = sizeof(si); si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            int oldPos = si.nPos;
            switch (LOWORD(wParam)) {
                case SB_TOP:         si.nPos = si.nMin; break;
                case SB_BOTTOM:      si.nPos = si.nMax; break;
                case SB_LINEUP:      si.nPos -= 20; break;
                case SB_LINEDOWN:    si.nPos += 20; break;
                case SB_PAGEUP:      si.nPos -= si.nPage; break;
                case SB_PAGEDOWN:    si.nPos += si.nPage; break;
                case SB_THUMBTRACK:  si.nPos = si.nTrackPos; break;
                default: break;
            }
            { int lo = si.nMin, hi = si.nMax - (int)si.nPage + 1; si.nPos = si.nPos < lo ? lo : si.nPos > hi ? hi : si.nPos; }
            si.fMask = SIF_POS;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            int delta = oldPos - si.nPos;
            if (delta != 0) {
                self->m_scrollY = si.nPos;
                ScrollWindowEx(hwnd, 0, delta, nullptr, nullptr, nullptr, nullptr,
                               SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            if (!self) break;
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            SCROLLINFO si = {}; si.cbSize = sizeof(si); si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            int oldPos = si.nPos;
            si.nPos -= delta / 3;  // 120 units per notch → 40px scroll
            { int lo = si.nMin, hi = si.nMax - (int)si.nPage + 1; si.nPos = si.nPos < lo ? lo : si.nPos > hi ? hi : si.nPos; }
            si.fMask = SIF_POS;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            int scrollDelta = oldPos - si.nPos;
            if (scrollDelta != 0) {
                self->m_scrollY = si.nPos;
                ScrollWindowEx(hwnd, 0, scrollDelta, nullptr, nullptr, nullptr, nullptr,
                               SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
            }
            return 0;
        }

        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (!dis) break;

            // ── Section header owner-draw ──────────────────────
            if (self->m_sectionHeaders.count(dis->hwndItem)) {
                HDC  hdc = dis->hDC;
                RECT r   = dis->rcItem;
                int  x   = r.left, y = r.top;
                int  w   = r.right - r.left;
                int  h   = r.bottom - r.top;

                // Get title from window text
                wchar_t wtitle[64] = {};
                GetWindowTextW(dis->hwndItem, wtitle, 64);
                char title[64] = {};
                WideCharToMultiByte(CP_ACP, 0, wtitle, -1, title, 64, nullptr, nullptr);

                // Determine accent color by section
                auto sit = self->m_ctrlSection.find(dis->hwndItem);
                COLORREF accent = RGB(0, 229, 255); // default cyan
                bool isCollapsible = false;
                bool collapsed = false;
                if (sit != self->m_ctrlSection.end()) {
                    switch (sit->second) {
                        case Section::Image:
                            accent = RGB(0, 229, 255); isCollapsible = true;
                            collapsed = self->IsCollapsed(Section::Image); break;
                        case Section::Camera:
                            accent = RGB(0, 229, 255); isCollapsible = true;
                            collapsed = self->IsCollapsed(Section::Camera); break;
                        case Section::EAF:
                            accent = RGB(255, 185, 0); isCollapsible = true;
                            collapsed = self->IsCollapsed(Section::EAF); break;
                        case Section::Mount:
                            accent = RGB(77, 255, 145); isCollapsible = true;
                            collapsed = self->IsCollapsed(Section::Mount); break;
                        case Section::Capture:
                            accent = RGB(255, 59, 92); isCollapsible = true;
                            collapsed = self->IsCollapsed(Section::Capture); break;
                        case Section::Notes:
                            accent = RGB(140, 175, 190); isCollapsible = true;
                            collapsed = self->IsCollapsed(Section::Notes); break;
                        default:
                            accent = RGB(0, 229, 255); break;
                    }
                }

                // Background
                {
                    HBRUSH bg = CreateSolidBrush(RGB(10, 21, 32));
                    FillRect(hdc, &r, bg);
                    DeleteObject(bg);
                }

                // Colored circle
                const int circ = h - 6;
                const int cx = x + 4 + circ/2;
                const int cy = y + h/2;
                {
                    HBRUSH cb = CreateSolidBrush(accent);
                    HPEN cp = CreatePen(PS_SOLID, 1,
                        RGB((int)(GetRValue(accent)*0.55f),
                            (int)(GetGValue(accent)*0.55f),
                            (int)(GetBValue(accent)*0.55f)));
                    HPEN op  = (HPEN)SelectObject(hdc, cp);
                    HBRUSH ob = (HBRUSH)SelectObject(hdc, cb);
                    Ellipse(hdc, cx-circ/2, cy-circ/2, cx+circ/2+1, cy+circ/2+1);
                    SelectObject(hdc, op); SelectObject(hdc, ob);
                    DeleteObject(cb); DeleteObject(cp);
                }
                // Letter in circle
                if (title[0]) {
                    char letter[2] = { title[0], 0 };
                    HFONT fi = CreateFontA(-9, 0,0,0, FW_BOLD, 0,0,0,
                        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH|FF_DONTCARE, "Barlow Condensed");
                    HFONT oldf = fi ? (HFONT)SelectObject(hdc, fi) : nullptr;
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, RGB(4, 8, 15));
                    RECT lr = {cx-circ/2, cy-circ/2, cx+circ/2+1, cy+circ/2+1};
                    DrawTextA(hdc, letter, 1, &lr, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                    if (oldf) SelectObject(hdc, oldf);
                    if (fi)   DeleteObject(fi);
                }
                // Title text
                {
                    HFONT ft = CreateFontA(-12, 0,0,0, FW_BOLD, 0,0,0,
                        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH|FF_DONTCARE, "Barlow Condensed");
                    HFONT oldf = ft ? (HFONT)SelectObject(hdc, ft) : nullptr;
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, accent);
                    RECT tr = {x + 4 + circ + 6, y, x+w-20, y+h};
                    DrawTextA(hdc, title, -1, &tr, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
                    if (oldf) SelectObject(hdc, oldf);
                    if (ft)   DeleteObject(ft);
                }
                // Chevron if collapsible
                if (isCollapsible) {
                    HFONT fc = CreateFontW(-11, 0,0,0, FW_NORMAL, 0,0,0,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH|FF_DONTCARE, L"Barlow Condensed");
                    HFONT oldf = fc ? (HFONT)SelectObject(hdc, fc) : nullptr;
                    SetTextColor(hdc, accent);
                    RECT chevR = {x+w-18, y, x+w-2, y+h};
                    const wchar_t* chev = collapsed ? L"\u25BA" : L"\u25BC";
                    DrawTextW(hdc, chev, -1, &chevR, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                    if (oldf) SelectObject(hdc, oldf);
                    if (fc)   DeleteObject(fc);
                }
                // Accent bottom line
                {
                    COLORREF lc = RGB((int)(GetRValue(accent)*0.35f),
                                      (int)(GetGValue(accent)*0.35f),
                                      (int)(GetBValue(accent)*0.35f));
                    HPEN lp = CreatePen(PS_SOLID, 1, lc);
                    HPEN op = (HPEN)SelectObject(hdc, lp);
                    MoveToEx(hdc, x, y+h-1, nullptr);
                    LineTo(hdc, x+w, y+h-1);
                    SelectObject(hdc, op);
                    DeleteObject(lp);
                }
                return TRUE;
            }

            // PLANET LOCK button
            if (dis->hwndItem == self->m_btnSync) {
                HDC hdc      = dis->hDC;
                RECT r       = dis->rcItem;
                bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                bool locked  = self->m_planetLock;

                // Elite Raduk: red glow when locked, deep navy otherwise
                COLORREF bg = locked
                    ? (pressed ? RGB(200, 30, 50)  : RGB(160, 15, 35))
                    : (pressed ? RGB(14, 30, 48)   : RGB(10, 21, 32));
                HBRUSH hbg = CreateSolidBrush(bg);
                FillRect(hdc, &r, hbg);
                DeleteObject(hbg);

                HPEN pen = CreatePen(PS_SOLID, 1,
                    locked ? RGB(255, 59, 92) : RGB(0, 90, 110));
                HPEN oldPen = (HPEN)SelectObject(hdc, pen);
                HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, r.left, r.top, r.right, r.bottom);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBrush);
                DeleteObject(pen);

                const wchar_t* label = locked ? L"\u25CF TRACKING" : L"PLANET LOCK";
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, locked ? RGB(255, 59, 92) : RGB(0, 150, 170));
                DrawTextW(hdc, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                if (dis->itemState & ODS_FOCUS) DrawFocusRect(hdc, &r);
                return TRUE;
            }

            // AUTO FOCUS / ABORT AF button — cyan when idle, amber when running
            if (dis->hwndItem == self->m_btnAutofocus) {
                HDC hdc      = dis->hDC;
                RECT r       = dis->rcItem;
                bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                bool running = self->m_afRunning;

                COLORREF bg = running
                    ? (pressed ? RGB(180, 110, 0)  : RGB(140,  80,  0))
                    : (pressed ? RGB(0,  80, 100)  : RGB(0,   50,  70));
                HBRUSH hbg = CreateSolidBrush(bg);
                FillRect(hdc, &r, hbg);
                DeleteObject(hbg);

                HPEN pen = CreatePen(PS_SOLID, 1,
                    running ? RGB(255, 185, 0) : RGB(0, 229, 255));
                HPEN oldPen = (HPEN)SelectObject(hdc, pen);
                HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, r.left, r.top, r.right, r.bottom);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBrush);
                DeleteObject(pen);

                const wchar_t* label = running ? L"\u25A0  ABORT AF" : L"\u2605  AUTO FOCUS";
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, running ? RGB(255, 185, 0) : RGB(0, 229, 255));
                HFONT f = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Barlow Condensed");
                HFONT old = f ? (HFONT)SelectObject(hdc, f) : nullptr;
                DrawTextW(hdc, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                if (old) SelectObject(hdc, old);
                if (f) DeleteObject(f);
                if (dis->itemState & ODS_FOCUS) DrawFocusRect(hdc, &r);
                return TRUE;
            }

            // AE AUTO button — dim=off, cyan=advisory, green=auto
            if (dis->hwndItem == self->m_btnAEAuto) {
                HDC  hdc     = dis->hDC;
                RECT r       = dis->rcItem;
                bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                int  state   = self->m_aeState;  // 0=off, 1=advisory, 2=auto

                COLORREF bg = (state == 2) ? (pressed ? RGB(0, 100, 30) : RGB(0, 70, 20))
                            : (state == 1) ? (pressed ? RGB(0, 60, 80)  : RGB(0, 40, 60))
                            :               (pressed ? RGB(25, 30, 40)  : RGB(15, 20, 30));
                COLORREF fg = (state == 2) ? RGB(77, 255, 145)
                            : (state == 1) ? RGB(0, 229, 255)
                            :               RGB(80, 100, 110);
                HBRUSH hbg = CreateSolidBrush(bg);
                FillRect(hdc, &r, hbg);
                DeleteObject(hbg);
                HPEN pen = CreatePen(PS_SOLID, 1, fg);
                HPEN oldPen = (HPEN)SelectObject(hdc, pen);
                HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, r.left, r.top, r.right, r.bottom);
                SelectObject(hdc, oldPen); SelectObject(hdc, oldBrush);
                DeleteObject(pen);
                const wchar_t* lbl = (state == 2) ? L"AE AUTO"
                                   : (state == 1) ? L"AE ADV" : L"AE OFF";
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, fg);
                HFONT f = CreateFontW(-12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Barlow Condensed");
                HFONT old = f ? (HFONT)SelectObject(hdc, f) : nullptr;
                DrawTextW(hdc, lbl, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                if (old) SelectObject(hdc, old);
                if (f) DeleteObject(f);
                return TRUE;
            }

            // RECORD / STOP button — red=recording, amber=paused, cyan=idle
            if (dis->hwndItem == self->m_btnRecord) {
                HDC  hdc     = dis->hDC;
                RECT r       = dis->rcItem;
                bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                bool rec     = self->m_status.recording;
                bool paused  = self->m_recordPaused;

                // Background
                COLORREF bg = paused ? (pressed ? RGB(120, 70,  0)  : RGB(90,  50,  0))
                            : rec    ? (pressed ? RGB(220, 20,  40) : RGB(180, 10,  25))
                            :          (pressed ? RGB(0,   70,  90) : RGB(0,   40,  60));
                HBRUSH hbg = CreateSolidBrush(bg);
                FillRect(hdc, &r, hbg);
                DeleteObject(hbg);

                // Border
                COLORREF borderCol = paused ? RGB(255, 160, 0)
                                   : rec    ? RGB(255,  59, 92)
                                   :          RGB(0,   229, 255);
                HPEN pen = CreatePen(PS_SOLID, 2, borderCol);
                HPEN oldPen = (HPEN)SelectObject(hdc, pen);
                HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, r.left, r.top, r.right, r.bottom);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBr);
                DeleteObject(pen);

                // Label
                HFONT f = CreateFontW(-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Barlow Condensed");
                HFONT oldf = f ? (HFONT)SelectObject(hdc, f) : nullptr;
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, borderCol);
                const wchar_t* label = paused        ? L"\u23F8  PAUSED"
                                     : rec           ? L"\u25A0  STOP RECORDING"
                                     :                 L"\u25BA  RECORD";
                DrawTextW(hdc, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                if (oldf) SelectObject(hdc, oldf);
                if (f)    DeleteObject(f);
                if (dis->itemState & ODS_FOCUS) DrawFocusRect(hdc, &r);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            HDC  hdc  = (HDC)wParam;
            HWND ctrl = (HWND)lParam;

            auto it = self->m_ctrlSection.find(ctrl);
            bool isHeader = self->m_sectionHeaders.count(ctrl) > 0 ||
                            (it != self->m_ctrlSection.end() &&
                             it->second == Section::Header);

            if (isHeader) {
                // Elite Raduk section header: deep navy + cyan text
                SetBkMode(hdc, OPAQUE);
                SetBkColor(hdc, RGB(10, 21, 32));
                SetTextColor(hdc, RGB(0, 229, 255));
                static HBRUSH hHdrBg = CreateSolidBrush(RGB(10, 21, 32));
                return (LRESULT)hHdrBg;
            }

            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, RGB(7, 13, 22));  // --bg-deep

            // Focuser position — big Space Mono amber
            if (ctrl == self->m_lblFocusPos) {
                static HFONT hBigFont = CreateFontW(
                    -22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Space Mono");
                if (hBigFont) SelectObject(hdc, hBigFont);
                SetBkColor(hdc, RGB(7, 13, 22));
                SetBkMode(hdc, OPAQUE);
                SetTextColor(hdc, RGB(255, 185, 0));  // --amber
                HWND hw = self->m_lblFocusPos;
                RECT r; GetClientRect(hw, &r);
                SetWindowLongPtr(hw, GWL_STYLE,
                    GetWindowLongPtr(hw, GWL_STYLE) & ~SS_RIGHT | SS_CENTER);
                static HBRUSH hFocBg = CreateSolidBrush(RGB(7, 13, 22));
                return (LRESULT)hFocBg;
            }

            if (ctrl == self->m_lblMountConnected) {
                // Green when connected, dim otherwise
                bool connected = self->m_mount && self->m_mount->IsConnected();
                SetTextColor(hdc, connected ? RGB(77, 255, 145) : RGB(140, 175, 190));
                static HBRUSH hConBg = CreateSolidBrush(RGB(7, 13, 22));
                return (LRESULT)hConBg;
            }
            if (ctrl == self->m_lblMountControl && self->m_planetLock) {
                SetTextColor(hdc, RGB(255, 59, 92));  // --red
            } else if (ctrl == self->m_lblSkyMapLock) {
                // Bright green when locked, dim when empty
                SetTextColor(hdc, self->m_skyMapLocked
                    ? RGB(77, 255, 145)    // --green
                    : RGB(140, 175, 190)); // --dim
                static HBRUSH hLockBg = CreateSolidBrush(RGB(7, 13, 22));
                return (LRESULT)hLockBg;
            } else if (ctrl == self->m_lblSkyMapTarget) {
                // Target name in cyan beneath PLANET LOCK
                SetTextColor(hdc, self->m_skyMapLocked
                    ? RGB(0, 229, 255)     // --cyan
                    : RGB(140, 175, 190)); // --dim
                static HBRUSH hTgtBg = CreateSolidBrush(RGB(7, 13, 22));
                return (LRESULT)hTgtBg;
            } else if (it != self->m_ctrlSection.end()) {
                switch (it->second) {
                    case Section::Camera:
                        SetTextColor(hdc, RGB(0,   229, 255)); break;  // --cyan
                    case Section::EAF:
                        SetTextColor(hdc, RGB(255, 185,   0)); break;  // --amber
                    case Section::Mount:
                        SetTextColor(hdc, RGB( 77, 255, 145)); break;  // --green
                    default:
                        SetTextColor(hdc, RGB(140, 175, 190)); break;  // --dim
                }
            } else {
                SetTextColor(hdc, RGB(140, 175, 190));  // --dim
            }
            static HBRUSH hPanelBg = CreateSolidBrush(RGB(7, 13, 22));
            return (LRESULT)hPanelBg;
        }
        // ── Elite Raduk buttons ────────────────────────────────
        case WM_CTLCOLORBTN: {
            HDC hdc  = (HDC)wParam;
            HWND btn = (HWND)lParam;
            // Radio buttons and checkboxes need TRANSPARENT so the glyph
            // background matches the panel, not a solid box
            LONG style = GetWindowLongW(btn, GWL_STYLE) & 0x0F;
            bool isCheck = (style == BS_AUTOCHECKBOX || style == BS_CHECKBOX ||
                            style == BS_AUTORADIOBUTTON || style == BS_RADIOBUTTON);
            if (isCheck) {
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(140, 175, 190));  // --dim
                static HBRUSH hCheckBg = CreateSolidBrush(RGB(7, 13, 22));
                return (LRESULT)hCheckBg;
            }
            // Plain push buttons
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc,   RGB(10, 21, 32));
            SetTextColor(hdc, RGB(140, 175, 190));
            static HBRUSH hBtnBg = CreateSolidBrush(RGB(10, 21, 32));
            return (LRESULT)hBtnBg;
        }
        // ── Edit boxes ─────────────────────────────────────────
        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc,   RGB(4, 8, 15));
            SetTextColor(hdc, RGB(200, 232, 240));
            static HBRUSH hEditBg = CreateSolidBrush(RGB(4, 8, 15));
            return (LRESULT)hEditBg;
        }
        // ── Combo dropdown ─────────────────────────────────────
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc,   RGB(10, 21, 32));
            SetTextColor(hdc, RGB(200, 232, 240));
            static HBRUSH hListBg = CreateSolidBrush(RGB(10, 21, 32));
            return (LRESULT)hListBg;
        }
        // ── Scrollbar / slider track ───────────────────────────
        case WM_CTLCOLORSCROLLBAR: {
            // Trackbar (slider) background — deep navy-black track
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, RGB(4, 8, 15));
            static HBRUSH hScrBg = CreateSolidBrush(RGB(14, 29, 46));
            return (LRESULT)hScrBg;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT cr; GetClientRect(hwnd, &cr);
            if (self->m_contentBottom > 0) {
                int screenBottom = self->m_contentBottom - self->m_scrollY;
                HRGN clipRgn = CreateRectRgn(0, screenBottom, cr.right, cr.bottom);
                SelectClipRgn(hdc, clipRgn);
                DeleteObject(clipRgn);
            }
            self->PaintAll(hdc, cr);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            // Check if click landed on a GDI-painted section header
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            auto hitGdi = [&](const RECT& r, Section s) -> bool {
                if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                    self->ToggleCollapse(s);
                    return true;
                }
                return false;
            };
            if (hitGdi(self->m_gdiHdrStatus,      Section::Status))      return 0;
            if (hitGdi(self->m_gdiHdrHistogram,   Section::Histogram))   return 0;
            if (hitGdi(self->m_gdiHdrTemperature, Section::Temperature)) return 0;
            break;
        }
        case WM_COMMAND:
            if (self->HandleCommand(wParam, lParam)) return 0;
            break;
        case WM_HSCROLL:
            if (self->HandleScroll(wParam, lParam)) return 0;
            break;
        case WM_CLOSE:
            self->m_docked = false;
            if (self->m_btnDock) SetWindowTextW(self->m_btnDock, L"DOCK");
            ShowWindow(hwnd, SW_SHOW);
            return 0;
        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wParam;
            RECT cr; GetClientRect(hwnd, &cr);
            static HBRUSH hBg = CreateSolidBrush(RGB(7, 13, 22));  // --bg-deep
            // WS_CLIPCHILDREN on the panel excludes child control rects from
            // this fill, so FillRect over the whole client is safe and won't
            // flash controls. Previously we only filled below m_contentBottom
            // which left ghost images of controls when the panel was resized.
            FillRect(hdc, &cr, hBg);
            return 1;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ── Create ────────────────────────────────────────────────────

bool ControlPanel::Create(HWND mainHwnd,
                           raduk::core::ASICamera*    camera,
                           raduk::core::EAFFocuser*   focuser,
                           raduk::core::IMount*       mount) {
    m_mainHwnd = mainHwnd;
    m_camera   = camera;
    m_focuser  = focuser;
    m_mount    = mount;
    m_docked   = true;  // always docked as a child window

    // Every collapsible section starts collapsed, so the panel opens
    // as a short list of headers rather than a long scroll. Must run
    // before the first RebuildControls, which reads this map to decide
    // which controls to create at all.
    InitCollapsed();

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = PanelWndProc;
    wc.hInstance     = GetModuleHandle(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(7, 13, 22));
    wc.lpszClassName = PANEL_CLASS_NAME;
    if (!RegisterClassExW(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            printf("[ControlPanel] RegisterClassEx failed: %lu\n", err);
            return false;
        }
    }

    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icex);

    // Get main window client area to position panel flush right
    RECT mainClient;
    GetClientRect(mainHwnd, &mainClient);
    int mainH = mainClient.bottom;
    int panelX = mainClient.right - PANEL_WIDTH;

    // Create as WS_CHILD — no title bar, no caption, moves with parent automatically
    m_panelHwnd = CreateWindowExW(
        0,
        PANEL_CLASS_NAME, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VSCROLL,
        panelX, 0, PANEL_WIDTH, mainH,
        mainHwnd, nullptr, GetModuleHandle(nullptr), this);

    if (!m_panelHwnd) {
        printf("[ControlPanel] CreateWindowExW failed: %lu\n", GetLastError());
        return false;
    }

    // Build all child controls at panel client width
    RebuildControls(PANEL_WIDTH);

    // Draw a 1px separator line on the left edge of the panel
    // (handled in WM_PAINT via PaintAll — nothing extra needed here)

    UpdatePlanetLockUI();

    // Barlow Condensed 11pt — Elite Raduk body font
    HFONT hPanelFont = CreateFontW(
        -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Barlow Condensed");
    if (hPanelFont) {
        SendMessage(m_panelHwnd, WM_SETFONT, (WPARAM)hPanelFont, TRUE);
        EnumChildWindows(m_panelHwnd,
            [](HWND child, LPARAM lp) -> BOOL {
                SendMessage(child, WM_SETFONT, (WPARAM)lp, FALSE);
                return TRUE;
            },
            (LPARAM)hPanelFont);
    }

    // Bold Barlow Condensed for section header statics
    HFONT hHdrFont = CreateFontW(
        -12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Barlow Condensed");
    if (hHdrFont) {
        for (HWND hdr : m_sectionHeaders) {
            SendMessage(hdr, WM_SETFONT, (WPARAM)hHdrFont, FALSE);
        }
        for (auto& kv : m_ctrlSection) {
            if (kv.second == Section::Header)
                SendMessage(kv.first, WM_SETFONT, (WPARAM)hHdrFont, FALSE);
        }
    }

    // Strip UxTheme from all buttons and edit boxes so they respond
    // to WM_CTLCOLORxxx messages and show our dark background.
    // Without this, themed controls ignore WM_CTLCOLORBTN/EDIT entirely.
    EnumChildWindows(m_panelHwnd,
        [](HWND child, LPARAM) -> BOOL {
            wchar_t cls[32] = {};
            GetClassNameW(child, cls, 32);
            if (wcscmp(cls, L"Button") == 0 ||
                wcscmp(cls, L"Edit")   == 0 ||
                wcscmp(cls, L"ComboBox") == 0)
            {
                SetWindowTheme(child, L"", L"");
            }
            return TRUE;
        },
        0);

    ShowWindow(m_panelHwnd, SW_SHOW);
    return true;
}

// ── Dock / Undock ──────────────────────────────────────────────
// Panel is now a WS_CHILD — it always moves and resizes with the main window.
// SnapToMainWindow repositions/resizes it to fill the right side of the client area.

void ControlPanel::SnapToMainWindow() {
    if (!m_panelHwnd || !m_mainHwnd) return;
    RECT r; GetClientRect(m_mainHwnd, &r);
    int mainH = r.bottom;
    int panelX = r.right - PANEL_WIDTH;
    SetWindowPos(m_panelHwnd, nullptr,
        panelX, 0, PANEL_WIDTH, mainH,
        SWP_NOZORDER | SWP_NOACTIVATE);

    // Refresh scroll range now that panel height may have changed
    int totalH = m_contentBottom + GdiStripHeight() + 8;
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE;
    si.nMin   = 0;
    si.nMax   = totalH;
    si.nPage  = mainH;
    SetScrollInfo(m_panelHwnd, SB_VERT, &si, TRUE);
    ShowScrollBar(m_panelHwnd, SB_VERT, totalH > mainH);
}

void ControlPanel::OnMainWindowMoved() { SnapToMainWindow(); }

void ControlPanel::ToggleDock() {
    // Child window is always docked — this is a no-op kept for API compatibility.
    // The UNDOCK PANEL button is hidden in the new layout.
}

// ── Label updates ──────────────────────────────────────────────

void ControlPanel::SetZoomLabel(const wchar_t* label) {
    if (m_btnZoom) {
        wchar_t buf[32]; swprintf(buf, 32, L"ZOOM: %s", label);
        SetWindowTextW(m_btnZoom, buf);
    }
}

void ControlPanel::SetDockLabel(bool docked) {
    if (m_btnDock) SetWindowTextW(m_btnDock, docked ? L"UNDOCK" : L"DOCK");
}

void ControlPanel::SetProfileNames(const std::vector<std::string>& names) {
    m_cachedProfileNames = names;   // cache for rebuild
    if (!m_comboProfile) return;
    SendMessageA(m_comboProfile, CB_RESETCONTENT, 0, 0);
    for (const auto& n : names)
        SendMessageA(m_comboProfile, CB_ADDSTRING, 0, (LPARAM)n.c_str());
    SendMessage(m_comboProfile, CB_SETCURSEL, 0, 0);
}

void ControlPanel::SetActiveProfile(int index) {
    m_cachedProfileIndex = index;   // cache for rebuild
    if (m_comboProfile)
        SendMessage(m_comboProfile, CB_SETCURSEL, (WPARAM)index, 0);
}

// ── Emergency stop ─────────────────────────────────────────────

void ControlPanel::EmergencyStop() {
    printf("[SAFETY] EMERGENCY STOP\n");
    if (m_mount && m_mount->IsConnected()) {
        m_mount->StopSlew(0); m_mount->StopSlew(1);
    }
    if (m_focuser && m_focuser->IsOpen()) m_focuser->Stop();
    m_jogHeld = false; m_activeJogAxis = -1; ReleaseCapture();
    // Release RC control back to CPWI on emergency stop
    if (m_rcInControl) SetRCControl(false);
}

// ── Jog button subclassing ─────────────────────────────────────

void ControlPanel::SubclassJogButton(HWND btn) {
    SetWindowSubclass(btn, JogButtonProc, 0, reinterpret_cast<DWORD_PTR>(this));
}

LRESULT CALLBACK ControlPanel::JogButtonProc(HWND hwnd, UINT msg,
                                              WPARAM wParam, LPARAM lParam,
                                              UINT_PTR uIdSubclass,
                                              DWORD_PTR dwRefData) {
    ControlPanel* self = reinterpret_cast<ControlPanel*>(dwRefData);
    switch (msg) {
        case WM_LBUTTONDOWN:
            // Let button process normally first (visual press), then start jog
            DefSubclassProc(hwnd, msg, wParam, lParam);
            self->OnJogStart(hwnd);
            return 0;
        case WM_LBUTTONUP:
            self->OnJogStop();
            DefSubclassProc(hwnd, msg, wParam, lParam);
            return 0;
        case WM_CAPTURECHANGED:
            // Failsafe: capture stolen mid-press (Alt-Tab, popup) means
            // WM_LBUTTONUP never arrives — stop the jog before it runs away.
            if (self->m_jogHeld) self->OnJogStop();
            break;
        case WM_MOUSELEAVE:
            if (self->m_jogHeld) self->OnJogStop();
            break;
        case WM_DESTROY:
            RemoveWindowSubclass(hwnd, JogButtonProc, uIdSubclass);
            break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ── HeaderClickProc ─────────────────────────────────────────
// Subclass proc on collapsible section header labels.
// Left-click toggles the section's collapsed state and rebuilds.

LRESULT CALLBACK ControlPanel::HeaderClickProc(HWND hwnd, UINT msg, WPARAM wParam,
                                                 LPARAM lParam, UINT_PTR /*id*/,
                                                 DWORD_PTR data) {
    auto* self = reinterpret_cast<ControlPanel*>(data);
    if (msg == WM_LBUTTONDOWN && self) {
        auto it = self->m_headerSection.find(hwnd);
        if (it != self->m_headerSection.end()) {
            self->ToggleCollapse(it->second);
            return 0;
        }
    }
    // Change cursor to hand on hover to hint clickability
    if (msg == WM_SETCURSOR) {
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void ControlPanel::OnJogStart(HWND btn) {
    printf("[ControlPanel] OnJogStart: mount=%p connected=%s\n",
           (void*)m_mount,
           (m_mount && m_mount->IsConnected()) ? "yes" : "no");

    if (!m_mount || !m_mount->IsConnected()) return;

    if (m_jogHeld) OnJogStop();
    int axis = -1, dir = -1;
    if (btn == m_btnJogN) { axis=1; dir=0; }
    if (btn == m_btnJogS) { axis=1; dir=1; }
    if (btn == m_btnJogE) { axis=0; dir=0; }
    if (btn == m_btnJogW) { axis=0; dir=1; }
    printf("[ControlPanel] OnJogStart: axis=%d dir=%d rate=%d\n", axis, dir, m_jogRate);
    if (axis < 0) return;
    bool ok = m_mount->SlewFixed(axis, dir, m_jogRate);
    printf("[ControlPanel] SlewFixed returned: %s\n", ok ? "OK" : "FAILED");
    m_activeJogAxis = axis; m_jogHeld = true;
    if (m_onJogActivity) m_onJogActivity(true);
    SetCapture(btn);
}

void ControlPanel::OnJogStop() {
    if (!m_jogHeld) return;
    // Clear state BEFORE ReleaseCapture: ReleaseCapture synchronously sends
    // WM_CAPTURECHANGED, which re-enters here via the failsafe path.
    int axis = m_activeJogAxis;
    m_activeJogAxis = -1; m_jogHeld = false;
    // Stop unconditionally — a transient IsConnected() flicker mid-jog must
    // never strand a moving mount.
    if (m_mount && axis >= 0)
        m_mount->StopSlew(axis);
    if (m_onJogActivity) m_onJogActivity(false);
    ReleaseCapture();
}

// ── WM_COMMAND ─────────────────────────────────────────────────

bool ControlPanel::HandleCommand(WPARAM wParam, LPARAM lParam) {
    int id         = LOWORD(wParam);
    int notifyCode = HIWORD(wParam);

    if (id == ID_COMBO_PROFILE && notifyCode == CBN_SELCHANGE) return true;
    if (notifyCode != BN_CLICKED) return false;

    switch (id) {
        case ID_BTN_ZOOM:        if (m_onZoomCycle)        m_onZoomCycle();        return true;
        case ID_BTN_FULLSCREEN:  if (m_onFullscreenToggle) m_onFullscreenToggle(); return true;
        case ID_BTN_RGB_ALIGN:   if (m_onRGBAlign)  m_onRGBAlign();  return true;
        case ID_BTN_FX:          if (m_onFXCycle)       m_onFXCycle();       return true;
        case ID_BTN_NIGHT:       if (m_onNightVision)   m_onNightVision();   return true;
        case ID_BTN_CAL_DARK:    if (m_onCalDark)       m_onCalDark();       return true;
        case ID_BTN_CAL_FLAT:    if (m_onCalFlat)       m_onCalFlat();       return true;
        case ID_BTN_CAL_CLEAR:   if (m_onCalClear)      m_onCalClear();      return true;
        case ID_BTN_CHANNEL:     if (m_onChannelCycle)  m_onChannelCycle();  return true;
        case ID_BTN_DOCK:        ToggleDock(); if (m_onDockToggle) m_onDockToggle(); return true;

        case ID_BTN_RECORD:
            if (m_onRecordStart) m_onRecordStart();
            return true;

        case ID_BTN_BROWSE_DIR: {
            wchar_t path[MAX_PATH] = {};
            if (m_editOutputDir) GetWindowTextW(m_editOutputDir, path, MAX_PATH);
            BROWSEINFOW bi = {};
            bi.hwndOwner = m_panelHwnd;
            bi.lpszTitle = L"Select output directory for captures:";
            bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
            bi.lParam    = (LPARAM)path;
            bi.lpfn      = [](HWND hwnd, UINT msg2, LPARAM, LPARAM data) -> int {
                if (msg2 == BFFM_INITIALIZED && data)
                    SendMessage(hwnd, BFFM_SETSELECTION, TRUE, data);
                return 0;
            };
            PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t chosen[MAX_PATH] = {};
                if (SHGetPathFromIDListW(pidl, chosen) && m_editOutputDir)
                    SetWindowTextW(m_editOutputDir, chosen);
                CoTaskMemFree(pidl);
            }
            return true;
        }

        case ID_BTN_PROFILE_LOAD: {
            if (m_onProfileLoad && m_comboProfile) {
                int idx = (int)SendMessage(m_comboProfile, CB_GETCURSEL, 0, 0);
                if (idx != CB_ERR) { m_cachedProfileIndex = idx; m_onProfileLoad(idx); }
            }
            return true;
        }
        case ID_BTN_PROFILE_SAVE: {
            if (m_onProfileSave && m_comboProfile) {
                int idx = (int)SendMessage(m_comboProfile, CB_GETCURSEL, 0, 0);
                if (idx != CB_ERR) { m_cachedProfileIndex = idx; m_onProfileSave(idx); }
            }
            return true;
        }
        case ID_BTN_PROFILE_EDIT: {
            if (m_onProfileEdit && m_comboProfile) {
                int idx = (int)SendMessage(m_comboProfile, CB_GETCURSEL, 0, 0);
                if (idx != CB_ERR) { m_cachedProfileIndex = idx; m_onProfileEdit(idx); }
            }
            return true;
        }

        case ID_BTN_FOCUS_STOP:
            if (m_focuser && m_focuser->IsOpen()) m_focuser->Stop();
            return true;
        case ID_BTN_FOCUS_IN5000:  if (m_focuser) m_focuser->MoveRelative(-5000); return true;
        case ID_BTN_FOCUS_IN1000:  if (m_focuser) m_focuser->MoveRelative(-1000); return true;
        case ID_BTN_FOCUS_OUT1000: if (m_focuser) m_focuser->MoveRelative(1000);  return true;
        case ID_BTN_FOCUS_OUT5000: if (m_focuser) m_focuser->MoveRelative(5000);  return true;
        case ID_BTN_FOCUS_IN100:   if (m_focuser) m_focuser->MoveRelative(-100);  return true;
        case ID_BTN_FOCUS_IN10:    if (m_focuser) m_focuser->MoveRelative(-10);   return true;
        case ID_BTN_FOCUS_OUT10:   if (m_focuser) m_focuser->MoveRelative(10);    return true;
        case ID_BTN_FOCUS_OUT100:  if (m_focuser) m_focuser->MoveRelative(100);   return true;
        case ID_BTN_FOCUS_GOTO: {
            if (m_focuser && m_editFocusGoto) {
                wchar_t buf[32] = {};
                GetWindowTextW(m_editFocusGoto, buf, 32);
                int pos = _wtoi(buf);
                pos = (pos < 0) ? 0 : (pos > 600000) ? 600000 : pos;
                m_focuser->MoveTo(pos);
            }
            return true;
        }

        case ID_BTN_AUTOFOCUS:
            if (m_onAutofocus) m_onAutofocus(!m_afRunning);
            return true;

        case ID_BTN_AE_AUTO:
            // Cycle: off -> advisory -> auto -> off
            {
                int next = (m_aeState + 1) % 3;
                SetAEState(next);
                if (m_onAEToggle) m_onAEToggle(next > 0, next == 2);
            }
            return true;

        case ID_BTN_CABLES:
            if (m_onCablesDressed) m_onCablesDressed();
            return true;

        case ID_BTN_RIGSETUP:
            if (m_onRigSetup) m_onRigSetup();
            return true;

        case ID_BTN_READY:
            if (m_onReadinessCheck) m_onReadinessCheck();
            return true;

        case ID_BTN_MOUNT_CTRL:
            // Show and reposition floating mount controller to default position
            if (m_onMountCtrl) m_onMountCtrl();
            return true;

        // NOTE: these four buttons are never actually created - see
        // UpdateJogRateButtons, whose table skips null handles - so these
        // cases are unreachable today and the rate UI lives only in the
        // floating Mount Controller. That is precisely why the arrow keys
        // were stuck: they read m_jogRate, and nothing could change it.
        // SetJogRate below is the live path, called from the sync wiring.
        // Notify, so the floating Mount Controller follows. Until v1.2.0
        // the two panels each owned a SEPARATE m_jogRate, both starting at
        // 9: picking FINE on one left the other - and the ARROW KEYS, which
        // read this one - still slewing at 9. The button highlighted, so it
        // looked applied and was not.
        case ID_BTN_RATE_9: SetJogRate(9); if (m_onJogRateChange) m_onJogRateChange(9); return true;
        case ID_BTN_RATE_8: SetJogRate(8); if (m_onJogRateChange) m_onJogRateChange(8); return true;
        case ID_BTN_RATE_7: SetJogRate(7); if (m_onJogRateChange) m_onJogRateChange(7); return true;
        case ID_BTN_RATE_5: SetJogRate(5); if (m_onJogRateChange) m_onJogRateChange(5); return true;

        case ID_BTN_MOUNT_STOP:
            if (m_mount && m_mount->IsConnected()) {
                m_mount->StopSlew(0); m_mount->StopSlew(1);
                m_jogHeld = false; m_activeJogAxis = -1; ReleaseCapture();
            }
            // Also disengage planet lock on emergency stop
            if (m_planetLock) {
                m_planetLock = false;
                UpdatePlanetLockUI();
            }
            return true;

        case ID_BTN_SYNC: {
            m_planetLock = !m_planetLock;
            printf("[ControlPanel] Planet lock: %s\n", m_planetLock ? "ON" : "OFF");
            UpdatePlanetLockUI();
            if (m_onPlanetLock) m_onPlanetLock(m_planetLock);
            return true;
        }

        case ID_BTN_TRACKING: {
            // Toggle tracking on/off
            if (m_mount && m_mount->IsConnected()) {
                auto mode = m_mount->GetTrackingMode();
                bool tracking = (mode != raduk::core::IMount::TrackingMode::OFF);
                m_mount->SetTrackingMode(tracking
                    ? raduk::core::IMount::TrackingMode::OFF
                    : raduk::core::IMount::TrackingMode::ALT_AZ);
                // Button text updates on next UpdateReadouts tick
            }
            return true;
        }

        case ID_BTN_PARK: {
            // Park the mount — slew to home position and stop tracking
            if (m_mount && m_mount->IsConnected()) {
                printf("[ControlPanel] Park requested\n");
                m_mount->SetTrackingMode(raduk::core::IMount::TrackingMode::OFF);
                // NexStar doesn't have a formal park command via our protocol;
                // stopping tracking is the closest equivalent.
                // Full park (slew to Az=0, Alt=0) can be added to IMount later.
                if (m_lblGoToStatus)
                    SetWindowTextW(m_lblGoToStatus, L"Tracking stopped (Park)");
            }
            return true;
        }
        case ID_BTN_GOTO_FIND: {
            if (m_editGoToTarget && m_onGoTo) {
                wchar_t buf[64] = {};
                GetWindowTextW(m_editGoToTarget, buf, 64);
                // Convert to narrow string for ephemeris lookup
                char narrow[64] = {};
                WideCharToMultiByte(CP_ACP, 0, buf, -1, narrow, 64, nullptr, nullptr);
                if (narrow[0]) {
                    SetGoToStatus(L"Slewing...");
                    m_onGoTo(std::string(narrow));
                }
            }
            return true;
        }
        case ID_BTN_SOLVE_SYNC: {
            if (m_onSolveSync) {
                SetGoToStatus(L"Plate solving...");
                m_onSolveSync();
            }
            return true;
        }
    }
    return false;
}

// ── WM_HSCROLL ─────────────────────────────────────────────────

bool ControlPanel::HandleScroll(WPARAM wParam, LPARAM lParam) {
    if (!lParam) return false;
    HWND slider = (HWND)lParam;
    int action  = LOWORD(wParam);
    int pos     = (int)SendMessage(slider, TBM_GETPOS, 0, 0);
    bool commit = (action == TB_ENDTRACK || action == TB_THUMBPOSITION);
    wchar_t buf[32];

    if (slider == m_sliderGain) {
        swprintf(buf, 32, L"%d", pos); SetWindowTextW(m_lblGainVal, buf);
        if (commit && m_camera) m_camera->SetGain(pos);
        return true;
    }
    if (slider == m_sliderExposure) {
        // Slider range 1-60000 µs; SetExposure takes milliseconds
        double ms = pos / 1000.0;
        if (ms < 1.0)       swprintf(buf, 32, L"%.3fms", ms);
        else if (ms < 10.0) swprintf(buf, 32, L"%.2fms", ms);
        else                swprintf(buf, 32, L"%.1fms", ms);
        SetWindowTextW(m_lblExpVal, buf);
        if (commit && m_camera) m_camera->SetExposure(ms);
        return true;
    }
    if (slider == m_sliderGamma) {
        swprintf(buf, 32, L"%d", pos); SetWindowTextW(m_lblGammaVal, buf);
        if (commit && m_camera) m_camera->SetGamma(pos);
        return true;
    }
    if (slider == m_sliderWBR) {
        swprintf(buf, 32, L"%d", pos); SetWindowTextW(m_lblWBRVal, buf);
        if (commit && m_camera) {
            int wbb = (int)SendMessage(m_sliderWBB, TBM_GETPOS, 0, 0);
            m_camera->SetWhiteBalance(pos, wbb);
        }
        return true;
    }
    if (slider == m_sliderWBB) {
        swprintf(buf, 32, L"%d", pos); SetWindowTextW(m_lblWBBVal, buf);
        if (commit && m_camera) {
            int wbr = (int)SendMessage(m_sliderWBR, TBM_GETPOS, 0, 0);
            m_camera->SetWhiteBalance(wbr, pos);
        }
        return true;
    }
    if (slider == m_sliderUSB) {
        swprintf(buf, 32, L"%d%%", pos); SetWindowTextW(m_lblUSBVal, buf);
        if (commit && m_camera) m_camera->SetUSBBandwidth(pos);
        return true;
    }
    return false;
}

// ── UpdateReadouts ─────────────────────────────────────────────

void ControlPanel::SetJogRate(int r) {
    if (r != 5 && r != 7 && r != 8 && r != 9) return;
    m_jogRate = r;
    UpdateJogRateButtons();
    printf("[ControlPanel] Jog rate = %d\n", r);
}

void ControlPanel::UpdateJogRateButtons() {
    // Visually highlight the active speed button using WM_SETTEXT trick
    // Active button gets a marker prefix, inactive buttons are plain
    // This works with owner-draw or default button styles
    struct { HWND btn; int rate; const wchar_t* label; } btns[] = {
        { m_btnRate9, 9, L"9x"   },
        { m_btnRate8, 8, L"FAST" },
        { m_btnRate7, 7, L"MED"  },
        { m_btnRate5, 5, L"FINE" },
    };
    for (auto& b : btns) {
        if (!b.btn) continue;
        // Active = add chevron prefix; inactive = plain label
        wchar_t lbl[32];
        if (m_jogRate == b.rate)
            swprintf(lbl, 32, L"\u25b6 %s", b.label); // \u25B6 prefix
        else
            swprintf(lbl, 32, L"%s", b.label);
        SetWindowTextW(b.btn, lbl);
    }
}

// ── UpdateSlider ───────────────────────────────────────────────
// Programmatically moves a slider and updates its value label.
// Used by Auto-Exposure assistant to sync panel when it applies changes.
void ControlPanel::UpdateSlider(bool isExposure, int val) {
    HWND slider = isExposure ? m_sliderExposure : m_sliderGain;
    HWND lbl    = isExposure ? m_lblExpVal      : m_lblGainVal;

    if (slider) SendMessage(slider, TBM_SETPOS, TRUE, (LPARAM)val);

    if (lbl) {
        wchar_t buf[32];
        if (isExposure) {
            double ms = val / 1000.0;
            if      (ms < 1.0)  swprintf(buf, 32, L"%.3fms", ms);
            else if (ms < 10.0) swprintf(buf, 32, L"%.2fms", ms);
            else                swprintf(buf, 32, L"%.1fms", ms);
        } else {
            swprintf(buf, 32, L"%d", val);
        }
        SetWindowTextW(lbl, buf);
    }
}

void ControlPanel::UpdateReadouts() {
    wchar_t buf[64];
    // EAF — fast I2C, always safe
    if (m_focuser && m_focuser->IsOpen()) {
        int pos = m_focuser->GetPosition();
        swprintf(buf, 64, L"%d", pos);
        SetWindowTextW(m_lblFocusPos, buf);
        // Keep the GoTo box showing the live position so GO TO never
        // fires at a stale target - but NEVER while the user is typing
        // in it.
        //
        // This ran unconditionally on every readout tick (~500ms), so
        // a typed target was overwritten within half a second and the
        // field could not be edited at all: the only way to reach a
        // position was the fixed +/-10 and +/-1K buttons. Gating on
        // focus keeps the original intent - the box reads the current
        // position whenever you are not editing it - while letting a
        // value actually be typed.
        if (m_editFocusGoto && GetFocus() != m_editFocusGoto)
            SetWindowTextW(m_editFocusGoto, buf);
        swprintf(buf, 64, L"%.1f C", m_focuser->GetTemperature());
        SetWindowTextW(m_lblFocusTemp, buf);
    }
    // Mount — NexStar returns from cache (instant)
    if (m_mount) {
        bool connected = m_mount->IsConnected();

        // Connected status line
        if (m_lblMountConnected) {
            wchar_t devLine[128];
            if (connected) {
                auto cs = m_mount->ConnectionId();
                std::wstring cid(cs.begin(), cs.end());
                swprintf(devLine, 128, L"\u25CF  Celestron 8SE  %s", cid.c_str());
            } else {
                wcscpy_s(devLine, 128, L"\u25CB  Celestron 8SE  (not connected)");
            }
            SetWindowTextW(m_lblMountConnected, devLine);
            // Force colour repaint — WM_CTLCOLORSTATIC will pick connected=true
            InvalidateRect(m_lblMountConnected, nullptr, TRUE);
        }

        if (connected) {
            // RA / Dec
            double ra = 0, dec = 0;
            if (m_mount->GetRADec(ra, dec)) {
                swprintf(buf, 64, L"%hs", raduk::core::IMount::FormatRA(ra).c_str());
                if (m_lblMountRA) SetWindowTextW(m_lblMountRA, buf);
                swprintf(buf, 64, L"%hs", raduk::core::IMount::FormatDec(dec).c_str());
                if (m_lblMountDec) SetWindowTextW(m_lblMountDec, buf);
            }

            // Az / Alt — fed by the render loop's background GetAltAz poll
            // via SetMountAltAz (Phase 67). Until then azimuth was read from
            // the mount every cycle and thrown away, and these two labels
            // only ever read "--".
            if (m_altAzValid) {
                if (m_lblMountAlt) {
                    swprintf(buf, 64, L"%.1f°", m_altDeg);
                    SetWindowTextW(m_lblMountAlt, buf);
                }
                if (m_lblMountAz) {
                    // Cord wrap rides alongside the azimuth rather than
                    // taking its own row: it is the same quantity over
                    // time, and it stays hidden until it means something
                    // so a fresh session is not cluttered by "+0".
                    if (m_showWrap) {
                        swprintf(buf, 64, L"%.1f°   wrap %+.0f°%s",
                                 m_azDeg, m_wrapDeg,
                                 m_wrapOverLimit ? L"  !" : L"");
                    } else {
                        swprintf(buf, 64, L"%.1f°", m_azDeg);
                    }
                    SetWindowTextW(m_lblMountAz, buf);
                }
            } else {
                if (m_lblMountAz)  SetWindowTextW(m_lblMountAz,  L"--°");
                if (m_lblMountAlt) SetWindowTextW(m_lblMountAlt, L"--°");
            }

            // Hour Angle — HA = LST - RA
            // NexStar doesn't provide LST directly; compute from RA and cached RA epoch.
            // For now show placeholder — can extend IMount with GetHA() later.
            if (m_lblMountHA) SetWindowTextW(m_lblMountHA, L"--:--:--");

            // Tracking mode
            auto mode = m_mount->GetTrackingMode();
            swprintf(buf, 64, L"%hs", raduk::core::IMount::TrackingModeName(mode));
            if (m_lblMountTrack) SetWindowTextW(m_lblMountTrack, buf);

            // Update Tracking button label to reflect current state
            if (m_btnTracking) {
                bool tracking = (mode != raduk::core::IMount::TrackingMode::OFF);
                SetWindowTextW(m_btnTracking, tracking ? L"Tracking \u2713" : L"Tracking");
            }
        }
    }
}

// ── UpdateCaptureStatus ────────────────────────────────────────

void ControlPanel::UpdateCaptureStatus(bool recording, uint32_t framesRecorded,
                                        uint32_t framesDropped, double elapsedSec,
                                        uint64_t bufferUsed, uint64_t bufferTotal,
                                        uint32_t limitFrames, double limitTimeSec,
                                        bool limitByFrames,
                                        const std::string& outputDir) {
    if (!m_lblRecStatus) return;
    wchar_t buf[256];

    // Cache values for WM_PAINT bars
    m_paintBufUsed    = bufferUsed;
    m_paintBufTotal   = bufferTotal > 0 ? bufferTotal : 1;
    m_paintRecording  = recording;
    m_paintFrames     = framesRecorded;
    m_paintDrops      = framesDropped;
    m_paintElapsed    = elapsedSec;

    // Keep m_status.recording in sync — WM_DRAWITEM reads this field to colour
    // the owner-drawn RECORD button. UpdateStatus() (the 7-arg readout call)
    // updates m_status on a ~500ms timer, so without this sync the button
    // stays stale on the first click and gets one frame behind from then on.
    m_status.recording = recording;

    // Disk space — query once, cache for paint
    {
        std::wstring wdir(outputDir.begin(), outputDir.end());
        ULARGE_INTEGER avail = {}, total = {}, dummy = {};
        if (!wdir.empty() &&
            GetDiskFreeSpaceExW(wdir.c_str(), &avail, &total, &dummy)) {
            m_paintDiskFreeGB  = avail.QuadPart / 1073741824.0;
            m_paintDiskTotalGB = total.QuadPart  / 1073741824.0;
        }
    }

    // Legacy text labels (keep for now — painted bars are primary display)
    if (m_lblRecBuffer && bufferTotal > 0) {
        uint64_t free = bufferTotal - bufferUsed;
        swprintf(buf, 256, L"RAM: %llu/%llu slots  (%llu free)",
                 bufferUsed, bufferTotal, free);
        SetWindowTextW(m_lblRecBuffer, buf);
    }

    if (m_lblDiskSpace) {
        swprintf(buf, 256, L"Disk: %.1f GB free / %.0f GB",
                 m_paintDiskFreeGB, m_paintDiskTotalGB);
        SetWindowTextW(m_lblDiskSpace, buf);
    }

    // Recording status
    if (recording) {
        int mins = (int)(elapsedSec / 60), secs = (int)(elapsedSec) % 60;
        swprintf(buf, 128, L"REC  %02d:%02d", mins, secs);
        SetWindowTextW(m_lblRecStatus, buf);

        if (limitByFrames)
            swprintf(buf, 128, L"%u / %u frames  %u drops", framesRecorded, limitFrames, framesDropped);
        else {
            double rem = limitTimeSec - elapsedSec;
            if (rem < 0) rem = 0;
            swprintf(buf, 128, L"%u frames  %.0fs left  %u drops", framesRecorded, rem, framesDropped);
        }
        SetWindowTextW(m_lblRecFrames, buf);
        // RECORD button is owner-drawn — repaint to show STOP RECORDING (red)
        if (m_btnRecord) InvalidateRect(m_btnRecord, nullptr, TRUE);
    } else {
        SetWindowTextW(m_lblRecStatus, L"IDLE");
        if (framesRecorded > 0) {
            swprintf(buf, 128, L"Last: %u frames  %u drops", framesRecorded, framesDropped);
            SetWindowTextW(m_lblRecFrames, buf);
        } else {
            SetWindowTextW(m_lblRecFrames, L"");
        }
        // RECORD button is owner-drawn — repaint to show RECORD (cyan)
        if (m_btnRecord) InvalidateRect(m_btnRecord, nullptr, TRUE);
    }
}

// ── Capture config getters ─────────────────────────────────────

std::string ControlPanel::GetFilePrefix() const {
    if (!m_editFilePrefix) return "Capture";
    wchar_t buf[64] = {};
    GetWindowTextW(m_editFilePrefix, buf, 64);
    char out[64] = {};
    for (int i = 0; i < 63 && buf[i]; i++) out[i] = (char)buf[i];
    return std::string(out[0] ? out : "Capture");
}

std::string ControlPanel::GetFrameType() const {
    if (!m_comboFrameType) return "Light";
    int sel = (int)SendMessage(m_comboFrameType, CB_GETCURSEL, 0, 0);
    switch (sel) {
        case 1:  return "Dark";
        case 2:  return "Flat";
        case 3:  return "Bias";
        default: return "Light";
    }
}

std::string ControlPanel::GetOutputDir() const {
    if (!m_editOutputDir) return "D:\\RadukCapture\\captures";
    wchar_t buf[MAX_PATH] = {};
    GetWindowTextW(m_editOutputDir, buf, MAX_PATH);
    char out[MAX_PATH] = {};
    for (int i = 0; i < MAX_PATH-1 && buf[i]; i++) out[i] = (char)buf[i];
    return std::string(out[0] ? out : "D:\\RadukCapture\\captures");
}

uint32_t ControlPanel::GetLimitFrames() const {
    if (!m_editLimitFrames) return 1000;
    wchar_t buf[16] = {};
    GetWindowTextW(m_editLimitFrames, buf, 16);
    int v = _wtoi(buf);
    return (v > 0) ? (uint32_t)v : 1000;
}

double ControlPanel::GetLimitTimeSec() const {
    if (!m_editLimitTime) return 60.0;
    wchar_t buf[16] = {};
    GetWindowTextW(m_editLimitTime, buf, 16);
    double v = _wtof(buf);
    return (v > 0) ? v : 60.0;
}

bool ControlPanel::GetLimitByFrames() const {
    if (!m_radioFrames) return true;
    return SendMessage(m_radioFrames, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

// ── Phase 85: profile → panel ──────────────────────────
// Each of these mirrors the formatting its HandleScroll branch uses, so
// a value shown after a load reads identically to the same value shown
// after a drag. They deliberately do NOT call back into the camera:
// ApplyProfile has already done that, and a second write here would
// race it.

void ControlPanel::SetCameraSliders(int gain, int exposureUs, int gamma,
                                    int wbRed, int wbBlue, int usbBandwidth) {
    wchar_t buf[32];
    auto put = [&](HWND sl, HWND lbl, int v, const wchar_t* fmt) {
        if (!sl) return;
        SendMessage(sl, TBM_SETPOS, TRUE, v);
        if (lbl) { swprintf(buf, 32, fmt, v); SetWindowTextW(lbl, buf); }
    };
    put(m_sliderGain,  m_lblGainVal,  gain,         L"%d");
    put(m_sliderGamma, m_lblGammaVal, gamma,        L"%d");
    put(m_sliderWBR,   m_lblWBRVal,   wbRed,        L"%d");
    put(m_sliderWBB,   m_lblWBBVal,   wbBlue,       L"%d");
    put(m_sliderUSB,   m_lblUSBVal,   usbBandwidth, L"%d%%");

    if (m_sliderExposure) {
        SendMessage(m_sliderExposure, TBM_SETPOS, TRUE, exposureUs);
        if (m_lblExpVal) {
            // The slider tops out at 60 ms, by design -- longer exposures
            // are set in the profile editor. But the DSO profile asks for
            // 30 SECONDS, and TBM_SETPOS silently clamps, so printing the
            // profile's value beside a pegged slider would have them
            // disagreeing: the same fault this method exists to fix.
            // The label tells the truth about the camera and marks itself
            // with a '+' when the slider cannot reach it.
            const int shown = (int)SendMessage(m_sliderExposure, TBM_GETPOS, 0, 0);
            const bool pegged = (shown != exposureUs);
            const double ms = (double)exposureUs / 1000.0;
            const wchar_t* tail = pegged ? L"+" : L"";
            if (ms < 1.0)       swprintf(buf, 32, L"%.3fms%ls", ms, tail);
            else if (ms < 10.0) swprintf(buf, 32, L"%.2fms%ls", ms, tail);
            else if (ms < 1000.0) swprintf(buf, 32, L"%.1fms%ls", ms, tail);
            else                swprintf(buf, 32, L"%.1fs%ls", ms / 1000.0, tail);
            SetWindowTextW(m_lblExpVal, buf);
        }
    }
}

void ControlPanel::SetFilePrefix(const std::string& prefix) {
    if (prefix.empty()) return;
    wchar_t w[64] = {};
    MultiByteToWideChar(CP_UTF8, 0, prefix.c_str(), -1, w, 63);
    // The cache first, and unconditionally. A profile can be loaded
    // while CAPTURE is collapsed -- a GoTo auto-load with the panel shut
    // is the normal case -- and the control does not exist then. Writing
    // only the window would drop the value, and expanding the section
    // later would restore the PREVIOUS prefix from this same cache while
    // the recorder was using the profile's. Cache then window keeps them
    // one value.
    m_ui.filePrefix = w;
    if (m_editFilePrefix) SetWindowTextW(m_editFilePrefix, w);
}

void ControlPanel::SetCaptureLimit(bool byFrames, uint32_t frames,
                                   double timeSec) {
    wchar_t buf[24];
    // Cached unconditionally, for the same reason as the prefix above.
    if (frames > 0) {
        swprintf(buf, 24, L"%u", frames);
        m_ui.limitFrames = buf;
        if (m_editLimitFrames) SetWindowTextW(m_editLimitFrames, buf);
    }
    if (timeSec > 0.0) {
        swprintf(buf, 24, L"%.0f", timeSec);
        m_ui.limitTime = buf;
        if (m_editLimitTime) SetWindowTextW(m_editLimitTime, buf);
    }
    m_ui.limitByFrames = byFrames ? 1 : 0;
    // The radio pair is what GetLimitByFrames() reads, so it has to move
    // too -- otherwise a time-limited profile loads its seconds into the
    // box and still stops on the frame count.
    if (m_radioFrames) SendMessage(m_radioFrames, BM_SETCHECK,
                                   byFrames ? BST_CHECKED : BST_UNCHECKED, 0);
    if (m_radioTime)   SendMessage(m_radioTime,   BM_SETCHECK,
                                   byFrames ? BST_UNCHECKED : BST_CHECKED, 0);
}

// ── Control creation helpers ───────────────────────────────────

HWND ControlPanel::MakeLabel(const wchar_t* text, int x, int y, int w, int h, int id) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, m_panelHwnd,
        (HMENU)(INT_PTR)id, GetModuleHandle(nullptr), nullptr);
}

void ControlPanel::MakeSectionLabel(const char* text, int x, int y, int w, int h) {
    wchar_t buf[64]; swprintf(buf, 64, L"--- %hs ---", text);
    MakeLabel(buf, x, y, w, h);
}

void ControlPanel::RefreshFilterLabel() {
    if (!m_lblFilter) return;
    const auto& rig = raduk::core::RigConfig::Get();
    wchar_t buf[96];
    // Name and band together: the name is what the user recognises on
    // the camera, the band is what the dispersion model actually used.
    // Showing only the name would hide a hand-edited rig.ini whose
    // edges no longer match the preset it is still called after.
    swprintf(buf, 96, L"Filter: %hs (%.0f-%.0fnm)",
             rig.filterName.c_str(), rig.filterLowNm, rig.filterHighNm);
    SetWindowTextW(m_lblFilter, buf);
}

HWND ControlPanel::MakeValueLabel(const wchar_t* text, int x, int y, int w, int h, int id) {
    // Right-aligned, like FC's value readouts
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT,
        x, y, w, h, m_panelHwnd,
        (HMENU)(INT_PTR)id, GetModuleHandle(nullptr), nullptr);
}

HWND ControlPanel::MakeEditBox(const wchar_t* text, int x, int y, int w, int h, int id) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_NUMBER | ES_AUTOHSCROLL,
        x, y, w, h, m_panelHwnd,
        (HMENU)(INT_PTR)id, GetModuleHandle(nullptr), nullptr);
}

HWND ControlPanel::MakeSlider(int x, int y, int w, int h,
                               int lo, int hi, int val, int id) {
    HWND s = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        x, y, w, h, m_panelHwnd,
        (HMENU)(INT_PTR)id, GetModuleHandle(nullptr), nullptr);
    SendMessage(s, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi));
    SendMessage(s, TBM_SETPOS,   TRUE, val);
    // Strip UxTheme so WM_CTLCOLORSCROLLBAR takes effect for Elite Raduk colors
    SetWindowTheme(s, L"", L"");
    return s;
}

HWND ControlPanel::MakeButton(const wchar_t* text, int x, int y, int w, int h, int id) {
    return CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, w, h, m_panelHwnd,
        (HMENU)(INT_PTR)id, GetModuleHandle(nullptr), nullptr);
}

HWND ControlPanel::MakeComboBox(int x, int y, int w, int h, int id) {
    return CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, y, w, h, m_panelHwnd,
        (HMENU)(INT_PTR)id, GetModuleHandle(nullptr), nullptr);
}

// ── Mount control ownership ────────────────────────────────────

void ControlPanel::SetRCControl(bool take) {
    if (take == m_rcInControl) return;
    if (!take && m_mount && m_mount->IsConnected()) {
        if (m_jogHeld) OnJogStop();
        m_mount->StopSlew(0);
        m_mount->StopSlew(1);
    }
    m_rcInControl = take;
    printf("[ControlPanel] RC control: %s\n", take ? "on" : "off");
}

void ControlPanel::UpdatePlanetLockUI() {
    if (!m_btnSync || !m_lblMountControl) return;
    InvalidateRect(m_btnSync, nullptr, TRUE); // triggers WM_DRAWITEM repaint
    // Phase 70: the label is driven by SetAcquisitionStatus now, which
    // updates every tick with the real stage. Writing a fixed string
    // here as well made the two fight, and the stale one usually won.
    if (m_acqStatus.empty()) {
        SetWindowTextW(m_lblMountControl, m_planetLock ? L"PLANET LOCK ACTIVE" : L"");
        InvalidateRect(m_lblMountControl, nullptr, TRUE);
    }
}

// ── Analysis / Histogram ───────────────────────────────────────

void ControlPanel::UpdateAnalysis(const raduk::core::AnalysisResult& analysis) {
    // Only repaint if values changed meaningfully — avoids 60fps strip repaints
    bool changed = !m_hasAnalysis
        || (int)(analysis.seeingScore * 10) != (int)(m_analysis.seeingScore * 10)
        || (int)(analysis.sharpnessPct)     != (int)(m_analysis.sharpnessPct);

    m_analysis    = analysis;
    m_hasAnalysis = analysis.valid;

    if (changed && m_panelHwnd) {
        RECT histR = { 0, m_contentBottom, 32767, m_contentBottom + 110 };
        InvalidateRect(m_panelHwnd, &histR, FALSE);
    }
}

// ── UpdateTemperature ─────────────────────────────────────────


// ── Elite Raduk colour palette ────────────────────────────────
namespace {
    // Base surfaces
    const COLORREF kBg       = RGB(4,   8,  15);  // --bg-void (histogram/strip bg)
    const COLORREF kPanel    = RGB(7,  13,  22);  // --bg-deep (panel background)
    const COLORREF kRaised   = RGB(10, 21,  32);  // --bg-panel (section headers)
    const COLORREF kSep      = RGB(0,  45,  60);   // dim cyan separator (~12% opacity equivalent)
    const COLORREF kDim      = RGB(140, 175, 190); // --dim
    const COLORREF kText     = RGB(200, 232, 240); // --white
    // Accent colors — match Elite Raduk design spec
    const COLORREF kCyan     = RGB(0,   229, 255); // --cyan (primary)
    const COLORREF kGreen    = RGB( 77, 255, 145); // --green (mount/live)
    const COLORREF kAmber    = RGB(255, 185,   0); // --amber (EAF/warning)
    const COLORREF kRed      = RGB(255,  59,  92); // --red (capture/danger)
    const COLORREF kHistBg   = RGB(4,   10,  18);  // histogram bar background
    // Per-channel histogram tints
    const COLORREF kHistR    = RGB(255,  80,  80);
    const COLORREF kHistG    = RGB( 80, 220,  80);
    const COLORREF kHistB    = RGB( 80, 140, 255);

    HFONT MakeFont(int px, bool bold, const char* face) {
        return CreateFontA(px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
            0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, face);
    }

    // ── Elite Raduk section header ─────────────────────────────
    // Deep navy background, 3px left accent stripe, Barlow Condensed bold.
    // A thin 1px separator line is drawn above by the caller's data rows.
    void DrawSectionHeader(HDC hdc, int x, int y, int w, int h,
                            const char* title, COLORREF accent,
                            bool collapsed = false) {
        // Background
        HBRUSH hdrBrush = CreateSolidBrush(RGB(10, 21, 32));
        RECT hr = {x, y, x+w, y+h};
        FillRect(hdc, &hr, hdrBrush);
        DeleteObject(hdrBrush);

        // Colored circle icon (diameter = h-6, centered vertically, 6px from left)
        const int circ = h - 6;
        const int cx = x + 4 + circ/2;
        const int cy = y + h/2;
        HBRUSH circleBrush = CreateSolidBrush(accent);
        HPEN circlePen = CreatePen(PS_SOLID, 1,
            RGB((int)(GetRValue(accent)*0.6f),
                (int)(GetGValue(accent)*0.6f),
                (int)(GetBValue(accent)*0.6f)));
        HPEN oldPen = (HPEN)SelectObject(hdc, circlePen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, circleBrush);
        Ellipse(hdc, cx-circ/2, cy-circ/2, cx+circ/2, cy+circ/2);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(circleBrush);
        DeleteObject(circlePen);

        // Title initial letter inside circle (dark, centered)
        if (title && title[0]) {
            char letter[2] = { title[0], 0 };
            HFONT fi = MakeFont(9, true, "Barlow Condensed");
            HFONT oldf = (HFONT)SelectObject(hdc, fi);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(4, 8, 15));
            RECT lr = { cx-circ/2, cy-circ/2, cx+circ/2, cy+circ/2 };
            DrawTextA(hdc, letter, 1, &lr, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            SelectObject(hdc, oldf);
            DeleteObject(fi);
        }

        // Section title — bold, accent color, left-aligned after circle
        HFONT f = MakeFont(12, true, "Barlow Condensed");
        HFONT old = (HFONT)SelectObject(hdc, f);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, accent);
        RECT tr = {x + 4 + circ + 6, y, x+w-20, y+h};
        DrawTextA(hdc, title, -1, &tr, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SelectObject(hdc, old);
        DeleteObject(f);

        // Chevron (▼ or ▶) right-aligned
        HFONT cf = MakeFont(11, false, "Barlow Condensed");
        HFONT oldc = (HFONT)SelectObject(hdc, cf);
        SetTextColor(hdc, accent);
        RECT chevR = {x+w-18, y, x+w-2, y+h};
        const wchar_t* chev = collapsed ? L"\u25BA" : L"\u25BC";
        DrawTextW(hdc, chev, -1, &chevR, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(hdc, oldc);
        DeleteObject(cf);

        // Accent bottom line
        HPEN linePen = CreatePen(PS_SOLID, 1,
            RGB((int)(GetRValue(accent)*0.4f),
                (int)(GetGValue(accent)*0.4f),
                (int)(GetBValue(accent)*0.4f)));
        HPEN oldLine = (HPEN)SelectObject(hdc, linePen);
        MoveToEx(hdc, x, y+h-1, nullptr);
        LineTo(hdc, x+w, y+h-1);
        SelectObject(hdc, oldLine);
        DeleteObject(linePen);
    }

    void FillRect_(HDC hdc, int x, int y, int w, int h, COLORREF c) {
        HBRUSH b = CreateSolidBrush(c);
        RECT r = {x, y, x+w, y+h};
        FillRect(hdc, &r, b);
        DeleteObject(b);
    }

    void DrawBar(HDC hdc, int x, int y, int w, int h,
                  float pct, COLORREF fillClr, COLORREF bgClr,
                  const char* label, COLORREF textClr) {
        // Background
        FillRect_(hdc, x, y, w, h, bgClr);
        // Fill
        int fillW = (int)(pct / 100.0f * w);
        if (fillW > 0) FillRect_(hdc, x, y, fillW, h, fillClr);
        // Label inside bar
        if (label) {
            HFONT f = MakeFont(12, false, "Barlow Condensed");
            HFONT old = (HFONT)SelectObject(hdc, f);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, textClr);
            RECT tr = {x+3, y, x+w-3, y+h};
            DrawTextA(hdc, label, -1, &tr, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            SelectObject(hdc, old);
            DeleteObject(f);
        }
    }
}

// ── PaintAll — GDI strip below Win32 controls ─────────────────
// STATUS, HISTOGRAM, TEMPERATURE sections with collapsible headers.
// Must be defined after the anonymous namespace so DrawSectionHeader
// and kCyan/kGreen/kAmber are visible.

void ControlPanel::PaintAll(HDC hdc, const RECT& cr) {
    const int scrollW = GetSystemMetrics(SM_CXVSCROLL) + 2;
    const int panW    = cr.right - scrollW;  // exclude scrollbar column
    const int screenY = m_contentBottom - m_scrollY + 2;
    if (screenY >= cr.bottom) return;

    const int HDR = 22;
    int y = screenY;

    HBRUSH bgBr = CreateSolidBrush(RGB(4, 8, 15));
    RECT stripRc = { 0, screenY, cr.right, cr.bottom };  // full width fill
    FillRect(hdc, &stripRc, bgBr);
    DeleteObject(bgBr);

    auto gdiHeader = [&](const char* title, COLORREF accent, Section s,
                          RECT& outHitRect) -> int {
        DrawSectionHeader(hdc, 0, y, panW, HDR, title, accent, IsCollapsed(s));
        outHitRect = { 0, y, panW, y + HDR };
        return y + HDR + 2;
    };

    // ── STATUS ────────────────────────────────────────────────
    m_gdiHdrStatus = {};
    y = gdiHeader("STATUS", kGreen, Section::Status, m_gdiHdrStatus);
    if (!IsCollapsed(Section::Status)) {
        PaintStatus(hdc, 0, y, panW);
        y += 66;
    }

    // ── HISTOGRAM ─────────────────────────────────────────────
    m_gdiHdrHistogram = {};
    y = gdiHeader("HISTOGRAM", kCyan, Section::Histogram, m_gdiHdrHistogram);
    if (!IsCollapsed(Section::Histogram)) {
        PaintHistogram(hdc, 0, y, panW, 110);
        y += 110;
    }

    // ── TEMPERATURE ───────────────────────────────────────────
    m_gdiHdrTemperature = {};
    y = gdiHeader("TEMPERATURE", kAmber, Section::Temperature, m_gdiHdrTemperature);
    if (!IsCollapsed(Section::Temperature) && m_hasTempData) {
        PaintTemperature(hdc, 0, y, panW, 130);
    }
}

// ── FC-style Histogram ─────────────────────────────────────────
void ControlPanel::PaintHistogram(HDC hdc, int panX, int panY, int panW, int panH) {

    const int padX = 10;  // matches panel left/right margin
    int y = panY;

    const int rowH  = 26;
    const int labW  = 16; // "R"/"G"/"B" label column — narrower, just the letter
    const int pctW  = 46;
    const int barX  = panX + padX + labW + 4;  // 4px gap after label
    const int barW  = panW - padX - labW - 4 - pctW - 4;
    if (barW < 10) return;

    // FrameAnalyzer provides a single luminance histogram (Bayer green channel).
    // Display as three tinted rows — same data, FC-style R/G/B visual layout.
    // When per-channel histograms are added to FrameAnalyzer, swap these pointers.
    struct ChanRow {
        const char* label;
        COLORREF    color;
        const std::array<uint32_t,256>* hist;
        float       clip;
        float       mean;
    } rows[3] = {
        {"R", kHistR, &m_analysis.histogram, m_analysis.clipPct, m_analysis.meanBrightness},
        {"G", kHistG, &m_analysis.histogram, m_analysis.clipPct, m_analysis.meanBrightness},
        {"B", kHistB, &m_analysis.histogram, m_analysis.clipPct, m_analysis.meanBrightness},
    };

    HFONT labFont = MakeFont(10, true,  "Barlow Condensed");
    HFONT valFont = MakeFont(10, false, "Space Mono");
    SetBkMode(hdc, TRANSPARENT);

    for (int ch = 0; ch < 3; ch++) {
        auto& row = rows[ch];
        int ry = y + ch * (rowH + 1);

        // Fill full row background first (prevents bleed from previous frames)
        FillRect_(hdc, panX + padX, ry, panW - padX, rowH, RGB(4, 8, 15));

        // Fill label area with panel deep bg (distinct from bar bg)
        FillRect_(hdc, panX + padX, ry, labW + 4, rowH, RGB(7, 13, 22));

        // Channel label (R / G / B)
        HFONT old = (HFONT)SelectObject(hdc, labFont);
        SetTextColor(hdc, row.color);
        RECT lr = {panX+padX, ry, panX+padX+labW, ry+rowH};
        DrawTextA(hdc, row.label, -1, &lr, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(hdc, old);

        // Histogram bar area — dark background
        FillRect_(hdc, barX, ry, barW, rowH, kHistBg);

        // Find peak
        uint32_t peak = 1;
        for (int i = 1; i < 255; i++)
            if ((*row.hist)[i] > peak) peak = (*row.hist)[i];

        // Draw histogram bars — skip bin 0/1 (dark current noise, not signal)
        for (int i = 2; i < 256; i++) {
            if (!(*row.hist)[i]) continue;
            int bh = (int)((float)(*row.hist)[i] / peak * rowH);
            if (bh < 1) continue;
            int bx  = barX + (int)((float)i / 256.0f * barW);
            int bx2 = barX + (int)((float)(i+1) / 256.0f * barW);
            if (bx2 <= bx) bx2 = bx + 1;
            // Clip zone: brighten bar color
            COLORREF c = (i >= 248) ? kRed : row.color;
            FillRect_(hdc, bx, ry + rowH - bh, bx2 - bx, bh, c);
        }

        // Mean marker — vertical line at mean position, only if meaningful
        if (row.mean > 2.0f) {
            int mx = barX + (int)(row.mean / 255.0f * barW);
            mx = (mx < barX) ? barX : (mx >= barX + barW) ? barX + barW - 1 : mx;
            HPEN mp = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
            HPEN olds = (HPEN)SelectObject(hdc, mp);
            MoveToEx(hdc, mx, ry + 2, nullptr);
            LineTo(hdc, mx, ry + rowH - 2);
            SelectObject(hdc, olds);
            DeleteObject(mp);
        }

        // Peak% label: "0/15.5%" like FC  (mean/clipPct)
        old = (HFONT)SelectObject(hdc, valFont);
        char pctBuf[20];
        sprintf_s(pctBuf, "%.1f%%", row.clip);
        COLORREF pctClr = (row.clip > 1.0f) ? kRed :
                           (row.clip > 0.1f) ? kAmber : kDim;
        SetTextColor(hdc, pctClr);
        int pctX = barX + barW + 2;
        RECT pr = {pctX, ry, pctX+pctW, ry+rowH};
        DrawTextA(hdc, pctBuf, -1, &pr, DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        SelectObject(hdc, old);

        // Row separator
        FillRect_(hdc, panX, ry + rowH, panW, 1, kSep);
    }

    // ── Seeing score + sharpness row ──────────────────────────
    int sy = y + 3 * (rowH + 1) + 2;
    HFONT sf = MakeFont(10, false, "Space Mono");
    HFONT old2 = (HFONT)SelectObject(hdc, sf);
    SetBkMode(hdc, TRANSPARENT);

    float score = m_analysis.seeingScore;
    COLORREF sClr = (score >= 7.0f) ? kGreen :
                     (score >= 4.0f) ? kAmber : kRed;
    char sBuf[40];
    sprintf_s(sBuf, "SEEING  %.1f/10    SHARP  %.0f%%",
              score, m_analysis.sharpnessPct);
    SetTextColor(hdc, sClr);
    RECT sr = {panX+padX, sy, panX+panW-padX, sy+14};
    DrawTextA(hdc, sBuf, -1, &sr, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    SelectObject(hdc, old2);
    DeleteObject(sf);
    DeleteObject(labFont);
    DeleteObject(valFont);
}

// ── Status — HDD/Buffer bars + FPS/Lost ──────────────────────
void ControlPanel::PaintStatus(HDC hdc, int panX, int panY, int panW) {
    const int padX  = 10;  // matches panel margin
    const int BH    = 20;
    const int GAP   = 3;

    int y = panY + 3;

    const int rightW = 90;   // wider right column for FPS/LOST values
    const int lblW   = 44;   // label column width
    const int barW   = panW - padX*2 - lblW - rightW - 4;

    HFONT lf = MakeFont(13, true,  "Barlow Condensed");  // bold label
    HFONT vf = MakeFont(12, false, "Space Mono");         // mono value
    SetBkMode(hdc, TRANSPARENT);

    // ── Row 1: HDD bar + FPS ─────────────────────────────────
    {
        HFONT old = (HFONT)SelectObject(hdc, lf);
        SetTextColor(hdc, kDim);
        RECT lr = {panX+padX, y, panX+padX+lblW, y+BH};
        DrawTextA(hdc, "HDD", -1, &lr, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SelectObject(hdc, old);

        float diskPct = m_status.recording
            ? (m_status.diskUsedGB / 500.0f * 100.0f < 100.0f
                ? m_status.diskUsedGB / 500.0f * 100.0f : 100.0f)
            : 0.0f;
        char diskLabel[24]; sprintf_s(diskLabel, "%.3f GB", m_status.diskUsedGB);
        int bx = panX + padX + lblW;
        DrawBar(hdc, bx, y, barW, BH, diskPct, kGreen, kHistBg, diskLabel, RGB(200,232,240));

        // FPS label + value
        old = (HFONT)SelectObject(hdc, lf);
        SetTextColor(hdc, kDim);
        RECT fl = {bx+barW+4, y, bx+barW+36, y+BH};
        DrawTextA(hdc, "FPS", -1, &fl, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SelectObject(hdc, old);

        old = (HFONT)SelectObject(hdc, vf);
        char fpsBuf[12]; sprintf_s(fpsBuf, "%.1f", m_status.fps);
        SetTextColor(hdc, kCyan);
        RECT fv = {bx+barW+36, y, bx+barW+rightW, y+BH};
        DrawTextA(hdc, fpsBuf, -1, &fv, DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        SelectObject(hdc, old);
    }

    y += BH + GAP;

    // ── Row 2: BUF bar + LOST ─────────────────────────────────
    {
        HFONT old = (HFONT)SelectObject(hdc, lf);
        SetTextColor(hdc, kDim);
        RECT lr = {panX+padX, y, panX+padX+lblW, y+BH};
        DrawTextA(hdc, "BUF", -1, &lr, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SelectObject(hdc, old);

        // BUF bar — shows pending/total so user sees buffer filling and draining
        char bufLabel[32];
        int bPending = m_status.bufferFrames;
        int bTotal   = m_status.bufferTotal > 0 ? m_status.bufferTotal : 512;
        sprintf_s(bufLabel, "%d / %d slots", bPending, bTotal);

        // Color shifts amber→red as buffer fills: green <50%, amber 50-80%, red >80%
        COLORREF bufClr = (m_status.bufferPct > 80.0f) ? kRed
                        : (m_status.bufferPct > 50.0f) ? kAmber
                        : kGreen;

        int bx = panX + padX + lblW;
        DrawBar(hdc, bx, y, barW, BH, m_status.bufferPct,
                bufClr, kHistBg, bufLabel, RGB(200,232,240));

        old = (HFONT)SelectObject(hdc, lf);
        SetTextColor(hdc, kDim);
        RECT ll = {bx+barW+4, y, bx+barW+44, y+BH};
        DrawTextA(hdc, "LOST", -1, &ll, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SelectObject(hdc, old);

        old = (HFONT)SelectObject(hdc, vf);
        char lostBuf[8]; sprintf_s(lostBuf, "%d", m_status.framesLost);
        SetTextColor(hdc, m_status.framesLost > 0 ? kRed : kDim);
        RECT lv = {bx+barW+44, y, bx+barW+rightW, y+BH};
        DrawTextA(hdc, lostBuf, -1, &lv, DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        SelectObject(hdc, old);
    }

    DeleteObject(lf);
    DeleteObject(vf);
}

// ── Temperature — rolling line chart ─────────────────────────
void ControlPanel::PaintTemperature(HDC hdc, int panX, int panY, int panW, int panH) {
    const int padX = 10;  // matches panel margin
    const int LBLH = 22;   // bottom label row

    int cy      = panY + 4;   // 4px top padding
    int chartH  = panH - LBLH - 8;  // subtract top padding too
    int chartW  = panW - padX * 2;
    if (chartH < 20) return;

    // Fill background including padding
    FillRect_(hdc, panX + padX, panY, chartW, panH, kHistBg);

    if (m_tempHistory.size() < 2) {
        HFONT f = MakeFont(13, false, "Barlow Condensed");
        HFONT old = (HFONT)SelectObject(hdc, f);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, kDim);
        RECT r = {panX+padX, cy, panX+padX+chartW, cy+chartH};
        DrawTextA(hdc, "Collecting data...", -1, &r, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(hdc, old);
        DeleteObject(f);
        return;
    }

    float tMin = 999.0f, tMax = -999.0f;
    for (auto& s : m_tempHistory) {
        if (s.camC < tMin) tMin = s.camC;
        if (s.focC < tMin) tMin = s.focC;
        if (s.camC > tMax) tMax = s.camC;
        if (s.focC > tMax) tMax = s.focC;
    }
    float tRange = tMax - tMin;
    if (tRange < 2.0f) { tMin -= 1.0f; tRange = 2.0f; }

    int n = (int)m_tempHistory.size();

    auto tempToY = [&](float t) -> int {
        float norm = (t - tMin) / tRange;
        return cy + chartH - 1 - (int)(norm * (chartH - 2));
    };
    auto idxToX = [&](int i) -> int {
        return panX + padX + (int)((float)i / (TEMP_HISTORY - 1) * chartW);
    };

    struct Trace { COLORREF clr; bool isFoc; } traces[] = {
        {kGreen, false},
        {kAmber, true},
    };
    for (auto& tr : traces) {
        HPEN pen = CreatePen(PS_SOLID, 1, tr.clr);
        HPEN old = (HPEN)SelectObject(hdc, pen);
        bool first = true;
        for (int i = 0; i < n; i++) {
            float t = tr.isFoc ? m_tempHistory[i].focC : m_tempHistory[i].camC;
            int px = idxToX(i);
            int py = tempToY(t);
            if (first) { MoveToEx(hdc, px, py, nullptr); first = false; }
            else        LineTo(hdc, px, py);
        }
        SelectObject(hdc, old);
        DeleteObject(pen);
    }

    // Bottom label row — larger, clearly readable
    HFONT lf  = MakeFont(13, true,  "Barlow Condensed");
    HFONT vf  = MakeFont(12, false, "Space Mono");
    SetBkMode(hdc, TRANSPARENT);
    int labY = panY + panH - LBLH;

    char buf[48];

    // Scale labels: tMin / tMax on the left
    HFONT old2 = (HFONT)SelectObject(hdc, vf);
    SetTextColor(hdc, kDim);
    sprintf_s(buf, "%.0f\xB0", tMin);
    RECT r1 = {panX+padX, labY, panX+padX+34, labY+LBLH};
    DrawTextA(hdc, buf, -1, &r1, DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    sprintf_s(buf, "%.0f\xB0", tMax);
    RECT r2 = {panX+padX+36, labY, panX+padX+72, labY+LBLH};
    DrawTextA(hdc, buf, -1, &r2, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    SelectObject(hdc, old2);

    // Cam + Foc current values on the right
    int rx = panX + panW - padX - 148;

    old2 = (HFONT)SelectObject(hdc, lf);
    SetTextColor(hdc, kDim);
    RECT cl = {rx, labY, rx+32, labY+LBLH};
    DrawTextA(hdc, "Cam", -1, &cl, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    SelectObject(hdc, old2);

    old2 = (HFONT)SelectObject(hdc, vf);
    sprintf_s(buf, "%.1f\xB0""C", m_tempCamC);
    SetTextColor(hdc, kGreen);
    RECT cv = {rx+34, labY, rx+92, labY+LBLH};
    DrawTextA(hdc, buf, -1, &cv, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    SelectObject(hdc, old2);

    old2 = (HFONT)SelectObject(hdc, lf);
    SetTextColor(hdc, kDim);
    RECT fl2 = {rx+96, labY, rx+128, labY+LBLH};
    DrawTextA(hdc, "Foc", -1, &fl2, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    SelectObject(hdc, old2);

    old2 = (HFONT)SelectObject(hdc, vf);
    sprintf_s(buf, "%.1f\xB0""C", m_tempFocC);
    SetTextColor(hdc, kAmber);
    RECT fv2 = {rx+130, labY, rx+190, labY+LBLH};
    DrawTextA(hdc, buf, -1, &fv2, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    SelectObject(hdc, old2);

    DeleteObject(lf);
    DeleteObject(vf);
}

// ── RebuildControls ────────────────────────────────────────────
// Destroys all child controls and rebuilds at the given client width.
// Called from Create() and WM_SIZE so the layout reflows on resize.

// Only records a field whose control currently EXISTS. A collapsed
// section has no controls, and reading one would otherwise overwrite a
// good saved value with an empty string.
void ControlPanel::SnapshotUi() {
    auto grab = [](HWND h, std::wstring& dst) {
        if (!h) return;
        int len = GetWindowTextLengthW(h);
        std::wstring s;
        s.resize((size_t)len + 1);
        GetWindowTextW(h, &s[0], len + 1);
        s.resize((size_t)len);
        dst = s;
    };
    grab(m_editFilePrefix,    m_ui.filePrefix);
    grab(m_editOutputDir,     m_ui.outputDir);
    grab(m_editLimitFrames,   m_ui.limitFrames);
    grab(m_editLimitTime,     m_ui.limitTime);
    grab(m_editNotes,         m_ui.notes);
    grab(m_editGateThreshold, m_ui.gateThreshold);
    // The gate tick, which was the one value with no store outside its
    // own control -- so collapsing CAPTURE lost it and turned the gate
    // off at the same time.
    if (m_btnGateEnable)
        m_ui.gateEnabled =
            (SendMessage(m_btnGateEnable, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
    if (m_comboFrameType)
        m_ui.frameType = (int)SendMessage(m_comboFrameType, CB_GETCURSEL, 0, 0);
    if (m_radioFrames)
        m_ui.limitByFrames =
            (SendMessage(m_radioFrames, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
}

void ControlPanel::RestoreUi() {
    auto put = [](HWND h, const std::wstring& v) {
        if (h && !v.empty()) SetWindowTextW(h, v.c_str());
    };
    put(m_editFilePrefix,    m_ui.filePrefix);
    put(m_editOutputDir,     m_ui.outputDir);
    put(m_editLimitFrames,   m_ui.limitFrames);
    put(m_editLimitTime,     m_ui.limitTime);
    put(m_editNotes,         m_ui.notes);
    put(m_editGateThreshold, m_ui.gateThreshold);
    if (m_btnGateEnable && m_ui.gateEnabled >= 0)
        SendMessage(m_btnGateEnable, BM_SETCHECK,
                    m_ui.gateEnabled == 1 ? BST_CHECKED : BST_UNCHECKED, 0);
    if (m_comboFrameType && m_ui.frameType >= 0)
        SendMessage(m_comboFrameType, CB_SETCURSEL, m_ui.frameType, 0);
    if (m_ui.limitByFrames >= 0) {
        if (m_radioFrames)
            SendMessage(m_radioFrames, BM_SETCHECK,
                        m_ui.limitByFrames ? BST_CHECKED : BST_UNCHECKED, 0);
        if (m_radioTime)
            SendMessage(m_radioTime, BM_SETCHECK,
                        m_ui.limitByFrames ? BST_UNCHECKED : BST_CHECKED, 0);
    }
}

void ControlPanel::RebuildControls(int clientW, bool fromResize) {
    if (!m_panelHwnd) return;

    // Keep what the user typed before the windows holding it are gone.
    SnapshotUi();

    // Destroy all existing children
    m_ctrlSection.clear();
    m_sectionHeaders.clear();
    m_headerSection.clear();  // clear header→section map before rebuild
    EnumChildWindows(m_panelHwnd, [](HWND child, LPARAM) -> BOOL {
        DestroyWindow(child); return TRUE;
    }, 0);

    // Reset all HWND members
    m_lblGainVal = m_lblExpVal = m_lblGammaVal = nullptr;
    m_lblUSBVal  = m_lblWBRVal = m_lblWBBVal   = nullptr;
    m_sliderGain = m_sliderExposure = m_sliderGamma  = nullptr;
    m_sliderUSB  = m_sliderWBR = m_sliderWBB    = nullptr;
    m_lblFocusPos = m_lblFocusTemp = nullptr;
    m_btnFocusIn5000 = m_btnFocusIn100 = m_btnFocusStop = nullptr;
    m_btnFocusOut100 = m_btnFocusOut5000 = nullptr;
    m_btnFocusIn1000 = m_btnFocusIn10 = nullptr;
    m_btnFocusOut10  = m_btnFocusOut1000 = nullptr;
    m_editFocusGoto  = m_btnFocusGoto    = nullptr;
    m_btnAutofocus   = m_lblAFStatus     = nullptr;
    m_lblADCStatus   = nullptr;
    m_btnAEAuto      = m_lblAEStatus     = nullptr;
    m_lblMountRA = m_lblMountDec = m_lblMountTrack = m_lblMountControl = nullptr;
    m_lblMountAz = m_lblMountAlt = m_lblMountHA = m_lblMountConnected = nullptr;
    m_btnTracking = m_btnPark = m_btnMountStop = nullptr;
    m_btnJogN = m_btnJogS = m_btnJogE = m_btnJogW = nullptr;
    m_btnRate9 = m_btnRate8 = m_btnRate7 = m_btnRate5 = nullptr;
    m_btnMountCtrl = nullptr;
    m_btnSync    = nullptr;
    m_lblSkyMapLock = m_lblSkyMapTarget = nullptr;
    m_lblGoToStatus  = nullptr;
    m_editGoToTarget = m_btnGoToFind = nullptr;
    m_editFilePrefix = m_btnBrowseDir = m_editOutputDir = nullptr;
    m_comboFrameType = nullptr;
    m_btnRGBAlign = m_sliderRGBManual = m_lblRGBAlign = nullptr;
    m_btnFX = nullptr;
    m_btnNight = nullptr;
    m_btnChannel = nullptr;
    m_editLimitFrames = m_editLimitTime = nullptr;
    m_radioFrames = m_radioTime = nullptr;
    m_editGateThreshold = m_btnGateEnable = nullptr;
    m_btnCalDark = m_btnCalFlat = m_btnCalClear = m_lblCalStatus = nullptr;
    m_editNotes = nullptr;
    m_btnRecord  = nullptr;
    m_lblRecStatus = m_lblRecFrames = nullptr;
    m_lblDiskSpace = nullptr;
    m_btnDock = m_btnZoom = m_btnFullscreen = nullptr;
    m_comboProfile = nullptr;
    m_btnProfileLoad = m_btnProfileSave = m_btnProfileEdit = nullptr;

    // ── Layout constants ──────────────────────────────────────────
    // WS_VSCROLL takes ~17px from the right of the client area.
    // Reserve it so controls never draw behind the scrollbar.
    const int scrollW = GetSystemMetrics(SM_CXVSCROLL) + 2; // +2 gap
    const int x  = 10;
    const int w  = clientW - 10 - scrollW; // right edge = before scrollbar
    const int w2 = (w - 4) / 2;
    const int SH = 13;
    const int BH = 20;
    const int SL = 14;

    int y = 4;
    wchar_t secBuf[64];
    const int HD = 22;  // section header height — taller for circle icon

    auto sec = [&](const char* title) {
        if (y > 4) y += 3;
        swprintf(secBuf, 64, L"%hs", title);
        HWND hdr = CreateWindowExW(0, L"STATIC", secBuf,
            WS_CHILD | WS_VISIBLE | SS_OWNERDRAW | SS_NOTIFY,
            x, y, w, HD, m_panelHwnd, nullptr,
            GetModuleHandle(nullptr), nullptr);
        m_ctrlSection[hdr] = Section::Header;
        m_sectionHeaders.insert(hdr);
        y += HD + 2;
    };

    auto secC = [&](const char* title, Section s) {
        if (y > 4) y += 3;
        bool collapsed = IsCollapsed(s);
        // Store title + collapse state in label text for WM_DRAWITEM
        // Format: "TITLE" — WM_DRAWITEM reads m_collapsed to get chevron
        swprintf(secBuf, 64, L"%hs", title);
        HWND hdr = CreateWindowExW(0, L"STATIC", secBuf,
            WS_CHILD | WS_VISIBLE | SS_OWNERDRAW | SS_NOTIFY,
            x, y, w, HD, m_panelHwnd, nullptr,
            GetModuleHandle(nullptr), nullptr);
        m_ctrlSection[hdr] = s;
        m_sectionHeaders.insert(hdr);
        m_headerSection[hdr] = s;
        SetWindowSubclass(hdr, HeaderClickProc, (UINT_PTR)hdr, (DWORD_PTR)this);
        y += HD + 2;
    };

    // IMAGE section — zoom and fullscreen only
    secC("IMAGE", Section::Image);
    if (!IsCollapsed(Section::Image)) {
    {
        int vW = (w - 2) / 2;
        m_btnZoom       = MakeButton(L"ZOOM: FIT", x,      y, vW, BH, ID_BTN_ZOOM);
        m_btnFullscreen = MakeButton(L"FULLSCR",   x+vW+2, y, vW, BH, ID_BTN_FULLSCREEN);
        y += BH + 2;
    }
    // RGB Align — Phase 23
    {
        int btnW = 100;
        m_btnRGBAlign = MakeButton(L"RGB ALIGN", x, y, btnW, BH, ID_BTN_RGB_ALIGN);
        m_lblRGBAlign = MakeLabel(L"ALIGN  +0 px", x + btnW + 4, y, w - btnW - 4, SH);
        TagControl(m_lblRGBAlign, Section::Image);
        y += BH + 2;

        // Manual offset slider: range 0-40, center 20 = 0px offset
        TagControl(MakeLabel(L"Manual:", x, y, 50, SH), Section::Image);
        m_sliderRGBManual = MakeSlider(x+52, y, w-52, SL, 0, 40, 20, ID_SLIDER_RGB_MANUAL);
        y += SL + 3;
    }
    // FX + Night Vision buttons — side by side
    {
        int half = (w - 2) / 2;
        m_btnFX    = MakeButton(L"[FX] NORMAL", x,          y, half, BH, ID_BTN_FX);
        m_btnNight = MakeButton(L"[N] NIGHT",   x+half+2,   y, half, BH, ID_BTN_NIGHT);
        TagControl(m_btnFX,    Section::Image);
        TagControl(m_btnNight, Section::Image);
        y += BH + 2;
    }
    // Single-Channel Display button — full width
    m_btnChannel = MakeButton(L"[H] CH: ALL", x, y, w, BH, ID_BTN_CHANNEL);
    TagControl(m_btnChannel, Section::Image);
    y += BH + 2;
    // Phases 27 & 28: calibration frames. In IMAGE because what they
    // change is what you SEE -- the recording stays raw.
    {
        const int bw = (w - 4) / 3;
        m_btnCalDark  = MakeButton(L"DARK",  x,            y, bw, BH, ID_BTN_CAL_DARK);
        m_btnCalFlat  = MakeButton(L"FLAT",  x+bw+2,       y, bw, BH, ID_BTN_CAL_FLAT);
        m_btnCalClear = MakeButton(L"CLEAR", x+(bw+2)*2,   y, bw, BH, ID_BTN_CAL_CLEAR);
        TagControls({ m_btnCalDark, m_btnCalFlat, m_btnCalClear },
                    Section::Image);
        y += BH + 2;
        m_lblCalStatus = MakeValueLabel(m_calStatus.c_str(), x, y, w, SH,
                                        ID_LBL_CAL_STATUS);
        TagControl(m_lblCalStatus, Section::Image);
        y += SH + 2;
    }
    } // end Image
    m_btnDock = nullptr;

    // ── PROFILE ───────────────────────────────────────────────────
    // ── CAMERA ────────────────────────────────────────────────────
    secC("CAMERA", Section::Camera);
    if (!IsCollapsed(Section::Camera)) {
    // Value labels are right-aligned, 44px wide; slider fills between label text and value
    const int LBLW = 58;   // left label column width
    const int VALW = 44;   // right value label width
    auto makeSliderRow = [&](const wchar_t* lbl, int lo, int hi, int val, int id,
                              int lblId, HWND& slider, HWND& valLbl,
                              const wchar_t* initTxt) {
        TagControl(MakeLabel(lbl, x, y, LBLW, SH), Section::Camera);
        slider = MakeSlider(x+LBLW+2, y, w-LBLW-2-VALW-2, SL, lo, hi, val, id);
        valLbl = MakeValueLabel(initTxt, x+w-VALW, y, VALW, SH, lblId);
        TagControl(valLbl, Section::Camera);
        y += SL + 3;
    };

    // Use default initial values — ASICamera has no getters for current settings
    int initGain = 100, initExp = 80, initGamma = 50, initUSB = 80;
    wchar_t gainBuf[16], expBuf[16], gammaBuf[16], usbBuf[16];
    swprintf(gainBuf,  16, L"%d",    initGain);
    // Phase 71 (review): the slider is in MICROSECONDS, as the move
    // handler and its comment have always said. This line used 0.01
    // (hundredths of a millisecond), so the same slider position was
    // labelled one way at startup and ten times differently the
    // moment it was touched -- and the startup label was the wrong
    // one, since the camera follows the move handler.
    swprintf(expBuf,   16, L"%.2fms",(float)initExp / 1000.0f);
    swprintf(gammaBuf, 16, L"%d",    initGamma);
    swprintf(usbBuf,   16, L"%d%%",  initUSB);

    makeSliderRow(L"Gain",     0, 570,   initGain,  ID_SLIDER_GAIN,  ID_LBL_GAIN_VAL,  m_sliderGain,     m_lblGainVal,  gainBuf);
    // Range in microseconds. Was 1-10000, i.e. a 10 ms ceiling -- below
    // the 20-30 ms the help recommends for Saturn and the 50 ms the
    // auto-exposure assistant is allowed to reach, so neither could be
    // dialled in by hand and touching the slider would drag a correct
    // exposure back down. 60000 us = 60 ms covers both. Longer
    // exposures are set through the profile editor.
    makeSliderRow(L"Exposure", 1, 60000, initExp,   ID_SLIDER_EXP,   ID_LBL_EXP_VAL,   m_sliderExposure, m_lblExpVal,   expBuf);
    makeSliderRow(L"Gamma",    1, 100,   initGamma, ID_SLIDER_GAMMA, ID_LBL_GAMMA_VAL, m_sliderGamma,    m_lblGammaVal, gammaBuf);

    // WB row — two half-width sliders, tighter geometry
    {
        const int WBLW = 46;  // "WB Red" / "WB Blue" label width
        const int WBVW = 22;  // value label width
        int hw = (w - 4) / 2;
        TagControl(MakeLabel(L"WB Red",  x,      y, WBLW, SH), Section::Camera);
        m_sliderWBR = MakeSlider(x+WBLW, y, hw-WBLW-WBVW-1, SL, 0, 100, 52, ID_SLIDER_WBR);
        m_lblWBRVal = MakeValueLabel(L"52", x+hw-WBVW, y, WBVW, SH, ID_LBL_WBR_VAL);
        TagControl(m_lblWBRVal, Section::Camera);
        TagControl(MakeLabel(L"WB Blue", x+hw+2, y, WBLW, SH), Section::Camera);
        m_sliderWBB = MakeSlider(x+hw+2+WBLW, y, hw-WBLW-WBVW-1, SL, 0, 100, 47, ID_SLIDER_WBB);
        m_lblWBBVal = MakeValueLabel(L"47", x+w-WBVW, y, WBVW, SH, ID_LBL_WBB_VAL);
        TagControl(m_lblWBBVal, Section::Camera);
        y += SL + 3;
    }
    makeSliderRow(L"USB",      40, 100,  initUSB,   ID_SLIDER_USB,   ID_LBL_USB_VAL,   m_sliderUSB,      m_lblUSBVal,   usbBuf);
    TagControls({ m_sliderGain, m_sliderExposure, m_sliderGamma,
                  m_sliderUSB, m_sliderWBR, m_sliderWBB }, Section::Camera);

    // Phase 12 — Auto-Exposure row: [AE ADVISORY] label + [AUTO] toggle button
    {
        int btnW = 52;
        m_lblAEStatus = MakeValueLabel(L"AE: off", x, y, w - btnW - 2, SH, ID_LBL_AE_STATUS);
        TagControl(m_lblAEStatus, Section::Camera);
        m_btnAEAuto = MakeButton(L"AE OFF", x + w - btnW, y - 2, btnW, BH, ID_BTN_AE_AUTO);
        SetWindowTheme(m_btnAEAuto, L"", L"");
        TagControl(m_btnAEAuto, Section::Camera);
        y += BH + 2;
    }
    } // end Camera

    // ── FOCUSER ───────────────────────────────────────────────────
    secC("FOCUSER", Section::EAF);
    if (!IsCollapsed(Section::EAF)) {
    m_lblFocusPos = MakeValueLabel(L"0", x, y, w, 28, ID_LBL_FOCUS_POS);
    TagControl(m_lblFocusPos, Section::EAF);
    y += 30;
    {
        int bW5 = (w - 4) / 5;
        m_btnFocusIn5000  = MakeButton(L"\u00AB\u00AB", x,           y, bW5, BH, ID_BTN_FOCUS_IN5000);
        m_btnFocusIn100   = MakeButton(L"\u00AB",        x+bW5+1,     y, bW5, BH, ID_BTN_FOCUS_IN100);
        m_btnFocusStop    = MakeButton(L"STOP",           x+(bW5+1)*2, y, bW5, BH, ID_BTN_FOCUS_STOP);
        m_btnFocusOut100  = MakeButton(L"\u00BB",        x+(bW5+1)*3, y, bW5, BH, ID_BTN_FOCUS_OUT100);
        m_btnFocusOut5000 = MakeButton(L"\u00BB\u00BB", x+(bW5+1)*4, y, bW5, BH, ID_BTN_FOCUS_OUT5000);
        y += BH + 2;
        m_btnFocusIn1000  = MakeButton(L"< 1K",  x,           y, bW5, BH, ID_BTN_FOCUS_IN1000);
        m_btnFocusIn10    = MakeButton(L"< 10",  x+bW5+1,     y, bW5, BH, ID_BTN_FOCUS_IN10);
        m_editFocusGoto   = MakeEditBox(L"0", x+(bW5+1)*2, y-1, bW5, BH, ID_EDIT_FOCUS_GOTO);
        m_btnFocusOut10   = MakeButton(L"10 >",  x+(bW5+1)*3, y, bW5, BH, ID_BTN_FOCUS_OUT10);
        m_btnFocusOut1000 = MakeButton(L"1K >",  x+(bW5+1)*4, y, bW5, BH, ID_BTN_FOCUS_OUT1000);
        y += BH + 1;
    }
    TagControl(MakeLabel(L"TEMP:", x, y, 34, SH), Section::EAF);
    m_lblFocusTemp = MakeValueLabel(L"--\u00B0C", x+36, y, 50, SH, ID_LBL_FOCUS_TEMP);
    m_btnFocusGoto = MakeButton(L"GO TO", x+w-52, y-2, 52, BH, ID_BTN_FOCUS_GOTO);
    y += BH + 2;

    m_btnAutofocus = MakeButton(L"AUTO FOCUS", x, y, w, BH+2, ID_BTN_AUTOFOCUS);
    SetWindowLongW(m_btnAutofocus, GWL_STYLE,
        GetWindowLongW(m_btnAutofocus, GWL_STYLE) & ~BS_PUSHBUTTON | BS_OWNERDRAW);
    SetWindowTheme(m_btnAutofocus, L"", L"");
    y += BH + 4;
    m_lblAFStatus = MakeValueLabel(L"", x, y, w, SH, ID_LBL_AF_STATUS);
    TagControl(m_lblAFStatus, Section::EAF);
    y += SH + 2;

    // ADC dispersion readout — updates live from mount altitude
    m_lblADCStatus = MakeValueLabel(L"ADC: --", x, y, w, SH, ID_LBL_ADC_STATUS);
    TagControl(m_lblADCStatus, Section::EAF);
    y += SH + 2;

    // Phase 83: the band the figure above was computed over. The ADC
    // number is meaningless without it — the same altitude disperses
    // 1.6" through a UV/IR cut and 0.4" through an IR pass filter —
    // and the filter is a physical part that leaves with the camera,
    // so the app cannot observe which one is fitted. Naming it beside
    // the figure it drives is what makes a wrong setting visible.
    m_lblFilter = MakeValueLabel(L"", x, y, w, SH, ID_LBL_FILTER);
    TagControl(m_lblFilter, Section::EAF);
    RefreshFilterLabel();
    y += SH + 2;
    } // end Focuser

    // ── TELESCOPE — Scope Controls (SharpCap-style) ──────────────
    secC("TELESCOPE", Section::Mount);
    if (!IsCollapsed(Section::Mount)) {

    // ── Device name + connected status ────────────────────────
    // "● Celestron 8SE (NexStar)  COM3   Connected"
    {
        bool connected = m_mount && m_mount->IsConnected();
        wchar_t devLine[128];
        if (connected) {
            std::wstring cid;
            auto cs = m_mount->ConnectionId();
            cid.assign(cs.begin(), cs.end());
            swprintf(devLine, 128, L"\u25CF  Celestron 8SE  %s", cid.c_str());
        } else {
            wcscpy_s(devLine, 128, L"\u25CB  Celestron 8SE  (not connected)");
        }
        m_lblMountConnected = MakeLabel(devLine, x, y, w, SH, ID_LBL_MOUNT_CONNECTED);
        TagControl(m_lblMountConnected, Section::Mount);
        y += SH + 3;
    }

    // ── Coordinate readout grid (left ~55%, right = rate+jog) ─
    // Layout:
    //  Col A: label (26px)  Col B: value (fills to midpoint)
    //  Right block: rate buttons stacked + d-pad + STOP
    //
    // Left column: Az / Alt / RA / Dec / HA  (5 rows × (SH+1))
    // Right block starts at x + coordW + 6, width = w - coordW - 6

    const int LBLC  = 26;          // coordinate label column width
    const int VALC  = w - LBLC - 4; // value column fills remaining width
    const int ROWH  = SH + 2;

    // Helper — make one coord row: label + monospace value
    auto coordRow = [&](const wchar_t* lbl, HWND& val, int id) {
        TagControl(MakeLabel(lbl, x, y, LBLC, SH), Section::Mount);
        val = MakeValueLabel(L"--:--:--", x + LBLC, y, VALC, SH, id);
        TagControl(val, Section::Mount);
        y += ROWH;
    };

    coordRow(L"Az",  m_lblMountAz,  ID_LBL_MOUNT_AZ);
    coordRow(L"Alt", m_lblMountAlt, ID_LBL_MOUNT_ALT);
    coordRow(L"RA",  m_lblMountRA,  ID_LBL_MOUNT_RA);
    coordRow(L"Dec", m_lblMountDec, ID_LBL_MOUNT_DEC);
    coordRow(L"HA",  m_lblMountHA,  ID_LBL_MOUNT_HA);
    y += 3;

    // MOUNT CTRL button — shows/repositions the floating mount controller
    // The floating controller has all jog/stop/rate/tracking/park controls
    {
        // Four across: MOUNT CTRL | CABLES OK | SETUP | READY.
        // At PANEL_WIDTH 560 that is about 131 px each, comfortable for
        // the longest label at this font. The panel width is fixed, so
        // this cannot be squeezed by resizing the window.
        int hw = (w - 6) / 4;
        m_btnMountCtrl = MakeButton(L"MOUNT CTRL", x, y, hw, BH, ID_BTN_MOUNT_CTRL);
        TagControl(m_btnMountCtrl, Section::Mount);
        // Phase 67: the other half of this row was empty. CABLES resets
        // the cord-wrap total after the cables have been physically
        // freed - press it when you dress them, so the count that warns
        // you later is measured from a known-good state.
        m_btnCables = MakeButton(L"CABLES OK", x + hw + 2, y, hw, BH, ID_BTN_CABLES);
        TagControl(m_btnCables, Section::Mount);
        // Orientation, keep-out sector and wrap limit, drawn so the
        // stored numbers can be checked against the actual garden.
        m_btnRigSetup = MakeButton(L"SETUP", x + (hw + 2) * 2, y, hw, BH,
                                   ID_BTN_RIGSETUP);
        TagControl(m_btnRigSetup, Section::Mount);
        // Phase 72: reopens the readiness check. Startup shows it only
        // on a failure, which is correct, but a failed check is usually
        // something you go and fix -- and then want to re-test without
        // relaunching.
        m_btnReady = MakeButton(L"READY", x + (hw + 2) * 3, y, hw, BH,
                                ID_BTN_READY);
        TagControl(m_btnReady, Section::Mount);
        y += BH + 2;
    }

    // ── Tracking status label + PLANET LOCK / SKYMAP ──────────
    // Row: TRK [status]   [●SKYMAP LOCKED]   [PLANET LOCK btn]
    const int lockIndW = w - 64 - 90 - 4;
    TagControl(MakeLabel(L"TRK", x, y, 22, SH), Section::Mount);
    m_lblMountTrack = MakeValueLabel(L"OFF", x+24, y, 40, SH, ID_LBL_MOUNT_TRACK);
    TagControl(m_lblMountTrack, Section::Mount);
    m_lblSkyMapLock = MakeLabel(L"", x+64, y, lockIndW, SH, ID_LBL_SKYMAP_LOCK);
    TagControl(m_lblSkyMapLock, Section::Mount);
    m_lblMountControl = MakeLabel(L"", x+70, y, w-70-90, SH, ID_LBL_MOUNT_CONTROL);
    TagControl(m_lblMountControl, Section::Mount);
    m_btnSync = CreateWindowExW(0, L"BUTTON", L"PLANET LOCK",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        x+w-90, y-2, 90, BH, m_panelHwnd,
        (HMENU)(INT_PTR)ID_BTN_SYNC, GetModuleHandle(nullptr), nullptr);
    SetWindowTheme(m_btnSync, L"", L"");
    y += BH + 1;

    m_lblSkyMapTarget = MakeValueLabel(L"", x+w-90, y, 90, SH, ID_LBL_SKYMAP_TARGET);
    TagControl(m_lblSkyMapTarget, Section::Mount);
    y += SH + 2;

    if (m_skyMapLocked) {
        SetWindowTextW(m_lblSkyMapLock,   L"\u25CF  SKYMAP LOCKED");
        SetWindowTextW(m_lblSkyMapTarget, m_skyMapTarget.c_str());
    }

    // ── GoTo target + plate solve ──────────────────────────────
    TagControl(MakeLabel(L"TGT:", x, y, 28, SH), Section::Mount);
    m_editGoToTarget = MakeEditBox(L"Jupiter", x+32, y-2, w-32-60, BH, ID_EDIT_GOTO_TARGET);
    SetWindowLong(m_editGoToTarget, GWL_STYLE,
        GetWindowLong(m_editGoToTarget, GWL_STYLE) & ~ES_NUMBER);
    m_btnGoToFind = MakeButton(L"GOTO", x+w-56, y-2, 56, BH, ID_BTN_GOTO_FIND);
    y += BH + 3;

    m_btnSolveSync = MakeButton(L"POINT SOLVE \u2192 SYNC", x, y, w, BH, ID_BTN_SOLVE_SYNC);
    y += BH + 2;
    m_lblGoToStatus = MakeValueLabel(L"", x, y, w, SH, ID_LBL_GOTO_STATUS);
    TagControl(m_lblGoToStatus, Section::Mount);
    y += SH + 2;
    } // end Telescope

    // ── CAPTURE ───────────────────────────────────────────────────
    secC("CAPTURE", Section::Capture);
    if (!IsCollapsed(Section::Capture)) {
    // Phase 85: the target picker moved in here from its own PROFILE
    // section. The two described the same thing from opposite ends -- a
    // profile carries a file prefix and a frame limit, and the boxes
    // below are a file prefix and a frame limit -- and because nothing
    // pushed the profile's copies into these boxes, the boxes silently
    // won every time. One header makes that overlap visible instead of
    // hiding it behind two collapsed ones.
    {
        const int BTN_W  = 48;
        const int BTN_GAP = 2;
        const int LBL_W  = 42;
        const int btnsTotalW = BTN_W * 3 + BTN_GAP * 2;
        TagControl(MakeLabel(L"Target:", x, y, LBL_W, SH), Section::Capture);
        int cbX = x + LBL_W + 2;
        int cbW = w - LBL_W - 2 - btnsTotalW - 4;
        m_comboProfile   = MakeComboBox(cbX, y-1, cbW, 140, ID_COMBO_PROFILE);
        TagControl(m_comboProfile, Section::Capture);
        int bx = cbX + cbW + 4;
        m_btnProfileLoad = MakeButton(L"LOAD", bx,                     y, BTN_W, BH, ID_BTN_PROFILE_LOAD);
        m_btnProfileSave = MakeButton(L"SAVE", bx + BTN_W + BTN_GAP,   y, BTN_W, BH, ID_BTN_PROFILE_SAVE);
        m_btnProfileEdit = MakeButton(L"EDIT", bx + (BTN_W+BTN_GAP)*2, y, BTN_W, BH, ID_BTN_PROFILE_EDIT);
        TagControls({ m_btnProfileLoad, m_btnProfileSave, m_btnProfileEdit },
                    Section::Capture);
        y += BH + 3;
    }
    TagControl(MakeLabel(L"Prefix:", x, y, 42, SH), Section::Capture);
    m_editFilePrefix = MakeEditBox(L"Jupiter", x+44, y-2, w-44-22, BH, ID_EDIT_FILE_PREFIX);
    SetWindowLong(m_editFilePrefix, GWL_STYLE,
        GetWindowLong(m_editFilePrefix, GWL_STYLE) & ~ES_NUMBER);
    m_btnBrowseDir = MakeButton(L"...", x+w-20, y-2, 20, BH, ID_BTN_BROWSE_DIR);
    y += BH + 1;

    // Frame type dropdown — Light / Dark / Flat / Bias
    {
        int lblW = 32;
        TagControl(MakeLabel(L"Type:", x, y, lblW, SH), Section::Capture);
        m_comboFrameType = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            x + lblW + 2, y - 2, w - lblW - 2, 80, m_panelHwnd,
            (HMENU)(INT_PTR)ID_COMBO_FRAME_TYPE, GetModuleHandle(nullptr), nullptr);
        if (m_comboFrameType) {
            // Apply panel font immediately so items render at correct size
            HFONT hf = (HFONT)SendMessage(m_panelHwnd, WM_GETFONT, 0, 0);
            if (hf) SendMessage(m_comboFrameType, WM_SETFONT, (WPARAM)hf, FALSE);
            SendMessage(m_comboFrameType, CB_ADDSTRING, 0, (LPARAM)L"Light");
            SendMessage(m_comboFrameType, CB_ADDSTRING, 0, (LPARAM)L"Dark");
            SendMessage(m_comboFrameType, CB_ADDSTRING, 0, (LPARAM)L"Flat");
            SendMessage(m_comboFrameType, CB_ADDSTRING, 0, (LPARAM)L"Bias");
            SendMessage(m_comboFrameType, CB_SETCURSEL, 0, 0);
        }
        TagControl(m_comboFrameType, Section::Capture);
        y += BH + 1;
    }
    m_editOutputDir = MakeEditBox(L"D:\\RadukCapture\\captures",
                                   x, y-1, w, BH-2, ID_EDIT_OUTPUT_DIR);
    SetWindowLong(m_editOutputDir, GWL_STYLE,
        GetWindowLong(m_editOutputDir, GWL_STYLE) & ~ES_NUMBER);
    y += BH + 1;
    {
        m_radioFrames = CreateWindowExW(0, L"BUTTON", L"Frames:",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
            x, y, 50, SH, m_panelHwnd,
            (HMENU)(INT_PTR)ID_RADIO_FRAMES, GetModuleHandle(nullptr), nullptr);
        m_editLimitFrames = MakeEditBox(L"1000", x+52, y-1, 52, BH-2, ID_EDIT_LIMIT_FRAMES);
        m_radioTime = CreateWindowExW(0, L"BUTTON", L"Sec:",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            x+110, y, 34, SH, m_panelHwnd,
            (HMENU)(INT_PTR)ID_RADIO_TIME, GetModuleHandle(nullptr), nullptr);
        m_editLimitTime = MakeEditBox(L"60", x+146, y-1, w-146, BH-2, ID_EDIT_LIMIT_TIME);
        SendMessage(m_radioFrames, BM_SETCHECK, BST_CHECKED, 0);
        y += BH + 2;
    }
    // Quality gate row
    {
        TagControl(MakeLabel(L"MIN SEEING:", x, y, 72, SH), Section::Default);
        m_editGateThreshold = MakeEditBox(L"0.0", x+74, y-1, 38, BH-2, ID_EDIT_GATE_THRESHOLD);
        TagControl(MakeLabel(L"/10", x+114, y, 22, SH), Section::Default);
        m_btnGateEnable = CreateWindowExW(0, L"BUTTON", L"GATE",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            x+140, y, 46, SH, m_panelHwnd,
            (HMENU)(INT_PTR)ID_BTN_GATE_ENABLE, GetModuleHandle(nullptr), nullptr);
        SetWindowTheme(m_btnGateEnable, L"", L"");
        y += BH + 2;
    }
    m_btnRecord = MakeButton(L"\u25BA  RECORD", x, y, w, BH+8, ID_BTN_RECORD);
    SetWindowLongW(m_btnRecord, GWL_STYLE,
        GetWindowLongW(m_btnRecord, GWL_STYLE) & ~BS_PUSHBUTTON | BS_OWNERDRAW);
    SetWindowTheme(m_btnRecord, L"", L"");
    y += BH + 10;
    } // end Capture

    // ── NOTES ────────────────────────────────────────────────────
    secC("NOTES  (saved with profile)", Section::Notes);
    if (!IsCollapsed(Section::Notes)) {
    m_editNotes = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
        x, y, w, 72, m_panelHwnd,
        (HMENU)(INT_PTR)ID_EDIT_NOTES, GetModuleHandle(nullptr), nullptr);
    TagControl(m_editNotes, Section::Notes);
    y += 72 + 4;
    } // end Notes

    // Tag sections
    TagControls({ m_lblGainVal, m_lblExpVal, m_lblGammaVal,
                  m_lblUSBVal, m_lblWBRVal, m_lblWBBVal }, Section::Camera);
    TagControls({ m_lblFocusPos, m_lblFocusTemp }, Section::EAF);
    TagControls({ m_lblMountRA, m_lblMountDec,
                  m_lblMountTrack, m_lblMountControl }, Section::Mount);

    m_contentBottom = y + 4;
    int totalH = m_contentBottom + GdiStripHeight() + 8;  // controls + dynamic GDI strip

    // ── Scroll bar setup ──────────────────────────────────────
    // During Create(), GetClientRect may return 0 height because the panel
    // hasn't been laid out yet. Fall back to the main window client height.
    {
        RECT cr; GetClientRect(m_panelHwnd, &cr);
        int panelH = cr.bottom;
        if (panelH < 1) {
            RECT mr; GetClientRect(m_mainHwnd, &mr);
            panelH = mr.bottom;
        }
        if (panelH < 1) panelH = 800; // safe fallback

        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin   = 0;
        si.nMax   = totalH;
        si.nPage  = (UINT)panelH;
        si.nPos   = 0;
        SetScrollInfo(m_panelHwnd, SB_VERT, &si, TRUE);

        // Show scrollbar only when content exceeds panel height
        ShowScrollBar(m_panelHwnd, SB_VERT, totalH > panelH);

        // Always reset scroll offset on rebuild — controls start at y=4
        m_scrollY = 0;
    }

    UpdatePlanetLockUI();

    // Apply fonts to all newly created controls
    HFONT hPanelFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Barlow Condensed");
    if (hPanelFont) {
        SendMessage(m_panelHwnd, WM_SETFONT, (WPARAM)hPanelFont, TRUE);
        EnumChildWindows(m_panelHwnd, [](HWND child, LPARAM lp) -> BOOL {
            SendMessage(child, WM_SETFONT, (WPARAM)lp, FALSE);
            return TRUE;
        }, (LPARAM)hPanelFont);
    }
    HFONT hHdrFont = CreateFontW(-12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Barlow Condensed");
    if (hHdrFont) {
        for (HWND hdr : m_sectionHeaders)
            SendMessage(hdr, WM_SETFONT, (WPARAM)hHdrFont, FALSE);
        for (auto& kv : m_ctrlSection)
            if (kv.second == Section::Header)
                SendMessage(kv.first, WM_SETFONT, (WPARAM)hHdrFont, FALSE);
    }

    // Strip UxTheme from all buttons/edits/combos
    EnumChildWindows(m_panelHwnd, [](HWND child, LPARAM) -> BOOL {
        wchar_t cls[32] = {};
        GetClassNameW(child, cls, 32);
        if (wcscmp(cls, L"Button")   == 0 ||
            wcscmp(cls, L"Edit")     == 0 ||
            wcscmp(cls, L"ComboBox") == 0)
            SetWindowTheme(child, L"", L"");
        return TRUE;
    }, 0);

    // Re-push cached status strings — HWNDs are new after rebuild
    if (!m_lastGoToStatus.empty() && m_lblGoToStatus)
        SetWindowTextW(m_lblGoToStatus, m_lastGoToStatus.c_str());
    if (m_afRunning && m_btnAutofocus)
        SetWindowTextW(m_btnAutofocus, L"ABORT AF");

    // Re-populate profile combo — destroyed and recreated on every rebuild
    if (m_comboProfile && !m_cachedProfileNames.empty()) {
        SendMessageA(m_comboProfile, CB_RESETCONTENT, 0, 0);
        for (const auto& n : m_cachedProfileNames)
            SendMessageA(m_comboProfile, CB_ADDSTRING, 0, (LPARAM)n.c_str());
        SendMessage(m_comboProfile, CB_SETCURSEL, (WPARAM)m_cachedProfileIndex, 0);
    }

    // Same idea as the cached status strings above, for everything the
    // user typed or chose. Must come last: the controls have to exist.
    RestoreUi();
}

} // namespace raduk::gui
