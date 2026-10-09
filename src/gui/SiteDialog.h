#pragma once
// ============================================================
// SiteDialog.h — Phase 66: ask for the observing site, once
//
// Last resort in the site chain. Reached only when rig.ini has no
// site, Windows Location declined or failed, no GPS receiver answered,
// and the mount has no site of its own.
//
// Before this the app fell back to 0,0 — a real place in the Gulf of
// Guinea — and pushed it to the mount, which broke alt/az tracking,
// the sky map and the ADC model with nothing on screen to say why. A
// dialog that states the problem and takes thirty seconds to fill in
// is strictly better than a wrong answer nobody was told about.
//
// Built in code as a WS_POPUP, matching the profile editor and the
// startup device dialog; the project carries no .rc dialog resources.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace raduk::gui {

struct SiteDialogResult {
    bool   accepted = false;
    double latDeg   = 0.0;
    double lonDeg   = 0.0;
    double elevM    = 0.0;
};

// Modal. `owner` and `panel` are disabled for the duration, matching
// the profile editor — otherwise key routing keeps reaching them.
// `why` explains which sources were tried, so the user knows this is
// a fallback rather than a routine question.
inline SiteDialogResult AskForSite(HWND owner, HWND panel,
                                   const std::string& why) {
    SiteDialogResult out;

    static bool registered = false;
    static const wchar_t* kClass = L"RadukSiteDialog";
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = DefWindowProcW;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(7, 13, 22));
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        registered = true;
    }

    const int DW = 460, DH = 300;
    RECT wr = {};
    if (owner) GetWindowRect(owner, &wr);
    int dx = wr.left + ((wr.right - wr.left) - DW) / 2;
    int dy = wr.top  + ((wr.bottom - wr.top) - DH) / 2;
    if (dx < 0 || dx > 10000) dx = 200;
    if (dy < 0 || dy > 10000) dy = 200;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass,
                               L"Observing site required",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                               dx, dy, DW, DH,
                               owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!dlg) return out;

    enum { ID_LAT = 2001, ID_LON, ID_ELEV, ID_OK, ID_SKIP };

    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                  int x, int y, int w, int h, int id) -> HWND {
        return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                               x, y, w, h, dlg, (HMENU)(INT_PTR)id,
                               GetModuleHandleW(nullptr), nullptr);
    };

    int y = 12;
    mk(L"STATIC",
       L"RadukCapture needs to know where it is. Alt/az tracking rates, the "
       L"sky map and the atmospheric dispersion model are all computed from "
       L"your latitude and longitude.",
       0, 14, y, DW - 34, 52, -1);
    y += 56;

    wchar_t whyW[256] = {};
    MultiByteToWideChar(CP_UTF8, 0, why.c_str(), -1, whyW, 255);
    mk(L"STATIC", whyW, 0, 14, y, DW - 34, 34, -1);
    y += 38;

    mk(L"STATIC", L"Latitude  (+N, e.g. 33.86):",  0, 14, y + 3, 190, 20, -1);
    HWND eLat  = mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 212, y, 120, 22, ID_LAT);
    y += 30;
    mk(L"STATIC", L"Longitude (+E, e.g. -112.09):", 0, 14, y + 3, 190, 20, -1);
    HWND eLon  = mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 212, y, 120, 22, ID_LON);
    y += 30;
    mk(L"STATIC", L"Elevation in metres (optional):", 0, 14, y + 3, 190, 20, -1);
    HWND eElev = mk(L"EDIT", L"0", WS_BORDER | ES_AUTOHSCROLL, 212, y, 120, 22, ID_ELEV);
    y += 36;

    HWND bOk   = mk(L"BUTTON", L"Save to rig.ini", BS_DEFPUSHBUTTON, DW - 250, y, 130, 26, ID_OK);
    HWND bSkip = mk(L"BUTTON", L"Skip",            BS_PUSHBUTTON,    DW - 112, y, 90,  26, ID_SKIP);
    (void)bOk; (void)bSkip;

    struct State { bool done = false; SiteDialogResult* out = nullptr; };
    State st; st.out = &out;
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&st);

    struct Sub {
        static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp,
                                     UINT_PTR, DWORD_PTR) {
            auto* s = (State*)GetWindowLongPtrW(h, GWLP_USERDATA);
            if (!s) return DefSubclassProc(h, m, wp, lp);
            if (m == WM_COMMAND) {
                int id = LOWORD(wp);
                if (id == ID_SKIP) { s->done = true; return 0; }
                if (id == ID_OK) {
                    auto readD = [&](int ctl, double& v) -> bool {
                        wchar_t buf[64] = {};
                        GetDlgItemTextW(h, ctl, buf, 63);
                        if (!buf[0]) return false;
                        wchar_t* end = nullptr;
                        double d = wcstod(buf, &end);
                        return end && end != buf && (v = d, true);
                    };
                    double la = 0, lo = 0, el = 0;
                    bool okLa = readD(ID_LAT, la), okLo = readD(ID_LON, lo);
                    readD(ID_ELEV, el);
                    // Validate before accepting: a typo here propagates
                    // straight to the mount, which is the failure mode
                    // this whole phase exists to prevent.
                    if (!okLa || !okLo || la < -90 || la > 90 ||
                        lo < -180 || lo > 180) {
                        MessageBoxW(h,
                            L"Latitude must be between -90 and 90, longitude "
                            L"between -180 and 180.",
                            L"Check the coordinates", MB_OK | MB_ICONWARNING);
                        return 0;
                    }
                    s->out->latDeg   = la;
                    s->out->lonDeg   = lo;
                    s->out->elevM    = el;
                    s->out->accepted = true;
                    s->done = true;
                    return 0;
                }
            }
            if (m == WM_CLOSE) { s->done = true; return 0; }
            return DefSubclassProc(h, m, wp, lp);
        }
    };
    SetWindowSubclass(dlg, Sub::Proc, 1, 0);

    if (owner) EnableWindow(owner, FALSE);
    if (panel) EnableWindow(panel, FALSE);
    SetForegroundWindow(dlg);
    SetFocus(eLat);
    (void)eLon; (void)eElev;

    MSG msg = {};
    while (!st.done) {
        if (!GetMessageW(&msg, nullptr, 0, 0)) break;   // WM_QUIT propagates
        if (!IsDialogMessageW(dlg, &msg)) {             // Tab / Enter / Esc
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (owner) EnableWindow(owner, TRUE);
    if (panel) EnableWindow(panel, TRUE);
    DestroyWindow(dlg);
    if (owner) SetForegroundWindow(owner);
    return out;
}

} // namespace raduk::gui
