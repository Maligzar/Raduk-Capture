#pragma once
// ============================================================
// ToolbarStrip.h — Win32 native button toolbar for RadukCapture
//
// A WS_CHILD HWND containing native BS_PUSHBUTTON controls.
// Sits above the D2D preview area. Because these are real Win32
// controls, DPI scaling is handled by the OS — no coordinate
// math, no D2D brushes, no visibility issues.
//
// Layout (left → right):
//   [CENTROID]  [LOCK]  [ROI]  |  [COLOR]  [MAP]  [GOTO]  [RETICLE]
//
// Button IDs start at ID_TB_FIRST. Caller wires WM_COMMAND via
// SetCommandCallback. Active state is shown by bold/underline
// via owner-draw or by updating button text with a marker.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>
#include <functional>
#include <cstdio>

#pragma comment(lib, "comctl32.lib")

namespace raduk::gui {

// Command IDs for toolbar buttons
enum ToolbarCmd : int {
    ID_TB_FIRST     = 3000,
    ID_TB_CENTROID  = 3000,
    ID_TB_LOCK      = 3001,
    ID_TB_ROI       = 3002,
    ID_TB_COLOR     = 3003,
    ID_TB_MAP       = 3004,
    ID_TB_GOTO      = 3005,
    ID_TB_RETICLE   = 3006,
    ID_TB_CLIP      = 3007,
    ID_TB_HELP      = 3008,
    ID_TB_AI        = 3009,
    ID_TB_EXIT      = 3010,
    ID_TB_LAST      = 3010,
};

static constexpr int TOOLBAR_HEIGHT = 30; // physical pixels

class ToolbarStrip {
public:
    ToolbarStrip() = default;
    ~ToolbarStrip() {
        if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
        if (m_font) { DeleteObject(m_font);  m_font = nullptr; }
        if (m_fontActive) { DeleteObject(m_fontActive); m_fontActive = nullptr; }
    }

    // Create the toolbar as a WS_CHILD of mainHwnd.
    // panelWidth: right panel width — toolbar spans (clientW - panelWidth).
    bool Create(HWND mainHwnd, int panelWidth) {
        m_mainHwnd   = mainHwnd;
        m_panelWidth = panelWidth;

        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = ToolbarWndProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(10, 21, 32)); // --bg-panel
        wc.lpszClassName = L"RadukToolbarStrip";
        RegisterClassExW(&wc); // ignore already-registered error

        RECT cr; GetClientRect(mainHwnd, &cr);
        int w = cr.right - panelWidth;
        int h = TOOLBAR_HEIGHT;

        m_hwnd = CreateWindowExW(
            0, L"RadukToolbarStrip", L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0, 0, w, h,
            mainHwnd, nullptr, GetModuleHandle(nullptr), this);

        if (!m_hwnd) {
            printf("[ToolbarStrip] CreateWindowExW failed: %lu\n", GetLastError());
            return false;
        }

        // Font: Space Mono 9pt for inactive, bold for active
        m_font = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                              L"Space Mono");
        if (!m_font)
            m_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        m_fontActive = CreateFontW(-11, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                    L"Space Mono");
        if (!m_fontActive) m_fontActive = m_font;

        BuildButtons(w, h);
        printf("[ToolbarStrip] Created (%dx%d)\n", w, h);
        return true;
    }

    // Reposition and resize when main window resizes
    void SnapToMainWindow() {
        if (!m_hwnd || !m_mainHwnd) return;
        RECT cr; GetClientRect(m_mainHwnd, &cr);
        int w = cr.right - m_panelWidth;
        int h = TOOLBAR_HEIGHT;
        SetWindowPos(m_hwnd, HWND_TOP, 0, 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        RebuildButtons(w, h);
    }

    HWND Handle() const { return m_hwnd; }

    // Wire all button actions
    void SetCentroidCallback(std::function<void()> cb) { m_onCentroid = cb; }
    void SetLockCallback    (std::function<void()> cb) { m_onLock     = cb; }
    void SetROICallback     (std::function<void()> cb) { m_onROI      = cb; }
    void SetColorCallback   (std::function<void()> cb) { m_onColor    = cb; }
    void SetMapCallback     (std::function<void()> cb) { m_onMap      = cb; }
    void SetGoToCallback    (std::function<void()> cb) { m_onGoTo     = cb; }
    void SetReticleCallback (std::function<void()> cb) { m_onReticle  = cb; }
    void SetClipCallback    (std::function<void()> cb) { m_onClip     = cb; }
    void SetHelpCallback    (std::function<void()> cb) { m_onHelp     = cb; }
    void SetAICallback      (std::function<void()> cb) { m_onAI       = cb; }
    void SetExitCallback    (std::function<void()> cb) { m_onExit     = cb; }

    // Update button labels to reflect active state — only repaints when changed
    void UpdateStates(int acqStage, bool lockReady, bool roiReady,
                      bool colorActive, bool mapVisible, bool gotoVisible,
                      bool reticleVisible, bool clipVisible = false,
                      bool helpVisible = false, bool aiVisible = false) {
        if (!m_hwnd) return;

        if (acqStage      == m_lastAcqStage   &&
            lockReady     == m_lastLockReady   &&
            roiReady      == m_lastRoiReady    &&
            colorActive   == m_lastColorActive &&
            mapVisible    == m_lastMapVisible  &&
            gotoVisible   == m_lastGotoVisible &&
            reticleVisible== m_lastReticleVis  &&
            clipVisible   == m_lastClipVis     &&
            helpVisible   == m_lastHelpVis     &&
            aiVisible     == m_lastAIVis) return;

        m_lastAcqStage    = acqStage;
        m_lastLockReady   = lockReady;
        m_lastRoiReady    = roiReady;
        m_lastColorActive = colorActive;
        m_lastMapVisible  = mapVisible;
        m_lastGotoVisible = gotoVisible;
        m_lastReticleVis  = reticleVisible;
        m_lastClipVis     = clipVisible;
        m_lastHelpVis     = helpVisible;
        m_lastAIVis       = aiVisible;

        const wchar_t* centLabel =
            (acqStage >= 2) ? L"[CENTROID \u2713]" :
            (acqStage == 1) ? L"[CENTROID \u25CF]" : L"[CENTROID]";
        const wchar_t* lockLabel =
            (acqStage >= 2) ? L"[LOCK \u2713]" :
            lockReady       ? L"[LOCK \u25CF]"  : L"[LOCK]";
        const wchar_t* roiLabel =
            (acqStage >= 3) ? L"[ROI \u2713]" :
            roiReady        ? L"[ROI \u25CF]"  : L"[ROI]";

        SetWindowTextW(m_btnCentroid, centLabel);
        SetWindowTextW(m_btnLock,     lockLabel);
        SetWindowTextW(m_btnROI,      roiLabel);
        SetWindowTextW(m_btnColor,    colorActive    ? L"[C] COLOR \u25CF" : L"[C] COLOR");
        SetWindowTextW(m_btnMap,      mapVisible      ? L"[*] MAP ON"      : L"[*] MAP");
        SetWindowTextW(m_btnGoTo,     gotoVisible     ? L"[I] GOTO ON"     : L"[I] GOTO");
        SetWindowTextW(m_btnReticle,  reticleVisible  ? L"[+] RETICLE ON"  : L"[+] RETICLE");
        SetWindowTextW(m_btnClip,     clipVisible     ? L"[CLIP ON]"       : L"[CLIP]");
        SetWindowTextW(m_btnHelp,     helpVisible     ? L"[? ON]"          : L"[?]");
        SetWindowTextW(m_btnAI,       aiVisible       ? L"[AI ON]"         : L"[AI]");

        SendMessage(m_btnCentroid, WM_SETFONT, (WPARAM)(acqStage > 0  ? m_fontActive : m_font), TRUE);
        SendMessage(m_btnLock,     WM_SETFONT, (WPARAM)(acqStage >= 2 ? m_fontActive : m_font), TRUE);
        SendMessage(m_btnROI,      WM_SETFONT, (WPARAM)(acqStage >= 3 ? m_fontActive : m_font), TRUE);
        SendMessage(m_btnColor,    WM_SETFONT, (WPARAM)(colorActive   ? m_fontActive : m_font), TRUE);
        SendMessage(m_btnMap,      WM_SETFONT, (WPARAM)(mapVisible    ? m_fontActive : m_font), TRUE);
        SendMessage(m_btnGoTo,     WM_SETFONT, (WPARAM)(gotoVisible   ? m_fontActive : m_font), TRUE);
        SendMessage(m_btnReticle,  WM_SETFONT, (WPARAM)(reticleVisible? m_fontActive : m_font), TRUE);
        SendMessage(m_btnClip,     WM_SETFONT, (WPARAM)(clipVisible   ? m_fontActive : m_font), TRUE);
        SendMessage(m_btnHelp,     WM_SETFONT, (WPARAM)(helpVisible   ? m_fontActive : m_font), TRUE);
        SendMessage(m_btnAI,       WM_SETFONT, (WPARAM)(aiVisible     ? m_fontActive : m_font), TRUE);
    }

    // Handle WM_COMMAND forwarded from main window — returns true if consumed
    bool HandleCommand(WPARAM wParam) {
        int id = LOWORD(wParam);
        if (id < ID_TB_FIRST || id > ID_TB_LAST) return false;
        switch (id) {
            case ID_TB_CENTROID: if (m_onCentroid) m_onCentroid(); return true;
            case ID_TB_LOCK:     if (m_onLock)     m_onLock();     return true;
            case ID_TB_ROI:      if (m_onROI)      m_onROI();      return true;
            case ID_TB_COLOR:    if (m_onColor)    m_onColor();    return true;
            case ID_TB_MAP:      if (m_onMap)      m_onMap();      return true;
            case ID_TB_GOTO:     if (m_onGoTo)     m_onGoTo();     return true;
            case ID_TB_RETICLE:  if (m_onReticle)  m_onReticle();  return true;
            case ID_TB_CLIP:     if (m_onClip)     m_onClip();     return true;
            case ID_TB_HELP:     if (m_onHelp)     m_onHelp();     return true;
            case ID_TB_AI:       if (m_onAI)       m_onAI();       return true;
            case ID_TB_EXIT:     if (m_onExit)     m_onExit();     return true;
        }
        return false;
    }

private:
    void BuildButtons(int w, int h) {
        // Button layout:
        //  Left group  (acquisition): CENTROID | LOCK | ROI
        //  Separator gap (8px)
        //  Right group (view):        COLOR | MAP | GOTO | RETICLE
        //
        // Each button is auto-sized to its label + padding.
        // We use fixed widths that fit the longest label.

        struct BtnDef { int id; const wchar_t* label; int width; HWND* out; };
        BtnDef defs[] = {
            { ID_TB_CENTROID, L"[CENTROID]",  90, &m_btnCentroid },
            { ID_TB_LOCK,     L"[LOCK]",      56, &m_btnLock     },
            { ID_TB_ROI,      L"[ROI]",       46, &m_btnROI      },
            // gap handled by x offset below
            { ID_TB_COLOR,    L"[C] COLOR",   74, &m_btnColor    },
            { ID_TB_MAP,      L"[*] MAP",     60, &m_btnMap      },
            { ID_TB_GOTO,     L"[I] GOTO",    64, &m_btnGoTo     },
            { ID_TB_RETICLE,  L"[+] RETICLE", 84, &m_btnReticle  },
            { ID_TB_CLIP,     L"[CLIP]",      60, &m_btnClip     },
            { ID_TB_HELP,     L"[?]",         36, &m_btnHelp     },
            { ID_TB_AI,       L"[AI]",        40, &m_btnAI       },
            { ID_TB_EXIT,     L"EXIT",        44, &m_btnExit     },
        };

        const int pad  = 4;
        const int sep  = 12;
        const int btnH = h - 6;
        const int btnY = 3;
        int x = 4;

        for (int i = 0; i < 11; i++) {
            auto& d = defs[i];
            if (i == 3) x += sep;   // gap before view buttons
            if (i == 10) x += sep;  // gap before EXIT

            HWND btn = CreateWindowExW(
                0, L"BUTTON", d.label,
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
                x, btnY, d.width, btnH,
                m_hwnd, (HMENU)(intptr_t)d.id,
                GetModuleHandle(nullptr), nullptr);

            if (btn) {
                SendMessage(btn, WM_SETFONT, (WPARAM)m_font, TRUE);
                *d.out = btn;
            }
            x += d.width + pad;
        }
    }

    void RebuildButtons(int w, int h) {
        // Destroy and recreate — simplest resize strategy for a small fixed toolbar
        if (m_btnCentroid) { DestroyWindow(m_btnCentroid); m_btnCentroid = nullptr; }
        if (m_btnLock)     { DestroyWindow(m_btnLock);     m_btnLock     = nullptr; }
        if (m_btnROI)      { DestroyWindow(m_btnROI);      m_btnROI      = nullptr; }
        if (m_btnColor)    { DestroyWindow(m_btnColor);    m_btnColor    = nullptr; }
        if (m_btnMap)      { DestroyWindow(m_btnMap);      m_btnMap      = nullptr; }
        if (m_btnGoTo)     { DestroyWindow(m_btnGoTo);     m_btnGoTo     = nullptr; }
        if (m_btnReticle)  { DestroyWindow(m_btnReticle);  m_btnReticle  = nullptr; }
        if (m_btnClip)     { DestroyWindow(m_btnClip);     m_btnClip     = nullptr; }
        if (m_btnHelp)     { DestroyWindow(m_btnHelp);     m_btnHelp     = nullptr; }
        if (m_btnAI)       { DestroyWindow(m_btnAI);       m_btnAI       = nullptr; }
        if (m_btnExit)     { DestroyWindow(m_btnExit);     m_btnExit     = nullptr; }
        BuildButtons(w, h);
    }

    static LRESULT CALLBACK ToolbarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        ToolbarStrip* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            self = static_cast<ToolbarStrip*>(cs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<ToolbarStrip*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }

        switch (msg) {
            case WM_CTLCOLORBTN: {
                // Paint button background to match toolbar dark theme
                HDC hdc = (HDC)wParam;
                SetBkColor(hdc, RGB(10, 21, 32));
                SetTextColor(hdc, RGB(200, 232, 240)); // --text
                static HBRUSH hBg = CreateSolidBrush(RGB(10, 21, 32));
                return (LRESULT)hBg;
            }
            case WM_ERASEBKGND: {
                HDC hdc = (HDC)wParam;
                RECT cr; GetClientRect(hwnd, &cr);
                static HBRUSH hBg = CreateSolidBrush(RGB(10, 21, 32));
                FillRect(hdc, &cr, hBg);
                static HPEN hBorder = CreatePen(PS_SOLID, 1, RGB(14, 29, 46));
                HPEN old = (HPEN)SelectObject(hdc, hBorder);
                MoveToEx(hdc, 0, cr.bottom - 1, nullptr);
                LineTo(hdc, cr.right, cr.bottom - 1);
                SelectObject(hdc, old);
                return 1;
            }
            case WM_COMMAND:
                if (self) self->HandleCommand(wParam);
                return 0;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    HWND  m_hwnd       = nullptr;
    HWND  m_mainHwnd   = nullptr;
    HFONT m_font       = nullptr;
    HFONT m_fontActive = nullptr;
    int   m_panelWidth = 560;

    HWND  m_btnCentroid = nullptr;
    HWND  m_btnLock     = nullptr;
    HWND  m_btnROI      = nullptr;
    HWND  m_btnColor    = nullptr;
    HWND  m_btnMap      = nullptr;
    HWND  m_btnGoTo     = nullptr;
    HWND  m_btnReticle  = nullptr;
    HWND  m_btnClip     = nullptr;
    HWND  m_btnHelp     = nullptr;
    HWND  m_btnAI       = nullptr;
    HWND  m_btnExit     = nullptr;

    // State cache — UpdateStates skips SetWindowTextW when unchanged (prevents flash)
    int  m_lastAcqStage    = -1;
    bool m_lastLockReady   = false;
    bool m_lastRoiReady    = false;
    bool m_lastColorActive = false;
    bool m_lastMapVisible  = false;
    bool m_lastGotoVisible = false;
    bool m_lastReticleVis  = false;
    bool m_lastClipVis     = false;
    bool m_lastHelpVis     = false;
    bool m_lastAIVis       = false;

    std::function<void()> m_onCentroid;
    std::function<void()> m_onLock;
    std::function<void()> m_onROI;
    std::function<void()> m_onColor;
    std::function<void()> m_onMap;
    std::function<void()> m_onGoTo;
    std::function<void()> m_onReticle;
    std::function<void()> m_onClip;
    std::function<void()> m_onHelp;
    std::function<void()> m_onAI;
    std::function<void()> m_onExit;
};

} // namespace raduk::gui
