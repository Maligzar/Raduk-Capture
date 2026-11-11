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

// ── Phase 134.2: groups ──────────────────────────────────────
//
// A drag starts with a snapshot (WM_ENTERSIZEMOVE): every target panel's
// rectangle, and the groups they form. Each WM_MOVING / WM_SIZING then
// works from that snapshot, not from where the members have got to, so
// rounding never accumulates over a long drag. Holding Shift when the
// drag STARTS makes it solo: the panel moves alone, and since groups are
// only geometry, it has left its group the moment it is no longer flush.
// A group spanning monitors at different scaling is also dragged solo,
// until per-monitor DPI (119.6) lands.

struct Drag {
    HWND self = nullptr;
    std::vector<HWND> hwnds;
    std::vector<dock::Rect> start;
    std::vector<int> ids;
    int selfIndex = -1;
    int members = 0;      // panels in self's group, self included
    bool solo = true;
};
inline Drag& D() { static Drag d; return d; }

inline void BeginDrag(HWND self) {
    Drag d;
    d.self = self;
    for (HWND p : S().panels) {
        if (p != self && !IsTarget(p)) continue;
        RECT r;
        if (!GetWindowRect(p, &r)) continue;
        if (p == self) d.selfIndex = (int)d.hwnds.size();
        d.hwnds.push_back(p);
        d.start.push_back(ToDock(r));
    }
    if (d.selfIndex < 0) { D() = Drag{}; return; }
    d.ids = dock::Groups(d.start);
    const int g = d.ids[d.selfIndex];
    const UINT dpi = GetDpiForWindow(self);
    bool mixedDpi = false;
    for (size_t i = 0; i < d.hwnds.size(); ++i)
        if (d.ids[i] == g) {
            ++d.members;
            mixedDpi = mixedDpi || GetDpiForWindow(d.hwnds[i]) != dpi;
        }
    d.solo = GetKeyState(VK_SHIFT) < 0 || d.members < 2 || mixedDpi;
    D() = std::move(d);
}

inline void EndDrag() { D() = Drag{}; }

// Every member but self to where `rects` says, in one batch.
inline void PlaceMembers(const std::vector<dock::Rect>& rects) {
    const Drag& d = D();
    HDWP batch = BeginDeferWindowPos((int)d.members);
    for (size_t i = 0; i < d.hwnds.size(); ++i) {
        if ((int)i == d.selfIndex || d.ids[i] != d.ids[d.selfIndex]) continue;
        const dock::Rect& r = rects[i];
        if (batch)
            batch = DeferWindowPos(batch, d.hwnds[i], nullptr, r.l, r.t, r.w(), r.h(),
                                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    if (batch) EndDeferWindowPos(batch);
}

// WM_MOVING: a group moves as one and snaps as one block.
inline void DragMove(HWND self, RECT* proposed) {
    const Drag& d = D();
    if (!proposed) return;
    if (d.self != self || d.solo) { SnapProposed(self, proposed, false, 0); return; }
    const auto moved = dock::MoveGroupSnapped(d.start, d.ids, d.selfIndex, ToDock(*proposed),
                                              WorkAreaFor(*proposed), Scaled(self, dock::kSnapPx));
    *proposed = FromDock(moved[d.selfIndex]);
    PlaceMembers(moved);
}

// WM_SIZING: snap the dragged edges against panels outside the group,
// then carry every member on the same line with them (the splitter).
inline void DragSize(HWND self, RECT* proposed, int edges) {
    const Drag& d = D();
    if (!proposed) return;
    if (d.self != self || d.solo) { SnapProposed(self, proposed, true, edges); return; }
    const int g = d.ids[d.selfIndex];
    std::vector<dock::Rect> outside;
    for (size_t i = 0; i < d.start.size(); ++i)
        if (d.ids[i] != g) outside.push_back(d.start[i]);
    dock::Rect cur = ToDock(*proposed);
    if (GetKeyState(VK_SHIFT) >= 0)
        cur = dock::Snap(cur, outside, WorkAreaFor(*proposed), Scaled(self, dock::kSnapPx),
                         true, edges).rect;
    std::vector<dock::Rect> rects = d.start;
    const int minW = Scaled(self, dock::kMinW), minH = Scaled(self, dock::kMinH);
    if (edges & dock::Left)   rects = dock::ResizeEdge(rects, d.ids, d.selfIndex, dock::Left,   cur.l, minW, minH);
    if (edges & dock::Right)  rects = dock::ResizeEdge(rects, d.ids, d.selfIndex, dock::Right,  cur.r, minW, minH);
    if (edges & dock::Top)    rects = dock::ResizeEdge(rects, d.ids, d.selfIndex, dock::Top,    cur.t, minW, minH);
    if (edges & dock::Bottom) rects = dock::ResizeEdge(rects, d.ids, d.selfIndex, dock::Bottom, cur.b, minW, minH);
    *proposed = FromDock(rects[d.selfIndex]);
    PlaceMembers(rects);
}

// ── Phase 134.3: a group fills its monitor ───────────────────
//
// F11 on a panel in a group: every member is mapped onto the monitor in
// proportion (dock::FillMonitor, which leaves no gap at any joint), with
// its caption and sizing border taken away, as single fullscreen does,
// so the group reads as one instrument. F11 or Esc puts back each
// member's style and rectangle exactly. A panel alone, or a group
// across monitors with different scaling, keeps single fullscreen.

struct GroupFull {
    bool active = false;
    std::vector<HWND> hwnds;
    std::vector<RECT> savedRect;
    std::vector<LONG> savedStyle;
};
inline GroupFull& GF() { static GroupFull g; return g; }

inline std::vector<HWND> GroupOf(HWND self) {
    std::vector<HWND> hw;
    std::vector<dock::Rect> rs;
    int si = -1;
    for (HWND p : S().panels) {
        if (p != self && !IsTarget(p)) continue;
        RECT r;
        if (!GetWindowRect(p, &r)) continue;
        if (p == self) si = (int)hw.size();
        hw.push_back(p);
        rs.push_back(ToDock(r));
    }
    std::vector<HWND> out;
    if (si < 0) return out;
    const auto ids = dock::Groups(rs);
    for (size_t i = 0; i < hw.size(); ++i)
        if (ids[i] == ids[si]) out.push_back(hw[i]);
    return out;
}

inline bool InGroupFull(HWND h) {
    if (!GF().active) return false;
    for (HWND m : GF().hwnds) if (m == h) return true;
    return false;
}

inline void ExitGroupFull() {
    GroupFull& g = GF();
    if (!g.active) return;
    for (size_t i = 0; i < g.hwnds.size(); ++i) {
        if (!IsWindow(g.hwnds[i])) continue;
        SetWindowLongW(g.hwnds[i], GWL_STYLE, g.savedStyle[i]);
        const RECT& r = g.savedRect[i];
        SetWindowPos(g.hwnds[i], nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    g = GroupFull{};
}

inline bool EnterGroupFull(HWND self) {
    if (GF().active) ExitGroupFull();
    const std::vector<HWND> members = GroupOf(self);
    if (members.size() < 2) return false;
    const UINT dpi = GetDpiForWindow(self);
    for (HWND m : members) if (GetDpiForWindow(m) != dpi) return false;

    GroupFull g;
    std::vector<dock::Rect> rs;
    for (HWND m : members) {
        RECT r;
        GetWindowRect(m, &r);
        g.hwnds.push_back(m);
        g.savedRect.push_back(r);
        g.savedStyle.push_back(GetWindowLongW(m, GWL_STYLE));
        rs.push_back(ToDock(r));
    }
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(self, MONITOR_DEFAULTTONEAREST), &mi);
    const auto filled = dock::FillMonitor(rs, ToDock(mi.rcMonitor));
    for (size_t i = 0; i < members.size(); ++i) {
        const LONG st = (g.savedStyle[i] & ~(LONG)(WS_CAPTION | WS_THICKFRAME)) | (LONG)WS_POPUP;
        SetWindowLongW(members[i], GWL_STYLE, st);
        const dock::Rect& r = filled[i];
        SetWindowPos(members[i], nullptr, r.l, r.t, r.w(), r.h(),
                     SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    g.active = true;
    GF() = std::move(g);
    return true;
}

} // namespace raduk::gui::panel_dock
