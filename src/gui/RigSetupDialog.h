#pragma once
// ============================================================
// RigSetupDialog.h — Phase 67 step 3: see the layout you described
//
// By the time this was built, steps 4, 5 and 6 already worked from
// numbers alone: the align helper asks one question, and the
// keep-out sector is two values in rig.ini. So this is not an input
// mechanism the rest depends on. Its job is verification.
//
// A bearing typed into a file is unfalsifiable. Drawn against a
// compass, with the observer and the mount's live azimuth on it, a
// wrong answer is obvious at a glance. That check is the reason to
// draw it at all, which is why the controls stay numeric and the
// picture is a mirror rather than a canvas.
//
// Orientation note: this is a GROUND plan, looking down, so north is
// up and east is RIGHT. The sky map is the opposite - a chart read
// looking up, where east is left. Drawing them the same way would be
// the mistake.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Theme.h"
#include <windows.h>
#include <commctrl.h>
#include <cmath>
#include <string>
#include "core/RigConfig.h"
#include "core/SkyMath.h"

namespace raduk::gui {

// liveAz supplies the mount's current azimuth, or <0 when unknown.
inline void ShowRigSetupDialog(HWND owner, HWND panel,
                               std::function<double()> liveAz) {
    using namespace raduk::core;
    auto& rig = RigConfig::Get();

    struct State {
        bool   done     = false;
        double observer = 0.0;
        double koStart  = 0.0;
        double koEnd    = 0.0;
        double wrapLim  = 360.0;
        std::function<double()>* liveAz = nullptr;
        HWND   canvas   = nullptr;
    };
    static State st;              // static: the paint handler needs it
    st = State{};
    st.observer = rig.ObserverBearingSet() ? rig.observerBearingDeg : 0.0;
    st.koStart  = rig.keepOutStartDeg;
    st.koEnd    = rig.keepOutEndDeg;
    st.wrapLim  = rig.wrapLimitDeg;
    st.liveAz   = &liveAz;

    static bool registered = false;
    static const wchar_t* kClass = L"RadukRigSetup";
    static const wchar_t* kCanvas = L"RadukRigCanvas";
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = DefWindowProcW;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(Theme::NV(Theme::Rgb::DEEP));
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);

        WNDCLASSEXW cc = wc;
        cc.lpszClassName = kCanvas;
        cc.lpfnWndProc = [](HWND h, UINT m, WPARAM wp, LPARAM lp) -> LRESULT {
            if (m != WM_PAINT) return DefWindowProcW(h, m, wp, lp);
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc; GetClientRect(h, &rc);
            HBRUSH bg = CreateSolidBrush(Theme::NV(Theme::Rgb::DEEP));
            FillRect(dc, &rc, bg); DeleteObject(bg);

            int cx = (rc.right - rc.left) / 2;
            int cy = (rc.bottom - rc.top) / 2;
            int R  = (cx < cy ? cx : cy) - 22;

            SetBkMode(dc, TRANSPARENT);

            // Ground plan: north up, east RIGHT.
            auto pt = [&](double bearing, double rad, int& x, int& y) {
                double a = bearing * 3.14159265358979 / 180.0;
                x = cx + (int)(rad * std::sin(a));
                y = cy - (int)(rad * std::cos(a));
            };

            // Keep-out wedge first, so everything else draws over it.
            if (st.koStart != st.koEnd) {
                HBRUSH kb = CreateSolidBrush(Theme::NV(RGB(70, 30, 30)));
                HPEN   kp = CreatePen(PS_SOLID, 1, Theme::NV(RGB(150, 60, 60)));
                HGDIOBJ ob = SelectObject(dc, kb), op = SelectObject(dc, kp);
                int sx, sy, ex, ey;
                pt(st.koStart, R, sx, sy);
                pt(st.koEnd,   R, ex, ey);
                // Pie runs anticlockwise in GDI, so the clockwise arc
                // from start to end is drawn end-first.
                Pie(dc, cx - R, cy - R, cx + R, cy + R, ex, ey, sx, sy);
                SelectObject(dc, ob); SelectObject(dc, op);
                DeleteObject(kb); DeleteObject(kp);
            }

            HPEN grid = CreatePen(PS_SOLID, 1, Theme::NV(RGB(60, 80, 100)));
            HGDIOBJ oldPen = SelectObject(dc, grid);
            SelectObject(dc, GetStockObject(NULL_BRUSH));
            Ellipse(dc, cx - R, cy - R, cx + R, cy + R);
            SelectObject(dc, oldPen); DeleteObject(grid);

            SetTextColor(dc, Theme::NV(Theme::Rgb::TEXT_DIM));
            const wchar_t* lbl[] = {L"N", L"E", L"S", L"W"};
            for (int i = 0; i < 4; i++) {
                int x, y; pt(i * 90.0, R + 12, x, y);
                TextOutW(dc, x - 4, y - 8, lbl[i], 1);
            }

            // Observer, and the line of sight to the mount.
            {
                int x, y; pt(st.observer, R - 8, x, y);
                HPEN p = CreatePen(PS_SOLID, 2, Theme::NV(RGB(90, 200, 120)));
                HGDIOBJ o = SelectObject(dc, p);
                MoveToEx(dc, cx, cy, nullptr); LineTo(dc, x, y);
                SelectObject(dc, o); DeleteObject(p);
                SetTextColor(dc, Theme::NV(RGB(90, 200, 120)));
                TextOutW(dc, x - 14, y - 6, L"you", 3);
            }

            // Live mount azimuth, when there is one.
            double az = st.liveAz && *st.liveAz ? (*st.liveAz)() : -1.0;
            if (az >= 0.0) {
                int x, y; pt(az, R - 2, x, y);
                HPEN p = CreatePen(PS_SOLID, 2, Theme::NV(RGB(230, 180, 60)));
                HGDIOBJ o = SelectObject(dc, p);
                MoveToEx(dc, cx, cy, nullptr); LineTo(dc, x, y);
                SelectObject(dc, o); DeleteObject(p);
                SetTextColor(dc, Theme::NV(RGB(230, 180, 60)));
                TextOutW(dc, x + 2, y - 6, L"scope", 5);
            }

            // The mount itself.
            HBRUSH mb = CreateSolidBrush(Theme::NV(Theme::Rgb::TEXT));
            HGDIOBJ om = SelectObject(dc, mb);
            Ellipse(dc, cx - 5, cy - 5, cx + 5, cy + 5);
            SelectObject(dc, om); DeleteObject(mb);

            EndPaint(h, &ps);
            return 0;
        };
        RegisterClassExW(&cc);
        registered = true;
    }

    const int DW = 680, DH = 470;
    RECT wr = {};
    if (owner) GetWindowRect(owner, &wr);
    int dx = wr.left + ((wr.right - wr.left) - DW) / 2;
    int dy = wr.top  + ((wr.bottom - wr.top) - DH) / 2;
    if (dx < 0 || dx > 10000) dx = 220;
    if (dy < 0 || dy > 10000) dy = 140;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, L"Rig setup",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                               dx, dy, DW, DH, owner, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
    if (!dlg) return;

    enum { ID_OBS = 5001, ID_KOS, ID_KOE, ID_WRAP, ID_FILTER, ID_SAVE, ID_CANCEL };

    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                  int x, int y, int w, int h, int id) -> HWND {
        return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                               x, y, w, h, dlg, (HMENU)(INT_PTR)id,
                               GetModuleHandleW(nullptr), nullptr);
    };

    st.canvas = CreateWindowExW(0, kCanvas, L"", WS_CHILD | WS_VISIBLE,
                                14, 14, 330, 360, dlg, nullptr,
                                GetModuleHandleW(nullptr), nullptr);

    // Right column. Each label gets its own line and real height --
    // the first attempt clipped three lines into a two-line box and
    // pushed the controls below it off the dialog.
    const int fx = 360, fw = DW - fx - 24;
    int fy = 16;
    auto label = [&](const wchar_t* t, int h = 18) {
        mk(L"STATIC", t, SS_LEFT, fx, fy, fw, h, -1);
        fy += h + 2;
    };

    label(L"Check the drawing against what is actually in front", 16);
    label(L"of you. A bearing typed into a file is unfalsifiable;", 16);
    label(L"one drawn beside the scope is not.", 16);
    fy += 10;

    wchar_t b[32];
    label(L"Observer bearing - mount to you (deg true):");
    swprintf(b, 32, L"%.0f", st.observer);
    mk(L"EDIT", b, WS_BORDER | WS_TABSTOP, fx, fy, 80, 22, ID_OBS);
    fy += 32;

    label(L"Keep-out sector, clockwise (deg):");
    swprintf(b, 32, L"%.0f", st.koStart);
    mk(L"EDIT", b, WS_BORDER | WS_TABSTOP, fx, fy, 80, 22, ID_KOS);
    mk(L"STATIC", L"to", SS_CENTER, fx + 86, fy + 3, 20, 16, -1);
    swprintf(b, 32, L"%.0f", st.koEnd);
    mk(L"EDIT", b, WS_BORDER | WS_TABSTOP, fx + 112, fy, 80, 22, ID_KOE);
    fy += 26;
    label(L"(equal values = no sector)", 16);
    fy += 10;

    label(L"Cord-wrap limit (deg of travel):");
    swprintf(b, 32, L"%.0f", st.wrapLim);
    mk(L"EDIT", b, WS_BORDER | WS_TABSTOP, fx, fy, 80, 22, ID_WRAP);
    fy += 32;

    // Phase 83: the filter is a physical part the software cannot see.
    // It lives here rather than behind a launch prompt because it only
    // changes when the camera comes off the scope -- asking nightly
    // about a yearly event is the speed bump Readiness.h warns about.
    label(L"Filter fitted to the camera:");
    {
        HWND cb = mk(L"COMBOBOX", L"",
                     CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                     fx, fy, 200, 24, ID_FILTER);
        int pn = 0;
        const auto* pp = RigConfig::Presets(pn);
        int sel = -1;
        for (int i = 0; i < pn; i++) {
            wchar_t wn[64];
            swprintf(wn, 64, L"%hs (%.0f-%.0fnm)",
                     pp[i].name, pp[i].lowNm, pp[i].highNm);
            SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)wn);
            if (_stricmp(pp[i].name, RigConfig::Get().filterName.c_str()) == 0)
                sel = i;
        }
        // A rig.ini with hand-edited band edges matches no preset. Leave
        // the box blank rather than showing a preset the model is not
        // using, and leave the band alone unless one is chosen.
        if (sel >= 0) SendMessageW(cb, CB_SETCURSEL, sel, 0);
    }
    fy += 30;
    {
        wchar_t cur[80];
        swprintf(cur, 80, L"(in use: %hs, %.0f-%.0fnm)",
                 RigConfig::Get().filterName.c_str(),
                 RigConfig::Get().filterLowNm, RigConfig::Get().filterHighNm);
        label(cur, 16);
    }
    fy += 10;

    mk(L"BUTTON", L"Save", BS_DEFPUSHBUTTON, DW - 230, DH - 70, 100, 26, ID_SAVE);
    mk(L"BUTTON", L"Cancel", BS_PUSHBUTTON,  DW - 122, DH - 70, 100, 26, ID_CANCEL);

    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&st);

    struct Sub {
        static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp,
                                     UINT_PTR, DWORD_PTR) {
            auto* s = (State*)GetWindowLongPtrW(h, GWLP_USERDATA);
            if (!s) return DefSubclassProc(h, m, wp, lp);

            auto readD = [&](int id, double& v) {
                wchar_t buf[64] = {};
                GetDlgItemTextW(h, id, buf, 63);
                if (!buf[0]) return;
                wchar_t* e = nullptr;
                double d = wcstod(buf, &e);
                if (e && e != buf) v = d;
            };

            if (m == WM_TIMER || (m == WM_COMMAND && HIWORD(wp) == EN_CHANGE)) {
                // Redraw as the numbers are typed, so the picture and
                // the fields can never disagree.
                readD(ID_OBS, s->observer);
                readD(ID_KOS, s->koStart);
                readD(ID_KOE, s->koEnd);
                readD(ID_WRAP, s->wrapLim);
                if (s->canvas) InvalidateRect(s->canvas, nullptr, TRUE);
                return 0;
            }
            if (m == WM_COMMAND) {
                int id = LOWORD(wp);
                if (id == ID_CANCEL) { s->done = true; return 0; }
                if (id == ID_SAVE) {
                    readD(ID_OBS, s->observer);
                    readD(ID_KOS, s->koStart);
                    readD(ID_KOE, s->koEnd);
                    readD(ID_WRAP, s->wrapLim);
                    auto& r = RigConfig::Get();
                    r.observerBearingDeg = NormDeg360(s->observer);
                    r.keepOutStartDeg    = NormDeg360(s->koStart);
                    r.keepOutEndDeg      = NormDeg360(s->koEnd);
                    if (s->wrapLim > 10.0) r.wrapLimitDeg = s->wrapLim;
                    // CB_ERR means nothing was picked -- a hand-edited
                    // band, left alone on purpose. Only an explicit
                    // choice overwrites the edges.
                    HWND cb = GetDlgItem(h, ID_FILTER);
                    LRESULT ci = cb ? SendMessageW(cb, CB_GETCURSEL, 0, 0)
                                    : CB_ERR;
                    if (ci != CB_ERR) {
                        int pn = 0;
                        const auto* pp = RigConfig::Presets(pn);
                        if (ci >= 0 && ci < pn) r.ApplyFilterPreset(pp[ci].name);
                    }
                    r.Save();
                    printf("[RigSetup] Saved: observer %.0f, keep-out %.0f-%.0f, "
                           "wrap limit %.0f, filter %s (%.0f-%.0fnm)\n",
                           r.observerBearingDeg,
                           r.keepOutStartDeg, r.keepOutEndDeg, r.wrapLimitDeg,
                           r.filterName.c_str(), r.filterLowNm, r.filterHighNm);
                    s->done = true;
                    return 0;
                }
            }
            if (m == WM_CTLCOLORSTATIC || m == WM_CTLCOLOREDIT) {
                // Without this the controls paint on the system default,
                // which is white against this dialog's dark background.
                HBRUSH bg = Theme::Brush(Theme::Rgb::DEEP);
                HDC dc = (HDC)wp;
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, m == WM_CTLCOLOREDIT ? Theme::NV(RGB(230, 240, 250))
                                                      : Theme::NV(Theme::Rgb::TEXT));
                if (m == WM_CTLCOLOREDIT) {
                    HBRUSH eb = Theme::Brush(Theme::Rgb::BORDER);
                    SetBkMode(dc, OPAQUE);
                    SetBkColor(dc, Theme::NV(Theme::Rgb::BORDER));
                    return (LRESULT)eb;
                }
                return (LRESULT)bg;
            }
            if (m == WM_CLOSE) { s->done = true; return 0; }
            return DefSubclassProc(h, m, wp, lp);
        }
    };
    SetWindowSubclass(dlg, Sub::Proc, 1, 0);
    SetTimer(dlg, 1, 1000, nullptr);     // keeps the live azimuth moving

    if (owner) EnableWindow(owner, FALSE);
    if (panel) EnableWindow(panel, FALSE);
    SetForegroundWindow(dlg);

    MSG msg = {};
    while (!st.done) {
        if (!GetMessageW(&msg, nullptr, 0, 0)) break;
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    KillTimer(dlg, 1);
    if (owner) EnableWindow(owner, TRUE);
    if (panel) EnableWindow(panel, TRUE);
    DestroyWindow(dlg);
    if (owner) SetForegroundWindow(owner);
}

} // namespace raduk::gui
