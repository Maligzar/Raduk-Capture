#pragma once
// ============================================================
// FlatButton.h -- the owner-drawn button face every operator window
// shares
//
// Written for the control panel in phase 119.4, where it lived inside
// ControlPanel's WM_DRAWITEM. Phase 132.3b moved it here so the GoTo
// window, whose buttons were still stock system grey, paints the same
// face rather than a second copy of it.
//
// A stock button paints COLOR_BTNFACE whatever it is told, which is a
// white block on a dark panel and the one thing night vision could not
// reach. This face is the panel, a hairline frame with a lit top edge
// (the instrument look, 132.3b), the word in primary text, and the
// label's icon from ButtonIcons.h. Pressed and disabled read from fill
// and text weight, so both survive the dark.
// ============================================================

#include "gui/ButtonIcons.h"
#include "gui/Theme.h"

namespace raduk::gui {

// `openTab`: draw the open-tab bar under it, which is shape rather than
// hue so it survives night vision. Only the control panel's tab strip
// sets it.
inline void DrawFlatButton(const DRAWITEMSTRUCT* dis, bool openTab = false) {
    HDC  hdc = dis->hDC;
    RECT r   = dis->rcItem;
    const bool down = (dis->itemState & ODS_SELECTED) != 0;
    const bool off  = (dis->itemState & ODS_DISABLED) != 0;
    FillRect(hdc, &r, Theme::Brush(down ? Theme::Rgb::BORDER : Theme::Rgb::PANEL));
    FrameRect(hdc, &r, Theme::Brush(Theme::Rgb::BORDER));

    // The lit top edge, inside the frame: a raised face, not a flat
    // box. Not on a pressed button, which is the point of pressing.
    if (!down && r.right - r.left > 4) {
        HPEN lit = CreatePen(PS_SOLID, 1, Theme::HighlightOver(Theme::Rgb::PANEL, 1.0f));
        HPEN op  = (HPEN)SelectObject(hdc, lit);
        MoveToEx(hdc, r.left + 1, r.top + 1, nullptr);
        LineTo(hdc, r.right - 1, r.top + 1);
        SelectObject(hdc, op);
        DeleteObject(lit);
    }

    if (openTab) {
        RECT bar = {r.left, r.bottom - 3, r.right, r.bottom};
        FillRect(hdc, &bar, Theme::Brush(Theme::Rgb::ACCENT));
    }

    wchar_t text[64] = {};
    const int n = GetWindowTextW(dis->hwndItem, text, 64);
    wchar_t icon = IconForLabel(text);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, Theme::NV(off ? Theme::Rgb::TEXT_DIM : Theme::Rgb::TEXT));

    HFONT labelFont = (HFONT)SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0);
    HFONT oldf = (HFONT)SelectObject(hdc, labelFont);
    SIZE sz = {};
    GetTextExtentPoint32W(hdc, text, n, &sz);

    // Icon and word centred together, so a row of buttons keeps its
    // rhythm whatever the words are. In a narrow button the word wins:
    // "SA..." with an icon says less than SAVE without one.
    if (sz.cx + Theme::Type::TITLE + Theme::Space::XS * 3 > r.right - r.left)
        icon = 0;
    const int iconW = icon ? Theme::Type::TITLE + Theme::Space::XS : 0;
    int cx = r.left + ((r.right - r.left) - sz.cx - iconW) / 2;
    if (cx < r.left + Theme::Space::XS) cx = r.left + Theme::Space::XS;
    if (icon) {
        HFONT g = CreateFontW(-Theme::Type::STRONG, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
        SelectObject(hdc, g);
        RECT ir = {cx, r.top, cx + Theme::Type::TITLE, r.bottom};
        DrawTextW(hdc, &icon, 1, &ir, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, labelFont);
        DeleteObject(g);
    }
    RECT tr = {cx + iconW, r.top, r.right - Theme::Space::XS, r.bottom};
    DrawTextW(hdc, text, n, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, oldf);
    if (dis->itemState & ODS_FOCUS) {
        RECT fr = r;
        InflateRect(&fr, -2, -2);
        DrawFocusRect(hdc, &fr);
    }
}

} // namespace raduk::gui
