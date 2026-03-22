#pragma once
// ============================================================
// ControlPanel.h — Persistent floating panel for RadukCapture
// Phase 9/10 complete: autofocus, quality gate, profile EDIT
// WS_CHILD | WS_CLIPCHILDREN, scrollable, PANEL_WIDTH=520
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
#include <vector>
#include <string>
#include <array>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include "../core/DeviceFactory.h"
#include "../core/IMount.h"
#include "../core/FrameAnalyzer.h"

#pragma comment(lib, "comctl32.lib")

namespace raduk::gui {

static constexpr int PANEL_WIDTH  = 560;   // panel client width (wider for SharpCap-style layout)
static constexpr int PANEL_HEIGHT = 900;   // reference height (panel is scrollable)

class ControlPanel {
public:
    ControlPanel() = default;
    ~ControlPanel();

    // Creates the panel as a WS_CHILD of mainHwnd, docked flush right.
    bool Create(HWND mainHwnd,
                raduk::core::ASICamera*    camera,
                raduk::core::EAFFocuser*   focuser,
                raduk::core::IMount*       mount);

    // Call when the main window moves or resizes to keep docked position.
    void OnMainWindowMoved();
    void SnapToMainWindow();  // public — called from main.cpp after Create()

    // Dock/undock toggle — kept for API compat; panel is always docked as WS_CHILD.
    void ToggleDock();
    bool IsDocked()   const { return m_docked; }
    HWND PanelHwnd()  const { return m_panelHwnd; }

    // Message routing — called from PanelWndProc
    bool HandleCommand(WPARAM wParam, LPARAM lParam);
    bool HandleScroll(WPARAM wParam, LPARAM lParam);

    void EmergencyStop();
    void UpdateReadouts();

    // Called each render tick with latest frame analysis.
    void UpdateAnalysis(const raduk::core::AnalysisResult& analysis);

    // Temperature sample — called from readout loop (~500ms)
    void PushTempSample(float camC, float focC) {
        m_tempCamC = camC; m_tempFocC = focC; m_hasTempData = true;
        m_tempHistory.push_back({camC, focC});
        if ((int)m_tempHistory.size() > TEMP_HISTORY)
            m_tempHistory.pop_front();
        if (m_panelHwnd) InvalidateRect(m_panelHwnd, nullptr, FALSE);
    }

    // Status bar data — called from readout loop
    void UpdateStatus(float fps, int lost, float bufPct, int bufFrames,
                      int bufTotal, float diskGB, bool recording) {
        m_status = {fps, lost, bufPct, bufFrames, bufTotal, diskGB, recording};
        if (m_panelHwnd) InvalidateRect(m_panelHwnd, nullptr, FALSE);
    }

    // ── Quality gate accessors (Phase 10) ─────────────────────
    bool  GateEnabled()   const {
        if (!m_btnGateEnable) return false;
        return SendMessage(m_btnGateEnable, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    float GateThreshold() const {
        if (!m_editGateThreshold) return 0.0f;
        wchar_t buf[16] = {};
        GetWindowTextW(m_editGateThreshold, buf, 16);
        return (float)_wtof(buf);
    }
    void SetGateActive(bool gating) {
        // Visual feedback: tint the GATE checkbox label amber when actively gating
        // (waiting for a good frame), green when passing. Just invalidate for now —
        // WM_CTLCOLORBTN will pick up m_gateActive and tint accordingly.
        m_gateActive = gating;
        if (m_btnGateEnable) InvalidateRect(m_btnGateEnable, nullptr, TRUE);
    }

    // ── Callbacks ─────────────────────────────────────────────
    void SetProfileLoadCallback(std::function<void(int)> cb)        { m_onProfileLoad      = cb; }
    void SetProfileSaveCallback(std::function<void(int)> cb)        { m_onProfileSave      = cb; }
    void SetProfileEditCallback(std::function<void(int)> cb)        { m_onProfileEdit      = cb; }
    void SetZoomCycleCallback(std::function<void()> cb)             { m_onZoomCycle        = cb; }
    void SetFullscreenToggleCallback(std::function<void()> cb)      { m_onFullscreenToggle = cb; }
    void SetDockToggleCallback(std::function<void()> cb)            { m_onDockToggle       = cb; }
    void SetPlanetLockCallback(std::function<void(bool)> cb)        { m_onPlanetLock       = cb; }
    void SetGoToCallback(std::function<void(const std::string&)> cb){ m_onGoTo             = cb; }
    void SetSolveSyncCallback(std::function<void()> cb)             { m_onSolveSync        = cb; }
    void SetRecordStartCallback(std::function<void()> cb)           { m_onRecordStart      = cb; }
    void SetRecordStopCallback(std::function<void()> cb)            { m_onRecordStop       = cb; }

    // Phase 9 — autofocus
    // cb(true) = start autofocus; cb(false) = abort
    void SetAutofocusCallback(std::function<void(bool)> cb) { m_onAutofocus = cb; }
    // Mount controller button — clicked to show/reposition the floating panel
    void SetMountCtrlCallback(std::function<void()> cb) { m_onMountCtrl = cb; }
    void SetAutofocusRunning(bool running) {
        m_afRunning = running;
        if (m_btnAutofocus) {
            SetWindowTextW(m_btnAutofocus, running ? L"ABORT AF" : L"\u2605  AUTO FOCUS");
            InvalidateRect(m_btnAutofocus, nullptr, TRUE);
        }
    }
    void SetAutofocusStatus(const wchar_t* msg) {
        m_afStatusMsg = msg ? msg : L"";
        if (m_lblAFStatus) SetWindowTextW(m_lblAFStatus, m_afStatusMsg.c_str());
    }

    // GoTo status label — safe to call from any thread via SendMessage
    void SetGoToStatus(const std::wstring& status) {
        m_lastGoToStatus = status;
        if (m_lblGoToStatus) SetWindowTextW(m_lblGoToStatus, status.c_str());
    }

    // SkyMap lock indicator — call after GoTo acquisition completes/resets.
    // locked=true  → shows "● SKYMAP LOCKED" in green, target name beneath
    // locked=false → clears both labels
    void SetSkyMapLocked(bool locked, const std::wstring& targetName = L"") {
        m_skyMapLocked      = locked;
        m_skyMapTarget      = targetName;
        if (m_lblSkyMapLock) {
            SetWindowTextW(m_lblSkyMapLock,
                locked ? L"\u25CF  SKYMAP LOCKED" : L"");
            InvalidateRect(m_lblSkyMapLock, nullptr, TRUE);
        }
        if (m_lblSkyMapTarget) {
            SetWindowTextW(m_lblSkyMapTarget,
                locked ? targetName.c_str() : L"");
            InvalidateRect(m_lblSkyMapTarget, nullptr, TRUE);
        }
    }

    void SetMount(raduk::core::IMount* mount) { m_mount = mount; }
    void SetRCControl(bool take);

    // ── Label updates ─────────────────────────────────────────
    void SetZoomLabel(const wchar_t* label);
    void SetDockLabel(bool docked);
    void SetProfileNames(const std::vector<std::string>& names);
    void SetActiveProfile(int index);

    void UpdateCaptureStatus(bool recording, uint32_t framesRecorded,
                              uint32_t framesDropped, double elapsedSec,
                              uint64_t bufferUsed, uint64_t bufferTotal,
                              uint32_t limitFrames,
                              double limitTimeSec, bool limitByFrames,
                              const std::string& outputDir);

    // Call immediately after starting/stopping a recording so the owner-drawn
    // RECORD button redraws without waiting for the next 500ms readout tick.
    void ForceRecordButtonUpdate(bool recording) {
        m_status.recording  = recording;
        m_paintRecording    = recording;
        if (m_btnRecord) {
            // SetWindowText forces a WM_DRAWITEM on BS_OWNERDRAW buttons
            // even when the text is not displayed — more reliable than
            // InvalidateRect alone, which can be coalesced by the message queue.
            SetWindowTextW(m_btnRecord,
                recording ? L"\u25A0  STOP RECORDING" : L"\u25BA  RECORD");
            InvalidateRect(m_btnRecord, nullptr, TRUE);
            UpdateWindow(m_btnRecord);
        }
    }

    std::string GetFilePrefix() const;
    std::string GetOutputDir() const;
    uint32_t    GetLimitFrames() const;
    double      GetLimitTimeSec() const;
    bool        GetLimitByFrames() const;

    int  JogRate()        const { return m_jogRate; }
    bool IsRCInControl()  const { return m_rcInControl; }
    bool IsPlanetLocked() const { return m_planetLock; }

private:
    // ── State ─────────────────────────────────────────────────
    bool m_rcInControl  = false;
    bool m_planetLock   = false;
    bool m_inMountSwap  = false;
    int  m_jogRate      = 9;
    bool m_afRunning    = false;
    bool m_gateActive   = false;       // true = gate is holding (waiting for good seeing)
    std::wstring m_afStatusMsg;
    std::wstring m_lastGoToStatus;

    static LRESULT CALLBACK PanelWndProc(HWND hwnd, UINT msg,
                                          WPARAM wParam, LPARAM lParam);

    // GDI painting
    void PaintAll(HDC hdc, const RECT& clientRect);
    void PaintHistogram(HDC hdc, int x, int y, int w, int h);
    void PaintStatus(HDC hdc, int x, int y, int w);
    void PaintTemperature(HDC hdc, int x, int y, int w, int h);

    // Layout rebuild
    void RebuildControls(int clientW, bool fromResize = false);

    // ── Profile cache — repopulated after every RebuildControls ──
    std::vector<std::string> m_cachedProfileNames;
    int                      m_cachedProfileIndex = 0;

    // ── Analysis ──────────────────────────────────────────────
    raduk::core::AnalysisResult m_analysis;
    bool m_hasAnalysis   = false;
    int  m_contentBottom = 0;

    // ── Status bar paint state ─────────────────────────────────
    uint64_t m_paintBufUsed     = 0;
    uint64_t m_paintBufTotal    = 1;
    double   m_paintDiskFreeGB  = 0.0;
    double   m_paintDiskTotalGB = 1.0;
    bool     m_paintRecording   = false;
    uint32_t m_paintFrames      = 0;
    uint32_t m_paintDrops       = 0;
    double   m_paintElapsed     = 0.0;
    float    m_paintFPS         = 0.0f;

    // ── Temperature chart ─────────────────────────────────────
    struct TempSample { float camC; float focC; };
    static constexpr int TEMP_HISTORY = 120;
    std::deque<TempSample> m_tempHistory;
    float m_tempCamC    = 0.0f;
    float m_tempFocC    = 0.0f;
    bool  m_hasTempData = false;

    // ── Status cache ───────────────────────────────────────────
    struct StatusCache {
        float fps          = 0.0f;
        int   framesLost   = 0;
        float bufferPct    = 0.0f;
        int   bufferFrames = 0;
        int   bufferTotal  = 512;
        float diskUsedGB   = 0.0f;
        bool  recording    = false;
    };
    StatusCache m_status;

    // ── Control creation helpers ───────────────────────────────
    HWND MakeLabel(const wchar_t* text, int x, int y, int w, int h, int id = 0);
    void MakeSectionLabel(const char* ascii, int x, int y, int w, int h);
    HWND MakeSlider(int x, int y, int w, int h, int lo, int hi, int val, int id);
    HWND MakeButton(const wchar_t* text, int x, int y, int w, int h, int id);
    HWND MakeValueLabel(const wchar_t* text, int x, int y, int w, int h, int id);
    HWND MakeEditBox(const wchar_t* text, int x, int y, int w, int h, int id);
    HWND MakeComboBox(int x, int y, int w, int h, int id);

    // Jog button subclassing
    void SubclassJogButton(HWND btn);
    static LRESULT CALLBACK JogButtonProc(HWND hwnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam,
                                           UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    void OnJogStart(HWND btn);
    void OnJogStop();

    void UpdatePlanetLockUI();
    void UpdateJogRateButtons();

    // ── Section tagging ───────────────────────────────────────
    enum class Section { Default, Header, Camera, EAF, Mount,
                         Image, Profile, Capture, Notes, Status, Histogram, Temperature };
    std::unordered_map<HWND, Section> m_ctrlSection;
    std::unordered_set<HWND> m_sectionHeaders;
    void TagControl(HWND h, Section s) { if (h) m_ctrlSection[h] = s; }
    void TagControls(std::initializer_list<HWND> hs, Section s) {
        for (auto h : hs) TagControl(h, s);
    }

    // Collapse state — Camera, EAF, Mount are collapsible
    std::unordered_map<Section, bool> m_collapsed;
    std::unordered_map<HWND, Section> m_headerSection;
    bool IsCollapsed(Section s) const {
        auto it = m_collapsed.find(s);
        return it != m_collapsed.end() && it->second;
    }
    // Dynamic height of GDI-painted strip (STATUS+HISTOGRAM+TEMPERATURE headers + content)
    int GdiStripHeight() const {
        const int HDR = 22;
        int h = 0;
        h += HDR + 2; // STATUS header always shown
        if (!IsCollapsed(Section::Status))      h += 66;
        h += HDR + 2; // HISTOGRAM header
        if (!IsCollapsed(Section::Histogram))   h += 110;
        h += HDR + 2; // TEMPERATURE header
        if (!IsCollapsed(Section::Temperature)) h += 130;
        return h;
    }
    void ToggleCollapse(Section s) {
        m_collapsed[s] = !IsCollapsed(s);
        RebuildControls(PANEL_WIDTH, true);
    }
    // Subclass proc for clickable section headers
    static LRESULT CALLBACK HeaderClickProc(HWND hwnd, UINT msg, WPARAM wParam,
                                              LPARAM lParam, UINT_PTR id, DWORD_PTR data);

    // Control IDs for collapse headers
    enum { ID_HDR_CAMERA = 2100, ID_HDR_EAF, ID_HDR_MOUNT };

    // ── GDI section header hit rects (STATUS/HISTOGRAM/TEMPERATURE) ──
    // Updated every PaintAll call; used by WM_LBUTTONDOWN for collapse clicks.
    RECT m_gdiHdrStatus      = {};
    RECT m_gdiHdrHistogram   = {};
    RECT m_gdiHdrTemperature = {};

    // ── Window handles ─────────────────────────────────────────
    HWND m_panelHwnd = nullptr;
    HWND m_mainHwnd  = nullptr;
    bool m_docked    = true;
    int  m_scrollY   = 0;

    raduk::core::ASICamera*    m_camera  = nullptr;
    raduk::core::EAFFocuser*   m_focuser = nullptr;
    raduk::core::IMount*       m_mount   = nullptr;

    // ── Camera controls ────────────────────────────────────────
    HWND m_sliderGain = nullptr, m_sliderExposure = nullptr;
    HWND m_sliderGamma = nullptr, m_sliderWBR = nullptr;
    HWND m_sliderWBB = nullptr, m_sliderUSB = nullptr;
    HWND m_lblGainVal = nullptr, m_lblExpVal = nullptr;
    HWND m_lblGammaVal = nullptr, m_lblWBRVal = nullptr;
    HWND m_lblWBBVal = nullptr, m_lblUSBVal = nullptr;

    // ── Focuser controls ───────────────────────────────────────
    HWND m_lblFocusPos = nullptr, m_lblFocusTemp = nullptr;
    HWND m_editFocusGoto = nullptr, m_btnFocusGoto = nullptr;
    HWND m_btnFocusStop = nullptr;
    HWND m_btnFocusIn5000 = nullptr, m_btnFocusIn1000 = nullptr;
    HWND m_btnFocusOut1000 = nullptr, m_btnFocusOut5000 = nullptr;
    HWND m_btnFocusIn100 = nullptr, m_btnFocusIn10 = nullptr;
    HWND m_btnFocusOut10 = nullptr, m_btnFocusOut100 = nullptr;
    HWND m_btnAutofocus = nullptr;
    HWND m_lblAFStatus  = nullptr;

    // ── Mount controls ─────────────────────────────────────────
    HWND m_lblMountConnected = nullptr;  // "● Device Name  Connected" status line
    HWND m_lblMountAz  = nullptr;        // Az readout
    HWND m_lblMountAlt = nullptr;        // Alt readout
    HWND m_lblMountRA  = nullptr;
    HWND m_lblMountDec = nullptr;
    HWND m_lblMountHA  = nullptr;
    HWND m_lblMountTrack = nullptr;
    HWND m_btnMountCtrl = nullptr;   // Shows/repositions floating mount controller
    HWND m_btnSync = nullptr;
    HWND m_lblMountControl = nullptr;
    HWND m_lblSkyMapLock   = nullptr;
    HWND m_lblSkyMapTarget = nullptr;
    bool m_skyMapLocked    = false;
    std::wstring m_skyMapTarget;
    // Jog controls moved to floating MountController — members kept for .cpp compat
    HWND m_btnMountStop = nullptr;
    HWND m_btnJogN = nullptr, m_btnJogS = nullptr;
    HWND m_btnJogE = nullptr, m_btnJogW = nullptr;
    HWND m_btnRate9 = nullptr, m_btnRate8 = nullptr;
    HWND m_btnRate7 = nullptr, m_btnRate5 = nullptr;
    HWND m_btnTracking = nullptr, m_btnPark = nullptr;
    int  m_activeJogAxis = -1;
    bool m_jogHeld       = false;

    // ── GoTo controls (Phase 6) ────────────────────────────────
    HWND m_editGoToTarget = nullptr;
    HWND m_btnGoToFind    = nullptr;
    HWND m_btnSolveSync   = nullptr;
    HWND m_lblGoToStatus  = nullptr;

    // ── Profile controls ───────────────────────────────────────
    HWND m_comboProfile    = nullptr;
    HWND m_btnProfileLoad  = nullptr;
    HWND m_btnProfileSave  = nullptr;
    HWND m_btnProfileEdit  = nullptr;   // Phase 9/10 — opens profile editor modal

    // ── View controls ──────────────────────────────────────────
    HWND m_btnZoom       = nullptr;
    HWND m_btnFullscreen = nullptr;
    HWND m_btnDock       = nullptr;

    // ── Capture controls (Phase 2) ─────────────────────────────
    HWND m_btnRecord       = nullptr;
    HWND m_lblRecStatus    = nullptr;
    HWND m_lblRecFrames    = nullptr;
    HWND m_lblRecBuffer    = nullptr;
    HWND m_lblDiskSpace    = nullptr;
    HWND m_editOutputDir   = nullptr;
    HWND m_btnBrowseDir    = nullptr;
    HWND m_editFilePrefix  = nullptr;
    HWND m_editLimitFrames = nullptr;
    HWND m_editLimitTime   = nullptr;
    HWND m_radioFrames     = nullptr;
    HWND m_radioTime       = nullptr;

    HWND m_editNotes         = nullptr;   // Session notes — multiline edit at bottom
    // ── Quality gate (Phase 10) ────────────────────────────────
    HWND m_editGateThreshold = nullptr;
    HWND m_btnGateEnable     = nullptr;

    // ── Control IDs ───────────────────────────────────────────
    enum ControlId {
        ID_SLIDER_GAIN = 1001, ID_SLIDER_EXP, ID_SLIDER_GAMMA,
        ID_SLIDER_WBR, ID_SLIDER_WBB, ID_SLIDER_USB,
        ID_LBL_GAIN_VAL = 1100, ID_LBL_EXP_VAL, ID_LBL_GAMMA_VAL,
        ID_LBL_WBR_VAL, ID_LBL_WBB_VAL, ID_LBL_USB_VAL,
        ID_LBL_FOCUS_POS = 1200, ID_LBL_FOCUS_TEMP, ID_EDIT_FOCUS_GOTO,
        ID_BTN_FOCUS_IN5000 = 1300, ID_BTN_FOCUS_IN1000,
        ID_BTN_FOCUS_OUT1000, ID_BTN_FOCUS_OUT5000,
        ID_BTN_FOCUS_IN100, ID_BTN_FOCUS_IN10,
        ID_BTN_FOCUS_OUT10, ID_BTN_FOCUS_OUT100,
        ID_BTN_FOCUS_GOTO, ID_BTN_FOCUS_STOP,
        ID_BTN_AUTOFOCUS,
        ID_LBL_AF_STATUS,
        ID_LBL_MOUNT_RA = 1400, ID_LBL_MOUNT_DEC, ID_LBL_MOUNT_TRACK,
        ID_LBL_MOUNT_AZ, ID_LBL_MOUNT_ALT, ID_LBL_MOUNT_HA,
        ID_LBL_MOUNT_CONNECTED,
        ID_BTN_SYNC = 1500,
        ID_BTN_MOUNT_CTRL,
        ID_BTN_MOUNT_STOP,
        ID_BTN_JOG_N, ID_BTN_JOG_S, ID_BTN_JOG_E, ID_BTN_JOG_W,
        ID_BTN_RATE_9, ID_BTN_RATE_8, ID_BTN_RATE_7, ID_BTN_RATE_5,
        ID_BTN_TRACKING, ID_BTN_PARK,
        ID_LBL_MOUNT_CONTROL,
        ID_LBL_SKYMAP_LOCK,
        ID_LBL_SKYMAP_TARGET,
        ID_COMBO_PROFILE = 1600,
        ID_BTN_PROFILE_LOAD, ID_BTN_PROFILE_SAVE,
        ID_BTN_PROFILE_EDIT,            // ← new EDIT button
        ID_EDIT_GOTO_TARGET = 1650,
        ID_BTN_GOTO_FIND,
        ID_BTN_SOLVE_SYNC,
        ID_LBL_GOTO_STATUS,
        ID_BTN_ZOOM = 1700,
        ID_BTN_FULLSCREEN,
        ID_BTN_DOCK,
        ID_BTN_RECORD = 1800,
        ID_LBL_REC_STATUS,
        ID_LBL_REC_FRAMES,
        ID_LBL_REC_BUFFER,
        ID_LBL_DISK_SPACE,
        ID_EDIT_OUTPUT_DIR,
        ID_BTN_BROWSE_DIR,
        ID_EDIT_FILE_PREFIX,
        ID_EDIT_LIMIT_FRAMES,
        ID_EDIT_LIMIT_TIME,
        ID_RADIO_FRAMES,
        ID_RADIO_TIME,
        ID_EDIT_GATE_THRESHOLD,
        ID_BTN_GATE_ENABLE,
        ID_EDIT_NOTES = 1900,
    };

    // ── Callbacks ─────────────────────────────────────────────
    std::function<void(int)>  m_onProfileLoad;
    std::function<void(int)>  m_onProfileSave;
    std::function<void(int)>  m_onProfileEdit;   // ← new EDIT callback
    std::function<void()>     m_onZoomCycle;
    std::function<void()>     m_onFullscreenToggle;
    std::function<void()>     m_onDockToggle;
    std::function<void()>     m_onRecordStart;
    std::function<void()>     m_onRecordStop;
    std::function<void(bool)> m_onPlanetLock;
    std::function<void(const std::string&)> m_onGoTo;
    std::function<void()>                   m_onSolveSync;
    std::function<void(bool)>               m_onAutofocus;  // Phase 9
    std::function<void()>                   m_onMountCtrl;  // floating mount controller toggle
};

} // namespace raduk::gui
