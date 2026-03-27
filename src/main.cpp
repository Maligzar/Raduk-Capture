// ============================================================
// main.cpp — RadukCapture v0.9 Phase 9/10 Complete
// M7:  USB traffic manager
// M9:  Frame analyzer + autofocus routine
// M10: Quality gate (seeing score threshold)
// M12: WebSocket telemetry server → APEX dashboard
// P4:  Remote commands (load_profile, move_eaf, goto_target)
// P4:  Session summary log at shutdown
// Profile EDIT button — modal editor, all fields, persists to disk
// ============================================================

// winsock2.h must come before windows.h to prevent v1 winsock redefinition errors.
// WIN32_LEAN_AND_MEAN stops windows.h from pulling in the old winsock.h.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>    // must be first — before windows.h
#include <windows.h>
#include <shellapi.h>
#include <shellscalingapi.h>  // SetProcessDpiAwarenessContext — must be after windows.h
#include <cstdio>
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "core/DeviceFactory.h"
#include "core/RingBuffer.h"
#include "core/CaptureThread.h"
#include "core/CaptureManager.h"
#include "core/USBTrafficManager.h"
#include "core/FrameAnalyzer.h"
#include "core/CentroidTracker.h"
#include "core/Timestamp.h"
#include "core/SessionLog.h"
#include "net/WebSocketServer.h"
#include "net/APEXTelemetry.h"
#include "gui/Window.h"
#include "gui/PreviewRenderer.h"
#include "gui/ControlPanel.h"
#include "gui/ToolbarStrip.h"
#include "gui/GoToPanel.h"
#include "gui/SkyMapWindow.h"
#include "gui/MountController.h"
#include "gui/StatusBar.h"
#include "gui/HelpWindow.h"
#include "gui/AIChatPanel.h"
#include "gui/NotificationBar.h"
#include "gui/ObjectInfoPanel.h"
#include "core/AIContextBuilder.h"
#include "profiles/TargetProfile.h"
#include "profiles/ProfileManager.h"
#include "core/AutofocusRoutine.h"
#include "core/ADCAssistant.h"
#include "core/AutoExposureAssistant.h"

using namespace raduk::core;
using namespace raduk::gui;
using namespace raduk::net;
using namespace raduk::profiles;

// ── Crash-safe logging ─────────────────────────────────────────
static FILE* g_log = nullptr;

static void LogInit() {
    g_log = fopen("D:\\RadukCapture\\raduk_log.txt", "w");
    if (!g_log) g_log = fopen("raduk_log.txt", "w");
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
}

static void Log(const char* fmt, ...) {
    va_list args;
    if (g_log) { va_start(args, fmt); vfprintf(g_log, fmt, args); va_end(args); fflush(g_log); }
    va_start(args, fmt); vprintf(fmt, args); va_end(args); fflush(stdout);
}

// ── Global mount pointer for crash/exit emergency stop ────────
// Set after mount connects, cleared on disconnect.
// Used by CrashHandler and atexit to stop mount on fatal exit.
static raduk::core::NexStarMount* g_mountForCrash = nullptr;

static void EmergencyStopOnExit() {
    if (g_mountForCrash && g_mountForCrash->IsConnected()) {
        printf("[SAFETY] atexit: sending emergency mount stop\n");
        g_mountForCrash->EmergencyStop();
    }
}

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep) {
    Log("\n[CRASH] Exception 0x%08lX at %p\n",
        ep->ExceptionRecord->ExceptionCode,
        ep->ExceptionRecord->ExceptionAddress);
    // Emergency stop mount before crash dump — runaway mount is dangerous
    if (g_mountForCrash && g_mountForCrash->IsConnected()) {
        Log("[CRASH] Sending emergency mount stop...\n");
        g_mountForCrash->EmergencyStop();
        Log("[CRASH] Mount stopped\n");
    }
    if (g_log) fclose(g_log);
    return EXCEPTION_CONTINUE_SEARCH;
}

// ── Hardware detection ─────────────────────────────────────────
#ifndef RADUK_EMULATOR
// ── Hardware detection ─────────────────────────────────────────
struct HardwareStatus {
    bool cameraFound = false;
    bool eafFound    = false;
    bool mountFound  = false;
    bool AllPresent() const { return cameraFound && eafFound && mountFound; }
};

static HardwareStatus CheckHardware() {
    HardwareStatus s;
    Log("[CHECK] Scanning for hardware...\n");
    int camCount = ASIGetNumOfConnectedCameras();
    s.cameraFound = (camCount > 0);
    Log("[CHECK]   Camera: %s (%d)\n", s.cameraFound ? "OK" : "NOT FOUND", camCount);
    int eafCount = EAFFocuser::GetCount();
    s.eafFound = (eafCount > 0);
    Log("[CHECK]   EAF:    %s (%d)\n", s.eafFound ? "OK" : "NOT FOUND", eafCount);
    HANDLE hTest = CreateFileA("\\\\.\\COM3", GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hTest != INVALID_HANDLE_VALUE) {
        s.mountFound = true;
        CloseHandle(hTest);
        Log("[CHECK]   Mount:  OK (COM3 available)\n");
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED || err == ERROR_SHARING_VIOLATION) {
            s.mountFound = true;
            Log("[CHECK]   Mount:  OK (COM3 owned by CPWI)\n");
        } else {
            s.mountFound = false;
            Log("[CHECK]   Mount:  NOT FOUND (COM3 error %lu)\n", err);
        }
    }
    return s;
}

// ── Startup Device Dialog ───────────────────────────────────────
// Shown when hardware scan fails. Lets user choose emulator mode
// or retry after plugging devices in.

struct StartupDlgState {
    HardwareStatus  hw;
    bool            launchEmulator = false;
    bool            launchHardware = false;
    bool            quit           = false;
    HWND            hDlg           = nullptr;
    // Labels for each device
    HWND lblCamera = nullptr, lblEAF = nullptr, lblMount = nullptr;
    HWND lblHint   = nullptr;
};

static void UpdateStartupDeviceLabels(StartupDlgState& s) {
    auto setLbl = [](HWND h, bool ok, const wchar_t* name) {
        if (!h) return;
        wchar_t buf[64];
        swprintf(buf, 64, L"%s  %s", ok ? L"\u2705" : L"\u274C", name);
        SetWindowTextW(h, buf);
    };
    setLbl(s.lblCamera, s.hw.cameraFound, L"ZWO ASI678MC Camera");
    setLbl(s.lblEAF,    s.hw.eafFound,    L"ZWO EAF Focuser");
    setLbl(s.lblMount,  s.hw.mountFound,  L"NexStar Mount (COM3)");
    if (s.lblHint) {
        if (s.hw.AllPresent())
            SetWindowTextW(s.lblHint, L"All devices found — ready for hardware mode.");
        else
            SetWindowTextW(s.lblHint, L"One or more devices missing. Plug in and retry, or launch in emulator mode.");
    }
    // Enable [Launch Hardware] only when all present
    HWND hHW = GetDlgItem(s.hDlg, 1003);
    if (hHW) EnableWindow(hHW, s.hw.AllPresent() ? TRUE : FALSE);
}

static LRESULT CALLBACK StartupDlgProc(HWND hDlg, UINT msg,
                                        WPARAM wp, LPARAM lp) {
    StartupDlgState* s = nullptr;
    if (msg == WM_NCCREATE) {
        s = reinterpret_cast<StartupDlgState*>(
            reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)s);
        s->hDlg = hDlg;
    } else {
        s = reinterpret_cast<StartupDlgState*>(
            GetWindowLongPtrW(hDlg, GWLP_USERDATA));
    }
    if (!s) return DefWindowProcW(hDlg, msg, wp, lp);

    switch (msg) {
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == 1001) { // Retry
                s->hw = CheckHardware();
                UpdateStartupDeviceLabels(*s);
            } else if (id == 1002) { // Start Emulators
                // Always launch build_emu\Release\RadukCapture.exe — that's
                // the only path that has CameraEmulator, EAFEmulator, MountEmulator.
                // If it doesn't exist yet, run build_emulator.bat first.
                wchar_t exePath[MAX_PATH] = {};
                GetModuleFileNameW(nullptr, exePath, MAX_PATH);
                std::wstring dir(exePath);
                auto slash = dir.rfind(L'\\');
                std::wstring repoRoot;
                if (slash != std::wstring::npos) {
                    std::wstring exeDir = dir.substr(0, slash);
                    // exe lives in build\Release\ — go up two levels
                    auto s2 = exeDir.rfind(L'\\');
                    if (s2 != std::wstring::npos) {
                        auto s3 = exeDir.substr(0, s2).rfind(L'\\');
                        repoRoot = (s3 != std::wstring::npos)
                            ? exeDir.substr(0, s2) // build\Release -> build -> repo
                            : exeDir.substr(0, s2);
                        // Try one more level up
                        auto s4 = repoRoot.rfind(L'\\');
                        if (s4 != std::wstring::npos)
                            repoRoot = repoRoot.substr(0, s4);
                    }
                }
                std::wstring emuExe = repoRoot + L"\\build_emu\\Release\\RadukCapture.exe";
                std::wstring buildBat = repoRoot + L"\\build_emulator.bat";

                // Check if emulator exe exists; if not, build it
                if (GetFileAttributesW(emuExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
                    // Show building message
                    if (s->lblHint)
                        SetWindowTextW(s->lblHint,
                            L"Building emulator... this takes ~30 seconds.");
                    // Run build_emulator.bat synchronously
                    SHELLEXECUTEINFOW sei = {}; sei.cbSize = sizeof(sei);
                    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
                    sei.lpVerb = L"open";
                    sei.lpFile = L"cmd.exe";
                    std::wstring args = L"/c \"" + buildBat + L"\"";
                    sei.lpParameters = args.c_str();
                    sei.lpDirectory  = repoRoot.c_str();
                    sei.nShow = SW_SHOW;
                    if (ShellExecuteExW(&sei) && sei.hProcess) {
                        WaitForSingleObject(sei.hProcess, 120000); // 2 min max
                        CloseHandle(sei.hProcess);
                    }
                }

                // Now launch the emulator exe
                if (GetFileAttributesW(emuExe.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    STARTUPINFOW si = {}; si.cb = sizeof(si);
                    PROCESS_INFORMATION pi = {};
                    if (CreateProcessW(emuExe.c_str(), nullptr, nullptr, nullptr,
                            FALSE, 0, nullptr, nullptr, &si, &pi)) {
                        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
                        s->quit = true; // exit hardware exe cleanly
                    } else {
                        if (s->lblHint)
                            SetWindowTextW(s->lblHint,
                                L"Failed to launch emulator exe. Check build_emu\\Release\\.");
                        return 0; // stay in dialog
                    }
                } else {
                    if (s->lblHint)
                        SetWindowTextW(s->lblHint,
                            L"Build failed. Check D:\\RadukCapture\\build_emulator.bat");
                    return 0;
                }
                DestroyWindow(hDlg);
            } else if (id == 1003) { // Launch Hardware
                s->launchHardware = true;
                DestroyWindow(hDlg);
            } else if (id == 1004) { // Exit
                s->quit = true;
                DestroyWindow(hDlg);
            }
            return 0;
        }
        case WM_CLOSE:
            s->quit = true;
            DestroyWindow(hDlg);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wp;
            HWND hCtrl = (HWND)lp;
            SetBkColor(hdc, RGB(10, 18, 30));
            // Colour the device labels
            if (hCtrl == s->lblCamera)
                SetTextColor(hdc, s->hw.cameraFound ? RGB(60,220,90) : RGB(220,60,60));
            else if (hCtrl == s->lblEAF)
                SetTextColor(hdc, s->hw.eafFound    ? RGB(60,220,90) : RGB(220,60,60));
            else if (hCtrl == s->lblMount)
                SetTextColor(hdc, s->hw.mountFound  ? RGB(60,220,90) : RGB(220,60,60));
            else
                SetTextColor(hdc, RGB(180, 210, 230));
            static HBRUSH bg = CreateSolidBrush(RGB(10, 18, 30));
            return (LRESULT)bg;
        }
        case WM_CTLCOLORBTN: {
            HDC hdc = (HDC)wp;
            SetBkColor(hdc, RGB(20, 30, 45));
            SetTextColor(hdc, RGB(200, 225, 245));
            static HBRUSH btnBg = CreateSolidBrush(RGB(20, 30, 45));
            return (LRESULT)btnBg;
        }
    }
    return DefWindowProcW(hDlg, msg, wp, lp);
}

static bool ShowStartupDialog(HardwareStatus& hwOut) {
    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = StartupDlgProc;
    wc.hInstance     = GetModuleHandle(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(10, 18, 30));
    wc.lpszClassName = L"RadukStartupDlg";
    RegisterClassExW(&wc);

    const int DW = 420, DH = 260;
    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);

    StartupDlgState state;
    state.hw = hwOut;

    HWND hDlg = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"RadukStartupDlg", L"RadukCapture — Device Check",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        (sx - DW) / 2, (sy - DH) / 2, DW, DH,
        nullptr, nullptr, GetModuleHandle(nullptr), &state);
    if (!hDlg) return false;

    // Fonts
    HFONT fTitle = CreateFontW(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT fBody  = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT fMono  = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_MODERN, L"Consolas");

    // Title
    HWND hTitle = CreateWindowExW(0, L"STATIC",
        L"RadukCapture  \u00B7  Device Check",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        16, 12, DW-32, 22, hDlg, nullptr, GetModuleHandle(nullptr), nullptr);
    SendMessage(hTitle, WM_SETFONT, (WPARAM)fTitle, TRUE);

    // Device status labels
    state.lblCamera = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        28, 46, DW-44, 18, hDlg, nullptr, GetModuleHandle(nullptr), nullptr);
    state.lblEAF = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        28, 68, DW-44, 18, hDlg, nullptr, GetModuleHandle(nullptr), nullptr);
    state.lblMount = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        28, 90, DW-44, 18, hDlg, nullptr, GetModuleHandle(nullptr), nullptr);
    // Hint text
    state.lblHint = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        16, 120, DW-32, 36, hDlg, nullptr, GetModuleHandle(nullptr), nullptr);

    SendMessage(state.lblCamera, WM_SETFONT, (WPARAM)fMono, TRUE);
    SendMessage(state.lblEAF,    WM_SETFONT, (WPARAM)fMono, TRUE);
    SendMessage(state.lblMount,  WM_SETFONT, (WPARAM)fMono, TRUE);
    SendMessage(state.lblHint,   WM_SETFONT, (WPARAM)fBody, TRUE);

    // Buttons
    const int BH = 30, BY = DH - 70, BW = 120;
    HWND hRetry = CreateWindowExW(0, L"BUTTON", L"\u21BA  Retry",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        16, BY, BW, BH, hDlg, (HMENU)1001, GetModuleHandle(nullptr), nullptr);
    HWND hEmu = CreateWindowExW(0, L"BUTTON", L"\u25B6  Start Emulators",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        144, BY, 140, BH, hDlg, (HMENU)1002, GetModuleHandle(nullptr), nullptr);
    HWND hHW = CreateWindowExW(0, L"BUTTON", L"\u2705  Launch Hardware",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        292, BY, 112, BH, hDlg, (HMENU)1003, GetModuleHandle(nullptr), nullptr);
    HWND hExit = CreateWindowExW(0, L"BUTTON", L"\u2716  Exit",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        DW - 80, BY + 36, 64, 22, hDlg, (HMENU)1004, GetModuleHandle(nullptr), nullptr);

    SendMessage(hRetry, WM_SETFONT, (WPARAM)fBody, TRUE);
    SendMessage(hEmu,   WM_SETFONT, (WPARAM)fBody, TRUE);
    SendMessage(hHW,    WM_SETFONT, (WPARAM)fBody, TRUE);
    SendMessage(hExit,  WM_SETFONT, (WPARAM)fBody, TRUE);

    // Populate initial device status
    UpdateStartupDeviceLabels(state);
    ShowWindow(hDlg, SW_SHOW);

    // Run message loop until dialog is dismissed
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    hwOut = state.hw;
    if (state.launchEmulator) return true;  // true = use emulator
    if (state.launchHardware) return false; // false = use hardware
    return false; // default if quit
}
#endif // !RADUK_EMULATOR

// ── Apply profile ──────────────────────────────────────────────
static void ApplyProfile(const TargetProfile& p,
                          ASICamera* cam, EAFFocuser* focuser,
                          IMount* mount, SessionLog& slog) {
    Log("[PROFILE] Applying: %s\n", p.name.c_str());
    slog.Profile("Applied: %s", p.name.c_str());
    if (cam && cam->IsOpen()) {
        cam->SetGain(p.gain); cam->SetExposure(p.exposureMs);
        cam->SetGamma(p.gamma); cam->SetWhiteBalance(p.wbRed, p.wbBlue);
        cam->SetUSBBandwidth(p.usbBandwidth); cam->SetHighSpeedMode(p.highSpeed);
    }
    if (focuser && focuser->IsOpen()) {
        focuser->MoveTo(p.focusPosition);
        slog.Focuser("GoTo %d", p.focusPosition);
    }
    if (mount && mount->IsConnected()) {
        IMount::TrackingMode mode = IMount::ALT_AZ;
        if (p.trackingMode == "off")      mode = IMount::OFF;
        if (p.trackingMode == "eq_north") mode = IMount::EQ_NORTH;
        if (p.trackingMode == "eq_south") mode = IMount::EQ_SOUTH;
        mount->SetTrackingMode(mode);
    }
}

// ── Application core ───────────────────────────────────────────
static int RunApp(bool useEmulator) {
    // Window covers the full screen. The control panel is a WS_CHILD
    // positioned at the right edge (clientW - PANEL_WIDTH).
    // Preview renders to (0, 0, clientW - PANEL_WIDTH, clientH).
    const int screenW  = GetSystemMetrics(SM_CXSCREEN);
    const int screenH  = GetSystemMetrics(SM_CYSCREEN);

    Log("==============================================\n");
    Log("  RADUKCAPTURE v0.9 -- Phase 4 Complete %s\n", useEmulator ? "[EMULATOR]" : "");
    Log("==============================================\n\n");

    SessionLog slog;
    slog.Open();
    slog.System("Started -- %s", useEmulator ? "EMULATOR" : "HARDWARE");

    // ── Profiles ───────────────────────────────────────────────
    ProfileManager profiles;
    profiles.Init();
    Log("[INIT] %d profiles loaded\n", (int)profiles.Profiles().size());

    // ── Camera ─────────────────────────────────────────────────
    // DeviceFactory.h swaps ASICamera↔CameraEmulator at compile time.
    // Emulator build: Open() always succeeds, no SDK needed.
    // Hardware build running as emulator (useEmulator=true): also skip SDK check.
#ifndef RADUK_EMULATOR
    if (!useEmulator) {
        auto cameras = ASICamera::Enumerate();
        if (cameras.empty()) {
            MessageBoxA(nullptr, "No cameras detected.", "RadukCapture", MB_ICONERROR);
            return 1;
        }
    }
#endif // RADUK_EMULATOR
    ASICamera cam;
    if (!cam.Open(0)) {
        if (!useEmulator)
            MessageBoxA(nullptr, "Failed to open camera.", "RadukCapture", MB_ICONERROR);
        if (!useEmulator) return 1;
        Log("[INIT] Camera open skipped (emulator mode)\n");
    } else {
        Log("[INIT] Camera: %s\n", cam.Name().c_str());
        slog.Camera("Opened: %s", cam.Name().c_str());
    }

    int roiWidth = 3840, roiHeight = 2160;
    cam.SetROI(roiWidth, roiHeight, 1, ASI_IMG_RAW8);
    // Read back actual ROI — emulator caps to 1280x960
    {
        int aW=roiWidth,aH=roiHeight,aB=1; ASI_IMG_TYPE aT=ASI_IMG_RAW8;
        cam.GetROI(aW,aH,aB,aT); roiWidth=aW; roiHeight=aH;
    }
    cam.SetGain(100); cam.SetExposure(8.0); cam.SetGamma(50);
    cam.SetWhiteBalance(52, 47); cam.SetUSBBandwidth(80); cam.SetHighSpeedMode(true);

    // ── EAF ────────────────────────────────────────────────────
    EAFFocuser focuser;
    raduk::core::AutofocusRoutine autofocus;
#ifdef RADUK_EMULATOR
    focuser.Open(0);
    Log("[INIT] EAF emulated — pos=%d\n", focuser.GetPosition());
#else
    if (useEmulator) {
        // Runtime emulator mode in hardware build — EAFEmulator not available,
        // just skip; focuser stays closed and all focuser calls are guarded by IsOpen()
        Log("[INIT] EAF: skipped (emulator mode)\n");
    } else if (EAFFocuser::GetCount() > 0) {
        if (focuser.Open(0)) Log("[INIT] EAF OK pos=%d\n", focuser.GetPosition());
        else Log("WARNING: EAF open failed.\n");
    }
#endif // RADUK_EMULATOR

    // Phase 9: wire autofocus routine to focuser
    autofocus.SetFocuser(&focuser);
    std::unique_ptr<IMount> mountPtr;
    bool startInRCControl = false;

#ifdef RADUK_EMULATOR
    {
        auto* em = new raduk::core::NexStarMount();
        em->Connect("EMU");
        mountPtr.reset(em);
        startInRCControl = true;
        Log("[INIT] Mount: emulated\n");
    }
#else
    if (useEmulator) {
        // Runtime emulator mode — use a null mount (no mount emulator in hardware build)
        // All mount calls are guarded by mount->IsConnected()
        Log("[INIT] Mount: skipped (emulator mode)\n");
        startInRCControl = false;
    } else {
        // Try ASCOM first (CPWI running)
        std::string ascomProgId = ASCOMMount::FindCPWIDriver();
        if (!ascomProgId.empty()) {
            auto* am = new ASCOMMount();
            if (am->Connect(ascomProgId) && !am->IsSimulator()) {
                mountPtr.reset(am);
                startInRCControl = false;
                Log("[INIT] Mount: ASCOM (%s) — CPWI in control\n", ascomProgId.c_str());
                slog.System("Mount: CPWI active via %s", ascomProgId.c_str());
            } else {
                if (am->IsSimulator())
                    Log("[INIT] ASCOM → simulator detected, ignoring\n");
                am->Disconnect();
                delete am;
            }
        }
        // No ASCOM — try NexStar direct
        if (!mountPtr) {
            auto* nm = new NexStarMount();
            if (nm->Connect("COM3")) {
                mountPtr.reset(nm);
                startInRCControl = true;
                Log("[INIT] Mount: NexStar direct (COM3) — RC in control\n");
                slog.System("Mount: RC direct via COM3");
                g_mountForCrash = nm;
                std::atexit(EmergencyStopOnExit);
                Log("[SAFETY] Heartbeat watchdog armed (timeout=%dms)\n",
                    raduk::core::NexStarMount::WATCHDOG_TIMEOUT_MS);
            } else {
                delete nm;
                Log("[INIT] Mount: not connected (neither CPWI nor COM3 available)\n");
            }
        }
    } // end !useEmulator hardware path
#endif // RADUK_EMULATOR

    IMount* mount = mountPtr.get(); // may be nullptr

    // ── Preview ring + capture thread ──────────────────────────
    int frameSizeBytes = ASICamera::FrameSize(roiWidth, roiHeight, ASI_IMG_RAW8);
    RingBuffer ring;
    ring.Allocate(DEFAULT_RING_SIZE, frameSizeBytes);
    CaptureThread capture;
    capture.Start(cam, ring, 8.0);
    Log("[INIT] Capture thread started\n");

    // ── USB traffic manager (M7) ───────────────────────────────
    USBTrafficManager usbMgr;
    Log("[INIT] USB traffic manager ready\n");

    // ── Frame analyzer (M9) ────────────────────────────────────
    FrameAnalyzer analyzer;
    analyzer.Start();
    Log("[INIT] Frame analyzer started\n");

    // ── Centroid tracker (Phase 5) ─────────────────────────────
    CentroidTracker tracker;
    tracker.SetMount(mount);
    tracker.SetLostCallback([&]() {
        Log("[TRACK] Centroid lost — planet may have drifted out of frame\n");
    });
    Log("[INIT] Centroid tracker ready\n");

    // ── WebSocket telemetry server (M12) ───────────────────────
    WebSocketServer wsServer;
    APEXTelemetry   telemetry;
    std::vector<SessionEvent> pendingEvents; // accumulates between broadcasts
    std::mutex eventsMutex;

    wsServer.Start(8765);
    Log("[INIT] WebSocket server started on :8765\n");

    // ── Capture manager ────────────────────────────────────────
    CaptureManager capturer;
    capturer.SetSessionLog(&slog);
    {
        CaptureConfig cfg;
        cfg.outputDir        = "D:\\RadukCapture\\captures";
        cfg.filePrefix       = "Jupiter";
        cfg.limitType        = LimitType::Frames;
        cfg.limitFrames      = 1000;
        cfg.limitTimeSec     = 60.0;
        cfg.writeBufferSlots = 512;
        capturer.SetConfig(cfg);
    }
    Log("[INIT] Capture manager ready\n");

    // ── Session summary accumulators ───────────────────────────
    // Accumulate across all capture runs; written to log at shutdown.
    uint32_t summaryFramesRecorded = 0;
    int      summaryFramesDropped  = 0;
    double   summaryElapsedSec     = 0.0;
    uint64_t summaryBytesWritten   = 0;
    int      summaryRunCount       = 0;
    float    summarySeeingAcc      = 0.0f;
    int      summarySeeingCount    = 0;
    bool     summaryWasRecording   = false;

    // ── Window ─────────────────────────────────────────────────
    Window window;
    wchar_t title[128];
    swprintf(title, 128, L"RadukCapture v1.0.5%s",
             useEmulator ? L" [EMULATOR]" : L"");
    if (!window.Create(title, screenW, screenH)) {
        Log("[FATAL] Window creation failed\n");
        analyzer.Stop(); capture.Stop(); cam.Close(); return 1;
    }

    // ── APEX remote command queue ──────────────────────────────
    // WS thread enqueues commands; main thread drains in WM_USER+1 handler.
    // Using a queue avoids the lParam-forwarding limitation of
    // SetUserMessageHandler — string args are safe across the thread boundary.
    struct RemoteCmd { std::string cmd; std::string arg; };
    std::vector<RemoteCmd> remoteCmdQueue;
    std::mutex             remoteCmdMutex;

    wsServer.SetCommandCallback([&](const std::string& json) {
        APEXCommand cmd;
        if (!APEXCommand::Parse(json, cmd)) return;
        Log("[WS] Command: %s arg=%s\n", cmd.cmd.c_str(), cmd.arg.c_str());
        {
            std::lock_guard<std::mutex> lk(remoteCmdMutex);
            remoteCmdQueue.push_back({ cmd.cmd, cmd.arg });
        }
        PostMessage(window.Handle(), WM_USER+1, 0, 0); // ping main thread
    });

    // ── Control panel ──────────────────────────────────────────
    ControlPanel controls;
    controls.Create(window.Handle(), &cam,
                    focuser.IsOpen()    ? &focuser : nullptr,
                    mount && mount->IsConnected() ? mount : nullptr);
    controls.SnapToMainWindow();

    // ── Toolbar strip (Win32 native buttons above preview) ─────
    raduk::gui::ToolbarStrip toolbar;
    toolbar.Create(window.Handle(), PANEL_WIDTH);

    // ── GoTo Panel (Phase 6) ───────────────────────────────────
    raduk::gui::GoToPanel gotoPanel;
    gotoPanel.SetMount(mount);
    gotoPanel.SetLocation(33.86, -112.09); // Anthem, AZ

    gotoPanel.Create(window.Handle(), 0, 0, 500, 750);

    // ── Sky Map (Phase 6.1) ────────────────────────────────────
    // Positioned to the right of the GoTo panel
    // Click any object → GoTo fires automatically
    raduk::gui::SkyMapWindow skyMap;
    skyMap.SetMount(mount);
    {
        // Always open centered on the primary monitor
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        int mw = 620, mh = 640;
        int mx = (sw - mw) / 2;
        int my = (sh - mh) / 2;
        skyMap.Create(window.Handle(), mx, my, mw, mh);
    }
    skyMap.Hide(); // hidden by default — M key or [★ MAP] to show

    // Click-to-GoTo: sky map click → GoToPanel.RequestGoTo
    // This triggers profile auto-load + mount slew + all status updates
    // Same code path as clicking GOTO SELECTED in the GoTo panel
    skyMap.SetGoToCallback([&](double ra, double dec, const std::string& name) {
        Log("[SKYMAP] GoTo: %s (%.4fh, %.4f\u00B0)\n", name.c_str(), ra, dec);
        slog.Mount("SkyMap GoTo: %s", name.c_str());
        gotoPanel.RequestGoTo(ra, dec, name);
    });

    // Phase 50a: Object Info Panel — shown on right-click (sky map) or
    // double-click (Tonight planner)
    raduk::gui::ObjectInfoPanel objectInfo;
    objectInfo.Create(window.Handle());
    // DB wired after aiChat is created below — see "Phase 50a DB wire" comment
    // GoTo from info panel — same path as sky map GoTo
    objectInfo.SetGoToCallback([&](const std::string& name) {
        gotoPanel.RequestGoToByName(name);
    });
    // Wire sky map right-click → info panel
    skyMap.SetInfoCallback([&](const std::string& name,
                               float alt, float az, float mag) {
        objectInfo.ShowObject(name, alt, az, mag);
    });
    // Wire Tonight planner double-click → info panel
    gotoPanel.SetInfoCallback([&](const std::string& name,
                                  float alt, float az, float mag) {
        objectInfo.ShowObject(name, alt, az, mag);
    });

    gotoPanel.SetGoToCompleteCallback([&](const std::string& target) {
        Log("[GOTO] Slew to '%s' complete — starting acquisition in 3s\n",
            target.c_str());
        slog.Mount("GoTo complete: %s — acquisition armed", target.c_str());

        tracker.Reset();
        controls.SetSkyMapLocked(false);  // clear stale lock from previous target

        std::thread([&, target]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(3000));
            PostMessage(window.Handle(), WM_USER + 10, 0, 0);
        }).detach();
    });

    // Auto-load matching camera profile when GoTo is issued
    // Matches by name prefix (case-insensitive):
    //   "Jupiter" / "M*"  → exact profile name match
    //   Messier objects   → DSO profile
    //   Unknown           → no profile change
    gotoPanel.SetProfileAutoLoadCallback([&](const std::string& targetName) {
        // Find best matching profile by name
        int bestIdx = -1;

        // 1. Try exact name match first (e.g. "Jupiter", "Saturn")
        for (int i = 0; i < (int)profiles.Profiles().size(); i++) {
            const auto& p = profiles.Profiles()[i];
            if (_stricmp(p.name.c_str(), targetName.c_str()) == 0) {
                bestIdx = i;
                break;
            }
        }

        // 2. For non-planet targets, match by profile name keywords
        if (bestIdx < 0) {
            bool isMoon   = (targetName.find("Moon") != std::string::npos);
            bool isMessier = (targetName.size() > 1
                              && targetName[0] == 'M'
                              && targetName[1] >= '0' && targetName[1] <= '9');

            // Find profile whose name contains "Moon" or "DSO"
            std::string wantName = "";
            if (isMoon)         wantName = "Moon";
            else if (isMessier) wantName = "DSO";

            if (!wantName.empty()) {
                for (int i = 0; i < (int)profiles.Profiles().size(); i++) {
                    if (_stricmp(profiles.Profiles()[i].name.c_str(),
                                 wantName.c_str()) == 0) {
                        bestIdx = i;
                        break;
                    }
                }
            }
        }

        if (bestIdx >= 0) {
            const TargetProfile& p = profiles.Profiles()[bestIdx];
            Log("[PROFILE] Auto-loading '%s' profile for GoTo target '%s'\n",
                p.name.c_str(), targetName.c_str());
            slog.Profile("Auto: %s → %s", targetName.c_str(), p.name.c_str());
            ApplyProfile(p, &cam,
                focuser.IsOpen() ? &focuser : nullptr,
                mount, slog);
            controls.SetActiveProfile(bestIdx);
            profiles.SetActive(bestIdx);
        } else {
            Log("[PROFILE] No matching profile for '%s' — keeping current settings\n",
                targetName.c_str());
        }
    });
    gotoPanel.SetSyncCallback([&](double ra, double dec) {
        Log("[GOTO] Sync: RA=%.4fh Dec=%.4f\u00B0\n", ra, dec);
        slog.Mount("Sync: RA=%.4fh Dec=%.4f\u00B0", ra, dec);
    });
    Log("[INIT] GoTo panel ready (G key to toggle)\n");

    // ── Floating Mount Controller (Safety Panel) ───────────────
    // Always-on-top WS_EX_TOPMOST window — visible above all apps.
    // Lets you stop the mount even when RadukCapture is not focused.
    raduk::gui::MountController mountCtrl;
    mountCtrl.SetMount(mount);
    mountCtrl.SetJogCallback([&](int axis, int dir, bool start) {
        if (!mount || !mount->IsConnected()) return;
        if (start) {
            mount->SlewFixed(axis, dir, 9);
            slog.Mount("MountCtrl: jog axis=%d dir=%d start", axis, dir);
        } else {
            mount->StopSlew(axis);
            slog.Mount("MountCtrl: jog stop axis=%d", axis);
        }
    });
    mountCtrl.SetEmergencyStopCallback([&]() {
        Log("[SAFETY] MountCtrl: STOP pressed\n");
        slog.Mount("Emergency stop (MountCtrl)");
    });

    mountCtrl.SetAlignCallback([&](double ra_h, double dec_deg,
                                    const std::string& name) {
        Log("[ALIGN] Syncing to %s (RA=%.4fh Dec=%.4f°)\n",
            name.c_str(), ra_h, dec_deg);
        slog.Mount("Align sync: %s RA=%.4fh Dec=%.4f°",
                   name.c_str(), ra_h, dec_deg);
        // Tell mount where it's actually pointing
        if (mount && mount->IsConnected())
            mount->SyncRADec(ra_h, dec_deg);
        // Update sky map crosshair to true position
        skyMap.SetMountPosition(ra_h, dec_deg, true);
        // Flash confirmation in notification bar
        notifBar.NotifyInfo(
            std::wstring(L"\u2605 Aligned to ") +
            std::wstring(name.begin(), name.end()));
    });
    mountCtrl.Create(window.Handle());
    mountCtrl.Show();  // visible by default - safety first
    Log("[INIT] Mount controller ready (always on top)\n");

    // Phase 49: Help Window
    raduk::gui::HelpWindow helpWindow;
    {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring exeDir(exePath);
        auto slash = exeDir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) exeDir = exeDir.substr(0, slash);
        helpWindow.Create(window.Handle(), exeDir);
    }
    Log("[INIT] Help window ready\n");

    // Phase 50: AI Chat Panel
    raduk::gui::AIChatPanel aiChat;
    {
        wchar_t exePath2[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath2, MAX_PATH);
        std::wstring exeDir2(exePath2);
        auto slash2 = exeDir2.find_last_of(L"\\/");
        if (slash2 != std::wstring::npos) exeDir2 = exeDir2.substr(0, slash2);
        aiChat.Create(window.Handle(), exeDir2);
    }
    Log("[INIT] AI chat panel ready\n");
    // Phase 50a DB wire — both AI panel and object info panel share the same DB
    objectInfo.SetKnowledgeDB(&aiChat.GetKnowledgeDB());

    // Phase 40: Notification Bar
    raduk::gui::NotificationBar notifBar;
    notifBar.Create(window.Handle(), PANEL_WIDTH);
    notifBar.SetDismissCallback([&]() {
        window.SetNotifBarHeight(notifBar.Height());
        notifBar.SnapToMainWindow();
    });
    Log("[INIT] Notification bar ready\n");

    // RC is always in control - no CPWI. Set initial state before callbacks wired.
    if (startInRCControl) {
        controls.SetRCControl(true);
        Log("[INIT] RC in control (NexStar direct)\n");
    }

    {
        std::vector<std::string> names;
        for (const auto& p : profiles.Profiles()) names.push_back(p.name);
        controls.SetProfileNames(names);
        controls.SetActiveProfile(0);
    }

    controls.SetProfileLoadCallback([&](int index) {
        profiles.SetActive(index);
        const TargetProfile* p = profiles.Active();
        if (!p) return;
        ApplyProfile(*p, &cam,
                     focuser.IsOpen()    ? &focuser : nullptr,
                     mount && mount->IsConnected() ? mount : nullptr, slog);
        controls.SetActiveProfile(index);
        CaptureConfig cfg = capturer.Config();
        cfg.filePrefix = p->filePrefix.empty() ? p->name : p->filePrefix;
        capturer.SetConfig(cfg);
        // Restore profile notes into the NOTES panel
        controls.SetNotes(p->notes);
    });

    controls.SetProfileSaveCallback([&](int index) {
        TargetProfile* p = profiles.ProfileAt(index);
        if (!p) return;
        // Write current notes box back into profile before saving
        p->notes = controls.GetNotes();
        profiles.Save(*p);
        slog.Profile("Saved: %s", p->name.c_str());
    });

    // ── Profile editor modal (Phase 9/10 — EDIT button) ───────────
    // Builds a WS_POPUP dialog window entirely in code (no .rc resource).
    // Shows all TargetProfile fields as labeled edit boxes.
    // SAVE & CLOSE writes back to the profile and persists to disk.
    controls.SetProfileEditCallback([&](int index) {
        if (index < 0 || index >= (int)profiles.Profiles().size()) return;

        // We need a mutable copy to edit
        TargetProfile prof = profiles.Profiles()[index];

        // ── Dialog dimensions ─────────────────────────────────
        const int DW = 420, DH = 520;
        const int LW = 130, EW = 250, EH = 22;
        const int PX = 12, PY = 10, ROWH = 28;

        // Centre over main window
        RECT wr; GetWindowRect(window.Handle(), &wr);
        int dlgX = wr.left + (wr.right  - wr.left - DW) / 2;
        int dlgY = wr.top  + (wr.bottom - wr.top  - DH) / 2;

        // ── Create the dialog window ──────────────────────────
        HWND hDlg = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
            L"STATIC",     // placeholder; we subclass immediately
            L"Edit Profile",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
            dlgX, dlgY, DW, DH,
            window.Handle(), nullptr, GetModuleHandle(nullptr), nullptr);
        if (!hDlg) return;

        // Dark background
        SetClassLongPtrW(hDlg, GCLP_HBRBACKGROUND,
            (LONG_PTR)CreateSolidBrush(RGB(10, 21, 32)));

        // ── Helpers ───────────────────────────────────────────
        auto MkLabel = [&](const wchar_t* txt, int x, int y, int w, int h) -> HWND {
            HWND h2 = CreateWindowExW(0, L"STATIC", txt, WS_CHILD | WS_VISIBLE | SS_LEFT,
                x, y, w, h, hDlg, nullptr, GetModuleHandle(nullptr), nullptr);
            return h2;
        };
        auto MkEdit = [&](const wchar_t* txt, int x, int y, int w, int h, int id) -> HWND {
            HWND h2 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", txt,
                WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
                x, y, w, h, hDlg, (HMENU)(INT_PTR)id, GetModuleHandle(nullptr), nullptr);
            return h2;
        };

        // ── Build rows ────────────────────────────────────────
        // Each row: label on left (LW px), edit box (EW px), 1 row apart ROWH px
        int y = PY;

        enum EditIds {
            ED_NAME=3001, ED_GAIN, ED_EXP, ED_GAMMA,
            ED_WBR, ED_WBB, ED_USB, ED_FOCUS,
            ED_LIMIT, ED_PREFIX, ED_NOTES,
            BTN_SAVE=3100, BTN_CANCEL
        };

        wchar_t tmp[64];
        struct Row { const wchar_t* lbl; HWND edit; } rows[11];
        int ri = 0;

        // Name
        MkLabel(L"Profile Name:", PX, y+3, LW, EH);
        wchar_t wName[64] = {}; MultiByteToWideChar(CP_UTF8, 0, prof.name.c_str(), -1, wName, 64);
        rows[ri++] = {L"Profile Name:", MkEdit(wName, PX+LW, y, EW, EH, ED_NAME)};
        y += ROWH;

        // Gain
        MkLabel(L"Gain (0-570):", PX, y+3, LW, EH);
        swprintf(tmp, 64, L"%d", prof.gain);
        rows[ri++] = {L"Gain:", MkEdit(tmp, PX+LW, y, EW, EH, ED_GAIN)};
        y += ROWH;

        // Exposure ms
        MkLabel(L"Exposure (ms):", PX, y+3, LW, EH);
        swprintf(tmp, 64, L"%.3f", prof.exposureMs);
        rows[ri++] = {L"Exposure ms:", MkEdit(tmp, PX+LW, y, EW, EH, ED_EXP)};
        y += ROWH;

        // Gamma
        MkLabel(L"Gamma (1-100):", PX, y+3, LW, EH);
        swprintf(tmp, 64, L"%d", prof.gamma);
        rows[ri++] = {L"Gamma:", MkEdit(tmp, PX+LW, y, EW, EH, ED_GAMMA)};
        y += ROWH;

        // WB Red
        MkLabel(L"WB Red (0-100):", PX, y+3, LW, EH);
        swprintf(tmp, 64, L"%d", prof.wbRed);
        rows[ri++] = {L"WB Red:", MkEdit(tmp, PX+LW, y, EW, EH, ED_WBR)};
        y += ROWH;

        // WB Blue
        MkLabel(L"WB Blue (0-100):", PX, y+3, LW, EH);
        swprintf(tmp, 64, L"%d", prof.wbBlue);
        rows[ri++] = {L"WB Blue:", MkEdit(tmp, PX+LW, y, EW, EH, ED_WBB)};
        y += ROWH;

        // USB Bandwidth
        MkLabel(L"USB Bandwidth %:", PX, y+3, LW, EH);
        swprintf(tmp, 64, L"%d", prof.usbBandwidth);
        rows[ri++] = {L"USB BW:", MkEdit(tmp, PX+LW, y, EW, EH, ED_USB)};
        y += ROWH;

        // Focus position
        MkLabel(L"Focus Position:", PX, y+3, LW, EH);
        swprintf(tmp, 64, L"%d", prof.focusPosition);
        rows[ri++] = {L"Focus Pos:", MkEdit(tmp, PX+LW, y, EW, EH, ED_FOCUS)};
        y += ROWH;

        // Limit frames
        MkLabel(L"Limit Frames:", PX, y+3, LW, EH);
        swprintf(tmp, 64, L"%d", prof.limitValue);
        rows[ri++] = {L"Limit Frames:", MkEdit(tmp, PX+LW, y, EW, EH, ED_LIMIT)};
        y += ROWH;

        // File prefix
        MkLabel(L"File Prefix:", PX, y+3, LW, EH);
        wchar_t wPfx[64] = {}; MultiByteToWideChar(CP_UTF8, 0, prof.filePrefix.c_str(), -1, wPfx, 64);
        rows[ri++] = {L"File Prefix:", MkEdit(wPfx, PX+LW, y, EW, EH, ED_PREFIX)};
        y += ROWH;

        // Notes
        MkLabel(L"Notes:", PX, y+3, LW, EH);
        wchar_t wNotes[256] = {}; MultiByteToWideChar(CP_UTF8, 0, prof.notes.c_str(), -1, wNotes, 256);
        rows[ri++] = {L"Notes:", MkEdit(wNotes, PX+LW, y, EW, EH, ED_NOTES)};
        y += ROWH + 6;

        // ── Buttons ───────────────────────────────────────────
        const int BW = 120, BH2 = 28;
        CreateWindowExW(0, L"BUTTON", L"SAVE && CLOSE",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            PX, y, BW, BH2, hDlg,
            (HMENU)(INT_PTR)BTN_SAVE, GetModuleHandle(nullptr), nullptr);
        CreateWindowExW(0, L"BUTTON", L"CANCEL",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            PX + BW + 12, y, BW, BH2, hDlg,
            (HMENU)(INT_PTR)BTN_CANCEL, GetModuleHandle(nullptr), nullptr);

        // Apply dark font to all children
        HFONT hFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Barlow Condensed");
        if (hFont) {
            EnumChildWindows(hDlg, [](HWND child, LPARAM lp) -> BOOL {
                SendMessage(child, WM_SETFONT, (WPARAM)lp, FALSE);
                return TRUE;
            }, (LPARAM)hFont);
        }

        // ── Modal loop — subclassed WndProc ───────────────────
        // Root causes of the old freeze+slew bug:
        //   1. WM_COMMAND from child buttons is dispatched by DefWindowProc to
        //      hDlg — the raw GetMessage loop never saw it on the right hwnd.
        //   2. Only the main window was disabled; PanelWndProc still forwarded
        //      arrow keys to the main window → accidental mount jog.
        // Fix: SetWindowSubclass on hDlg to catch WM_COMMAND correctly.
        //      Disable BOTH the main window and the panel for the duration.

        bool saved = false;

        struct DlgState { bool* pSaved; bool done; };
        DlgState dlgState { &saved, false };
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)&dlgState);

        struct DlgSubclass {
            static LRESULT CALLBACK Proc(HWND hw, UINT m, WPARAM wp, LPARAM lp,
                                         UINT_PTR, DWORD_PTR) {
                auto* st = reinterpret_cast<DlgState*>(
                               GetWindowLongPtrW(hw, GWLP_USERDATA));
                if (m == WM_COMMAND) {
                    int id   = LOWORD(wp);
                    int note = HIWORD(wp);
                    if (note == BN_CLICKED && (id == 3100 || id == 3101)) {
                        if (st) { *(st->pSaved) = (id == 3100); st->done = true; }
                        return 0;
                    }
                }
                if (m == WM_KEYDOWN && wp == VK_ESCAPE) {
                    if (st) { *(st->pSaved) = false; st->done = true; }
                    return 0;
                }
                if (m == WM_CLOSE) {
                    if (st) { *(st->pSaved) = false; st->done = true; }
                    return 0;
                }
                return DefSubclassProc(hw, m, wp, lp);
            }
        };
        SetWindowSubclass(hDlg, DlgSubclass::Proc, 1, 0);

        // Disable main window AND panel — prevents jog key forwarding
        EnableWindow(window.Handle(),      FALSE);
        EnableWindow(controls.PanelHwnd(), FALSE);
        SetForegroundWindow(hDlg);

        MSG msg2 = {};
        while (!dlgState.done) {
            if (!GetMessageW(&msg2, nullptr, 0, 0)) break; // WM_QUIT — propagate
            if (!IsDialogMessageW(hDlg, &msg2)) {          // handles Tab/Enter/Esc
                TranslateMessage(&msg2);
                DispatchMessageW(&msg2);
            }
        }

        // ── Read back values if saved ─────────────────────────
        if (saved) {
            auto GetEditA = [&](int id) -> std::string {
                HWND h2 = GetDlgItem(hDlg, id);
                if (!h2) return "";
                wchar_t buf[256] = {};
                GetWindowTextW(h2, buf, 256);
                char out[256] = {};
                WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, 256, nullptr, nullptr);
                return std::string(out);
            };
            auto GetEditI = [&](int id, int def) -> int {
                std::string s = GetEditA(id);
                return s.empty() ? def : atoi(s.c_str());
            };
            auto GetEditF = [&](int id, double def) -> double {
                std::string s = GetEditA(id);
                return s.empty() ? def : atof(s.c_str());
            };

            prof.name          = GetEditA(ED_NAME);
            prof.gain          = GetEditI(ED_GAIN,  prof.gain);
            prof.exposureMs    = GetEditF(ED_EXP,   prof.exposureMs);
            prof.gamma         = GetEditI(ED_GAMMA, prof.gamma);
            prof.wbRed         = GetEditI(ED_WBR,   prof.wbRed);
            prof.wbBlue        = GetEditI(ED_WBB,   prof.wbBlue);
            prof.usbBandwidth  = GetEditI(ED_USB,   prof.usbBandwidth);
            prof.focusPosition = GetEditI(ED_FOCUS, prof.focusPosition);
            prof.limitValue    = GetEditI(ED_LIMIT, prof.limitValue);
            prof.filePrefix    = GetEditA(ED_PREFIX);
            prof.notes         = GetEditA(ED_NOTES);

            // Clamp values to valid ranges
            prof.gain          = std::max(0,      std::min(570,    prof.gain));
            prof.exposureMs    = std::max(0.001,  std::min(60000.0,prof.exposureMs));
            prof.gamma         = std::max(1,      std::min(100,    prof.gamma));
            prof.wbRed         = std::max(0,      std::min(100,    prof.wbRed));
            prof.wbBlue        = std::max(0,      std::min(100,    prof.wbBlue));
            prof.usbBandwidth  = std::max(40,     std::min(100,    prof.usbBandwidth));
            prof.focusPosition = std::max(0,      std::min(600000, prof.focusPosition));

            // Write back into the profiles collection and persist
            // profiles.Profiles() returns const&; we save via Save() with the modified copy
            profiles.Save(prof);
            // Reload all so the in-memory copy matches disk
            profiles.LoadAll();

            // Refresh combo box names (name may have changed)
            {
                std::vector<std::string> names;
                for (const auto& p : profiles.Profiles()) names.push_back(p.name);
                controls.SetProfileNames(names);
                controls.SetActiveProfile(index);
            }

            slog.Profile("Edited: %s", prof.name.c_str());
            Log("[PROFILE] Edited and saved: %s\n", prof.name.c_str());
        }

        // Cleanup — re-enable both windows before destroying the dialog
        EnableWindow(controls.PanelHwnd(), TRUE);
        EnableWindow(window.Handle(),      TRUE);
        SetForegroundWindow(window.Handle());
        RemoveWindowSubclass(hDlg, DlgSubclass::Proc, 1);
        DestroyWindow(hDlg);
        if (hFont) DeleteObject(hFont);
    });

    // ── Status bar ─────────────────────────────────────────────
    StatusBar statusBar;
    statusBar.Init(window.RenderTarget(), window.DWriteFactory());
    statusBar.SetEmulatorMode(useEmulator);
    statusBar.SetProfile("Jupiter");

    // ── Preview renderer ───────────────────────────────────────
    PreviewRenderer preview;
    if (!preview.Init(window.RenderTarget(), window.DWriteFactory()) ||
        !preview.SetFrameSize(roiWidth, roiHeight)) {
        Log("[FATAL] PreviewRenderer init failed\n");
        analyzer.Stop(); capture.Stop(); cam.Close(); return 1;
    }
    preview.SetSensorROI(roiWidth, roiHeight);

    // ── Wire handlers ──────────────────────────────────────────
    window.SetMoveHandler([&]() {
        controls.OnMainWindowMoved();
        toolbar.SnapToMainWindow();
        notifBar.SnapToMainWindow();
        window.SetNotifBarHeight(notifBar.Height());
    });

    // Forward clicks to preview renderer for frame bar + ROI drag start
    window.SetClickHandler([&](float x, float y) -> bool {
        return preview.HandleMouseDown(x, y);
    });

    // Mouse move — feeds rubber-band drag
    window.SetMouseMoveHandler([&](float x, float y) {
        preview.HandleMouseMove(x, y);
        // Phase 24: feed hover position for pixel readout (convert screen→D2D coords)
        if (preview.PixelReadoutEnabled()) {
            float dpi = window.DpiScale();
            preview.SetPixelHover(x / dpi, y / dpi);
        }
    });

    // Phase 24: pixel click lock/unlock
    window.SetPixelClickHandler([&](float x, float y) {
        if (preview.PixelReadoutEnabled()) {
            float dpi = window.DpiScale();
            preview.ClickPixelLock(x / dpi, y / dpi);
        }
    });

    // Mouse up — completes ROI selection
    window.SetMouseUpHandler([&](float x, float y) {
        preview.HandleMouseUp(x, y);
    });

    // ROI change — stop capture, apply new ROI, restart everything
    preview.SetROIChangeCallback([&](int newW, int newH, int startX, int startY) {
        Log("[ROI] Applying %dx%d at (%d,%d)\n", newW, newH, startX, startY);
        slog.System("ROI: %dx%d  origin:(%d,%d)", newW, newH, startX, startY);

        bool wasRecording = capturer.IsRecording();
        if (wasRecording) {
            capturer.StopRecording();
            usbMgr.SetCapturing(false);
        }

        // Stop capture thread — required before SetROI
        capture.Stop();

        // Apply new ROI size then start position
        cam.SetROI(newW, newH, 1, ASI_IMG_RAW8);
        cam.SetStartPos(startX, startY);
        // Read back actual ROI — emulator may cap dimensions
        {
            int actualW = newW, actualH = newH, actualBin = 1;
            ASI_IMG_TYPE actualType = ASI_IMG_RAW8;
            cam.GetROI(actualW, actualH, actualBin, actualType);
            newW = actualW; newH = actualH;
        }
        roiWidth  = newW;
        roiHeight = newH;

        // Reallocate ring buffer for new frame size
        int newFrameBytes = ASICamera::FrameSize(newW, newH, ASI_IMG_RAW8);
        ring.Allocate(DEFAULT_RING_SIZE, newFrameBytes);

        // Restart capture
        capture.Start(cam, ring, 8.0);

        // Resize preview and GPU textures
        preview.SetFrameSize(newW, newH);
        preview.SetSensorROI(newW, newH);

        // Update status bar
        statusBar.SetCameraStats(0.0, 0, cam.IsOpen() ? cam.Temperature() : 0.0f,
                                  newW, newH, 8);

        Log("[ROI] Applied — %dx%d@(%d,%d)  %d bytes/frame\n",
            newW, newH, startX, startY, newFrameBytes);

        // Resume recording if it was active (new file at new ROI)
        if (wasRecording) {
            CaptureConfig cfg = capturer.Config();
            capturer.SetConfig(cfg);
            if (capturer.StartRecording(newW, newH, 8)) {
                usbMgr.SetCapturing(true);
            }
        }
    });

    // Reticle toggle — now lives on the frame bar. Keyboard R key still works.
    // preview.ToggleReticle() is the single source of truth for state.
    auto doReticle = [&]() {
        preview.ToggleReticle();
        slog.System("Reticle %s", preview.ReticleVisible() ? "ON" : "OFF");
    };
    auto doZoom     = [&]() { preview.CycleZoom(); controls.SetZoomLabel(preview.ZoomLabel()); };
    auto doZoomStep = [&](int dir) { preview.ZoomStep(dir); controls.SetZoomLabel(preview.ZoomLabel()); };
    auto doFullscreen = [&]() { window.ToggleFullscreen(); };

    // ── Record toggle — also notifies USB manager ──────────────
    auto doRecord = [&]() {
        if (capturer.IsRecording()) {
            capturer.StopRecording();
            usbMgr.SetCapturing(false);
            slog.System("USB polling restored — %s", usbMgr.StatusString());
            Log("[USB] Polling restored: %s\n", usbMgr.StatusString());
        } else {
            CaptureConfig cfg = capturer.Config();
            cfg.filePrefix    = controls.GetFilePrefix();
            cfg.outputDir     = controls.GetOutputDir();
            cfg.frameType     = controls.GetFrameType();  // Phase 44
            cfg.limitFrames   = controls.GetLimitFrames();
            cfg.limitTimeSec  = controls.GetLimitTimeSec();
            cfg.limitType     = controls.GetLimitByFrames()
                                    ? LimitType::Frames : LimitType::Time;
            capturer.SetConfig(cfg);
            CreateDirectoryA(cfg.outputDir.c_str(), nullptr);

            if (capturer.StartRecording(roiWidth, roiHeight, 8)) {
                usbMgr.SetCapturing(true);
                slog.System("USB capture mode — %s", usbMgr.StatusString());
                Log("[USB] Throttling EAF+mount: %s\n", usbMgr.StatusString());
                // Phase 25: disable FX on recording start — capture raw frames
                if (preview.GetDisplayFX() != DisplayFX::Normal) {
                    preview.SetDisplayFX(DisplayFX::Normal);
                    controls.SetFXLabel(L"[FX] NORMAL");
                    Log("[FX] Reset to NORMAL for capture\n");
                }
            }
        }
        // Immediately sync the button visual — don't wait for the 500ms readout tick.
        // UpdateCaptureStatus sets m_status.recording and invalidates the owner-drawn
        // button; without this the button stays stale until the next readout fires.
        controls.ForceRecordButtonUpdate(capturer.IsRecording());
    };

    controls.SetZoomCycleCallback(doZoom);
    controls.SetFullscreenToggleCallback(doFullscreen);
    controls.SetRecordStartCallback(doRecord);
    controls.SetDockToggleCallback([&]() {});

    // ── Phase 9: Autofocus ────────────────────────────────────
    // Completion callback — fires from scan thread, no Win32 UI calls here
    autofocus.SetOnComplete([&](raduk::core::AFResult result) {
        controls.SetAutofocusRunning(false);
        if (result.success) {
            wchar_t buf[128];
            swprintf(buf, 128, L"AF done: pos=%d  sharp=%.1f%%  +%.1f%%",
                     result.bestPosition, result.bestSharpness, result.improvement);
            controls.SetAutofocusStatus(buf);
            slog.Focuser("AutoFocus: pos=%d  sharp=%.1f%%  +%.1f%%",
                         result.bestPosition, result.bestSharpness, result.improvement);
            Log("[AF] Complete: pos=%d  sharp=%.1f%%  improvement=+%.1f%%\n",
                result.bestPosition, result.bestSharpness, result.improvement);
        } else {
            wchar_t buf[128];
            MultiByteToWideChar(CP_UTF8, 0, result.message.c_str(), -1, buf, 128);
            controls.SetAutofocusStatus(buf);
            slog.Focuser("AutoFocus failed: %s", result.message.c_str());
            Log("[AF] Failed: %s\n", result.message.c_str());
        }
    });

    // Button callback — toggles start/abort
    controls.SetAutofocusCallback([&](bool start) {
        if (start) {
            if (!focuser.IsOpen()) {
                controls.SetAutofocusStatus(L"No focuser connected");
                return;
            }
            int pos = focuser.GetPosition();
            Log("[AF] Starting V-curve scan from pos=%d\n", pos);
            slog.Focuser("AutoFocus: starting from pos=%d", pos);
            controls.SetAutofocusStatus(L"Starting...");
            controls.SetAutofocusRunning(true);
            autofocus.Start(pos);
        } else {
            Log("[AF] Abort requested\n");
            autofocus.Abort();
            controls.SetAutofocusRunning(false);
            controls.SetAutofocusStatus(L"Aborted");
        }
    });

    // MOUNT CTRL button in panel - shows floating controller at default position
    controls.SetMountCtrlCallback([&]() {
        mountCtrl.ShowAtDefault();
        Log("[INIT] Mount controller repositioned\n");
    });

    // ── Phase 12: Auto-Exposure Assistant ─────────────────────
    raduk::core::AutoExposureAssistant ae;
    {
        raduk::core::AutoExposureAssistant::Config aeCfg;
        // ASI678MC HCG floor — never go below 182 in auto mode
        aeCfg.gainMin   = raduk::profiles::CameraRails::GAIN_HCG_THRESHOLD;
        aeCfg.gainMax   = 570;
        aeCfg.expMinMs  = 0.5;
        aeCfg.expMaxMs  = 40.0;
        ae.SetConfig(aeCfg);
    }
    controls.SetAECallback([&](bool enabled, bool autoApply) {
        ae.SetEnabled(enabled);
        if (enabled) ae.SetAuto(autoApply);
        const wchar_t* msg = !enabled    ? L"AE: off"
                           : !autoApply  ? L"AE: advisory"
                           :               L"AE: auto";
        controls.SetAEStatus(msg);
        Log("[AE] %s\n", !enabled ? "off" : (autoApply ? "AUTO" : "advisory"));
    });
    Log("[INIT] Auto-exposure assistant ready\n");
    // Stage buttons live in the frame bar. Main thread owns all transitions.
    // Jog suspend: corrections pause during mount jog + 500ms settle.
    // ESC: Reset() wired in emergency stop handler below.

    // Phase 6.6: frame snapshot for auto-solve on Stage 3 lock.
    // Declared here so the ROILock lambda (below) can capture by reference.
    // Updated every render tick to hold the latest RAW8 frame.
    std::vector<uint8_t> lastFrameForSolve;
    uint32_t lastFrameSolveW = 0, lastFrameSolveH = 0;

    // lastAnalysis also captured by ROILock lambda — must be declared before it
    AnalysisResult lastAnalysis{};

    // Phase 13: Survey frame delivery — wired here so lastFrameForSolve/lastAnalysis are in scope
    gotoPanel.SetSurveyFrameCallback([&]() {
        if (lastFrameForSolve.empty() || lastFrameSolveW == 0) {
            Log("[SURVEY] Frame not ready for solve\n");
            return;
        }
        gotoPanel.SurveyProvideFrame(
            lastFrameForSolve.data(),
            lastFrameSolveW, lastFrameSolveH,
            cam.IsOpen() ? cam.Temperature() : 0.0f,
            lastAnalysis.valid ? lastAnalysis.seeingScore : 0.0f);
    });

    // [CENTROID] button — toggles searching on/off
    // Centroid/Lock/ROI callbacks now wired to toolbar buttons above.

    // doROILock — named lambda so it can be called from:
    //   a) [ROI] frame bar button press
    //   b) Auto-advance in the render loop (post-GoTo flow)
    //   c) Remote APEX command
    auto doROILock = [&]() {
        if (tracker.Stage() == raduk::core::AcquisitionStage::Centering && tracker.IsDwellMet()) {
            Log("[ACQ] Stage 3: LOCKED\n");
            slog.Mount("Acquisition: Locked");
            tracker.StartLocked();

            // Show SKYMAP LOCKED indicator with current target name
            {
                const TargetProfile* active = profiles.Active();
                std::wstring tgt;
                if (active && !active->name.empty()) {
                    tgt.assign(active->name.begin(), active->name.end());
                }
                controls.SetSkyMapLocked(true, tgt);
            }

            float cx = tracker.LastCentroidX();
            float cy = tracker.LastCentroidY();
            if (tracker.LastCentroidValid() && cx > 0 && cy > 0) {
                int newW = 1280, newH = 960;
                int startX = (int)(cx - newW * 0.5f);
                int startY = (int)(cy - newH * 0.5f);
                startX = std::max(0, std::min(startX, 3840 - newW));
                startY = std::max(0, std::min(startY, 2160 - newH));
                startX = (startX / 8) * 8;
                startY = (startY / 2) * 2;
                Log("[ACQ] ROI crop -> %dx%d @ (%d,%d)\n", newW, newH, startX, startY);
                slog.System("ROI: %dx%d centred on planet", newW, newH);

                bool wasRecording = capturer.IsRecording();
                if (wasRecording) { capturer.StopRecording(); usbMgr.SetCapturing(false); }
                capture.Stop();
                cam.SetROI(newW, newH, 1, ASI_IMG_RAW8);
                cam.SetStartPos(startX, startY);
                // Read back actual ROI — emulator may cap dimensions
                {
                    int aW=newW,aH=newH,aB=1; ASI_IMG_TYPE aT=ASI_IMG_RAW8;
                    cam.GetROI(aW,aH,aB,aT); newW=aW; newH=aH;
                }
                roiWidth = newW; roiHeight = newH;
                int newBytes = ASICamera::FrameSize(newW, newH, ASI_IMG_RAW8);
                ring.Allocate(DEFAULT_RING_SIZE, newBytes);
                capture.Start(cam, ring, 8.0);
                preview.SetFrameSize(newW, newH);
                preview.SetSensorROI(newW, newH);
                statusBar.SetCameraStats(0.0, 0, cam.Temperature(), newW, newH, 8);
                if (wasRecording) {
                    if (capturer.StartRecording(newW, newH, 8)) usbMgr.SetCapturing(true);
                }

                if (!lastFrameForSolve.empty() && lastFrameSolveW > 0) {
                    const TargetProfile* active = profiles.Active();
                    std::string target = active ? active->name : "Unknown";
                    float camTemp   = cam.Temperature();
                    float seeingScr = lastAnalysis.valid ? lastAnalysis.seeingScore : 0.0f;
                    gotoPanel.AutoSolveOnLock(
                        lastFrameForSolve.data(),
                        lastFrameSolveW, lastFrameSolveH,
                        target, camTemp, seeingScr);
                    slog.Mount("AutoSync: triggered for '%s'", target.c_str());
                }
            }
        }
    };

    // Wire [ROI] button to the shared lambda
    // ROI lock callback now wired to toolbar.SetROICallback above.

    // Centroid lost callback — reset all stages, restore full frame
    tracker.SetLostCallback([&]() {
        Log("[ACQ] Centroid lost — resetting to Off\n");
        slog.Mount("Acquisition: Centroid lost — reset");
        tracker.Reset();
        controls.SetSkyMapLocked(false);
        // Note: ROI is NOT automatically restored here — keeps last crop.
    });

    // Dwell met callback — just log; UI already reflects IsDwellMet()
    tracker.SetDwellCallback([&]() {
        Log("[ACQ] Dwell condition met — ROI button now active\n");
        slog.Mount("Acquisition: Planet centred — ROI ready");
    });

    // Phase 6.6: auto-solve callback — fires from worker thread after Stage 3 lock
    // Updates session log and the sidebar status flash (thread-safe string write)
    gotoPanel.SetAutoSolveCallback([&](bool success, const std::string& obj,
                                       double errArcsec, const std::string& msg) {
        if (success) {
            slog.Mount("AutoSync: %s  error=%.0f\"  syncs=%d",
                       obj.c_str(), errArcsec, gotoPanel.AutoSyncCount());
            Log("[ACQ] AutoSync OK: %s  %.0f\"  model=%d entries\n",
                obj.c_str(), errArcsec, gotoPanel.PointingModelSize());
            // Flash the status message on the GoTo panel if visible
            if (gotoPanel.IsVisible()) {
                wchar_t wmsg[64];
                MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, wmsg, 64);
                // Post to main thread — SetWindowTextW is not worker-thread safe
                PostMessageW(window.Handle(), WM_USER + 2,
                             (WPARAM)0, (LPARAM)0);
            }
        } else {
            slog.Mount("AutoSync: %s  solve failed", obj.c_str());
            Log("[ACQ] AutoSync failed for %s\n", obj.c_str());
        }
    });

    window.SetEmergencyStopHandler([&]() {
        if (capturer.IsRecording()) {
            capturer.StopRecording();
            usbMgr.SetCapturing(false);
        }
        autofocus.Abort();                // Phase 9: stop any running AF scan
        controls.SetAutofocusRunning(false);
        tracker.Reset();  // stops all stages, joins correction thread
        controls.EmergencyStop();
        slog.Safety("EMERGENCY STOP");
    });
    window.SetReticleToggleHandler([&]()    { doReticle();          });
    window.SetClipToggleHandler([&]()       { preview.ToggleClip(); });
    window.SetQualityGraphToggleHandler([&]() { preview.ToggleQualityGraph(); });
    window.SetPauseHandler([&]() {
        if (!capturer.IsRecording()) return;
        capturer.TogglePause();
        bool paused = capturer.IsPaused();
        controls.SetPausedState(paused);
        slog.System(paused ? "Recording paused" : "Recording resumed");
        Log("[REC] %s\n", paused ? "PAUSED" : "RESUMED");
    });

    // Phase 23 — RGB Align: V key or panel button toggles, slider sets manual offset
    window.SetRGBAlignHandler([&]() {
        preview.ToggleRGBAlign();
        bool on = preview.RGBAlignEnabled();
        controls.SetRGBAlignActive(on);
        Log("[RGB] Align %s\n", on ? "ON" : "OFF");
    });
    controls.SetRGBAlignCallback([&]() {
        preview.ToggleRGBAlign();
        bool on = preview.RGBAlignEnabled();
        controls.SetRGBAlignActive(on);
        Log("[RGB] Align %s\n", on ? "ON" : "OFF");
    });

    // Phase 25: B key and panel button cycle Display FX
    auto doFXCycle = [&]() {
        preview.CycleDisplayFX();
        const wchar_t* lbl = preview.DisplayFXLabel();
        wchar_t wbuf[32]; swprintf(wbuf, 32, L"[FX] %s", lbl);
        controls.SetFXLabel(wbuf);
        Log("[FX] %ls\n", lbl);
    };
    window.SetFXCycleHandler([&]() { doFXCycle(); });
    controls.SetFXCycleCallback([&]() { doFXCycle(); });

    // Phase 26: N key and panel button toggle Night Vision Mode
    auto doNightVision = [&]() {
        preview.ToggleNightVision();
        bool on = preview.NightVisionOn();
        controls.SetNightVisionActive(on);
        Log("[NIGHT] Night Vision %s\n", on ? "ON" : "OFF");
    };
    window.SetNightVisionHandler([&]() { doNightVision(); });
    controls.SetNightVisionCallback([&]() { doNightVision(); });

    // Phase 38: V key and panel button cycle Single-Channel Display Mode
    auto doChannelCycle = [&]() {
        preview.CycleChannelMode();
        const wchar_t* lbl = preview.ChannelLabel();
        wchar_t wbuf[32]; swprintf(wbuf, 32, L"[H] %s", lbl);
        controls.SetChannelLabel(wbuf);
        Log("[CHANNEL] %ls\n", lbl);
    };
    window.SetChannelCycleHandler([&]() { doChannelCycle(); });
    controls.SetChannelCycleCallback([&]() { doChannelCycle(); });

    // Phase 24: X key toggles pixel value readout
    window.SetPixelReadoutHandler([&]() {
        preview.TogglePixelReadout();
        Log("[PIXEL] Readout %s\n", preview.PixelReadoutEnabled() ? "ON" : "OFF");
    });
 

    // Phase 22 — Snapshot: save current debayered frame as PNG
    static int snapCount = 0;
    auto doSnapshot = [&]() {
        if (!preview.HasFrame()) return;

        std::string outDir  = controls.GetOutputDir();
        std::string prefix  = controls.GetFilePrefix();
        if (outDir.empty())  outDir  = "D:\\RadukCapture\\captures";
        if (prefix.empty())  prefix  = "Capture";

        // Timestamp: YYYYMMDD_HHMMSS
        SYSTEMTIME st; GetLocalTime(&st);
        char timestamp[32];
        snprintf(timestamp, sizeof(timestamp), "%04d%02d%02d_%02d%02d%02d",
                 st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond);

        // Build path: outDir\prefix_YYYYMMDD_HHMMSS_snap_NNNN.png
        char filename[256];
        snprintf(filename, sizeof(filename), "%s\\%s_%s_snap_%04d.png",
                 outDir.c_str(), prefix.c_str(), timestamp, ++snapCount);

        // Convert to wstring for WIC
        wchar_t wpath[512];
        MultiByteToWideChar(CP_ACP, 0, filename, -1, wpath, 512);

        if (preview.SaveSnapshotPNG(wpath)) {
            // Show filename only (no path) in notification
            wchar_t* fname = wcsrchr(wpath, L'\\');
            std::wstring notif = std::wstring(L"SAVED: ") + (fname ? fname+1 : wpath);
            preview.SetSnapshotNotification(notif);
            // Notification bar — with folder link
            std::wstring folder = std::wstring(wpath, wcsrchr(wpath, L'\\') ? wcsrchr(wpath, L'\\') - wpath : 0);
            notifBar.NotifyInfo(std::wstring(L"\u2714 Snapshot saved: ") + (fname ? fname+1 : wpath), folder);
            slog.System("Snapshot: %s", filename);
            Log("[SNAP] %s\n", filename);
        } else {
            preview.SetSnapshotNotification(L"SNAPSHOT FAILED");
            notifBar.NotifyError(L"\u2716 Snapshot failed — check output directory");
            Log("[SNAP] Failed to save %s\n", filename);
        }
    };
    window.SetSnapshotHandler([&]() { doSnapshot(); });
    window.SetZoomCycleHandler([&]()        { doZoom();              });
    window.SetFullscreenToggleHandler([&]() { doFullscreen();        });
    window.SetGoToToggleHandler([&]()       { gotoPanel.ShowCentered(); });
    window.SetSkyMapToggleHandler([&]()     { skyMap.Toggle();       });
    window.SetAutofocusToggleHandler([&]() {
        // A key: same as clicking the AF button — toggle start/abort
        bool start = !autofocus.IsRunning();
        if (start) {
            if (!focuser.IsOpen()) return;
            int pos = focuser.GetPosition();
            controls.SetAutofocusStatus(L"Starting...");
            controls.SetAutofocusRunning(true);
            autofocus.Start(pos);
        } else {
            autofocus.Abort();
            controls.SetAutofocusRunning(false);
            controls.SetAutofocusStatus(L"Aborted");
        }
    });

    // GoTo panel starts HIDDEN — opens only when user clicks [↑] GOTO in frame bar
    // or presses G. Prevents accidental slew from mis-clicks while imaging.
    gotoPanel.Hide();

    // ── Toolbar button callbacks (Win32 native buttons) ────────
    toolbar.SetCentroidCallback([&]() {
        if (tracker.Stage() == raduk::core::AcquisitionStage::Off) {
            Log("[ACQ] Stage 1: SEARCHING\n");
            slog.Mount("Acquisition: Searching");
            tracker.SetMount(mount);
            tracker.StartSearching();
        } else {
            Log("[ACQ] Cancelled — reset to Off\n");
            slog.Mount("Acquisition: Cancelled");
            tracker.Reset();
            controls.SetSkyMapLocked(false);
        }
    });
    toolbar.SetLockCallback([&]() {
        if (tracker.Stage() == raduk::core::AcquisitionStage::Searching && tracker.IsStable()) {
            Log("[ACQ] Stage 2: CENTERING\n");
            slog.Mount("Acquisition: Centering");
            tracker.StartCentering();
        }
    });
    toolbar.SetROICallback([&]()   { doROILock(); });
    toolbar.SetColorCallback([&]() { preview.CycleDebayer(); });
    toolbar.SetMapCallback([&]()   { skyMap.Toggle(); });
    toolbar.SetGoToCallback([&]()  { gotoPanel.ShowCentered(); });
    toolbar.SetReticleCallback([&]() { doReticle(); });
    toolbar.SetClipCallback([&]()    { preview.ToggleClip(); });
    // Phase 49: Help toggle — toolbar [?] button and ? key
    auto doHelp = [&]() { helpWindow.Toggle(); };
    toolbar.SetHelpCallback([&]()       { doHelp(); });
    window.SetHelpToggleHandler([&]()   { doHelp(); });

    // Phase 50 / Graceful Exit — wired to X button, Alt+F4, and toolbar EXIT
    // Confirms if recording is active, then tears down AI/floating windows before
    // posting WM_QUIT so the cleanup sequence runs normally.
    auto doGracefulExit = [&]() {
        // Confirm if recording active
        if (capturer.IsRecording()) {
            int choice = MessageBoxW(window.Handle(),
                L"A capture is currently recording.\n\nStop recording and exit?",
                L"RadukCapture — Exit",
                MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
            if (choice != IDYES) return;
            capturer.StopRecording();
        }
        // Shut down floating windows and AI gracefully before message loop exits
        aiChat.Destroy();
        helpWindow.Destroy();
        objectInfo.Hide();
        notifBar.Dismiss();
        mountCtrl.Hide();
        // Destroy main window → WM_DESTROY → PostQuitMessage → message loop exits
        DestroyWindow(window.Handle());
    };
    window.SetCloseRequestHandler([&]() { doGracefulExit(); });
    toolbar.SetExitCallback([&]()       { doGracefulExit(); });

    // Phase 50: AI Chat Panel — / key and [AI] toolbar button
    auto doAIToggle = [&]() { aiChat.Toggle(); };
    toolbar.SetAICallback([&]()         { doAIToggle(); });
    window.SetAIToggleHandler([&]()     { doAIToggle(); });

    // Phase 29: Ghost Frame Overlay — O key and [GH] toolbar button
    auto doGhostToggle = [&]() { preview.ToggleGhost(); };
    toolbar.SetGhostCallback([&]()      { doGhostToggle(); });
    window.SetGhostToggleHandler([&]()  { doGhostToggle(); });

    // Forward WM_COMMAND from main window to toolbar
    window.SetToolbarCommandHandler([&](int id) -> bool {
        return toolbar.HandleCommand((WPARAM)id);
    });

    window.SetZoomWheelHandler([&](int dir) { doZoomStep(dir); });
    window.SetScrollHandler([&](WPARAM w, LPARAM l) { return controls.HandleScroll(w, l); });
    window.SetFocusFineHandler([&](int steps) {
        if (focuser.IsOpen()) focuser.MoveRelative(steps);
    });
    window.SetFocusCoarseHandler([&](int steps) {
        if (focuser.IsOpen()) focuser.MoveRelative(steps);
    });
    window.SetTrackingToggleHandler([&]() {
        if (mount && mount->IsConnected()) {
            auto mode = mount->GetTrackingMode();
            mount->SetTrackingMode(mode == IMount::OFF
                ? IMount::ALT_AZ : IMount::OFF);
            slog.Mount("Tracking toggled");
        }
    });
    window.SetJogHandler([&](int axis, int dir, bool pressed) {
        if (!mount || !mount->IsConnected()) return;
        if (pressed) {
            mount->SlewFixed(axis, dir, controls.JogRate());
            if (tracker.IsRunning()) tracker.SuspendCorrections();
        } else {
            mount->StopSlew(axis);
            if (tracker.IsRunning()) {
                std::thread([&tracker = tracker]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    tracker.ResumeCorrections();
                }).detach();
            }
        }
    });

    // ── WS command messages (cross-thread safe via PostMessage) ──
    // WM_USER+1  — drain APEX WebSocket command queue
    // WM_USER+10 — GoTo settle timer expired → start centroid acquisition
    window.SetUserMessageHandler([&](UINT msg) {
        if (msg == WM_USER + 10) {
            // 3s post-slew settle complete — auto-start Stage 1 (Searching)
            // The render loop will auto-advance to Centering and Locked
            // as IsStable() and IsDwellMet() thresholds are met.
            if (tracker.Stage() == raduk::core::AcquisitionStage::Off) {
                Log("[ACQ] Auto-start: Stage 1 SEARCHING (post-GoTo settle)\n");
                slog.Mount("Auto-acquisition: Searching");
                tracker.SetMount(mount);
                tracker.StartSearching();
            }
            return;
        }
        if (msg != WM_USER + 1) return;

        std::vector<RemoteCmd> cmds;
        {
            std::lock_guard<std::mutex> lk(remoteCmdMutex);
            cmds.swap(remoteCmdQueue);
        }
        for (const auto& rc : cmds) {
            if (rc.cmd == "start_capture") {
                if (!capturer.IsRecording()) {
                    Log("[WS] Remote start_capture\n");
                    slog.System("APEX: start_capture");
                    doRecord();
                }
            } else if (rc.cmd == "stop_capture") {
                if (capturer.IsRecording()) {
                    Log("[WS] Remote stop_capture\n");
                    slog.System("APEX: stop_capture");
                    capturer.StopRecording();
                    usbMgr.SetCapturing(false);
                    slog.System("USB polling restored — %s", usbMgr.StatusString());
                }
            } else if (rc.cmd == "set_gain") {
                int g = -1;
                try { if (!rc.arg.empty()) g = std::stoi(rc.arg); } catch (...) {}
                if (g >= 0 && g <= 570 && cam.IsOpen()) {
                    cam.SetGain(g);
                    Log("[WS] Remote set_gain: %d\n", g);
                }
            } else if (rc.cmd == "set_exposure") {
                double ms = 0;
                try { ms = std::stod(rc.arg); } catch (...) {}
                if (ms > 0 && cam.IsOpen()) {
                    cam.SetExposure(ms);
                    Log("[WS] Remote set_exposure: %.2fms\n", ms);
                }
            } else if (rc.cmd == "set_gamma") {
                int g = -1;
                try { if (!rc.arg.empty()) g = std::stoi(rc.arg); } catch (...) {}
                if (g >= 1 && g <= 100 && cam.IsOpen()) {
                    cam.SetGamma(g);
                    Log("[WS] Remote set_gamma: %d\n", g);
                }
            } else if (rc.cmd == "autofocus") {
                if (!autofocus.IsRunning() && focuser.IsOpen()) {
                    Log("[WS] Remote autofocus\n");
                    slog.Focuser("APEX: autofocus triggered");
                    controls.SetAutofocusRunning(true);
                    controls.SetAutofocusStatus(L"Remote AF...");
                    autofocus.Start(focuser.GetPosition());
                }
            } else if (rc.cmd == "move_eaf_rel") {
                int steps = 0;
                try { if (!rc.arg.empty()) steps = std::stoi(rc.arg); } catch (...) {}
                if (steps != 0 && focuser.IsOpen()) {
                    Log("[WS] Remote move_eaf_rel: %d\n", steps);
                    focuser.MoveRelative(steps);
                }
            } else if (rc.cmd == "autofocus_abort") {
                if (autofocus.IsRunning()) {
                    autofocus.Abort();
                    controls.SetAutofocusRunning(false);
                    Log("[WS] Remote autofocus_abort\n");
                }
            } else if (rc.cmd == "centroid_start") {
                if (tracker.Stage() == raduk::core::AcquisitionStage::Off) {
                    tracker.SetMount(mount);
                    tracker.StartSearching();
                    Log("[WS] Remote centroid_start\n");
                }
            } else if (rc.cmd == "centroid_lock") {
                if (tracker.Stage() == raduk::core::AcquisitionStage::Searching && tracker.IsStable()) {
                    tracker.StartCentering();
                    Log("[WS] Remote centroid_lock\n");
                }
            } else if (rc.cmd == "centroid_roi") {
                doROILock();
                Log("[WS] Remote centroid_roi\n");
            } else if (rc.cmd == "centroid_reset") {
                tracker.Reset();
                Log("[WS] Remote centroid_reset\n");
            } else if (rc.cmd == "mount_stop") {
                if (mount && mount->IsConnected()) {
                    mount->StopSlew(0); mount->StopSlew(1);
                    Log("[WS] Remote mount_stop\n");
                }
            } else if (rc.cmd == "jog") {
                if (mount && mount->IsConnected()) {
                    // Short async pulse — 250ms jog without blocking the main thread
                    int axis = (rc.arg == "N" || rc.arg == "S") ? 1 : 0;
                    int dir  = (rc.arg == "S" || rc.arg == "E") ? 1 : 0;
                    IMount* mnt = mount;
                    std::thread([mnt, axis, dir]() {
                        mnt->SlewFixed(axis, dir, 5);
                        Sleep(250);
                        mnt->StopSlew(axis);
                    }).detach();
                    Log("[WS] Remote jog %s\n", rc.arg.c_str());
                }
                // Case-insensitive name match
                int idx = -1;
                for (int i = 0; i < (int)profiles.Profiles().size(); i++) {
                    if (_stricmp(profiles.Profiles()[i].name.c_str(), rc.arg.c_str()) == 0) {
                        idx = i; break;
                    }
                }
                if (idx >= 0) {
                    Log("[WS] Remote load_profile: %s\n", rc.arg.c_str());
                    slog.System("APEX: load_profile %s", rc.arg.c_str());
                    const TargetProfile& p = profiles.Profiles()[idx];
                    ApplyProfile(p, &cam,
                                 focuser.IsOpen() ? &focuser : nullptr,
                                 mount, slog);
                    controls.SetActiveProfile(idx);
                    profiles.SetActive(idx);
                    CaptureConfig cfg = capturer.Config();
                    cfg.filePrefix = p.filePrefix.empty() ? p.name : p.filePrefix;
                    capturer.SetConfig(cfg);
                } else {
                    Log("[WS] Remote load_profile: unknown '%s'\n", rc.arg.c_str());
                }
            } else if (rc.cmd == "move_eaf") {
                int pos = 0;
                try { pos = std::stoi(rc.arg); } catch (...) { continue; }
                pos = std::max(0, std::min(pos, 600000));
                if (focuser.IsOpen()) {
                    Log("[WS] Remote move_eaf: %d\n", pos);
                    slog.Focuser("APEX: move_eaf %d", pos);
                    focuser.MoveTo(pos);
                }
            } else if (rc.cmd == "goto_target") {
                Log("[WS] Remote goto_target: %s\n", rc.arg.c_str());
                slog.Mount("APEX: goto_target %s", rc.arg.c_str());
                // Same path as sky map click — auto-loads profile + slews
                gotoPanel.RequestGoToByName(rc.arg);
            }
        }
    });

    Log("\n--- RADUKCAPTURE v0.9 READY  [Phase 4 Complete] ---\n");
    Log("  M7:  USB traffic manager active (EAF 10s / Mount 2s during capture)\n");
    Log("  M9:  Frame analyzer active (histogram + sharpness)\n");
    Log("  M12: WebSocket server on :8765 (APEX dashboard)\n");
    Log("  P4:  Remote commands: start/stop_capture, load_profile, move_eaf, goto_target\n");
    Log("  P4:  Session summary written to log at shutdown\n\n");
    slog.System("Ready");

    // ── Render loop ────────────────────────────────────────────
    // Frame order per tick:
    //   1. GetLatestFrame() from preview ring
    //   2. Submit to CaptureManager (if recording)
    //   3. Submit to FrameAnalyzer (always — non-blocking)
    //   4. RenderFrame() + StatusBar + EndFrame()
    //   5. ReleaseRead()
    // Every 500ms: gated readouts via USBTrafficManager

    auto lastReadout = std::chrono::steady_clock::now();

    window.Run([&]() {
        // ── SAFETY: Feed mount watchdog every frame ────────────
        // Heartbeat() is a no-op in emulator mode; real watchdog in hardware mode.
        if (mount && mount->IsConnected())
            static_cast<raduk::core::NexStarMount*>(mount)->Heartbeat();

        FrameSlot* frame = ring.GetLatestFrame();
        const auto& stats = capture.Stats();

        // Submit to capture manager — Phase 10 quality gate
        // If GATE is enabled: only record frames meeting the seeing threshold.
        // The gate is checked against the rolling seeing score (updated ~2Hz).
        if (frame && capturer.IsRecording()) {
            bool gateEnabled   = controls.GateEnabled();
            float gateThresh   = controls.GateThreshold();
            bool seenOk        = !gateEnabled
                                 || !lastAnalysis.valid
                                 || lastAnalysis.seeingScore >= gateThresh;
            if (seenOk) {
                capturer.SubmitFrame(frame,
                    lastAnalysis.valid ? lastAnalysis.sharpnessPct : 0.0f);
                if (gateEnabled) controls.SetGateActive(false);  // show green STOP
            } else {
                controls.SetGateActive(true);  // show amber GATED (waiting)
            }
        }

        // Submit to frame analyzer (non-blocking, always)
        if (frame)
            analyzer.Submit(frame);

        // Submit latest frame to GoTo panel (for manual plate solving)
        // Also snapshot for Phase 6.6 auto-solve on Stage 3 lock
        if (frame) {
            if (gotoPanel.IsVisible())
                gotoPanel.SubmitFrame(frame->data, frame->width, frame->height);
            // Always keep a fresh snapshot for auto-solve — small memcpy, ~0.9MB at ROI
            if (frame->width != lastFrameSolveW || frame->height != lastFrameSolveH) {
                lastFrameForSolve.resize((size_t)frame->width * frame->height);
                lastFrameSolveW = frame->width;
                lastFrameSolveH = frame->height;
            }
            if (!lastFrameForSolve.empty())
                memcpy(lastFrameForSolve.data(), frame->data,
                       lastFrameForSolve.size());
        }

        // Keep frame bar GoTo button in sync with panel visibility
        preview.SetGoToPanelVisible(gotoPanel.IsVisible());
        preview.SetSkyMapVisible(skyMap.IsVisible());

        // Grab latest analysis result — push to ControlPanel for GDI histogram
        if (analyzer.HasResult())
            lastAnalysis = analyzer.GetResult();

        if (lastAnalysis.valid)
            controls.UpdateAnalysis(lastAnalysis);

        // Phase 21: feed clip percentage to preview for clipping overlay
        if (lastAnalysis.valid)
            preview.SetClipPct(lastAnalysis.clipPct);

        // Phase 25a: push sharpness sample to quality bar graph
        if (lastAnalysis.valid)
            preview.PushQualitySample(lastAnalysis.sharpnessPct);

        // Phase 23: sync manual offset slider → preview each frame
        if (preview.RGBAlignEnabled())
            preview.SetRGBAlignManual(controls.GetRGBManual());

        // Phase 9: feed sharpness to autofocus scan if running
        if (lastAnalysis.valid && autofocus.IsRunning())
            autofocus.SubmitSharpness(lastAnalysis.sharpnessPct);

        // Feed centroid to tracker for all active stages
        // Stage 1 (Searching): SubmitCentroid updates stable counter, no corrections
        // Stage 2 (Centering): updates dwell counter + fires corrections
        // Stage 3 (Locked):    corrections only
        {
            auto stage = tracker.Stage();
            if (stage != raduk::core::AcquisitionStage::Off) {
                bool transition = false;
                if (lastAnalysis.valid && lastAnalysis.centroidValid)
                    transition = tracker.SubmitCentroid(lastAnalysis.centroidX,
                                                        lastAnalysis.centroidY,
                                                        lastAnalysis.width,
                                                        lastAnalysis.height);
                else
                    tracker.SubmitCentroidLost();

                // Auto-advance stages — no button press needed after a GoTo.
                // When stable: automatically move Searching → Centering.
                // When dwell met: automatically call doROILock (ROI crop + hand-off).
                if (transition) {
                    if (stage == raduk::core::AcquisitionStage::Searching) {
                        Log("[ACQ] Centroid stable — auto-advancing to Stage 2: CENTERING\n");
                        slog.Mount("Auto-acquisition: Centering");
                        tracker.StartCentering();
                    } else if (stage == raduk::core::AcquisitionStage::Centering) {
                        Log("[ACQ] Dwell met — auto-advancing to Stage 3: LOCKED\n");
                        slog.Mount("Auto-acquisition: Locked");
                        doROILock();
                    }
                }
            }

            // Update toolbar button states every frame
            int stageInt = static_cast<int>(stage);
            preview.SetAcquisitionStage(stageInt);
            preview.SetLockReady(tracker.IsStable());
            preview.SetROIReady(tracker.IsDwellMet());
            toolbar.UpdateStates(
                stageInt,
                tracker.IsStable(),
                tracker.IsDwellMet(),
                preview.GetDebayerMode() == raduk::gui::DebayerMode::Color,
                skyMap.IsVisible(),
                gotoPanel.IsVisible(),
                preview.ReticleVisible(),
                preview.ClipVisible(),
                helpWindow.IsVisible(),
                aiChat.IsVisible(),
                preview.GhostEnabled());

            // Phase 50: Update AI session state every frame
            {
                using namespace raduk::core;
                AISessionState aiState;
                // Time
                time_t t = time(nullptr);
                struct tm* loc = localtime(&t);
                char timeBuf[32];
                snprintf(timeBuf, 32, "%04d-%02d-%02d %02d:%02d MST",
                    loc->tm_year+1900, loc->tm_mon+1, loc->tm_mday,
                    loc->tm_hour, loc->tm_min);
                aiState.localTime = timeBuf;
                // LST
                {
                    double lstH = gotoPanel.ApproximateLSTPublic();
                    int lh = (int)lstH % 24;
                    int lm = (int)((lstH - (int)lstH) * 60);
                    char lstBuf[16]; snprintf(lstBuf, 16, "%02dh %02dm", lh, lm);
                    aiState.lstStr = lstBuf;
                }
                // Target / profile — use file prefix as target name
                aiState.targetName  = controls.GetFilePrefix();
                aiState.profileName = controls.GetFilePrefix(); // best available
                // Mount
                if (mount && mount->IsConnected()) {
                    double ra, dec;
                    mount->GetRADec(ra, dec);
                    int rh = (int)ra, rm = (int)((ra - rh) * 60);
                    char raBuf[16], decBuf[16];
                    snprintf(raBuf,  16, "%02dh %02dm", rh, rm);
                    snprintf(decBuf, 16, "%+.1f deg", dec);
                    aiState.mountRA  = raBuf;
                    aiState.mountDec = decBuf;
                    aiState.tracking = (mount->GetTrackingMode() != raduk::core::IMount::OFF);
                }
                aiState.mountAlt = 0.0f;  // alt injected via planner context
                aiState.mountAz  = 0.0f;
                // Camera
                if (cam.IsOpen()) {
                    aiState.gain       = (int)cam.GetControl(ASI_GAIN);
                    aiState.exposureMs = (float)(cam.GetControl(ASI_EXPOSURE) / 1000.0);
                }
                aiState.fps        = (float)stats.fps.load();
                aiState.seeingScore= lastAnalysis.valid ? lastAnalysis.seeingScore : 0.0f;
                // Focuser
                if (focuser.IsOpen()) {
                    aiState.eafPos   = focuser.GetPosition();
                    aiState.eafTempC = focuser.GetTemperature();
                }
                // Capture + buffer
                aiState.recording      = capturer.IsRecording();
                aiState.framesRecorded = capturer.FramesRecorded();
                aiState.framesDropped  = capturer.FramesDropped();
                aiState.bufferUsed     = (int)capturer.BufferPending();
                aiState.bufferTotal    = 512;
                aiState.outputDir      = controls.GetOutputDir();
                aiChat.SetSessionState(aiState);
                // Inject planner data (uses cached list — no recompute each frame)
                aiChat.UpdatePlanner(gotoPanel.GetPlannerData());
            }

            // Phase 23: update RGB align label with live shift value
            if (preview.RGBAlignEnabled())
                controls.SetRGBAlignLabel(preview.RGBAlignShift());
        }

        int clientW = window.ClientWidth();
        int clientH = window.ClientHeight();
        int statusH = window.IsFullscreen() ? 0 : STATUS_BAR_HEIGHT;

        // Preview dimensions come from the preview child window client size.
        // D2D renders into that child — its bounds ARE the preview area.
        float dpiScale  = window.DpiScale();
        RECT  pvRect    = {};
        if (window.PreviewHandle()) GetClientRect(window.PreviewHandle(), &pvRect);
        int   previewW  = (int)(pvRect.right  / dpiScale);
        int   previewH  = (int)(pvRect.bottom / dpiScale);
        // statusBar height in DIPs
        int   stDIPs    = (int)(statusH / dpiScale);
        if (previewW < 100) previewW = 100;
        if (previewH < 100) previewH = 100;

        // windowHeight = full child height; statusBarHeight drawn at bottom
        int windowH = previewW > 0 ? (int)(pvRect.bottom / dpiScale) : previewH + stDIPs;

        preview.RenderFrame(
            frame, 0, previewW, windowH, stDIPs,
            stats.fps.load(),
            stats.framesCaptured.load(),
            stats.droppedFrames.load());

        if (lastAnalysis.valid)
            preview.DrawAnalysisOverlay(lastAnalysis, 0, previewW, windowH, stDIPs);

        if (!window.IsFullscreen())
            statusBar.Draw(previewW, windowH);

        preview.EndFrame();
        ring.ReleaseRead(frame);

        // ── Readouts every 500ms, EAF/Mount gated by USB manager ──
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastReadout).count() > 500) {

            controls.UpdateReadouts();
            mountCtrl.UpdateReadouts();

            // Phase 12 — Auto-Exposure: feed analysis, tick, apply/display
            if (ae.IsEnabled() && lastAnalysis.valid && cam.IsOpen()) {
                long curGain = cam.GetControl(ASI_GAIN);
                long curExpUs = cam.GetControl(ASI_EXPOSURE);
                double curExpMs = curExpUs / 1000.0;

                ae.Feed(lastAnalysis.meanBrightness,
                        lastAnalysis.clipPct,
                        curGain, curExpMs);
                auto sug = ae.Tick();

                // Update panel status text
                wchar_t aeBuf[80];
                MultiByteToWideChar(CP_UTF8, 0, sug.text.c_str(), -1, aeBuf, 80);
                controls.SetAEStatus(aeBuf);

                // Apply suggestions if AUTO mode
                if (ae.IsAuto()) {
                    if (sug.gain >= 0 && sug.gain != (int)curGain) {
                        cam.SetGain(sug.gain);
                        controls.UpdateSlider(false, sug.gain);
                    }
                    if (sug.exposureMs > 0 &&
                        fabs(sug.exposureMs - curExpMs) >= 0.05) {
                        cam.SetExposure(sug.exposureMs);
                        controls.UpdateSlider(true, (int)(sug.exposureMs * 1000.0));
                    }
                }
            }

            // GoTo panel — refresh planet positions and slew status
            if (gotoPanel.IsVisible())
                gotoPanel.Tick();

            // Sky map — update mount crosshair and redraw
            if (skyMap.IsVisible())
                skyMap.Tick();

            // Centroid tracker — dispatch callbacks on main thread
            if (tracker.Stage() != raduk::core::AcquisitionStage::Off)
                tracker.Tick();

            // Camera — always update (never blocks)
            statusBar.SetCameraStats(
                stats.fps.load(),
                (int)stats.droppedFrames.load(),
                cam.IsOpen() ? cam.Temperature() : 0.0f,
                roiWidth, roiHeight, 8);

            // EAF — only poll if USB manager allows (never blocks for long)
            if (focuser.IsOpen() && usbMgr.ShouldPollFocuser())
                statusBar.SetFocuserStats(focuser.GetPosition(),
                    focuser.GetTemperature(), focuser.IsMoving());

            // Mount — reads from NexStarMount cache (instant) or ASCOM (fast property)
            if (mount && mount->IsConnected() && usbMgr.ShouldPollMount()) {
                static int mountTick = 0;
                if (++mountTick % 4 == 0) {  // still gate at 2s for ASCOM
                    double ra = 0, dec = 0;
                    if (mount->GetRADec(ra, dec)) {
                        statusBar.SetMountStats(
                            IMount::FormatRA(ra),
                            IMount::FormatDec(dec),
                            std::string(IMount::TrackingModeName(mount->GetTrackingMode())),
                            true);
                    }
                    // GetAltAz is a serial round-trip — run it on a dedicated thread
                    // so it never stalls the render loop. Cached result read back here.
                    static std::atomic<float> cachedAlt{-1.0f};
                    static std::atomic<bool>  altPending{false};
                    if (!altPending.load()) {
                        altPending.store(true);
                        std::thread([&]() {
                            double alt = 0, az = 0;
                            if (mount && mount->IsConnected() && mount->GetAltAz(alt, az))
                                cachedAlt.store((float)alt);
                            altPending.store(false);
                        }).detach();
                    }
                    float alt = cachedAlt.load();
                    if (alt >= 0.0f) {
                        preview.SetMountAltitude(alt);
                        auto adcResult = raduk::core::ADCAssistant::Compute(alt);
                        auto adcText   = raduk::core::ADCAssistant::SuggestionText(adcResult);
                        wchar_t adcBuf[64];
                        MultiByteToWideChar(CP_UTF8, 0, adcText.c_str(), -1, adcBuf, 64);
                        controls.SetADCStatus(adcBuf);
                    }
                }
            }

            // ── FC-style temperature chart sample ─────────────
            float camT = cam.IsOpen()     ? cam.Temperature()        : 0.0f;
            float focT = focuser.IsOpen() ? focuser.GetTemperature() : 0.0f;
            controls.PushTempSample(camT, focT);

            // ── FC-style status bars ───────────────────────────
            {
                float fps      = (float)stats.fps.load();
                int   lost     = (int)stats.droppedFrames.load();
                uint64_t bufU  = capturer.BufferPending();
                uint64_t bufT  = capturer.BufferTotal();
                float bufPct   = (bufT > 0) ? (float)bufU / bufT * 100.0f : 0.0f;
                int   bufFr    = (int)bufU;
                int   bufTot   = (int)bufT;
                float diskGB   = capturer.IsRecording()
                                 ? (float)capturer.BytesWritten() / 1e9f : 0.0f;
                controls.UpdateStatus(fps, lost, bufPct, bufFr, bufTot, diskGB,
                                      capturer.IsRecording());
            }

            // Capture status panel
            bool limitByFrames = (capturer.GetLimitType() == LimitType::Frames);
            controls.UpdateCaptureStatus(
                capturer.IsRecording(),
                capturer.FramesRecorded(),
                capturer.FramesDropped(),
                capturer.ElapsedSec(),
                capturer.BufferPending(),
                capturer.BufferTotal(),
                capturer.LimitFrames(),
                capturer.LimitTimeSec(),
                limitByFrames,
                capturer.Config().outputDir);

            // Profile
            const TargetProfile* active = profiles.Active();
            if (active) statusBar.SetProfile(active->name);

            // Log sharpness score periodically (every ~5s) to session log
            // when not recording — during capture the session log is for events
            static int sharpLogTick = 0;
            if (++sharpLogTick >= 10 && lastAnalysis.valid && !capturer.IsRecording()) {
                sharpLogTick = 0;
                slog.System("Seeing: %.1f/10  Sharpness: %.0f%%",
                    lastAnalysis.seeingScore, lastAnalysis.sharpnessPct);
            }

            // ── Session summary accumulators ───────────────────
            // Detect end of a capture run (was recording, now stopped)
            bool nowRecording = capturer.IsRecording();
            if (summaryWasRecording && !nowRecording) {
                // Run just finished — snapshot totals
                summaryFramesRecorded += capturer.FramesRecorded();
                summaryFramesDropped  += capturer.FramesDropped();
                summaryElapsedSec     += capturer.ElapsedSec();
                summaryBytesWritten   += capturer.BytesWritten();
                ++summaryRunCount;

                // Notification bar — show frame count + file size
                uint64_t bytes = capturer.BytesWritten();
                double   mb    = bytes / (1024.0 * 1024.0);
                wchar_t  nbuf[256];
                if (mb >= 1024.0)
                    swprintf(nbuf, 256, L"\u2714 Capture complete: %u frames  %.2f GB",
                             capturer.FramesRecorded(), mb / 1024.0);
                else
                    swprintf(nbuf, 256, L"\u2714 Capture complete: %u frames  %.0f MB",
                             capturer.FramesRecorded(), mb);
                // Open folder link — use output dir from controls
                std::string outDirA = controls.GetOutputDir();
                std::wstring outDirW(outDirA.begin(), outDirA.end());
                notifBar.NotifyInfo(nbuf, outDirW);

                // Warn if frames were dropped
                if (capturer.FramesDropped() > 0) {
                    wchar_t wbuf[128];
                    swprintf(wbuf, 128, L"\u26A0 %u frames dropped — reduce USB BW or exposure",
                             capturer.FramesDropped());
                    notifBar.NotifyWarn(wbuf);
                }
            }
            summaryWasRecording = nowRecording;
            // Accumulate seeing score continuously (used for session avg)
            if (lastAnalysis.valid && lastAnalysis.seeingScore > 0.0f) {
                summarySeeingAcc += lastAnalysis.seeingScore;
                ++summarySeeingCount;
            }

            // ── APEX WebSocket telemetry broadcast (M12) ───────────
            if (wsServer.IsRunning() && wsServer.ClientCount() > 0) {
                telemetry.rc_running     = true;
                telemetry.fps            = stats.fps.load();
                telemetry.exposure_ms    = cam.IsOpen() ? cam.GetControl(ASI_EXPOSURE)/1000.0 : 8.0;
                telemetry.gain           = cam.IsOpen() ? (int)cam.GetControl(ASI_GAIN)    : 100;
                telemetry.gamma          = cam.IsOpen() ? (int)cam.GetControl(ASI_GAMMA)   : 50;
                telemetry.wb_red         = cam.IsOpen() ? (int)cam.GetControl(ASI_WB_R)    : 52;
                telemetry.wb_blue        = cam.IsOpen() ? (int)cam.GetControl(ASI_WB_B)    : 47;
                telemetry.usb_bw         = cam.IsOpen() ? (int)cam.GetControl(ASI_BANDWIDTHOVERLOAD) : 80;
                telemetry.sensor_temp    = cam.IsOpen() ? cam.Temperature() : 0.0f;
                telemetry.width          = roiWidth;
                telemetry.height         = roiHeight;
                telemetry.bit_depth      = 8;
                telemetry.frames_dropped = (int)stats.droppedFrames.load();

                // Disk free space on capture drive
                {
                    ULARGE_INTEGER freeBytesAvail{};
                    const std::string& outDir = capturer.Config().outputDir;
                    std::wstring wDir(outDir.begin(), outDir.end());
                    if (GetDiskFreeSpaceExW(wDir.c_str(), &freeBytesAvail, nullptr, nullptr))
                        telemetry.disk_free_gb = (double)freeBytesAvail.QuadPart / 1e9;
                }

                // Current capture file name
                if (capturer.IsRecording())
                    telemetry.capture_file = capturer.CurrentFile();

                if (focuser.IsOpen()) {
                    telemetry.eaf_position = focuser.GetPosition();
                    telemetry.eaf_temp     = focuser.GetTemperature();
                    telemetry.eaf_moving   = focuser.IsMoving();
                }

                if (mount && mount->IsConnected()) {
                    double ra=0, dec=0;
                    mount->GetRADec(ra, dec);
                    telemetry.mount_ra       = IMount::FormatRA(ra);
                    telemetry.mount_dec      = IMount::FormatDec(dec);
                    telemetry.mount_tracking = IMount::TrackingModeName(
                                                  mount->GetTrackingMode());
                }

                telemetry.recording        = capturer.IsRecording();
                telemetry.frames_recorded  = capturer.FramesRecorded();
                telemetry.elapsed_sec      = capturer.ElapsedSec();
                telemetry.ram_used_slots   = (int)capturer.BufferPending();
                telemetry.ram_total_slots  = (int)capturer.BufferTotal();

                const CaptureConfig& cfg = capturer.Config();
                telemetry.output_dir   = cfg.outputDir;
                telemetry.file_prefix  = cfg.filePrefix;
                telemetry.limit_frames = cfg.limitFrames;

                if (lastAnalysis.valid) {
                    telemetry.seeing_score = lastAnalysis.seeingScore;
                    telemetry.seeing_rms   = lastAnalysis.sharpnessPct;
                    telemetry.histogram    = lastAnalysis.histogram;
                }

                // Planet lock / centroid tracking state
                {
                    auto stage = tracker.Stage();
                    telemetry.planet_lock  = (stage == raduk::core::AcquisitionStage::Centering ||
                                              stage == raduk::core::AcquisitionStage::Locked);
                    telemetry.drift_ra_as  = tracker.DriftRA();
                    telemetry.drift_dec_as = tracker.DriftDec();
                }

                // Flush pending session events
                {
                    std::lock_guard<std::mutex> lk(eventsMutex);
                    telemetry.session_events = pendingEvents;
                    pendingEvents.clear();
                }

                wsServer.Broadcast(telemetry.ToJSON());
                telemetry.session_events.clear();
            }

            lastReadout = now;
        }
    });

    // ── Cleanup ────────────────────────────────────────────────
    Log("\nShutting down...\n");
    slog.System("Shutdown");

    // ── Session summary ────────────────────────────────────────
    // If a capture was running at exit, snapshot its final state
    if (capturer.IsRecording()) {
        summaryFramesRecorded += capturer.FramesRecorded();
        summaryFramesDropped  += capturer.FramesDropped();
        summaryElapsedSec     += capturer.ElapsedSec();
        summaryBytesWritten   += capturer.BytesWritten();
        ++summaryRunCount;
    }
    {
        float avgSeeing = (summarySeeingCount > 0)
                          ? summarySeeingAcc / (float)summarySeeingCount : 0.0f;
        std::string lastRA = "--", lastDec = "--";
        int   eafPos  = focuser.IsOpen() ? focuser.GetPosition()    : 0;
        float eafTemp = focuser.IsOpen() ? focuser.GetTemperature() : 0.0f;
        if (mount && mount->IsConnected()) {
            double ra = 0, dec = 0;
            mount->GetRADec(ra, dec);
            lastRA  = IMount::FormatRA(ra);
            lastDec = IMount::FormatDec(dec);
        }
        const TargetProfile* activeP = profiles.Active();
        std::string targetName = activeP ? activeP->name : "Unknown";

        slog.WriteSummary(targetName,
                          summaryFramesRecorded, summaryFramesDropped,
                          summaryElapsedSec, summaryBytesWritten,
                          summaryRunCount, avgSeeing,
                          eafPos, eafTemp, lastRA, lastDec);
    }

    wsServer.Stop();

    // Shut down Phase 50 AI before hardware teardown
    aiChat.Destroy();

    if (capturer.IsRecording()) capturer.StopRecording();
    tracker.Reset();
    analyzer.Stop();
    statusBar.Release();
    preview.Release();
    capture.Stop();
    if (mount && mount->IsConnected()) { mount->StopSlew(0); mount->StopSlew(1); mount->Disconnect(); }
    if (focuser.IsOpen()) focuser.Close();
    cam.Close();

    slog.System("Done");
    Log("Goodbye.\n");
    if (g_log) fclose(g_log);
    return 0;
}

// ──────────────────────────────────────────────────────────────

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // Declare Per-Monitor DPI awareness so Direct2D coordinates and Win32 pixel
    // measurements stay in sync. Without this, Windows virtualizes coordinates
    // on high-DPI displays (e.g. 125%/150% scaling), causing the preview image
    // to appear off-center, the status bar text to be misaligned, and the
    // reticle to not bisect the image correctly.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

#ifdef RADUK_EMULATOR
    const char* MUTEX_NAME = "RadukCapture_Emulator_SingleInstance";
#else
    const char* MUTEX_NAME = "RadukCapture_SingleInstance";
#endif
    HANDLE hMutex = CreateMutexA(nullptr, TRUE, MUTEX_NAME);
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxA(nullptr,
            "RadukCapture is already running.\n\nClose the existing instance first.",
            "RadukCapture", MB_ICONWARNING | MB_OK);
        CloseHandle(hMutex);
        return 0;
    }

    Timestamp::Init();
    LogInit();
    SetUnhandledExceptionFilter(CrashHandler);

#ifdef RADUK_EMULATOR
    Log("[STARTUP] Emulator build\n");
    int result = RunApp(true);
#else
    Log("[STARTUP] Hardware build\n");
    HardwareStatus hw = CheckHardware();
    bool useEmulator = false;
    if (!hw.AllPresent()) {
        // Release mutex before showing dialog — if user picks Start Emulators,
        // the emulator exe needs to acquire its own mutex without us holding it.
        if (hMutex) { CloseHandle(hMutex); hMutex = nullptr; }
        bool wantsEmulator = ShowStartupDialog(hw);
        if (wantsEmulator || !hw.AllPresent()) {
            // Either emulator was launched (wantsEmulator=true and the exe was
            // spawned) or user exited the dialog — either way, this process exits.
            if (hMutex) CloseHandle(hMutex);
            if (g_log) fclose(g_log);
            return 0;
        }
        useEmulator = wantsEmulator;
    }
    int result = RunApp(useEmulator);
#endif

    if (hMutex) CloseHandle(hMutex);
    if (g_log) fclose(g_log);
    return result;
}
