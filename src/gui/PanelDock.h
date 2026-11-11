#pragma once
// ============================================================
// PanelDock.h -- phase 134.1: which panels snap, and to what
//
// The Win32 side of docking, kept small: a list of the panels that dock,
// the rectangles of the others, the monitor's work area, and the snap
// distance at the window's DPI. The arithmetic is DockGeometry.h's and
// is tested there; WindowChrome::Filter calls this during WM_MOVING and
// WM_SIZING, since every dockable panel routes its messages through it.
//
// Nothing is unregistered: a hidden, minimised, maximised or fullscreen
// panel (no WS_CAPTION) is simply not a target, checked each time.
// rig.ini PanelSnap=0 turns snapping off.
// ============================================================

#include <windows.h>

#include <vector>

#include "DockGeometry.h"

namespace raduk::gui::panel_dock {

struct State {
    std::vector<HWND> panels;
    bool enabled = true;
};
inline State& S() { static State s; return s; }

inline void Register(HWND h) { if (h) S().panels.push_back(h); }
inline void SetEnabled(bool on) { S().enabled = on; }
inline bool Enabled() { return S().enabled; }
inline int Count() { return (int)S().panels.size(); }
inline bool IsRegistered(HWND h) {
    for (HWND p : S().panels) if (p == h) return true;
    return false;
}

inline dock::Rect ToDock(const RECT& r) { return { r.left, r.top, r.right, r.bottom }; }
inline RECT FromDock(const dock::Rect& r) { return { r.l, r.t, r.r, r.b }; }

// A panel another can snap to: on screen, in its normal state.
inline bool IsTarget(HWND h) {
    return IsWindow(h) && IsWindowVisible(h) && !IsIconic(h) && !IsZoomed(h) &&
           (GetWindowLongW(h, GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
}

inline std::vector<dock::Rect> OthersFor(HWND self) {
    std::vector<dock::Rect> out;
    for (HWND p : S().panels) {
        if (p == self || !IsTarget(p)) continue;
        RECT r;
        if (GetWindowRect(p, &r)) out.push_back(ToDock(r));
    }
    return out;
}

inline dock::Rect WorkAreaFor(const RECT& r) {
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi);
    return ToDock(mi.rcWork);
}

inline int Scaled(HWND h, int px96) {
    const UINT dpi = GetDpiForWindow(h);
    return MulDiv(px96, dpi ? (int)dpi : 96, 96);
}

// WM_MOVING / WM_SIZING: snap the proposed rectangle in place. Holding
// Shift places the panel freely -- the owner's pull-out modifier, which
// in 134.2 will also take a panel out of its group.
inline void SnapProposed(HWND self, RECT* proposed, bool resizing, int edges) {
    if (!proposed || GetKeyState(VK_SHIFT) < 0) return;
    const auto r = dock::Snap(ToDock(*proposed), OthersFor(self), WorkAreaFor(*proposed),
                              Scaled(self, dock::kSnapPx), resizing, edges);
    *proposed = FromDock(r.rect);
}

} // namespace raduk::gui::panel_dock
