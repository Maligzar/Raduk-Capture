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
    };

    void BuildControls(int w, int) {
        const int x = 8, SH = 15, BH = 24;
        int y = 4;

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
        if (!m_mount || !m_mount->IsConnected()) return;
        if (m_mount->IsSlewing()) {
            SetWindowTextW(m_lblStatus, L"SLEWING...");
        } else if (m_lastSlewing) {
            // Just finished slewing
            SetWindowTextW(m_lblStatus, L"SLEW COMPLETE — consider plate solving");
            if (m_onGoToComplete) m_onGoToComplete(m_currentTarget);
        }
        m_lastSlewing = m_mount->IsSlewing();
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
                break;
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

    // Phase 6.6: auto solve+sync on Stage 3 lock
    std::atomic<bool>           m_autoSolving{false};
    raduk::sky::PointingModel   m_pointingModel;
    std::function<void(bool, const std::string&, double, const std::string&)>
                                m_onAutoSolveComplete;
};

} // namespace raduk::gui
