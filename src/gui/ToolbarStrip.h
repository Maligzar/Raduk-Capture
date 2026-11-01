#pragma once
// ============================================================
// ToolbarStrip.h — Win32 native button toolbar for RadukCapture
//
// A WS_CHILD HWND containing owner-drawn buttons, two rows of them,
// above the D2D preview area. Where they go is ToolbarLayout.h, which
// is arithmetic and is tested; how they look is DrawButton below.
//
// Button IDs start at ID_TB_FIRST. Caller wires WM_COMMAND via
// SetCommandCallback.
//
// Phase 111: these were stock BS_PUSHBUTTONs, which cannot follow
// night vision and could show "active" only by bold type or a marker
// in the label. A button now has a state and is painted from it.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Theme.h"
#include "ToolbarLayout.h"
#include "../core/HardwareLink.h"   // Phase 46: what the [HW] button shows
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <functional>
#include <cstdio>
#include <string>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace raduk::gui {

// One resolution preset. FULL is first because it is the default and the
// widest field -- the one to come back to when a crop went wrong.
struct ResPreset { const wchar_t* label; int w; int h; };
static constexpr ResPreset kResPresets[] = {
    { L"FULL", 3840, 2160 },   // whole sensor, widest FOV
    { L"1920", 1920, 1080 },
    { L"1280", 1280,  960 },   // what [ROI] crops to
    { L"640",   640,  480 },   // high frame rate
};
static constexpr int kResPresetCount =
    (int)(sizeof(kResPresets) / sizeof(kResPresets[0]));

// HandleCommand turns a button id into a preset index by subtracting
// ID_TB_RES_FULL, so the ids and the table must stay the same length.
// Adding a preset without adding an id would index past the table.
static_assert(ID_TB_RES_640 - ID_TB_RES_FULL + 1 == kResPresetCount,
              "resolution button ids and kResPresets have drifted apart");

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
        wc.hbrBackground = CreateSolidBrush(Theme::NV(Theme::Rgb::PANEL)); // --bg-panel
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
        m_font = CreateFontW(-Theme::Type::MICRO, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                              L"Space Mono");
        if (!m_font)
            m_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        m_fontActive = CreateFontW(-Theme::Type::MICRO, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                    L"Space Mono");
        if (!m_fontActive) m_fontActive = m_font;

        BuildButtons();
        PlaceButtons(w);
        printf("[ToolbarStrip] Created (%dx%d)\n", w, h);
        return true;
    }

    // Reposition and resize when main window resizes
    void SnapToMainWindow() {
        if (!m_hwnd || !m_mainHwnd) return;
        RECT cr; GetClientRect(m_mainHwnd, &cr);
        int w = cr.right - m_panelWidth;
        SetWindowPos(m_hwnd, HWND_TOP, 0, 0, w, TOOLBAR_HEIGHT,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        PlaceButtons(w);
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
    void SetGhostCallback   (std::function<void()> cb) { m_onGhost    = cb; }
    // Phase 46: one press connects what is missing, or disconnects
    // everything when all three are up. The decision is
    // raduk::core::NextAction's, not this button's.
    void SetHardwareCallback(std::function<void()> cb) { m_onHardware = cb; }
    // Opens RadukCaptureDashboard.html in the browser with the access
    // token already in it, so nobody has to copy WsToken out of rig.ini.
    void SetDashCallback    (std::function<void()> cb) { m_onDash     = cb; }
    // The same action as the K key, which had no button.
    void SetStackCallback   (std::function<void()> cb) { m_onStack    = cb; }
    void SetFollowCallback  (std::function<void()> cb) { m_onFollow   = cb; }
    void SetFollowOn(bool on) {
        Show(ID_TB_FOLLOW, L"FOLLOW", on ? State::Active : State::Idle);
    }
    void SetDerotCallback   (std::function<void()> cb) { m_onDerot    = cb; }
    void SetDerotStop(bool stop) {
        Show(ID_TB_DEROT, stop ? L"DEROT STOP" : L"DEROT WARN",
             stop ? State::Active : State::Idle);
    }
    void SetRunsCallback    (std::function<void()> cb) { m_onRuns     = cb; }
    // Phase 113: lit with the run under way, which is all that fits.
    void SetRuns(bool active, int ordinal) {
        wchar_t lbl[8];
        swprintf(lbl, 8, L"#%d", ordinal);
        Show(ID_TB_RUNS, active ? lbl : L"RUNS",
             active ? State::Active : State::Idle);
    }
    // Phase 45: the reticle button cycles shapes, so its label has to
    // name the shape rather than just ON.
    void SetReticleShapeLabel(const wchar_t* lbl) {
        m_reticleLabel = ReticleButtonLabel(lbl ? lbl : L"RETICLE");
        Show(ID_TB_RETICLE, m_reticleLabel.c_str(), StateOf(ID_TB_RETICLE));
    }

    // Resolution: called with the preset's sensor dimensions.
    void SetFramingShotCallback(std::function<void()> cb) { m_onFrame = cb; }
    void SetResolutionCallback(std::function<void(int,int)> cb) { m_onRes = cb; }
    // Bit depth: called with 8 or 16.
    void SetBitDepthCallback  (std::function<void(int)> cb)     { m_onBits = cb; }

    // Phase 84: show which format is live. Separate from UpdateStates
    // because the format changes on a camera reconfigure rather than
    // every render tick, and reusing that already-eleven-argument
    // signature to carry it would have made both harder to read.
    void UpdateFormat(int roiW, int roiH, int bits) {
        if (!m_hwnd) return;
        // A size no preset matches -- a dragged box, or a crop from
        // [ROI] that is not 1280x960 because the sensor clamped it.
        // Every preset reads inactive, which is correct: none is live.
        for (int i = 0; i < kResPresetCount; i++) {
            const bool active = (kResPresets[i].w == roiW &&
                                 kResPresets[i].h == roiH);
            Toggle(ID_TB_RES_FULL + i, kResPresets[i].label, active);
        }
        Toggle(ID_TB_BIT8,  L"8b",  bits == 8);
        Toggle(ID_TB_BIT16, L"16b", bits == 16);
    }

    // Phase 46: which devices are live. Separate from UpdateStates for
    // the same reason UpdateFormat is -- this changes when hardware
    // comes or goes, not every render tick. "2/3" says how many to go
    // looking for, and an unlit button at a glance means nothing is
    // talking to the app.
    void UpdateHardware(const raduk::core::DeviceLinks& links) {
        if (!m_hwnd) return;
        wchar_t w[24] = {};
        StripBrackets(raduk::core::LinkLabel(links).c_str(), w, 24);
        const int n = links.Count();
        Show(ID_TB_HW, w, n == 3 ? State::Active
                        : n > 0  ? State::Ready : State::Idle);
    }

    // Update button labels to reflect active state — only repaints when changed
    void UpdateStates(int acqStage, bool lockReady, bool roiReady,
                      bool colorActive, bool mapVisible, bool gotoVisible,
                      bool reticleVisible, bool clipVisible = false,
                      bool helpVisible = false, bool aiVisible = false,
                      bool ghostActive = false) {
        if (!m_hwnd) return;

        // Stage values (AcquisitionStage): 0 Off, 1 Searching,
        // 2 Calibrating (Phase 51), 3 Centering, 4 Locked
        Show(ID_TB_CENTROID, L"CENTROID",
             acqStage >= 3 ? State::Active :
             acqStage >= 1 ? State::Ready  : State::Idle);
        Show(ID_TB_LOCK, acqStage == 2 ? L"LOCK CAL\u2026" : L"LOCK",
             acqStage >= 3                   ? State::Active :
             (acqStage == 2 || lockReady)    ? State::Ready  : State::Idle);
        Show(ID_TB_ROI, L"ROI",
             acqStage >= 4 ? State::Active :
             roiReady      ? State::Ready  : State::Idle);

        Toggle(ID_TB_COLOR,   L"COLOR",               colorActive);
        Toggle(ID_TB_MAP,     L"MAP",                 mapVisible);
        Toggle(ID_TB_GOTO,    L"GOTO",                gotoVisible);
        Toggle(ID_TB_RETICLE, m_reticleLabel.c_str(), reticleVisible);
        Toggle(ID_TB_CLIP,    L"CLIP",                clipVisible);
        Toggle(ID_TB_HELP,    L"?",                   helpVisible);
        Toggle(ID_TB_AI,      L"AI",                  aiVisible);
        Toggle(ID_TB_GHOST,   L"GH",                  ghostActive);
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
            case ID_TB_GHOST:    if (m_onGhost)    m_onGhost();    return true;
            case ID_TB_HW:       if (m_onHardware) m_onHardware(); return true;
            case ID_TB_DASH:     if (m_onDash)     m_onDash();     return true;
            case ID_TB_STACK:    if (m_onStack)    m_onStack();    return true;
            case ID_TB_FOLLOW:   if (m_onFollow)   m_onFollow();   return true;
            case ID_TB_DEROT:    if (m_onDerot)    m_onDerot();    return true;
            case ID_TB_RUNS:     if (m_onRuns)     m_onRuns();     return true;
            case ID_TB_EXIT:     if (m_onExit)     m_onExit();     return true;
            case ID_TB_RES_FULL:
            case ID_TB_RES_1920:
            case ID_TB_RES_1280:
            case ID_TB_RES_640: {
                const int i = id - ID_TB_RES_FULL;
                if (m_onRes && i >= 0 && i < kResPresetCount)
                    m_onRes(kResPresets[i].w, kResPresets[i].h);
                return true;
            }
            case ID_TB_BIT8:     if (m_onBits)     m_onBits(8);    return true;
            case ID_TB_BIT16:    if (m_onBits)     m_onBits(16);   return true;
            case ID_TB_FRAME:    if (m_onFrame)    m_onFrame();    return true;
        }
        return false;
    }

private:
    // Idle, on its way (searching, or two devices of three), and on.
    // Night vision flattens hue, so the three differ in shape as well as
    // colour: nothing, an outline, a filled face with a bar under it.
    enum class State { Idle, Ready, Active };

    static int Index(int id) { return id - ID_TB_FIRST; }
    State StateOf(int id) const { return m_state[Index(id)]; }

    void Toggle(int id, const wchar_t* label, bool on) {
        Show(id, label, on ? State::Active : State::Idle);
    }

    // Repaints only on a change: this runs every render tick and a
    // redraw per tick would flash.
    void Show(int id, const wchar_t* label, State s) {
        const int i = Index(id);
        if (!m_btn[i] || (m_state[i] == s && m_text[i] == label)) return;
        m_state[i] = s;
        m_text[i]  = label;
        InvalidateRect(m_btn[i], nullptr, FALSE);
    }

    void BuildButtons() {
        for (const ToolbarRow& row : kToolbarRows) {
            for (int i = 0; i < row.count; i++) {
                const ToolbarItem& d = row.items[i];
                HWND btn = CreateWindowExW(
                    0, L"BUTTON", d.label,
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    0, 0, d.width, kToolbarBtnH,
                    m_hwnd, (HMENU)(intptr_t)d.id,
                    GetModuleHandle(nullptr), nullptr);
                if (btn) SetWindowTheme(btn, L"", L"");
                m_btn[Index(d.id)]  = btn;
                m_text[Index(d.id)] = d.label;
            }
        }
    }

    // Moved rather than recreated, so a resize keeps every button's
    // state and there is nothing to repaint from scratch.
    void PlaceButtons(int stripW) {
        for (int r = 0; r < 2; r++) {
            const ToolbarRow& row = kToolbarRows[r];
            int x[16] = {};
            PlaceToolbarRow(row, stripW, x);
            for (int i = 0; i < row.count; i++) {
                HWND btn = m_btn[Index(row.items[i].id)];
                if (btn) SetWindowPos(btn, nullptr, x[i], kToolbarRowY[r],
                                      row.items[i].width, kToolbarBtnH,
                                      SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
    }

    // Composited on the far side of the night transform, as Theme's
    // shadow helpers do: NV is a table for tokens and blending first
    // would land on the derived luminance, not the designed one.
    void DrawButton(const DRAWITEMSTRUCT* dis) {
        const int i = Index((int)dis->CtlID);
        if (i < 0 || i >= kToolbarButtons) return;

        HDC  hdc     = dis->hDC;
        RECT r       = dis->rcItem;
        const State s = m_state[i];
        const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
        const COLORREF panel  = Theme::NV(Theme::Rgb::PANEL);
        const COLORREF accent = Theme::NV(Theme::Rgb::ACCENT);

        COLORREF face = panel;
        if (s == State::Active) face = Theme::Mix(panel, accent, 0.18f);
        if (pressed)            face = Theme::Mix(face, accent, 0.25f);
        HBRUSH hbg = CreateSolidBrush(face);
        FillRect(hdc, &r, hbg);
        DeleteObject(hbg);
        Theme::RaiseInRect(hdc, r, Theme::Rgb::PANEL, Theme::Elev::RAISED);

        HPEN pen = CreatePen(PS_SOLID, 1,
            s == State::Ready ? Theme::NV(Theme::Rgb::ACCENT2)
                              : Theme::NV(Theme::Rgb::BORDER));
        HPEN   oldPen   = (HPEN)SelectObject(hdc, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, r.left, r.top, r.right, r.bottom);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);

        if (s == State::Active) {
            RECT bar = { r.left + 1, r.bottom - 3, r.right - 1, r.bottom - 1 };
            HBRUSH hbar = CreateSolidBrush(accent);
            FillRect(hdc, &bar, hbar);
            DeleteObject(hbar);
        }

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, s == State::Active ? accent
                                             : Theme::NV(Theme::Rgb::TEXT));
        HFONT old = (HFONT)SelectObject(hdc,
            s == State::Active ? m_fontActive : m_font);
        DrawTextW(hdc, m_text[i].c_str(), -1, &r,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, old);
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
            case WM_DRAWITEM:
                if (self) self->DrawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
                return TRUE;
            case WM_ERASEBKGND: {
                HDC hdc = (HDC)wParam;
                RECT cr; GetClientRect(hwnd, &cr);
                HBRUSH hBg = Theme::Brush(Theme::Rgb::PANEL);
                FillRect(hdc, &cr, hBg);
                HPEN hBorder = Theme::Pen(Theme::Rgb::BORDER, 1);
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

    // Indexed by id - ID_TB_FIRST.
    HWND         m_btn[kToolbarButtons]   = {};
    State        m_state[kToolbarButtons] = {};
    std::wstring m_text[kToolbarButtons];
    std::wstring m_reticleLabel = L"RETICLE";

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
    std::function<void()> m_onGhost;
    std::function<void()> m_onHardware;
    std::function<void()> m_onDash;
    std::function<void()> m_onStack;
    std::function<void()> m_onFollow;
    std::function<void()> m_onDerot;
    std::function<void()> m_onRuns;
    std::function<void()> m_onExit;
    std::function<void(int,int)> m_onRes;
    std::function<void(int)>     m_onBits;
    std::function<void()>        m_onFrame;
};

} // namespace raduk::gui
