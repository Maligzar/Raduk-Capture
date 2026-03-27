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
    // Fired when user picks an alignment star and confirms.
    // ra_h / dec_deg = the object's true catalogue coordinates.
    // Caller should: mount->SyncToRADec(ra,dec) + skyMap.SetMountPosition(ra,dec,true)
    void SetAlignCallback(std::function<void(double ra_h, double dec_deg,
                                              const std::string& name)> cb) {
        m_onAlign = cb;
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
        if (m_btnAlign) InvalidateRect(m_btnAlign, nullptr, FALSE);
    }

private:
    // ── Layout constants ──────────────────────────────────────
    static constexpr int W = 210;
    static constexpr int H = 310;  // height with Align button
    static constexpr int MARGIN = 8;
    static constexpr int TIMER_READOUT = 1;

    // Control IDs
    enum {
        ID_BTN_STOP = 100,
        ID_BTN_N, ID_BTN_S, ID_BTN_E, ID_BTN_W,
        ID_BTN_RATE9, ID_BTN_RATE8, ID_BTN_RATE7, ID_BTN_RATE5,
        ID_BTN_ALIGN,
        ID_LBL_RA, ID_LBL_DEC, ID_LBL_TRK,
        // Align popup controls
        ID_ALIGN_LIST = 200,
        ID_ALIGN_OK   = 201,
        ID_ALIGN_CANCEL = 202,
    };

    // ── Alignment stars ───────────────────────────────────────
    // Bright stars (mag < 2.5) useful for visual alignment from Anthem AZ.
    // Sorted at runtime by current altitude so best candidates appear first.
    struct AlignStar {
        const char* name;
        double      ra_h;   // hours
        double      dec_deg;
        float       mag;
    };

    static constexpr AlignStar ALIGN_STARS[] = {
        { "Sirius",      6.7525,   -16.716,  -1.46f },
        { "Canopus",     6.3992,   -52.696,  -0.74f },
        { "Arcturus",   14.2612,    19.182,  -0.05f },
        { "Vega",       18.6156,    38.784,   0.03f },
        { "Capella",     5.2781,    45.998,   0.08f },
        { "Rigel",       5.2422,    -8.202,   0.13f },
        { "Procyon",     7.6553,     5.225,   0.34f },
        { "Achernar",    1.6286,   -57.237,   0.46f },
        { "Betelgeuse",  5.9194,     7.407,   0.50f },
        { "Hadar",      14.0637,   -60.374,   0.61f },
        { "Altair",     19.8464,     8.868,   0.76f },
        { "Acrux",      12.4432,   -63.099,   0.77f },
        { "Aldebaran",   4.5987,    16.509,   0.87f },
        { "Antares",    16.4901,   -26.432,   1.06f },
        { "Spica",      13.4199,   -11.161,   1.04f },
        { "Pollux",      7.7553,    28.026,   1.16f },
        { "Fomalhaut",  22.9608,   -29.622,   1.16f },
        { "Deneb",      20.6905,    45.280,   1.25f },
        { "Mimosa",     12.7954,   -59.689,   1.25f },
        { "Regulus",    10.1395,    11.967,   1.36f },
        { "Adhara",      6.9771,   -28.972,   1.50f },
        { "Castor",      7.5766,    31.889,   1.58f },
        { "Gacrux",     12.5194,   -57.113,   1.59f },
        { "Shaula",     17.5601,   -37.104,   1.62f },
        { "Bellatrix",   5.4186,     6.349,   1.64f },
        { "Elnath",      5.4381,    28.608,   1.65f },
        { "Miaplacidus",9.2200,   -69.716,   1.67f },
        { "Alnilam",     5.6036,    -1.202,   1.69f },
        { "Alnitak",     5.6792,    -1.943,   1.74f },
        { "Alioth",     12.9007,    55.960,   1.76f },
        { "Dubhe",      11.0621,    61.751,   1.81f },
        { "Mirfak",      3.4055,    49.861,   1.79f },
        { "Wezen",       7.1393,   -26.393,   1.83f },
        { "Kaus Australis",18.4028,-34.384,   1.85f },
        { "Avior",       8.3749,   -59.510,   1.86f },
        { "Alkaid",     13.7923,    49.313,   1.86f },
        { "Sargas",     17.6218,   -42.998,   1.87f },
        { "Menkent",    14.1114,   -36.370,   2.06f },
        { "Atria",      16.8113,   -69.028,   1.91f },
        { "Peacock",    20.4275,   -56.735,   1.94f },
        { "Polaris",     2.5303,    89.264,   1.98f },
        { "Denebola",   11.8177,    14.572,   2.14f },
        { "Nunki",      18.9211,   -26.297,   2.02f },
        { "Alphard",     9.4598,    -8.659,   1.99f },
        { "Mirach",      1.1622,    35.620,   2.07f },
    };
    static constexpr int NUM_ALIGN_STARS = 45;

    // Compute altitude of a star given LST and observer latitude
    static float StarAltitude(double ra_h, double dec_deg,
                               double lst_h, double lat_deg) {
        const double DEG = 3.14159265 / 180.0;
        double ha  = (lst_h - ra_h) * 15.0 * DEG;
        double dec = dec_deg * DEG;
        double lat = lat_deg * DEG;
        double sinAlt = sin(lat)*sin(dec) + cos(lat)*cos(dec)*cos(ha);
        return (float)(asin(sinAlt > 1.0 ? 1.0 : sinAlt < -1.0 ? -1.0 : sinAlt)
                       / DEG);
    }

    // Get approximate LST for Anthem AZ right now
    static double ApproxLST() {
        time_t t = time(nullptr);
        double jd = (t / 86400.0) + 2440587.5;
        double d  = jd - 2451545.0;
        double gmst = 18.697374558 + 24.06570982441908 * d;
        double lon_h = -112.09 / 15.0; // Anthem AZ longitude in hours
        double lst = fmod(gmst + lon_h, 24.0);
        if (lst < 0) lst += 24.0;
        return lst;
    }

    void ShowAlignPopup() {
        // Register popup class once
        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc = {};
            wc.cbSize        = sizeof(wc);
            wc.lpfnWndProc   = AlignPopupProc;
            wc.hInstance     = GetModuleHandle(nullptr);
            wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = CreateSolidBrush(RGB(7, 13, 22));
            wc.lpszClassName = L"RadukAlignPopup";
            RegisterClassExW(&wc);
            registered = true;
        }

        // Compute current star altitudes and sort — best first
        double lst = ApproxLST();
        const double LAT = 33.86;

        struct Entry { int idx; float alt; };
        std::vector<Entry> sorted;
        for (int i = 0; i < NUM_ALIGN_STARS; i++) {
            float alt = StarAltitude(ALIGN_STARS[i].ra_h, ALIGN_STARS[i].dec_deg,
                                     lst, LAT);
            if (alt > 15.0f) // only show stars above 15° horizon
                sorted.push_back({i, alt});
        }
        std::sort(sorted.begin(), sorted.end(),
            [](const Entry& a, const Entry& b) { return a.alt > b.alt; });

        if (sorted.empty()) {
            MessageBoxW(m_hwnd,
                L"No bright alignment stars above 15° right now.\n"
                L"Try again after dark or adjust your time.",
                L"Align", MB_OK | MB_ICONINFORMATION);
            return;
        }

        // Store sorted list for the popup to use
        m_alignEntries = sorted;
        m_alignLST     = lst;

        // Create popup window
        RECT wr; GetWindowRect(m_hwnd, &wr);
        int pw = 260, ph = (int)std::min((int)sorted.size(), 14) * 22 + 80;
        HWND popup = CreateWindowExW(
            WS_EX_TOPMOST,
            L"RadukAlignPopup", L"Align — Select Star You Are Centered On",
            WS_POPUP | WS_CAPTION | WS_SYSMENU,
            wr.left - pw - 4, wr.top, pw, ph,
            m_hwnd, nullptr, GetModuleHandle(nullptr), this);
        if (!popup) return;

        // List box
        HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            6, 28, pw-12, ph-66, popup, (HMENU)ID_ALIGN_LIST,
            GetModuleHandle(nullptr), nullptr);

        HFONT fnt = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        if (fnt) SendMessage(list, WM_SETFONT, (WPARAM)fnt, TRUE);

        // Populate list
        for (auto& e : sorted) {
            const auto& s = ALIGN_STARS[e.idx];
            wchar_t line[80];
            swprintf(line, 80, L"%-16S  Alt %3.0f\u00B0  mag %+.1f",
                     s.name, e.alt, s.mag);
            SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)line);
        }
        SendMessageW(list, LB_SETCURSEL, 0, 0);

        // OK / Cancel
        HFONT ufnt = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HWND ok  = CreateWindowExW(0, L"BUTTON", L"\u2713  Align Here",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            6, ph-34, 116, 26, popup, (HMENU)ID_ALIGN_OK,
            GetModuleHandle(nullptr), nullptr);
        HWND can = CreateWindowExW(0, L"BUTTON", L"\u2716  Cancel",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            126, ph-34, 116, 26, popup, (HMENU)ID_ALIGN_CANCEL,
            GetModuleHandle(nullptr), nullptr);
        if (ufnt) { SendMessage(ok,  WM_SETFONT,(WPARAM)ufnt,TRUE);
                    SendMessage(can, WM_SETFONT,(WPARAM)ufnt,TRUE); }

        ShowWindow(popup, SW_SHOW);
        SetFocus(list);
    }

    static LRESULT CALLBACK AlignPopupProc(HWND hwnd, UINT msg,
                                             WPARAM wp, LPARAM lp) {
        MountController* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCT*>(lp);
            self = static_cast<MountController*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        } else {
            self = reinterpret_cast<MountController*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

        switch (msg) {
            case WM_COMMAND: {
                int id = LOWORD(wp);
                if (id == ID_ALIGN_CANCEL) { DestroyWindow(hwnd); return 0; }
                if (id == ID_ALIGN_OK ||
                    (id == ID_ALIGN_LIST && HIWORD(wp) == LBN_DBLCLK)) {
                    HWND list = GetDlgItem(hwnd, ID_ALIGN_LIST);
                    int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
                    if (sel >= 0 && sel < (int)self->m_alignEntries.size()) {
                        int idx = self->m_alignEntries[sel].idx;
                        const AlignStar& s = ALIGN_STARS[idx];
                        printf("[Align] Syncing to: %s  RA=%.4fh  Dec=%.4f°\n",
                               s.name, s.ra_h, s.dec_deg);
                        if (self->m_onAlign)
                            self->m_onAlign(s.ra_h, s.dec_deg, s.name);
                    }
                    DestroyWindow(hwnd);
                    return 0;
                }
                return 0;
            }
            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;
            case WM_CTLCOLORLISTBOX:
            case WM_CTLCOLORBTN:
            case WM_CTLCOLORSTATIC: {
                HDC hdc = (HDC)wp;
                SetBkColor(hdc,   RGB(10, 18, 30));
                SetTextColor(hdc, RGB(180, 220, 240));
                static HBRUSH bg = CreateSolidBrush(RGB(10, 18, 30));
                return (LRESULT)bg;
            }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

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

        // ── Align button — full width ─────────────────────────
        m_btnAlign = MakeBtn(L"\u2605  ALIGN", x, y, w, 24, ID_BTN_ALIGN);
        SetWindowTheme(m_btnAlign, L"", L"");
        y += 28;

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

            case ID_BTN_ALIGN:
                self->ShowAlignPopup();
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
            if (id == ID_BTN_ALIGN) {
                // Align button — gold/amber accent
                SetBkColor(hdc,   RGB(40, 30, 5));
                SetTextColor(hdc, RGB(255, 200, 60));
                static HBRUSH hAlign = CreateSolidBrush(RGB(40, 30, 5));
                return (LRESULT)hAlign;
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
    HWND m_btnAlign    = nullptr;

    // Align popup state
    struct AlignEntry { int idx; float alt; };
    std::vector<AlignEntry> m_alignEntries;
    double                  m_alignLST = 0;

    std::function<void(int, int, bool)> m_onJog;
    std::function<void()>               m_onEmergencyStop;
    std::function<void(double, double, const std::string&)> m_onAlign;
};

} // namespace raduk::gui
