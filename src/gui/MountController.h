#pragma once
// ============================================================
// MountController.h — Floating always-on-top mount safety panel
//
// A compact WS_EX_TOPMOST window that stays above ALL applications.
// Safety purpose: lets you stop the mount no matter what program
// has focus — even if RadukCapture is behind another window.
//
// Contains:
//   - Big STOP button (red, always visible)
//   - N/S/E/W jog d-pad (hold-to-slew, same subclass as panel)
//   - Rate selector: 9x / FAST / MED / FINE
//   - Tracking toggle
//   - Live RA/Dec readout
//   - Drag by title bar area to reposition
//
// Usage (main.cpp):
//   mountCtrl.SetMount(mount);
//   mountCtrl.SetJogCallback([&](int axis, int dir, bool start) { ... });
//   mountCtrl.Create(parentHwnd);
//   mountCtrl.Show();   // M key or button to toggle
// ============================================================

#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <commctrl.h>
#include <cstdio>
#include <functional>
#include <string>
#include <atomic>
#include "core/IMount.h"

#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")

namespace raduk::gui {

class MountController {
public:
    MountController()  = default;
    ~MountController() { if (m_hwnd) DestroyWindow(m_hwnd); }

    MountController(const MountController&) = delete;
    MountController& operator=(const MountController&) = delete;

    // ── Setup ─────────────────────────────────────────────────
    void SetMount(raduk::core::IMount* m) { m_mount = m; }

    // Jog: axis=0 (Az/RA), axis=1 (Alt/Dec); dir=0 (pos), dir=1 (neg)
    void SetJogCallback(std::function<void(int axis, int dir, bool start)> cb) {
        m_onJog = cb;
    }
    void SetEmergencyStopCallback(std::function<void()> cb) {
        m_onEmergencyStop = cb;
    }

    // ── Create / visibility ───────────────────────────────────
    bool Create(HWND parent) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(7, 13, 22));
        wc.lpszClassName = L"RadukMountController";
        wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);

        if (!RegisterClassExW(&wc)) {
            DWORD err = GetLastError();
            if (err != ERROR_CLASS_ALREADY_EXISTS) return false;
        }

        // Position: top-right corner of primary monitor
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int x  = sw - W - 12;
        int y  = 40;

        m_hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,   // always on top, no taskbar entry
            L"RadukMountController",
            L"MOUNT CONTROL",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
            x, y, W, H,
            parent, nullptr, GetModuleHandle(nullptr), this);

        if (!m_hwnd) return false;

        BuildControls();
        UpdateRateButtons();

        // Start a 500ms readout timer
        SetTimer(m_hwnd, TIMER_READOUT, 500, nullptr);

        printf("[MountCtrl] Created at (%d,%d) %dx%d\n", x, y, W, H);
        return true;
    }

    void Show()   {
        if (m_hwnd) {
            ShowWindow(m_hwnd, SW_SHOW);
            // Re-assert topmost in case another app grabbed HWND_TOPMOST
            SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    void Hide()   { if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE); }
    void Toggle() { if (m_hwnd) IsVisible() ? Hide() : Show(); }

    // Show and snap back to default position (top-right corner)
    void ShowAtDefault() {
        if (!m_hwnd) return;
        int sw = GetSystemMetrics(SM_CXSCREEN);
        SetWindowPos(m_hwnd, HWND_TOPMOST,
                     sw - W - 12, 40, 0, 0,
                     SWP_NOSIZE | SWP_SHOWWINDOW);
    }
    bool IsVisible() const {
        return m_hwnd && (GetWindowLong(m_hwnd, GWL_STYLE) & WS_VISIBLE);
    }

    HWND Handle() const { return m_hwnd; }

    // ── Live coordinate update (call from readout loop) ───────
    void UpdateReadouts() {
        if (!m_hwnd || !m_mount || !m_mount->IsConnected()) {
            SetText(m_lblRA,  L"RA  --h --m --s");
            SetText(m_lblDec, L"Dec --° --' --\"");
            SetText(m_lblTrk, L"TRK: OFF");
            return;
        }
        double ra = 0, dec = 0;
        if (m_mount->GetRADec(ra, dec)) {
            wchar_t buf[64];
            // RA
            int rh = (int)ra;
            int rm = (int)((ra - rh) * 60.0);
            int rs = (int)(((ra - rh) * 60.0 - rm) * 60.0);
            swprintf(buf, 64, L"RA  %02dh %02dm %02ds", rh, rm, rs);
            SetText(m_lblRA, buf);
            // Dec
            bool neg = dec < 0; double d = neg ? -dec : dec;
            int dd = (int)d, dm = (int)((d-dd)*60), ds = (int)(((d-dd)*60-dm)*60);
            swprintf(buf, 64, L"Dec %s%02d° %02d' %02d\"", neg?L"-":L"+", dd, dm, ds);
            SetText(m_lblDec, buf);
        }
        // Tracking
        auto mode = m_mount->GetTrackingMode();
        const wchar_t* trkText = (mode == raduk::core::IMount::OFF)    ? L"TRK: OFF" :
                                  (mode == raduk::core::IMount::ALT_AZ)  ? L"TRK: ALT/AZ" :
                                  (mode == raduk::core::IMount::EQ_NORTH)? L"TRK: EQ N" : L"TRK: EQ S";
        SetText(m_lblTrk, trkText);
        InvalidateRect(m_btnTracking, nullptr, TRUE);
    }

private:
    // ── Layout constants ──────────────────────────────────────
    static constexpr int W = 210;
    static constexpr int H = 316;  // taller for Park button
    static constexpr int MARGIN = 8;
    static constexpr int TIMER_READOUT = 1;

    // Control IDs
    enum {
        ID_BTN_STOP = 100,
        ID_BTN_N, ID_BTN_S, ID_BTN_E, ID_BTN_W,
        ID_BTN_RATE9, ID_BTN_RATE8, ID_BTN_RATE7, ID_BTN_RATE5,
        ID_BTN_TRACKING, ID_BTN_PARK,
        ID_LBL_RA, ID_LBL_DEC, ID_LBL_TRK,
    };

    // ── BuildControls ─────────────────────────────────────────
    void BuildControls() {
        HINSTANCE hi = GetModuleHandle(nullptr);
        int x = MARGIN;
        int w = W - MARGIN * 2;
        int y = 28;  // below caption

        // ── RA/Dec readout ────────────────────────────────────
        m_lblRA  = MakeLbl(L"RA  --h --m --s", x, y, w, 16, ID_LBL_RA);   y += 18;
        m_lblDec = MakeLbl(L"Dec --° --' --\"", x, y, w, 16, ID_LBL_DEC);  y += 18;
        m_lblTrk = MakeLbl(L"TRK: OFF",         x, y, w/2, 16, ID_LBL_TRK); y += 22;

        // ── Rate row ──────────────────────────────────────────
        int rw = (w - 6) / 4;
        m_btnRate9 = MakeBtn(L"9x",   x,           y, rw, 20, ID_BTN_RATE9);
        m_btnRate8 = MakeBtn(L"FAST", x+rw+2,      y, rw, 20, ID_BTN_RATE8);
        m_btnRate7 = MakeBtn(L"MED",  x+(rw+2)*2,  y, rw, 20, ID_BTN_RATE7);
        m_btnRate5 = MakeBtn(L"FINE", x+(rw+2)*3,  y, rw, 20, ID_BTN_RATE5);
        y += 24;

        // ── D-pad ─────────────────────────────────────────────
        int dpW = (w - 4) / 3;
        int dpH = 26;
        int dpCX = x + dpW + 2;
        m_btnN = MakeBtn(L"N", dpCX,          y,          dpW, dpH, ID_BTN_N);
        y += dpH + 2;
        m_btnW    = MakeBtn(L"W",    x,             y,      dpW, dpH, ID_BTN_W);
        m_btnStop = MakeBtn(L"STOP", dpCX,           y,      dpW, dpH, ID_BTN_STOP);
        m_btnE    = MakeBtn(L"E",    x + dpW*2 + 4,  y,      dpW, dpH, ID_BTN_E);
        y += dpH + 2;
        m_btnS = MakeBtn(L"S", dpCX,          y,          dpW, dpH, ID_BTN_S);
        y += dpH + 6;

        // Subclass jog buttons for hold-to-slew
        SubclassJog(m_btnN); SubclassJog(m_btnS);
        SubclassJog(m_btnE); SubclassJog(m_btnW);

        // Remove visual themes from jog buttons
        SetWindowTheme(m_btnN, L"", L"");
        SetWindowTheme(m_btnS, L"", L"");
        SetWindowTheme(m_btnE, L"", L"");
        SetWindowTheme(m_btnW, L"", L"");
        SetWindowTheme(m_btnStop, L"", L"");

        // ── Tracking + Park side by side ─────────────────────
        int trkW = (w - 2) / 2;
        m_btnTracking = MakeBtn(L"Tracking", x,          y, trkW, 22, ID_BTN_TRACKING);
        m_btnPark     = MakeBtn(L"Park",     x+trkW+2,   y, trkW, 22, ID_BTN_PARK);
        SetWindowTheme(m_btnTracking, L"", L"");
        SetWindowTheme(m_btnPark,     L"", L"");
    }

    // ── Jog button subclass — hold-to-slew ────────────────────
    void SubclassJog(HWND btn) {
        SetWindowSubclass(btn, JogProc, (UINT_PTR)btn, (DWORD_PTR)this);
    }

    static LRESULT CALLBACK JogProc(HWND hwnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam, UINT_PTR /*id*/, DWORD_PTR data) {
        MountController* self = reinterpret_cast<MountController*>(data);
        if (!self) return DefSubclassProc(hwnd, msg, wParam, lParam);

        int id = GetDlgCtrlID(hwnd);
        auto getAxisDir = [](int btnId, int& axis, int& dir) {
            switch (btnId) {
                case ID_BTN_N: axis=1; dir=0; break;
                case ID_BTN_S: axis=1; dir=1; break;
                case ID_BTN_E: axis=0; dir=0; break;
                case ID_BTN_W: axis=0; dir=1; break;
                default: axis=-1; dir=0;
            }
        };

        if (msg == WM_LBUTTONDOWN) {
            int axis, dir;
            getAxisDir(id, axis, dir);
            if (axis >= 0 && self->m_mount && self->m_mount->IsConnected()) {
                SetCapture(hwnd);
                if (self->m_onJog) self->m_onJog(axis, dir, true);
                printf("[MountCtrl] JogStart axis=%d dir=%d\n", axis, dir);
            }
            return 0;
        }
        if (msg == WM_LBUTTONUP) {
            ReleaseCapture();
            int axis, dir;
            getAxisDir(id, axis, dir);
            if (axis >= 0 && self->m_mount && self->m_mount->IsConnected()) {
                if (self->m_onJog) self->m_onJog(axis, dir, false);
                printf("[MountCtrl] JogStop axis=%d dir=%d\n", axis, dir);
            }
            return 0;
        }
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    // ── Rate button state ──────────────────────────────────────
    void UpdateRateButtons() {
        // Highlight active rate — change text to indicate selection
        struct { HWND btn; int rate; } btns[] = {
            {m_btnRate9, 9}, {m_btnRate8, 8},
            {m_btnRate7, 7}, {m_btnRate5, 5}
        };
        for (auto& b : btns) {
            if (!b.btn) continue;
            wchar_t cur[32];
            GetWindowTextW(b.btn, cur, 32);
            // Strip any ▶ prefix
            wchar_t clean[32];
            const wchar_t* src = (cur[0] == L'\u25BA' || cur[0] == L' ') ? cur + 2 : cur;
            wcscpy_s(clean, src);
            // Add ▶ if active
            if (b.rate == m_jogRate) {
                wchar_t marked[32];
                swprintf(marked, 32, L"\u25BA %s", clean);
                SetWindowTextW(b.btn, marked);
            } else {
                SetWindowTextW(b.btn, clean);
            }
            InvalidateRect(b.btn, nullptr, TRUE);
        }
    }

    // ── WndProc ───────────────────────────────────────────────
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        MountController* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            self = static_cast<MountController*>(cs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->m_hwnd = hwnd;
        } else {
            self = reinterpret_cast<MountController*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }
        if (!self) return DefWindowProc(hwnd, msg, wParam, lParam);

        switch (msg) {

        case WM_TIMER:
            if (wParam == TIMER_READOUT) self->UpdateReadouts();
            return 0;

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            switch (id) {
            case ID_BTN_STOP:
                // Big red STOP — calls emergency stop callback
                if (self->m_mount && self->m_mount->IsConnected()) {
                    self->m_mount->StopSlew(0);
                    self->m_mount->StopSlew(1);
                    printf("[MountCtrl] STOP pressed — both axes stopped\n");
                }
                if (self->m_onEmergencyStop) self->m_onEmergencyStop();
                return 0;

            case ID_BTN_RATE9: self->m_jogRate = 9; self->UpdateRateButtons(); return 0;
            case ID_BTN_RATE8: self->m_jogRate = 8; self->UpdateRateButtons(); return 0;
            case ID_BTN_RATE7: self->m_jogRate = 7; self->UpdateRateButtons(); return 0;
            case ID_BTN_RATE5: self->m_jogRate = 5; self->UpdateRateButtons(); return 0;

            case ID_BTN_TRACKING:
                if (self->m_mount && self->m_mount->IsConnected()) {
                    auto mode = self->m_mount->GetTrackingMode();
                    auto next = (mode == raduk::core::IMount::OFF)
                                ? raduk::core::IMount::ALT_AZ
                                : raduk::core::IMount::OFF;
                    self->m_mount->SetTrackingMode(next);
                    self->UpdateReadouts();
                }
                return 0;

            case ID_BTN_PARK:
                // Park = stop tracking + stop all slew
                if (self->m_mount && self->m_mount->IsConnected()) {
                    self->m_mount->StopSlew(0);
                    self->m_mount->StopSlew(1);
                    self->m_mount->SetTrackingMode(raduk::core::IMount::OFF);
                    printf("[MountCtrl] Park: tracking OFF, slew stopped\n");
                    self->UpdateReadouts();
                }
                return 0;
            }
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, RGB(7, 13, 22));
            SetTextColor(hdc, RGB(0, 229, 255));  // cyan readout text
            static HBRUSH hBg = CreateSolidBrush(RGB(7, 13, 22));
            return (LRESULT)hBg;
        }

        case WM_CTLCOLORBTN: {
            HDC hdc  = (HDC)wParam;
            HWND btn = (HWND)lParam;
            int id   = GetDlgCtrlID(btn);

            SetBkMode(hdc, OPAQUE);

            if (id == ID_BTN_STOP) {
                // Big STOP — always bright red
                SetBkColor(hdc,   RGB(180, 10, 10));
                SetTextColor(hdc, RGB(255, 255, 255));
                static HBRUSH hRed = CreateSolidBrush(RGB(180, 10, 10));
                return (LRESULT)hRed;
            }
            if (id == ID_BTN_N || id == ID_BTN_S ||
                id == ID_BTN_E || id == ID_BTN_W) {
                // Jog buttons — dark navy
                SetBkColor(hdc,   RGB(14, 29, 46));
                SetTextColor(hdc, RGB(200, 232, 240));
                static HBRUSH hJog = CreateSolidBrush(RGB(14, 29, 46));
                return (LRESULT)hJog;
            }
            if (id == ID_BTN_TRACKING) {
                // Green when tracking, dim when off
                bool trk = self->m_mount && self->m_mount->IsConnected() &&
                           self->m_mount->GetTrackingMode() != raduk::core::IMount::OFF;
                COLORREF bg = trk ? RGB(0, 80, 30) : RGB(14, 29, 46);
                COLORREF fg = trk ? RGB(77, 255, 145) : RGB(140, 175, 190);
                SetBkColor(hdc, bg); SetTextColor(hdc, fg);
                HBRUSH h = CreateSolidBrush(bg);
                return (LRESULT)h;
            }
            if (id == ID_BTN_PARK) {
                // Amber - park/stop action
                SetBkColor(hdc,   RGB(50, 15, 5));
                SetTextColor(hdc, RGB(255, 140, 60));
                static HBRUSH hPark = CreateSolidBrush(RGB(50, 15, 5));
                return (LRESULT)hPark;
            }
            // Rate buttons — highlight active
            {
                int rateMap[] = {0, 0, 0, 0, 0, 9, 8, 7, 0, 5}; // ID→rate (ID_BTN_RATE9=104)
                int rate = (id >= ID_BTN_RATE9 && id <= ID_BTN_RATE5)
                           ? (id == ID_BTN_RATE9 ? 9
                            : id == ID_BTN_RATE8 ? 8
                            : id == ID_BTN_RATE7 ? 7 : 5)
                           : -1;
                bool active = (rate == self->m_jogRate);
                SetBkColor(hdc,   active ? RGB(0, 60, 80)     : RGB(14, 29, 46));
                SetTextColor(hdc, active ? RGB(0, 229, 255)   : RGB(140, 175, 190));
                static HBRUSH hAct = CreateSolidBrush(RGB(0, 60, 80));
                static HBRUSH hDim = CreateSolidBrush(RGB(14, 29, 46));
                return (LRESULT)(active ? hAct : hDim);
            }
        }

        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wParam;
            RECT r; GetClientRect(hwnd, &r);
            static HBRUSH hBg = CreateSolidBrush(RGB(7, 13, 22));
            FillRect(hdc, &r, hBg);
            return 1;
        }

        case WM_CLOSE:
            // Don't destroy — just hide. Mount controller is permanent.
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, TIMER_READOUT);
            return 0;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    // ── Control factory helpers ───────────────────────────────
    HWND MakeBtn(const wchar_t* label, int x, int y, int w, int h, int id) {
        return CreateWindowExW(0, L"BUTTON", label,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
            x, y, w, h, m_hwnd,
            (HMENU)(intptr_t)id, GetModuleHandle(nullptr), nullptr);
    }
    HWND MakeLbl(const wchar_t* text, int x, int y, int w, int h, int id) {
        return CreateWindowExW(0, L"STATIC", text,
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            x, y, w, h, m_hwnd,
            (HMENU)(intptr_t)id, GetModuleHandle(nullptr), nullptr);
    }
    void SetText(HWND hwnd, const wchar_t* text) {
        if (hwnd) SetWindowTextW(hwnd, text);
    }

    // ── Members ───────────────────────────────────────────────
    HWND m_hwnd        = nullptr;
    raduk::core::IMount* m_mount = nullptr;
    int  m_jogRate     = 9;

    // Controls
    HWND m_lblRA       = nullptr;
    HWND m_lblDec      = nullptr;
    HWND m_lblTrk      = nullptr;
    HWND m_btnN        = nullptr;
    HWND m_btnS        = nullptr;
    HWND m_btnE        = nullptr;
    HWND m_btnW        = nullptr;
    HWND m_btnStop     = nullptr;
    HWND m_btnRate9    = nullptr;
    HWND m_btnRate8    = nullptr;
    HWND m_btnRate7    = nullptr;
    HWND m_btnRate5    = nullptr;
    HWND m_btnTracking = nullptr;
    HWND m_btnPark     = nullptr;

    std::function<void(int, int, bool)> m_onJog;
    std::function<void()>               m_onEmergencyStop;
};

} // namespace raduk::gui
