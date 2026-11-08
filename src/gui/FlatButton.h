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
//
// Phase 132.5: hover and press. The face lifts half a surface step
// under the pointer, easing in over Motion::FAST, and a press drops it
// to the panel background with the content 1 px lower (ButtonState.h
// has the rules). The hover is tracked by a subclass installed the
// first time a button is drawn, so a button the control panel's
// RebuildControls creates later is covered without being registered,
// and GoTo's buttons get it with no change of their own.
// ============================================================

// Before anything pulls in windows.h: without NOMINMAX its min and max
// macros break std::min in any header included after this one (132.3b's
// first CI run failed on exactly that, in EAFEmulator.h).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "gui/ButtonIcons.h"
#include "gui/ButtonState.h"
#include "gui/Theme.h"

#include <chrono>

namespace raduk::gui {

namespace flat_detail {

constexpr UINT_PTR kHoverSubclassId = 0x1325B01;
constexpr UINT_PTR kHoverTimerId    = 0x1325;

inline double NowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

// Tracks the pointer for one button. The ramp lives in the subclass's
// reference data and is freed with the window.
inline LRESULT CALLBACK HoverProc(HWND h, UINT m, WPARAM w, LPARAM l,
                                  UINT_PTR, DWORD_PTR ref) {
    auto* ramp = reinterpret_cast<button::HoverRamp*>(ref);
    switch (m) {
        case WM_MOUSEMOVE:
            if (!ramp->Hot()) {
                TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, h, 0 };
                TrackMouseEvent(&t);
                ramp->Set(true, NowMs(), Theme::Motion::FAST, Theme::Motion::On());
                SetTimer(h, kHoverTimerId, Theme::Motion::FRAME_MS, nullptr);
                InvalidateRect(h, nullptr, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            ramp->Set(false, NowMs(), Theme::Motion::FAST, Theme::Motion::On());
            SetTimer(h, kHoverTimerId, Theme::Motion::FRAME_MS, nullptr);
            InvalidateRect(h, nullptr, FALSE);
            break;
        case WM_TIMER:
            if (w == kHoverTimerId) {
                // Stops once settled, so nothing ticks over an idle panel.
                if (ramp->Settled(NowMs())) KillTimer(h, kHoverTimerId);
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_NCDESTROY:
            KillTimer(h, kHoverTimerId);
            RemoveWindowSubclass(h, HoverProc, kHoverSubclassId);
            delete ramp;
            break;
    }
    return DefSubclassProc(h, m, w, l);
}

// The button's hover level, installing the tracker on first sight.
inline float HoverLevel(HWND btn) {
    if (!btn) return 0.0f;
    DWORD_PTR ref = 0;
    if (GetWindowSubclass(btn, HoverProc, kHoverSubclassId, &ref) && ref)
        return reinterpret_cast<button::HoverRamp*>(ref)->Level(NowMs());
    auto* ramp = new button::HoverRamp();
    if (!SetWindowSubclass(btn, HoverProc, kHoverSubclassId, reinterpret_cast<DWORD_PTR>(ramp)))
        delete ramp;
    return 0.0f;
}

// The face fill for a lift, in the active space: blended between the
// TINTED neighbours, as Theme::Mix requires, so night vision gets the
// designed surface luminances and no green or blue.
inline COLORREF FaceFill(float lift) {
    const COLORREF panel = Theme::NV(Theme::Rgb::PANEL);
    return lift >= 0.0f ? Theme::Mix(panel, Theme::NV(Theme::Rgb::BORDER), lift)
                        : Theme::Mix(panel, Theme::NV(Theme::Rgb::DEEP), -lift);
}

// The lit edge over an already-tinted fill: Theme::HighlightOver's
// multiply, which keeps an emptied channel empty.
inline COLORREF LitOver(COLORREF tinted) {
    auto up = [](int x) { const int v = (int)(x * 1.5f + 0.5f); return v > 255 ? 255 : v; };
    return RGB(up(GetRValue(tinted)), up(GetGValue(tinted)), up(GetBValue(tinted)));
}

} // namespace flat_detail

// `openTab`: draw the open-tab bar under it, which is shape rather than
// hue so it survives night vision. Only the control panel's tab strip
// sets it.
inline void DrawFlatButton(const DRAWITEMSTRUCT* dis, bool openTab = false) {
    HDC  hdc = dis->hDC;
    RECT r   = dis->rcItem;
    const bool down = (dis->itemState & ODS_SELECTED) != 0;
    const bool off  = (dis->itemState & ODS_DISABLED) != 0;
    const button::Look look = button::LookFor(off, down, flat_detail::HoverLevel(dis->hwndItem));
    const COLORREF fill = flat_detail::FaceFill(look.lift);
    {
        HBRUSH fb = CreateSolidBrush(fill);
        FillRect(hdc, &r, fb);
        DeleteObject(fb);
    }
    FrameRect(hdc, &r, Theme::Brush(Theme::Rgb::BORDER));

    // The lit top edge, inside the frame: a raised face, not a flat
    // box. Not on a pressed button, which is the point of pressing.
    if (look.litEdge && r.right - r.left > 4) {
        HPEN lit = CreatePen(PS_SOLID, 1, flat_detail::LitOver(fill));
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
    SetTextColor(hdc, Theme::NV(look.dimText ? Theme::Rgb::TEXT_DIM : Theme::Rgb::TEXT));
    // A press drops the content with the face. The frame and the
    // open-tab bar stay where they are; only the icon and word move.
    OffsetRect(&r, look.offset, look.offset);

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
        RECT fr = dis->rcItem;
        InflateRect(&fr, -2, -2);
        DrawFocusRect(hdc, &fr);
    }
}

} // namespace raduk::gui
