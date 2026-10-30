// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once
// ============================================================
// ObjectInfoPanel.h — Phase 50a: Object Knowledge Panel
//
// Floating WS_EX_TOPMOST | WS_EX_TOOLWINDOW window showing
// full knowledge for any astronomical object from the DB.
//
// Opens from:
//   - Sky map right-click on an object
//   - Tonight planner double-click on a row
//   - AI chat "more info" responses (future)
//
// Layout:
//   ┌─────────────────────────────────┐
//   │  OBJECT NAME        [type/const]│  ← header (cyan)
//   │  Mag X.X  Dist XXX ly  Alt XX° │  ← quick stats
//   ├─────────────────────────────────┤
//   │  ABOUT                          │  ← Wikipedia summary
//   │  ...                            │
//   ├─────────────────────────────────┤
//   │  DISCOVERY / MYTHOLOGY          │
//   │  ...                            │
//   ├─────────────────────────────────┤
//   │  IMAGING NOTES                  │  ← from hardcoded DB
//   │  Aperture: 8"+  Filters: Ha,OIII│
//   │  ...                            │
//   ├─────────────────────────────────┤
//   │            [GoTo]  [Close]      │
//   └─────────────────────────────────┘
//
// If DB not available, shows basic ephemeris data only.
// ============================================================

#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "Theme.h"
#include <string>
#include <functional>
#include <sstream>
#include <cstdio>
#include "../core/ObjectKnowledgeDB.h"

namespace raduk::gui {

class ObjectInfoPanel {
public:
    ObjectInfoPanel() = default;
    ~ObjectInfoPanel() { Destroy(); }

    // Create once — reused for every object lookup
    bool Create(HWND parent) {
        m_parent = parent;

        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = PanelWndProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(Theme::NV(RGB(7, 13, 22)));
        wc.lpszClassName = L"RadukObjectInfo";
        RegisterClassExW(&wc);

        m_hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"RadukObjectInfo", L"Object Info",
            WS_POPUP | WS_CAPTION | WS_SYSMENU,
            100, 100, PANEL_W, PANEL_H,
            parent, nullptr, GetModuleHandle(nullptr), this);

        if (!m_hwnd) return false;
        CreateControls();
        printf("[ObjectInfoPanel] Created\n");
        return true;
    }

    void Destroy() {
        if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    }

    bool IsVisible() const { return m_hwnd && IsWindowVisible(m_hwnd); }

    // Wire knowledge DB — optional, panel works without it
    void SetKnowledgeDB(raduk::core::ObjectKnowledgeDB* db) { m_db = db; }

    // Wire GoTo callback — fires when user clicks [GoTo] in panel
    void SetGoToCallback(std::function<void(const std::string&)> cb) {
        m_onGoTo = cb;
    }

    // Show panel for a named object with optional current alt/az
    void ShowObject(const std::string& name, float altDeg = -999,
                    float azDeg = -999, float mag = 99) {
        if (!m_hwnd) return;
        m_currentName = name;
        m_currentAlt  = altDeg;
        m_currentAz   = azDeg;
        m_currentMag  = mag;

        PopulateContent(name, altDeg, azDeg, mag);

        // Position near parent centre
        RECT pr; GetWindowRect(m_parent, &pr);
        int cx = (pr.left + pr.right)  / 2;
        int cy = (pr.top  + pr.bottom) / 2;
        int wx = cx - PANEL_W / 2 + 80;  // slight offset so it doesn't cover the map
        int wy = cy - PANEL_H / 2;
        // Keep on screen
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        if (wx + PANEL_W > sw - 10) wx = sw - PANEL_W - 10;
        if (wx < 10)                 wx = 10;
        if (wy + PANEL_H > sh - 40) wy = sh - PANEL_H - 40;
        if (wy < 10)                 wy = 10;

        SetWindowPos(m_hwnd, HWND_TOPMOST, wx, wy, 0, 0,
                     SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(m_hwnd);
    }

    void Hide() { if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE); }

private:
    static constexpr int PANEL_W = 440;
    static constexpr int PANEL_H = 580;

    enum : int {
        ID_EDIT_CONTENT = 4300,
        ID_BTN_GOTO     = 4301,
        ID_BTN_CLOSE    = 4302,
        ID_LBL_HEADER   = 4303,
    };

    void CreateControls() {
        RECT cr; GetClientRect(m_hwnd, &cr);
        int cw = cr.right, ch = cr.bottom;
        const int PAD = 6, BH = 28, HDR = 44;

        // Header label — object name + quick stats
        m_lblHeader = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
            PAD, PAD, cw - PAD*2, HDR,
            m_hwnd, (HMENU)ID_LBL_HEADER,
            GetModuleHandle(nullptr), nullptr);

        // Content area — read-only multiline EDIT
        m_editContent = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            PAD, HDR + PAD, cw - PAD*2, ch - HDR - BH - PAD*3,
            m_hwnd, (HMENU)ID_EDIT_CONTENT,
            GetModuleHandle(nullptr), nullptr);

        // Buttons
        int btnY = ch - BH - PAD;
        int bw   = (cw - PAD*3) / 2;
        m_btnGoTo  = CreateWindowExW(0, L"BUTTON", L"GoTo Object",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            PAD, btnY, bw, BH,
            m_hwnd, (HMENU)ID_BTN_GOTO,
            GetModuleHandle(nullptr), nullptr);
        m_btnClose = CreateWindowExW(0, L"BUTTON", L"Close",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            PAD*2 + bw, btnY, bw, BH,
            m_hwnd, (HMENU)ID_BTN_CLOSE,
            GetModuleHandle(nullptr), nullptr);

        // Fonts
        HFONT hdrFont = CreateFontW(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Barlow Condensed");
        if (!hdrFont) hdrFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        HFONT bodyFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Courier New");
        if (!bodyFont) bodyFont = (HFONT)GetStockObject(SYSTEM_FIXED_FONT);

        HFONT uiFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (!uiFont) uiFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        SendMessage(m_lblHeader,   WM_SETFONT, (WPARAM)hdrFont,  TRUE);
        SendMessage(m_editContent, WM_SETFONT, (WPARAM)bodyFont, TRUE);
        SendMessage(m_btnGoTo,     WM_SETFONT, (WPARAM)uiFont,   TRUE);
        SendMessage(m_btnClose,    WM_SETFONT, (WPARAM)uiFont,   TRUE);
    }

    void PopulateContent(const std::string& name, float alt, float az, float mag) {
        if (!m_hwnd) return;

        // ── Window title ──────────────────────────────────────
        wchar_t wtitle[128];
        wchar_t wname[64];
        MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, wname, 64);
        swprintf(wtitle, 128, L"RadukScope  \u00B7  %s", wname);
        SetWindowTextW(m_hwnd, wtitle);

        // ── Header label ──────────────────────────────────────
        wchar_t hdr[256] = {};
        if (alt > -90 && az > -1) {
            swprintf(hdr, 256, L"%s    Alt %.0f\u00B0  Az %.0f\u00B0%s",
                wname, alt, az,
                mag < 90 ? ([]()->const wchar_t*{return L"";}()) : L"");
            if (mag < 90) {
                wchar_t tmp[256];
                swprintf(tmp, 256, L"%s    Mag %.1f    Alt %.0f\u00B0  Az %.0f\u00B0",
                    wname, mag, alt, az);
                wcscpy_s(hdr, 256, tmp);
            }
        } else {
            wcscpy_s(hdr, 256, wname);
        }
        SetWindowTextW(m_lblHeader, hdr);

        // ── Body content ──────────────────────────────────────
        std::ostringstream body;

        if (m_db && m_db->IsAvailable()) {
            std::string knowledge = m_db->Query(name);
            if (!knowledge.empty()) {
                // Strip the [KNOWLEDGE: X] header line — already shown in title
                size_t newline = knowledge.find('\n');
                if (newline != std::string::npos)
                    knowledge = knowledge.substr(newline + 1);
                body << knowledge;
            } else {
                body << "No detailed knowledge available for this object.\r\n\r\n";
                body << "Run tools\\build_knowledge_db.py to build the\r\n";
                body << "Star Professor knowledge base.\r\n";
            }
        } else {
            // DB not available — show basic ephemeris
            body << name << "\r\n";
            if (alt > -90) {
                body << "Altitude: " << (int)alt << "\u00B0\r\n";
                body << "Azimuth:  " << (int)az  << "\u00B0\r\n";
            }
            if (mag < 90) body << "Magnitude: " << mag << "\r\n";
            body << "\r\nStar Professor DB not loaded.\r\n";
            body << "Run tools\\build_knowledge_db.py to enable\r\n";
            body << "detailed object knowledge.\r\n";
        }

        // Convert to wide, normalise line endings for EDIT control
        std::string utf8 = body.str();
        int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            std::wstring wide(wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), wlen);
            // Normalise \n → \r\n
            std::wstring norm;
            norm.reserve(wide.size() * 2);
            for (size_t i = 0; i < wide.size(); i++) {
                if (wide[i] == L'\n' && (i == 0 || wide[i-1] != L'\r'))
                    norm += L'\r';
                if (wide[i] != L'\0') norm += wide[i];
            }
            SetWindowTextW(m_editContent, norm.c_str());
            // Scroll to top
            SendMessageW(m_editContent, EM_SETSEL, 0, 0);
            SendMessageW(m_editContent, EM_SCROLLCARET, 0, 0);
        }
    }

    // ── Window procedure ──────────────────────────────────────
    static LRESULT CALLBACK PanelWndProc(HWND hwnd, UINT msg,
                                          WPARAM wp, LPARAM lp) {
        ObjectInfoPanel* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCT*>(lp);
            self = static_cast<ObjectInfoPanel*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        } else {
            self = reinterpret_cast<ObjectInfoPanel*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        switch (msg) {
            case WM_COMMAND: {
                int id = LOWORD(wp);
                if (id == ID_BTN_CLOSE) {
                    ShowWindow(hwnd, SW_HIDE);
                    return 0;
                }
                if (id == ID_BTN_GOTO && self) {
                    if (self->m_onGoTo && !self->m_currentName.empty())
                        self->m_onGoTo(self->m_currentName);
                    ShowWindow(hwnd, SW_HIDE);
                    return 0;
                }
                return 0;
            }
            case WM_CLOSE:
                ShowWindow(hwnd, SW_HIDE);
                return 0;

            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLOREDIT: {
                HDC hdc = (HDC)wp;
                SetTextColor(hdc, Theme::NV(RGB(180, 220, 240)));
                SetBkColor(hdc,   Theme::NV(RGB(10,  21,  32)));
                HBRUSH bg = Theme::Brush(RGB(10, 21, 32));
                return (LRESULT)bg;
            }
            case WM_CTLCOLORBTN: {
                HDC hdc = (HDC)wp;
                SetTextColor(hdc, Theme::NV(RGB(200, 232, 240)));
                SetBkColor(hdc,   Theme::NV(RGB(10,  21,  32)));
                HBRUSH bg = Theme::Brush(RGB(10, 21, 32));
                return (LRESULT)bg;
            }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    // ── Members ────────────────────────────────────────────────
    HWND    m_hwnd        = nullptr;
    HWND    m_parent      = nullptr;
    HWND    m_lblHeader   = nullptr;
    HWND    m_editContent = nullptr;
    HWND    m_btnGoTo     = nullptr;
    HWND    m_btnClose    = nullptr;

    std::string m_currentName;
    float       m_currentAlt = -999;
    float       m_currentAz  = -999;
    float       m_currentMag = 99;

    raduk::core::ObjectKnowledgeDB*            m_db     = nullptr;
    std::function<void(const std::string&)>    m_onGoTo;
};

} // namespace raduk::gui
