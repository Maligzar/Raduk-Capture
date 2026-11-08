#pragma once
// ============================================================
// StripDraw.h -- phase 132.3f: list headers and tab strips, drawn
//
// The last of 119.4's open item. Buttons went owner-drawn there and in
// 132.3b; what stayed system grey were the column headers of every list
// view and the GoTo window's tab strip. Neither can simply be coloured:
//
//   A list's header is a separate control that notifies the LIST, not
//   the window, and once de-themed it paints COLOR_BTNFACE. So each
//   list is subclassed to answer its header's NM_CUSTOMDRAW: every
//   column is drawn as an engraved legend on Panel -- Dim, upper case,
//   tracked a pixel, a hairline under it -- and the strip past the last
//   column is filled at post-paint.
//
//   A tab strip de-themed falls back to classic grey. So it is
//   subclassed and painted whole: Panel, each tab's word in Dim, the
//   open tab in Text with the signal colour's bar under it -- shape,
//   not hue, as the control panel's own tabs do -- and a hairline
//   along the bottom. Clicks and keys are still the control's own.
//
// Phase 132.6: the selected row. A de-themed list paints its selection
// in COLOR_HIGHLIGHT, the system's blue, in day mode and at night: a
// bright blue bar on a red screen. Item custom draw goes to the list's
// PARENT, not the list, so the parent is subclassed too, answering only
// NM_CUSTOMDRAW from its own list views. A selected row is painted
// whole -- BORDER, the signal colour's 2 px bar at its left edge, every
// column's text in Text, and the check box where a list has them --
// and the control is told to skip its own. Unselected rows are left to
// the control, which already paints them from the tokens.
//
// Painting the row rather than unsetting its selected state for the
// paint: the usual trick of clearing LVIS_SELECTED and restoring it
// would send LVN_ITEMCHANGED to GoTo's own handlers mid-paint.
//
// Found by class, like Theme::StyleStockControls, so a list or a tab
// strip added later is styled without being registered anywhere.
// Installing twice is harmless: SetWindowSubclass with the same id
// replaces. Every colour goes through Theme::NV or Theme::Brush, so
// night vision reaches both, and tests/night_coverage.cpp scans this
// file with the rest of src/gui.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <cwchar>

#include "Theme.h"

#pragma comment(lib, "comctl32.lib")

namespace raduk::gui {

namespace strip_detail {

constexpr UINT_PTR kHeaderSubclassId = 0x1323F01;
constexpr UINT_PTR kTabSubclassId    = 0x1323F02;
constexpr UINT_PTR kListParentSubclassId = 0x1326A01;   // 132.6

inline void HLine(HDC dc, int x0, int x1, int y, COLORREF c) {
    HPEN p = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ o = SelectObject(dc, p);
    MoveToEx(dc, x0, y, nullptr);
    LineTo(dc, x1, y);
    SelectObject(dc, o);
    DeleteObject(p);
}

// A legend: upper case, tracked a pixel, in the colour given.
inline void Legend(HDC dc, const wchar_t* text, RECT r, COLORREF c, UINT align) {
    wchar_t up[128] = {};
    wcsncpy_s(up, text ? text : L"", _TRUNCATE);
    CharUpperBuffW(up, (DWORD)wcslen(up));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    const int oldExtra = SetTextCharacterExtra(dc, 1);
    DrawTextW(dc, up, -1, &r, align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SetTextCharacterExtra(dc, oldExtra);
}

// The list's own proc, answering its header's custom draw.
inline LRESULT CALLBACK ListProc(HWND h, UINT m, WPARAM w, LPARAM l,
                                 UINT_PTR, DWORD_PTR) {
    if (m == WM_NOTIFY) {
        auto* nh = reinterpret_cast<NMHDR*>(l);
        HWND hdr = ListView_GetHeader(h);
        if (nh && hdr && nh->hwndFrom == hdr && nh->code == NM_CUSTOMDRAW) {
            auto* cd = reinterpret_cast<NMCUSTOMDRAW*>(l);
            switch (cd->dwDrawStage) {
                case CDDS_PREPAINT:
                    return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
                case CDDS_ITEMPREPAINT: {
                    RECT r = cd->rc;
                    FillRect(cd->hdc, &r, Theme::Brush(Theme::Rgb::PANEL));
                    wchar_t text[96] = {};
                    HDITEMW it = {};
                    it.mask = HDI_TEXT | HDI_FORMAT;
                    it.pszText = text;
                    it.cchTextMax = 96;
                    Header_GetItem(hdr, (int)cd->dwItemSpec, &it);
                    RECT tr = r;
                    tr.left += Theme::Space::SM;
                    tr.right -= Theme::Space::SM;
                    const UINT align = (it.fmt & HDF_RIGHT)  ? DT_RIGHT
                                     : (it.fmt & HDF_CENTER) ? DT_CENTER : DT_LEFT;
                    Legend(cd->hdc, text, tr, Theme::NV(Theme::Rgb::TEXT_DIM), align);
                    // A hairline between columns, short of the edges.
                    const int x = r.right - 1;
                    HPEN p = CreatePen(PS_SOLID, 1, Theme::NV(Theme::Rgb::BORDER));
                    HGDIOBJ o = SelectObject(cd->hdc, p);
                    MoveToEx(cd->hdc, x, r.top + 4, nullptr);
                    LineTo(cd->hdc, x, r.bottom - 4);
                    SelectObject(cd->hdc, o);
                    DeleteObject(p);
                    HLine(cd->hdc, r.left, r.right, r.bottom - 1, Theme::NV(Theme::Rgb::BORDER));
                    return CDRF_SKIPDEFAULT;
                }
                case CDDS_POSTPAINT: {
                    // The strip past the last column.
                    RECT all;
                    GetClientRect(hdr, &all);
                    const int n = Header_GetItemCount(hdr);
                    int right = all.left;
                    if (n > 0) {
                        RECT last;
                        if (Header_GetItemRect(hdr, Header_OrderToIndex(hdr, n - 1), &last))
                            right = last.right;
                    }
                    if (right < all.right) {
                        RECT rest = { right, all.top, all.right, all.bottom };
                        FillRect(cd->hdc, &rest, Theme::Brush(Theme::Rgb::PANEL));
                        HLine(cd->hdc, right, all.right, all.bottom - 1,
                              Theme::NV(Theme::Rgb::BORDER));
                    }
                    return CDRF_DODEFAULT;
                }
                default:
                    return CDRF_DODEFAULT;
            }
        }
    } else if (m == WM_NCDESTROY) {
        RemoveWindowSubclass(h, ListProc, kHeaderSubclassId);
    }
    return DefSubclassProc(h, m, w, l);
}

// One selected row of `lv`, painted whole from the tokens.
inline void PaintSelectedRow(HWND lv, HDC dc, int item) {
    RECT row;
    if (!ListView_GetItemRect(lv, item, &row, LVIR_BOUNDS)) return;
    FillRect(dc, &row, Theme::Brush(Theme::Rgb::BORDER));
    RECT bar = { row.left, row.top, row.left + 2, row.bottom };
    FillRect(dc, &bar, Theme::Brush(Theme::Rgb::ACCENT));

    if (ListView_GetExtendedListViewStyle(lv) & LVS_EX_CHECKBOXES) {
        RECT lab;
        if (ListView_GetItemRect(lv, item, &lab, LVIR_LABEL) && lab.left > row.left + 4) {
            const int side = 11;
            const int cx = (row.left + lab.left) / 2, cy = (row.top + row.bottom) / 2;
            RECT box = { cx - side / 2, cy - side / 2, cx - side / 2 + side, cy - side / 2 + side };
            FrameRect(dc, &box, Theme::Brush(Theme::Rgb::TEXT_DIM));
            if (ListView_GetCheckState(lv, item)) {
                InflateRect(&box, -3, -3);
                FillRect(dc, &box, Theme::Brush(Theme::Rgb::TEXT));
            }
        }
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, Theme::NV(Theme::Rgb::TEXT));
    HWND hdr = ListView_GetHeader(lv);
    const int cols = hdr ? Header_GetItemCount(hdr) : 1;
    for (int c = 0; c < (cols > 0 ? cols : 1); ++c) {
        RECT cr;
        if (c == 0) {
            if (!ListView_GetItemRect(lv, item, &cr, LVIR_LABEL)) continue;
            cr.left += 2;
        } else {
            if (!ListView_GetSubItemRect(lv, item, c, LVIR_BOUNDS, &cr)) continue;
            cr.left += 6;
        }
        cr.right -= 6;
        if (cr.right <= cr.left) continue;
        wchar_t text[256] = {};
        ListView_GetItemText(lv, item, c, text, 256);
        LVCOLUMNW col = {};
        col.mask = LVCF_FMT;
        UINT align = DT_LEFT;
        if (ListView_GetColumn(lv, c, &col)) {
            const int f = col.fmt & LVCFMT_JUSTIFYMASK;
            align = (f == LVCFMT_RIGHT) ? DT_RIGHT : (f == LVCFMT_CENTER) ? DT_CENTER : DT_LEFT;
        }
        DrawTextW(dc, text, -1, &cr,
                  align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
}

// The parent of a list view, answering its lists' item custom draw.
inline LRESULT CALLBACK ListParentProc(HWND h, UINT m, WPARAM w, LPARAM l,
                                       UINT_PTR, DWORD_PTR) {
    if (m == WM_NOTIFY) {
        auto* nh = reinterpret_cast<NMHDR*>(l);
        if (nh && nh->code == NM_CUSTOMDRAW && nh->hwndFrom && GetParent(nh->hwndFrom) == h) {
            wchar_t cls[32] = {};
            if (GetClassNameW(nh->hwndFrom, cls, 32) > 0 && _wcsicmp(cls, WC_LISTVIEWW) == 0) {
                auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(l);
                switch (cd->nmcd.dwDrawStage) {
                    case CDDS_PREPAINT:
                        return CDRF_NOTIFYITEMDRAW;
                    case CDDS_ITEMPREPAINT: {
                        const int item = (int)cd->nmcd.dwItemSpec;
                        if (ListView_GetItemState(nh->hwndFrom, item, LVIS_SELECTED)) {
                            PaintSelectedRow(nh->hwndFrom, cd->nmcd.hdc, item);
                            return CDRF_SKIPDEFAULT;
                        }
                        return CDRF_DODEFAULT;
                    }
                    default:
                        return CDRF_DODEFAULT;
                }
            }
        }
    } else if (m == WM_NCDESTROY) {
        RemoveWindowSubclass(h, ListParentProc, kListParentSubclassId);
    }
    return DefSubclassProc(h, m, w, l);
}

// The tab strip, painted whole.
inline LRESULT CALLBACK TabProc(HWND h, UINT m, WPARAM w, LPARAM l,
                                UINT_PTR, DWORD_PTR) {
    switch (m) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT all;
            GetClientRect(h, &all);
            FillRect(dc, &all, Theme::Brush(Theme::Rgb::PANEL));
            HFONT f = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
            HGDIOBJ of = f ? SelectObject(dc, f) : nullptr;
            const int n = TabCtrl_GetItemCount(h);
            const int sel = TabCtrl_GetCurSel(h);
            int bottom = all.top;
            for (int i = 0; i < n; ++i) {
                RECT r;
                if (!TabCtrl_GetItemRect(h, i, &r)) continue;
                if (r.bottom > bottom) bottom = r.bottom;
                wchar_t text[64] = {};
                TCITEMW ti = {};
                ti.mask = TCIF_TEXT;
                ti.pszText = text;
                ti.cchTextMax = 64;
                TabCtrl_GetItem(h, i, &ti);
                Legend(dc, text, r,
                       Theme::NV(i == sel ? Theme::Rgb::TEXT : Theme::Rgb::TEXT_DIM), DT_CENTER);
                if (i == sel) {
                    RECT bar = { r.left + 2, r.bottom - 3, r.right - 2, r.bottom };
                    FillRect(dc, &bar, Theme::Brush(Theme::Rgb::ACCENT));
                }
            }
            if (bottom > all.top)
                HLine(dc, all.left, all.right, bottom, Theme::NV(Theme::Rgb::BORDER));
            if (of) SelectObject(dc, of);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(h, TabProc, kTabSubclassId);
            break;
    }
    return DefSubclassProc(h, m, w, l);
}

} // namespace strip_detail

// Every list header and tab strip under `top`. Call with the stock
// controls' styling, and again on a night-vision toggle.
inline void StyleStrips(HWND top) {
    if (!top) return;
    EnumChildWindows(top, [](HWND c, LPARAM) -> BOOL {
        wchar_t cls[64] = {};
        if (GetClassNameW(c, cls, 64) <= 0) return TRUE;
        if (_wcsicmp(cls, WC_LISTVIEWW) == 0) {
            SetWindowSubclass(c, strip_detail::ListProc, strip_detail::kHeaderSubclassId, 0);
            if (HWND parent = GetParent(c))   // 132.6: the selected row
                SetWindowSubclass(parent, strip_detail::ListParentProc,
                                  strip_detail::kListParentSubclassId, 0);
            InvalidateRect(c, nullptr, FALSE);
            if (HWND hdr = ListView_GetHeader(c)) InvalidateRect(hdr, nullptr, TRUE);
        } else if (_wcsicmp(cls, WC_TABCONTROLW) == 0) {
            SetWindowSubclass(c, strip_detail::TabProc, strip_detail::kTabSubclassId, 0);
            InvalidateRect(c, nullptr, TRUE);
        }
        return TRUE;
    }, 0);
}

} // namespace raduk::gui
