#pragma once
// ============================================================
// DockWindow.h -- phase 134.4a: one window the panels go into
//
// The second half of the owner's docking request: a container, tiled by
// a split tree (DockGeometry.h, tested), that can fill a monitor.
//
// Panels are NOT reparented. A docked panel stays the top-level window
// it was, with its own render target, chrome and night handling; it is
// owned by the dock (GWLP_HWNDPARENT), which keeps it above the dock,
// loses its sizing border, and is placed over its slot every time the
// dock moves, resizes or changes. That gives the picture of one window
// with none of the risk of rebuilding a Direct2D target under a new
// parent -- a bug there is a blank panel on a night.
//
// In: the panel's window menu, Dock (right-click its title, or
// Alt+Space). Out: Undock, or Shift-drag it away, or close the dock,
// which undocks everything. F11 on the dock or any panel in it fills
// the monitor. Drag the bars between panels to share the space.
// ============================================================

#include <windows.h>
#include <windowsx.h>

#include <optional>
#include <string>
#include <vector>

#include "DockGeometry.h"
#include "PanelDock.h"
#include "Theme.h"
#include "WindowChrome.h"

namespace raduk::gui {

class DockWindow {
public:
    bool Create(HWND owner) {
        static bool registered = false;
        HINSTANCE inst = GetModuleHandleW(nullptr);
        if (!registered) {
            WNDCLASSEXW wc{ sizeof(wc) };
            wc.lpfnWndProc = Proc;
            wc.hInstance = inst;
            wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));   // IDC_ARROW; the build is ANSI, so IDC_* is narrow
            wc.lpszClassName = L"RadukDock";
            if (!RegisterClassExW(&wc)) return false;
            registered = true;
        }
        m_hwnd = CreateWindowExW(0, L"RadukDock", L"Dock",
                                 WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX | WS_MAXIMIZEBOX |
                                     WS_CLIPCHILDREN,
                                 120, 120, 1200, 700, owner, nullptr, inst, this);
        if (!m_hwnd) return false;
        chrome::Flags f;
        f.minimize = false;
        WindowChrome::Install(m_hwnd, f, {});

        auto& s = panel_dock::S();
        s.dockWnd = m_hwnd;
        s.toggleDock = [this](HWND h) { if (Has(h)) Undock(h, true); else Dock(h); };
        s.undockForDrag = [this](HWND h) { Undock(h, false); };
        s.dockFullscreen = [this]() { ToggleFullscreen(); };
        return true;
    }

    HWND Handle() const { return m_hwnd; }
    bool Has(HWND h) const { return Find(h) >= 0; }
    int Count() const { return (int)m_entries.size(); }

    void Dock(HWND h) {
        if (!m_hwnd || !h || Has(h)) return;
        panel_dock::ExitGroupFull();
        if (IsZoomed(h)) ShowWindow(h, SW_RESTORE);
        Entry e;
        e.h = h;
        e.role = panel_dock::RoleOf(h);
        if (e.role.empty()) e.role = "panel" + std::to_string(m_entries.size());
        e.style = GetWindowLongW(h, GWL_STYLE);
        GetWindowRect(h, &e.rect);
        e.owner = reinterpret_cast<HWND>(GetWindowLongPtrW(h, GWLP_HWNDPARENT));

        // The first panel in: the dock opens where that panel was, a
        // caption taller, so it lands under the operator's eye.
        if (!IsWindowVisible(m_hwnd)) {
            const int cap = WindowChrome::CaptionFor(m_hwnd);
            SetWindowPos(m_hwnd, nullptr, e.rect.left, e.rect.top - cap,
                         (std::max)((int)(e.rect.right - e.rect.left), 480),
                         (std::max)((int)(e.rect.bottom - e.rect.top) + cap, 320),
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        }
        SetWindowLongPtrW(h, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(m_hwnd));
        SetWindowLongW(h, GWL_STYLE, e.style & ~(LONG)WS_THICKFRAME);
        SetWindowPos(h, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        m_tree = dock::Append(m_tree, e.role, Content());
        m_entries.push_back(e);
        panel_dock::SetDocked(h, true);
        if (!IsWindowVisible(h)) ShowWindow(h, SW_SHOWNOACTIVATE);
        Relayout();
    }

    // `atSavedPlace`: back where it was before docking (Undock, closing
    // the dock); otherwise it keeps its position at its old size, for a
    // Shift-drag out that is already following the mouse.
    void Undock(HWND h, bool atSavedPlace) {
        const int i = Find(h);
        if (i < 0) return;
        const Entry e = m_entries[i];
        m_entries.erase(m_entries.begin() + i);
        panel_dock::SetDocked(h, false);
        if (m_tree) m_tree = dock::Remove(*m_tree, e.role);
        if (IsWindow(h)) {
            SetWindowLongPtrW(h, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(e.owner));
            SetWindowLongW(h, GWL_STYLE, e.style);
            RECT now;
            GetWindowRect(h, &now);
            const RECT& to = atSavedPlace ? e.rect : now;
            SetWindowPos(h, nullptr, to.left, to.top, e.rect.right - e.rect.left, e.rect.bottom - e.rect.top,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        if (m_entries.empty()) {
            if (m_full) ToggleFullscreen();
            ShowWindow(m_hwnd, SW_HIDE);
        } else {
            Relayout();
        }
    }

    void UndockAll() {
        while (!m_entries.empty()) Undock(m_entries.back().h, true);
    }

    void ToggleFullscreen() {
        if (!m_hwnd) return;
        if (!m_full) {
            m_place.length = sizeof(m_place);
            GetWindowPlacement(m_hwnd, &m_place);
            m_savedStyle = GetWindowLongW(m_hwnd, GWL_STYLE);
            MONITORINFO mi{ sizeof(mi) };
            GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
            SetWindowLongW(m_hwnd, GWL_STYLE,
                           (m_savedStyle & ~(LONG)(WS_CAPTION | WS_THICKFRAME)) | (LONG)WS_POPUP);
            const RECT& r = mi.rcMonitor;
            SetWindowPos(m_hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            m_full = true;
        } else {
            SetWindowLongW(m_hwnd, GWL_STYLE, m_savedStyle);
            SetWindowPlacement(m_hwnd, &m_place);
            SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            m_full = false;
        }
        Relayout();
    }

    // Every docked panel over its slot, in one batch. Runs on every
    // move, resize and change of the dock.
    void Relayout() {
        if (!m_hwnd) return;
        const dock::Rect area = Content();
        m_bars = m_tree ? dock::Splitters(*m_tree, area, Splitter()) : std::vector<dock::SplitBar>{};
        InvalidateRect(m_hwnd, nullptr, TRUE);
        if (!m_tree || m_entries.empty()) return;
        POINT origin{ 0, 0 };
        ClientToScreen(m_hwnd, &origin);
        const auto slots = dock::Layout(*m_tree, area, Splitter());
        HDWP batch = BeginDeferWindowPos((int)slots.size());
        for (const auto& [role, r] : slots) {
            const int i = FindRole(role);
            if (i < 0 || !batch) continue;
            batch = DeferWindowPos(batch, m_entries[i].h, nullptr, origin.x + r.l, origin.y + r.t,
                                   r.w(), r.h(), SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        }
        if (batch) EndDeferWindowPos(batch);
    }

private:
    struct Entry {
        HWND h = nullptr;
        std::string role;
        LONG style = 0;
        RECT rect{};
        HWND owner = nullptr;
    };

    int Find(HWND h) const {
        for (size_t i = 0; i < m_entries.size(); ++i) if (m_entries[i].h == h) return (int)i;
        return -1;
    }
    int FindRole(const std::string& r) const {
        for (size_t i = 0; i < m_entries.size(); ++i) if (m_entries[i].role == r) return (int)i;
        return -1;
    }
    int Splitter() const { return panel_dock::Scaled(m_hwnd, 6); }

    // The client area below the dock's own title bar, in client coordinates.
    dock::Rect Content() const {
        RECT cr{};
        GetClientRect(m_hwnd, &cr);
        return { 0, WindowChrome::CaptionFor(m_hwnd), cr.right, cr.bottom };
    }

    int BarAt(int x, int y) const {
        for (size_t i = 0; i < m_bars.size(); ++i) {
            const dock::Rect& b = m_bars[i].bar;
            if (x >= b.l && x < b.r && y >= b.t && y < b.b) return (int)i;
        }
        return -1;
    }

    void Paint() {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(m_hwnd, &ps);
        RECT cr;
        GetClientRect(m_hwnd, &cr);
        FillRect(dc, &cr, Theme::Brush(Theme::Rgb::DEEP));
        for (const auto& b : m_bars) {
            RECT r{ b.bar.l, b.bar.t, b.bar.r, b.bar.b };
            FillRect(dc, &r, Theme::Brush(Theme::Rgb::BORDER));
        }
        if (m_entries.empty()) {
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, Theme::NV(Theme::Rgb::TEXT_DIM));
            RECT t = cr;
            t.top = WindowChrome::CaptionFor(m_hwnd);
            DrawTextW(dc, L"Right-click a panel's title and choose Dock", -1, &t,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        EndPaint(m_hwnd, &ps);
    }

    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        DockWindow* self = reinterpret_cast<DockWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            self = static_cast<DockWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(h, msg, wp, lp);
        switch (msg) {
        case WM_PAINT: self->Paint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_SIZE:
        case WM_MOVE:
            self->Relayout();
            break;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                POINT p;
                GetCursorPos(&p);
                ScreenToClient(h, &p);
                const int b = self->BarAt(p.x, p.y);
                if (b >= 0) {
                    SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(self->m_bars[b].dir == 'H' ? 32644 : 32645)));   // IDC_SIZEWE / IDC_SIZENS
                    return TRUE;
                }
            }
            break;
        case WM_LBUTTONDOWN:
            self->m_drag = self->BarAt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (self->m_drag >= 0) SetCapture(h);
            return 0;
        case WM_MOUSEMOVE:
            if (self->m_drag >= 0 && self->m_drag < (int)self->m_bars.size() && self->m_tree) {
                const dock::SplitBar bar = self->m_bars[self->m_drag];
                const int pos = (bar.dir == 'H' ? GET_X_LPARAM(lp) : GET_Y_LPARAM(lp)) - self->Splitter() / 2;
                dock::SetRatio(*self->m_tree, bar.path, dock::RatioAt(bar, pos, self->Splitter()));
                self->Relayout();
            }
            return 0;
        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
            if (self->m_drag >= 0) { self->m_drag = -1; if (msg == WM_LBUTTONUP) ReleaseCapture(); }
            return 0;
        // F11 fills the monitor; claimed through WM_GETDLGCODE because the
        // main loop routes keys thread-wide. Esc is never claimed: it is
        // the emergency stop.
        case WM_GETDLGCODE:
            if (lp && wp == VK_F11) return DLGC_WANTMESSAGE;
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_F11) { self->ToggleFullscreen(); return 0; }
            break;
        case WM_CLOSE:
            self->UndockAll();
            ShowWindow(h, SW_HIDE);
            return 0;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }

    HWND m_hwnd = nullptr;
    std::optional<dock::Node> m_tree;
    std::vector<Entry> m_entries;
    std::vector<dock::SplitBar> m_bars;
    int m_drag = -1;
    bool m_full = false;
    WINDOWPLACEMENT m_place{ sizeof(WINDOWPLACEMENT) };
    LONG m_savedStyle = 0;
};

} // namespace raduk::gui
