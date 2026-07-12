#pragma once
// ============================================================
// NotificationBar.h — Phase 40: Notification Bar
//
// Thin WS_CHILD strip that sits between the toolbar and the
// preview window. Shows transient status messages:
//
//   Green  — info  (file saved, capture stopped, snapshot saved)
//   Amber  — warn  (buffer overrun, EAF limit, mount lost)
//   Red    — error (disk full, camera timeout, write failure)
//
// Auto-dismisses after 8 seconds. Click anywhere to dismiss.
// "→ Open folder" suffix on file-save notifications opens the
// folder in Explorer via ShellExecute.
//
// Public API (call from main.cpp):
//   bar.NotifyInfo (text, folderPath = L"")
//   bar.NotifyWarn (text)
//   bar.NotifyError(text)
//   bar.Dismiss()
//   bool bar.IsVisible()
//   int  bar.Height()   — 0 or NOTIF_HEIGHT, use for layout
//
// SetDismissCallback — called when bar hides so main can
// nudge the preview window back up.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>
#include <functional>
#include <string>
#include <cstdio>

#pragma comment(lib, "shell32.lib")

namespace raduk::gui {

static constexpr int NOTIF_HEIGHT = 28;  // px — visible height of the bar
static constexpr int NOTIF_TIMER_ID  = 901;
static constexpr int NOTIF_TIMEOUT_MS = 8000;

enum class NotifLevel { Info, Warn, Error };

class NotificationBar {
public:
    NotificationBar() = default;
    ~NotificationBar() {
        if (m_hwnd) {
            KillTimer(m_hwnd, NOTIF_TIMER_ID);
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
        if (m_font) { DeleteObject(m_font); m_font = nullptr; }
    }

    // Create as WS_CHILD of mainHwnd, initially hidden (zero height).
    // panelWidth: reserved right width (same as ControlPanel) so bar
    // spans only the preview area.
    bool Create(HWND mainHwnd, int panelWidth) {
        m_mainHwnd   = mainHwnd;
        m_panelWidth = panelWidth;

        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = NotifWndProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_HAND);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = L"RadukNotifBar";
        RegisterClassExW(&wc);

        RECT cr; GetClientRect(mainHwnd, &cr);
        int barW = cr.right - panelWidth;

        m_hwnd = CreateWindowExW(
            0, L"RadukNotifBar", L"",
            WS_CHILD | WS_CLIPSIBLINGS,   // NOT WS_VISIBLE — hidden initially
            0, TOOLBAR_HEIGHT, barW, 0,   // height=0 → invisible but exists
            mainHwnd, nullptr, GetModuleHandle(nullptr), this);

        if (!m_hwnd) {
            printf("[NotifBar] CreateWindowExW failed: %lu\n", GetLastError());
            return false;
        }

        m_font = CreateFontW(
            -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
        if (!m_font) m_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        printf("[NotifBar] Created\n");
        return true;
    }

    // ── Public notification API ───────────────────────────────

    // Info (green): file saved, snapshot, capture stopped
    // folderPath: if non-empty, adds "→ Open folder" click target
    void NotifyInfo(const std::wstring& text, const std::wstring& folderPath = L"") {
        Show(NotifLevel::Info, text, folderPath);
    }

    // Warning (amber): buffer overrun, EAF limit, mount lost
    void NotifyWarn(const std::wstring& text) {
        Show(NotifLevel::Warn, text, L"");
    }

    // Error (red): disk full, camera timeout, write failure
    void NotifyError(const std::wstring& text) {
        Show(NotifLevel::Error, text, L"");
    }

    void Dismiss() { Hide(); }

    bool IsVisible() const {
        return m_hwnd && IsWindowVisible(m_hwnd);
    }

    // Returns actual pixel height — 0 when hidden, NOTIF_HEIGHT when shown.
    // main.cpp uses this to offset the preview child window.
    int Height() const { return IsVisible() ? NOTIF_HEIGHT : 0; }

    // Called when bar hides so main can reflow the preview window.
    void SetDismissCallback(std::function<void()> cb) { m_onDismiss = cb; }

    // Call on WM_SIZE of main window so bar stays full width
    void SnapToMainWindow() {
        if (!m_hwnd || !m_mainHwnd) return;
        RECT cr; GetClientRect(m_mainHwnd, &cr);
        int barW = cr.right - m_panelWidth;
        int barH = IsVisible() ? NOTIF_HEIGHT : 0;
        SetWindowPos(m_hwnd, nullptr, 0, TOOLBAR_HEIGHT, barW, barH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

private:
    static constexpr int TOOLBAR_HEIGHT = 30; // must match Window.cpp

    void Show(NotifLevel level, const std::wstring& text,
              const std::wstring& folderPath) {
        m_level      = level;
        m_text       = text;
        m_folderPath = folderPath;

        // Resize to visible height
        RECT cr; GetClientRect(m_mainHwnd, &cr);
        int barW = cr.right - m_panelWidth;
        SetWindowPos(m_hwnd, HWND_TOP,
                     0, TOOLBAR_HEIGHT, barW, NOTIF_HEIGHT,
                     SWP_NOACTIVATE);
        ShowWindow(m_hwnd, SW_SHOWNA);
        InvalidateRect(m_hwnd, nullptr, TRUE);

        // Notify main to shift preview down
        if (m_onDismiss) {
            // We reuse the callback for layout — fire it on show too
            // Main checks Height() to decide direction
            m_onDismiss();
        }

        // (Re)start auto-dismiss timer
        KillTimer(m_hwnd, NOTIF_TIMER_ID);
        SetTimer(m_hwnd, NOTIF_TIMER_ID, NOTIF_TIMEOUT_MS, nullptr);
    }

    void Hide() {
        if (!m_hwnd) return;
        KillTimer(m_hwnd, NOTIF_TIMER_ID);
        ShowWindow(m_hwnd, SW_HIDE);
        // Collapse height to 0
        RECT cr; GetClientRect(m_mainHwnd, &cr);
        int barW = cr.right - m_panelWidth;
        SetWindowPos(m_hwnd, nullptr,
                     0, TOOLBAR_HEIGHT, barW, 0,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_onDismiss) m_onDismiss();
    }

    void Paint(HDC hdc) {
        RECT cr; GetClientRect(m_hwnd, &cr);

        // Background colour by level
        COLORREF bg, fg;
        switch (m_level) {
            case NotifLevel::Info:
                bg = RGB(10, 60, 30);   // dark green
                fg = RGB(60, 255, 120); // bright green text
                break;
            case NotifLevel::Warn:
                bg = RGB(60, 42, 0);    // dark amber
                fg = RGB(255, 185, 0);  // amber text
                break;
            case NotifLevel::Error:
            default:
                bg = RGB(60, 8, 8);     // dark red
                fg = RGB(255, 80, 80);  // red text
                break;
        }

        // Fill background
        HBRUSH hBg = CreateSolidBrush(bg);
        FillRect(hdc, &cr, hBg);
        DeleteObject(hBg);

        // Bottom border line (1px, slightly lighter)
        HPEN hPen = CreatePen(PS_SOLID, 1, fg);
        HPEN hOld = (HPEN)SelectObject(hdc, hPen);
        MoveToEx(hdc, 0,         cr.bottom - 1, nullptr);
        LineTo  (hdc, cr.right,  cr.bottom - 1);
        SelectObject(hdc, hOld);
        DeleteObject(hPen);

        // Text
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, fg);
        HFONT hOldFont = (HFONT)SelectObject(hdc, m_font);

        // Build display string
        std::wstring display = m_text;
        if (!m_folderPath.empty())
            display += L"    \u2192 Open folder";

        RECT textRect = { 10, 0, cr.right - 10, cr.bottom };
        DrawTextW(hdc, display.c_str(), (int)display.size(),
                  &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        SelectObject(hdc, hOldFont);
    }

    void OnClick(int x, int y) {
        // If folderPath set and click is in the right half, open folder
        if (!m_folderPath.empty()) {
            RECT cr; GetClientRect(m_hwnd, &cr);
            // "→ Open folder" is drawn roughly in the right portion —
            // clicking anywhere with a folder path opens it for simplicity
            ShellExecuteW(nullptr, L"explore", m_folderPath.c_str(),
                          nullptr, nullptr, SW_SHOWNORMAL);
        }
        Hide();
    }

    static LRESULT CALLBACK NotifWndProc(HWND hwnd, UINT msg,
                                          WPARAM wp, LPARAM lp) {
        NotificationBar* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCT*>(lp);
            self = static_cast<NotificationBar*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        } else {
            self = reinterpret_cast<NotificationBar*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                if (self) self->Paint(hdc);
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_LBUTTONDOWN:
                if (self) self->OnClick(LOWORD(lp), HIWORD(lp));
                return 0;
            case WM_TIMER:
                if (wp == NOTIF_TIMER_ID && self) self->Hide();
                return 0;
            case WM_SETCURSOR:
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return TRUE;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    HWND    m_hwnd       = nullptr;
    HWND    m_mainHwnd   = nullptr;
    HFONT   m_font       = nullptr;
    int     m_panelWidth = 560;

    NotifLevel   m_level      = NotifLevel::Info;
    std::wstring m_text;
    std::wstring m_folderPath;

    std::function<void()> m_onDismiss;
};

} // namespace raduk::gui
