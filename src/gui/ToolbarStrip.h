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

#include "Theme.h"
#include "../core/HardwareLink.h"   // Phase 46: what the [HW] button shows
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
    ID_TB_GHOST     = 3011,
    // Capture format group. Resolution had no control at all: once the
    // ROI was cropped, by [ROI] or by dragging a box, nothing could widen
    // it again short of restarting. Bit depth was hardcoded RAW8 in every
    // call site, so the 16-bit the DSO profile asks for never happened.
    ID_TB_RES_FULL  = 3012,
    ID_TB_RES_1920  = 3013,
    ID_TB_RES_1280  = 3014,
    ID_TB_RES_640   = 3015,
    ID_TB_BIT8      = 3016,
    ID_TB_BIT16     = 3017,
    ID_TB_FRAME     = 3018,   // Phase 39: framing shot (boosted 5s preview)
    ID_TB_HW        = 3019,   // Phase 46: connect / disconnect all hardware
    ID_TB_DASH      = 3020,   // the browser dashboard, signed in
    ID_TB_STACK     = 3021,   // Phase 106: the last recording, in AutoStakkert
    ID_TB_FOLLOW    = 3022,   // Phase 107: move the ROI to hold the target
    ID_TB_DEROT     = 3023,   // Phase 108: warn or stop at the derotation window
    ID_TB_LAST      = 3023,
};

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
    void SetFollowLabel(const wchar_t* lbl) {
        if (m_btnFollow) SetWindowTextW(m_btnFollow, lbl);
    }
    void SetDerotCallback   (std::function<void()> cb) { m_onDerot    = cb; }
    void SetDerotLabel(const wchar_t* lbl) {
        if (m_btnDerot) SetWindowTextW(m_btnDerot, lbl);
    }
    // Phase 45: the reticle button now cycles shapes, so its label has
    // to name the shape rather than just ON. Stored rather than written
    // directly, because UpdateStates owns that button's text and would
    // overwrite anything set behind its back on the next tick.
    void SetReticleShapeLabel(const wchar_t* lbl) {
        std::wstring s = lbl ? lbl : L"RETICLE";
        if (s == m_reticleLabel) return;
        m_reticleLabel = s;
        // Written straight to the button. The first attempt flipped
        // m_lastReticleVis to force UpdateStates to repaint, which
        // backfired: on the two presses that also change visibility,
        // the flip made the cached value MATCH the new one, so
        // UpdateStates early-returned and the label stayed a press
        // behind. Writing it here needs no cooperation from the cache.
        if (m_btnReticle) {
            std::wstring rl = L"[+] " + m_reticleLabel;
            SetWindowTextW(m_btnReticle, rl.c_str());
        }
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
        if (roiW == m_lastResW && roiH == m_lastResH && bits == m_lastBits)
            return;
        m_lastResW = roiW; m_lastResH = roiH; m_lastBits = bits;

        HWND resBtns[kResPresetCount] = {
            m_btnResFull, m_btnRes1920, m_btnRes1280, m_btnRes640
        };
        for (int i = 0; i < kResPresetCount; i++) {
            if (!resBtns[i]) continue;
            const bool active = (kResPresets[i].w == roiW &&
                                 kResPresets[i].h == roiH);
            wchar_t lbl[16];
            // A dot rather than a different colour: these are plain
            // BS_PUSHBUTTONs, and the bold font alone is too subtle to
            // read at a glance on a dark strip at night.
            swprintf(lbl, 16, active ? L"%s●" : L"%s",
                     kResPresets[i].label);
            SetWindowTextW(resBtns[i], lbl);
            SendMessage(resBtns[i], WM_SETFONT,
                        (WPARAM)(active ? m_fontActive : m_font), TRUE);
        }
        // A size no preset matches -- a dragged box, or a crop from
        // [ROI] that is not 1280x960 because the sensor clamped it.
        // Every preset reads inactive, which is correct: none is live.
        if (m_btnBit8) {
            SetWindowTextW(m_btnBit8, bits == 8 ? L"8b●" : L"8b");
            SendMessage(m_btnBit8, WM_SETFONT,
                        (WPARAM)(bits == 8 ? m_fontActive : m_font), TRUE);
        }
        if (m_btnBit16) {
            SetWindowTextW(m_btnBit16, bits == 16 ? L"16b●" : L"16b");
            SendMessage(m_btnBit16, WM_SETFONT,
                        (WPARAM)(bits == 16 ? m_fontActive : m_font), TRUE);
        }
    }

    // Phase 46: which devices are live. Separate from UpdateStates for
    // the same reason UpdateFormat is -- this changes when hardware
    // comes or goes, not every render tick.
    //
    // The plan asked for green / amber / red. These are stock
    // BS_PUSHBUTTONs on a themed strip, with no owner-draw and no
    // per-button colour anywhere in this file, so the state is carried
    // the way every other button here carries it: the label, plus the
    // bold font when something is live. "2/3" also says more than amber
    // does -- it says how many to go looking for.
    void UpdateHardware(const raduk::core::DeviceLinks& links) {
        if (!m_hwnd || !m_btnHW) return;
        if (m_hwLinksKnown && links == m_lastLinks) return;
        m_hwLinksKnown = true;
        m_lastLinks = links;

        const std::string label = raduk::core::LinkLabel(links);
        wchar_t w[24] = {};
        MultiByteToWideChar(CP_UTF8, 0, label.c_str(), -1, w, 24);
        SetWindowTextW(m_btnHW, w);
        // Bold while anything is connected: an unlit [HW] at a glance
        // means nothing is talking to the app.
        const bool any = links.Count() > 0;
        SendMessage(m_btnHW, WM_SETFONT,
                    (WPARAM)(any ? m_fontActive : m_font), TRUE);
    }

    // Update button labels to reflect active state — only repaints when changed
    void UpdateStates(int acqStage, bool lockReady, bool roiReady,
                      bool colorActive, bool mapVisible, bool gotoVisible,
                      bool reticleVisible, bool clipVisible = false,
                      bool helpVisible = false, bool aiVisible = false,
                      bool ghostActive = false) {
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
            aiVisible     == m_lastAIVis       &&
            ghostActive   == m_lastGhostActive) return;

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
        m_lastGhostActive = ghostActive;

        // Stage values (AcquisitionStage): 0 Off, 1 Searching,
        // 2 Calibrating (Phase 51), 3 Centering, 4 Locked
        const wchar_t* centLabel =
            (acqStage >= 3) ? L"[CENTROID \u2713]" :
            (acqStage >= 1) ? L"[CENTROID \u25CF]" : L"[CENTROID]";
        const wchar_t* lockLabel =
            (acqStage >= 3) ? L"[LOCK \u2713]" :
            (acqStage == 2) ? L"[LOCK CAL\u2026]" :
            lockReady       ? L"[LOCK \u25CF]"  : L"[LOCK]";
        const wchar_t* roiLabel =
            (acqStage >= 4) ? L"[ROI \u2713]" :
            roiReady        ? L"[ROI \u25CF]"  : L"[ROI]";

        SetWindowTextW(m_btnCentroid, centLabel);
        SetWindowTextW(m_btnLock,     lockLabel);
        SetWindowTextW(m_btnROI,      roiLabel);
        SetWindowTextW(m_btnColor,    colorActive    ? L"[C] COLOR \u25CF" : L"[C] COLOR");
        SetWindowTextW(m_btnMap,      mapVisible      ? L"[*] MAP ON"      : L"[*] MAP");
        SetWindowTextW(m_btnGoTo,     gotoVisible     ? L"[I] GOTO ON"     : L"[I] GOTO");
        {
            // m_reticleLabel already reads "RETICLE" when the reticle is
            // off, so the visibility flag does not need consulting here --
            // and consulting it was what let the two values disagree.
            std::wstring rl = L"[+] " + m_reticleLabel;
            SetWindowTextW(m_btnReticle, rl.c_str());
        }
        SetWindowTextW(m_btnClip,     clipVisible     ? L"[CLIP ON]"       : L"[CLIP]");
        SetWindowTextW(m_btnHelp,     helpVisible     ? L"[? ON]"          : L"[?]");
        SetWindowTextW(m_btnAI,       aiVisible       ? L"[AI ON]"         : L"[AI]");
        SetWindowTextW(m_btnGhost,    ghostActive     ? L"[GH ON]"         : L"[GH]");

        SendMessage(m_btnCentroid, WM_SETFONT, (WPARAM)(acqStage > 0  ? m_fontActive : m_font), TRUE);
        SendMessage(m_btnLock,     WM_SETFONT, (WPARAM)(acqStage >= 2 ? m_fontActive : m_font), TRUE);
        SendMessage(m_btnROI,      WM_SETFONT, (WPARAM)(acqStage >= 4 ? m_fontActive : m_font), TRUE);
        SendMessage(m_btnColor,    WM_SETFONT, (WPARAM)(colorActive   ? m_fontActive : m_font), TRUE);
        SendMessage(m_btnMap,      WM_SETFONT, (WPARAM)(mapVisible    ? m_fontActive : m_font), TRUE);
        SendMessage(m_btnGoTo,     WM_SETFONT, (WPARAM)(gotoVisible   ? m_fontActive : m_font), TRUE);
        SendMessage(m_btnReticle,  WM_SETFONT, (WPARAM)(reticleVisible? m_fontActive : m_font), TRUE);
        SendMessage(m_btnClip,     WM_SETFONT, (WPARAM)(clipVisible   ? m_fontActive : m_font), TRUE);
        SendMessage(m_btnHelp,     WM_SETFONT, (WPARAM)(helpVisible   ? m_fontActive : m_font), TRUE);
        SendMessage(m_btnAI,       WM_SETFONT, (WPARAM)(aiVisible     ? m_fontActive : m_font), TRUE);
        SendMessage(m_btnGhost,    WM_SETFONT, (WPARAM)(ghostActive   ? m_fontActive : m_font), TRUE);
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
            // gap at i==3
            { ID_TB_COLOR,    L"[C] COLOR",   74, &m_btnColor    },
            { ID_TB_MAP,      L"[*] MAP",     60, &m_btnMap      },
            { ID_TB_GOTO,     L"[I] GOTO",    64, &m_btnGoTo     },
            { ID_TB_RETICLE,  L"[+] RETICLE", 84, &m_btnReticle  },
            { ID_TB_CLIP,     L"[CLIP]",      60, &m_btnClip     },
            { ID_TB_FRAME,    L"[FRAME]",     62, &m_btnFrame    },
            { ID_TB_GHOST,    L"[GH]",        40, &m_btnGhost    },
            // Wide enough for the longest label it takes, "[HW] OFF".
            { ID_TB_HW,       L"[HW] OFF",    74, &m_btnHW       },
            { ID_TB_HELP,     L"[?]",         36, &m_btnHelp     },
            { ID_TB_AI,       L"[AI]",        40, &m_btnAI       },
            { ID_TB_DASH,     L"[DASH]",      52, &m_btnDash     },
            { ID_TB_STACK,    L"[STACK]",     60, &m_btnStack    },
            { ID_TB_FOLLOW,   L"[FOLLOW] OFF", 92, &m_btnFollow  },
            { ID_TB_DEROT,    L"[DEROT] WARN", 92, &m_btnDerot   },
            // gap at i==17 — capture format group
            { ID_TB_RES_FULL, L"FULL",        46, &m_btnResFull  },
            { ID_TB_RES_1920, L"1920",        46, &m_btnRes1920  },
            { ID_TB_RES_1280, L"1280",        46, &m_btnRes1280  },
            { ID_TB_RES_640,  L"640",         40, &m_btnRes640   },
            { ID_TB_BIT8,     L"8b",          32, &m_btnBit8     },
            { ID_TB_BIT16,    L"16b",         36, &m_btnBit16    },
            // gap at i==23
            { ID_TB_EXIT,     L"EXIT",        44, &m_btnExit     },
        };
        const int nDefs = (int)(sizeof(defs) / sizeof(defs[0]));

        const int pad  = 4;
        const int sep  = 12;
        const int btnH = h - 6;
        const int btnY = 3;
        int x = 4;

        for (int i = 0; i < nDefs; i++) {
            auto& d = defs[i];
            // Indices, so they move when a button is inserted. [HW]
            // went in at 10, [DASH] at 13, [STACK] at 14, [FOLLOW] at 15 and
            // [DEROT] at 16, each pushing
            // the format group and EXIT along by one.
            if (i == 3)  x += sep;   // gap before view buttons
            if (i == 17) x += sep;   // gap before the format group
            if (i == 23) x += sep;   // gap before EXIT

            HWND btn = CreateWindowExW(
                0, L"BUTTON", d.label,
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
                x, btnY, d.width, btnH,
                m_hwnd, (HMENU)(intptr_t)d.id,
                GetModuleHandle(nullptr), nullptr);

            if (btn) {
                // No SetWindowTheme here, deliberately. Stripping the
                // visual style was tried to make these button faces
                // follow night vision, and measurement rejected it: the
                // toolbar only went from 5.1% to 4% blue, because the
                // residual is ClearType subpixel fringing on the labels
                // and not the face at all -- while day mode lost its
                // normal appearance permanently, reading 0.17% blue with
                // night vision OFF where it had been 39%. A fix that
                // misses its target and damages the common case is worse
                // than the gap it was aimed at.
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
        if (m_btnGhost)    { DestroyWindow(m_btnGhost);    m_btnGhost    = nullptr; }
        if (m_btnHW)       { DestroyWindow(m_btnHW);       m_btnHW       = nullptr; }
        if (m_btnDash)     { DestroyWindow(m_btnDash);     m_btnDash     = nullptr; }
        if (m_btnStack)    { DestroyWindow(m_btnStack);    m_btnStack    = nullptr; }
        if (m_btnFollow)   { DestroyWindow(m_btnFollow);   m_btnFollow   = nullptr; }
        if (m_btnDerot)    { DestroyWindow(m_btnDerot);    m_btnDerot    = nullptr; }
        if (m_btnExit)     { DestroyWindow(m_btnExit);     m_btnExit     = nullptr; }
        if (m_btnResFull)  { DestroyWindow(m_btnResFull);  m_btnResFull  = nullptr; }
        if (m_btnRes1920)  { DestroyWindow(m_btnRes1920);  m_btnRes1920  = nullptr; }
        if (m_btnRes1280)  { DestroyWindow(m_btnRes1280);  m_btnRes1280  = nullptr; }
        if (m_btnRes640)   { DestroyWindow(m_btnRes640);   m_btnRes640   = nullptr; }
        if (m_btnBit8)     { DestroyWindow(m_btnBit8);     m_btnBit8     = nullptr; }
        if (m_btnBit16)    { DestroyWindow(m_btnBit16);    m_btnBit16    = nullptr; }
        if (m_btnFrame)    { DestroyWindow(m_btnFrame);    m_btnFrame    = nullptr; }
        BuildButtons(w, h);
        // The new buttons carry their plain BuildButtons labels, but the
        // caches still hold the states those labels used to show, so every
        // Update* call would early-return and the window would come back
        // from a resize with every active marker gone. Clearing the caches
        // makes the next tick repaint. This applies to the acquisition and
        // view buttons too -- they had the same hole before the format
        // group was added.
        ResetStateCache();
    }

    void ResetStateCache() {
        m_lastAcqStage    = -1;
        m_lastLockReady   = false;
        m_lastRoiReady    = false;
        m_lastColorActive = false;
        m_lastMapVisible  = false;
        m_lastGotoVisible = false;
        m_lastReticleVis  = false;
        m_lastClipVis     = false;
        m_lastHelpVis     = false;
        m_lastAIVis       = false;
        m_lastGhostActive = false;
        m_lastResW = m_lastResH = m_lastBits = -1;
        // Phase 46: the same hole. A rebuild gives [HW] its plain
        // creation label, so the next UpdateHardware has to repaint
        // even though the link state itself has not changed.
        m_hwLinksKnown = false;
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
                SetBkColor(hdc, Theme::NV(Theme::Rgb::PANEL));
                SetTextColor(hdc, Theme::NV(Theme::Rgb::TEXT)); // --text
                HBRUSH hBg = Theme::Brush(Theme::Rgb::PANEL);
                return (LRESULT)hBg;
            }
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
    HWND  m_btnGhost    = nullptr;
    HWND  m_btnExit     = nullptr;
    HWND  m_btnResFull  = nullptr;
    HWND  m_btnRes1920  = nullptr;
    HWND  m_btnRes1280  = nullptr;
    HWND  m_btnRes640   = nullptr;
    HWND  m_btnBit8     = nullptr;
    HWND  m_btnBit16    = nullptr;
    HWND  m_btnFrame    = nullptr;

    // -1 so the first UpdateFormat always paints, and so ResetStateCache()
    // can force a repaint after the buttons are recreated.
    std::wstring m_reticleLabel = L"RETICLE";
    int  m_lastResW = -1;
    int  m_lastResH = -1;
    int  m_lastBits = -1;

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
    bool m_lastGhostActive = false;

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
    HWND m_btnHW = nullptr;
    std::function<void()> m_onDash;
    HWND m_btnDash = nullptr;
    std::function<void()> m_onStack;
    HWND m_btnStack = nullptr;
    std::function<void()> m_onFollow;
    HWND m_btnFollow = nullptr;
    std::function<void()> m_onDerot;
    HWND m_btnDerot = nullptr;
    // The link state last painted. m_hwLinksKnown distinguishes "all
    // three down" from "never set", which matters because the
    // all-down state is the DEFAULT-constructed one -- without the
    // flag, the first paint of a disconnected rig is skipped as
    // unchanged and the button keeps its creation-time label.
    raduk::core::DeviceLinks m_lastLinks;
    bool m_hwLinksKnown = false;
    std::function<void()> m_onExit;
    std::function<void(int,int)> m_onRes;
    std::function<void(int)>     m_onBits;
    std::function<void()>        m_onFrame;
};

} // namespace raduk::gui
