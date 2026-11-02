#pragma once
// ============================================================
// GoToPanel.h — Phase 6: GoTo tab replacing CPWI
//
// A Win32 child window that provides:
//  - Object catalog list (planets + Messier, sorted by altitude)
//  - Search box for quick object lookup
//  - GoTo button → NexStar 'r' command
//  - Plate Solve button → ASTAP → sync to solved position
//  - Slew status display
//  - Alignment point counter
//
// Rendered as a Win32 HWND (GDI) — no D2D needed for this tab.
// Attaches alongside the ControlPanel.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <string>
#include <vector>
#include <functional>
#include <cstdio>
#include <cmath>
#include <mutex>
#include <atomic>

#include "../sky/SkyObject.h"
#include "../sky/ASTAPSolver.h"
#include "../sky/PlateScaleCheck.h"
#include "../sky/PushTo.h"
#include "../sky/MosaicPlan.h"
#include "../sky/MosaicRunner.h"
#include <chrono>
#include "../sky/PointingModel.h"
#include "../core/IMount.h"
#include "../core/FrameSlot.h"
#include "Theme.h"
#include "../core/SkyMath.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace raduk::gui {

class GoToPanel {
public:
    GoToPanel() = default;
    ~GoToPanel() { if (m_hwnd) DestroyWindow(m_hwnd); }

    GoToPanel(const GoToPanel&) = delete;
    GoToPanel& operator=(const GoToPanel&) = delete;

    // ── Setup ─────────────────────────────────────────────────
    void SetMount(raduk::core::IMount* mount) { m_mount = mount; }

    // Observer location for altitude sorting (set via SetLocation from RigConfig)
    void SetLocation(double lat, double lon) {
        m_lat = lat; m_lon = lon;
        RefreshSky();
    }

    // Phase 115: the catalogue is reported from the site, because the
    // Moon's position depends on where you stand by up to a degree.
    // Read fresh each time: the site can arrive after startup.
    void RefreshSky() {
        m_catalog.SetObserver(SiteObserver());
        m_catalog.UpdatePlanets();
    }

    static raduk::sky::LunarObserver SiteObserver() {
        const auto& r = raduk::core::RigConfig::Get();
        raduk::sky::LunarObserver o;
        o.latDeg = r.siteLatDeg;
        o.lonDeg = r.siteLonDeg;
        o.elevM  = r.siteElevationM;
        o.set    = r.SiteIsSet();
        return o;
    }

    // Called when GoTo is issued — main.cpp uses this to auto-load
    // the matching camera profile (e.g. GoTo Jupiter → load Jupiter profile)
    // Phase 6.6: so the pointing correction is visible. printf reaches
    // nothing in a GUI build, which is how the guide-rate warning in
    // CentroidTracker stayed invisible for its whole life.
    void SetLogCallback(std::function<void(const std::string&)> cb)
        { m_onLog = cb; }
    // From the camera, not a literal. Zero until one is connected.
    // Atomic because the solve callbacks read it on a worker thread.
    void SetPixelSizeUm(double um) { m_pixelUm.store(um); }

    void SetProfileAutoLoadCallback(std::function<void(const std::string&)> cb)
        { m_onProfileAutoLoad = cb; }

    // Called when GoTo completes (to trigger plate solve)
    void SetGoToCompleteCallback(std::function<void(const std::string&)> cb)
        { m_onGoToComplete = cb; }

    // Called when sync is performed (notify status bar etc.)
    // Phase 67 step 5: asked before a user-initiated slew. Return
    // false to abort. Lets the cord-wrap check live in main.cpp, where
    // the tracker and the window are, without GoToPanel knowing
    // anything about cables.
    void SetPreSlewCheck(std::function<bool(double,double)> cb) { m_preSlew = cb; }

    // ── Phase 97.5: mosaic execution ───────────────────────────
    //
    // The panel drives the mount; main.cpp owns the recorder, so the
    // two capture edges come back out as callbacks. Supplying them is
    // what ENABLES mosaics: without them StartMosaic refuses rather
    // than slewing eighteen times and recording nothing.
    void SetMosaicCapture(std::function<bool(int ordinal, int total)> begin,
                          std::function<void()> end) {
        m_mosaicBegin = begin;
        m_mosaicEnd   = end;
    }

    void SetSyncCallback(std::function<void(double,double)> cb)
        { m_onSync = cb; }

    // Phase 47b. Every successful solve, with the frame it was of,
    // so the preview can name the pixel under the cursor.
    //
    // Fired from Tick() rather than from the solver callback: the
    // solver runs on a worker and the receiver draws, which is the
    // same rule the rest of this file follows. A solve the panel
    // used for something else still goes out, because the question
    // "what is under the cursor" does not care why the frame was
    // solved.
    void SetWcsCallback(
        std::function<void(const raduk::sky::SolveResult&, int, int)> cb)
        { m_onWcs = cb; }

    // ── Lifecycle ──────────────────────────────────────────────
    bool Create(HWND parent, int x, int y, int w, int h) {
        m_parent = parent;

        WNDCLASSEXW wc = {};
        wc.cbSize       = sizeof(wc);
        wc.lpfnWndProc  = WndProc;
        wc.hInstance    = GetModuleHandle(nullptr);
        wc.hCursor      = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(Theme::NV(Theme::Rgb::PANEL));
        wc.lpszClassName = L"RadukGoToPanel";
        RegisterClassExW(&wc);

        m_hwnd = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            L"RadukGoToPanel", L"GoTo",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX,
            x, y, w, h,
            parent, nullptr, GetModuleHandle(nullptr), this);

        if (!m_hwnd) return false;

        BuildControls(w - 20, h);
        m_catalog.UpdatePlanets();
        PopulateList("");

        ShowWindow(m_hwnd, SW_HIDE); // hidden by default
        printf("[GoToPanel] Created\n");
        return true;
    }

    // Show centered on the primary monitor — called when GOTO button pressed
    void ShowCentered() {
        if (!m_hwnd) return;
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        RECT r; GetWindowRect(m_hwnd, &r);
        int w = r.right - r.left;
        int h = r.bottom - r.top;
        int x = (sw - w) / 2;
        int y = (sh - h) / 2;
        SetWindowPos(m_hwnd, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    }

    HWND Handle() const { return m_hwnd; }
    bool IsVisible() const { return m_hwnd && IsWindowVisible(m_hwnd); }

    void Show()  { if (m_hwnd) ShowWindow(m_hwnd, SW_SHOW); }
    void Hide()  { if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE); }
    void Toggle(){ IsVisible() ? Hide() : Show(); }

    // ── Phase 14: PlannerEntry — public so AIContextBuilder can reference it ──
    struct PlannerEntry {
        std::string name;
        std::string type;
        float       mag;
        float       altNow;     // degrees, current altitude
        float       azDeg;      // degrees, current azimuth
        float       peakAlt;    // max altitude tonight
        int         minsLeft;   // imaging minutes remaining (alt > 30°)
        std::string windowStr;  // e.g. "21:15–02:40"
        double      ra, dec;
    };

    // ── Phase 14 / Phase 50 AI integration ────────────────────
    // Returns the last-built planner list (from most recent Tonight tab visit).
    // If the list is empty (tab never opened), builds it fresh.
    // AIContextBuilder calls this to inject tonight's imaging windows into the
    // AI system prompt so it can reason about urgency and recommend targets.
    const std::vector<PlannerEntry>& GetPlannerData() {
        if (m_plannerItems.empty())
            m_plannerItems = BuildPlannerList();
        return m_plannerItems;
    }

    // Force a fresh rebuild — call before each AI query to ensure data is current.
    const std::vector<PlannerEntry>& RefreshPlannerData() {
        m_catalog.UpdatePlanets();
        m_plannerItems = BuildPlannerList();
        return m_plannerItems;
    }

    // Expose LST publicly for AI session state injection in main.cpp
    double ApproximateLSTPublic() const { return ApproximateLST(); }

    // Phase 13: Survey — called by main.cpp to deliver a frame for plate-solving
    // Wire: window.SetSurveyFrameCallback([&]{ gotoPanel.SurveyFrameReady(data,w,h); });
    void SetSurveyFrameCallback(std::function<void()> cb) { m_onSurveyNeedFrame = cb; }

    // Call from main.cpp when a fresh frame is ready during a survey solve request
    void SurveyProvideFrame(const uint8_t* data, uint32_t w, uint32_t h,
                             float tempC, float seeingScore) {
        if (!m_surveyRunning.load()) return;
        // Build a position label for this survey point
        char label[32];
        snprintf(label, 32, "Survey_%02d", m_surveyIdx);
        // Set a flag so the AutoSolveOnLock completion can signal the thread
        m_surveySolveReady.store(false);
        m_surveyLastSolved = false;
        // Wire a one-shot completion hook via m_onAutoSolveComplete override
        auto prevHook = m_onAutoSolveComplete;
        m_onAutoSolveComplete = [this, prevHook](bool ok, const std::string& msg,
                                                  double ra, const std::string& obj) {
            m_surveyLastSolved = ok;
            m_surveySolveReady.store(true);
            if (prevHook) prevHook(ok, msg, ra, obj);
        };
        AutoSolveOnLock(data, w, h, label, tempC, seeingScore);
    }

    bool IsSurveying() const { return m_surveyRunning.load(); }

    // Phase 50a: double-click in Tonight list → show knowledge panel
    // args: name, altDeg, azDeg, mag
    void SetInfoCallback(std::function<void(const std::string&,
                                            float, float, float)> cb)
        { m_onInfo = cb; }

    // Public GoTo entry point — used by SkyMapWindow click-to-GoTo
    // Fires profile auto-load + mount slew + status updates
    // Fired whenever a slew to a named object is requested, from any
    // route. Hooked here rather than at the call sites because DoGoTo
    // has seven of them -- the sky map, the object info panel, this
    // panel's own two lists, the APEX remote command and the
    // auto-acquire path -- and a sync that covers six of seven is
    // worse than none, since you would learn to trust it.
    void SetTargetChangedCallback(std::function<void(const std::string&)> cb) {
        m_onTargetChanged = std::move(cb);
    }

    void RequestGoTo(double raH, double decDeg, const std::string& name) {
        DoGoTo(raH, decDeg, name);
    }

    // Phase 86: the last target actually slewed to. The AI context was
    // reporting the capture file prefix as both the target and the
    // profile name, with the comment "best available" -- so an
    // assistant asked "what am I pointing at" answered with a filename.
    const std::string& CurrentTarget() const { return m_currentTarget; }

    // ── Phase 30: push-to assistant ────────────────────────────
    bool PushToActive() const { return m_pushToActive.load(); }

    // Begin guiding the observer to a target by eye. Safe to call
    // with no mount: that is the case it exists for.
    void StartPushTo(double raHours, double decDeg, const std::string& name) {
        m_pushToTargetRA  = raHours;
        m_pushToTargetDec = decDeg;
        m_pushToName      = name;
        {
            std::lock_guard<std::mutex> lk(m_pushToMutex);
            m_pushToHint = raduk::sky::PushToHint{};
            m_pushToLine.clear();
        }
        m_pushToLastKickMs = 0;          // solve immediately
        m_pushToActive.store(true);
        if (m_btnPushTo) SetWindowTextW(m_btnPushTo, L"PUSH-TO: ON");
        wchar_t msg[160];
        swprintf(msg, 160, L"Push-to: %hs — solving...", name.c_str());
        SetStatus(msg);
    }

    // Where push-to gets its target. Mirrors the two GOTO buttons
    // in the same order they sit on the panel: a selected object
    // first, then the typed coordinates. Deliberately not a third
    // way of naming a target -- one more place to type RA would be
    // one more place for the two to disagree.
    bool ResolveCurrentTarget(double& raHours, double& decDeg, std::string& name) {
        if (m_listObjects) {
            const int sel = ListView_GetNextItem(m_listObjects, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < (int)m_listItems.size()) {
                const auto* obj = m_listItems[sel];
                raHours = obj->ra;
                decDeg  = obj->dec;
                name    = obj->name;
                return true;
            }
        }
        if (m_editRA && m_editDec) {
            wchar_t raBuf[32] = {}, decBuf[32] = {};
            GetWindowTextW(m_editRA,  raBuf,  32);
            GetWindowTextW(m_editDec, decBuf, 32);
            const double ra  = _wtof(raBuf);
            const double dec = _wtof(decBuf);
            if (ra > 0 || dec != 0) {
                raHours = ra;
                decDeg  = dec;
                name    = "Manual coords";
                return true;
            }
        }
        return false;
    }

    // The button. Uses whatever the GoTo side already resolved as
    // the current target, so push-to is not a second place to type
    // coordinates.
    void TogglePushTo() {
        if (m_pushToActive.load()) { StopPushTo(); return; }

        if (!m_solver.IsAvailable()) {
            SetStatus(L"ASTAP not found - push-to needs a plate solver");
            return;
        }
        if (m_lastFrameData.empty()) {
            SetStatus(L"No frame yet - wait for the camera to stream");
            return;
        }
        double ra = 0.0, dec = 0.0;
        std::string name;
        if (!ResolveCurrentTarget(ra, dec, name)) {
            SetStatus(L"Pick a target first, or type RA/Dec");
            return;
        }
        StartPushTo(ra, dec, name);
    }

    void StopPushTo() {
        if (!m_pushToActive.exchange(false)) return;
        if (m_btnPushTo) SetWindowTextW(m_btnPushTo, L"PUSH-TO: OFF");
        SetStatus(L"Push-to stopped");
    }

    // Search catalog by name (case-insensitive), slew to first match.
    // Used by APEX remote commands and sky map auto-GoTo.
    void RequestGoToByName(const std::string& name) {
        RefreshSky();
        auto results = m_catalog.Search(name.c_str());
        if (results.empty()) {
            printf("[GoToPanel] RequestGoToByName: '%s' not found in catalog\n",
                   name.c_str());
            return;
        }
        const auto* obj = results[0];
        printf("[GoToPanel] RequestGoToByName: '%s' -> RA=%.4fh Dec=%.4f°\n",
               obj->name.c_str(), obj->ra, obj->dec);
        DoGoTo(obj->ra, obj->dec, obj->name);
    }

    // Phase 48: where a named object is NOW. Exact name only, case
    // aside -- RequestGoToByName's substring search would send a recall
    // of "Mars" to whichever brighter object happens to contain it.
    // Planets are recomputed first, because a recall is usually of a
    // planet, and a planet's position from the night it was recorded
    // is a position it has since left.
    bool LookupNow(const std::string& name, double& raH, double& decDeg) {
        if (name.empty()) return false;
        RefreshSky();
        for (const auto& obj : m_catalog.Objects()) {
            if (_stricmp(obj.name.c_str(), name.c_str()) == 0) {
                raH = obj.ra; decDeg = obj.dec;
                return true;
            }
        }
        return false;
    }

    // ── Alignment state ────────────────────────────────────────
    enum class AlignState {
        Unknown,      // just started, haven't checked yet
        Unaligned,    // HC reports not aligned
        Aligning,     // StarSense scan in progress
        Aligned,      // HC reports aligned — GoTo enabled
        PlateSynced   // user has plate-solved and synced — GoTo enabled
    };

    // Phase 48: synced by a plate solve, which enables GoTo without the
    // hand control reporting aligned. Read on its own because the align
    // state above is only refreshed while the panel is open.
    bool IsPlateSynced() const { return m_alignState == AlignState::PlateSynced; }

    bool IsGoToEnabled() const {
        return m_alignState == AlignState::Aligned
            || m_alignState == AlignState::PlateSynced;
    }

    void UpdateAlignmentState() {
        if (!m_mount || !m_mount->IsConnected()) return;

        // Poll IsAligned() — runs on background poll thread cache, instant
        bool aligned = m_mount->IsAligned();

        AlignState prev = m_alignState;

        if (aligned) {
            m_alignState = AlignState::Aligned;
        } else if (m_alignState == AlignState::PlateSynced) {
            // Keep PlateSynced even if HC reports unaligned
            // (user plate-solved their way to alignment)
        } else if (m_alignState == AlignState::Aligning) {
            // Still waiting for StarSense
        } else {
            m_alignState = AlignState::Unaligned;
        }

        if (m_alignState != prev) {
            UpdateAlignmentUI();
            // Enable/disable GoTo button
            if (m_btnGoTo)
                EnableWindow(m_btnGoTo, IsGoToEnabled() ? TRUE : FALSE);
            if (m_btnGoToCoord)
                EnableWindow(m_btnGoToCoord, IsGoToEnabled() ? TRUE : FALSE);
        }
    }

    void UpdateAlignmentUI() {
        if (!m_lblAlignState) return;
        switch (m_alignState) {
            case AlignState::Unknown:
                SetWindowTextW(m_lblAlignState,
                    L"⏳ Checking alignment...");
                break;
            case AlignState::Unaligned:
                SetWindowTextW(m_lblAlignState,
                    L"NOT ALIGNED  |  Point at Jupiter/Saturn/bright star "
                    L"\u2192 SOLVE TO ALIGN  |  or press ALIGN on HC");
                break;
            case AlignState::Aligning:
                SetWindowTextW(m_lblAlignState,
                    L"🔄 StarSense aligning... (~3 min)");
                break;
            case AlignState::Aligned:
                SetWindowTextW(m_lblAlignState,
                    L"✅ ALIGNED (StarSense) — GoTo enabled");
                break;
            case AlignState::PlateSynced:
                SetWindowTextW(m_lblAlignState,
                    L"✅ ALIGNED (plate solve) — GoTo enabled");
                break;
        }
    }

    // Slew-completion detection must run even while the panel is hidden —
    // the sky-map-click GoTo flow relies on m_onGoToComplete to arm the
    // post-slew acquisition sequence, and the panel is hidden by default.
    // main.cpp calls this every readout tick when the panel is not visible.
    void TickSlewStatus() { UpdateSlewStatus(); }

    // Call every ~2s to refresh planet positions and list altitude
    void Tick() {
        RefreshSky();
        UpdateSlewStatus();
        UpdateAlignmentState();

        // Apply any sync results posted from the solver worker thread
        // All Win32 UI calls must happen here on the main thread
        if (m_pendingSyncSuccess.exchange(false)) {
            m_alignState = AlignState::PlateSynced;
            UpdateAlignmentUI();
            if (m_btnGoTo)      EnableWindow(m_btnGoTo,      TRUE);
            if (m_btnGoToCoord) EnableWindow(m_btnGoToCoord, TRUE);
            wchar_t alnMsg[32];
            swprintf(alnMsg, 32, L"Alignment points: %d", m_alignPoints);
            if (m_lblAlign) SetWindowTextW(m_lblAlign, alnMsg);
        }

        PushToTick();
        WcsTick();
        MosaicTick();
    }

    // ── Phase 97.5: the mosaic loop ────────────────────────────
    //
    // Driven from Tick() like everything else on this panel, so it
    // runs on the UI thread and needs no synchronisation with the
    // slew status it reads.
    //
    // The runner itself is a pure state machine in
    // src/sky/MosaicRunner.h and is fully tested there. This is only
    // the plumbing: a clock, the target's live position, the mount's
    // own slewing answer, and the two capture edges.
    void MosaicTick() {
        if (!m_mosaic.Running()) {
            // Report the ending once, then stop talking about it.
            if (m_mosaicWasRunning) {
                m_mosaicWasRunning = false;
                if (m_onLog) {
                    char b[200];
                    const char* how =
                        m_mosaic.State() == raduk::sky::MosaicRunner::Phase::Complete
                            ? "complete"
                        : m_mosaic.State() == raduk::sky::MosaicRunner::Phase::Aborted
                            ? "stopped" : "FAILED";
                    snprintf(b, sizeof(b), "Mosaic %s: %d of %d panels%s%s",
                             how, m_mosaic.Captured(), m_mosaic.Total(),
                             m_mosaic.Reason().empty() ? "" : " - ",
                             m_mosaic.Reason().c_str());
                    m_onLog(b);
                }
                SetStatus(L"Mosaic finished");
            }
            return;
        }
        if (!m_mount) { m_mosaic.Abort("mount went away"); return; }

        // The target's position NOW, not where it was when the mosaic
        // started. This is the whole design of the runner: with it the
        // last panel is aimed at where the Moon is, and the 272 arcsec
        // of cumulative drift phase 97 measured simply does not
        // accumulate.
        double ra = m_mosaicRa, dec = m_mosaicDec;
        const auto live = raduk::sky::PlanetApparentSizeAt(
                              m_mosaicTarget, raduk::sky::CurrentJD());
        if (live.valid) LiveTargetRaDec(m_mosaicTarget, ra, dec);

        const double now = NowSeconds();
        const auto a = m_mosaic.Tick(now, ra, dec, m_mount->IsSlewing());

        using A = raduk::sky::MosaicRunner::Action;
        switch (a.kind) {
        case A::Slew:
            // The cord-wrap veto applies to every slew a mosaic makes,
            // not only the first. Eighteen unattended slews is exactly
            // the situation phase 67's guard was written for, and a
            // mosaic that ignored it would be the one caller allowed
            // to wrap the cord.
            if (m_preSlew && !m_preSlew(a.raHours, a.decDeg)) {
                m_mosaic.Abort("cord wrap");
                break;
            }
            m_mount->GoToRADec(a.raHours, a.decDeg);
            break;
        case A::BeginCapture: {
            if (!m_mosaicBegin || !m_mosaicBegin(a.ordinal, m_mosaic.Total())) {
                m_mosaic.Abort("could not start recording");
                break;
            }
            wchar_t w[96];
            swprintf(w, 96, L"Mosaic panel %d of %d",
                     a.ordinal, m_mosaic.Total());
            SetStatus(w);
            break;
        }
        case A::EndCapture:
            if (m_mosaicEnd) m_mosaicEnd();
            break;
        default: break;
        }
    }

    // Start a mosaic of the named target. Refuses rather than
    // half-starting: every reason it can decline is reported, because
    // a mosaic that silently does not begin looks exactly like one
    // that is about to.
    bool StartMosaic(const std::string& requested) {
        // A place on the Moon has no disc of its own; the mosaic is of
        // the Moon it is on.
        const std::string name = IsLunarFeature(requested) ? "Moon" : requested;
        auto decline = [&](const char* why) {
            if (m_onLog) {
                char b[200];
                snprintf(b, sizeof(b), "Mosaic not started: %s", why);
                m_onLog(b);
            }
            return false;
        };
        if (m_mosaic.Running())        return decline("one is already running");
        if (!m_mount || !m_mount->IsConnected()) return decline("no mount");
        if (!m_mosaicBegin || !m_mosaicEnd)
            return decline("no recorder wired up");
        if (m_lastFrameW == 0 || m_lastFrameH == 0)
            return decline("no frame yet, so no field of view");

        const auto sz = raduk::sky::PlanetApparentSizeAt(
                            name, raduk::sky::CurrentJD());
        if (!sz.valid) return decline("target has no measurable disc");

        raduk::sky::MosaicRunner::Config c;
        c.fov = raduk::sky::FrameFov((int)m_lastFrameW, (int)m_lastFrameH,
                    raduk::core::RigConfig::Get().plateScaleArcsecPx);
        c.targetArcmin = sz.framingArcsec / 60.0;
        c.plan = raduk::sky::PlanMosaic(c.targetArcmin, c.fov, MOSAIC_OVERLAP);
        c.captureSec = MOSAIC_CAPTURE_SEC;
        c.settleSec  = MOSAIC_SETTLE_SEC;
        m_mosaic.Configure(c);
        if (m_mosaic.Total() <= 0) return decline("nothing to capture");

        m_mosaicTarget = name;
        LiveTargetRaDec(name, m_mosaicRa, m_mosaicDec);
        m_mosaic.Start(NowSeconds());
        if (m_mosaic.State() == raduk::sky::MosaicRunner::Phase::Failed)
            return decline(m_mosaic.Reason().c_str());

        m_mosaicWasRunning = true;
        // The framing report, so the operator sees what they are
        // committing to at the moment they commit to it.
        ReportFraming(name);
        if (m_onLog) {
            char b[160];
            snprintf(b, sizeof(b), "Mosaic started: %d panels, at least"
                     " %.1f minutes.", m_mosaic.Total(),
                     m_mosaic.SecondsRemaining(NowSeconds()) / 60.0);
            m_onLog(b);
        }
        return true;
    }

    void AbortMosaic() {
        if (m_mosaic.Running()) m_mosaic.Abort("stopped by the operator");
    }
    bool MosaicRunning() const { return m_mosaic.Running(); }

private:
    // A monotonic second count. steady_clock, not the wall clock: a
    // mosaic is a stopwatch and an NTP correction mid-run must not
    // make a settle last an hour or end instantly.
    static double NowSeconds() {
        using namespace std::chrono;
        return duration<double>(
            steady_clock::now().time_since_epoch()).count();
    }

    // Where the target is now. The catalogue is updated every Tick by
    // UpdatePlanets, so this reads the same position everything else
    // on the panel does.
    void LiveTargetRaDec(const std::string& name, double& ra, double& dec) {
        for (const auto& o : m_catalog.Objects())
            if (o.name == name) { ra = o.ra; dec = o.dec; return; }
    }

public:

    // Phase 47b: hand the last solve to whoever draws the cursor.
    void WcsTick() {
        if (!m_wcsFresh.exchange(false)) return;
        if (!m_onWcs) return;
        // An unsolved result is the invalidation; the receiver treats
        // it the same way it treats a solve that failed.
        raduk::sky::SolveResult r;
        int w = 0, h = 0;
        {
            std::lock_guard<std::mutex> lk(m_wcsMutex);
            r = m_wcsResult;
            w = m_wcsW;
            h = m_wcsH;
        }
        m_onWcs(r, w, h);
    }

    // Phase 30. Two jobs, both on the main thread: show whatever
    // the last solve concluded, and start the next one.
    void PushToTick() {
        if (m_pushToFresh.exchange(false)) {
            std::string line;
            bool centred = false;
            {
                std::lock_guard<std::mutex> lk(m_pushToMutex);
                line    = m_pushToLine;
                centred = m_pushToHint.centred;
            }
            wchar_t wbuf[256];
            MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, wbuf, 256);
            SetStatus(wbuf);

            // Centred is the end of the job, not a reason to keep
            // solving. Leaving it running would burn a solve every
            // half minute telling somebody what they can already
            // see, and would eventually drift back to "move" as the
            // sky turns, which reads like a fault.
            if (centred) StopPushTo();
        }

        if (!m_pushToActive.load())      return;
        if (m_solver.IsSolving())        return;
        if (m_lastFrameData.empty())     return;

        const uint64_t now = GetTickCount64();
        if (now - m_pushToLastKickMs < PUSHTO_MIN_GAP_MS) return;
        m_pushToLastKickMs = now;

        // Hint the solver with the TARGET rather than the mount's
        // idea of where it is. In the case this feature exists for
        // the mount is wrong or absent, so its position is the one
        // number not worth trusting; the target is where the user
        // is trying to get, and after the first nudge it is close.
        m_solver.SetHint(m_pushToTargetRA, m_pushToTargetDec);

        const double tgtRA  = m_pushToTargetRA;
        const double tgtDec = m_pushToTargetDec;
        const uint32_t fw = m_lastFrameW, fh = m_lastFrameH;
        const int gen = WcsGen();

        m_solver.SolveAsync(m_lastFrameData.data(), fw, fh,
            [this, tgtRA, tgtDec, fw, fh, gen](raduk::sky::SolveResult result) {
                // WORKER THREAD. No Win32 UI here; PushToTick picks
                // this up on the next pass.
                std::string line;
                raduk::sky::PushToHint hint;

                if (result.solved) {
                    ReportPlateScale(result, fw);
                    // The tolerance rule lives in PushTo.h so it can
                    // be gated; see tests/push_to.cpp.
                    const double tol =
                        raduk::sky::CentredToleranceDeg(result.fovW);
                    hint = raduk::sky::ComputePushTo(result.ra, result.dec,
                                                     tgtRA, tgtDec, tol);
                    line = "Push-to: " + raduk::sky::FormatPushTo(hint);
                } else {
                    // A failed solve is normal while the target is
                    // far off and the field holds nothing known. Say
                    // so and keep going rather than stopping.
                    line = "Push-to: no solve (" + result.error +
                           ") - try a wider view or a brighter field";
                }

                {
                    std::lock_guard<std::mutex> lk(m_pushToMutex);
                    m_pushToHint = hint;
                    m_pushToLine = line;
                }
                m_pushToFresh.store(true);
                PublishWcs(result, (int)fw, (int)fh, gen);
            });
    }

    // A solve knows the true field width, so it knows the true plate
    // scale. Called from all four solve callbacks rather than from
    // PublishWcs, which AutoSolveOnLock does not use and which drops a
    // solve that a slew overtook: the frame moved, the scale did not.
    void ReportPlateScale(const raduk::sky::SolveResult& r, uint32_t frameW) {
        const std::string line = raduk::sky::FormatPlateScale(
            raduk::sky::CheckPlateScale(
                r, frameW, raduk::core::RigConfig::Get().plateScaleArcsecPx,
                m_pixelUm.load()));
        if (!line.empty() && m_onLog) m_onLog(line);
    }

    // Called from whichever worker thread finished a solve. Stores
    // and flags; Tick() delivers it. Deliberately takes the frame
    // size alongside: a solve that finishes after a ROI change
    // describes the frame it was given, not the one on screen now.
    // A slew means the solved frame is no longer the frame. Sent as
    // an unsolved result, because "forget it" and "here is a new
    // one" are the same event to the receiver and splitting them
    // into two callbacks is two chances to wire only one up.
    //
    // Lives here rather than at main.cpp's call sites so the panel's
    // own GOTO buttons are covered along with everything that drives
    // them from outside.
    void InvalidateWcs() {
        // Bump first. A solve already in flight captured the old
        // generation and will now be dropped when it lands, which is
        // the point: a solve of a frame from before the slew is not a
        // description of where the scope is pointing now, and it would
        // arrive looking exactly like a fresh one.
        //
        // Same idea as main.cpp's jogGeneration guarding the tracker
        // resume timer, and for the same reason: the thing that
        // finishes last is not the thing that is true.
        m_wcsGen.fetch_add(1);
        {
            std::lock_guard<std::mutex> lk(m_wcsMutex);
            m_wcsResult = raduk::sky::SolveResult{};
            m_wcsW = m_wcsH = 0;
        }
        m_wcsFresh.store(true);
    }

    // The generation to stamp a solve with, read when it is SUBMITTED.
    int WcsGen() const { return m_wcsGen.load(); }

    void PublishWcs(const raduk::sky::SolveResult& r, int w, int h, int gen) {
        if (!r.solved) return;
        if (gen != m_wcsGen.load()) return;      // the mount moved meanwhile
        {
            std::lock_guard<std::mutex> lk(m_wcsMutex);
            m_wcsResult = r;
            m_wcsW = w;
            m_wcsH = h;
        }
        m_wcsFresh.store(true);
    }

    // Submit current frame for plate solving (called every render tick when visible)
    void SubmitFrame(const uint8_t* data, uint32_t w, uint32_t h) {
        m_lastFrameData.assign(data, data + w * h);
        m_lastFrameW = w;
        m_lastFrameH = h;
    }

    // ── Phase 6.6: Auto solve+sync on Stage 3 lock ─────────────
    // Called from main.cpp ROILock callback. Runs ASTAP in a
    // background thread — never blocks the capture pipeline.
    // On success: syncs mount, records to pointing_model.csv,
    // fires m_onAutoSolveComplete with the result summary.
    void AutoSolveOnLock(const uint8_t* frameData, uint32_t w, uint32_t h,
                         const std::string& objectName,
                         float camTempC, float seeingScore) {
        if (!m_solver.IsAvailable()) {
            printf("[GoToPanel] AutoSolveOnLock: ASTAP not available — skipping\n");
            return;
        }
        if (m_solver.IsSolving() || m_autoSolving.load()) {
            printf("[GoToPanel] AutoSolveOnLock: solve already running — skipping\n");
            return;
        }

        // Hint solver with current mount position (tight hint — we just locked)
        double hintRA = 0.0, hintDec = 0.0;
        bool hasHint = false;
        if (m_mount && m_mount->IsConnected()) {
            if (m_mount->GetRADec(hintRA, hintDec)) {
                m_solver.SetHint(hintRA, hintDec);
                m_solver.SetSearchRadius(1.0f);  // tight: we know we're close
                hasHint = true;
            }
        }

        printf("[GoToPanel] AutoSolveOnLock: solving for '%s' (hint=%s)\n",
               objectName.c_str(), hasHint ? "yes" : "no");

        m_autoSolving.store(true);
        std::string objCopy = objectName;

        // Snapshot mount Az/Alt for pointing model (may be slightly stale — that's fine)
        double az = 0.0, alt = 0.0;
        if (m_mount && m_mount->IsConnected())
            m_mount->GetAltAz(alt, az);

        m_solver.SolveAsync(frameData, w, h,
            [this, objCopy, hintRA, hintDec, az, alt, w,
             camTempC, seeingScore](raduk::sky::SolveResult result) {
                m_autoSolving.store(false);

                if (!result.solved) {
                    printf("[GoToPanel] AutoSolveOnLock: solve failed — %s\n",
                           result.error.c_str());
                    if (m_onAutoSolveComplete)
                        m_onAutoSolveComplete(false, objCopy, 0.0, "");
                    return;
                }

                // Compute pointing error
                double errArcsec = raduk::sky::AngularSeparationArcsec(
                    hintRA, hintDec, result.ra, result.dec);

                printf("[GoToPanel] AutoSolveOnLock: %s  RA=%.4fh Dec=%.4f°  "
                       "error=%.1f\"\n",
                       objCopy.c_str(), result.ra, result.dec, errArcsec);

                // Sync mount (serial mutex protects this — safe from worker thread)
                if (m_mount && m_mount->IsConnected()) {
                    m_mount->SyncRADec(result.ra, result.dec);
                    m_alignPoints++;
                }

                ReportPlateScale(result, w);

                // Record to pointing model
                m_pointingModel.Record(
                    hintRA, hintDec,
                    result.ra, result.dec,
                    az, alt,
                    camTempC, seeingScore,
                    objCopy);

                // Build status string for sidebar flash
                char statusBuf[64];
                sprintf_s(statusBuf, "SYNC \u2713  %.0f\"  (#%d)",
                          errArcsec, m_alignPoints);

                if (m_onAutoSolveComplete)
                    m_onAutoSolveComplete(true, objCopy, errArcsec, statusBuf);
            });
    }

    // Callback: fires on auto-solve completion (from worker thread — no Win32 UI)
    // Args: success, objectName, errorArcsec, statusMessage
    // Phase 102. The control panel's POINT SOLVE -> SYNC button was
    // wired to a callback nobody ever set, so pressing it did nothing
    // at all -- not even the "Plate solving..." status, because that
    // sits inside the null check. This is the same solve the SOLVE TO
    // ALIGN button here runs; DoPlateSolveAndSync guards its own
    // preconditions (no frame yet, ASTAP missing, solve already
    // running) and reports each one through SetStatus, so a second
    // entry point needs no checks of its own.
    void PlateSolveAndSync() { DoPlateSolveAndSync(); }

    void SetAutoSolveCallback(
        std::function<void(bool, const std::string&, double, const std::string&)> cb) {
        m_onAutoSolveComplete = cb;
    }

    // Number of auto-syncs recorded this session
    int AutoSyncCount() const { return m_alignPoints; }

    // Last pointing error (arcseconds) from PointingModel
    double LastPointingError() const { return m_pointingModel.LastError(); }

    // Total historical entries in pointing model
    int PointingModelSize() const { return m_pointingModel.EntryCount(); }

    // Apply pointing model correction to a GoTo target
    // Returns true if a correction was available and applied
    bool ApplyPointingCorrection(double az_deg, double alt_deg,
                                  double& ra_h, double& dec_deg) const {
        double dRA = 0.0, dDec = 0.0;
        if (!m_pointingModel.GetCorrection(az_deg, alt_deg, dRA, dDec))
            return false;
        ra_h    += dRA;
        dec_deg += dDec;
        printf("[GoToPanel] PointingCorrection: dRA=%.4fh dDec=%.4f° "
               "(from %d nearby pts)\n",
               dRA, dDec,
               m_pointingModel.NearbyCount(az_deg, alt_deg));
        return true;
    }

private:
    // ── Control IDs ───────────────────────────────────────────
    enum {
        ID_EDIT_SEARCH    = 2001,
        ID_LIST_OBJECTS   = 2002,
        ID_BTN_GOTO       = 2003,
        ID_BTN_SOLVE      = 2004,
        ID_BTN_SYNC       = 2005,
        ID_BTN_STOP       = 2006,
        ID_LBL_STATUS     = 2007,
        ID_LBL_ALIGN      = 2008,
        ID_EDIT_RA        = 2009,
        ID_EDIT_DEC       = 2010,
        ID_BTN_GOTO_COORD = 2011,
        ID_LBL_ALIGN_STATE = 2012,
        ID_BTN_SOLVE_ALIGN = 2013,
        ID_BTN_PUSHTO      = 2014,  // Phase 30: push-to assistant
        ID_TAB_MAIN        = 2020,  // Phase 14: tab control
        ID_LIST_TONIGHT    = 2021,  // Phase 14: tonight's planner list
        ID_BTN_GOTO_TONIGHT= 2022,  // Phase 14: GoTo selected from planner
        ID_LBL_TONIGHT_HDR = 2023,  // Phase 14: header label
        ID_BTN_SURVEY      = 2030,  // Phase 13: start/stop sky survey
        ID_LBL_SURVEY      = 2031,  // Phase 13: survey progress label
    };

    // ── Phase 14: Session Planner ──────────────────────────────

    // Astronomical dusk JD: first time tonight the sun drops BELOW -18°.
    // If already below -18° (i.e. it's nighttime), returns nowJD immediately.
    double AstroDuskJD() const {
        using namespace raduk::sky;
        double jd = CurrentJD();
        // Check if already dark
        {
            double sunRA, sunDec;
            SunRADec(jd, sunRA, sunDec);
            double lst = LSTFromJD(jd);
            double alt = SkyObjectCatalog::CalcAlt(sunRA, sunDec, lst, m_lat);
            if (alt <= -18.0) return jd; // already astronomical night
        }
        // Scan forward up to 24h for sun to drop below -18°
        for (int i = 1; i <= 1440; i++) {
            double testJD = jd + i / 1440.0;
            double sunRA, sunDec;
            SunRADec(testJD, sunRA, sunDec);
            double lst = LSTFromJD(testJD);
            double alt = SkyObjectCatalog::CalcAlt(sunRA, sunDec, lst, m_lat);
            if (alt <= -18.0) return testJD;
        }
        return jd + 0.5; // fallback (polar summer — no true night)
    }

    // Astronomical dawn JD: first time the sun rises ABOVE -18° after dusk.
    double AstroDawnJD(double duskJD) const {
        using namespace raduk::sky;
        // Scan forward from dusk up to 16 hours
        for (int i = 1; i <= 960; i++) {
            double testJD = duskJD + i / 1440.0;
            double sunRA, sunDec;
            SunRADec(testJD, sunRA, sunDec);
            double lst = LSTFromJD(testJD);
            double alt = SkyObjectCatalog::CalcAlt(sunRA, sunDec, lst, m_lat);
            if (alt > -18.0) return testJD;
        }
        return duskJD + 0.55; // fallback ~8h
    }

    double LSTFromJD(double jd) const {
        using namespace raduk::sky;
        double T = JD_to_T(jd);
        double gmst = 280.46061837 + 360.98564736629 * (jd - 2451545.0)
                    + 0.000387933 * T * T;
        return NormDeg(gmst + m_lon) / 15.0;
    }

    std::vector<PlannerEntry> BuildPlannerList() {
        using namespace raduk::sky;
        double nowJD  = CurrentJD();
        double duskJD = AstroDuskJD();
        double dawnJD = AstroDawnJD(duskJD);

        // Scan starts at the later of now or astronomical dusk
        double scanStart = (nowJD > duskJD) ? nowJD : duskJD;

        m_catalog.UpdatePlanets();
        const auto& objects = m_catalog.Objects();

        std::vector<PlannerEntry> entries;
        entries.reserve(objects.size());

        for (const auto& obj : objects) {
            // Skip very faint objects and Sun (daytime only)
            if (obj.magnitude > 9.5f) continue;
            if (obj.type == "Sun") continue;

            PlannerEntry e;
            e.name = obj.name;
            e.type = obj.type;
            e.mag  = obj.magnitude;
            e.ra   = obj.ra;
            e.dec  = obj.dec;

            // Current altitude and azimuth (always from right now, even if daytime)
            double lstNow = LSTFromJD(nowJD);
            e.altNow = (float)SkyObjectCatalog::CalcAlt(obj.ra, obj.dec, lstNow, m_lat);
            // Compute azimuth
            {
                using namespace raduk::sky;
                double ha  = Rad((lstNow - obj.ra) * 15.0);
                double dec = Rad(obj.dec);
                double lat = Rad(m_lat);
                double sinAlt = sin(lat)*sin(dec) + cos(lat)*cos(dec)*cos(ha);
                double cosAlt = sqrt(1.0 - sinAlt*sinAlt);
                double az = 0.0;
                if (cosAlt > 1e-6) {
                    double cosAz = (sin(dec) - sin(lat)*sinAlt) / (cos(lat)*cosAlt);
                    cosAz = cosAz < -1.0 ? -1.0 : (cosAz > 1.0 ? 1.0 : cosAz);
                    az = Deg(acos(cosAz));
                    if (sin(ha) > 0) az = 360.0 - az;
                }
                e.azDeg = (float)az;
            }

            // Scan from dusk (or now if already dark) to dawn in 10-min steps
            int    minsAbove  = 0;
            float  peakAlt    = -90.0f;
            double winStartJD = 0, winEndJD = 0;
            bool   inWindow   = false;

            double stepJD = 10.0 / 1440.0; // 10 minutes
            // Peak scan covers full dusk→dawn regardless of current time
            for (double t = duskJD; t < dawnJD; t += stepJD) {
                double lst = LSTFromJD(t);
                double alt = SkyObjectCatalog::CalcAlt(obj.ra, obj.dec, lst, m_lat);
                if ((float)alt > peakAlt) peakAlt = (float)alt;
            }
            // Remaining time scan starts from now (or dusk if not yet dark)
            for (double t = scanStart; t < dawnJD; t += stepJD) {
                double lst = LSTFromJD(t);
                double alt = SkyObjectCatalog::CalcAlt(obj.ra, obj.dec, lst, m_lat);
                if (alt >= 30.0) {
                    minsAbove += 10;
                    if (!inWindow) { winStartJD = t; inWindow = true; }
                    winEndJD = t + stepJD;
                }
            }
            e.peakAlt  = peakAlt;
            e.minsLeft = minsAbove;

            // Format imaging window as local time strings (UTC-7)
            if (minsAbove > 0) {
                auto jdToLocal = [](double jd, wchar_t* buf, int bufLen) {
                    double dayFrac = fmod(jd + 0.5 - 7.0/24.0, 1.0);
                    if (dayFrac < 0) dayFrac += 1.0;
                    int h = (int)(dayFrac * 24) % 24;
                    int m = (int)(dayFrac * 1440) % 60;
                    swprintf(buf, bufLen, L"%02d:%02d", h, m);
                };
                wchar_t s1[8], s2[8];
                jdToLocal(winStartJD, s1, 8);
                jdToLocal(winEndJD,   s2, 8);
                wchar_t win[20]; swprintf(win, 20, L"%s\u2013%s", s1, s2);
                char winA[24] = {};
                WideCharToMultiByte(CP_UTF8, 0, win, -1, winA, 24, nullptr, nullptr);
                e.windowStr = winA;
            } else {
                e.windowStr = "—";
            }

            entries.push_back(e);
        }

        // Sort: objects with time remaining first (most minutes first),
        // then planets always at top within their group
        std::sort(entries.begin(), entries.end(),
            [](const PlannerEntry& a, const PlannerEntry& b) {
                bool aP = (a.type == "Planet" || a.type == "Moon");
                bool bP = (b.type == "Planet" || b.type == "Moon");
                if (aP != bP) return aP > bP; // planets first
                return a.minsLeft > b.minsLeft;
            });

        return entries;
    }

    void PopulateTonightList() {
        if (!m_listTonight) return;
        ListView_DeleteAllItems(m_listTonight);
        m_plannerItems.clear();
        m_plannerItems = BuildPlannerList();

        for (int i = 0; i < (int)m_plannerItems.size(); i++) {
            const auto& e = m_plannerItems[i];

            // Col 0: name
            LVITEMW lvi = {};
            lvi.mask    = LVIF_TEXT;
            lvi.iItem   = i;
            lvi.iSubItem = 0;
            wchar_t wname[64];
            MultiByteToWideChar(CP_ACP, 0, e.name.c_str(), -1, wname, 64);
            lvi.pszText = wname;
            SendMessageW(m_listTonight, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

            // Col 1: Alt now
            wchar_t alt[16];
            if (e.altNow < -5.0f) swprintf(alt, 16, L"below");
            else                   swprintf(alt, 16, L"%d°", (int)e.altNow);
            LVITEMW sub = {}; sub.mask = LVIF_TEXT; sub.iItem = i;
            sub.iSubItem = 1; sub.pszText = alt;
            SendMessageW(m_listTonight, LVM_SETITEMW, 0, (LPARAM)&sub);

            // Col 2: Best window
            wchar_t win[24];
            MultiByteToWideChar(CP_UTF8, 0, e.windowStr.c_str(), -1, win, 24);
            sub.iSubItem = 2; sub.pszText = win;
            SendMessageW(m_listTonight, LVM_SETITEMW, 0, (LPARAM)&sub);

            // Col 3: Mins left
            wchar_t mins[12];
            if (e.minsLeft == 0) swprintf(mins, 12, L"—");
            else                  swprintf(mins, 12, L"%dh%02d", e.minsLeft/60, e.minsLeft%60);
            sub.iSubItem = 3; sub.pszText = mins;
            SendMessageW(m_listTonight, LVM_SETITEMW, 0, (LPARAM)&sub);

            // Col 4: Peak alt
            wchar_t peak[12]; swprintf(peak, 12, L"%d°", (int)e.peakAlt);
            sub.iSubItem = 4; sub.pszText = peak;
            SendMessageW(m_listTonight, LVM_SETITEMW, 0, (LPARAM)&sub);

            // Col 5: Mag
            wchar_t magW[10]; swprintf(magW, 10, L"%.1f", e.mag);
            sub.iSubItem = 5; sub.pszText = magW;
            SendMessageW(m_listTonight, LVM_SETITEMW, 0, (LPARAM)&sub);
        }
    }

    // ── P3.3: one place that decides where everything goes ────
    //
    // This panel had WS_SIZEBOX and no WM_SIZE handler, so it could be
    // dragged larger and every control stayed exactly where it was:
    // at 1000x1000 the whole interface sat in the left half of an
    // otherwise empty window. Being resizable into a broken state is
    // worse than not being resizable.
    //
    // The layout was already parameterised -- BuildControls takes a
    // width and a height and walks a single vertical column -- it was
    // simply only ever called once. So the arithmetic moves here and
    // BuildControls calls it, which means creation and resize cannot
    // drift apart: there is one copy and both paths use it.
    //
    // Everything stretches to the width. The object list takes
    // whatever height is left over, because that is where the content
    // is and the rest of the stack is fixed-height furniture.
    void LayoutControls(int w, int h) {
        if (!m_hwnd || w <= 0 || h <= 0) return;

        const int x  = 8;
        const int cw = w - 16;                  // the column's width
        if (cw < 120) return;                   // too narrow to mean anything

        // Row heights with room for descenders. m_fontPx + 3 was
        // enough at 12 px and clipped the bottom off "Alignment
        // points: 0" at 22, which is the sort of thing that only shows
        // up once the font actually starts moving.
        auto rowH = [](int px) { return (px * 3) / 2; };
        auto btnH = [](int px) { return px * 2; };

        // The stack has a natural minimum height, and taller rows made
        // it taller: at 464x661 the START SURVEY button went 29 px off
        // the bottom. Rather than refuse to be that small -- the saved
        // workspace already has it at 487x711 -- step the type down
        // until the fixed furniture fits. Twelve is the floor, below
        // which this stops being readable at all and the window is
        // simply too short for its contents.
        int px = h / 46;
        if (px < 12) px = 12;
        if (px > 24) px = 24;
        while (px > 12 && FixedStackHeight(px) > h) px--;
        ApplyFontPx(px);

        const int SH = rowH(m_fontPx);          // a label row
        const int BH = btnH(m_fontPx);          // a button
        const int TABH = BH + 4;

        auto place = [](HWND c, int px, int py, int pw, int ph) {
            if (c) SetWindowPos(c, nullptr, px, py, pw, ph,
                                SWP_NOZORDER | SWP_NOACTIVATE);
        };

        // ── Tab 0: GoTo ──
        int y = 4;
        place(m_tabCtrl, x, y, cw, TABH);
        y += TABH + 4;

        const int tabTop = y;

        place(m_hdrAlign, x, y, cw, SH);              y += SH + 2;
        place(m_lblAlignState, x, y, cw, SH * 2);     y += SH * 2 + 2;
        place(m_btnSolveAlign, x, y, cw, BH);         y += BH + 6;
        place(m_hdrCatalog, x, y, cw, SH);            y += SH + 4;

        const int searchLabW = m_fontPx * 4;
        place(m_lblSearch, x, y, searchLabW, SH);
        place(m_editSearch, x + searchLabW + 4, y - 2,
              cw - searchLabW - 4, BH);
        y += BH + 4;

        // Everything from here down is fixed height. Measure it, then
        // give the list the remainder -- a taller window should mean
        // more objects visible, not more empty space under the last
        // button.
        const int below = BH + 4 + 8            // GOTO SELECTED
                        + SH + 4                // MANUAL COORDS
                        + BH + 4                // RA / DEC row
                        + BH + 8                // GOTO COORDS
                        + SH + 4                // PLATE SOLVE
                        + BH + 4 + 8            // SOLVE / SYNC
                        + SH + 4                // alignment points
                        + BH + 4                // STOP SLEW
                        + SH * 2 + 6            // status
                        + SH + 4                // SKY SURVEY
                        + SH * 2 + 4            // survey state
                        + BH + 8;               // START SURVEY
        int listH = h - y - below;
        if (listH < m_fontPx * 6) listH = m_fontPx * 6;
        place(m_listObjects, x, y, cw, listH);
        y += listH + 6;

        place(m_btnGoTo, x, y, cw, BH + 4);           y += BH + 4 + 8;
        place(m_hdrManual, x, y, cw, SH);             y += SH + 4;

        const int coordLabW = m_fontPx * 4 + 6;
        const int halfW     = (cw - 6) / 2;
        place(m_lblRA,   x, y, coordLabW, SH);
        place(m_editRA,  x + coordLabW, y - 2, halfW - coordLabW, BH);
        place(m_lblDec,  x + halfW + 6, y, coordLabW, SH);
        place(m_editDec, x + halfW + 6 + coordLabW, y - 2,
              halfW - coordLabW, BH);
        y += BH + 4;

        place(m_btnGoToCoord, x, y, cw, BH);          y += BH + 8;
        place(m_hdrSolve, x, y, cw, SH);              y += SH + 4;

        const int hw = (cw - 4) / 2;
        place(m_btnSolve, x,          y, hw, BH + 4);
        place(m_btnSync,  x + hw + 4, y, hw, BH + 4);
        y += BH + 4 + 8;
        place(m_btnPushTo, x, y, cw, BH + 4);
        y += BH + 4 + 8;

        place(m_lblAlign, x, y, cw, SH);              y += SH + 4;

        // The stop button. Checked explicitly below, because a control
        // that moves off the bottom of its own window is the one defect
        // in this panel that would matter at the telescope.
        place(m_btnStop, x, y, cw, BH);               y += BH + 4;
        place(m_lblStatus, x, y, cw, SH * 2);         y += SH * 2 + 6;

        place(m_hdrSurvey, x, y, cw, SH);             y += SH + 4;
        place(m_lblSurvey, x, y, cw, SH * 2);         y += SH * 2 + 4;
        place(m_btnSurvey, x, y, cw, BH);

        // ── Tab 1: Tonight ──
        int ty = tabTop;
        place(m_lblTonightHdr, x, ty, cw, SH);        ty += SH + 4;
        int tonightListH = h - ty - (BH + 4) - 12;
        if (tonightListH < m_fontPx * 6) tonightListH = m_fontPx * 6;
        place(m_listTonight, x, ty, cw, tonightListH);
        ty += tonightListH + 4;
        place(m_btnGoToTonight, x, ty, cw, BH + 4);

        ScaleListColumns(cw);

        // Repaint the background. Static controls do not erase behind
        // themselves when they move, so the old text stays on the
        // parent and the heading reads as two overlapping copies of
        // itself -- which is exactly what the first pass looked like.
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    // List columns in proportion to the width. The object name is what
    // the list is read for, so it takes the slack -- at the old fixed
    // 160 px a wider window just left dead space to the right of Dec.
    void ScaleListColumns(int cw) {
        if (m_listObjects) {
            const int fixed[] = { 0, 50, 40, 70, 70 };
            const double k = (double)m_fontPx / 12.0;
            int used = 0;
            for (int i = 1; i < 5; i++) used += (int)(fixed[i] * k);
            int name = cw - used - 24;             // 24: scrollbar + edges
            if (name < (int)(90 * k)) name = (int)(90 * k);
            ListView_SetColumnWidth(m_listObjects, 0, name);
            for (int i = 1; i < 5; i++)
                ListView_SetColumnWidth(m_listObjects, i, (int)(fixed[i] * k));
        }
        if (m_listTonight) {
            const int fixed[] = { 0, 38, 90, 48, 40, 36 };
            const double k = (double)m_fontPx / 12.0;
            int used = 0;
            for (int i = 1; i < 6; i++) used += (int)(fixed[i] * k);
            int name = cw - used - 24;
            if (name < (int)(90 * k)) name = (int)(90 * k);
            ListView_SetColumnWidth(m_listTonight, 0, name);
            for (int i = 1; i < 6; i++)
                ListView_SetColumnWidth(m_listTonight, i, (int)(fixed[i] * k));
        }
    }

    // What the stack needs vertically at a given type size, with the
    // object list at its minimum. Kept beside LayoutControls and in
    // the same terms, because the two disagreeing is how a button ends
    // up off the bottom of the window.
    int FixedStackHeight(int px) const {
        const int SH = (px * 3) / 2;
        const int BH = px * 2;
        const int top = 4 + (BH + 4) + 4        // tab strip
                      + SH + 2                  // ALIGNMENT
                      + SH * 2 + 2              // align state
                      + BH + 6                  // solve-to-align
                      + SH + 4                  // OBJECT CATALOG
                      + BH + 4;                 // search row
        const int below = (BH + 4 + 8)          // GOTO SELECTED
                        + (SH + 4)              // MANUAL COORDS
                        + (BH + 4)              // RA / DEC
                        + (BH + 8)              // GOTO COORDS
                        + (SH + 4)              // PLATE SOLVE
                        + (BH + 4 + 8)          // SOLVE / SYNC
                        + (SH + 4)              // alignment points
                        + (BH + 4)              // STOP SLEW
                        + (SH * 2 + 6)          // status
                        + (SH + 4)              // SKY SURVEY
                        + (SH * 2 + 4)          // survey state
                        + (BH + 8);             // START SURVEY
        return top + (px * 6) + 6 + below;      // + the list's minimum
    }

    // One font for every control, scaled off the window height, the
    // same way Help and the AI panel do it.
    void ApplyFontPx(int px) {
        if (m_uiFont && px == m_fontPx) return;
        m_fontPx = px;

        HFONT old = m_uiFont;
        m_uiFont = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (!m_uiFont) { m_uiFont = old; return; }

        EnumChildWindows(m_hwnd, [](HWND c, LPARAM lp) -> BOOL {
            SendMessageW(c, WM_SETFONT, (WPARAM)lp, TRUE);
            return TRUE;
        }, (LPARAM)m_uiFont);

        // After the controls have taken the new one, never before.
        if (old) DeleteObject(old);
    }

    void ShowTab(int tab) {
        m_activeTab = tab;
        // Tab 0: GoTo controls
        bool g = (tab == 0);
        auto setVis = [](HWND h, bool v) { if (h) ShowWindow(h, v ? SW_SHOW : SW_HIDE); };
        setVis(m_editSearch,    g);
        setVis(m_listObjects,   g);
        setVis(m_btnGoTo,       g);
        setVis(m_editRA,        g);
        setVis(m_editDec,       g);
        setVis(m_btnGoToCoord,  g);
        setVis(m_btnSolve,      g);
        setVis(m_btnPushTo,     g);
        setVis(m_btnSync,       g);
        setVis(m_btnStop,       g);
        setVis(m_lblStatus,     g);
        setVis(m_lblAlign,      g);
        setVis(m_lblAlignState, g);
        setVis(m_btnSolveAlign, g);
        // Tab 1: Tonight planner
        bool t = (tab == 1);
        setVis(m_listTonight,      t);
        setVis(m_btnGoToTonight,   t);
        setVis(m_lblTonightHdr,    t);
        if (t) PopulateTonightList();
    }

    // ── Phase 13: Sky Survey ────────────────────────────────────

    struct SurveyPoint {
        double az_deg;
        double alt_deg;
        bool   solved   = false;
        double errorArcsec = 0.0;
    };

    // Generate a grid of survey positions spread across the sky above minAlt
    std::vector<SurveyPoint> GenerateSurveyGrid(int count = 20,
                                                 double minAlt = 30.0,
                                                 double maxAlt = 78.0) {
        std::vector<SurveyPoint> grid;
        // Concentric altitude rings with evenly-spaced azimuths
        // Ring altitudes: 35, 50, 65, 75 degrees
        struct Ring { double alt; int n; };
        Ring rings[] = { {35,8}, {50,6}, {65,4}, {75,2} };
        for (auto& ring : rings) {
            if (ring.alt < minAlt || ring.alt > maxAlt) continue;
            for (int i = 0; i < ring.n; i++) {
                SurveyPoint p;
                p.az_deg  = (360.0 / ring.n) * i;
                p.alt_deg = ring.alt;
                grid.push_back(p);
            }
        }
        // Truncate to requested count
        if ((int)grid.size() > count) grid.resize(count);
        return grid;
    }

    // Convert Alt/Az to RA/Dec for current time and site
    void AltAzToRADec(double alt_deg, double az_deg,
                      double& ra_h, double& dec_deg) {
        using namespace raduk::sky;
        double lst = ApproximateLST();
        double lat = Rad(m_lat);
        double alt = Rad(alt_deg);
        double az  = Rad(az_deg);

        double sinDec = sin(lat)*sin(alt) - cos(lat)*cos(alt)*cos(az);
        dec_deg = Deg(asin(sinDec));

        double cosHA = (sin(alt) - sin(lat)*sinDec) /
                       (cos(lat)*cos(Rad(dec_deg)));
        cosHA = cosHA < -1.0 ? -1.0 : (cosHA > 1.0 ? 1.0 : cosHA);
        double ha_deg = Deg(acos(cosHA));
        if (sin(az) > 0) ha_deg = 360.0 - ha_deg;

        ra_h = NormDeg(lst * 15.0 - ha_deg) / 15.0;
    }

    void StartSurvey() {
        if (m_surveyRunning.load()) return;
        if (!m_mount || !m_mount->IsConnected()) {
            SetStatus(L"No mount — cannot survey");
            return;
        }
        if (!m_solver.IsAvailable()) {
            SetStatus(L"ASTAP not found — cannot survey (install from hnsky.org)");
            return;
        }
        m_surveyGrid    = GenerateSurveyGrid(20);
        m_surveyIdx     = 0;
        m_surveyRunning.store(true);
        m_surveyAbort.store(false);
        UpdateSurveyLabel();
        SetWindowTextW(m_btnSurvey, L"STOP SURVEY");
        SetStatus(L"Survey started — slewing to position 1...");
        // Launch sequencer on a background thread
        std::thread([this]() { SurveyThread(); }).detach();
    }

    void StopSurvey() {
        m_surveyAbort.store(true);
        // Thread will exit on next iteration
    }

    void UpdateSurveyLabel() {
        if (!m_lblSurvey) return;
        if (!m_surveyRunning.load() && m_surveyGrid.empty()) {
            int total = m_pointingModel.EntryCount();
            wchar_t buf[128];
            if (total == 0)
                swprintf(buf, 128, L"Survey: not run yet   [GoTo accuracy: uncalibrated]");
            else
                swprintf(buf, 128, L"Survey: %d points   Mean error: %.0f\"",
                         total, m_pointingModel.MeanError());
            SetWindowTextW(m_lblSurvey, buf);
            return;
        }
        int solved = 0;
        for (auto& p : m_surveyGrid) if (p.solved) solved++;
        int total = (int)m_surveyGrid.size();
        wchar_t buf[128];
        swprintf(buf, 128, L"Survey: %d/%d   Mean: %.0f\"",
                 solved, total, m_pointingModel.MeanError());
        SetWindowTextW(m_lblSurvey, buf);
    }

    // Survey sequencer — runs on background thread
    void SurveyThread() {
        printf("[Survey] Starting %d-point sky survey\n",
               (int)m_surveyGrid.size());

        for (int i = 0; i < (int)m_surveyGrid.size(); i++) {
            if (m_surveyAbort.load()) break;

            m_surveyIdx = i;
            auto& pt = m_surveyGrid[i];

            // Convert Alt/Az to RA/Dec for current LST
            double ra_h = 0, dec_deg = 0;
            AltAzToRADec(pt.alt_deg, pt.az_deg, ra_h, dec_deg);

            // Status update (safe via PostMessage → WM_USER+20)
            wchar_t status[128];
            swprintf(status, 128,
                     L"Survey %d/%d: slewing to Az %.0f\u00B0 Alt %.0f\u00B0...",
                     i+1, (int)m_surveyGrid.size(), pt.az_deg, pt.alt_deg);
            {
                auto* ws = new std::wstring(status);
                PostMessageW(m_hwnd, WM_USER+20, 0, (LPARAM)ws);
            }

            // Slew to position
            if (m_mount && m_mount->IsConnected())
                m_mount->GoToRADec(ra_h, dec_deg);

            // Wait for slew to start (up to 3s)
            Sleep(3000);

            // Wait for slew to complete (up to 90s)
            int waitMs = 0;
            while (waitMs < 90000 && !m_surveyAbort.load()) {
                if (!m_mount->IsSlewing()) break;
                Sleep(500);
                waitMs += 500;
            }
            if (m_surveyAbort.load()) break;

            // Settle time — vibration dampening
            Sleep(5000);
            if (m_surveyAbort.load()) break;

            // Plate-solve current position
            swprintf(status, 128, L"Survey %d/%d: plate solving...",
                     i+1, (int)m_surveyGrid.size());
            {
                auto* ws = new std::wstring(status);
                PostMessageW(m_hwnd, WM_USER+20, 0, (LPARAM)ws);
            }

            // Request a solve via main thread (WM_USER+21)
            // Main thread will call AutoSolveOnLock with current frame
            m_surveySolveReady.store(false);
            PostMessageW(m_hwnd, WM_USER+21, i, 0);

            // Wait for solve result (up to 30s)
            int solveWait = 0;
            while (solveWait < 30000 && !m_surveySolveReady.load()
                   && !m_surveyAbort.load()) {
                Sleep(200);
                solveWait += 200;
            }

            if (m_surveyLastSolved) {
                pt.solved      = true;
                pt.errorArcsec = m_pointingModel.LastError();
            }

            // Update label on main thread
            PostMessageW(m_hwnd, WM_USER+22, 0, 0);
        }

        // Survey complete
        m_surveyRunning.store(false);
        m_surveyAbort.store(false);
        PostMessageW(m_hwnd, WM_USER+23, 0, 0);
        printf("[Survey] Complete. %d entries in pointing model. Mean error: %.0f\"\n",
               m_pointingModel.EntryCount(),
               m_pointingModel.MeanError());
    }

    // ── Phase 29b: Action Popup ─────────────────────────────────
    // Small WS_POPUP near a clicked list row with SLEW / INFO / CANCEL.
    // Created fresh on each click, destroys on any action or focus loss.

    struct ActionPopupState {
        HWND         hwnd      = nullptr;
        std::string  name;
        double       ra        = 0;
        double       dec       = 0;
        float        alt       = -999;
        float        az        = -999;
        float        mag       = 99;
        GoToPanel*   owner     = nullptr;
    };

    static constexpr int AP_SLEW   = 7100;
    static constexpr int AP_INFO   = 7101;
    static constexpr int AP_CANCEL = 7102;

    static LRESULT CALLBACK ActionPopupProc(HWND hwnd, UINT msg,
                                             WPARAM wp, LPARAM lp) {
        ActionPopupState* s = nullptr;
        if (msg == WM_NCCREATE) {
            s = reinterpret_cast<ActionPopupState*>(
                reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)s);
        } else {
            s = reinterpret_cast<ActionPopupState*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        switch (msg) {
            case WM_COMMAND: {
                int id = LOWORD(wp);
                if (!s || !s->owner) { DestroyWindow(hwnd); return 0; }
                GoToPanel* p = s->owner;
                if (id == AP_SLEW) {
                    p->DoGoTo(s->ra, s->dec, s->name);
                    p->ShowTab(0);
                    TabCtrl_SetCurSel(p->m_tabCtrl, 0);
                } else if (id == AP_INFO) {
                    if (p->m_onInfo)
                        p->m_onInfo(s->name, s->alt, s->az, s->mag);
                }
                DestroyWindow(hwnd);
                return 0;
            }
            case WM_KILLFOCUS:
                // Dismiss if focus moves outside the popup and its children
                if ((HWND)wp != hwnd) {
                    HWND child = GetWindow(hwnd, GW_CHILD);
                    bool focusOnChild = false;
                    while (child) {
                        if (child == (HWND)wp) { focusOnChild = true; break; }
                        child = GetWindow(child, GW_HWNDNEXT);
                    }
                    if (!focusOnChild) DestroyWindow(hwnd);
                }
                return 0;
            case WM_DESTROY:
                if (s) { delete s; }
                return 0;
            case WM_CTLCOLORBTN:
            case WM_CTLCOLORSTATIC: {
                HDC hdc = (HDC)wp;
                SetBkColor(hdc, Theme::NV(Theme::Rgb::PANEL));
                SetTextColor(hdc, Theme::NV(Theme::Rgb::TEXT));
                HBRUSH bg = Theme::Brush(Theme::Rgb::PANEL);
                return (LRESULT)bg;
            }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    void ShowActionPopup(HWND listHwnd, int row,
                         const std::string& name, double ra, double dec,
                         float alt, float az, float mag) {
        // Register window class once
        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc = {};
            wc.cbSize        = sizeof(wc);
            wc.lpfnWndProc   = ActionPopupProc;
            wc.hInstance     = GetModuleHandle(nullptr);
            wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = CreateSolidBrush(Theme::NV(Theme::Rgb::PANEL));
            wc.lpszClassName = L"RadukActionPopup";
            RegisterClassExW(&wc);
            registered = true;
        }

        // Position near the clicked row
        RECT itemRect = {};
        ListView_GetItemRect(listHwnd, row, &itemRect, LVIR_BOUNDS);
        POINT pt = { itemRect.left, itemRect.bottom };
        ClientToScreen(listHwnd, &pt);

        const int PW = 228, PH = 34;
        // Keep on screen
        int sw = GetSystemMetrics(SM_CXSCREEN);
        if (pt.x + PW > sw - 4) pt.x = sw - PW - 4;

        bool belowHorizon = (alt > -90 && alt < 15.0f);

        auto* state = new ActionPopupState();
        state->name  = name;
        state->ra    = ra;
        state->dec   = dec;
        state->alt   = alt;
        state->az    = az;
        state->mag   = mag;
        state->owner = this;

        HWND popup = CreateWindowExW(
            WS_EX_TOPMOST,
            L"RadukActionPopup", L"",
            WS_POPUP | WS_BORDER,
            pt.x, pt.y, PW, PH,
            m_hwnd, nullptr, GetModuleHandle(nullptr), state);
        if (!popup) { delete state; return; }
        state->hwnd = popup;

        // Fonts
        HFONT fnt = CreateFontW(-Theme::Type::MICRO, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        const int BH = PH - 6, BY = 3;
        int x = 3;

        // ✓ SLEW button — disabled if below horizon
        if (!belowHorizon) {
            HWND bSlew = CreateWindowExW(0, L"BUTTON", L"\u2713 Slew",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                x, BY, 68, BH, popup, (HMENU)(intptr_t)AP_SLEW,
                GetModuleHandle(nullptr), nullptr);
            if (fnt) SendMessage(bSlew, WM_SETFONT, (WPARAM)fnt, TRUE);
            x += 71;
        }

        // ℹ INFO button
        HWND bInfo = CreateWindowExW(0, L"BUTTON", L"\u2139 Info",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, BY, 68, BH, popup, (HMENU)(intptr_t)AP_INFO,
            GetModuleHandle(nullptr), nullptr);
        if (fnt) SendMessage(bInfo, WM_SETFONT, (WPARAM)fnt, TRUE);
        x += 71;

        // ✗ CANCEL
        HWND bCancel = CreateWindowExW(0, L"BUTTON", L"\u2716 Cancel",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, BY, 68, BH, popup, (HMENU)(intptr_t)AP_CANCEL,
            GetModuleHandle(nullptr), nullptr);
        if (fnt) SendMessage(bCancel, WM_SETFONT, (WPARAM)fnt, TRUE);

        ShowWindow(popup, SW_SHOWNOACTIVATE);
        // Give focus so WM_KILLFOCUS fires when user clicks elsewhere
        SetFocus(popup);
    }

    void BuildControls(int w, int h) {
        const int x = 8, SH = 15, BH = 24;
        int y = 4;

        // ── Phase 14: Tab control ─────────────────────────────
        m_tabCtrl = CreateWindowExW(0, WC_TABCONTROLW, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FIXEDWIDTH,
            x, y, w, 26,
            m_hwnd, (HMENU)(INT_PTR)ID_TAB_MAIN,
            GetModuleHandle(nullptr), nullptr);
        if (m_tabCtrl) {
            TCITEMW ti = {}; ti.mask = TCIF_TEXT;
            ti.pszText = const_cast<wchar_t*>(L"GoTo");
            SendMessageW(m_tabCtrl, TCM_INSERTITEMW, 0, (LPARAM)&ti);
            ti.pszText = const_cast<wchar_t*>(L"Tonight");
            SendMessageW(m_tabCtrl, TCM_INSERTITEMW, 1, (LPARAM)&ti);
        }
        y += 30;

        // ── Alignment status banner ───────────────────────────
        // Shows alignment state prominently — GoTo is locked until aligned
        m_hdrAlign = Label(L"\u2500\u2500 ALIGNMENT \u2500\u2500", x, y, w, SH);
        y += SH + 2;
        m_lblAlignState = Label(L"Checking...", x, y, w, SH*2, ID_LBL_ALIGN_STATE);
        y += SH*2 + 2;

        // Plate-solve to align — point at any bright object and click
        m_btnSolveAlign = Button(
            L"POINT AT OBJECT \u2192 SOLVE TO ALIGN",
            x, y, w, BH, ID_BTN_SOLVE_ALIGN);
        y += BH + 6;

        // ── Search box ────────────────────────────────────────
        m_hdrCatalog = Label(L"\u2500\u2500 OBJECT CATALOG \u2500\u2500", x, y, w, SH);
        y += SH + 4;

        m_lblSearch = Label(L"SEARCH:", x, y, 52, SH);
        m_editSearch = Edit(L"", x+56, y-2, w-56, BH, ID_EDIT_SEARCH);
        SetWindowTheme(m_editSearch, L"", L"");
        y += BH + 4;

        // ── Object list ───────────────────────────────────────
        int listH = 280;
        m_listObjects = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            x, y, w, listH, m_hwnd,
            (HMENU)(INT_PTR)ID_LIST_OBJECTS, GetModuleHandle(nullptr), nullptr);
        ListView_SetExtendedListViewStyle(m_listObjects,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        SetWindowTheme(m_listObjects, L"", L"");

        // Columns: Name | Type | Mag | RA | Dec
        auto addCol = [&](const wchar_t* text, int width, int col) {
            LVCOLUMNW lvc = {};
            lvc.mask     = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            lvc.cx       = width;
            lvc.pszText  = const_cast<wchar_t*>(text);
            lvc.iSubItem = col;
            SendMessageW(m_listObjects, LVM_INSERTCOLUMNW, col, (LPARAM)&lvc);
        };
        addCol(L"Object",    160, 0);
        addCol(L"Type",       50, 1);
        addCol(L"Mag",        40, 2);
        addCol(L"RA",         70, 3);
        addCol(L"Dec",        70, 4);
        y += listH + 6;

        // ── GoTo button ───────────────────────────────────────
        m_btnGoTo = Button(L"GOTO SELECTED", x, y, w, BH+4, ID_BTN_GOTO);
        y += BH + 8;

        // ── Manual RA/DEC entry ───────────────────────────────
        m_hdrManual = Label(L"\u2500\u2500 MANUAL COORDS \u2500\u2500", x, y, w, SH);
        y += SH + 4;

        m_lblRA = Label(L"RA (h):", x, y, 46, SH);
        m_editRA = Edit(L"", x+50, y-2, (w-50)/2-4, BH, ID_EDIT_RA);
        m_lblDec = Label(L"DEC (°):", x + (w+6)/2, y, 46, SH);
        m_editDec = Edit(L"", x + (w+6)/2 + 50, y-2, (w-50)/2-4, BH, ID_EDIT_DEC);
        y += BH + 4;
        m_btnGoToCoord = Button(L"GOTO COORDS", x, y, w, BH, ID_BTN_GOTO_COORD);
        y += BH + 8;

        // GoTo disabled until aligned
        EnableWindow(m_btnGoTo, FALSE);
        EnableWindow(m_btnGoToCoord, FALSE);

        // ── Plate solve / sync ────────────────────────────────
        m_hdrSolve = Label(L"\u2500\u2500 PLATE SOLVE \u2500\u2500", x, y, w, SH);
        y += SH + 4;

        int hw = (w-4)/2;
        m_btnSolve = Button(L"SOLVE FRAME", x,      y, hw, BH+4, ID_BTN_SOLVE);
        m_btnSync  = Button(L"SYNC MOUNT",  x+hw+4, y, hw, BH+4, ID_BTN_SYNC);
        y += BH + 8;
        m_btnPushTo = Button(L"PUSH-TO: OFF", x, y, w, BH+4, ID_BTN_PUSHTO);
        y += BH + 8;

        // Alignment points counter
        m_lblAlign = Label(L"Alignment points: 0", x, y, w, SH, ID_LBL_ALIGN);
        y += SH + 4;

        // ── Stop + status ─────────────────────────────────────
        m_btnStop  = Button(L"!! STOP SLEW !!", x, y, w, BH, ID_BTN_STOP);
        y += BH + 4;
        m_lblStatus = Label(L"READY", x, y, w, SH*2, ID_LBL_STATUS);
        y += SH*2 + 6;

        // ── Phase 13: Sky Survey ──────────────────────────────
        m_hdrSurvey = Label(L"\u2500\u2500 SKY SURVEY (POINTING MODEL) \u2500\u2500", x, y, w, SH);
        y += SH + 4;
        m_lblSurvey = Label(L"Survey: not run yet   [GoTo accuracy: uncalibrated]",
                            x, y, w, SH*2, ID_LBL_SURVEY);
        y += SH*2 + 4;
        m_btnSurvey = Button(L"START SURVEY  (20 positions, ~15 min)",
                             x, y, w, BH, ID_BTN_SURVEY);
        y += BH + 4;

        // ── Phase 14: Tonight Planner controls (Tab 1) ────────
        int tonightY = 34; // just below tab strip
        m_lblTonightHdr = Label(
            L"Objects sorted by imaging time remaining tonight (> 30° alt)",
            x, tonightY, w, SH, ID_LBL_TONIGHT_HDR);
        tonightY += SH + 4;

        int tonightListH = h - tonightY - BH - 20;
        if (tonightListH < 100) tonightListH = 100;
        m_listTonight = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VSCROLL |
            LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            x, tonightY, w, tonightListH,
            m_hwnd, (HMENU)(INT_PTR)ID_LIST_TONIGHT,
            GetModuleHandle(nullptr), nullptr);
        if (m_listTonight) {
            ListView_SetExtendedListViewStyle(m_listTonight,
                LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            SetWindowTheme(m_listTonight, L"", L"");
            auto addCol = [&](const wchar_t* text, int width, int col) {
                LVCOLUMNW lvc = {};
                lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
                lvc.cx = width; lvc.pszText = const_cast<wchar_t*>(text);
                lvc.iSubItem = col;
                SendMessageW(m_listTonight, LVM_INSERTCOLUMNW, col, (LPARAM)&lvc);
            };
            addCol(L"Object",   155, 0);
            addCol(L"Alt",       38, 1);
            addCol(L"Window",    90, 2);
            addCol(L"Left",      48, 3);
            addCol(L"Peak",      40, 4);
            addCol(L"Mag",       36, 5);
        }
        tonightY += tonightListH + 4;
        m_btnGoToTonight = Button(L"GOTO SELECTED OBJECT", x, tonightY, w, BH+4,
                                  ID_BTN_GOTO_TONIGHT);

        // P3.3: everything above was created at whatever coordinates
        // the creation code happened to compute. This is what actually
        // decides where they sit, and it is the same call WM_SIZE
        // makes -- so there is no second copy of the arithmetic to
        // fall out of step.
        {
            RECT cr; GetClientRect(m_hwnd, &cr);
            LayoutControls(cr.right, cr.bottom);
        }

        // Start on GoTo tab (tab 0), tonight controls hidden
        ShowTab(0);
    }

    // ── List population ────────────────────────────────────────
    void PopulateList(const std::string& filter) {
        ListView_DeleteAllItems(m_listObjects);
        m_listItems.clear();

        std::vector<const raduk::sky::SkyObject*> items;
        if (filter.empty()) {
            // Sort by altitude at the configured site (RigConfig)
            // Use approximate LST based on current UTC
            double lst = ApproximateLST();
            items = m_catalog.ByAltitude(lst, m_lat);
            // Filter to mag < 10 to keep list manageable
            std::vector<const raduk::sky::SkyObject*> filtered;
            for (auto* o : items)
                if (o->magnitude < 10.0f) filtered.push_back(o);
            items = filtered;
        } else {
            auto results = m_catalog.Search(filter);
            items = results;
        }

        // Limit to 200 items
        if (items.size() > 200) items.resize(200);

        int row = 0;
        for (const auto* obj : items) {
            m_listItems.push_back(obj);

            wchar_t wname[128];
            MultiByteToWideChar(CP_UTF8, 0, obj->name.c_str(), -1, wname, 128);

            LVITEMW lvi = {};
            lvi.mask     = LVIF_TEXT;
            lvi.iItem    = row;
            lvi.iSubItem = 0;
            lvi.pszText  = wname;
            // LVM_INSERTITEMW, not the ListView_InsertItem macro.
            //
            // This project is not built with UNICODE defined, so that
            // macro resolves to the ANSI message -- and it was being
            // handed a UTF-16 string. "M13" is 4D 00 31 00 33 00, which
            // read as ANSI is "M" followed by a terminator. Every
            // object in the catalogue has been showing as its first
            // letter alone, in the list the slew target is chosen from.
            //
            // The Type, Mag, RA and Dec columns were right all along
            // because they go through LVM_SETITEMTEXTW explicitly, and
            // so does the Tonight list's insert. This one call was the
            // odd one out.
            SendMessageW(m_listObjects, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

            wchar_t wtype[32];
            MultiByteToWideChar(CP_UTF8, 0, obj->type.c_str(), -1, wtype, 32);
            {LVITEMW li={};li.iSubItem=1;li.pszText=wtype;
             SendMessageW(m_listObjects,LVM_SETITEMTEXTW,row,(LPARAM)&li);}

            wchar_t wmag[16];
            swprintf(wmag, 16, L"%.1f", obj->magnitude);
            {LVITEMW li={};li.iSubItem=2;li.pszText=wmag;
             SendMessageW(m_listObjects,LVM_SETITEMTEXTW,row,(LPARAM)&li);}

            // RA
            int rh = (int)obj->ra;
            int rm = (int)((obj->ra - rh) * 60.0);
            wchar_t wra[16]; swprintf(wra, 16, L"%dh%02dm", rh, rm);
            {LVITEMW li={};li.iSubItem=3;li.pszText=wra;
             SendMessageW(m_listObjects,LVM_SETITEMTEXTW,row,(LPARAM)&li);}

            // Dec
            char dsign = obj->dec >= 0 ? '+' : '-';
            int dd = (int)fabs(obj->dec);
            int dm = (int)((fabs(obj->dec) - dd) * 60.0);
            wchar_t wdec[16]; swprintf(wdec, 16, L"%c%d\xB0%02d'", dsign, dd, dm);
            {LVITEMW li={};li.iSubItem=4;li.pszText=wdec;
             SendMessageW(m_listObjects,LVM_SETITEMTEXTW,row,(LPARAM)&li);}

            row++;
        }

        printf("[GoToPanel] Populated list: %d objects\n", row);
    }

    void UpdateSlewStatus() {
        // Only poll the mount while a GoTo is actually in flight —
        // m_lastSlewing is seeded by DoGoTo. IsSlewing() is a serial
        // round-trip on the NexStar path, so don't call it idly, and
        // call it only ONCE per tick (it was previously called twice,
        // racing against the slew ending between the two calls).
        if (!m_lastSlewing) return;
        if (!m_mount || !m_mount->IsConnected()) { m_lastSlewing = false; return; }
        if (m_mount->IsSlewing()) {
            SetWindowTextW(m_lblStatus, L"SLEWING...");
        } else {
            m_lastSlewing = false;
            SetWindowTextW(m_lblStatus, L"SLEW COMPLETE — consider plate solving");
            if (m_onGoToComplete) m_onGoToComplete(m_currentTarget);
        }
    }

    void DoGoTo(double ra_h, double dec_deg, const std::string& name) {
        if (!m_mount || !m_mount->IsConnected()) {
            SetStatus(L"No mount connected");
            return;
        }
        // Before the mount moves, not after: a solve-derived readout
        // must stop answering the moment the frame stops being the
        // frame that was solved.
        InvalidateWcs();
        m_currentTarget = name;
        if (m_onTargetChanged) m_onTargetChanged(name);

        // Auto-load matching camera profile before slewing
        // e.g. "Jupiter" → loads Jupiter preset (gain, exp, ROI, WB)
        // Do this BEFORE the slew so camera is ready when mount arrives
        if (m_onProfileAutoLoad) m_onProfileAutoLoad(name);

        // Phase 6.6: the accumulated pointing map, REPORTED and not
        // applied. It records every plate-solved error and can already
        // compute a correction, and wiring that into the commanded
        // coordinates was the obvious next step -- but it is wrong twice
        // over, and both reasons need sky time to settle rather than a
        // guess here:
        //
        //  1. SIGN. GetCorrection returns (actual - expected), i.e.
        //     truth minus what the mount claimed. A GoTo drives until
        //     the mount's REPORTED position equals the target, which
        //     leaves the telescope that error away from it. The fix is
        //     therefore to command target MINUS the error;
        //     ApplyPointingCorrection adds it, which would double the
        //     error rather than remove it.
        //
        //  2. THE DATA IS ALREADY SPENT. AutoSolveOnLock calls
        //     SyncRADec with the solved position immediately before
        //     recording it, so the mount's model is corrected at that
        //     point and the error just recorded no longer exists.
        //     Applying it later would reintroduce an error the mount
        //     does not have.
        //
        // Doing this properly means either recording without syncing,
        // so the map is the only correction, or recording post-sync
        // residuals, which measure what the sync failed to fix. Which
        // of those is right is a measurement, not a decision. Until
        // then the prediction is shown so the map is at least visible,
        // and the slew is left alone.
        double predRA = 0.0, predDec = 0.0;
        double tAlt = 0.0, tAz = 0.0;
        bool corrected = false;
        if (raduk::core::RigConfig::Get().SiteIsSet()) {
            raduk::core::RADecToAltAzNow(ra_h, dec_deg, tAlt, tAz);
            // Below the horizon there is nothing to correct for, and
            // the map has no entries there anyway.
            if (tAlt > 0.0)
                corrected = m_pointingModel.GetCorrection(tAz, tAlt,
                                                          predRA, predDec);
        }
        char buf[256];
        sprintf_s(buf, "Loading %s profile → slewing to %s (%.4fh, %.4f°)...",
                  name.c_str(), name.c_str(), ra_h, dec_deg);
        printf("[GoToPanel] %s\n", buf);
        wchar_t wbuf[256];
        MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, 256);
        SetStatus(wbuf);
        // Phase 67: let the cord-wrap guard veto this. Only the
        // user-initiated path is checked -- the sky survey below fires
        // dozens of slews unattended and must not raise a modal each
        // time.
        if (m_preSlew && !m_preSlew(ra_h, dec_deg)) {
            SetStatus(L"Slew cancelled - cord wrap");
            return;
        }
        m_mount->GoToRADec(ra_h, dec_deg);
        m_lastSlewing = true;
        if (corrected) {
            // Reported so the map is visible and can be judged against
            // what the mount actually does, which is the evidence needed
            // to settle the two problems above.
            char cb[200];
            sprintf_s(cb, "Pointing model predicts %+.1f\" RA, %+.1f\" Dec "
                          "here (from %d nearby points) - NOT applied, see "
                          "Phase 6.6 note",
                      predRA  * 15.0 * 3600.0,
                      predDec * 3600.0,
                      m_pointingModel.NearbyCount(tAz, tAlt));
            if (m_onLog) m_onLog(cb);
        }
        ReportFraming(name);
        ReportLunarFeature(name);
    }

    bool IsLunarFeature(const std::string& name) const {
        for (const auto& o : m_catalog.Objects())
            if (o.name == name) return o.isLunarFeature();
        return false;
    }

    // ── Phase 115: is it lit, and can it be seen ────────────────
    //
    // Down the log for the reasons ReportFraming gives. It goes after
    // the slew and does not refuse one: the terminator may be an hour
    // away, and whether to wait is the operator's call.
    void ReportLunarFeature(const std::string& name) {
        if (!m_onLog) return;
        for (const auto& o : m_catalog.Objects()) {
            if (o.name != name || !o.isLunarFeature()) continue;
            const auto site = SiteObserver();
            const auto view = raduk::sky::LunarViewAt(
                                  raduk::sky::CurrentJD(), site);
            const auto spot = raduk::sky::LunarSpotAt(
                                  view, o.lunarLatDeg, o.lunarLonDeg);
            m_onLog(raduk::sky::DescribeLunarSpot(
                        name.c_str(), spot, view, o.lunarLonDeg,
                        o.lunarSizeKm));
            if (!site.set)
                m_onLog("No site set: that slew is up to a degree off, "
                        "which is several frames.");
            return;
        }
    }

    // ── Phase 97.4: say whether it fits, before the slew lands ──
    //
    // Phase 97 worked out that the Moon is 2.4 frames wide and 4.3
    // tall on this rig, that it needs eighteen panels, and that under
    // sidereal tracking the drift over those eighteen exceeds the
    // whole overlap -- so the mosaic does not close. None of that was
    // on any screen.
    //
    // It goes down m_onLog rather than into the panel's layout, which
    // is a deliberate choice and not laziness. The log already carries
    // the plate-scale comparison after every solve and the pointing
    // model's prediction after every corrected slew, so an operator
    // reads it; and a framing report has nothing to add on the great
    // majority of targets, which makes it exactly the wrong thing to
    // reserve permanent pixels for. Jupiter gets one line saying it
    // fits and the Moon gets four.
    //
    // Phrased for the moment it arrives: the slew has gone, and what
    // the operator decides next is whether to commit the next eight
    // minutes.
    void ReportFraming(const std::string& name) {
        if (!m_onLog) return;
        const auto sz = raduk::sky::PlanetApparentSizeAt(
                            name, raduk::sky::CurrentJD());
        if (!sz.valid) return;          // a star has no disc to frame

        // The LIVE frame, from SubmitFrame, not a nominal sensor size.
        // The whole point of the report is how much of the target fits
        // in the frame being captured right now, and a 640x480 crop
        // needs 432 panels where the full sensor needs 18.
        if (m_lastFrameW == 0 || m_lastFrameH == 0) return;
        const raduk::sky::FieldOfView fov = raduk::sky::FrameFov(
            (int)m_lastFrameW, (int)m_lastFrameH,
            raduk::core::RigConfig::Get().plateScaleArcsecPx);

        // The Sun drifts thirteen times slower than the Moon, which is
        // why the rate is chosen per target rather than assumed: a
        // solar mosaic closes under sidereal and a lunar one does not.
        const auto rate = raduk::core::IMount::RateForTarget(name.c_str());
        const double drift =
            rate == raduk::core::IMount::RATE_LUNAR
                ? raduk::sky::MOON_DRIFT_ARCSEC_PER_SEC
            : rate == raduk::core::IMount::RATE_SOLAR
                ? raduk::sky::SUN_DRIFT_ARCSEC_PER_SEC
                : 0.0;        // a planet is fixed against the stars

        // Whether the mount is ACTUALLY following the target, not
        // whether we asked it to. A driver may refuse the rate, and a
        // report that assumed success would tell the operator the
        // mosaic closes when it will not.
        const bool tracked =
            rate != raduk::core::IMount::RATE_SIDEREAL &&
            m_mount && m_mount->SupportsTrackingRate();

        for (const auto& line : raduk::sky::DescribeFraming(
                 name.c_str(), sz.framingArcsec / 60.0, fov,
                 MOSAIC_OVERLAP, tracked ? drift * LUNAR_RATE_RESIDUAL : drift,
                 MOSAIC_CAPTURE_SEC, MOSAIC_SETTLE_SEC, tracked)) {
            m_onLog(line);
        }
    }

    // The assumptions the report rests on, named rather than buried as
    // literals, because the settle time is a GUESS -- nobody has timed
    // an 8SE after a 10-arcmin slew, and it is the one input that
    // decides whether the drift budget is reachable. Clear-night item
    // 18 is about measuring it.
    static constexpr double MOSAIC_OVERLAP     = 0.20;
    static constexpr double MOSAIC_CAPTURE_SEC = 20.0;
    static constexpr double MOSAIC_SETTLE_SEC  =  8.0;
    // A mount at lunar rate still carries the error between a MEAN
    // rate and the true one, which varies a few per cent over the
    // month. Five per cent is the pessimistic end of that.
    static constexpr double LUNAR_RATE_RESIDUAL = 0.05;

    void DoPlateSolve() {
        if (m_lastFrameData.empty()) {
            SetStatus(L"No frame available — wait for camera");
            return;
        }
        if (!m_solver.IsAvailable()) {
            SetStatus(L"ASTAP not found. Install from hnsky.org/astap.htm");
            return;
        }
        if (m_solver.IsSolving()) {
            SetStatus(L"Solve already in progress...");
            return;
        }

        // Hint solver with current mount position
        if (m_mount && m_mount->IsConnected()) {
            double ra, dec;
            if (m_mount->GetRADec(ra, dec))
                m_solver.SetHint(ra, dec);
        }

        SetStatus(L"Plate solving... (may take 5-30s)");
        printf("[GoToPanel] Starting plate solve\n");

        uint32_t fw = m_lastFrameW, fh = m_lastFrameH;
        auto* data = m_lastFrameData.data();

        const int gen = WcsGen();
        m_solver.SolveAsync(data, fw, fh,
            [this, fw, fh, gen](raduk::sky::SolveResult result) {
                // Called from worker thread — post to main thread via message
                if (result.solved) {
                    m_solvedRA  = result.ra;
                    m_solvedDec = result.dec;
                    m_hasSolution = true;
                    wchar_t msg[128];
                    swprintf(msg, 128,
                        L"SOLVED: RA=%.4fh Dec=%.4f° FoV=%.3f°",
                        result.ra, result.dec, result.fovW);
                    PostSolveStatus(msg);
                    PublishWcs(result, (int)fw, (int)fh, gen);
                    ReportPlateScale(result, fw);
                    printf("[GoToPanel] Plate solve OK: RA=%.4fh Dec=%.4f°\n",
                           result.ra, result.dec);
                } else {
                    wchar_t msg[128];
                    MultiByteToWideChar(CP_UTF8, 0,
                        result.error.c_str(), -1, msg, 128);
                    PostSolveStatus(msg);
                    m_hasSolution = false;
                }
            });
    }

    void DoPlateSolveAndSync() {
        // Combined solve+sync for alignment — called from SOLVE TO ALIGN button
        // Uses the same solve path but marks mount as PlateSynced after success
        if (m_lastFrameData.empty()) {
            SetStatus(L"No frame yet — wait for camera to stream");
            return;
        }
        if (!m_solver.IsAvailable()) {
            SetStatus(L"ASTAP not found — install from hnsky.org/astap.htm");
            return;
        }
        if (m_solver.IsSolving()) {
            SetStatus(L"Solve already running...");
            return;
        }

        // Use current mount position as hint if available
        if (m_mount && m_mount->IsConnected()) {
            double ra, dec;
            if (m_mount->GetRADec(ra, dec))
                m_solver.SetHint(ra, dec);
        }

        SetStatus(L"Solving... (works great pointed at Jupiter, Saturn, or any bright star)");

        const uint32_t sfw = m_lastFrameW, sfh = m_lastFrameH;
        const int sgen = WcsGen();
        m_solver.SolveAsync(m_lastFrameData.data(), sfw, sfh,
            [this, sfw, sfh, sgen](raduk::sky::SolveResult result) {
                // WORKER THREAD — only touch thread-safe state here.
                // No Win32 UI calls (EnableWindow, SetWindowText etc).
                // Tick() runs on main thread and applies pending UI changes.
                if (result.solved) {
                    m_solvedRA    = result.ra;
                    m_solvedDec   = result.dec;
                    m_hasSolution = true;

                    // Sync mount — serial I/O is mutex-protected, thread-safe
                    if (m_mount && m_mount->IsConnected()) {
                        m_mount->SyncRADec(result.ra, result.dec);
                        m_alignPoints++;
                    }

                    // Signal main thread to update UI via Tick()
                    m_pendingSyncSuccess.store(true);
                    PublishWcs(result, (int)sfw, (int)sfh, sgen);
                    ReportPlateScale(result, sfw);

                    wchar_t msg[128];
                    swprintf(msg, 128,
                        L"ALIGNED via plate solve — RA=%.4fh Dec=%.4f° — GoTo enabled",
                        result.ra, result.dec);
                    PostSolveStatus(msg);
                    printf("[GoToPanel] Alignment via plate solve OK\n");
                    if (m_onSync) m_onSync(result.ra, result.dec);
                } else {
                    wchar_t msg[128];
                    MultiByteToWideChar(CP_UTF8, 0,
                        result.error.c_str(), -1, msg, 128);
                    PostSolveStatus(msg);
                }
            });
    }

    void DoSync() {
        if (!m_hasSolution) {
            SetStatus(L"Solve the frame first before syncing");
            return;
        }
        if (!m_mount || !m_mount->IsConnected()) {
            SetStatus(L"No mount connected");
            return;
        }
        m_mount->SyncRADec(m_solvedRA, m_solvedDec);
        m_alignPoints++;

        // Mark as plate-synced — GoTo now works even if HC wasn't aligned
        if (m_alignState != AlignState::Aligned)
            m_alignState = AlignState::PlateSynced;
        UpdateAlignmentUI();
        if (m_btnGoTo)      EnableWindow(m_btnGoTo, TRUE);
        if (m_btnGoToCoord) EnableWindow(m_btnGoToCoord, TRUE);

        wchar_t msg[128];
        swprintf(msg, 128,
            L"SYNCED: RA=%.4fh Dec=%.4f° — Alignment points: %d",
            m_solvedRA, m_solvedDec, m_alignPoints);
        SetStatus(msg);

        wchar_t alnMsg[32];
        swprintf(alnMsg, 32, L"Alignment points: %d", m_alignPoints);
        SetWindowTextW(m_lblAlign, alnMsg);

        if (m_onSync) m_onSync(m_solvedRA, m_solvedDec);
        m_hasSolution = false; // require a new solve before next sync

        printf("[GoToPanel] Sync complete — %d alignment points\n", m_alignPoints);
    }

    void SetStatus(const wchar_t* msg) {
        if (m_lblStatus) SetWindowTextW(m_lblStatus, msg);
    }

    // Thread-safe status update for solve callback
    void PostSolveStatus(const wchar_t* msg) {
        if (m_lblStatus) {
            // SetWindowText is thread-safe on Win32
            SetWindowTextW(m_lblStatus, msg);
        }
    }

    // Approximate Local Sidereal Time from UTC + observer longitude
    double ApproximateLST() const {
        using namespace raduk::sky;
        double jd  = CurrentJD();
        double T   = JD_to_T(jd);
        double gmst = 280.46061837 + 360.98564736629 * (jd - 2451545.0)
                    + 0.000387933 * T*T;
        gmst = NormDeg(gmst);
        double lst_deg = NormDeg(gmst + m_lon); // east positive
        return lst_deg / 15.0; // hours
    }

    // ── Win32 helpers ──────────────────────────────────────────
    HWND Button(const wchar_t* text, int x, int y, int w, int h, int id) {
        return CreateWindowExW(0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, y, w, h, m_hwnd,
            (HMENU)(INT_PTR)id, GetModuleHandle(nullptr), nullptr);
    }
    HWND Edit(const wchar_t* text, int x, int y, int w, int h, int id) {
        return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
            x, y, w, h, m_hwnd,
            (HMENU)(INT_PTR)id, GetModuleHandle(nullptr), nullptr);
    }
    HWND Label(const wchar_t* text, int x, int y, int w, int h, int id = 0) {
        return CreateWindowExW(0, L"STATIC", text,
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            x, y, w, h, m_hwnd,
            (HMENU)(INT_PTR)id, GetModuleHandle(nullptr), nullptr);
    }

    // ── Window proc ────────────────────────────────────────────
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                     WPARAM wParam, LPARAM lParam) {
        GoToPanel* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            self = static_cast<GoToPanel*>(cs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
            self->m_hwnd = hwnd;
        } else {
            self = reinterpret_cast<GoToPanel*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }
        if (!self) return DefWindowProc(hwnd, msg, wParam, lParam);

        switch (msg) {
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLOREDIT: {
                HDC hdc = (HDC)wParam;
                SetBkMode(hdc, OPAQUE);
                SetBkColor(hdc, Theme::NV(Theme::Rgb::PANEL));
                SetTextColor(hdc, Theme::NV(Theme::Rgb::ACCENT));
                HBRUSH hBg = Theme::Brush(Theme::Rgb::PANEL);
                return (LRESULT)hBg;
            }
            case WM_ERASEBKGND: {
                HDC hdc = (HDC)wParam;
                RECT r; GetClientRect(hwnd, &r);
                HBRUSH hBg = Theme::Brush(Theme::Rgb::PANEL);
                FillRect(hdc, &r, hBg);
                return 1;
            }
            case WM_COMMAND: {
                int id = LOWORD(wParam);
                int notif = HIWORD(wParam);

                if (id == ID_BTN_SOLVE_ALIGN) {
                    // Plate solve current frame and sync — establishes alignment
                    // without needing the HC. After sync, GoTo is enabled.
                    self->SetStatus(L"Solving frame for alignment...");
                    self->DoPlateSolveAndSync();
                    return 0;
                }
                if (id == ID_BTN_GOTO) {
                    // Get selected object from list
                    int sel = ListView_GetNextItem(self->m_listObjects, -1, LVNI_SELECTED);
                    if (sel >= 0 && sel < (int)self->m_listItems.size()) {
                        const auto* obj = self->m_listItems[sel];
                        self->DoGoTo(obj->ra, obj->dec, obj->name);
                    } else {
                        self->SetStatus(L"Select an object from the list first");
                    }
                    return 0;
                }
                if (id == ID_BTN_GOTO_COORD) {
                    wchar_t raBuf[32] = {}, decBuf[32] = {};
                    GetWindowTextW(self->m_editRA,  raBuf, 32);
                    GetWindowTextW(self->m_editDec, decBuf, 32);
                    double ra  = _wtof(raBuf);
                    double dec = _wtof(decBuf);
                    if (ra > 0 || dec != 0) {
                        self->DoGoTo(ra, dec, "Manual coords");
                    } else {
                        self->SetStatus(L"Enter RA (hours) and Dec (degrees)");
                    }
                    return 0;
                }
                if (id == ID_BTN_SOLVE) {
                    self->DoPlateSolve();
                    return 0;
                }
                if (id == ID_BTN_PUSHTO) {
                    self->TogglePushTo();
                    return 0;
                }
                if (id == ID_BTN_SYNC) {
                    self->DoSync();
                    return 0;
                }
                if (id == ID_BTN_STOP) {
                    if (self->m_mount && self->m_mount->IsConnected()) {
                        self->m_mount->CancelGoTo();
                        self->m_mount->StopSlew(0);
                        self->m_mount->StopSlew(1);
                    }
                    self->SetStatus(L"Slew stopped");
                    return 0;
                }
                if (id == ID_EDIT_SEARCH && notif == EN_CHANGE) {
                    wchar_t wbuf[128] = {};
                    GetWindowTextW(self->m_editSearch, wbuf, 128);
                    char buf[128] = {};
                    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, 128, nullptr, nullptr);
                    self->PopulateList(buf);
                    return 0;
                }
                // Phase 14: GoTo from tonight planner
                if (id == ID_BTN_GOTO_TONIGHT) {
                    int sel = ListView_GetNextItem(self->m_listTonight, -1, LVNI_SELECTED);
                    if (sel >= 0 && sel < (int)self->m_plannerItems.size()) {
                        const auto& e = self->m_plannerItems[sel];
                        self->DoGoTo(e.ra, e.dec, e.name);
                        // Switch to GoTo tab so status is visible
                        self->ShowTab(0);
                        TabCtrl_SetCurSel(self->m_tabCtrl, 0);
                    } else {
                        self->SetStatus(L"Select an object from Tonight list first");
                    }
                    return 0;
                }
                // Phase 13: Survey start/stop
                if (id == ID_BTN_SURVEY) {
                    if (self->m_surveyRunning.load())
                        self->StopSurvey();
                    else
                        self->StartSurvey();
                    return 0;
                }
                break;
            }
            // Phase 13: Survey thread messages
            case WM_USER+20: {
                // Status string from survey thread
                auto* ws = reinterpret_cast<std::wstring*>(lParam);
                if (ws) { self->SetStatus(ws->c_str()); delete ws; }
                return 0;
            }
            case WM_USER+21: {
                // Survey thread requesting a plate solve of current frame
                // main.cpp wires this via SetSurveyFrameCallback
                if (self->m_onSurveyNeedFrame) self->m_onSurveyNeedFrame();
                return 0;
            }
            case WM_USER+22: {
                // Survey step complete — update label
                self->UpdateSurveyLabel();
                return 0;
            }
            case WM_USER+23: {
                // Survey fully complete
                self->UpdateSurveyLabel();
                SetWindowTextW(self->m_btnSurvey,
                    L"START SURVEY  (20 positions, ~15 min)");
                double mean = self->m_pointingModel.MeanError();
                wchar_t buf[128];
                swprintf(buf, 128,
                    L"Survey complete! %d points  Mean error: %.0f\"  "
                    L"GoTo accuracy improved.",
                    self->m_pointingModel.EntryCount(), mean);
                self->SetStatus(buf);
                return 0;
            }
            // Phase 14: tab switch
            case WM_NOTIFY: {
                auto* hdr = reinterpret_cast<NMHDR*>(lParam);
                if (!hdr) return 0;
                // Tab switch
                if (hdr->hwndFrom == self->m_tabCtrl
                        && hdr->code == TCN_SELCHANGE) {
                    int tab = TabCtrl_GetCurSel(self->m_tabCtrl);
                    self->ShowTab(tab);
                }
                // Single click on GoTo list → action popup
                if (hdr->hwndFrom == self->m_listObjects
                        && hdr->code == NM_CLICK) {
                    auto* nm = reinterpret_cast<NMITEMACTIVATE*>(lParam);
                    int row = nm->iItem;
                    if (row >= 0) {
                        wchar_t wname[64] = {};
                        LVITEMW li = {}; li.iItem = row;
                        li.iSubItem = 0; li.mask = LVIF_TEXT;
                        li.pszText = wname; li.cchTextMax = 64;
                        SendMessageW(self->m_listObjects, LVM_GETITEMW, 0, (LPARAM)&li);
                        char name[64] = {};
                        WideCharToMultiByte(CP_UTF8, 0, wname, -1, name, 64, nullptr, nullptr);
                        auto results = self->m_catalog.Search(name);
                        if (!results.empty()) {
                            const raduk::sky::SkyObject* obj = results[0];
                            double lstNow = self->LSTFromJD(raduk::sky::CurrentJD());
                            float alt = (float)raduk::sky::SkyObjectCatalog::CalcAlt(
                                obj->ra, obj->dec, lstNow, self->m_lat);
                            self->ShowActionPopup(self->m_listObjects, row,
                                obj->name, obj->ra, obj->dec,
                                alt, 0.0f, obj->magnitude);
                        }
                    }
                }
                // Single click on Tonight list → action popup
                if (hdr->hwndFrom == self->m_listTonight
                        && hdr->code == NM_CLICK) {
                    auto* nm = reinterpret_cast<NMITEMACTIVATE*>(lParam);
                    int row = nm->iItem;
                    if (row >= 0 && row < (int)self->m_plannerItems.size()) {
                        const auto& e = self->m_plannerItems[row];
                        self->ShowActionPopup(self->m_listTonight, row,
                            e.name, e.ra, e.dec,
                            e.altNow, e.azDeg, e.mag);
                    }
                }
                // Double-click on Tonight list → info panel (keep as shortcut)
                if (hdr->hwndFrom == self->m_listTonight
                        && (hdr->code == NM_DBLCLK || hdr->code == NM_RETURN)) {
                    int sel = ListView_GetNextItem(self->m_listTonight, -1, LVNI_SELECTED);
                    if (sel >= 0 && sel < (int)self->m_plannerItems.size()
                            && self->m_onInfo) {
                        const auto& e = self->m_plannerItems[sel];
                        self->m_onInfo(e.name, e.altNow, e.azDeg, e.mag);
                    }
                }
                return 0;
            }
            // P3.3: this panel has had WS_SIZEBOX since it was written
            // and never handled WM_SIZE, so dragging it larger left
            // every control where it was.
            case WM_SIZE:
                if (self && wParam != SIZE_MINIMIZED)
                    self->LayoutControls(LOWORD(lParam), HIWORD(lParam));
                return 0;

            case WM_CLOSE:
                ShowWindow(hwnd, SW_HIDE);
                return 0;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    // ── Members ────────────────────────────────────────────────
    HWND m_hwnd    = nullptr;
    HWND m_parent  = nullptr;

    // Controls
    HWND m_editSearch    = nullptr;
    HWND m_listObjects   = nullptr;
    HWND m_btnGoTo       = nullptr;
    HWND m_editRA        = nullptr;
    HWND m_editDec       = nullptr;
    HWND m_btnGoToCoord  = nullptr;
    HWND m_btnSolve      = nullptr;
    HWND m_btnPushTo     = nullptr;

    // ── Phase 30: push-to assistant ────────────────────────────
    // A repeating plate solve that says which way to move the
    // scope, for when the mount cannot be told to do it: not
    // connected, badly aligned, or being pushed by hand.
    //
    // The cadence is deliberately "as fast as solving allows"
    // rather than a timer. A solve takes 5 to 30 seconds and the
    // answer is stale the moment the scope moves, so the useful
    // rate is simply the solve rate. PUSHTO_MIN_GAP_MS exists only
    // so a fast FAILURE cannot spin the CPU.
    static constexpr uint64_t PUSHTO_MIN_GAP_MS = 1000;

    std::atomic<bool>      m_pushToActive{false};
    double                 m_pushToTargetRA  = 0.0;   // hours
    double                 m_pushToTargetDec = 0.0;   // degrees
    std::string            m_pushToName;
    uint64_t               m_pushToLastKickMs = 0;    // main thread only

    // Written by the solver worker, read by Tick on the main
    // thread. A mutex rather than atomics because the hint is a
    // struct and a half-updated one would point somebody the wrong
    // way, which is the single worst thing this feature can do.
    // Phase 47b: the last solve, on its way to the preview.
    std::mutex             m_wcsMutex;
    raduk::sky::SolveResult m_wcsResult;
    int                    m_wcsW = 0;
    int                    m_wcsH = 0;
    std::atomic<bool>      m_wcsFresh{false};
    std::atomic<int>       m_wcsGen{0};
    std::function<void(const raduk::sky::SolveResult&, int, int)> m_onWcs;

    std::mutex             m_pushToMutex;
    raduk::sky::PushToHint m_pushToHint;
    std::string            m_pushToLine;
    std::atomic<bool>      m_pushToFresh{false};
    HWND m_btnSync       = nullptr;
    HWND m_btnStop       = nullptr;
    HWND m_lblStatus     = nullptr;
    HWND m_lblAlign      = nullptr;
    HWND m_lblAlignState = nullptr;   // alignment banner
    HWND m_btnSolveAlign = nullptr;   // "SOLVE TO ALIGN" button

    // Phase 14: Session Planner controls
    HWND m_tabCtrl        = nullptr;
    HWND m_listTonight    = nullptr;
    HWND m_btnGoToTonight = nullptr;
    HWND m_lblTonightHdr  = nullptr;
    int  m_activeTab      = 0;
    std::vector<PlannerEntry> m_plannerItems;

    // Data
    raduk::sky::SkyObjectCatalog m_catalog;
    raduk::sky::ASTAPSolver      m_solver;
    raduk::core::IMount*         m_mount = nullptr;

    std::vector<const raduk::sky::SkyObject*> m_listItems;

    // Latest frame for plate solving
    std::vector<uint8_t> m_lastFrameData;
    uint32_t             m_lastFrameW = 0;
    uint32_t             m_lastFrameH = 0;

    // Solve state
    bool   m_hasSolution = false;
    double m_solvedRA    = 0.0;
    double m_solvedDec   = 0.0;
    std::atomic<bool> m_pendingSyncSuccess{false}; // set by solver thread, cleared by Tick()

    // Alignment state machine
    AlignState m_alignState = AlignState::Unknown;

    // GoTo state
    bool        m_lastSlewing   = false;
    int         m_alignPoints   = 0;
    std::string m_currentTarget;
    std::function<void(const std::string&)> m_onTargetChanged;

    // Observer location — 0,0 until SetLocation() wires in RigConfig
    double m_lat = 0.0;
    double m_lon = 0.0;

    // Callbacks
    std::function<void(const std::string&)> m_onGoToComplete;

    // Phase 97.5: mosaic execution. The runner is a pure state
    // machine; these are the handles the panel holds for it.
    raduk::sky::MosaicRunner m_mosaic;
    std::string m_mosaicTarget;
    double m_mosaicRa = 0.0, m_mosaicDec = 0.0;
    bool   m_mosaicWasRunning = false;
    std::function<bool(int,int)> m_mosaicBegin;
    std::function<void()>        m_mosaicEnd;
    std::function<void(const std::string&)> m_onProfileAutoLoad;
    std::function<void(const std::string&)> m_onLog;
    std::atomic<double> m_pixelUm{0.0};
    std::function<bool(double,double)>  m_preSlew;   // cord-wrap guard
    std::function<void(double,double)>      m_onSync;
    std::function<void(const std::string&, float, float, float)> m_onInfo;

    // Phase 6.6: auto solve+sync on Stage 3 lock
    std::atomic<bool>           m_autoSolving{false};
    raduk::sky::PointingModel   m_pointingModel;
    std::function<void(bool, const std::string&, double, const std::string&)>
                                m_onAutoSolveComplete;

    // Phase 13: Sky Survey
    std::vector<SurveyPoint>    m_surveyGrid;
    int                         m_surveyIdx        = 0;
    std::atomic<bool>           m_surveyRunning{false};
    std::atomic<bool>           m_surveyAbort{false};
    std::atomic<bool>           m_surveySolveReady{false};
    bool                        m_surveyLastSolved = false;
    HWND                        m_btnSurvey        = nullptr;
    HWND                        m_lblSurvey        = nullptr;

    // ── P3.3: the section headings, and the layout's own state ──
    //
    // These labels used to be created and their handles thrown away,
    // which was fine while nothing ever moved them. LayoutControls has
    // to move everything or the stack comes apart, so they are kept.
    HWND                        m_lblSearch        = nullptr;
    HWND                        m_lblRA            = nullptr;
    HWND                        m_lblDec           = nullptr;
    HWND                        m_hdrAlign         = nullptr;
    HWND                        m_hdrCatalog       = nullptr;
    HWND                        m_hdrManual        = nullptr;
    HWND                        m_hdrSolve         = nullptr;
    HWND                        m_hdrSurvey        = nullptr;
    HFONT                       m_uiFont           = nullptr;
    int                         m_fontPx           = 0;
    std::function<void()>       m_onSurveyNeedFrame;
};

} // namespace raduk::gui
