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
#include "../core/TextConv.h"

#pragma comment(lib, "comctl32.lib")

namespace raduk::gui {

static constexpr int PANEL_WIDTH  = 560;   // panel client width (wider for SharpCap-style layout)

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
    HWND PanelHwnd()  const { return m_panelHwnd; }

    // Message routing — called from PanelWndProc
    bool HandleCommand(WPARAM wParam, LPARAM lParam);
    bool HandleScroll(WPARAM wParam, LPARAM lParam);

    void EmergencyStop();
    void UpdateReadouts();
    // Programmatically update a slider position and its value label.
    // isExposure=true updates the exposure slider, false updates gain.
    void UpdateSlider(bool isExposure, int val);
    // Phase 98: one exposure formatter, shared. See the .cpp.
    static void FormatExposure(wchar_t* buf, size_t n, double ms);
    void CommitTypedExposure();
    static LRESULT CALLBACK ExposureEditProc(HWND, UINT, WPARAM, LPARAM,
                                            UINT_PTR, DWORD_PTR);

    // Called each render tick with latest frame analysis.
    // Point the TELESCOPE card's GoTo box at an object.
    //
    // Called when something else has already decided to slew there --
    // a click on the sky map, or a pick from the object info panel. The
    // box is the input to a button that moves the telescope, and it
    // used to keep whatever was last typed, so after slewing to Saturn
    // from the map the GOTO button still said Jupiter. A mis-click then
    // sent the mount somewhere nobody had asked for.
    //
    // Writes the cache unconditionally and the control only if it
    // exists: TELESCOPE is collapsible, and a map click with the
    // section shut is the normal case.
    void SetGoToTarget(const std::string& name) {
        if (name.empty()) return;
        wchar_t w[64] = {};
        MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, w, 63);
        m_ui.goToTarget = w;
        if (m_editGoToTarget) SetWindowTextW(m_editGoToTarget, w);
    }

    void UpdateAnalysis(const raduk::core::AnalysisResult& analysis);

    // Status bar data — called from readout loop
    void UpdateStatus(float fps, int lost, float bufPct, int bufFrames,
                      int bufTotal, float diskGB, bool recording) {
        // Cached only. The panel no longer draws this -- StatusWindow
        // does. Kept because the readout loop still calls here and the
        // values are wanted by whatever displays them.
        const bool was = m_status.recording;
        m_status = {fps, lost, bufPct, bufFrames, bufTotal, diskGB, recording};

        // ── Phase 98: repaint the button when the state changes ──
        //
        // From the owner's report: after a run that stops on its own
        // frame limit, the RECORD button stays showing STOP RECORDING,
        // so it looks like capture is still going.
        //
        // The state was never wrong. m_status.recording does go false
        // here, on this ~500 ms timer, from the capturer's own
        // IsRecording(). What was missing is that NOTHING INVALIDATED
        // THE BUTTON, and it is owner-drawn: it repaints when Windows
        // decides to, which after a click is immediately and after a
        // limit-triggered stop is never.
        //
        // Invalidating on the EDGE rather than patching the frame-limit
        // call site fixes every stop path at once -- the frame limit,
        // the time limit, the disk-full abort, the emergency stop and
        // the mosaic runner's per-panel stops -- instead of the one
        // that happened to be reported. Only on a change, so this is
        // not a repaint twice a second forever.
        if (was != recording && m_btnRecord)
            InvalidateRect(m_btnRecord, nullptr, FALSE);
    }

    // ── Quality gate accessors (Phase 10) ─────────────────────
    bool  GateEnabled()   const {
        // Falls back to the cache, not to false. The checkbox does not
        // exist while CAPTURE is collapsed, and answering false there
        // silently disabled the gate for the whole session.
        if (!m_btnGateEnable) return m_ui.gateEnabled == 1;
        return SendMessage(m_btnGateEnable, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    float GateThreshold() const {
        // Same reasoning as GateEnabled: a collapsed section must not
        // report 0, which with the gate on would admit every frame and
        // look like the gate had stopped working.
        if (!m_editGateThreshold)
            return m_ui.gateThreshold.empty()
                 ? 0.0f : (float)_wtof(m_ui.gateThreshold.c_str());
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
    // Phase 67: "cables dressed" zeroes the cord-wrap total. It is the
    // only thing that legitimately resets it, because the app has no
    // way to observe a cable - it has to be told.
    void SetCablesDressedCallback(std::function<void()> cb) { m_onCablesDressed = cb; }
    // Phase 67 step 3: opens the rig setup view.
    void SetRigSetupCallback(std::function<void()> cb) { m_onRigSetup = cb; }
    // Phase 72: reopen the readiness check on demand. It appears at
    // startup only when something is wrong, which is right -- but a
    // failing check is usually something you walk off and fix, and
    // until now the only way back to it was relaunching the app.
    void SetReadinessCheckCallback(std::function<void()> cb) { m_onReadinessCheck = cb; }

    // Phase 70: the acquisition stage and what is blocking it, shown
    // continuously rather than only in response to a button press. A
    // state machine that reports itself only when poked leaves the
    // user guessing which of five stages they are in and why it is
    // not advancing - which is most of what went wrong on 2026-10-08.
    void SetAcquisitionStatus(const std::wstring& text) {
        if (text == m_acqStatus) return;        // avoid needless repaints
        m_acqStatus = text;
        if (m_lblMountControl) {
            // Phase 71 (review): when acquisition ends the status goes
            // empty, and the label was simply blanked -- so an active
            // PLANET LOCK stopped being indicated until something else
            // happened to call UpdatePlanetLockUI. Hand the label back
            // to the other writer rather than leaving it blank.
            const wchar_t* shown = !m_acqStatus.empty() ? m_acqStatus.c_str()
                                 : (m_planetLock ? L"PLANET LOCK ACTIVE" : L"");
            SetWindowTextW(m_lblMountControl, shown);
            InvalidateRect(m_lblMountControl, nullptr, TRUE);
        }
    }
    // Jog activity — cb(true) on jog start, cb(false) on jog stop.
    // main.cpp suspends centroid-tracker corrections during manual jogs
    // (otherwise the guide loop fights the user).

    // Phase 12 — Auto-Exposure
    // cb(true) = enable AE advisory; cb(false) = disable
    // cb with second bool: true = auto apply, false = advisory only
    void SetAECallback(std::function<void(bool enabled, bool autoApply)> cb) {
        m_onAEToggle = cb;
    }
    void SetAEStatus(const wchar_t* msg) {
        if (m_lblAEStatus) SetWindowTextW(m_lblAEStatus, msg ? msg : L"");
    }
    // state: 0=off, 1=advisory, 2=auto
    void SetAEState(int state) {
        m_aeState = state;
        if (m_btnAEAuto) {
            const wchar_t* label = (state == 2) ? L"AE AUTO"
                                 : (state == 1) ? L"AE ADV"
                                 :                L"AE OFF";
            SetWindowTextW(m_btnAEAuto, label);
            InvalidateRect(m_btnAEAuto, nullptr, TRUE);
        }
    }
    int AEState() const { return m_aeState; }

    // Phase 23 — RGB Align
    void SetRGBAlignCallback(std::function<void()> cb) { m_onRGBAlign = cb; }
    int  GetRGBManual() const {
        if (!m_sliderRGBManual) return 0;
        return (int)SendMessage(m_sliderRGBManual, TBM_GETPOS, 0, 0) - 20;
    }
    void SetRGBAlignActive(bool active) {
        if (m_btnRGBAlign) {
            SetWindowTextW(m_btnRGBAlign, active ? L"RGB ALIGN ON" : L"RGB ALIGN");
            InvalidateRect(m_btnRGBAlign, nullptr, TRUE);
        }
    }
    void SetRGBAlignLabel(int shift) {
        if (m_lblRGBAlign) {
            wchar_t buf[24]; swprintf(buf, 24, L"ALIGN  %+d px", shift);
            SetWindowTextW(m_lblRGBAlign, buf);
        }
    }

    // Phase 25: Display FX
    void SetFXCycleCallback(std::function<void()> cb) { m_onFXCycle = cb; }
    void SetFXLabel(const wchar_t* label) {
        if (m_btnFX) {
            SetWindowTextW(m_btnFX, label ? label : L"[FX] NORMAL");
            InvalidateRect(m_btnFX, nullptr, TRUE);
        }
    }

    // Phase 26: Night Vision Mode
    void SetNightVisionCallback(std::function<void()> cb) { m_onNightVision = cb; }
    void SetNightVisionActive(bool on) {
        if (m_btnNight) {
            SetWindowTextW(m_btnNight, on ? L"[N] NIGHT ON" : L"[N] NIGHT");
            InvalidateRect(m_btnNight, nullptr, TRUE);
        }
    }

    // Phase 38: Single-Channel Display Mode
    void SetChannelCycleCallback(std::function<void()> cb) { m_onChannelCycle = cb; }
    void SetChannelLabel(const wchar_t* label) {
        if (m_btnChannel) {
            SetWindowTextW(m_btnChannel, label ? label : L"[H] CH: ALL");
            InvalidateRect(m_btnChannel, nullptr, TRUE);
        }
    }
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
    // ADC readout — called from readout loop with ADCAssistant::SuggestionText()
    void SetADCStatus(const wchar_t* msg) {
        if (m_lblADCStatus) SetWindowTextW(m_lblADCStatus, msg ? msg : L"");
    }
    // Phase 83: restate the filter the ADC figure was computed over.
    // Call after anything that can change the band (rig setup, a
    // reloaded rig.ini) so the label can never outlive the setting.
    // Phase 26: repaint everything after a palette change. The GDI
    // strip is drawn on demand so invalidating is enough, but the child
    // controls hold their own background brushes and only re-ask for
    // them on their next WM_CTLCOLOR*, which is why they are
    // invalidated individually rather than relying on the parent.
    void ForceRepaint() {
        if (!m_panelHwnd) return;
        InvalidateRect(m_panelHwnd, nullptr, TRUE);
        for (auto& kv : m_ctrlSection)
            if (kv.first) InvalidateRect(kv.first, nullptr, TRUE);
        for (HWND h : m_sectionHeaders)
            if (h) InvalidateRect(h, nullptr, TRUE);
    }

    void RefreshFilterLabel();

    // ── Phases 27 & 28: calibration frames ────────────────────
    // ── Phase 37: display stretch ─────────────────────────────
    void SetStretchCallback(std::function<void(int,int)> cb) { m_onStretch = cb; }
    void SetStretchLabel(const std::wstring& t) {
        m_stretchLabel = t;
        if (m_lblStretch) SetWindowTextW(m_lblStretch, t.c_str());
    }
    int  BlackPoint() const {
        if (!m_sliderBlack) return m_uiBlack;
        return (int)SendMessage(m_sliderBlack, TBM_GETPOS, 0, 0);
    }
    int  WhitePoint() const {
        if (!m_sliderWhite) return m_uiWhite;
        return (int)SendMessage(m_sliderWhite, TBM_GETPOS, 0, 0);
    }
    // Seeds the sliders, and the cache the getters fall back to while
    // IMAGE is collapsed -- the same hole that silently disabled the
    // quality gate in review pass 2.
    void SetStretchPoints(int black, int white) {
        m_uiBlack = black; m_uiWhite = white;
        if (m_sliderBlack) SendMessage(m_sliderBlack, TBM_SETPOS, TRUE, black);
        if (m_sliderWhite) SendMessage(m_sliderWhite, TBM_SETPOS, TRUE, white);
    }

    void SetCalDarkCallback (std::function<void()> cb) { m_onCalDark  = cb; }
    void SetCalFlatCallback (std::function<void()> cb) { m_onCalFlat  = cb; }
    void SetCalClearCallback(std::function<void()> cb) { m_onCalClear = cb; }
    // Status is a sentence, not a flag: "none", "building", "applied" and
    // "loaded but does not fit this format" need different responses from
    // the user, and a tick would collapse all four into one.
    void SetCalStatus(const std::wstring& text) {
        m_calStatus = text;
        if (m_lblCalStatus) SetWindowTextW(m_lblCalStatus, text.c_str());
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

    // Phase 67: altitude and azimuth, plus accumulated cord wrap.
    // Azimuth was read from the mount every poll and discarded, and
    // these labels only ever showed "--" -- so there was no way to see
    // how far the mount had rotated, which is what wraps a cable.
    // wrapDeg is signed cumulative travel; pass showWrap false to keep
    // the readout clean until it means something.
    void SetMountAltAz(float altDeg, float azDeg, bool valid,
                       double wrapDeg = 0.0, bool showWrap = false,
                       bool wrapOverLimit = false) {
        m_altDeg       = altDeg;
        m_azDeg        = azDeg;
        m_altAzValid   = valid;
        m_wrapDeg      = wrapDeg;
        m_showWrap     = showWrap;
        m_wrapOverLimit= wrapOverLimit;
    }
    void SetRCControl(bool take);

    // ── Label updates ─────────────────────────────────────────
    void SetZoomLabel(const wchar_t* label);
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

    // Phase 41: show PAUSED state on RECORD button (amber)
    void SetPausedState(bool paused) {
        m_recordPaused = paused;
        if (m_btnRecord) {
            const wchar_t* label = paused             ? L"\u23F8  PAUSED"
                                 : m_status.recording ? L"\u25A0  STOP RECORDING"
                                 :                      L"\u25BA  RECORD";
            SetWindowTextW(m_btnRecord, label);
            InvalidateRect(m_btnRecord, nullptr, TRUE);
        }
    }

    std::string GetFilePrefix() const;

    // ── Phase 85: push a loaded profile into the panel ─────────
    // These exist because the panel had getters only. ApplyProfile
    // changed the camera and nothing told the UI, so after loading a
    // profile the sliders still showed the previous values -- and
    // nudging one then committed that stale value straight back to
    // the camera, undoing the profile that had just been applied.
    // exposureUs, not ms: the slider is in microseconds, as the move
    // handler has always been.
    void SetCameraSliders(int gain, int exposureUs, int gamma,
                          int wbRed, int wbBlue, int usbBandwidth);
    void SetFilePrefix(const std::string& prefix);
    void SetCaptureLimit(bool byFrames, uint32_t frames, double timeSec);
    std::string GetFrameType() const;  // Phase 44: "Light", "Dark", "Flat", "Bias"
    std::string GetOutputDir() const;

    // Notes — profile-linked (Phase 24 / notes redesign)
    //
    // The box is not always there to ask. Every section starts
    // collapsed, and RebuildControls destroys every child on each
    // toggle, so m_editNotes is null until NOTES has been opened at
    // least once -- and a getter that answered "" for a missing
    // control told SAVE a profile had no note while its note sat in
    // m_ui.notes. Both accessors go through that cache, which
    // Snapshot/RestoreUi already treat as the store of record.
    std::string GetNotes() const {
        if (!m_editNotes) return raduk::core::ToUtf8(m_ui.notes);
        return raduk::core::GetWindowTextUtf8(m_editNotes);
    }
    // profileName says WHOSE note this is. SAVE needs it to tell this
    // profile's note, deliberately emptied, from another profile's
    // note that was never loaded here -- see NotesBoxMayReplaceStored
    // in TargetProfile.h. Pass nothing for text that is not a
    // profile's note: the box then owns no profile, and an empty box
    // can overwrite nothing.
    void SetNotes(const std::string& text, const std::string& profileName = "") {
        m_ui.notes     = raduk::core::ToWide(text);
        m_notesProfile = profileName;
        if (m_editNotes) SetWindowTextW(m_editNotes, m_ui.notes.c_str());
    }
    // The profile the NOTES box was last populated from; empty if the
    // box holds no profile's note.
    const std::string& NotesProfile() const { return m_notesProfile; }
    uint32_t    GetLimitFrames() const;
    double      GetLimitTimeSec() const;
    bool        GetLimitByFrames() const;

    int  JogRate()        const { return m_jogRate; }
    // Set the rate WITHOUT firing the change callback - used to mirror a
    // change made in the floating Mount Controller, which keeps the two
    // panels in step without bouncing the notification back and forth.
    void SetJogRate(int r);
    // Fired when the user picks a rate on THIS panel.
    void SetJogRateChangeCallback(std::function<void(int)> cb) {
        m_onJogRateChange = std::move(cb);
    }
    bool IsRCInControl()  const { return m_rcInControl; }
    bool IsPlanetLocked() const { return m_planetLock; }

private:
    // ── State ─────────────────────────────────────────────────
    bool m_rcInControl  = false;
    bool m_planetLock   = false;
    bool m_inMountSwap  = false;
    int  m_jogRate      = 9;
    std::function<void(int)> m_onJogRateChange;
    bool m_afRunning    = false;
    bool m_gateActive   = false;       // true = gate is holding (waiting for good seeing)
    std::wstring m_afStatusMsg;
    std::wstring m_lastGoToStatus;

    static LRESULT CALLBACK PanelWndProc(HWND hwnd, UINT msg,
                                          WPARAM wParam, LPARAM lParam);

    // GDI painting



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
    bool     m_recordPaused     = false;  // Phase 41: amber PAUSED state on RECORD button
    uint32_t m_paintFrames      = 0;
    uint32_t m_paintDrops       = 0;
    double   m_paintElapsed     = 0.0;
    float    m_paintFPS         = 0.0f;

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
    HWND MakeSlider(int x, int y, int w, int h, int lo, int hi, int val, int id);
    HWND MakeButton(const wchar_t* text, int x, int y, int w, int h, int id);
    HWND MakeValueLabel(const wchar_t* text, int x, int y, int w, int h, int id);
    HWND MakeEditBox(const wchar_t* text, int x, int y, int w, int h, int id);
    HWND MakeComboBox(int x, int y, int w, int h, int id);

    // Jog button subclassing

    void UpdatePlanetLockUI();
    void UpdateJogRateButtons();

    // ── Section tagging ───────────────────────────────────────
    // Histogram and Temperature are gone from here: they are permanent
    // overlays on the preview now (PreviewRenderer::DrawSidePanels), so
    // they no longer need a collapse state or a header to hit-test.
    enum class Section { Default, Header, Camera, EAF, Mount,
                         Image, Profile, Capture, Notes, Status };
    std::unordered_map<HWND, Section> m_ctrlSection;
    std::unordered_set<HWND> m_sectionHeaders;
    void TagControl(HWND h, Section s) { if (h) m_ctrlSection[h] = s; }
    void TagControls(std::initializer_list<HWND> hs, Section s) {
        for (auto h : hs) TagControl(h, s);
    }

    // Collapse state. Every collapsible section starts COLLAPSED - see
    // InitCollapsed(), called from Create().
    std::unordered_map<Section, bool> m_collapsed;
    void InitCollapsed() {
        for (Section s : { Section::Image,
                           Section::Camera, Section::EAF, Section::Mount,
                           Section::Capture, Section::Notes,
                           Section::Status })
            m_collapsed[s] = true;
    }

    // Values the user has typed or chosen, kept ACROSS a rebuild.
    //
    // RebuildControls destroys every child window and recreates it, and
    // it runs on every collapse toggle - so without this, expanding any
    // one section reset the capture prefix to "Jupiter", the output
    // directory, the frame limits and the session notes. The controls
    // are the only store for those values; nothing else holds them.
    // Harmless while sections were expanded by default and rarely
    // toggled; a trap now that everything starts collapsed and gets
    // opened and closed constantly.
    struct UiValues {
        std::wstring filePrefix, outputDir, limitFrames, limitTime;
        std::wstring notes, gateThreshold;
        // The TELESCOPE card's GoTo target. It was not cached, so every
        // collapse toggle rebuilt the control and reset it to the
        // default "Jupiter" -- the same trap that took the capture
        // prefix, the notes and the quality gate.
        std::wstring goToTarget;
        int  frameType    = -1;
        int  limitByFrames = -1;   // -1 unknown, 0 time, 1 frames
        // Review pass 2: the capture quality gate lived ONLY in its
        // checkbox, which exists only while CAPTURE is expanded. Since
        // every section now starts collapsed, GateEnabled() found a null
        // handle and answered false -- so the gate was off at every
        // launch, and ticking it then collapsing the section turned it
        // back off without saying so. -1 = never set.
        int  gateEnabled  = -1;
    } m_ui;
    void SnapshotUi();
    void RestoreUi();
    // Which profile m_ui.notes belongs to. It lives beside the cache
    // because the two are only meaningful together.
    std::string m_notesProfile;
    std::unordered_map<HWND, Section> m_headerSection;
    bool IsCollapsed(Section s) const {
        auto it = m_collapsed.find(s);
        return it != m_collapsed.end() && it->second;
    }
    // There is no GDI-painted strip any more.
    //
    // STATUS was the last of it, and it was the last thing the panel
    // painted onto its OWN dc rather than building from child controls.
    // That put it on a different plane from every other section: the
    // children are clipped and composited over the parent, while the
    // strip went underneath them, so each collapse toggle -- which
    // destroys and recreates every child -- left the parent's painted
    // strip showing through wherever a child had been. It also meant
    // the STATUS header was drawn by a different code path from all the
    // others, so collapsed it did not line up with them.
    //
    // Kept as a function returning zero rather than deleted, because
    // the scroll extent maths reads it in several places and a zero is
    // clearer there than removing the term.
    int GdiStripHeight() const { return 0; }
    void ToggleCollapse(Section s) {
        // Preserve scroll position so panel doesn't jump to top on collapse
        int savedScroll = m_scrollY;
        m_collapsed[s] = !IsCollapsed(s);
        RebuildControls(PANEL_WIDTH, true);
        // Restore scroll if it's still valid within the new content height
        if (savedScroll > 0) {
            RECT cr; GetClientRect(m_panelHwnd, &cr);
            int maxScroll = m_contentBottom + GdiStripHeight() - cr.bottom;
            if (maxScroll > 0) {
                int restored = savedScroll < maxScroll ? savedScroll : maxScroll;
                SCROLLINFO si = {};
                si.cbSize = sizeof(si); si.fMask = SIF_POS;
                si.nPos = restored;
                SetScrollInfo(m_panelHwnd, SB_VERT, &si, TRUE);
                ScrollWindowEx(m_panelHwnd, 0, -restored, nullptr, nullptr,
                               nullptr, nullptr, SW_SCROLLCHILDREN | SW_INVALIDATE);
                m_scrollY = restored;
            }
        }
    }
    // Subclass proc for clickable section headers
    static LRESULT CALLBACK HeaderClickProc(HWND hwnd, UINT msg, WPARAM wParam,
                                              LPARAM lParam, UINT_PTR id, DWORD_PTR data);

    // Control IDs for collapse headers


    // ── Window handles ─────────────────────────────────────────
    HWND m_panelHwnd = nullptr;
    HWND m_mainHwnd  = nullptr;
    int  m_scrollY   = 0;

    raduk::core::ASICamera*    m_camera  = nullptr;
    raduk::core::EAFFocuser*   m_focuser = nullptr;
    raduk::core::IMount*       m_mount   = nullptr;

    // ── Camera controls ────────────────────────────────────────
    HWND m_sliderGain = nullptr, m_sliderExposure = nullptr;
    // Phase 98: the exposure readout IS an edit box now, so an exact
    // value can be typed. m_lblExpVal is left declared and null -- the
    // callers that wrote to it are guarded rather than deleted, since
    // a label and a box showing the same number is two things to
    // disagree.
    HWND m_editExposure = nullptr;
    // Set while the operator is dragging a slider, so auto-exposure
    // does not write to the thumb under their hand.
    HWND m_draggingSlider = nullptr;
    double m_lastLiveApplyMs = 0.0;
    HWND m_sliderGamma = nullptr, m_sliderWBR = nullptr;
    HWND m_sliderWBB = nullptr, m_sliderUSB = nullptr;
    HWND m_btnAEAuto = nullptr;   // Phase 12: AE mode toggle
    HWND m_lblAEStatus = nullptr; // Phase 12: AE status text
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
    HWND m_lblADCStatus = nullptr;  // ADC dispersion readout in FOCUSER section
    HWND m_lblFilter    = nullptr;  // Phase 83: the band that readout assumes
    HWND m_btnCalDark   = nullptr;  // Phase 27
    HWND m_btnCalFlat   = nullptr;  // Phase 28
    HWND m_btnCalClear  = nullptr;
    HWND m_lblCalStatus = nullptr;
    HWND m_sliderBlack  = nullptr;  // Phase 37
    HWND m_sliderWhite  = nullptr;
    HWND m_lblStretch   = nullptr;
    int  m_uiBlack = 0, m_uiWhite = 255;
    std::wstring m_stretchLabel = L"Stretch: off";
    std::wstring m_calStatus = L"Cal: none";

    // ── Mount controls ─────────────────────────────────────────
    HWND m_lblMountConnected = nullptr;  // "● Device Name  Connected" status line
    float  m_altDeg        = 0.0f;
    float  m_azDeg         = 0.0f;
    bool   m_altAzValid    = false;
    double m_wrapDeg       = 0.0;
    bool   m_showWrap      = false;
    bool   m_wrapOverLimit = false;

    HWND m_lblMountAz  = nullptr;        // Az readout
    HWND m_lblMountAlt = nullptr;        // Alt readout
    HWND m_lblMountRA  = nullptr;
    HWND m_lblMountDec = nullptr;
    HWND m_lblMountHA  = nullptr;
    HWND m_lblMountTrack = nullptr;
    HWND m_btnMountCtrl = nullptr;   // Shows/repositions floating mount controller
    HWND m_btnSync = nullptr;
    HWND m_btnCables = nullptr;   // cord-wrap reset
    HWND m_btnRigSetup = nullptr;
    HWND m_btnReady = nullptr;   // reopen the readiness check
    std::wstring m_acqStatus;   // last acquisition line shown
    HWND m_lblMountControl = nullptr;
    HWND m_lblSkyMapLock   = nullptr;
    HWND m_lblSkyMapTarget = nullptr;
    bool m_skyMapLocked    = false;
    std::wstring m_skyMapTarget;
    // Jog controls moved to floating MountController — members kept for .cpp compat
    HWND m_btnMountStop = nullptr;
    HWND m_btnRate9 = nullptr, m_btnRate8 = nullptr;
    HWND m_btnRate7 = nullptr, m_btnRate5 = nullptr;
    HWND m_btnTracking = nullptr, m_btnPark = nullptr;
    // Kept although the panel no longer jogs: EmergencyStop() and the
    // STOP button both clear them, and a safety path that over-resets
    // costs nothing while one that under-resets strands a moving mount.
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

    // ── Capture controls (Phase 2) ─────────────────────────────
    HWND m_btnRecord       = nullptr;
    HWND m_lblRecStatus    = nullptr;
    HWND m_lblRecFrames    = nullptr;
    HWND m_lblRecBuffer    = nullptr;
    HWND m_lblDiskSpace    = nullptr;
    HWND m_editOutputDir   = nullptr;
    HWND m_btnBrowseDir    = nullptr;
    HWND m_editFilePrefix  = nullptr;
    HWND m_comboFrameType  = nullptr;
    HWND m_editLimitFrames = nullptr;
    // Phase 23: RGB align
    HWND m_btnRGBAlign     = nullptr;
    HWND m_sliderRGBManual = nullptr;
    HWND m_lblRGBAlign     = nullptr;
    // Phase 25: FX
    HWND m_btnFX           = nullptr;
    // Phase 26: Night Vision
    HWND m_btnNight        = nullptr;
    // Phase 38: Single-Channel Display
    HWND m_btnChannel      = nullptr;
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
        ID_BTN_AE_AUTO, ID_LBL_AE_STATUS,
        // Phase 98: the camera section had no numeric entry at all.
        ID_EDIT_EXPOSURE,
        ID_LBL_GAIN_VAL = 1100, ID_LBL_GAMMA_VAL,
        ID_LBL_WBR_VAL, ID_LBL_WBB_VAL, ID_LBL_USB_VAL,
        ID_LBL_FOCUS_POS = 1200, ID_LBL_FOCUS_TEMP, ID_EDIT_FOCUS_GOTO,
        ID_BTN_FOCUS_IN5000 = 1300, ID_BTN_FOCUS_IN1000,
        ID_BTN_FOCUS_OUT1000, ID_BTN_FOCUS_OUT5000,
        ID_BTN_FOCUS_IN100, ID_BTN_FOCUS_IN10,
        ID_BTN_FOCUS_OUT10, ID_BTN_FOCUS_OUT100,
        ID_BTN_FOCUS_GOTO, ID_BTN_FOCUS_STOP,
        ID_BTN_AUTOFOCUS,
        ID_LBL_AF_STATUS,
        ID_LBL_ADC_STATUS,
        ID_LBL_FILTER,
        ID_BTN_CAL_DARK,
        ID_BTN_CAL_FLAT,
        ID_BTN_CAL_CLEAR,
        ID_LBL_CAL_STATUS,
        ID_SLIDER_BLACK,
        ID_SLIDER_WHITE,
        ID_LBL_STRETCH,
        ID_LBL_MOUNT_RA = 1400, ID_LBL_MOUNT_DEC, ID_LBL_MOUNT_TRACK,
        ID_LBL_MOUNT_AZ, ID_LBL_MOUNT_ALT, ID_LBL_MOUNT_HA,
        ID_LBL_MOUNT_CONNECTED,
        ID_BTN_SYNC = 1500,
        ID_BTN_MOUNT_CTRL,
        ID_BTN_CABLES,
        ID_BTN_RIGSETUP,
        ID_BTN_READY,
        ID_BTN_MOUNT_STOP,
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
        
        ID_BTN_RECORD = 1800,
        ID_COMBO_FRAME_TYPE,
        ID_BTN_RGB_ALIGN,       // Phase 23: toggle RGB align
        ID_SLIDER_RGB_MANUAL,   // Phase 23: manual fine-tune ±20px
        ID_BTN_FX,              // Phase 25: Display FX cycle
        ID_BTN_NIGHT,           // Phase 26: Night Vision Mode
        ID_BTN_CHANNEL,         // Phase 38: Single-Channel Display
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
    std::function<void()>     m_onRecordStart;
    std::function<void()>     m_onRecordStop;
    std::function<void(bool)> m_onPlanetLock;
    std::function<void(const std::string&)> m_onGoTo;
    std::function<void()>                   m_onSolveSync;
    std::function<void(bool)>               m_onAutofocus;  // Phase 9
    std::function<void()>                   m_onMountCtrl;  // floating mount controller toggle
    std::function<void()>                   m_onCablesDressed;  // cord-wrap reset
    std::function<void()>                   m_onRigSetup;
    std::function<void()>                   m_onReadinessCheck;
    std::function<void(bool,bool)>          m_onAEToggle;
    std::function<void()>                   m_onRGBAlign;
    std::function<void()>                   m_onFXCycle;
    std::function<void()>                   m_onNightVision;
    std::function<void()>                   m_onCalDark;
    std::function<void(int,int)>            m_onStretch;
    std::function<void()>                   m_onCalFlat;
    std::function<void()>                   m_onCalClear;
    std::function<void()>                   m_onChannelCycle;
    int                                     m_aeState = 0;  // 0=off, 1=advisory, 2=auto
};

} // namespace raduk::gui
