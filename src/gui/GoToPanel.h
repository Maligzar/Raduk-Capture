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

#include "../sky/SkyObject.h"
#include "../sky/ASTAPSolver.h"
#include "../sky/PointingModel.h"
#include "../core/IMount.h"
#include "../core/FrameSlot.h"

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

    // Observer location for altitude sorting (Anthem, AZ defaults)
    void SetLocation(double lat, double lon) { m_lat = lat; m_lon = lon; }

    // Called when GoTo is issued — main.cpp uses this to auto-load
    // the matching camera profile (e.g. GoTo Jupiter → load Jupiter profile)
    void SetProfileAutoLoadCallback(std::function<void(const std::string&)> cb)
        { m_onProfileAutoLoad = cb; }

    // Called when GoTo completes (to trigger plate solve)
    void SetGoToCompleteCallback(std::function<void(const std::string&)> cb)
        { m_onGoToComplete = cb; }

    // Called when sync is performed (notify status bar etc.)
    void SetSyncCallback(std::function<void(double,double)> cb)
        { m_onSync = cb; }

    // ── Lifecycle ──────────────────────────────────────────────
    bool Create(HWND parent, int x, int y, int w, int h) {
        m_parent = parent;

        WNDCLASSEXW wc = {};
        wc.cbSize       = sizeof(wc);
        wc.lpfnWndProc  = WndProc;
        wc.hInstance    = GetModuleHandle(nullptr);
        wc.hCursor      = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(10, 21, 32));
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
    void RequestGoTo(double raH, double decDeg, const std::string& name) {
        DoGoTo(raH, decDeg, name);
    }

    // Search catalog by name (case-insensitive), slew to first match.
    // Used by APEX remote commands and sky map auto-GoTo.
    void RequestGoToByName(const std::string& name) {
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

    // ── Alignment state ────────────────────────────────────────
    enum class AlignState {
        Unknown,      // just started, haven't checked yet
        Unaligned,    // HC reports not aligned
        Aligning,     // StarSense scan in progress
        Aligned,      // HC reports aligned — GoTo enabled
        PlateSynced   // user has plate-solved and synced — GoTo enabled
    };

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
        m_catalog.UpdatePlanets();
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
            [this, objCopy, hintRA, hintDec, az, alt,
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
                SetBkColor(hdc, RGB(10, 18, 30));
                SetTextColor(hdc, RGB(200, 230, 245));
                static HBRUSH bg = CreateSolidBrush(RGB(10, 18, 30));
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
            wc.hbrBackground = CreateSolidBrush(RGB(10, 18, 30));
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
        HFONT fnt = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
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
        Label(L"\u2500\u2500 ALIGNMENT \u2500\u2500", x, y, w, SH);
        y += SH + 2;
        m_lblAlignState = Label(L"Checking...", x, y, w, SH*2, ID_LBL_ALIGN_STATE);
        y += SH*2 + 2;

        // Plate-solve to align — point at any bright object and click
        m_btnSolveAlign = Button(
            L"POINT AT OBJECT \u2192 SOLVE TO ALIGN",
            x, y, w, BH, ID_BTN_SOLVE_ALIGN);
        y += BH + 6;

        // ── Search box ────────────────────────────────────────
        Label(L"\u2500\u2500 OBJECT CATALOG \u2500\u2500", x, y, w, SH);
        y += SH + 4;

        Label(L"SEARCH:", x, y, 52, SH);
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
        Label(L"\u2500\u2500 MANUAL COORDS \u2500\u2500", x, y, w, SH);
        y += SH + 4;

        Label(L"RA (h):", x, y, 46, SH);
        m_editRA = Edit(L"", x+50, y-2, (w-50)/2-4, BH, ID_EDIT_RA);
        Label(L"DEC (°):", x + (w+6)/2, y, 46, SH);
        m_editDec = Edit(L"", x + (w+6)/2 + 50, y-2, (w-50)/2-4, BH, ID_EDIT_DEC);
        y += BH + 4;
        m_btnGoToCoord = Button(L"GOTO COORDS", x, y, w, BH, ID_BTN_GOTO_COORD);
        y += BH + 8;

        // GoTo disabled until aligned
        EnableWindow(m_btnGoTo, FALSE);
        EnableWindow(m_btnGoToCoord, FALSE);

        // ── Plate solve / sync ────────────────────────────────
        Label(L"\u2500\u2500 PLATE SOLVE \u2500\u2500", x, y, w, SH);
        y += SH + 4;

        int hw = (w-4)/2;
        m_btnSolve = Button(L"SOLVE FRAME", x,      y, hw, BH+4, ID_BTN_SOLVE);
        m_btnSync  = Button(L"SYNC MOUNT",  x+hw+4, y, hw, BH+4, ID_BTN_SYNC);
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
        Label(L"\u2500\u2500 SKY SURVEY (POINTING MODEL) \u2500\u2500", x, y, w, SH);
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

        // Start on GoTo tab (tab 0), tonight controls hidden
        ShowTab(0);
    }

    // ── List population ────────────────────────────────────────
    void PopulateList(const std::string& filter) {
        ListView_DeleteAllItems(m_listObjects);
        m_listItems.clear();

        std::vector<const raduk::sky::SkyObject*> items;
        if (filter.empty()) {
            // Sort by altitude: Anthem, AZ (lat 33.86, lon -112.09)
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
            ListView_InsertItem(m_listObjects, &lvi);

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
        m_currentTarget = name;

        // Auto-load matching camera profile before slewing
        // e.g. "Jupiter" → loads Jupiter preset (gain, exp, ROI, WB)
        // Do this BEFORE the slew so camera is ready when mount arrives
        if (m_onProfileAutoLoad) m_onProfileAutoLoad(name);
        char buf[256];
        sprintf_s(buf, "Loading %s profile → slewing to %s (%.4fh, %.4f°)...",
                  name.c_str(), name.c_str(), ra_h, dec_deg);
        printf("[GoToPanel] %s\n", buf);
        wchar_t wbuf[256];
        MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, 256);
        SetStatus(wbuf);
        m_mount->GoToRADec(ra_h, dec_deg);
        m_lastSlewing = true;
    }

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

        m_solver.SolveAsync(data, fw, fh,
            [this](raduk::sky::SolveResult result) {
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

        m_solver.SolveAsync(m_lastFrameData.data(),
                            m_lastFrameW, m_lastFrameH,
            [this](raduk::sky::SolveResult result) {
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
                SetBkColor(hdc, RGB(10, 21, 32));
                SetTextColor(hdc, RGB(0, 229, 255));
                static HBRUSH hBg = CreateSolidBrush(RGB(10, 21, 32));
                return (LRESULT)hBg;
            }
            case WM_ERASEBKGND: {
                HDC hdc = (HDC)wParam;
                RECT r; GetClientRect(hwnd, &r);
                static HBRUSH hBg = CreateSolidBrush(RGB(10, 21, 32));
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

    // Observer location (Anthem, AZ)
    double m_lat =  33.86;
    double m_lon = -112.09;

    // Callbacks
    std::function<void(const std::string&)> m_onGoToComplete;
    std::function<void(const std::string&)> m_onProfileAutoLoad;
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
    std::function<void()>       m_onSurveyNeedFrame;
};

} // namespace raduk::gui
