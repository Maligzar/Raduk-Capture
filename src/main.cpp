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
#include <commdlg.h>          // phase 48: the recall file picker (LEAN_AND_MEAN leaves it out)
#pragma comment(lib, "comdlg32.lib")
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <functional>

#include "Version.h"
#include "core/DeviceFactory.h"
#include "core/RingBuffer.h"
#include "core/CaptureThread.h"
#include "core/CaptureManager.h"
#include "core/USBTrafficManager.h"
#include "core/FrameAnalyzer.h"
#include "core/CentroidTracker.h"
#include "core/RigConfig.h"
#include "core/SerialPortScan.h"
#include "core/SiteLocator.h"
#include "core/CordWrap.h"
#include "core/SkyMath.h"
#include "core/TextConv.h"
#include "core/Paths.h"
#include "core/Readiness.h"
#include "gui/ReadinessDialog.h"
#include "gui/RigSetupDialog.h"
#include "gui/SiteDialog.h"
#include "gui/AlignHelperDialog.h"
#include "core/Timestamp.h"
#include "core/SessionLog.h"
#include "net/WebSocketServer.h"
#include "net/APEXTelemetry.h"
#include "net/PreviewStreamer.h"
#include "net/LeobenClient.h"     // weather and sky from Leoben, on the home network
#include "gui/Window.h"
#include "gui/PreviewRenderer.h"
#include "gui/Theme.h"
#include "gui/ControlPanel.h"
#include "gui/ToolbarStrip.h"
#include "gui/GoToPanel.h"
#include "gui/SkyMapWindow.h"
#include "core/GuideSolve.h"   // phase 47: a clicked pixel into guide pulses
#include "core/ObservationRecord.h" // phase 48: the record beside each capture, and recall
#include "core/MountProxy.h"   // phase 46: one mount pointer that never goes stale
#include "core/HardwareLink.h" // phase 46: what the [HW] button shows and says
#include "core/RoiFollower.h"
#include "core/DashboardLaunch.h" // [DASH]: open the dashboard with its token
#include <shlwapi.h>               // AssocQueryStringW: which browser [DASH] uses
#pragma comment(lib, "shlwapi.lib")
#include "gui/ShareView.h"
#include "gui/ObjectPanel.h"
#include "gui/WorkspaceLayout.h"
#include "gui/HistogramWindow.h"
#include "gui/SharpnessWindow.h"
#include "gui/TemperatureWindow.h"
#include "gui/StatusWindow.h"
#include "gui/ConfirmToast.h"
#include "gui/ADCAlignWindow.h"
#include "core/ADCAlignment.h"
#include "core/SkyConditions.h"
#include "gui/MountController.h"
#include "gui/StatusBar.h"
#include "gui/HelpWindow.h"
#include "gui/AIChatPanel.h"
#include "gui/NotificationBar.h"
#include "gui/ObjectInfoPanel.h"
#include "core/AIContextBuilder.h"
#include "core/AdvisoryLoop.h"
#include "core/CalibrationFrames.h"
#include "core/AutoStakkertRunner.h"
#include "profiles/TargetProfile.h"
#include "profiles/ProfileManager.h"
#include "core/AutofocusRoutine.h"
#include "core/ADCAssistant.h"
#include "core/DerotationLimit.h"
#include "core/AutoExposureAssistant.h"

using namespace raduk::core;
using namespace raduk::gui;
using namespace raduk::net;
using namespace raduk::profiles;

// ── Crash-safe logging ─────────────────────────────────────────
static FILE* g_log = nullptr;

static void LogInit() {
    g_log = fopen(raduk::core::Paths::LogFile().c_str(), "w");
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
#ifdef RADUK_EMULATOR
static raduk::core::MountEmulator* g_mountForCrash = nullptr;
#else
static raduk::core::NexStarMount* g_mountForCrash = nullptr;
#endif

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
    // Where the mount was actually found. A USB-serial adapter's COM
    // number is assigned by Windows and moves between reboots and USB
    // ports, so it is discovered at scan time rather than assumed.
    std::string mountPort;
    bool        mountBusy = false;  // port held by CPWI
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
    // Mount: probe every serial port Windows reports as present and
    // keep the one that answers the NexStar echo. rig.ini's MountPort,
    // when set, is tried first. A port that exists but cannot be
    // opened is CPWI holding the mount, which still counts as found.
    auto scan = raduk::core::FindNexStarPort(raduk::core::RigConfig::Get().mountPort);
    if (scan.found) {
        s.mountFound = true;
        s.mountPort  = scan.port;
        Log("[CHECK]   Mount:  OK (%s responded)\n", scan.port.c_str());
    } else if (scan.busy) {
        s.mountFound = true;
        s.mountPort  = scan.busyPort;
        s.mountBusy  = true;
        Log("[CHECK]   Mount:  OK (%s owned by CPWI)\n", scan.busyPort.c_str());
    } else {
        s.mountFound = false;
        Log("[CHECK]   Mount:  NOT FOUND (probed: %s)\n",
            raduk::core::DescribeProbedPorts(scan).c_str());
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
    {
        wchar_t mountName[64];
        if (s.hw.mountPort.empty()) {
            wcscpy_s(mountName, L"NexStar Mount");
        } else {
            swprintf(mountName, 64, L"NexStar Mount (%hs)", s.hw.mountPort.c_str());
        }
        setLbl(s.lblMount, s.hw.mountFound, mountName);
    }
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
                            L"Build failed. Check build_emulator.bat in the source tree.");
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
// Phase 85: what SAVE records and what LOAD applied had drifted apart.
// SAVE captured gain, exposure, gamma, white balance, USB bandwidth,
// high-speed, ROI, binning, bit depth and focus position; LOAD applied
// everything except the last four, so a profile's resolution and bit
// depth were written to disk every time and read back from it never.
// And nothing told the control panel anything, so after a load the
// sliders still showed the previous values -- then nudging one wrote
// that stale value back to the camera and undid the profile.
//
// These hooks close both gaps. They are passed in rather than called
// directly because the format path and the panel both live inside
// RunApp, while this is a free function above it.
struct ProfileApplyHooks {
    // Null leaves the capture format alone. Supplied only for an
    // explicit target selection -- startup must stay at full frame.
    std::function<void(int w, int h, int bits)> setFormat;
    // Null leaves the panel showing whatever it showed before.
    std::function<void(const TargetProfile&)>   syncUi;
    // Phase 95. Null leaves star mode as it was. Told on every profile
    // apply, because whether the star metrics mean anything depends
    // entirely on what the scope is pointed at.
    std::function<void(bool)>                   setStarMode;
    // Phase 100. Null leaves the assistant's clamp where it was. Told
    // on every profile apply, because a DSO rail and a planetary rail
    // differ by four orders of magnitude and an assistant capped at a
    // planetary ceiling cannot expose a deep-sky sub at all.
    std::function<void(const TargetProfile&)>   setAERails;
};

// ── Phase 95: is this target a star? ─────────────────────────
//
// Decided from the name against the bright-star catalogue the app
// already ships, rather than a new field on TargetProfile. That keeps
// it out of rig.ini and the saved profiles, works identically in the
// emulator and the hardware build, and needs no migration -- and the
// catalogue is exactly the list of things anyone would name a profile
// after when they mean a star.
//
// Leading-substring and case-insensitive, matching how the emulator
// resolves its own targets, so "Vega focus test" is Vega.
static bool TargetIsStar(const std::string& name) {
    if (name.empty()) return false;
    for (int i = 0; i < raduk::sky::kBrightStarCount; ++i) {
        const char* n = raduk::sky::kBrightStars[i].name;
        const size_t len = strlen(n);
        if (name.size() >= len && _strnicmp(name.c_str(), n, len) == 0)
            return true;
    }
    return false;
}

#ifdef RADUK_EMULATOR
// ── Phase 94.2: tell the emulator how big the target really is ──
//
// EMU_TARGETS' radii are constants "at a representative opposition",
// and apparent size is the one property of a planet that is never
// constant: Mars runs 3.5" to 25" across its synodic cycle. The
// ephemeris already computes it from the Earth-planet distance it was
// working out anyway, and tests/planet_apparent_size.cpp already gates
// that the number MOVES correctly over years rather than merely being
// plausible. So there is nothing to build here, only to connect.
//
// Called AFTER SetTargetName, and it reads the name back from the
// camera rather than using the profile's. SetTargetName matches on a
// leading substring, so a profile called "Jupiter - GRS transit"
// resolves to "Jupiter"; the ephemeris needs the exact name, and
// re-implementing that matching here would be a second place for it to
// drift.
//
// The disc radius, not the framing radius. Saturn's rings are drawn as
// a multiple of the globe, so feeding the ring span in would inflate
// the globe to the size of its own rings.
static void EmuApplyApparentSize(raduk::core::ASICamera* cam) {
    if (!cam) return;
    const std::string resolved = cam->TargetName();
    const double jd = raduk::sky::CurrentJD();
    const auto sz = raduk::sky::PlanetApparentSizeAt(resolved, jd);
    // Phase 97.4: this comment used to say "not a planet (a star, the
    // Moon) leaves the table in charge", and for the Moon that was the
    // defect. PlanetApparentSizeAt now covers the Moon and the Sun as
    // well, so the emulator renders them at TONIGHT'S size instead of
    // the 900-arcsec mean radius in the target table -- a figure that
    // cannot follow the 14% swing over an anomalistic month, which is
    // exactly the swing that decides whether a mosaic needs fifteen
    // panels or eighteen.
    //
    // Indoor practice at the wrong apparent size teaches the wrong
    // framing, which is the whole reason the emulator exists.
    //
    // A star still falls through to the table, correctly: it has no
    // disc, and 94.1 renders it as an Airy pattern instead.
    cam->SetTargetAngularRadiusArcsec(
        sz.valid ? (float)(sz.discArcsec * 0.5) : 0.0f);

    // Phase 94.3: the Moon needs which way the sunlight is coming
    // from, which is the one thing that decides where the terminator
    // falls -- and the terminator is the whole reason the Moon is
    // worth rendering as something other than a bright rectangle.
    //
    // MoonRADec reports the phase in the field it reuses for
    // magnitude, 0 new through 0.5 full. Only asked for when the
    // target IS the Moon, because it is a Meeus series and the
    // emulator switches targets on every profile load.
    if (resolved == std::string("Moon")) {
        double mra = 0.0, mdec = 0.0, phase = 0.0;
        raduk::sky::MoonRADec(jd, mra, mdec, phase);
        cam->SetLunarPhase((float)phase);
        Log("[EMU] Moon phase %.3f (0 new, 0.5 full)\n", phase);
    }
}
#endif

static void ApplyProfile(const TargetProfile& p,
                          ASICamera* cam, EAFFocuser* focuser,
                          IMount* mount, SessionLog& slog,
                          const ProfileApplyHooks& hooks = {}) {
    Log("[PROFILE] Applying: %s\n", p.name.c_str());
    slog.Profile("Applied: %s", p.name.c_str());

    // ── Phase 100: the exposure rails, enforced at last ────────
    //
    // Phase 99 found all four practical limits read by nothing, so a
    // planetary profile could carry 500 ms and the application would
    // not say a word. Said once, here, where every profile change
    // already funnels -- and said rather than refused, because these
    // are practical limits with physical reasons and not hardware
    // bounds. A 500 ms planetary exposure is almost certainly a
    // mistake and just possibly deliberate on a very dim target.
    //
    // The assistant gets the same rail as a hard clamp below. It has
    // no judgement to respect.
    {
        const std::string warn = p.ExposureWarning();
        if (!warn.empty()) {
            Log("[PROFILE] %s\n", warn.c_str());
            slog.Profile("%s", warn.c_str());
        }
    }
    if (hooks.setAERails) hooks.setAERails(p);
    // Phase 95: the star metrics are only meaningful on a star.
    if (hooks.setStarMode) {
        const bool isStar = TargetIsStar(p.name);
        hooks.setStarMode(isStar);
        Log("[PROFILE] Star focus metrics %s\n", isStar ? "ON" : "off");
    }
#ifdef RADUK_EMULATOR
    // Render what the profile actually names. Every profile change
    // funnels through here, so this covers the startup load and every
    // later switch without a second wiring point to forget.
    if (cam) { cam->SetTargetName(p.name); EmuApplyApparentSize(cam); }
#endif
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

        // ── Phase 97: the rate, which is not the mode ──────────
        //
        // AFTER SetTrackingMode, not before: that call writes the
        // Tracking property, and a driver may ignore a rate written
        // while tracking is off.
        //
        // Why it is here. ASCOMMount wrote the literal 0 -- sidereal
        // -- at two sites, including on every GoTo, with the comment
        // "DriveRates: 0=Sidereal, 1=Lunar, 2=Solar" beside it. The
        // Moon moves 13.176 deg/day east against the stars, so a
        // sidereal mount loses it at 0.55 arcsec/s, which on this
        // rig is 2.7 px/s.
        //
        // That is NOT a smear problem: 20 ms of exposure is 0.05 px,
        // and a 60-second run drifts 4% of the frame, which the
        // stacker aligns out. It is a MOSAIC problem. The Moon is
        // 2.4 frames wide and 4.2 tall here, so it needs eighteen
        // panels, which is about eight minutes, which is 277 arcsec
        // of drift against a 156-arcsec overlap at 20%. The drift
        // exceeds the whole overlap by 1.8x and the mosaic does not
        // close. See src/sky/MosaicPlan.h.
        if (mode != IMount::OFF) {
            const IMount::TrackingRate want =
                IMount::RateForTarget(p.name.c_str());
            if (want != IMount::RATE_SIDEREAL) {
                // Reported either way. A caller that believes it has
                // lunar rate will plan a mosaic that cannot close, so
                // a refusal has to be visible rather than swallowed.
                const bool got = mount->SetTrackingRate(want);
                Log("[PROFILE] %s rate: %s\n",
                    want == IMount::RATE_LUNAR ? "lunar" : "solar",
                    got ? "set" : "REFUSED by backend, still sidereal");
                if (got) slog.Mount("Tracking rate: %s",
                                    want == IMount::RATE_LUNAR ? "lunar"
                                                               : "solar");
            } else {
                // Back to sidereal when leaving the Moon or the Sun.
                // Without this, selecting Jupiter after the Moon would
                // keep lunar rate and drift the planet out at
                // 0.55 arcsec/s -- a bug introduced by the fix.
                mount->SetTrackingRate(IMount::RATE_SIDEREAL);
            }
        }
    }
    // Format after the control writes: it stops and restarts the capture
    // thread, and the gain/exposure values set above survive that.
    if (hooks.setFormat && p.roiWidth > 0 && p.roiHeight > 0) {
        hooks.setFormat(p.roiWidth, p.roiHeight,
                        p.imgType == ASI_IMG_RAW16 ? 16 : 8);
    }
    // Last, so the panel describes the state everything above left.
    if (hooks.syncUi) hooks.syncUi(p);
}

// ── Application core ───────────────────────────────────────────
static int RunApp(bool useEmulator) {
    // Window covers the full screen. The control panel is a WS_CHILD
    // positioned at the right edge (clientW - PANEL_WIDTH).
    // Preview renders to (0, 0, clientW - PANEL_WIDTH, clientH).
    const int screenW  = GetSystemMetrics(SM_CXSCREEN);
    const int screenH  = GetSystemMetrics(SM_CYSCREEN);

    Log("==============================================\n");
    Log("  RADUKCAPTURE v%s  (built %s %s) %s\n",
        RADUK_VERSION_A, __DATE__, __TIME__,
        useEmulator ? "[EMULATOR]" : "");
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
        auto cameras = EnumerateCameras();
        if (cameras.empty()) {
            MessageBoxA(nullptr, "No cameras detected.", "RadukCapture", MB_ICONERROR);
            return 1;
        }
    }
#endif // RADUK_EMULATOR
    // Phase 66: resolve the observing site before anything consumes it.
    // Order matters -- an explicit rig.ini entry always wins, and only
    // a genuinely unknown site reaches out to Windows. The mount is
    // asked later, on connect, because it is not open yet here.
    {
        auto& rig = raduk::core::RigConfig::Get();
        if (rig.SiteIsSet()) {
            Log("[INIT] Site: %.1f%c %.1f%c (%s)\n",
                rig.siteLatDeg < 0 ? -rig.siteLatDeg : rig.siteLatDeg,
                rig.siteLatDeg < 0 ? 'S' : 'N',
                rig.siteLonDeg < 0 ? -rig.siteLonDeg : rig.siteLonDeg,
                rig.siteLonDeg < 0 ? 'W' : 'E',
                rig.siteSource.empty() ? "manual" : rig.siteSource.c_str());
        } else {
            Log("[INIT] Site unknown - asking Windows Location\n");
            auto fix = raduk::core::QueryWindowsLocation();
            if (fix.valid) {
                rig.AdoptSite(fix.latDeg, fix.lonDeg,
                              fix.hasElev ? fix.elevM
                                          : raduk::core::RigConfig::UNKNOWN_ELEV,
                              "windows");
                Log("[INIT] Site from Windows Location (+/-%.0f m)\n",
                    fix.accuracyM);
            } else {
                Log("[INIT] Windows Location: %s\n", fix.status.c_str());
                // Opportunistic: a USB GPS dongle is just another serial
                // port, and the scanner already enumerates them. Passive
                // listen only -- nothing is written to a port that might
                // belong to someone else's device.
                raduk::core::GpsFix g;
                if (raduk::core::FindGpsFix(g)) {
                    rig.AdoptSite(g.latDeg, g.lonDeg,
                                  g.hasElev ? g.elevM
                                            : raduk::core::RigConfig::UNKNOWN_ELEV,
                                  "gps");
                } else {
                    // Still not fatal: the mount is asked on connect.
                    Log("[INIT] No GPS receiver found either\n");
                }
            }
        }
    }

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

    // Full sensor at startup: the widest field, and the one to come back
    // to. roiBits tracks what the camera actually gave us -- before this
    // was tracked, every call site simply asserted RAW8, and the 16-bit
    // the DSO profile asks for was silently discarded.
    // Phase 88: come back where we left off. Full frame 8-bit remains
    // the fallback for a first run or an upgrade, so nothing changes for
    // a rig that has never stored a session.
    int roiWidth = 3840, roiHeight = 2160, roiBits = 8;
    {
        const auto& rgs = raduk::core::RigConfig::Get();
        if (rgs.HasLastFormat()) {
            roiWidth  = rgs.lastRoiWidth;
            roiHeight = rgs.lastRoiHeight;
            roiBits   = (rgs.lastBitDepth == 16) ? 16 : 8;
        }
    }
    const int reqW = roiWidth, reqH = roiHeight, reqBits = roiBits;
    cam.SetRegion(roiWidth, roiHeight, 1,
                  roiBits == 16 ? ImageType::Raw16 : ImageType::Raw8);
    // Read back what the camera accepted rather than trusting the request.
    {
        int aW=roiWidth,aH=roiHeight,aB=1; ImageType aT=ImageType::Raw8;
        cam.GetRegion(aW,aH,aB,aT);
        roiWidth=aW; roiHeight=aH;
        roiBits = (aT == ImageType::Raw16) ? 16 : 8;
    }
    Log("[FORMAT] startup -> %dx%d %d-bit (requested %dx%d %d-bit, %s)\n",
        roiWidth, roiHeight, roiBits, reqW, reqH, reqBits,
        raduk::core::RigConfig::Get().HasLastFormat()
            ? "restored from last session" : "first run default");
    // Sensor-absolute origin of the current ROI. Centroids and rubber-band
    // selections are ROI-relative, so every crop must add this offset —
    // without it, a second acquisition cycle (ROI still cropped from the
    // first) placed the new crop at the wrong sensor position.
    // The ASI SDK centres a ROI when no start position is set; seed to match.
    bool followGaveUp = false;   // Phase 107: until the button is pressed again
    int roiStartX = ((cam.MaxWidth()  - roiWidth)  / 2 / 8) * 8;
    int roiStartY = ((cam.MaxHeight() - roiHeight) / 2 / 2) * 2;
    // Phase 88: last session's values, falling back to the old
    // hardcoded defaults when nothing has been stored yet. These used to
    // overwrite the rig unconditionally, so a night of tuning was lost on
    // every relaunch unless it had been saved into a profile first.
    {
        const auto& rgs = raduk::core::RigConfig::Get();
        // Per field, not one flag. RigConfig rejects each stored value
        // on its own merits, so a session can come back with a good gain
        // and no usable USB figure -- which is exactly what a session
        // saved on the emulator does, since it discards that control.
        const int   g  = rgs.lastGain       >= 0    ? rgs.lastGain  : 100;
        const double e = rgs.lastExposureMs >  0.0f ? (double)rgs.lastExposureMs : 8.0;
        const int   gm = rgs.lastGamma      >= 0    ? rgs.lastGamma : 50;
        const int   wr = rgs.lastWbRed      >= 0    ? rgs.lastWbRed : 52;
        const int   wb = rgs.lastWbBlue     >= 0    ? rgs.lastWbBlue: 47;
        const int   ub = rgs.lastUsb        >= 0    ? rgs.lastUsb   : 80;
        cam.SetGain(g); cam.SetExposure(e); cam.SetGamma(gm);
        cam.SetWhiteBalance(wr, wb);
        cam.SetUSBBandwidth(ub);
        cam.SetHighSpeedMode(true);
        Log("[CAMERA] startup -> gain=%d exp=%.2fms gamma=%d wb=%d/%d usb=%d (%s)\n",
            g, e, gm, wr, wb, ub,
            rgs.HasLastCamera() ? "restored" : "first run default");
    }

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
    // Send the V-curve to raduk_log.txt as well as the console, so a focus
    // run can be judged after the fact instead of only while watching it.
    autofocus.SetLogger([](const char* s) { Log("%s", s); });
    // ── Phase 46: choosing a mount, more than once ─────────────
    //
    // This used to run straight-line at startup and never again, which
    // is why it reads as a script rather than a function. It has to be
    // callable twice now: a reconnect is not Connect() on the same
    // object, because the right answer may have changed since launch --
    // CPWI started, so ASCOM is now correct where NexStar direct was,
    // or the hand control came up on a different COM port, since
    // Windows assigns those by which socket the cable landed in.
    //
    // So it picks again, and returns a NEW backend. The proxy below is
    // what everything else holds, so a new backend does not strand the
    // tracker, the GoTo panel, the sky maps or the mount controller.
    raduk::core::MountProxy mountProxy;
    bool startInRCControl = false;
    // Phase P5.3: set when a simulator is standing in for a real
    // mount on the HARDWARE path, which is the case worth warning
    // about -- an emulator build says what it is everywhere else, so
    // it leaves this false on purpose.
    //
    // Lives out here because connectMountBackend runs once before the
    // notification bar exists and again on every reconnect, long
    // after it does.
    bool mountIsSimulated = false;

    auto connectMountBackend = [&]() -> std::unique_ptr<IMount> {
        std::unique_ptr<IMount> mountPtr;
        // Cleared on every fresh choice, not just on the ASCOM path.
        // Set only where it becomes true, this stayed true after a
        // reconnect fell through from a pinned simulator to the hand
        // control, and the warning outlived the thing it warned about.
        mountIsSimulated = false;

#ifdef RADUK_EMULATOR
    {
        auto* em = new raduk::core::MountEmulator();
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
            // The simulator rejection belongs to DISCOVERY, not to a
            // driver the owner named in rig.ini; AcceptConnectedDriver
            // holds that policy and tests/ascom_driver_choice.cpp
            // gates it. Pinning a simulator is the only way to drive
            // the generic ASCOM path without a mount on the desk, and
            // this line used to refuse it.
            const bool pinned = ASCOMMount::ProgIdWasPinned();
            if (am->Connect(ascomProgId) &&
                ASCOMMount::AcceptConnectedDriver(am->IsSimulator(), pinned)) {
                mountPtr.reset(am);
                startInRCControl = false;
                if (am->IsSimulator()) {
                    // Said every time, at the loudest level the log
                    // has. Forgetting that the mount is simulated is
                    // the hazard the blanket rejection was protecting
                    // against, so relaxing it owes a replacement
                    // signal rather than silence.
                    mountIsSimulated = true;
                    Log("[INIT] Mount: ASCOM (%s) - *** SIMULATED, nothing will move ***\n",
                        ascomProgId.c_str());
                    slog.System("Mount: SIMULATED via %s (AscomProgId in rig.ini)",
                                ascomProgId.c_str());
                } else {
                    Log("[INIT] Mount: ASCOM (%s) - CPWI in control\n", ascomProgId.c_str());
                    slog.System("Mount: CPWI active via %s", ascomProgId.c_str());
                }
            } else {
                if (am->IsSimulator())
                    Log("[INIT] ASCOM → simulator discovered, not named; ignoring. "
                        "Set AscomProgId in rig.ini to use one deliberately\n");
                am->Disconnect();
                delete am;
            }
        }
        // No ASCOM — try NexStar direct
        if (!mountPtr) {
            // Find the hand control rather than assuming a port: the
            // COM number of a USB-serial cable is assigned by Windows
            // and moves when the cable lands on a different USB port.
            // Rescanned here (not reused from the startup check) so a
            // cable replugged at the device dialog is still found.
            auto scan = raduk::core::FindNexStarPort(
                            raduk::core::RigConfig::Get().mountPort);
            auto* nm = new NexStarMount();
            if (scan.found && nm->Connect(scan.port)) {
                mountPtr.reset(nm);
                startInRCControl = true;
                Log("[INIT] Mount: NexStar direct (%s) — RC in control\n",
                    scan.port.c_str());
                slog.System("Mount: RC direct via %s", scan.port.c_str());
                g_mountForCrash = nm;
                // Once per process, not once per connect. atexit
                // handlers accumulate, and this lambda can now run
                // several times a session -- the standard only
                // guarantees 32 slots, and registering the same stop
                // thirty times would also run it thirty times.
                static bool s_exitStopArmed = false;
                if (!s_exitStopArmed) {
                    std::atexit(EmergencyStopOnExit);
                    s_exitStopArmed = true;
                }
                Log("[SAFETY] Heartbeat watchdog armed (timeout=%dms)\n",
                    raduk::core::NexStarMount::WATCHDOG_TIMEOUT_MS);
            } else {
                delete nm;
                Log("[INIT] Mount: not connected (no CPWI; no hand control on %s)\n",
                    raduk::core::DescribeProbedPorts(scan).c_str());
            }
        }
    } // end !useEmulator hardware path
#endif // RADUK_EMULATOR

        return mountPtr;
    };

    // Install the first backend. Null is a normal outcome -- no hand
    // control, no CPWI -- and the proxy answers as a disconnected mount,
    // which every caller already guards for.
    mountProxy.SetBackend(connectMountBackend());

    // The pointer handed to everything else, for the whole session. It
    // is the proxy, never a backend: backends come and go.
    IMount* mount = &mountProxy;

    // Phase 67: cord wrap. Measured as cumulative azimuth travel since
    // the cables were last dressed, not as absolute azimuth -- 350 and
    // 10 degrees are adjacent on a compass but 340 apart in travel.
    // Valid without alignment, which matters because the slewing that
    // wraps a cable usually happens while hunting for a target.
    raduk::core::CordWrapTracker cordWrap;
    cordWrap.SetLimit(raduk::core::RigConfig::Get().wrapLimitDeg);
    // Connecting is when the rig was set up, so treat it as dressed.
    cordWrap.CablesDressed();

#ifdef RADUK_EMULATOR
    // ── Phase 52: close the tracking loop in emulation ─────────
    // The synthetic planet's frame position follows the emulated mount's
    // pointing error, so guide pulses visibly re-centre it — and a wrong-sign
    // correction visibly runs away instead of passing silently.
    // Env knobs for adversarial runs:
    //   RADUK_EMU_ROTATION_DEG — camera orientation (default 0; set e.g. 137
    //                            to prove calibration measures the transform)
    //   RADUK_EMU_JITTER_AS    — seeing wander amplitude, arcsec (default 0.5)
    //   RADUK_EMU_DRIFT_AS     — RA tracking-error rate, arcsec/s (default 0.3)
    if (mount) {
        // The CONCRETE emulator, never the mount pointer.
        //
        // `mount` is a MountProxy since phase 46, and this line used to
        // static_cast it straight to MountEmulator -- a cast to a type
        // the object is not. It then WROTE through that pointer
        // (SetTrackingErrorRate below) while the camera's generator
        // thread read through it every frame, landing in the proxy's own
        // mutex and vector. The app reached its READY banner and died in
        // the CRT on the first frame, which is exactly the failure
        // IMount's own comments warn about twice: "a static_cast to
        // NexStarMount here previously corrupted ASCOMMount instances".
        //
        // dynamic_cast, and resolved per call rather than captured once:
        // type-checked, so a backend that is not an emulator answers "no
        // error" instead of corrupting memory, and a reconnect that
        // installs a new emulator backend is followed rather than
        // ignored. One cast per generated frame costs nothing against
        // generating the frame.
        cam.SetPointingErrorProvider([&mountProxy](float& eRA, float& eDec) {
            auto* em = dynamic_cast<raduk::core::MountEmulator*>(
                           mountProxy.Backend());
            return em ? em->GetPointingErrorArcsec(eRA, eDec) : false;
        });
        auto* emuMount = dynamic_cast<raduk::core::MountEmulator*>(
                             mountProxy.Backend());
        float rotDeg = 0.0f, jitAs = 0.5f, driftAs = 0.3f;
        char envBuf[32];
        if (GetEnvironmentVariableA("RADUK_EMU_ROTATION_DEG", envBuf, 32))
            rotDeg = (float)atof(envBuf);
        if (GetEnvironmentVariableA("RADUK_EMU_JITTER_AS", envBuf, 32))
            jitAs = (float)atof(envBuf);
        if (GetEnvironmentVariableA("RADUK_EMU_DRIFT_AS", envBuf, 32))
            driftAs = (float)atof(envBuf);
        cam.SetCameraRotationDeg(rotDeg);
        cam.SetSeeingJitterArcsec(jitAs);
        // Phase 107: a target that walks, to try ROI following indoors.
        if (const char* d = getenv("RADUK_EMU_DRIFT_PX_S")) {
            const float rate = (float)atof(d);
            cam.SetDriftPxPerSec(rate, rate * 0.5f);
        }
        if (emuMount) emuMount->SetTrackingErrorRate(driftAs, driftAs * 0.4f);
        Log("[INIT] Closed-loop emulation: rot=%.1f deg jitter=%.2f\" drift=%.2f\"/s\n",
            rotDeg, jitAs, driftAs);
    }

    // ── Phase 73: defocus in emulation ─────────────────────────
    // Without this the synthetic planet was equally sharp at every focuser
    // position, so an autofocus sweep fitted a parabola to noise and no
    // autofocus change could be verified on the bench at all. With it the
    // correct answer is a known number, so the bench test is an assertion.
    //   RADUK_EMU_TRUE_FOCUS   — step position of perfect focus
    //                            (default 168000, matching this rig's
    //                            measured ~168187; the EAF opens at
    //                            166500, so it starts 1500 steps out,
    //                            inside the +/-3000 sweep)
    //   RADUK_EMU_BLUR_PX_STEP — blur px per step off focus. The default
    //                            is NOT a literal here: it comes from
    //                            CameraEmulator::DefaultBlurPxPerStep(),
    //                            which divides the angular blur per step
    //                            by the emulator's own plate scale. It
    //                            was a literal 0.0083 and phase 91
    //                            changed that scale without it
    //                            following, so the blur ran 2.7x too
    //                            weak. See SetBlurPxPerStep.
    {
        int   trueFocus   = 168000;
        float blurPerStep = raduk::core::ASICamera::DefaultBlurPxPerStep();
        char  fb[32];
        if (GetEnvironmentVariableA("RADUK_EMU_TRUE_FOCUS", fb, 32))
            trueFocus = atoi(fb);
        if (GetEnvironmentVariableA("RADUK_EMU_BLUR_PX_STEP", fb, 32))
            blurPerStep = (float)atof(fb);
        // RADUK_EMU_FREEZE_SURFACE=1 stops planet rotation, belt turbulence,
        // the Great Red Spot and the moons, so an autofocus sweep measures
        // defocus and nothing else. Off by default: a static planet is
        // wrong for everything except a focus test.
        bool freezeSurface = false;
        if (GetEnvironmentVariableA("RADUK_EMU_FREEZE_SURFACE", fb, 32))
            freezeSurface = (atoi(fb) != 0);
        // RADUK_EMU_NOISE=N adds +/-N DN of per-pixel sensor noise. Needed
        // to exercise anything that averages frames: with the planet
        // rendered analytically and noiseless, a perfect aligned stack can
        // at best equal a single frame, so the gain is unmeasurable.
        int noiseDn = 0;
        if (GetEnvironmentVariableA("RADUK_EMU_NOISE", fb, 32))
            noiseDn = atoi(fb);
        cam.SetNoiseAmplitude(noiseDn);
        // RADUK_EMU_FPS=N lifts the emulator's 20 fps delivery cap.
        // Planetary capture is a 100-200 fps game at a small ROI, and
        // until this existed the decoupled recording path, the
        // run-sized write buffer and the SER writer had never been run
        // above 20 -- a fifth of what they were built for. 640x480 at
        // 120 fps is 37 MB/s; at 200 it is 61.
        if (GetEnvironmentVariableA("RADUK_EMU_FPS", fb, 32)) {
            const double want = atof(fb);
            if (want > 0.0) {
                cam.SetTargetFps(want);
                Log("[EMU] delivery rate set to %.0f fps (RADUK_EMU_FPS)\n",
                    cam.TargetFps());
            }
        }
        // RADUK_EMU_SEEING_AS gives frames a per-frame atmospheric
        // blur, so frame QUALITY varies and not just position. Without
        // it every frame is equally sharp and the seeing score, the
        // capture gate and the stacker's selection have nothing to
        // choose between.
        float seeingAs = 0.0f;
        if (GetEnvironmentVariableA("RADUK_EMU_SEEING_AS", fb, 32))
            seeingAs = (float)atof(fb);
        cam.SetSeeingBlurArcsec(seeingAs);
        // RADUK_EMU_PEAK_DN exposes the disc to a given peak level.
        // Default 0 keeps the old gain/exposure behaviour; set ~200 for
        // a realistically exposed frame with headroom, which is what
        // anything measuring a noise floor needs.
        int peakDn = 0;
        if (GetEnvironmentVariableA("RADUK_EMU_PEAK_DN", fb, 32))
            peakDn = atoi(fb);
        cam.SetPeakTargetDn(peakDn);
        cam.SetFreezeSurface(freezeSurface);
        // Render what is actually selected, not always Jupiter.
        // ApplyProfile keeps this in step on every later switch; this
        // covers launch, when no profile has been applied yet but one
        // is already active (ProfileManager defaults to index 0).
        // RADUK_EMU_TARGET overrides it for bench work.
        if (GetEnvironmentVariableA("RADUK_EMU_TARGET", fb, 32))
            cam.SetTargetName(fb);
        else if (const auto* ap = profiles.Active())
            cam.SetTargetName(ap->name);
        EmuApplyApparentSize(&cam);
        cam.SetTrueFocusPosition(trueFocus);
        cam.SetBlurPxPerStep(blurPerStep);
        cam.SetFocusPositionProvider([&focuser](int& pos) {
            pos = focuser.GetPosition();
            return true;
        });
        Log("[INIT] Defocus emulation: true focus=%d  %.4f px/step  "
            "surface=%s\n", trueFocus, blurPerStep,
            freezeSurface ? "FROZEN" : "live");
    }
#endif // RADUK_EMULATOR

    // ── Preview ring + capture thread ──────────────────────────
    // Phase 88: must follow the restored bit depth. This was hardcoded
    // ASI_IMG_RAW8, which was right while startup was always 8-bit --
    // once a session could come back 16-bit it allocated the ring at
    // half the needed size, every frame was rejected as too small, and
    // the app started with a dead preview and a readiness dialog
    // blaming the camera.
    int frameSizeBytes = FrameSizeBytes(
        roiWidth, roiHeight, roiBits == 16 ? ImageType::Raw16 : ImageType::Raw8);
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

    // Phase 57 (review S1): loopback-only by default. Set RADUK_WS_LAN=1 to
    // expose it on the LAN for the APEX dashboard machine (trusted WiFi only).
    //
    // P5.6: and a token either way. Loopback did not keep out a hostile
    // web page in the observer's own browser -- browsers let any page open
    // a WebSocket to localhost. The token lives in rig.ini (untracked) and
    // is NEVER logged: this log feeds the AI assistant's corpus. Only its
    // location is.
    {
        auto& rc = RigConfig::Get();
        if (rc.wsToken.empty()) {
            rc.wsToken = GenerateWsToken();
            if (!rc.wsToken.empty()) {
                rc.SaveWsToken();
                Log("[INIT] WebSocket: generated a new access token (rig.ini, [Rig] WsToken)\n");
            }
        }
        if (raduk::net::ws::TokenLooksStrong(rc.wsToken)) {
            wsServer.SetRequiredToken(rc.wsToken);
        } else {
            // Not replaced: overwriting a value somebody typed would hide
            // the mistake. Refusing everyone makes it visible instead.
            Log("[INIT] WebSocket: WsToken in rig.ini is not at least %zu hex characters - "
                "refusing ALL dashboard connections. Delete the line to have a new one generated.\n",
                raduk::net::ws::kMinTokenHex);
        }
    }
    {
        char wsEnv[8] = {};
        const auto& rc = RigConfig::Get();
        bool wsLan = (GetEnvironmentVariableA("RADUK_WS_LAN", wsEnv, 8) > 0
                      && wsEnv[0] == '1') || rc.wsLan;
        // WsLanOpen means nothing while the server only listens on this PC.
        const bool lanOpen = wsLan && rc.wsLanOpen;
        wsServer.SetLanOpen(lanOpen);
        wsServer.Start(8765, wsLan);
        Log("[INIT] WebSocket server on %s:8765%s%s\n",
            wsLan ? "0.0.0.0" : "127.0.0.1",
            wsLan ? " (home network: WsLan / RADUK_WS_LAN)" : "",
            lanOpen ? " - private-network clients need no token (WsLanOpen)" : "");
    }

    // Weather and sky conditions from Leoben (LeobenUrl / LeobenToken in
    // rig.ini). Off, silently, when no URL is set. Its own thread; the
    // readout below only reads the last good reading.
    raduk::net::LeobenClient leoben;
    leoben.Start(RigConfig::Get().leobenUrl, RigConfig::Get().leobenToken,
                 [](const std::string& s) { Log("%s\n", s.c_str()); });
    bool leobenAirLogged = false;

    // B3: the dashboard's preview picture. Encoded and sent on its own
    // thread; the render loop only decimates, twice a second, and only
    // while a client has asked for it.
    raduk::net::PreviewStreamer previewStreamer;
    previewStreamer.Start([&wsServer](const std::vector<uint8_t>& jpeg) {
        wsServer.BroadcastPreview(jpeg);
    });
    constexpr auto kPreviewInterval = std::chrono::milliseconds(500);
    auto lastPreview = std::chrono::steady_clock::now();

    // ── Capture manager ────────────────────────────────────────
    CaptureManager capturer;
    capturer.SetSessionLog(&slog);
    {
        CaptureConfig cfg;
        cfg.outputDir        = raduk::core::Paths::Captures();
        cfg.filePrefix       = "Jupiter";
        cfg.limitType        = LimitType::Frames;
        cfg.limitFrames      = 1000;
        cfg.limitTimeSec     = 60.0;
        cfg.writeBufferSlots = 512;
        cfg.format = raduk::core::CaptureFormatFromName(
            raduk::core::RigConfig::Get().captureFormat);
        capturer.SetConfig(cfg);
    }
    Log("[INIT] Capture manager ready\n");

    // Recording takes EVERY frame, from the capture thread, rather than
    // one per render pass. See the comment in the render loop for what
    // that was costing. The ring is untouched: it feeds the preview,
    // which is supposed to skip frames.
    //
    // Phase 107: this is also the only place that sees every frame, so
    // ROI following measures here, and moves the window from the thread
    // that is already talking to the camera.
    raduk::core::RoiFollowLink roiFollow;
    roiFollow.SetMover([&cam](int x, int y) { return cam.SetStartPos(x, y); });
    capture.SetFrameSink([&capturer, &roiFollow](const raduk::core::FrameSlot* slot) {
        capturer.SubmitFromCaptureThread(slot);
        roiFollow.OnFrame(slot->data, (int)slot->width, (int)slot->height,
                          (int)slot->bitDepth);
    });

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
    // Title carries the iteration version (Version.h, bumped every push)
    // plus this binary's compile timestamp — a stale exe is visible at
    // a glance instead of being inferred from behavior.
    Window window;
    wchar_t title[128];
    swprintf(title, 128, L"RadukCapture v%s  [build %hs %hs]%s",
             RADUK_VERSION_W, __DATE__, __TIME__,
             useEmulator ? L"  [EMULATOR]" : L"");
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
    // Phase 88: the sliders start from hardcoded constants, so after a
    // restore the camera and the panel would have disagreed on every
    // value -- and nudging a slider then wrote its stale number back to
    // the camera, undoing the restore. Same fault, same fix as Phase 85.
    if (cam.IsOpen()) {
        controls.SetCameraSliders(
            (int)cam.GetControlValue(ControlId::Gain),
            (int)cam.GetControlValue(ControlId::ExposureUs),        // slider is in us
            (int)cam.GetControlValue(ControlId::Gamma),
            (int)cam.GetControlValue(ControlId::WhiteBalanceRed),
            (int)cam.GetControlValue(ControlId::WhiteBalanceBlue),
            (int)cam.GetControlValue(ControlId::UsbBandwidth));
    }

    // ── Toolbar strip (Win32 native buttons above preview) ─────
    raduk::gui::ToolbarStrip toolbar;
    toolbar.Create(window.Handle(), PANEL_WIDTH);

    // ── GoTo Panel (Phase 6) ───────────────────────────────────
    raduk::gui::GoToPanel gotoPanel;
    gotoPanel.SetMount(mount);
    // Phase 66: last resort. rig.ini, Windows Location and a GPS probe
    // have all been tried above, and the mount was asked on connect.
    // If the site is still unknown, ask -- rather than falling back to
    // 0,0 and pushing a real place in the Gulf of Guinea to the mount,
    // which is what used to happen with nothing on screen to say so.
    {
        auto& rig = raduk::core::RigConfig::Get();
        if (!rig.SiteIsSet()) {
            Log("[INIT] Site still unknown after all sources - asking\n");
            auto res = raduk::gui::AskForSite(
                window.Handle(), controls.PanelHwnd(),
                "Windows Location was unavailable or declined, no GPS "
                "receiver answered, and the mount has no site of its own.");
            if (res.accepted) {
                // "manual" is deliberate: an explicit entry outranks any
                // later estimate and AdoptSite will refuse to replace it.
                rig.AdoptSite(res.latDeg, res.lonDeg,
                              res.elevM != 0.0 ? res.elevM
                                               : raduk::core::RigConfig::UNKNOWN_ELEV,
                              "manual");
                Log("[INIT] Site entered by hand and saved to rig.ini\n");
            } else {
                Log("[INIT] Site declined - alt/az tracking, the sky map and "
                    "the ADC model will not work until one is set\n");
                // notifBar is not constructed yet at this point in
                // startup; the log line is the record.
            }
        }
    }

    // Phase 67 step 4: an unaligned mount will not track, and the fix
    // is on the hand control rather than in here. Say so at the point
    // it matters, and translate "point the tube north" into the
    // landmarks actually in front of the user.
    {
        auto& rig = raduk::core::RigConfig::Get();
        if (rig.showAlignHelper && mount && mount->IsConnected()
            && !mount->IsAligned()) {
            Log("[ALIGN] Mount reports NOT aligned - showing Quick Align helper\n");
            bool never = raduk::gui::ShowQuickAlignHelper(window.Handle(),
                                                          controls.PanelHwnd());
            if (never) {
                rig.showAlignHelper = false;
                rig.Save();
                Log("[ALIGN] Helper suppressed at user request\n");
            }
        }
    }

    // Phase 68: say plainly whether the rig is ready. The device check
    // at startup answers "is it plugged in" and runs before the camera
    // or mount exist; this runs where alignment, tracking and the site
    // are all knowable.
    //
    // Logged every launch, shown to nobody. A list that appears each
    // time becomes a speed bump within a week, and the three manual
    // items are reminders rather than faults - they must never be the
    // reason for an interruption. The dialog is step 3.
    auto gatherReadiness = [&]() -> raduk::core::ReadyReport {
        raduk::core::ReadyInputs ri;
        ri.camOpen        = cam.IsOpen();
        ri.framesSeen     = (long long)capture.Stats().framesCaptured.load();
        ri.droppedFrames  = capture.Stats().droppedFrames.load();
        ri.focuserOpen    = focuser.IsOpen();
        ri.focusPos       = focuser.IsOpen() ? focuser.GetPosition() : 0;
        ri.mountConnected = mount && mount->IsConnected();
        ri.mountAligned   = ri.mountConnected && mount->IsAligned();
        ri.trackingKnown  = ri.mountConnected && mount->TrackingModeKnown();
        ri.trackingOn     = ri.trackingKnown &&
                            mount->GetTrackingMode() != IMount::OFF;
        ri.siteSet        = raduk::core::RigConfig::Get().SiteIsSet();
        ri.profileLoaded  = profiles.Active() != nullptr;
        ri.filterName     = raduk::core::RigConfig::Get().filterName;
        ri.filterLowNm    = raduk::core::RigConfig::Get().filterLowNm;
        ri.filterHighNm   = raduk::core::RigConfig::Get().filterHighNm;
        {
            ULARGE_INTEGER avail = {}, total = {}, dummy = {};
            // The directory actually in use, not a literal - the user
            // can point captures anywhere, and checking the wrong
            // volume is worse than not checking.
            std::string capDir = controls.GetOutputDir();
            if (!capDir.empty() &&
                GetDiskFreeSpaceExA(capDir.c_str(), &avail, &total, &dummy))
                ri.diskFreeGB = (double)avail.QuadPart / 1073741824.0;
        }

        return raduk::core::EvaluateReadiness(ri);
    };


    gotoPanel.SetLocation(raduk::core::RigConfig::Get().siteLatDeg,
                          raduk::core::RigConfig::Get().siteLonDeg);

    gotoPanel.Create(window.Handle(), 0, 0, 500, 750);

    // ── Sky Map (Phase 6.1) ────────────────────────────────────
    // Positioned to the right of the GoTo panel
    // Click any object → GoTo fires automatically
    //
    // ── Phase 90: there are two ────────────────────────────────
    //
    // A sky map is the single best thing to have on a wall at a star
    // party -- it answers "what are we looking at and where is it"
    // without anyone having to ask. But that is the map doing a job for
    // other people, and it leaves the owner without one to drive from.
    //
    // So: skyMap stays where it is, maximised on whichever screen the
    // layout gives it, and skyMap2 opens on the control screen. M and
    // Shift+M. Everything either of them can do, both can do -- they
    // are fed and wired through kMaps below rather than by repeating
    // each line, because a pair of objects that must be kept in step by
    // hand is the shape of defect this file already has five of (D3).
    raduk::gui::SkyMapWindow skyMap;    // the one on the wall
    raduk::gui::SkyMapWindow skyMap2;   // the one to drive from
    raduk::gui::SkyMapWindow* const kMaps[] = { &skyMap, &skyMap2 };

    {
        // Both open centred on the primary; the workspace layout moves
        // them afterwards. The second is smaller because its default
        // home is the control screen, where it shares space with the
        // preview rather than owning a monitor.
        const int sw = GetSystemMetrics(SM_CXSCREEN);
        const int sh = GetSystemMetrics(SM_CYSCREEN);
        const int mw = 620, mh = 640;
        skyMap.Create(window.Handle(), (sw - mw) / 2, (sh - mh) / 2, mw, mh);
        const int w2 = 560, h2 = 580;
        skyMap2.Create(window.Handle(), (sw - w2) / 2, (sh - h2) / 2, w2, h2);
    }
    for (auto* m : kMaps) { m->SetMount(mount); m->Hide(); }

    // ── Phase 42: five-monitor workspace ──────────────────────
    // Detect how many screens there are, pick the layout profile for
    // that count, and open every window already on the screen it
    // belongs on. Keyed by COUNT so the app has a sane answer for five
    // screens, for one, and for whatever it finds in between, rather
    // than one saved arrangement that is wrong as soon as a cable
    // moves. Everything it does is clamped to the current desktop:
    // a window placed where no monitor is would be invisible but
    // focusable, which looks exactly like a broken feature.
    raduk::gui::WorkspaceLayout workspace;
    const std::string kWorkspacePath = "workspace.ini";
    bool workspaceHadConfig = false;
    workspace.SetLogCallback([](const std::string& m) { Log("%s\n", m.c_str()); });
    workspace.Detect();
    workspaceHadConfig = workspace.Load(kWorkspacePath);
    workspace.SelectProfile();

    // Click-to-GoTo: sky map click → GoToPanel.RequestGoTo
    // This triggers profile auto-load + mount slew + all status updates
    // Same code path as clicking GOTO SELECTED in the GoTo panel
    // Whatever we slew to becomes the TELESCOPE card's GoTo target, by
    // every route. That box is the input to a button that moves the
    // telescope, and it used to hold whatever was last typed -- so after
    // slewing to Saturn from the map, GOTO still said Jupiter, and a
    // mis-click sent the mount somewhere nobody had asked for.
    gotoPanel.SetTargetChangedCallback([&](const std::string& name) {
        controls.SetGoToTarget(name);
    });

    // Both maps, same handler. Either can start a slew, and both ask
    // for the click-to-confirm first -- that guard is the reason a map
    // is safe to leave on a screen at an event, and it does not become
    // optional just because there are two of them now.
    for (auto* m : kMaps)
        m->SetGoToCallback([&](double ra, double dec, const std::string& name) {
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
    // Wire sky map right-click → info panel, from either map
    for (auto* m : kMaps)
        m->SetInfoCallback([&](const std::string& name,
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
    // Phase 85: filled in further down, once applyFormat and the panel
    // setters are in scope. Declared here because the callbacks below
    // are registered before that point and capture it by reference --
    // they run later, by which time it holds both hooks.
    ProfileApplyHooks profileHooks;

    gotoPanel.SetLogCallback([&](const std::string& m) {
        Log("[GOTO] %s\n", m.c_str());
        slog.Mount("%s", m.c_str());
    });
    // ── Phase 97.5: the two edges of a mosaic panel ───────────
    //
    // The panel drives the mount; the recorder lives here, so the two
    // capture edges come back out as callbacks. Wiring them is what
    // ENABLES mosaics at all -- StartMosaic refuses without them,
    // rather than slewing eighteen times and recording nothing.
    //
    // Each panel is its own recording, which is what a stacker wants:
    // eighteen files to register separately and then join, not one
    // file containing eighteen different parts of the Moon.
    // Phase 108: this run's window, worked out when it starts. 0 is none.
    double      derotWindowSec = 0.0;
    bool        derotWarned    = false;
    std::string derotBody;

    gotoPanel.SetMosaicCapture(
        [&](int ordinal, int total) -> bool {
            if (capturer.IsRecording()) {
                Log("[MOSAIC] already recording; refusing panel %d\n",
                    ordinal);
                return false;
            }
            // roiBits, not a literal: it tracks what the camera GAVE
            // us rather than what was asked for, which is the whole
            // reason it exists and the reason the RECORD button was
            // wrong about it once already.
            //
            // Phase 108: the config still holds the last planet's window.
            derotWindowSec = 0.0;
            {
                CaptureConfig mc = capturer.Config();
                mc.derotationStopSec = 0.0;
                capturer.SetConfig(mc);
            }
            if (!capturer.StartRecording(roiWidth, roiHeight, roiBits)) {
                Log("[MOSAIC] StartRecording failed on panel %d\n", ordinal);
                return false;
            }
            usbMgr.SetCapturing(true);
            Log("[MOSAIC] panel %d of %d recording\n", ordinal, total);
            slog.Capture("Mosaic panel %d of %d", ordinal, total);
            return true;
        },
        [&]() {
            // A normal finish, so the usual post-capture handling runs
            // for each panel exactly as it would for a manual run.
            capturer.StopRecording();
            usbMgr.SetCapturing(false);
        });

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
                mount, slog, profileHooks);
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



    // Suspend tracker corrections around manual jogs (resume after a 500ms
    // settle). The generation counter prevents a stale resume timer from
    // re-enabling corrections in the middle of a newer jog: every start OR
    // stop bumps the generation, and a timer only resumes if no newer jog
    // event happened while it slept.
    auto jogGeneration = std::make_shared<std::atomic<int>>(0);
    // Phase 47b. suspendTrackerForJog is defined here, before the
    // preview renderer exists, and is also copied into a detached
    // thread. A shared_ptr to the action solves both: the lambda below
    // captures it by value, so the detached copy keeps its own
    // reference, and the real work is installed once the renderer is
    // up. Until then it is a no-op, which is correct -- there can be
    // no solution to invalidate before there is anything to draw it.
    auto invalidateWcs = std::make_shared<std::function<void()>>([]{});

    auto suspendTrackerForJog = [&tracker, invalidateWcs, jogGeneration](bool start) {
        // Phase 47b. Every manual jog and every recentre pulse passes
        // through here, so it is where a solve-derived readout stops
        // being true. A GoTo is covered inside GoToPanel instead,
        // because its own buttons never reach this lambda.
        //
        // What this does NOT catch is somebody driving the mount from
        // the hand controller, which the app never sees. The age
        // ceiling in PixelWCS.h is the only guard against that, and it
        // is a weak one; detecting it properly needs the polled alt/az
        // turned back into RA/Dec, which this rig has no inverse for
        // yet. Recorded in PHASE-STATUS.md rather than left implied.
        if (start) (*invalidateWcs)();
        if (start) {
            jogGeneration->fetch_add(1);
            if (tracker.IsRunning()) tracker.SuspendCorrections();
        } else {
            int gen = jogGeneration->fetch_add(1) + 1;
            std::thread([&tracker, jogGeneration, gen]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                if (jogGeneration->load() == gen) tracker.ResumeCorrections();
            }).detach();
        }
    };

    // ── Keep the two jog-rate selectors in step ────────────────
    // The main panel and the floating Mount Controller each have their own
    // 9 / FAST / MED / FINE row, and until v1.2.0 each owned a separate
    // rate variable, both starting at 9. Picking FINE in the floating panel
    // left the main panel's value at 9 - and the ARROW KEYS read that one,
    // so they kept slewing at full speed while the button showed FINE.
    // Mirror each change into the other; the setters do not notify back, so
    // this cannot loop.
    mountCtrl.SetRateChangeCallback([&](int r) {
        controls.SetJogRate(r);
        slog.Mount("Jog rate %dx (from mount controller)", r);
    });
    controls.SetJogRateChangeCallback([&](int r) {
        mountCtrl.SetJogRate(r);
        slog.Mount("Jog rate %dx (from main panel)", r);
    });

    mountCtrl.SetJogCallback([&](int axis, int dir, bool start, int rate) {
        if (!mount) return;
        if (start) {
            if (!mount->IsConnected()) return;
            mount->SlewFixed(axis, dir, rate);
            suspendTrackerForJog(true);
            slog.Mount("MountCtrl: jog axis=%d dir=%d rate=%d start", axis, dir, rate);
        } else {
            // Stop is sent even if IsConnected() flickered — never strand a
            // moving mount because a status flag dropped mid-jog.
            mount->StopSlew(axis);
            suspendTrackerForJog(false);
            slog.Mount("MountCtrl: jog stop axis=%d", axis);
        }
    });
    mountCtrl.SetEmergencyStopCallback([&]() {
        Log("[SAFETY] MountCtrl: STOP pressed\n");
        slog.Mount("Emergency stop (MountCtrl)");
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
    aiChat.SetLogCallback([](const std::string& s) { Log("%s\n", s.c_str()); });
    Log("[INIT] AI chat panel ready\n");

    // Phase 42b: the histogram as a window of its own, for one of the
    // spare screens. Hidden until Shift+H; the overlay on the preview
    // stays as it was for single-screen work.
    raduk::gui::HistogramWindow histWindow;
    histWindow.Create(window.Handle(), 200, 200, 900, 520);
    Log("[INIT] Histogram window ready\n");

    // Phase 42c: sharpness over a long window, for the other spare
    // screen. Its own history, far longer than the overlay's five
    // seconds -- the overlay answers "is it sharp now", this answers
    // "is it drifting".
    raduk::gui::SharpnessWindow sharpWindow;
    sharpWindow.Create(window.Handle(), 240, 240, 1100, 520);
    Log("[INIT] Sharpness window ready\n");

    // Phase 42d: temperature over an hour, not the overlay's minute --
    // long enough to see the tube cooling pull focus with it.
    raduk::gui::TemperatureWindow tempWindow;
    tempWindow.Create(window.Handle(), 280, 280, 1100, 420);
    Log("[INIT] Temperature window ready\n");

    // Phase 42f: status in a window of its own. It was the last section
    // the control panel painted onto its own DC instead of building
    // from child controls, which put it on a different plane from
    // everything around it -- see StatusWindow.h.
    raduk::gui::StatusWindow statusWindow;
    statusWindow.Create(window.Handle(), 320, 320, 900, 220);

    // Phase 35: the ADC alignment tool. Starts hidden -- it is a job you
    // do once at the start of a session, not something you watch all
    // night, and on this desk a screen is worth more to anything you do
    // watch. Shift+A.
    raduk::gui::ADCAlignWindow adcAlignWindow;
    adcAlignWindow.Create(window.Handle(), 360, 360, 980, 560);
    raduk::core::ADCAlignment adcAlign;
    bool adcHaveFrame = false;   // set by the measuring pass each loop
    raduk::core::SkyTracker skyTracker;

    // ── Phase 89: the share view ──────────────────────────────
    // A second, deliberately empty window for a screen pointed at other
    // people. Hidden until Shift+V, and the sink below is the only
    // thing the preview does for it -- an empty std::function on every
    // night that is not a star party.
    raduk::gui::ShareView shareView;
    shareView.Create(window.Handle(), 200, 200, 1280, 720);

    // ── Phase 92: the object panel ────────────────────────────
    // The educational twin of the share view -- that window is the
    // photograph, this one is the caption. Hidden until Shift+I.
    raduk::gui::ObjectPanel objectPanel;
    objectPanel.Create(window.Handle(), 240, 240, 1100, 700);
    // The sink is wired further down, where `preview` exists.

    Log("[INIT] Status window ready\n");

    // ── Phase 42: place the windows ───────────────────────────
    // This has to be the point where the LAST window has been created,
    // and it has moved twice for that reason: Place() on a window that
    // does not exist yet finds a null HWND and silently does nothing,
    // so a placement simply goes missing with no error anywhere. The
    // mount panel and then these two were each lost that way. Anything
    // added later belongs after this line, not before it.
    //
    // Still ahead of the message loop, so the app comes up already
    // arranged instead of visibly rearranging itself.
    // ── D3: every window we own, found rather than listed ────
    //
    // kPlacedWindows is the roster the workspace places and saves by
    // NAME, and it has to be hand-maintained because a saved layout
    // needs a role for each entry. Three other things had quietly come
    // to depend on it being COMPLETE, which is a different and much
    // weaker claim -- a window added without a line here simply fell
    // out of each of them, silently, and the only symptom was a panel
    // left in day colours on a dark night or one that Ctrl+Shift+Home
    // did not rescue.
    //
    // Anything that just wants "all our windows" asks the OS instead.
    // EnumThreadWindows returns every top-level window on the UI
    // thread, which includes a few the system creates there (IME,
    // tooltips), so the filter is our own class prefix. Every window
    // class this app registers begins with "Raduk", and a new one that
    // did not would not be ours.
    //
    // The preview is a CHILD of the main window and so is not returned
    // here. It does not need to be: RDW_ALLCHILDREN reaches it.
    auto ourWindows = []() {
        std::vector<HWND> v;
        EnumThreadWindows(GetCurrentThreadId(),
            [](HWND h, LPARAM lp) -> BOOL {
                wchar_t cls[64] = {};
                if (GetClassNameW(h, cls, 64) > 0 &&
                    wcsncmp(cls, L"Raduk", 5) == 0)
                    reinterpret_cast<std::vector<HWND>*>(lp)->push_back(h);
                return TRUE;
            }, reinterpret_cast<LPARAM>(&v));
        return v;
    };

    const std::vector<std::pair<const char*, HWND>> kPlacedWindows = {
        { "main",   window.Handle()    },
        { "skymap",  skyMap.Handle()  },
        { "skymap2", skyMap2.Handle() },
        { "goto",   gotoPanel.Handle() },
        { "mount",  mountCtrl.Handle() },
        { "help",   helpWindow.Handle()},
        { "aichat", aiChat.Handle()    },
        { "histogram", histWindow.Handle() },
        { "sharpness", sharpWindow.Handle() },
        { "temperature", tempWindow.Handle() },
        { "status", statusWindow.Handle() },
        { "adcalign", adcAlignWindow.Handle() },
        { "share",  shareView.Handle()  },
        { "objectpanel", objectPanel.Handle() },
    };
    bool s_restoredAnyPanel = false;
    // ── Phase 101: apply the saved open/closed state ──────────
    //
    // Startup only. Place() also runs on every display change, which
    // is why it never shows a window itself and why this does not go
    // inside it: a monitor coming or going is not a reason to reopen a
    // panel somebody closed.
    //
    // Before the Place loop, not after. Place maximises a role only
    // when the window is already visible, so a panel saved open AND
    // maximised has to be open by the time Place reaches it or it
    // comes back open but restored down.
    //
    // Each panel gets its own Show()/Hide() rather than a ShowWindow
    // on its handle, because several of them do real work there. Not
    // listed here: "main", which is the control window and is never a
    // panel to be closed.
    const std::vector<std::pair<const char*, std::function<void(bool)>>>
    kPanelVisibility = {
        { "skymap",      [&](bool v){ v ? skyMap.Show()         : skyMap.Hide(); } },
        { "skymap2",     [&](bool v){ v ? skyMap2.Show()        : skyMap2.Hide(); } },
        { "goto",        [&](bool v){ v ? gotoPanel.Show()      : gotoPanel.Hide(); } },
        { "mount",       [&](bool v){ v ? mountCtrl.Show()      : mountCtrl.Hide(); } },
        { "help",        [&](bool v){ v ? helpWindow.Show()     : helpWindow.Hide(); } },
        { "aichat",      [&](bool v){ v ? aiChat.Show()         : aiChat.Hide(); } },
        { "histogram",   [&](bool v){ v ? histWindow.Show()     : histWindow.Hide(); } },
        { "sharpness",   [&](bool v){ v ? sharpWindow.Show()    : sharpWindow.Hide(); } },
        { "temperature", [&](bool v){ v ? tempWindow.Show()     : tempWindow.Hide(); } },
        { "status",      [&](bool v){ v ? statusWindow.Show()   : statusWindow.Hide(); } },
        { "adcalign",    [&](bool v){ v ? adcAlignWindow.Show() : adcAlignWindow.Hide(); } },
        { "share",       [&](bool v){ v ? shareView.Show()      : shareView.Hide(); } },
        { "objectpanel", [&](bool v){ v ? objectPanel.Show()    : objectPanel.Hide(); } },
    };
    //
    // A named lambda because it has to run twice. The first pass has to
    // precede Place for the maximise ordering; but startup carries on
    // for another two and a half thousand lines after that, and
    // anything in them that shows a window silently wins. One did: a
    // loop near the toolbar wiring force-showed the GoTo, help and AI
    // chat windows unconditionally, so those three never kept a closed
    // state no matter what the file said. That loop is fixed to respect
    // the saved state, and this runs again at the end of startup so the
    // next one cannot quietly undo the restore the same way.
    auto applySavedPanelState = [&](const char* when) {
        int opened = 0, closed = 0, unknown = 0;
        for (const auto& [role, apply] : kPanelVisibility) {
            const auto st = workspace.StateFor(role);
            if (st == raduk::gui::WorkspaceLayout::SavedState::Unknown) {
                ++unknown;
                continue;      // no line on disk: leave the app's default
            }
            const bool open =
                (st == raduk::gui::WorkspaceLayout::SavedState::Open);
            apply(open);
            if (open) ++opened; else ++closed;
            Log("[LAYOUT] %s: restored %s (%s)\n",
                role, open ? "open" : "closed", when);
        }
        Log("[LAYOUT] panel state (%s): %d open, %d closed, "
            "%d with no saved state\n", when, opened, closed, unknown);
        return opened > 0;
    };
    s_restoredAnyPanel = applySavedPanelState("startup");

    for (const auto& [role, hwnd] : kPlacedWindows)
        workspace.Place(role, hwnd);

    // Some panels take the foreground when shown, and restoring
    // several in a row leaves whichever came last holding focus. After
    // Place, not before: placing a window can raise it too. The
    // control window is where the keyboard belongs.
    if (s_restoredAnyPanel) SetForegroundWindow(window.Handle());

    if (!workspaceHadConfig && workspace.Save(kWorkspacePath))
        Log("[LAYOUT] wrote a starting %s\n", kWorkspacePath.c_str());

    // ── D2: the chrome Windows draws for us ───────────────────
    //
    // Found by the same enumeration D3 uses, so a window added later
    // is covered without being registered anywhere. Re-applied on
    // every night-vision toggle, because the list and tab colours
    // follow the mode even though the title bars do not.
    auto styleChrome = [&]() {
        for (HWND h : ourWindows()) {
            raduk::gui::Theme::DarkTitleBar(h);
            raduk::gui::Theme::StyleStockControls(h);
        }
    };
    styleChrome();

    // Ctrl+Shift+End records the arrangement as it stands. Drag the
    // windows where you want them, press it once, and that is what the
    // app opens with from then on. A generated default will never guess
    // an arrangement somebody built by hand.
    window.SetSaveLayoutHandler([&]() {
        workspace.CaptureCurrent(kPlacedWindows);
        const bool wrote = workspace.Save(kWorkspacePath);

        // Confirm against the FILE, not against Save() returning true.
        // fprintf not complaining is not the same as the arrangement
        // being on disk: buffering, a full disk, or a handle held by
        // something else can all leave a truthful-looking true beside a
        // file that is empty or stale. Re-reading costs a few
        // milliseconds and is the difference between telling the owner
        // it saved and knowing that it did.
        const auto proof = workspace.VerifyOnDisk(kWorkspacePath,
                                                  workspace.MonitorCount());
        const int expected = (int)kPlacedWindows.size();
        const bool ok = wrote && proof.Good(expected);

        if (ok)
            Log("[LAYOUT] saved %d window(s) to %s (%ld bytes, verified)\n",
                proof.rolesOnDisk, kWorkspacePath.c_str(), proof.bytes);
        else
            Log("[LAYOUT] SAVE FAILED for %s - wrote=%d exists=%d section=%d "
                "roles=%d/%d bytes=%ld\n",
                kWorkspacePath.c_str(), wrote ? 1 : 0, proof.fileExists ? 1 : 0,
                proof.sectionFound ? 1 : 0, proof.rolesOnDisk, expected,
                proof.bytes);

        // Name the windows that were written. The owner asked whether
        // the mount panel was included; a count alone cannot answer
        // that, and a list answers it for every role at once.
        // Wrapped at six per line. Eleven roles on one line overflowed
        // the toast and ellipsed the tail -- which hid exactly the kind
        // of question the list is there to answer.
        std::vector<std::wstring> roleLines;
        {
            std::wstring cur;
            int onLine = 0;
            for (const auto& [role, hwnd] : kPlacedWindows) {
                if (!hwnd || !IsWindow(hwnd)) continue;
                const std::string r(role);
                if (onLine == 6) { roleLines.push_back(cur); cur.clear(); onLine = 0; }
                if (!cur.empty()) cur += L"  ";
                cur += std::wstring(r.begin(), r.end());
                onLine++;
            }
            if (!cur.empty()) roleLines.push_back(cur);
        }

        std::vector<std::wstring> lines;
        if (ok) {
            wchar_t b[200];
            swprintf(b, 200, L"%d windows on %d screens, %ld bytes on disk",
                     proof.rolesOnDisk, proof.monitorCount, proof.bytes);
            lines.push_back(b);
            for (const std::wstring& rl : roleLines) lines.push_back(rl);
            lines.push_back(L"This is what the app will open with.");
        } else {
            lines.push_back(L"The file could not be verified after writing.");
            lines.push_back(L"The arrangement has NOT been saved.");
            lines.push_back(L"See raduk_log.txt for what failed.");
        }

        raduk::gui::ConfirmToast::Show(
            window.Handle(),
            ok ? L"LAYOUT SAVED" : L"LAYOUT NOT SAVED",
            lines, ok, 5000);
    });

    // Ctrl+Shift+Home brings everything home. Registered globally, so it
    // still works when the window you would otherwise click is sitting
    // on a monitor that has gone away.
    window.SetGatherWindowsHandler([&]() {
        // D3: everything we own. The help calls this "bring EVERY
        // window back", and a window stranded on a monitor that has
        // gone away is exactly as unreachable whether or not the
        // layout file happens to know its name.
        workspace.GatherToPrimary(ourWindows());
    });
    // Phase 50a DB wire — both AI panel and object info panel share the same DB
    objectInfo.SetKnowledgeDB(&aiChat.GetKnowledgeDB());

    // ── Phase 92a: say whether the knowledge base is there ────
    //
    // It reports itself with printf, which a GUI-subsystem build
    // discards -- the same thing that hid the AI panel's failure in
    // P0.1, and the reason this went unnoticed for months. The
    // database opens or it does not, and raduk_log.txt should say
    // which, because a graceful failure that nothing ever notices is
    // indistinguishable from a feature that does not exist.
    {
        const auto& kdb = aiChat.GetKnowledgeDB();
        if (kdb.IsAvailable()) {
            Log("[KNOWLEDGE] object_knowledge.db open - %zu objects reachable\n",
                (size_t)kdb.ObjectCount());
        } else {
            Log("[KNOWLEDGE] NOT available - the object panel and the "
                "assistant will both fall back to ephemeris only. Check "
                "sqlite3.dll beside the exe and data\\object_knowledge.db\n");
            slog.System("Object knowledge base unavailable");
        }
    }

    // Phase 40: Notification Bar
    raduk::gui::NotificationBar notifBar;

    // Phase P5.3: a simulated mount must never be a quiet fact.
    //
    // connectMountBackend decides this before the notification bar
    // exists, and again on every reconnect long after it does, so the
    // announcement is a lambda called from both rather than two copies
    // of the wording. It fires on the TRANSITION, so a reconnect from
    // simulator to real mount clears the claim instead of leaving the
    // last warning standing.
    bool mountSimAnnounced = false;
    auto announceMountSim = [&]() {
        if (mountIsSimulated == mountSimAnnounced) return;
        mountSimAnnounced = mountIsSimulated;
        if (mountIsSimulated)
            notifBar.NotifyWarn(L"Mount is SIMULATED (AscomProgId in rig.ini) "
                                L"- slews and jogs move nothing");
        else
            notifBar.NotifyInfo(L"Mount is a real telescope again");
    };
    // The startup mount choice was made before the bar existed, so it
    // is announced here, the first moment there is anywhere to say it.
    announceMountSim();
    notifBar.Create(window.Handle(), PANEL_WIDTH);
    notifBar.SetDismissCallback([&]() {
        window.SetNotifBarHeight(notifBar.Height());
        notifBar.SnapToMainWindow();
    });
    Log("[INIT] Notification bar ready\n");

    // Which card drives the preview. The rig runs five screens off two
    // GPUs, and the live preview presents through whichever adapter
    // drives the screen it is on; on the other card every frame costs a
    // cross-adapter copy for no gain, since both show the same picture.
    //
    // Said once at startup and once per move, never per frame.
    std::wstring previewAdapter = workspace.AdapterFor(window.Handle());
    {
        const std::wstring primaryAdapter = workspace.PrimaryAdapter();
        if (!previewAdapter.empty())
            Log("[LAYOUT] preview presents through %ls\n", previewAdapter.c_str());
        if (!previewAdapter.empty() && !primaryAdapter.empty() &&
            previewAdapter != primaryAdapter) {
            Log("[LAYOUT] WARNING preview is on %ls, not the primary's %ls - "
                "every frame costs a cross-adapter copy\n",
                previewAdapter.c_str(), primaryAdapter.c_str());
            notifBar.NotifyWarn(L"Preview is on the second GPU - every frame "
                                L"costs a cross-adapter copy");
        }
    }

    // ── The screens changed under us ──────────────────────────
    //
    // A monitor woke, slept, was unplugged, or a driver reset the
    // topology. Deliberately dull and defensive, and it logs what it
    // did: this fires during exactly the moments the rig is most
    // fragile, and a 3am misbehaviour has to be diagnosable afterwards.
    //
    // It re-places by saved layout rather than only rescuing. A monitor
    // coming back from sleep should get its windows back, not leave
    // them piled on the primary where a bare rescue would put them.
    window.SetDisplayChangeHandler([&]() {
        const int before = workspace.MonitorCount();
        const int now    = workspace.Detect();
        Log("[LAYOUT] display change: %d screen(s) -> %d\n", before, now);
        slog.Write("LAYOUT", "Display topology changed: %d screens -> %d",
                   before, now);

        // The profile is keyed by monitor count, so a changed count
        // means a different layout applies.
        if (now != before) workspace.SelectProfile();

        for (const auto& [role, hwnd] : kPlacedWindows)
            workspace.Place(role, hwnd);

        // Anything still unreachable goes home. Place() already clamps
        // to a live screen, so this should find nothing -- but "should"
        // is not what you want standing between you and an unreachable
        // window in the dark.
        // D3: checked across every window we own. Place() above moves
        // only the ones with a role; this is the net under all of
        // them, and a window with no line in the layout file is the
        // one most likely to be left somewhere unreachable.
        std::vector<HWND> stranded;
        for (HWND hwnd : ourWindows()) {
            if (!IsWindowVisible(hwnd)) continue;
            RECT r{};
            if (!GetWindowRect(hwnd, &r)) continue;
            if (!workspace.WindowReachable(r)) {
                wchar_t cls[64] = {};
                GetClassNameW(hwnd, cls, 64);
                Log("[LAYOUT]   %ls was stranded - bringing it home\n", cls);
                stranded.push_back(hwnd);
            }
        }
        if (!stranded.empty()) {
            workspace.GatherToPrimary(stranded);
            slog.Write("LAYOUT", "%d window(s) were stranded and recovered",
                       (int)stranded.size());
            notifBar.NotifyWarn(L"A screen went away - windows that were on "
                                L"it have been brought back");
        }

        // The preview may now be presenting through the other card.
        const std::wstring nowAdapter = workspace.AdapterFor(window.Handle());
        if (!nowAdapter.empty() && nowAdapter != previewAdapter) {
            Log("[LAYOUT] preview now presents through %ls\n", nowAdapter.c_str());
            const std::wstring primaryAdapter = workspace.PrimaryAdapter();
            if (!primaryAdapter.empty() && nowAdapter != primaryAdapter)
                notifBar.NotifyWarn(L"Preview moved to the second GPU - every "
                                    L"frame now costs a cross-adapter copy");
            previewAdapter = nowAdapter;
        }
    });

    // Phase 72: one implementation for both entry points. Startup
    // calls it only when something is wrong; the READY button calls it
    // unconditionally, because someone pressing it has asked to see
    // the list whatever it says.
    auto showReadiness = [&]() {
        bool allTicked = raduk::gui::ShowReadinessDialog(
            window.Handle(), controls.PanelHwnd(), gatherReadiness,
            [&](const std::string& section) {
                helpWindow.ShowSection(section);
            });
        if (allTicked) {
            // They confirmed the cables among the rest, so the wrap
            // count should start from here rather than from connect.
            cordWrap.CablesDressed();
            Log("[READY] All manual items ticked - cord wrap reset\n");
        }
    };
    controls.SetReadinessCheckCallback([&]() {
        Log("[READY] Re-check requested\n");
        showReadiness();
    });

    {
        // The capture thread started moments ago. At startup the first
        // frame can easily arrive after this check would have run, and a
        // check that races the pipeline it is checking reports a failure
        // that is not real -- then blocks the launch with a modal dialog
        // about it, before any of the wiring below has happened. Give the
        // first frame a bounded chance to appear.
        //
        // The user-requested re-check deliberately does not wait: by then
        // the app has been running, and no frames means no frames.
        for (int i = 0; i < 40 && capture.Stats().framesCaptured.load() == 0; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

        auto report = gatherReadiness();
        Log("[READY] %s\n", report.Summary().c_str());
        for (const auto& it : report.items) {
            // Only the things that need attention. Listing the passes
            // every launch buries them.
            if (it.state == raduk::core::ReadyState::Pass) continue;
            if (it.state == raduk::core::ReadyState::Manual) continue;
            Log("[READY]   %-10s %-5s %s\n", it.name.c_str(),
                raduk::core::ReadyStateName(it.state), it.detail.c_str());
        }
        if (report.AnyFailed())
            slog.System("Not ready: %s", report.Summary().c_str());

        // Phase 68 step 3: interrupt only when something is actually
        // wrong. Manual items never qualify on their own -- they are
        // reminders, and a dialog raised for them every launch is what
        // teaches people to dismiss it unread.
        if (report.WorthShowing() &&
            raduk::core::RigConfig::Get().showReadinessCheck) {
            showReadiness();
        }
    }

    // Phase 67: the only legitimate way to zero the cord-wrap total.
    // The app cannot see a cable, so it has to be told when one has
    // been freed; everything the warning later claims is measured from
    // this moment.
    // Phase 67 step 5: warn before a slew that would wrap the cables
    // further than the configured limit.
    //
    // The mount picks its own path for a GoTo -- the protocol carries
    // coordinates, not a direction -- so the forecast assumes the
    // SHORTEST turn. That is deliberately the optimistic case: if even
    // the short way breaks the limit, the long way certainly does, so
    // a warning raised on it is never a false alarm.
    //
    // The unwind is left to the user rather than driven automatically.
    // Shedding a turn means rotating the mount up to 360 degrees, which
    // is a long motion that can itself snag something; doing that
    // unattended on the strength of a dead-reckoned count is a worse
    // risk than the wrap. The dialog says which way to jog instead, and
    // the counter visibly falls as it unwinds.
    gotoPanel.SetPreSlewCheck([&](double raH, double decDeg) -> bool {
        double alt = 0.0, az = 0.0;
        raduk::core::RADecToAltAzNow(raH, decDeg, alt, az);
        // Phase 67 step 6: an obstruction sector, if one is configured.
        // Checked first because pointing into a wall is a harder stop
        // than wrapping a cable, and the two warnings should not stack
        // into a pair of dialogs.
        const auto& rigc = raduk::core::RigConfig::Get();
        if (rigc.KeepOutSet() &&
            raduk::core::BearingInArc(az, rigc.keepOutStartDeg,
                                      rigc.keepOutEndDeg)) {
            wchar_t kmsg[480];
            swprintf(kmsg, 480,
                L"That target is inside your keep-out sector.\n\n"
                L"  Target azimuth:  %.0f\u00b0\n"
                L"  Keep-out sector: %.0f\u00b0 to %.0f\u00b0 (clockwise)\n\n"
                L"This is the arc you marked as blocked - a wall, a tree, "
                L"the house. Slew anyway?",
                az, rigc.keepOutStartDeg, rigc.keepOutEndDeg);
            Log("[KEEPOUT] Target az %.0f is inside the sector %.0f-%.0f\n",
                az, rigc.keepOutStartDeg, rigc.keepOutEndDeg);
            if (MessageBoxW(window.Handle(), kmsg, L"Keep-out sector",
                            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
                Log("[KEEPOUT] Slew cancelled\n");
                slog.Mount("Slew cancelled - target inside keep-out sector");
                return false;
            }
            slog.Mount("Keep-out warning overridden (az %.0f)", az);
            // Accepted: fall through so cord wrap still gets its say.
        }

        auto f = cordWrap.ForecastSlewTo(az);
        if (!f.usable || !f.wouldExceed) return true;   // nothing to say

        const wchar_t* jog = (f.predictedDeg > 0) ? L"west (anticlockwise)"
                                                  : L"east (clockwise)";
        wchar_t msg[640];
        swprintf(msg, 640,
            L"This slew would wrap the cables further than your limit.\n\n"
            L"  Travelled since CABLES OK:  %+.0f°\n"
            L"  This slew adds:             %+.0f°\n"
            L"  Would leave you at:         %+.0f°   (limit %.0f°)\n\n"
            L"To unwind, jog the mount %s until the wrap figure beside Az "
            L"drops, then slew again. Or press CABLES OK if you have just "
            L"freed them.\n\nSlew anyway?",
            cordWrap.CumulativeDeg(), f.turnDeg, f.predictedDeg,
            cordWrap.Limit(), jog);

        Log("[CORDWRAP] Slew would reach %+.0f deg (limit %.0f) - asking\n",
            f.predictedDeg, cordWrap.Limit());
        int r = MessageBoxW(window.Handle(), msg, L"Cord wrap warning",
                            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
        if (r == IDYES) {
            Log("[CORDWRAP] User chose to slew anyway\n");
            slog.Mount("Cord wrap warning overridden (%+.0f deg)", f.predictedDeg);
            return true;
        }
        Log("[CORDWRAP] Slew cancelled\n");
        slog.Mount("Slew cancelled on cord-wrap warning");
        return false;
    });

    controls.SetRigSetupCallback([&]() {
        // The live azimuth is what makes the drawing checkable: a
        // bearing typed into a file is unfalsifiable, but one drawn
        // beside where the scope is actually pointing is not.
        raduk::gui::ShowRigSetupDialog(
            window.Handle(), controls.PanelHwnd(),
            [&]() -> double {
                return cordWrap.LastAz();   // -1 when not yet known
            });
        // The dialog can change the filter band, and the focuser panel
        // states it. Without this the label keeps naming the old filter
        // while the ADC figure beside it already uses the new band.
        controls.RefreshFilterLabel();
    });

    controls.SetCablesDressedCallback([&]() {
        double was = cordWrap.CumulativeDeg();
        cordWrap.CablesDressed();
        Log("[CORDWRAP] Cables dressed - reset from %+.0f deg\n", was);
        slog.Mount("Cord wrap reset (was %+.0f deg)", was);
        notifBar.NotifyInfo(L"Cord wrap reset - counting from here");
    });

    // Re-register align callback now that notifBar exists — adds notification flash
    mountCtrl.SetAlignCallback([&](double ra_h, double dec_deg,
                                    const std::string& name) {
        // Phase 62: a one-point align, not merely a sync. The sync gives
        // the mount its orientation reference; tracking then has to be
        // switched on explicitly, which this never did -- so ALIGN could
        // not have started the drive however well the target was centred.
        // Both results are reported, because the old code discarded
        // SyncRADec's return and claimed "Aligned" unconditionally.
        Log("[ALIGN] One-point align on %s (RA=%.4fh Dec=%.4f)\n",
            name.c_str(), ra_h, dec_deg);
        bool synced = false, tracking = false;
        if (mount && mount->IsConnected()) {
            synced = mount->SyncRADec(ra_h, dec_deg);
            if (synced) tracking = mount->SetTrackingMode(IMount::ALT_AZ);
            Log("[ALIGN] sync=%s tracking=%s\n",
                synced ? "ok" : "REFUSED", tracking ? "ALT-AZ" : "REFUSED");
            slog.Mount("Align: %s sync=%s tracking=%s", name.c_str(),
                       synced ? "ok" : "refused",
                       tracking ? "alt-az" : "refused");
        }
        for (auto* m : kMaps) m->SetMountPosition(ra_h, dec_deg, true);
        std::wstring wname(name.begin(), name.end());
        if (synced && tracking)
            notifBar.NotifyInfo(L"★ Aligned on " + wname + L" - tracking ALT-AZ");
        else if (synced)
            notifBar.NotifyWarn(L"Synced on " + wname + L" - mount refused tracking");
        else
            notifBar.NotifyError(L"Mount refused the sync - not aligned");
    });

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
                     mount && mount->IsConnected() ? mount : nullptr, slog, profileHooks);
        controls.SetActiveProfile(index);
        CaptureConfig cfg = capturer.Config();
        cfg.filePrefix = p->filePrefix.empty() ? p->name : p->filePrefix;
        capturer.SetConfig(cfg);
        // The NOTES panel is filled by profileHooks.syncUi, with the
        // rest of the panel -- so the GoTo auto-load and APEX
        // load_profile fill it too, and there is one place that knows
        // which profile the box is showing.
    });

    controls.SetProfileSaveCallback([&](int index) {
        TargetProfile* p = profiles.ProfileAt(index);
        if (!p) return;
        // Capture the LIVE rig state, not just the notes box. Before
        // Phase 61 this wrote the stored profile straight back out, so
        // SAVE silently preserved the old numbers and a night of
        // tuning was lost on the next LOAD -- the settings that
        // actually worked never reached disk.
        if (cam.IsOpen()) {
            p->gain         = (int)cam.GetControlValue(ControlId::Gain);
            // SDK exposure is microseconds; the profile stores ms.
            p->exposureMs   = (double)cam.GetControlValue(ControlId::ExposureUs) / 1000.0;
            p->gamma        = (int)cam.GetControlValue(ControlId::Gamma);
            p->wbRed        = (int)cam.GetControlValue(ControlId::WhiteBalanceRed);
            p->wbBlue       = (int)cam.GetControlValue(ControlId::WhiteBalanceBlue);
            p->usbBandwidth = (int)cam.GetControlValue(ControlId::UsbBandwidth);
            p->highSpeed    = cam.GetControlValue(ControlId::HighSpeedMode) != 0;

            int aW = 0, aH = 0, aB = 1; ImageType aT = ImageType::Raw8;
            if (cam.GetRegion(aW, aH, aB, aT) && aW > 0 && aH > 0) {
                p->roiWidth  = aW;
                p->roiHeight = aH;
                p->binning   = aB;
                p->imgType   = asimap::ToAsi(aT);  // TargetProfile persists the vendor enum
            }
        }
        // Focus is the setting most worth preserving: ApplyProfile drives
        // the focuser to p->focusPosition on every LOAD, so a stale value
        // actively defocuses the rig.
        if (focuser.IsOpen()) p->focusPosition = focuser.GetPosition();

        // Phase 85: the capture fields too. LOAD now pushes these into
        // the panel, so SAVE has to read them back or the round trip is
        // one-way: a limit or prefix typed into CAPTURE would be lost on
        // the next load, and the profile would quietly reimpose the old
        // one. These three are the whole overlap between the two
        // sections that were merged.
        p->filePrefix = controls.GetFilePrefix();
        if (controls.GetLimitByFrames()) {
            p->limitType  = "frames";
            p->limitValue = (int)controls.GetLimitFrames();
        } else {
            p->limitType  = "time";
            p->limitValue = (int)controls.GetLimitTimeSec();
        }
        // Notes are the one field SAVE cannot simply read off the
        // rig. The box holds a note for ONE profile, and it is empty
        // until something fills it, so writing it unconditionally
        // emptied whichever profile the combo named. That is how
        // saturn.ini lost its note during a Jupiter session.
        const std::string boxNotes = controls.GetNotes();
        const bool takeBoxNotes =
            NotesBoxMayReplaceStored(boxNotes, controls.NotesProfile(),
                                     p->name, p->notes);
        if (takeBoxNotes) {
            p->notes = boxNotes;
            // The box now speaks for this profile, so a later SAVE can
            // clear the note deliberately.
            controls.SetNotes(boxNotes, p->name);
        }
        profiles.Save(*p);
        Log("[PROFILE] Saved %s: gain=%d exp=%.2fms gamma=%d wb=%d/%d "
            "usb=%d focus=%d roi=%dx%d limit=%s/%d prefix=%s\n",
            p->name.c_str(), p->gain, p->exposureMs, p->gamma,
            p->wbRed, p->wbBlue, p->usbBandwidth, p->focusPosition,
            p->roiWidth, p->roiHeight, p->limitType.c_str(),
            p->limitValue, p->filePrefix.c_str());
        slog.Profile("Saved: %s (gain=%d exp=%.2fms focus=%d)",
                     p->name.c_str(), p->gain, p->exposureMs, p->focusPosition);
        // Silence here is what made the old loss invisible. The bar,
        // not a dialog: SAVE must not stop to be acknowledged, and
        // nothing has gone wrong -- a note was simply left alone.
        if (!takeBoxNotes) {
            wchar_t wName[64] = {};
            MultiByteToWideChar(CP_UTF8, 0, p->name.c_str(), -1, wName, 64);
            notifBar.NotifyWarn(std::wstring(L"Saved ") + wName +
                                L" - kept its stored note (NOTES box is empty)");
            Log("[PROFILE] %s: empty NOTES box belongs to '%s' - "
                "stored note kept\n",
                p->name.c_str(), controls.NotesProfile().c_str());
            slog.Profile("Saved: %s (stored note kept)", p->name.c_str());
        }
    });

    // ── Profile editor modal (Phase 9/10 — EDIT button) ───────────
    // Builds a WS_POPUP dialog window entirely in code (no .rc resource).
    // Shows all TargetProfile fields as labeled edit boxes.
    // SAVE & CLOSE writes back to the profile and persists to disk.
    controls.SetProfileEditCallback([&](int index) {
        if (index < 0 || index >= (int)profiles.Profiles().size()) return;

        // We need a mutable copy to edit
        TargetProfile prof = profiles.Profiles()[index];
        // The name before the dialog can rename it -- the NOTES box is
        // keyed by name, so this is what tells us whether the box is
        // showing the note being edited here.
        const std::string editedName = prof.name;

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
        const std::wstring wName = raduk::core::ToWide(prof.name);
        rows[ri++] = {L"Profile Name:", MkEdit(wName.c_str(), PX+LW, y, EW, EH, ED_NAME)};
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
        const std::wstring wPfx = raduk::core::ToWide(prof.filePrefix);
        rows[ri++] = {L"File Prefix:", MkEdit(wPfx.c_str(), PX+LW, y, EW, EH, ED_PREFIX)};
        y += ROWH;

        // Notes
        MkLabel(L"Notes:", PX, y+3, LW, EH);
        const std::wstring wNotes = raduk::core::ToWide(prof.notes);
        rows[ri++] = {L"Notes:", MkEdit(wNotes.c_str(), PX+LW, y, EW, EH, ED_NOTES)};
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
            // Ask each control how long its text is rather than
            // guessing: this read-back ran through wchar_t[256] and
            // char[256], so SAVE & CLOSE quietly shortened any note
            // past ~255 characters -- and planet_focus.ini ships a
            // 323-character one, so opening it here and saving was
            // enough to lose the tail. Same sizing as the NOTES panel
            // in ControlPanel; both go through core/TextConv.h.
            auto GetEditA = [&](int id) -> std::string {
                return raduk::core::GetWindowTextUtf8(GetDlgItem(hDlg, id));
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

            // If the NOTES box is showing this profile's note, it is
            // now the stale one: SAVE would write it straight back
            // over what was just typed here, which is the same loss
            // by another route.
            if (controls.NotesProfile() == editedName)
                controls.SetNotes(prof.notes, prof.name);

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

    // Phase 47b: every solve, and every slew that invalidates one,
    // on its way to the thing that draws the cursor. An unsolved
    // result is the invalidation.
    gotoPanel.SetWcsCallback([&](const raduk::sky::SolveResult& r,
                                 int fw, int fh) {
        if (!r.solved) {
            preview.ClearPlateSolution();
            Log("[WCS] cleared\n");
            return;
        }
        const raduk::sky::PlateSolution ps =
            raduk::sky::PlateSolutionFromSolve(r, fw, fh);
        preview.SetPlateSolution(ps);
        if (ps.valid) {
            Log("[WCS] %dx%d  %.3f\"/px  %s  ref [%.1f,%.1f]\n",
                fw, fh, raduk::sky::ArcsecPerPixel(ps),
                raduk::sky::IsMirrored(ps) ? "mirrored" : "normal",
                ps.refX, ps.refY);
        } else {
            Log("[WCS] solve carried no usable geometry\n");
        }
    });

    // Now that there is something to draw on, the jog path can really
    // clear the solution; see the shared_ptr above.
    *invalidateWcs = [&preview]() { preview.ClearPlateSolution(); };
    preview.SetSensorROI(roiWidth, roiHeight);

    // Phase 89: hand the finished RGBA to the share view as well. An
    // empty std::function until the window is up, and the view itself
    // drops most of the frames it is offered.
    preview.SetPresentSink(
        [&](const uint8_t* rgba, int fw, int fh) {
            shareView.Present(rgba, fw, fh);
        });

    // ── Wire handlers ──────────────────────────────────────────
    window.SetMoveHandler([&]() {
        controls.OnMainWindowMoved();
        toolbar.SnapToMainWindow();
        notifBar.SnapToMainWindow();
        window.SetNotifBarHeight(notifBar.Height());
    });

    // ── Phase 47: click to recentre ───────────────────────
    //
    // Centring a planet by jogging is overshoot in the dark: a jog
    // moves the MOUNT, and which way the image then goes depends on
    // where the camera sits in the focuser. The phase 51 calibration
    // already measured that, so a click can be exact -- and the same
    // solve the guide loop uses turns the offset into two pulses.
    //
    // Deliberately per-click with Ctrl held rather than a mode. A mode
    // that moves the telescope is a mode somebody forgets they are in,
    // and the first sign of it is the target leaving the frame.
    //
    // Everything that could make the move wrong refuses instead. The
    // notification says which, because "nothing happened" is the one
    // response that teaches the user nothing.
    // The guards a recentre needs before it may pulse, and the pulse
    // sender itself. Shared with phase 48's framing restore, so the two
    // can never disagree about when moving is safe or how it is done.
    // 'who' starts every refusal, so the user knows which one refused.
    auto recentreReady = [&](const wchar_t* who,
                             raduk::core::GuideCalibration& cal,
                             raduk::core::GuideResponse& resp) -> bool {
        if (!mount || !mount->IsConnected()) {
            notifBar.NotifyWarn(std::wstring(who) + L": no mount connected");
            return false;
        }
        if (!tracker.HasCalibration()) {
            notifBar.NotifyWarn(std::wstring(who) + L" needs a guide "
                                L"calibration -- run it once first");
            return false;
        }
        // While the loop is actively correcting, a manual move is two
        // hands on the same wheel: it would pull the target off centre
        // and the loop would pull it back.
        const auto stage = tracker.Stage();
        if (stage == raduk::core::AcquisitionStage::Locked ||
            stage == raduk::core::AcquisitionStage::Centering) {
            notifBar.NotifyWarn(std::wstring(who) + L": the tracker is "
                                L"centring already -- stop it first");
            return false;
        }

        cal  = tracker.Calibration();
        resp = raduk::core::GuideResponse{ cal.exPerMs, cal.eyPerMs,
                                           cal.nxPerMs, cal.nyPerMs };
        double altNow = -1.0, azNow = 0.0;
        if (mount->GetAltAz(altNow, azNow))
            resp = raduk::core::RescaleForAltitude(resp, cal.altDeg,
                                                   (float)altNow);
        return true;
    };

    // NexStar's PulseGuide blocks for the duration, so the pulses go on
    // their own thread and the UI is not held for several seconds --
    // which means the caller must have had its say already.
    auto sendRecentrePulses = [&](int eMs, int nMs) {
        suspendTrackerForJog(true);
        IMount* mnt = mount;
        std::thread([mnt, eMs, nMs, suspendTrackerForJog]() {
            using raduk::core::GuideDirection;
            if (eMs != 0) {
                mnt->PulseGuide(eMs > 0 ? GuideDirection::East
                                        : GuideDirection::West,
                                eMs > 0 ? eMs : -eMs);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (nMs != 0) {
                mnt->PulseGuide(nMs > 0 ? GuideDirection::North
                                        : GuideDirection::South,
                                nMs > 0 ? nMs : -nMs);
            }
            Log("[RECENTRE] pulses done\n");
            suspendTrackerForJog(false);
        }).detach();
    };

    auto doRecentreClick = [&](float sxD2D, float syD2D) {
        int fx = 0, fy = 0;
        if (!preview.ScreenToFramePixel(sxD2D, syD2D, fx, fy)) return;

        raduk::core::GuideCalibration cal;
        raduk::core::GuideResponse resp;
        if (!recentreReady(L"Recentre", cal, resp)) return;

        // 10 s is about 75" at the configured guide rate, which is far
        // more than a planet ever sits from centre. Past that the right
        // tool is a GoTo, and PlanRecentre says so rather than sending
        // a clamped pulse to somewhere nobody asked for.
        const raduk::core::RecentrePlan plan = raduk::core::PlanRecentre(
            (float)fx, (float)fy,
            preview.FrameWidthPx(), preview.FrameHeightPx(),
            resp, cal.arcsecPerPx, 10000);

        if (!plan.valid) {
            wchar_t w[160] = {};
            MultiByteToWideChar(CP_UTF8, 0, plan.why, -1, w, 160);
            notifBar.NotifyWarn(std::wstring(L"Recentre: ") + w);
            Log("[RECENTRE] refused at [%d,%d]: %s (%.0f px, %.1f\" off centre)\n",
                fx, fy, plan.why, plan.offsetPx, plan.offsetArcsec);
            return;
        }

        Log("[RECENTRE] [%d,%d] -> centre: %.0f px (%.1f\") "
            "E/W %dms N/S %dms\n",
            fx, fy, plan.offsetPx, plan.offsetArcsec,
            plan.eastMs, plan.northMs);
        slog.Mount("Recentre from [%d,%d]: %.1f\" (E/W %dms N/S %dms)",
                   fx, fy, plan.offsetArcsec, plan.eastMs, plan.northMs);

        // Said before the move, not after.
        {
            wchar_t w[160] = {};
            swprintf(w, 160, L"Recentring %.0f px (%.1f\")  E/W %dms  N/S %dms",
                     plan.offsetPx, plan.offsetArcsec,
                     plan.eastMs, plan.northMs);
            notifBar.NotifyInfo(w);
        }
        sendRecentrePulses(plan.eastMs, plan.northMs);
    };

    // ── Phase 48: framing recall ("go to previous image") ─────
    //
    // Shift+G: pick a past capture, and the telescope goes back to it.
    // Its observation record says what it was pointed at; the slew is
    // the GoTo panel's own DoGoTo, so the keep-out sector, the cord-
    // wrap forecast and the profile auto-load all apply exactly as for
    // any other GoTo. Nothing here commands the mount directly.
    //
    // PlanRecall adds what the GoTo buttons check elsewhere -- aligned,
    // above the Tonight list's 15 degree floor -- and one thing no GoTo
    // checks yet: distance from the Sun. Then a confirmation, defaulting
    // to No, because a slew picked from a file is a slew the person at
    // the eyepiece did not see coming.
    //
    // Ctrl+Shift+G, afterwards: put the target back where it sat in the
    // recorded frame, through the click-to-recentre path above. Only
    // needed when that framing was off-centre on purpose; acquisition
    // after the GoTo centres it otherwise.
    raduk::core::ObservationRecord recallRec;
    bool recallLoaded = false;

    auto toWide = [](const std::string& s) {
        std::wstring w;
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        if (n > 1) { w.resize(n - 1); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n); }
        return w;
    };

    auto doRecallFraming = [&]() {
        wchar_t fileBuf[MAX_PATH] = {};
        const std::wstring initDir = toWide(controls.GetOutputDir());
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner   = window.Handle();
        ofn.lpstrFilter = L"Captures (*_observation.txt, *.ser, *.fits)\0*_observation.txt;*.ser;*.fits\0"
                          L"All files\0*.*\0";
        ofn.lpstrFile   = fileBuf;
        ofn.nMaxFile    = MAX_PATH;
        ofn.lpstrInitialDir = initDir.empty() ? nullptr : initDir.c_str();
        ofn.lpstrTitle  = L"Recall framing - pick a past capture";
        ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&ofn)) return;

        std::string picked;
        {
            const int n = WideCharToMultiByte(CP_UTF8, 0, fileBuf, -1, nullptr, 0, nullptr, nullptr);
            if (n > 1) { picked.resize(n - 1); WideCharToMultiByte(CP_UTF8, 0, fileBuf, -1, &picked[0], n, nullptr, nullptr); }
        }
        const std::string obsPath = raduk::core::ObservationPathFor(picked);

        std::string text;
        if (FILE* f = _wfopen(toWide(obsPath).c_str(), L"rb")) {
            char buf[4096];
            size_t got;
            while ((got = fread(buf, 1, sizeof(buf), f)) > 0 && text.size() < (1u << 20))
                text.append(buf, got);
            fclose(f);
        } else {
            notifBar.NotifyWarn(L"Recall: no observation record beside that capture "
                                L"-- it was recorded before phase 48");
            Log("[RECALL] no record at %s\n", obsPath.c_str());
            return;
        }

        raduk::core::ObservationRecord rec;
        std::string why;
        if (!raduk::core::ParseObservation(text, rec, &why)) {
            notifBar.NotifyWarn(L"Recall: " + toWide(why));
            Log("[RECALL] %s: %s\n", obsPath.c_str(), why.c_str());
            return;
        }

        raduk::core::RecallInputs in;
        in.mountConnected = mount && mount->IsConnected();
        in.mountAligned   = in.mountConnected &&
                            (mount->IsAligned() || gotoPanel.IsPlateSynced());
        double tRa = 0, tDec = 0;
        if (gotoPanel.LookupNow(rec.target, tRa, tDec)) {
            in.targetResolved = true;
            in.resolvedRaH = tRa; in.resolvedDecDeg = tDec;
        }
        if (gotoPanel.LookupNow("Sun", tRa, tDec)) {
            in.sunKnown = true;
            in.sunRaH = tRa; in.sunDecDeg = tDec;
        }
        in.altitudeNow = [](double ra, double dec, double& alt) {
            if (!raduk::core::RigConfig::Get().SiteIsSet()) return false;
            double az = 0;
            raduk::core::RADecToAltAzNow(ra, dec, alt, az);
            return true;
        };

        const raduk::core::RecallPlan plan = raduk::core::PlanRecall(rec, in);
        if (!plan.valid) {
            notifBar.NotifyWarn(L"Recall refused: " + toWide(plan.why));
            Log("[RECALL] refused %s: %s\n", rec.file.c_str(), plan.why.c_str());
            slog.Mount("Recall refused (%s): %s", rec.file.c_str(), plan.why.c_str());
            return;
        }

        const std::string when = raduk::core::FormatUtcIso(rec.startUnix);
        const std::wstring wFile = toWide(rec.file), wWhen = toWide(when);
        const std::wstring wTarget = toWide(rec.target), wProfile = toWide(rec.profile);
        wchar_t msg[900];
        swprintf(msg, 900,
            L"Slew the telescope back to this capture?\n\n"
            L"  Capture:   %s\n"
            L"  Recorded:  %s\n"
            L"  Target:    %s\n"
            L"  Profile:   %s\n\n"
            L"  Slew to:   RA %.4fh  Dec %+.3f°  (%s)\n"
            L"  Altitude now: %.0f°\n\n"
            L"The slew goes through the normal GoTo, so the keep-out and "
            L"cable-wrap checks still apply.",
            wFile.c_str(),
            wWhen.empty()    ? L"(time not recorded)" : wWhen.c_str(),
            wTarget.empty()  ? L"(none recorded)"     : wTarget.c_str(),
            wProfile.empty() ? L"(none recorded)"     : wProfile.c_str(),
            plan.raHours, plan.decDeg,
            plan.fromCatalogue ? L"where it is tonight" : L"the recorded mount position",
            plan.altNowDeg);
        if (MessageBoxW(window.Handle(), msg, L"Recall framing",
                        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
            Log("[RECALL] declined: %s\n", rec.file.c_str());
            return;
        }

        Log("[RECALL] %s -> %s RA %.4fh Dec %+.3f (%s), alt %.1f\n",
            rec.file.c_str(), plan.name.c_str(), plan.raHours, plan.decDeg,
            plan.fromCatalogue ? "catalogue now" : "recorded position",
            plan.altNowDeg);
        slog.Mount("Recall %s: %s", rec.file.c_str(), plan.name.c_str());

        recallRec    = rec;
        recallLoaded = true;
        gotoPanel.RequestGoTo(plan.raHours, plan.decDeg, plan.name);
    };

    auto doRecallOffset = [&]() {
        if (!recallLoaded) {
            notifBar.NotifyWarn(L"Framing restore: recall a capture first (Shift+G)");
            return;
        }
        if (mount && mount->IsConnected() && mount->IsSlewing()) {
            notifBar.NotifyWarn(L"Framing restore: the mount is still slewing");
            return;
        }
        if (!tracker.LastCentroidValid()) {
            notifBar.NotifyWarn(L"Framing restore: no target in the frame to move "
                                L"-- start acquisition so it is found");
            return;
        }
        raduk::core::GuideCalibration cal;
        raduk::core::GuideResponse resp;
        if (!recentreReady(L"Framing restore", cal, resp)) return;

        const float nx = tracker.LastCentroidX(), ny = tracker.LastCentroidY();
        const raduk::core::RecentrePlan plan = raduk::core::PlanFramingRestore(
            recallRec, nx, ny,
            preview.FrameWidthPx(), preview.FrameHeightPx(),
            resp, cal.arcsecPerPx, 10000);
        if (!plan.valid) {
            wchar_t w[160] = {};
            MultiByteToWideChar(CP_UTF8, 0, plan.why, -1, w, 160);
            notifBar.NotifyWarn(std::wstring(L"Framing restore: ") + w);
            Log("[RECALL] offset refused: %s (now %.0f,%.0f, recorded %.0f,%.0f)\n",
                plan.why, nx, ny, recallRec.targetXPx, recallRec.targetYPx);
            return;
        }
        Log("[RECALL] offset (%.0f,%.0f) -> (%.0f,%.0f): E/W %dms N/S %dms\n",
            nx, ny, recallRec.targetXPx, recallRec.targetYPx,
            plan.eastMs, plan.northMs);
        slog.Mount("Recall framing restore: E/W %dms N/S %dms",
                   plan.eastMs, plan.northMs);
        {
            wchar_t w[160] = {};
            swprintf(w, 160, L"Restoring recorded framing %.0f px  E/W %dms  N/S %dms",
                     plan.offsetPx, plan.eastMs, plan.northMs);
            notifBar.NotifyInfo(w);
        }
        sendRecentrePulses(plan.eastMs, plan.northMs);
    };

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

    // Phase 24: pixel click lock/unlock.
    // Phase 47: Ctrl+click puts the clicked pixel in the middle.
    window.SetPixelClickHandler([&](float x, float y) {
        const float dpi = window.DpiScale();
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            doRecentreClick(x / dpi, y / dpi);
            return;   // a recentre is not also a pixel lock
        }
        if (preview.PixelReadoutEnabled())
            preview.ClickPixelLock(x / dpi, y / dpi);
    });

    // Mouse up — completes ROI selection
    window.SetMouseUpHandler([&](float x, float y) {
        preview.HandleMouseUp(x, y);
    });

    // Phase 88: store the live rig so the next launch comes back to it.
    // Reads the camera rather than any cached copy, so whatever the user
    // actually last dialled in is what gets kept -- including changes
    // made with the sliders, which write straight to the camera and are
    // not mirrored anywhere else.
    auto saveSession = [&](const char* why) {
        auto& rg = raduk::core::RigConfig::Get();
        rg.lastRoiWidth  = roiWidth;
        rg.lastRoiHeight = roiHeight;
        rg.lastBitDepth  = roiBits;
        if (cam.IsOpen()) {
            rg.lastGain       = (int)cam.GetControlValue(ControlId::Gain);
            rg.lastExposureMs = (float)(cam.GetControlValue(ControlId::ExposureUs) / 1000.0);
            rg.lastGamma      = (int)cam.GetControlValue(ControlId::Gamma);
            rg.lastWbRed      = (int)cam.GetControlValue(ControlId::WhiteBalanceRed);
            rg.lastWbBlue     = (int)cam.GetControlValue(ControlId::WhiteBalanceBlue);
            rg.lastUsb        = (int)cam.GetControlValue(ControlId::UsbBandwidth);
        }
        rg.Save();
        Log("[SESSION] Saved (%s): %dx%d %d-bit gain=%d exp=%.2fms gamma=%d wb=%d/%d usb=%d\n",
            why, rg.lastRoiWidth, rg.lastRoiHeight, rg.lastBitDepth,
            rg.lastGain, rg.lastExposureMs, rg.lastGamma,
            rg.lastWbRed, rg.lastWbBlue, rg.lastUsb);
    };

    // ── Phase 84: the one place the capture format changes ────
    // Three paths reconfigured the camera -- startup, a dragged box and
    // the [ROI] crop -- each carrying its own copy of the
    // stop/apply/read-back/reallocate/restart sequence, and each
    // hardcoding RAW8. Nothing could widen an ROI again, so a crop was
    // permanent until restart. This is the sequence they now share and
    // the only place bit depth is decided.
    auto applyFormat = [&](int reqW, int reqH, int bits,
                           int wantCx, int wantCy, const char* why) -> bool {
        if (!cam.IsOpen()) return false;
        roiFollow.Disarm();   // the render loop re-arms on the new format

        // ASI alignment: width %8, height %2. Enforced here rather than
        // at each call site, so a preset and a dragged box cannot
        // disagree about what is legal.
        int w = std::max(8, std::min(reqW, cam.MaxWidth()));
        int h = std::max(2, std::min(reqH, cam.MaxHeight()));
        w = (w / 8) * 8;
        h = (h / 2) * 2;
        const ImageType type = (bits == 16) ? ImageType::Raw16 : ImageType::Raw8;

        // The origin is NOT computed here. The camera may grant a
        // smaller mode than was asked for, and an origin clamped against
        // the requested size then leaves the ROI somewhere it was never
        // meant to be -- asking for the full sensor and being given less
        // used to pin it to the top-left corner instead of the centre.
        // So callers say where they want the ROI CENTRED, and the origin
        // is derived below from the size actually granted.

        const bool wasRecording = capturer.IsRecording();
        if (wasRecording) {
            // A format change mid-file would leave the rest of the SER
            // unreadable, so the file is closed. Not a normal finish,
            // so it must not auto-stack.
            capturer.StopRecording(false);
            usbMgr.SetCapturing(false);
        }

        capture.Stop();                      // required before SetROI
        cam.SetRegion(w, h, 1, type);

        // Believe the camera, not the request. Both the real SDK and the
        // emulator clamp, and trusting the request is how the ring buffer
        // ends up sized for a frame that never arrives.
        {
            int aW = w, aH = h, aB = 1;
            ImageType aT = type;
            cam.GetRegion(aW, aH, aB, aT);
            w = aW; h = aH;
            bits = (aT == ImageType::Raw16) ? 16 : 8;
        }

        // Now that the real size is known, place it. Centre-in, origin-out:
        // the ROI ends up where it was asked to be whatever the camera
        // decided to grant.
        int absX = wantCx - w / 2;
        int absY = wantCy - h / 2;
        absX = std::max(0, std::min(absX, cam.MaxWidth()  - w));
        absY = std::max(0, std::min(absY, cam.MaxHeight() - h));
        absX = (absX / 8) * 8;
        absY = (absY / 2) * 2;
        cam.SetStartPos(absX, absY);

        roiWidth  = w; roiHeight = h; roiBits = bits;
        roiStartX = absX; roiStartY = absY;

        const int bytes = FrameSizeBytes(
            w, h, (bits == 16) ? ImageType::Raw16 : ImageType::Raw8);
        ring.Allocate(DEFAULT_RING_SIZE, bytes);
        capture.Start(cam, ring, 8.0);

        preview.SetFrameSize(w, h);
        preview.SetSensorROI(w, h);
        statusBar.SetCameraStats(0.0, 0, cam.GetTemperatureC(), w, h, bits);
        toolbar.UpdateFormat(w, h, bits);

        Log("[FORMAT] %s -> %dx%d @ (%d,%d)  %d-bit  %d bytes/frame\n",
            why ? why : "change", w, h, absX, absY, bits, bytes);
        slog.System("Format: %dx%d %d-bit (%s)", w, h, bits,
                    why ? why : "change");

        if (wasRecording && capturer.StartRecording(w, h, bits))
            usbMgr.SetCapturing(true);
        // Persisted straight away. A format change is deliberate, and
        // the app does get killed at the mount rather than closed.
        saveSession("format change");
        return true;
    };

    // startX/startY arrive ROI-RELATIVE (PreviewRenderer computes them
    // within the currently displayed frame) -- convert to sensor-absolute.
    preview.SetROIChangeCallback([&](int newW, int newH, int startX, int startY) {
        applyFormat(newW, newH, roiBits,
                    roiStartX + startX + newW / 2,
                    roiStartY + startY + newH / 2, "dragged box");
    });

    // ── Phase 85: fill in the profile hooks ───────────────────
    // Declared far above, next to the GoTo auto-load callback that
    // captured it; only here are applyFormat and the panel setters in
    // scope. Both hooks run on an explicit target selection only --
    // selecting an object on the map or in GoTo, pressing LOAD, or an
    // APEX load_profile. Startup never calls ApplyProfile, so the app
    // still comes up at full frame.
    profileHooks.setFormat = [&](int w, int h, int bits) {
        // Keep the ROI where it already is. A profile says how wide to
        // read the sensor, not where to point it -- the mount and the
        // tracker decide that, and recentring here would fight them.
        applyFormat(w, h, bits,
                    roiStartX + roiWidth / 2, roiStartY + roiHeight / 2,
                    "profile");
    };
    // Phase 95: the analyser only answers the star question when told.
    profileHooks.setStarMode = [&](bool on) { analyzer.SetStarMode(on); };

    profileHooks.syncUi = [&](const TargetProfile& p) {
        // The slider is in microseconds; the profile stores milliseconds.
        controls.SetCameraSliders(p.gain, (int)(p.exposureMs * 1000.0),
                                  p.gamma, p.wbRed, p.wbBlue, p.usbBandwidth);
        controls.SetFilePrefix(p.filePrefix.empty() ? p.name : p.filePrefix);
        // The note, and whose note it is. Every explicit profile load
        // comes through here, so the box and its owner can never
        // disagree about which profile is on screen.
        controls.SetNotes(p.notes, p.name);
        // limitValue means frames or seconds depending on limitType, and
        // the two boxes are separate, so only the matching one is set.
        const bool byFrames = (p.limitType != "time");
        controls.SetCaptureLimit(byFrames,
                                 byFrames ? (uint32_t)p.limitValue : 0,
                                 byFrames ? 0.0 : (double)p.limitValue);
        Log("[PROFILE] Panel synced: gain=%d exp=%.2fms gamma=%d wb=%d/%d "
            "usb=%d limit=%s/%d prefix=%s\n",
            p.gain, p.exposureMs, p.gamma, p.wbRed, p.wbBlue, p.usbBandwidth,
            p.limitType.c_str(), p.limitValue,
            (p.filePrefix.empty() ? p.name : p.filePrefix).c_str());
    };

    // ── Phase 46: connect / disconnect all hardware ──────────
    //
    // Until now the rig was all-or-nothing at startup: whatever was
    // switched on and plugged in when the app launched was what the
    // session had. A camera that dropped off USB, a mount switched on
    // late, a focuser in the wrong socket -- each meant closing the app
    // and starting again, in the dark, usually after something had
    // already been framed.
    //
    // One button, three devices. What a press does is
    // raduk::core::NextAction's decision, not this code's: everything
    // up means disconnect (end of session), anything less means pick up
    // what is missing -- because the way a rig arrives at "two of three"
    // is a device that was not ready yet, and tearing down the two that
    // work is never the wanted answer there.
    auto currentLinks = [&]() {
        raduk::core::DeviceLinks d;
        d.camera  = cam.IsOpen();
        d.focuser = focuser.IsOpen();
        d.mount   = mountProxy.IsConnected();
        return d;
    };

    auto reportLinks = [&](const raduk::core::DeviceLinks& before) {
        const raduk::core::DeviceLinks after = currentLinks();
        const std::string msg = raduk::core::LinkSummary(before, after);
        wchar_t w[256] = {};
        MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, w, 256);
        // Amber unless everything is up: a bar that says "connected"
        // in green while the mount is missing is how a night gets lost
        // to a telescope that was never going to track.
        if (raduk::core::LinkStateOf(after) == raduk::core::LinkState::All)
            notifBar.NotifyInfo(w);
        else
            notifBar.NotifyWarn(w);
        Log("[HW] %s\n", msg.c_str());
        slog.System("Hardware: %s", msg.c_str());
        toolbar.UpdateHardware(after);
    };

    auto connectMissing = [&]() {
        const raduk::core::DeviceLinks before = currentLinks();

        // Camera first, same order as startup, because the preview and
        // every analyser downstream hang off it.
        if (!cam.IsOpen()) {
            if (cam.Open(0)) {
                Log("[HW] Camera: %s\n", cam.Name().c_str());
                // The settings the session was using, not the camera's
                // defaults: a reconnect mid-session should look like
                // nothing happened.
                const auto& rgs = raduk::core::RigConfig::Get();
                if (rgs.HasLastCamera()) {
                    cam.SetGain(rgs.lastGain >= 0 ? rgs.lastGain : 100);
                    cam.SetExposure(rgs.lastExposureMs > 0.0f
                                        ? (double)rgs.lastExposureMs : 8.0);
                    cam.SetGamma(rgs.lastGamma >= 0 ? rgs.lastGamma : 50);
                    cam.SetWhiteBalance(rgs.lastWbRed  >= 0 ? rgs.lastWbRed  : 52,
                                        rgs.lastWbBlue >= 0 ? rgs.lastWbBlue : 47);
                    cam.SetUSBBandwidth(rgs.lastUsb >= 0 ? rgs.lastUsb : 80);
                    cam.SetHighSpeedMode(true);
                }
                // applyFormat owns the ROI, the ring buffer and the
                // capture thread together. Reusing it here rather than
                // repeating those three steps is the difference between
                // one restart path and two that drift apart.
                applyFormat(roiWidth, roiHeight, roiBits,
                            roiStartX + roiWidth / 2,
                            roiStartY + roiHeight / 2, "hardware connect");
            } else {
                Log("[HW] Camera: open failed\n");
            }
        }

        if (!focuser.IsOpen()) {
#ifdef RADUK_EMULATOR
            focuser.Open(0);
#else
            if (!useEmulator && EAFFocuser::GetCount() > 0) focuser.Open(0);
#endif
            if (focuser.IsOpen())
                Log("[HW] EAF: pos=%d\n", focuser.GetPosition());
            else
                Log("[HW] EAF: not found\n");
        }

        if (!mountProxy.IsConnected()) {
            // A fresh choice, not a reconnect of the old object: CPWI
            // may have started since launch, or the hand control may
            // have come up on a different port.
            mountProxy.SetBackend(connectMountBackend());
            announceMountSim();
            if (mountProxy.IsConnected()) {
                controls.SetRCControl(startInRCControl);
                Log("[HW] Mount: %s\n", mountProxy.ConnectionId().c_str());
            }
        }

        reportLinks(before);
    };

    auto disconnectAll = [&]() {
        const raduk::core::DeviceLinks before = currentLinks();

        // Order is the reverse of connecting, and the first two steps
        // are not optional: a recording left open mid-write is a
        // truncated SER, and the tracker would keep pulsing a mount
        // that is about to go away.
        if (capturer.IsRecording()) {
            capturer.StopRecording(false);   // not a normal finish: no auto-stack
            usbMgr.SetCapturing(false);
        }
        if (tracker.IsRunning()) tracker.Reset();
        autofocus.Abort();

        if (mountProxy.IsConnected()) {
            mountProxy.StopSlew(0);
            mountProxy.StopSlew(1);
            mountProxy.Disconnect();
        }
        // Forget the backend without destroying it -- the tracker
        // thread may still be inside a pulse on it. MountProxy keeps
        // retired backends alive for exactly that reason.
        mountProxy.ClearBackend();

        if (focuser.IsOpen()) focuser.Close();

        if (cam.IsOpen()) {
            capture.Stop();     // before the camera, always
            cam.Close();
        }

        reportLinks(before);
    };

    auto doHardwareButton = [&]() {
        const raduk::core::DeviceLinks links = currentLinks();
        if (raduk::core::NeedsConfirmation(links, capturer.IsRecording())) {
            if (MessageBoxW(window.Handle(),
                    L"A capture is recording.\n\n"
                    L"Disconnecting closes the file and stops the mount.\n"
                    L"Disconnect anyway?",
                    L"RadukCapture \u2014 Disconnect hardware",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                return;
        }
        if (raduk::core::NextAction(links) ==
                raduk::core::LinkAction::DisconnectAll)
            disconnectAll();
        else
            connectMissing();
    };

    // Reticle toggle — now lives on the frame bar. Keyboard R key still works.
    // preview.ToggleReticle() is the single source of truth for state.
    // ── Phase 39: framing shot ────────────────────────────────
    // A faint target is invisible at planetary settings, so checking
    // whether it is even in frame meant dialling gain and exposure up
    // by hand and remembering what they were. This does that and puts
    // them back.
    //
    // Restore is driven from the main thread via WM_USER+11 rather than
    // from the sleeping thread: the camera control writes belong on the
    // thread that owns the rest of them, and a detached thread writing
    // gain while the user drags the gain slider is a race.
    //
    // Deliberately not a one-frame grab. The spec said capture a single
    // frame and display it, but the preview is already live, so leaving
    // the boosted settings up for five seconds shows the same thing and
    // avoids a second capture path that could desync the ring buffer.
    struct FramingShot {
        std::atomic<bool> active{false};
        int    savedGain = 0;
        long   savedExpUs = 0;
    };
    static FramingShot framing;
    auto doFramingShot = [&]() {
        if (!cam.IsOpen()) return;
        if (framing.active.load()) {
            Log("[FRAME] Framing shot already running\n");
            return;
        }
        if (capturer.IsRecording()) {
            // Boosting gain mid-recording would put unusable frames in
            // the middle of the file, and the user would not find out
            // until stacking.
            notifBar.NotifyWarn(L"Framing shot not available while recording");
            Log("[FRAME] Refused - recording in progress\n");
            return;
        }
        framing.savedGain  = (int)cam.GetControlValue(ControlId::Gain);
        framing.savedExpUs = (long)cam.GetControlValue(ControlId::ExposureUs);
        framing.active.store(true);

        cam.SetGain(570);            // slider maximum; HCG well into deep gain
        cam.SetExposure(2000.0);     // ms - long enough for a faint disc
        notifBar.NotifyInfo(L"Framing shot - boosted for 5s, then restored");
        Log("[FRAME] Framing shot: gain %d->570, exp %.1fms->2000ms\n",
            framing.savedGain, framing.savedExpUs / 1000.0);
        slog.Camera("Framing shot (5s boost)");

        std::thread([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(5000));
            PostMessage(window.Handle(), WM_USER + 11, 0, 0);
        }).detach();
    };
    window.SetFramingShotHandler([&]() { doFramingShot(); });
    toolbar.SetFramingShotCallback([&]() { doFramingShot(); });

    // ── Phase 45: move, size and rotate the reticle ──────
    // Modifiers rather than new keys: the plain wheel and plain drag are
    // the preview zoom and the ROI rubber band, both used constantly.
    // Each change reports the new state, because an overlay nudged by
    // accident with no feedback is just a mystery.
    auto reticleState = [&]() {
        wchar_t b[128];
        if (!preview.ReticleVisible()) return;
        if (preview.ReticleRotatable())
            swprintf(b, 128, L"%ls  %.0f%%  %.0f°",
                     preview.ReticleLabel(),
                     preview.ReticleScale() * 100.0f,
                     preview.ReticleRotation());
        else
            swprintf(b, 128, L"%ls  %.0f%%", preview.ReticleLabel(),
                     preview.ReticleScale() * 100.0f);
        notifBar.NotifyInfo(b);
        // Logged too. The notification bar is transient and cannot be
        // read back, so without this the transform is unverifiable --
        // the same blind spot as the advisory printf and the
        // guide-rate warning.
        Log("[RETICLE] %ls  scale=%.2f rot=%.0f off=(%.3f,%.3f)\n",
            preview.ReticleLabel(), preview.ReticleScale(),
            preview.ReticleRotation(),
            preview.ReticleDX(), preview.ReticleDY());
    };
    window.SetReticleSizeHandler([&](int dir) {
        if (!preview.ReticleVisible()) return;
        preview.SetReticleScale(preview.ReticleScale() + dir * 0.03f);
        reticleState();
    });
    window.SetReticleRotateHandler([&](int dir) {
        if (!preview.ReticleVisible()) return;
        if (!preview.ReticleRotatable()) {
            // Said rather than silently ignored: a circle and three
            // concentric rings look identical rotated, so a user turning
            // the wheel would otherwise think rotation was broken.
            notifBar.NotifyInfo(L"This reticle shape has no orientation "
                                L"- try BOX or CROSS");
            return;
        }
        preview.RotateReticle(dir * 5.0f);
        reticleState();
    });
    window.SetReticleDragHandler([&](float dxPx, float dyPx) {
        if (!preview.ReticleVisible()) return;
        // Pixels to fractions of the preview. Using the preview size,
        // not the window size, or the reticle would lag the cursor by
        // the width of the control panel.
        const float pw = (float)window.PreviewWidth();
        const float ph = (float)window.PreviewHeight();
        if (pw < 1.0f || ph < 1.0f) return;
        preview.NudgeReticle(dxPx / pw, dyPx / ph);
    });
    window.SetReticleDragEndHandler([&]() { reticleState(); });

    auto doReticle = [&]() {
        // Phase 45: steps Off -> Crosshair -> Circle -> Rings -> Box -> Off.
        preview.ToggleReticle();
        // Phase 45: cycling back to off also clears any drag, size and
        // rotation. Without that a reticle nudged somewhere odd stays
        // odd for the rest of the session, with nothing obvious to press.
        if (!preview.ReticleVisible()) preview.ResetReticleTransform();
        toolbar.SetReticleShapeLabel(preview.ReticleLabel());
        Log("[RETICLE] %ls\n", preview.ReticleLabel());
        slog.System("Reticle: %s",
                    preview.ReticleVisible() ? "on" : "off");
    };
    auto doZoom     = [&]() { preview.CycleZoom(); controls.SetZoomLabel(preview.ZoomLabel()); };
    auto doZoomStep = [&](int dir) { preview.ZoomStep(dir); controls.SetZoomLabel(preview.ZoomLabel()); };
    auto doFullscreen = [&]() { window.ToggleFullscreen(); };

    // ── Phase 77: hand a finished recording to AutoStakkert ────
    // AS does multi-point alignment and drizzle well and is free;
    // nothing here would beat it. What was missing was the handoff.
    static raduk::core::AutoStakkertRunner stacker;
    {
        auto& rg = raduk::core::RigConfig::Get();
        if (!rg.autoStakkertPath.empty()) {
            // Review pass 2: this was an iterator-pair widening, which
            // promotes each BYTE to a wchar_t. Phase 83 fixed the same
            // mistake in the other direction, but that one only spoiled
            // a log line -- this is the path used to launch the program,
            // so a non-ASCII character anywhere in it produced a path
            // that cannot exist and a launch that silently never starts.
            // Worth noting given how long AutoStakkert has been refusing
            // to run: it is not the cause for an ASCII path, but it
            // would be an invisible one for any other.
            const std::string& p = rg.autoStakkertPath;
            int need = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1,
                                           nullptr, 0);
            if (need > 0) {
                std::wstring w((size_t)need - 1, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, &w[0], need);
                stacker.SetPath(w);
            }
        }
        stacker.SetLogger([](const std::string& s) {
            Log("[STACK] %s\n", s.c_str());
        });
        Log("[STACK] AutoStakkert %s\n",
            stacker.IsAvailable() ? "found" : "NOT found");
    }
    auto doStackFile = [&](const std::string& serPath) {
        if (serPath.empty()) {
            notifBar.NotifyInfo(L"No finished recording to stack yet");
            return;
        }
        if (!stacker.IsAvailable()) {
            notifBar.NotifyWarn(L"AutoStakkert not found - set "
                                L"AutoStakkertPath in rig.ini");
            return;
        }
        if (raduk::core::IsFitsCapture(serPath)) {
            const bool opened = stacker.OpenInteractive(toWide(serPath));
            Log("[STACK] FITS, %s in AutoStakkert's window: %s\n",
                opened ? "opened" : "COULD NOT open", serPath.c_str());
            if (opened) notifBar.NotifyInfo(L"FITS opened in AutoStakkert - "
                                            L"it stacks from its own window");
            else        notifBar.NotifyWarn(L"Could not start AutoStakkert");
            return;
        }
        if (stacker.IsRunning()) {
            notifBar.NotifyInfo(L"A stack is already running");
            return;
        }
        slog.System("Stacking %s", serPath.c_str());
        notifBar.NotifyInfo(L"Stacking in AutoStakkert...");
        // MultiByteToWideChar, not an iterator copy: a byte-for-byte widen
        // mangles any non-ASCII character in the path and the stack then
        // fails on a file that plainly exists.
        std::wstring serW;
        {
            int need = MultiByteToWideChar(CP_UTF8, 0, serPath.c_str(),
                                           -1, nullptr, 0);
            if (need > 0) {
                serW.resize(need - 1);
                MultiByteToWideChar(CP_UTF8, 0, serPath.c_str(), -1,
                                    &serW[0], need);
            }
        }
        stacker.StackAsync(serW,
            [&](raduk::core::StackResult r) {
                // Worker thread: log only, no UI calls from here.
                if (r.ok)
                    Log("[STACK] done: %d image(s) in %.0fs\n",
                        r.filesProduced, r.seconds);
                else
                    Log("[STACK] failed: %s\n", r.error.c_str());
            });
    };

    // ── Phase 98: one place a recording ends ───────────────────
    //
    // From the owner's report that the RECORD button stays depressed
    // after a frame-limited run. The button is fixed in ControlPanel
    // (nothing invalidated it), but investigating turned up the same
    // root cause doing a second kind of damage: the limit-triggered
    // stop was a bare `capturer.StopRecording()` and skipped the
    // bookkeeping the manual stop does. So after a frame-limited run
    // USB STAYED IN CAPTURE MODE, with the EAF and the mount still
    // throttled, until something else happened to stop a recording.
    //
    // Two call sites doing the same thing differently is how that
    // happens, so now there is one.
    auto endRecording = [&](bool normal, const char* why) {
        if (!capturer.IsRecording()) return;
        capturer.StopRecording(normal);
        usbMgr.SetCapturing(false);
        slog.System("USB polling restored — %s", usbMgr.StatusString());
        Log("[REC] stopped (%s); USB polling restored: %s\n",
            why ? why : "manual", usbMgr.StatusString());
    };

    // ── Record toggle — also notifies USB manager ──────────────
    auto doRecord = [&]() {
        if (capturer.IsRecording()) {
            endRecording(true, "operator");
        } else {
            CaptureConfig cfg = capturer.Config();
            cfg.filePrefix    = controls.GetFilePrefix();
            cfg.outputDir     = controls.GetOutputDir();
            cfg.frameType     = controls.GetFrameType();  // Phase 44
            cfg.format        = controls.GetCaptureFits()
                                    ? CaptureFormat::FITS : CaptureFormat::SER;
            cfg.limitFrames   = controls.GetLimitFrames();
            cfg.limitTimeSec  = controls.GetLimitTimeSec();
            cfg.limitType     = controls.GetLimitByFrames()
                                    ? LimitType::Frames : LimitType::Time;

            // Phase 108: how long this target has before rotation blurs it.
            const auto& rig = raduk::core::RigConfig::Get();
            derotWindowSec = 0.0;
            derotWarned    = false;
            derotBody.clear();
            if (cfg.frameType == "Light") {
                std::string tgt = gotoPanel.CurrentTarget();
                if (tgt.empty())
                    if (const auto* ap = profiles.Active()) tgt = ap->name;
                if (const auto* b = raduk::core::FindRotatingBody(tgt)) {
                    const auto sz = raduk::sky::PlanetApparentSizeAt(
                        b->name, raduk::sky::CurrentJD());
                    if (sz.valid) {
                        derotBody      = b->name;
                        derotWindowSec = raduk::core::DerotationWindowSec(
                            sz.discArcsec, b->periodSec,
                            raduk::core::RayleighArcsec(rig.apertureMm));
                    }
                }
            }
            cfg.derotationStopSec = rig.derotationStop ? derotWindowSec : 0.0;
            cfg.winJuposNames     = rig.winJuposNames;
            capturer.SetConfig(cfg);
            CreateDirectoryA(cfg.outputDir.c_str(), nullptr);

            // roiBits, not 8.
            //
            // This said 8 outright, so every recording sized its write
            // buffer for an 8-bit frame whatever the camera was actually
            // sending. In 16-bit each frame is twice the slot size, every
            // Push is rejected as too large, the writer thread consumes
            // nothing, and the capture produces a 0-byte SER while the UI
            // reports it recording normally.
            //
            // roiBits is the variable that tracks what the camera gave
            // us rather than what was asked for -- which is the whole
            // reason it exists, and the format-change path on line ~2147
            // has always passed it. Only this one, the RECORD button,
            // did not.
            if (capturer.StartRecording(roiWidth, roiHeight, roiBits)) {
                usbMgr.SetCapturing(true);
                slog.System("USB capture mode — %s", usbMgr.StatusString());
                Log("[USB] Throttling EAF+mount: %s\n", usbMgr.StatusString());
                if (derotWindowSec > 0.0) {
                    Log("[DEROT] %s: %.0f s window, %s\n", derotBody.c_str(),
                        derotWindowSec, rig.derotationStop ? "stop" : "warn");
                    wchar_t msg[128];
                    swprintf(msg, 128, L"%hs: rotation blurs detail after %.1f min (%ls)",
                             derotBody.c_str(), derotWindowSec / 60.0,
                             rig.derotationStop ? L"the run stops there" : L"it will warn");
                    notifBar.NotifyInfo(msg);
                }
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

    // Auto-stack hangs off the recording COMPLETING, not off the button.
    // A planned capture ends by reaching its frame limit inside
    // SubmitFrame, so a button hook would almost never fire. 'normal' is
    // false for an emergency stop or an ROI change, and neither should
    // launch minutes of CPU work.
    capturer.SetOnComplete([&](const std::string& path, bool normal) {
        Log("[STACK] recording complete: normal=%d  autoStack=%d  path=%s\n",
            normal ? 1 : 0,
            raduk::core::RigConfig::Get().autoStackOnStop ? 1 : 0,
            path.empty() ? "(empty)" : path.c_str());
        if (!normal) return;
        if (!raduk::core::RigConfig::Get().autoStackOnStop) return;
        // Auto-stack is unattended, and a FITS cube only stacks with
        // someone at AutoStakkert's window. Do not throw one up over the
        // session when a recording ends.
        if (raduk::core::IsFitsCapture(path)) {
            Log("[STACK] auto-stack passes over FITS: %s\n", path.c_str());
            notifBar.NotifyInfo(L"Auto-stack is SER only - press [STACK] "
                                L"to open this FITS in AutoStakkert");
            return;
        }
        doStackFile(path);
    });

    // ── Phase 48: what goes into the observation record ──────
    //
    // Everything here is read from the live rig, not from the panels:
    // the sliders write straight to the camera, so the camera is the
    // only place the settings in force are certain to be. A value that
    // cannot be read is left unknown and the record omits it -- it is
    // never filled with a plausible guess.
    capturer.SetObservationSource([&](raduk::core::ObservationRecord& r) {
        r.appVersion = RADUK_VERSION_A;
        r.target = gotoPanel.CurrentTarget();
        if (const TargetProfile* ap = profiles.Active()) r.profile = ap->name;
        if (cam.IsOpen()) {
            r.exposureMs = cam.GetControlValue(ControlId::ExposureUs) / 1000.0;
            r.gain       = (double)cam.GetControlValue(ControlId::Gain);
            int w = 0, h = 0, bin = 0; ImageType it{};
            if (cam.GetRegion(w, h, bin, it)) r.bin = bin;
            int sx = -1, sy = -1;
            if (cam.GetStartPos(sx, sy)) { r.roiX = sx; r.roiY = sy; }
            const CameraInfo ci = cam.Describe();
            r.colour = BayerName(ci.bayer);
            if (ci.pixelSizeUm > 0.0) r.pixelUm = ci.pixelSizeUm;
        }
        if (focuser.IsOpen()) {
            r.focuserPos   = focuser.GetPosition();
            const float tc = focuser.GetTemperature();
            // The EAF reports -273 when it has no sensor reading.
            if (tc > -100.0f) r.focuserTempC = tc;
        }
        if (mount && mount->IsConnected()) {
            double ra = 0, dec = 0, alt = 0, az = 0;
            if (mount->GetRADec(ra, dec)) { r.raHours = ra; r.decDeg = dec; }
            if (mount->GetAltAz(alt, az)) { r.altDeg = alt; r.azDeg = az; }
        }
        // Only while the tracker is running: with it off, the last
        // centroid is from whatever it last looked at, possibly
        // another target, and a framing recall would chase it.
        if (tracker.Stage() != raduk::core::AcquisitionStage::Off &&
            tracker.LastCentroidValid()) {
            r.targetXPx = tracker.LastCentroidX();
            r.targetYPx = tracker.LastCentroidY();
        }
        const auto& rigc = raduk::core::RigConfig::Get();
        if (rigc.SiteIsSet()) {
            r.siteLatDeg = rigc.siteLatDeg;
            r.siteLonDeg = rigc.siteLonDeg;
        }
        // The sky at the start of the run, from Leoben -- only when the
        // reading is fresh; a stale one is left out, not written.
        {
            const auto ls = leoben.Get();
            const int64_t nowU = (int64_t)std::time(nullptr);
            if (raduk::net::LeobenFresh(ls.last, nowU)) {
                const auto& c = ls.last;
                r.wxSource    = "leoben";
                r.wxAgeMin    = (nowU - c.fetchedUnix) / 60.0;
                r.cloudPct    = c.cloudPct;
                r.airTempC    = c.TempC();
                r.humidityPct = c.humidityPct;
                r.windMs      = c.WindMs();
                r.gustMs      = c.GustMs();
                r.dewPointC   = c.DewPointC();
                r.pressureHpa = raduk::net::StationPressureHpa(
                    c.PressureMslHpa(), rigc.siteElevationM, c.TempC());
                if (c.imagingScore >= 0) r.imagingScore = c.imagingScore;
                r.wxVerdict   = c.goNoGo;
            }
        }
    });

    // ── Phase 9: Autofocus ────────────────────────────────────
    // Completion callback — fires from scan thread, no Win32 UI calls here
    autofocus.SetOnComplete([&](raduk::core::AFResult result) {
        controls.SetAutofocusRunning(false);
        if (result.success) {
            // A caveat on the success path still has to reach the user. When
            // the sweep did not bracket focus the routine lands on whichever
            // end it stopped at, which is the best it saw but probably not
            // focus — reporting only "AF done" there reads as a clean result.
            wchar_t buf[192];
            wchar_t note[128] = L"";
            if (!result.message.empty())
                MultiByteToWideChar(CP_UTF8, 0, result.message.c_str(), -1,
                                    note, 127);
            swprintf(buf, 192, L"AF done: pos=%d  sharp=%.1f%%  +%.1f%%%s%s",
                     result.bestPosition, result.bestSharpness,
                     result.improvement,
                     note[0] ? L" — " : L"", note);
            controls.SetAutofocusStatus(buf);
            slog.Focuser("AutoFocus: pos=%d  sharp=%.1f%%  +%.1f%%%s%s",
                         result.bestPosition, result.bestSharpness,
                         result.improvement,
                         result.message.empty() ? "" : " — ",
                         result.message.c_str());
            Log("[AF] Complete: pos=%d  sharp=%.1f%%  improvement=+%.1f%%%s%s\n",
                result.bestPosition, result.bestSharpness, result.improvement,
                result.message.empty() ? "" : " — ", result.message.c_str());
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
    capturer.SetLogCallback([](const std::string& s) { Log("%s\n", s.c_str()); });

    raduk::core::AutoExposureAssistant ae;
    // Its running commentary was printf, which a GUI build throws away.
    // Needed on the night: AE deciding something odd is only debuggable
    // if it says what it decided and what it saw.
    ae.SetLogCallback([](const std::string& m) { Log("%s\n", m.c_str()); });
    {
        raduk::core::AutoExposureAssistant::Config aeCfg;
        // ASI678MC HCG floor — never go below 182 in auto mode
        aeCfg.gainMin   = raduk::profiles::CameraRails::GAIN_HCG_THRESHOLD;
        aeCfg.gainMax   = 570;
        // ── Phase 100: from the rail, not a fourth literal ─────
        //
        // These two lines said 0.5 and 40.0, and enforcing the rails
        // turned up how many copies of the planet ceiling existed:
        // EXP_PLANET_MAX_MS said 50, the assistant's Config default
        // said 50, a comment in ControlPanel.cpp said 50, and THIS
        // said 40 -- so the documented figure was contradicted by the
        // only one that ran.
        //
        // Derived from CameraRails::RailsFor now, and re-derived on
        // every profile change, so a DSO profile lets the assistant
        // reach seconds instead of being capped at a planetary
        // ceiling it has no business near.
        const auto planetRail =
            raduk::profiles::CameraRails::RailsFor("planet");
        aeCfg.expMinMs  = planetRail.minMs;
        aeCfg.expMaxMs  = planetRail.maxMs;
        ae.SetConfig(aeCfg);
    }
    // ── Phase 100: the assistant's clamp follows the target ────
    //
    // Re-derived from CameraRails::RailsFor on every profile apply, so
    // the rail and the clamp cannot drift -- which they had, four
    // ways, before this phase. A planetary rail caps the assistant at
    // 50 ms; a deep-sky one lets it reach five minutes, and without
    // this a DSO profile would hand it a 50 ms ceiling and it could
    // never expose a sub at all.
    profileHooks.setAERails = [&](const TargetProfile& p) {
        auto cfg = ae.Config_();
        const auto rail = raduk::profiles::CameraRails::RailsFor(p.targetType);
        if (cfg.expMinMs == rail.minMs && cfg.expMaxMs == rail.maxMs) return;
        cfg.expMinMs = rail.minMs;
        cfg.expMaxMs = rail.maxMs;
        ae.SetConfig(cfg);
        Log("[AE] exposure clamp now %.3g-%.3g ms for a '%s' target\n",
            rail.minMs, rail.maxMs, p.targetType.c_str());
    };

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
            cam.IsOpen() ? cam.GetTemperatureC() : 0.0f,
            lastAnalysis.valid ? lastAnalysis.seeingScore : 0.0f);
    });

    // [CENTROID] button — toggles searching on/off
    // Centroid/Lock/ROI callbacks now wired to toolbar buttons above.

    // doROILock — named lambda so it can be called from:
    //   a) [ROI] frame bar button press
    //   b) Auto-advance in the render loop (post-GoTo flow)
    //   c) Remote APEX command
    auto doROILock = [&]() {
        // Phase 61: this is acquisition stage 3, not a manual ROI tool --
        // it crops to the planet only after Centering has held it inside
        // CENTRE_THRESHOLD_PX for DWELL_FRAMES. It used to do nothing at
        // all otherwise, which reads as "the ROI button is broken".
        // To draw an ROI by hand, drag a box on the preview instead.
        if (tracker.Stage() != raduk::core::AcquisitionStage::Centering ||
            !tracker.IsDwellMet()) {
            const char* why =
                (tracker.Stage() != raduk::core::AcquisitionStage::Centering)
                    ? "not in Centering (run CENTROID, then LOCK)"
                    : "planet not yet steady on centre";
            Log("[ACQ] ROI crop refused - %s\n", why);
            notifBar.NotifyWarn(L"ROI crop needs a centred, locked planet - drag a box to set one by hand");
            return;
        }
        {
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
                const int newW = 1280, newH = 960;
                // The centroid is ROI-relative, so the tracked origin has
                // to be added back to get a sensor-absolute position.
                // applyFormat does the alignment, clamping, read-back and
                // restart from there. This used to be a hand-copied
                // duplicate of that sequence, and the copy kept bit depth
                // pinned to 8 whatever the toolbar said -- so a 16-bit
                // session silently dropped to 8-bit the moment the planet
                // locked.
                slog.System("ROI: %dx%d centred on planet", newW, newH);
                applyFormat(newW, newH, roiBits,
                            roiStartX + (int)cx, roiStartY + (int)cy,
                            "[ROI] crop on planet");

                if (!lastFrameForSolve.empty() && lastFrameSolveW > 0) {
                    const TargetProfile* active = profiles.Active();
                    std::string target = active ? active->name : "Unknown";
                    float camTemp   = cam.GetTemperatureC();
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
        // Phase 102: and the PLANET LOCK button, which would otherwise
        // stay lit over a tracker that has gone back to Off.
        controls.SetPlanetLockState(false);
        // Note: ROI is NOT automatically restored here — keeps last crop.
    });

    // Dwell met callback — just log; UI already reflects IsDwellMet()
    tracker.SetDwellCallback([&]() {
        Log("[ACQ] Dwell condition met - planet is centred\n");
        slog.Mount("Acquisition: Planet centred");
    });
    // Phase 65: auto-acquire finished. Guiding is live; the ROI crop is
    // left to the user because it changes capture resolution.
    tracker.SetAutoLockedCallback([&]() {
        Log("[ACQ] Auto-acquire complete - LOCKED, guiding active\n");
        slog.Mount("Acquisition: Locked (auto) - guiding active");
        notifBar.NotifyInfo(L"Planet locked - guiding active. [ROI] to crop.");
    });

    // Phase 51: calibration result — success auto-advances to Centering
    // inside the tracker; failure drops back to Searching.
    tracker.SetCalibrationCallback([&](bool ok, const std::string& msg) {
        if (ok) {
            Log("[ACQ] Calibration OK: %s — Stage 2: CENTERING\n", msg.c_str());
            slog.Mount("Calibration OK: %s", msg.c_str());
        } else {
            Log("[ACQ] Calibration FAILED: %s — back to SEARCHING\n", msg.c_str());
            slog.Mount("Calibration failed: %s", msg.c_str());
        }
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

    // Shared by ESC and the MountController STOP button. Both must halt the
    // centroid-tracker correction thread, not just the active slews —
    // otherwise the guide loop restarts the mount within 250-500ms of the
    // user pressing STOP.
    auto doEmergencyStop = [&]() {
        if (capturer.IsRecording()) {
            capturer.StopRecording(false);  // emergency stop - do not auto-stack
            usbMgr.SetCapturing(false);
        }
        autofocus.Abort();                // Phase 9: stop any running AF scan
        controls.SetAutofocusRunning(false);
        tracker.Reset();  // stops all stages, joins correction thread
        controls.SetSkyMapLocked(false);
        controls.EmergencyStop();
        slog.Safety("EMERGENCY STOP");
    };
    window.SetEmergencyStopHandler(doEmergencyStop);

    // Re-register: the early wiring (before tracker callbacks existed) only
    // logged. MountController::EmergencyStop has already stopped both axes
    // by the time this fires; this adds the tracker/AF/recording shutdown.
    mountCtrl.SetEmergencyStopCallback([&]() {
        Log("[SAFETY] MountCtrl: STOP pressed\n");
        doEmergencyStop();
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
    window.SetStackLastHandler([&]() { doStackFile(capturer.LastFilePath()); });
    toolbar.SetStackCallback([&]() { doStackFile(capturer.LastFilePath()); });

    // Phase 106: Shift+K, the last recording in whatever Windows opens
    // that kind of file with. The association is the configuration, so
    // the app needs no setting per viewer, and a FITS and a SER can go
    // to different programs without it knowing either.
    window.SetOpenLastHandler([&]() {
        const std::string path = capturer.LastFilePath();
        if (path.empty()) {
            notifBar.NotifyInfo(L"No finished recording to open yet");
            return;
        }
        const HINSTANCE r = ShellExecuteW(nullptr, L"open", toWide(path).c_str(),
                                          nullptr, nullptr, SW_SHOWNORMAL);
        if ((INT_PTR)r <= 32) {
            Log("[OPEN] ShellExecute %d: %s\n", (int)(INT_PTR)r, path.c_str());
            notifBar.NotifyWarn(L"Could not open it - no program is set for "
                                L"this file type in Windows");
            return;
        }
        Log("[OPEN] %s\n", path.c_str());
    });
    controls.SetFXCycleCallback([&]() { doFXCycle(); });

    // ── Phase 12b: the advisory loop ──────────────────────────
    // Watches and occasionally remarks, where the chat panel answers
    // when asked. It is handed text and gives back text: it holds no
    // reference to the camera, mount, focuser or capturer, so it cannot
    // act on the rig whatever the model says. Advice is surfaced in the
    // notification bar, which is already the place transient remarks go.
    static raduk::core::AdvisoryLoop advisory;
    advisory.SetIntervalSeconds(raduk::core::RigConfig::Get().advisoryIntervalS);
    advisory.SetEnabled(raduk::core::RigConfig::Get().advisoryEnabled);
    advisory.SetLogger([](const std::string& m) {
        Log("[ADVISORY] %s\n", m.c_str());
    });
    advisory.SetOnAdvice([&](const std::string& text) {
        std::wstring w(text.begin(), text.end());   // model output is ASCII
        notifBar.NotifyInfo(L"Advice: " + w);
        slog.System("Advisory: %s", text.c_str());
    });
    if (advisory.Enabled())
        Log("[ADVISORY] Enabled from rig.ini, every %ds\n",
            advisory.IntervalSeconds());

    auto doAdvisoryToggle = [&]() {
        const bool on = !advisory.Enabled();
        advisory.SetEnabled(on);
        auto& rg = raduk::core::RigConfig::Get();
        rg.advisoryEnabled = on;
        rg.Save();
        Log("[ADVISORY] %s (saved)\n", on ? "ON" : "OFF");
        if (on && !raduk::core::AIAssistant::IsOllamaRunning())
            notifBar.NotifyWarn(L"Advice on, but Ollama is not running");
        else
            notifBar.NotifyInfo(on ? L"Advice on - occasional suggestions"
                                   : L"Advice off");
    };
    window.SetAdvisoryToggleHandler([&]() { doAdvisoryToggle(); });

    // ── Phases 27 & 28: dark and flat frames ──────────
    // Masters are built here from the live camera rather than loaded
    // from a PNG as the specs suggested: a PNG is debayered 8-bit RGB,
    // which has already been through the pass this correction has to
    // happen before. See CalibrationFrames.h.
    static raduk::core::CalibrationFrames calib;
    preview.SetCalibration(&calib);

    // Kept beside the captures, so a master found next launch is the
    // one belonging to this rig rather than a stale file in AppData.
    auto calPath = [&](bool dark) {
        std::string d = controls.GetOutputDir();
        if (!d.empty() && d.back() != '\\' && d.back() != '/') d += "\\";
        return d + (dark ? "master_dark.cal" : "master_flat.cal");
    };

    auto calStatus = [&]() {
        wchar_t buf[160];
        if (calib.Building()) {
            swprintf(buf, 160, L"Cal: averaging %d/%d frames",
                     calib.BuildProgress(), calib.BuildTarget());
            controls.SetCalStatus(buf);
            return;
        }
        // Says which masters exist AND whether they are actually being
        // applied. A master that does not fit the current format is
        // loaded but doing nothing, and reporting that as "on" is the
        // one thing this label must never do.
        const char* dp = calib.DarkProblem(roiWidth, roiHeight, roiBits,
                                           (int)cam.GetControlValue(ControlId::Gain),
                                           cam.GetControlValue(ControlId::ExposureUs) / 1000.0);
        const char* fp = calib.FlatProblem(roiWidth, roiHeight, roiBits);
        if (!calib.HasDark() && !calib.HasFlat()) {
            controls.SetCalStatus(L"Cal: none - DARK or FLAT to build");
            return;
        }
        // "ON" means the correction is running; a reason in brackets is a
        // warning that it is running anyway. "off" means it is not. The
        // first version printed the warning alone, which read as off.
        const bool dOn = calib.DarkApplies(roiWidth, roiHeight, roiBits);
        const bool fOn = calib.FlatApplies(roiWidth, roiHeight, roiBits);
        auto part = [](const wchar_t* name, bool have, bool on,
                       const char* why) -> std::wstring {
            if (!have) return L"";
            std::wstring t = L" "; t += name;
            if (on) {
                t += L" ON";
                // Geometry fits, so it is applied, but something else
                // differs and the result will be imperfect.
                if (why) { t += L"("; t += std::wstring(why, why + strlen(why));
                           t += L")"; }
            } else {
                t += L" off(";
                t += why ? std::wstring(why, why + strlen(why)) : L"unusable";
                t += L")";
            }
            return t;
        };
        std::wstring t = L"Cal:";
        t += part(L"dark", calib.HasDark(), dOn, dp);
        t += part(L"flat", calib.HasFlat(), fOn, fp);
        controls.SetCalStatus(t);
        // Logged as well as shown: the label lives in a section that
        // starts collapsed, and "loaded but not applied" is the state
        // most worth knowing, because the picture is uncorrected while
        // a master exists.
        Log("[CAL] dark=%s%s%s flat=%s  (format %dx%d %d-bit)\n",
            !calib.HasDark() ? "none"
                : (dOn ? (dp ? "APPLIED-with-warning" : "APPLIED")
                       : "NOT-APPLIED"),
            dp ? " - " : "", dp ? dp : "",
            !calib.HasFlat() ? "none"
                : (fOn ? (fp ? "APPLIED-with-warning" : "APPLIED")
                       : "NOT-APPLIED"),
            roiWidth, roiHeight, roiBits);
    };

    // A dark needs the light blocked and a flat needs an evenly lit
    // field; neither can be checked from here, so both say what they
    // need rather than assuming the user knew.
    auto beginCal = [&](bool dark) {
        if (!cam.IsOpen()) return;
        if (capturer.IsRecording()) {
            notifBar.NotifyWarn(L"Finish the recording before building a master");
            return;
        }
        if (calib.Building()) { calib.Abort();
            notifBar.NotifyInfo(L"Master build cancelled"); calStatus(); return; }
        calib.Begin(dark, roiWidth, roiHeight, roiBits,
                    (int)cam.GetControlValue(ControlId::Gain),
                    cam.GetControlValue(ControlId::ExposureUs) / 1000.0,
                    cam.GetTemperatureC(), 32);
        notifBar.NotifyInfo(dark
            ? L"Building master dark - cover the telescope now (32 frames)"
            : L"Building master flat - point at an evenly lit surface (32 frames)");
        Log("[CAL] Building master %s: %dx%d %d-bit gain=%d exp=%.2fms\n",
            dark ? "dark" : "flat", roiWidth, roiHeight, roiBits,
            (int)cam.GetControlValue(ControlId::Gain),
            cam.GetControlValue(ControlId::ExposureUs) / 1000.0);
        calStatus();
    };
    controls.SetCaptureFits(capturer.Config().format == CaptureFormat::FITS);
    controls.SetCaptureFormatCallback([&](bool fits) {
        const CaptureFormat f = fits ? CaptureFormat::FITS : CaptureFormat::SER;
        auto& rig = raduk::core::RigConfig::Get();
        rig.captureFormat = raduk::core::CaptureFormatName(f);
        rig.SaveCaptureFormat();
        // A run under way keeps the format it opened with. The next one,
        // from any route into a recording, takes this.
        if (!capturer.IsRecording()) {
            CaptureConfig cfg = capturer.Config();
            cfg.format = f;
            capturer.SetConfig(cfg);
        }
    });
    controls.SetCalDarkCallback([&]() { beginCal(true);  });
    controls.SetCalFlatCallback([&]() { beginCal(false); });
    controls.SetCalClearCallback([&]() {
        calib.Abort(); calib.ClearDark(); calib.ClearFlat();
        DeleteFileA(calPath(true).c_str());
        DeleteFileA(calPath(false).c_str());
        Log("[CAL] Cleared both masters\n");
        notifBar.NotifyInfo(L"Calibration cleared");
        calStatus();
    });

    // Whatever was built last night still applies tonight if the
    // format and settings match, and CalibrationFrames says so.
    if (calib.LoadMaster(calPath(true)))
        Log("[CAL] Loaded master dark (%d frames averaged)\n",
            calib.DarkMeta().averaged);
    if (calib.LoadMaster(calPath(false)))
        Log("[CAL] Loaded master flat (%d frames averaged)\n",
            calib.FlatMeta().averaged);
    calStatus();

    // ── Phase 37: display stretch ───────────────────
    // Black and white points on the DISPLAY. This is the control that
    // lets a faint target be seen without touching gain or exposure,
    // which would change what gets recorded.
    auto stretchStatus = [&]() {
        wchar_t b[96];
        if (!preview.StretchEnabled())
            swprintf(b, 96, L"Stretch: off (L)");
        else if (!preview.StretchActive())
            swprintf(b, 96, L"Stretch: on, 0-255 (no effect)");
        else
            swprintf(b, 96, L"Stretch: %d-%d", preview.StretchBlack(),
                     preview.StretchWhite());
        controls.SetStretchLabel(b);
    };
    controls.SetStretchCallback([&](int black, int white) {
        preview.SetStretch(black, white);
        // Moving a slider implies wanting to see the effect; requiring a
        // separate switch-on would just look broken.
        if (!preview.StretchEnabled() && preview.StretchActive())
            preview.SetStretchEnabled(true);
        // Read back: SetStretch keeps the window at least 4 wide, so the
        // sliders must follow what it decided rather than what was asked.
        controls.SetStretchPoints(preview.StretchBlack(),
                                  preview.StretchWhite());
        stretchStatus();
    });
    auto doStretchToggle = [&]() {
        preview.SetStretchEnabled(!preview.StretchEnabled());
        Log("[STRETCH] %s (%d-%d)\n",
            preview.StretchEnabled() ? "ON" : "OFF",
            preview.StretchBlack(), preview.StretchWhite());
        stretchStatus();
    };
    window.SetStretchToggleHandler([&]() { doStretchToggle(); });
    stretchStatus();

    // Phase 26: N key and panel button toggle Night Vision Mode
    auto doNightVision = [&]() {
        preview.ToggleNightVision();
        bool on = preview.NightVisionOn();
        // Phase 26: the chrome follows the preview. Theme holds the mode
        // as process state because the painting code is spread across
        // seventeen files; bumping it invalidates every cached brush and
        // pen, which is what makes a toggle actually repaint.
        raduk::gui::Theme::SetNightVision(on);
        controls.SetNightVisionActive(on);
        controls.ForceRepaint();
        // Every OTHER window as well, which is what was missing.
        //
        // Theme::NV is evaluated at paint time, so these windows had the
        // right colours available all along -- nobody ever told them to
        // paint. The control panel got a ForceRepaint and the rest were
        // left showing whatever they last drew, so on a multi-screen
        // desk an entire monitor of panels stayed in day colours while
        // the main screen went red.
        //
        // RDW_ALLCHILDREN matters: these panels are built from Win32
        // child controls whose colours come from WM_CTLCOLOR*, and
        // repainting only the parent would leave every edit box and
        // button behind.
        //
        // D3: every window we own, not every window in the layout
        // roster. The object info popup, the ADC tool and the share
        // view are all ours and none of them had a line in that list,
        // so each stayed in day colours until something else happened
        // to invalidate it.
        //
        // D2: re-tint the stock controls BEFORE the repaint, so the
        // list views and tab strips come back in the new mode rather
        // than a frame late. Title bars are not touched here -- they
        // are dark in either mode.
        styleChrome();
        for (HWND hwnd : ourWindows())
            RedrawWindow(hwnd, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN |
                         RDW_UPDATENOW);
        // Persist immediately rather than at shutdown: the app can be
        // killed at the mount, and losing this setting means the next
        // launch blinds you in the dark.
        auto& rg = raduk::core::RigConfig::Get();
        rg.nightVision = on;
        rg.Save();
        Log("[NIGHT] Night Vision %s (saved)\n", on ? "ON" : "OFF");
    };
    preview.SetShowOverlays(raduk::core::RigConfig::Get().overlayTelemetry);

    // Restore it before the first frame is drawn.
    if (raduk::core::RigConfig::Get().nightVision) {
        preview.SetNightVision(true);
        raduk::gui::Theme::SetNightVision(true);
        controls.SetNightVisionActive(true);
        Log("[NIGHT] Night Vision restored ON from rig.ini\n");
    }
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
        if (outDir.empty())  outDir  = raduk::core::Paths::Captures();
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
    window.SetRecallFramingHandler(doRecallFraming);   // phase 48
    window.SetRecallOffsetHandler(doRecallOffset);
    window.SetSkyMapToggleHandler([&]()     { skyMap.Toggle();       });
    // Shift+M. The second map, for the control screen, so the first
    // can stay where people can see it.
    window.SetSkyMap2ToggleHandler([&]() {
        skyMap2.Toggle();
        if (skyMap2.IsVisible()) skyMap2.Tick();
        Log("[SKYMAP] second map %s\n", skyMap2.IsVisible() ? "on" : "off");
    });
    window.SetObjectPanelToggleHandler([&]() {
        objectPanel.Toggle();
        Log("[OBJECT] panel %s\n", objectPanel.IsVisible() ? "on" : "off");
    });
    window.SetObjectPanelFullscreenHandler([&]() {
        if (!objectPanel.IsVisible()) objectPanel.Show();
        objectPanel.ToggleFullscreen();
    });
    // ── Phase 97.5: Ctrl+Shift+M starts or stops a mosaic ─────
    //
    // The same chord does both, so whoever started an eight-minute
    // unattended sequence does not have to find a different key under
    // pressure. Escape remains the emergency stop for the mount
    // itself and is untouched: this stops the SEQUENCE, that stops
    // the motion.
    window.SetMosaicToggleHandler([&]() {
        if (gotoPanel.MosaicRunning()) {
            gotoPanel.AbortMosaic();
            Log("[MOSAIC] stopped by the operator\n");
            return;
        }
        // Whatever the capture profile currently names, resolved the
        // same way everything else resolves it, so "Moon - Copernicus"
        // starts a lunar mosaic.
        std::string target = gotoPanel.CurrentTarget();
        if (target.empty()) {
            Log("[MOSAIC] no target selected\n");
            return;
        }
        gotoPanel.StartMosaic(target);
    });

    window.SetShareViewToggleHandler([&]() {
        shareView.Toggle();
        Log("[SHARE] view %s\n", shareView.IsVisible() ? "on" : "off");
    });
    // Ctrl+Shift+V. The view is borderless on a screen nobody works
    // at, so the way back to a window it can be dragged by has to be
    // reachable from the screen the owner IS at.
    window.SetShareViewFullscreenHandler([&]() {
        if (!shareView.IsVisible()) shareView.Show();
        shareView.ToggleFullscreen();
        Log("[SHARE] view %s\n",
            shareView.IsFullscreen() ? "filling its screen" : "windowed");
    });
    window.SetHistogramToggleHandler([&]()  { histWindow.Toggle();   });
    window.SetSharpnessToggleHandler([&]()  { sharpWindow.Toggle();  });
    window.SetTemperatureToggleHandler([&]() { tempWindow.Toggle(); });
    window.SetStatusToggleHandler([&]()      { statusWindow.Toggle(); });
    window.SetADCAlignHandler([&]() {
        // Nothing to align without a corrector in the train. The window
        // would still show a residual -- the atmosphere disperses
        // whether or not you own the part -- and that reads as
        // something to go and adjust.
        if (!raduk::core::RigConfig::Get().adcFitted) {
            Log("[ADC] Shift+A ignored - no ADC in the imaging train. "
                "Tick \"ADC fitted\" on the readiness dialog once one is.\n");
            notifBar.NotifyInfo(L"No ADC in the imaging train - tick "
                                L"\"ADC fitted\" on the READY screen first");
            return;
        }
        // Clear the rolling window on open. Samples from the last time
        // it was up could be from a different target at a different
        // altitude, and a median built from those would read as a
        // residual that no amount of dialling changes.
        if (!adcAlignWindow.IsVisible()) adcAlign.Reset();
        adcAlignWindow.Toggle();
    });
    window.SetOverlayToggleHandler([&]() {
        preview.ToggleOverlays();
        const bool on = preview.OverlaysVisible();
        auto& rg = raduk::core::RigConfig::Get();
        rg.overlayTelemetry = on;
        rg.Save();
        Log("[OVERLAY] viewfinder overlays %s (saved)\n", on ? "ON" : "OFF");
    });
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

    // ── Startup visibility for the windows on their own screens ──
    //
    // All shown, and shown WITHOUT taking focus: the main window must
    // keep it, or the first keystroke of a session goes to whichever
    // panel happened to open last. Their Show() methods call
    // SetForegroundWindow, which is right when you press the shortcut
    // and wrong at startup, so this goes through the handle directly.
    //
    // GoTo used to start hidden on purpose -- the comment here said it
    // prevented an accidental slew from a mis-click while imaging. That
    // was a fair worry when it opened on top of the imaging controls.
    // It now lives on a screen of its own, well away from anything you
    // touch during a capture, so the mis-click it guarded against is a
    // great deal less likely. Press G to close it if you would rather
    // have it shut, and this is a one-line change back.
    //
    // Phase 102: only where the layout file has no opinion. This loop
    // predates the saved open/closed state and overrode it: it runs
    // long after the restore pass, so these three came back open
    // however they were saved. The default below still stands for a
    // first run, or for a layout written before these roles existed.
    {
        const std::pair<const char*, HWND> kDefaultVisible[] = {
            { "goto",   gotoPanel.Handle()  },
            { "help",   helpWindow.Handle() },
            { "aichat", aiChat.Handle()     },
        };
        for (const auto& [role, h] : kDefaultVisible) {
            if (!h) continue;
            if (workspace.StateFor(role) !=
                raduk::gui::WorkspaceLayout::SavedState::Unknown) {
                Log("[LAYOUT] %s: startup default skipped, the layout "
                    "file has a saved state for it\n", role);
                continue;
            }
            ShowWindow(h, SW_SHOWNOACTIVATE);
        }
    }
    SetForegroundWindow(window.Handle());

    // ── Toolbar button callbacks (Win32 native buttons) ────────
    // doAcquireToggle -- named lambda for the same reason doROILock is
    // one: more than one control runs it. The toolbar's CENTROID button
    // and the control panel's PLANET LOCK button are the same action,
    // and before phase 102 the second was wired to nothing.
    //
    // It reports the stage it ended in, so the caller can show what
    // actually happened rather than what was asked for.
    auto doAcquireToggle = [&]() -> bool {
        if (tracker.Stage() == raduk::core::AcquisitionStage::Off) {
            // Phase 65: one press now runs the whole sequence -- find the
            // planet, calibrate, centre, lock -- instead of requiring
            // CENTROID then LOCK then ROI, each of which silently did
            // nothing when its precondition was unmet.
            tracker.SetMount(mount);
            std::string whyNot;
            if (!tracker.StartAutoAcquire(whyNot)) {
                Log("[ACQ] Auto-acquire refused: %s\n", whyNot.c_str());
                slog.Mount("Acquisition refused: %s", whyNot.c_str());
                std::wstring w(whyNot.begin(), whyNot.end());
                notifBar.NotifyWarn(L"Cannot acquire: " + w);
                return false;
            }
            Log("[ACQ] Auto-acquire started: searching\n");
            slog.Mount("Acquisition: auto (search -> calibrate -> centre -> lock)");
            notifBar.NotifyInfo(L"Acquiring planet - searching...");
            return true;
        }
        Log("[ACQ] Cancelled — reset to Off\n");
        slog.Mount("Acquisition: Cancelled");
        tracker.Reset();
        controls.SetSkyMapLocked(false);
        return false;
    };

    // Both controls run it, and both are told the outcome. A refused
    // acquire now leaves PLANET LOCK unlit instead of lit over nothing.
    toolbar.SetCentroidCallback([&]() {
        controls.SetPlanetLockState(doAcquireToggle());
    });
    controls.SetPlanetLockCallback([&](bool want) {
        // want is what the press asked for. Pressing PLANET LOCK while
        // acquisition is already running means stop, which is what the
        // toggle does from any stage other than Off -- so the request
        // and the toggle agree and the bool is only a sanity check.
        const bool runningNow =
            tracker.Stage() != raduk::core::AcquisitionStage::Off;
        if (want == runningNow) {
            // Already in the asked-for state: nothing to toggle, just
            // make the button agree with the tracker.
            controls.SetPlanetLockState(runningNow);
            return;
        }
        controls.SetPlanetLockState(doAcquireToggle());
    });
    toolbar.SetLockCallback([&]() {
        // Phase 61: refuse out loud. This silently did nothing when the
        // conditions were unmet, so a failed LOCK was indistinguishable
        // from a dead button.
        if (tracker.Stage() != raduk::core::AcquisitionStage::Searching ||
            !tracker.IsStable()) {
            const char* why =
                (tracker.Stage() == raduk::core::AcquisitionStage::Off)
                    ? "acquisition not started - press CENTROID first"
                : (tracker.Stage() != raduk::core::AcquisitionStage::Searching)
                    ? "already past Searching"
                    : "no steady centroid yet";
            // Phase 69: if the centroid is blocked, say which kind of
            // blocked -- too faint and nothing-in-frame need opposite
            // responses and used to read identically.
            std::string blocked = tracker.CentroidBlockReason();
            std::string reason  = blocked.empty() ? std::string(why)
                                                  : std::string(why) + " (" + blocked + ")";
            Log("[ACQ] LOCK refused - %s\n", reason.c_str());
            std::wstring wr(reason.begin(), reason.end());
            notifBar.NotifyWarn(L"LOCK refused - " + wr);
            return;
        }
        {
            // Phase 51: LOCK measures the pulse→pixel transform first
            // (or reuses a fresh calibration), then advances to Centering.
            Log("[ACQ] LOCK: calibrating guide axes\n");
            slog.Mount("Acquisition: Calibrating");
            tracker.StartCalibration();
        }
    });
    toolbar.SetROICallback([&]()   { doROILock(); });

    // Phase 102: POINT SOLVE -> SYNC on the control panel. Same solve
    // the GoTo panel's SOLVE TO ALIGN runs, and it guards and reports
    // its own preconditions, so there is nothing to check here.
    controls.SetSolveSyncCallback([&]() {
        Log("[SOLVE] Point solve -> sync requested from the control panel\n");
        gotoPanel.PlateSolveAndSync();
    });
    // Phase 84: resolution and bit depth. FULL is the way back from a
    // crop -- before this there was none, and a bad [ROI] or a dragged
    // box meant restarting the app to recover the field.
    toolbar.SetResolutionCallback([&](int w, int h) {
        // Keep whatever is in the middle of the frame in the middle of
        // the frame. Widening the field to find a planet that drifted
        // out should not also move the field somewhere else.
        const int cx = roiStartX + roiWidth  / 2;
        const int cy = roiStartY + roiHeight / 2;
        applyFormat(w, h, roiBits, cx, cy, "resolution button");
    });
    toolbar.SetBitDepthCallback([&](int bits) {
        applyFormat(roiWidth, roiHeight, bits,
                    roiStartX + roiWidth / 2, roiStartY + roiHeight / 2,
                    "bit depth button");
    });
    toolbar.UpdateFormat(roiWidth, roiHeight, roiBits);
    toolbar.SetColorCallback([&]() { preview.CycleDebayer(); });
    toolbar.SetMapCallback([&]()   { skyMap.Toggle(); });
    toolbar.SetGoToCallback([&]()  { gotoPanel.ShowCentered(); });
    toolbar.SetReticleCallback([&]() { doReticle(); });
    toolbar.SetClipCallback([&]()    { preview.ToggleClip(); });
    toolbar.SetHardwareCallback([&]() { doHardwareButton(); });

    // [DASH]: open the browser dashboard already holding the access token.
    // The browser is asked for by name rather than by opening the file:
    // handing a file:// URL to its file association can drop the
    // #fragment, and the fragment is where the token rides (see
    // DashboardLaunch.h for why). Neither the token nor the URL is
    // logged -- this log is the assistant's corpus.
    auto doDashboard = [&]() {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring exeDir(exePath);
        const auto slash = exeDir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) exeDir = exeDir.substr(0, slash);

        std::wstring page;
        for (const auto& d : raduk::core::DashboardSearchDirs(exeDir)) {
            const std::wstring p = d + L"\\" + raduk::core::kDashboardFile;
            if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) { page = p; break; }
        }
        if (page.empty()) {
            Log("[DASH] RadukCaptureDashboard.html not found beside the exe or above it\n");
            notifBar.NotifyWarn(L"Dashboard not found - RadukCaptureDashboard.html "
                                L"should be beside the exe or in a folder above it");
            return;
        }

        const std::string& tok = RigConfig::Get().wsToken;
        const bool withToken = raduk::net::ws::TokenLooksStrong(tok);
        const std::string url = raduk::core::DashboardUrl(page, withToken ? tok : "");
        const std::wstring wurl(url.begin(), url.end());   // ASCII: percent-encoded

        wchar_t browser[MAX_PATH] = {};
        DWORD n = MAX_PATH;
        HINSTANCE r;
        if (SUCCEEDED(AssocQueryStringW(ASSOCF_IS_PROTOCOL, ASSOCSTR_EXECUTABLE,
                                        L"http", L"open", browser, &n))) {
            const std::wstring arg = L"\"" + wurl + L"\"";
            r = ShellExecuteW(nullptr, L"open", browser, arg.c_str(), nullptr, SW_SHOWNORMAL);
        } else {
            r = ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        if ((INT_PTR)r <= 32) {
            Log("[DASH] Could not open the browser (ShellExecute %d)\n", (int)(INT_PTR)r);
            notifBar.NotifyWarn(L"Could not open the browser for the dashboard");
            return;
        }
        Log("[DASH] Dashboard opened%s\n",
            withToken ? ", signed in" : " WITHOUT a token - WsToken in rig.ini is missing or too weak");
        if (withToken) notifBar.NotifyInfo(L"Dashboard opened in the browser - it connects by itself");
        else           notifBar.NotifyWarn(L"Dashboard opened, but there is no usable WsToken to give it");
    };
    toolbar.SetDashCallback([&]() { doDashboard(); });
    toolbar.SetFollowLabel(raduk::core::RigConfig::Get().roiFollow
                               ? L"[FOLLOW] ON" : L"[FOLLOW] OFF");
    toolbar.SetFollowCallback([&]() {
        auto& rig = raduk::core::RigConfig::Get();
        rig.roiFollow = !rig.roiFollow;
        rig.SaveRoiFollow();
        followGaveUp = false;
        if (!roiFollow.Armed()) roiFollow.Reset();
        toolbar.SetFollowLabel(rig.roiFollow ? L"[FOLLOW] ON" : L"[FOLLOW] OFF");
        notifBar.NotifyInfo(rig.roiFollow
            ? L"ROI following on - it runs once the planet is locked on a crop"
            : L"ROI following off");
    });
    toolbar.SetDerotLabel(raduk::core::RigConfig::Get().derotationStop
                              ? L"[DEROT] STOP" : L"[DEROT] WARN");
    toolbar.SetDerotCallback([&]() {
        auto& rig = raduk::core::RigConfig::Get();
        rig.derotationStop = !rig.derotationStop;
        rig.SaveDerotationStop();
        toolbar.SetDerotLabel(rig.derotationStop ? L"[DEROT] STOP"
                                                 : L"[DEROT] WARN");
        notifBar.NotifyInfo(rig.derotationStop
            ? L"Runs now stop at the derotation window, from the next recording"
            : L"Runs now only warn at the derotation window, from the next recording");
    });
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
            capturer.StopRecording(false);  // app exiting - do not auto-stack
        }
        // Shut down floating windows and AI gracefully before message loop exits
        aiChat.Destroy();
        helpWindow.Destroy();
        objectInfo.Hide();
        notifBar.Dismiss();
        mountCtrl.Hide();
        // Phase 88: keep the rig state before the window goes. This is
        // the path that catches slider changes, which write straight to
        // the camera and are mirrored nowhere else.
        saveSession("exit");
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
    // Phase 85: the profile's stepFine / stepCoarse finally reach
    // something. They have been saved and loaded since the profile
    // format existed and were read by nothing; Window.cpp hardcoded 10
    // and 100, which are exactly their defaults, so these fields were
    // plainly meant for these two keys all along. The handler's sign is
    // the direction the key chose; the magnitude is the profile's.
    auto profileStep = [&](int signedSteps, bool coarse) {
        if (!focuser.IsOpen()) return;
        const TargetProfile* p = profiles.Active();
        int mag = coarse ? (p ? p->stepCoarse : 100)
                         : (p ? p->stepFine   : 10);
        if (mag < 1) mag = coarse ? 100 : 10;   // a 0 in a hand-edited ini
        focuser.MoveRelative(signedSteps < 0 ? -mag : mag);
    };
    window.SetFocusFineHandler  ([&](int steps) { profileStep(steps, false); });
    window.SetFocusCoarseHandler([&](int steps) { profileStep(steps, true);  });
    window.SetTrackingToggleHandler([&]() {
        if (mount && mount->IsConnected()) {
            auto mode = mount->GetTrackingMode();
            mount->SetTrackingMode(mode == IMount::OFF
                ? IMount::ALT_AZ : IMount::OFF);
            slog.Mount("Tracking toggled");
        }
    });
    window.SetJogHandler([&](int axis, int dir, bool pressed) {
        if (!mount) return;
        if (pressed) {
            if (!mount->IsConnected()) return;
            // Phase 93: a rate, not an index.
            //
            // SlewFixed takes one of nine presets, and this app has
            // never used anything else -- so the jog has been a
            // nine-position switch on a mount that has always had a
            // variable-rate command. The presets stay as the UI; they
            // are now a number of arcseconds per second, which is
            // what actually goes to the mount.
            //
            // Falls back when the backend says it cannot: better a
            // preset than a jog that does nothing.
            const int    preset = controls.JogRate();
            const double rateAs = raduk::core::JogRateArcsecPerSec(preset);
            bool moved = false;
            if (mount->SupportsVariableRate())
                moved = mount->SlewVariable(axis, dir, rateAs);
            if (!moved)
                mount->SlewFixed(axis, dir, preset);
            suspendTrackerForJog(true);
            slog.Mount("Arrow jog axis=%d dir=%d %.1f arcsec/s (preset %d) start",
                       axis, dir, rateAs, preset);
        } else {
            // Stop unconditionally — see MountController jog callback.
            mount->StopSlew(axis);
            suspendTrackerForJog(false);
        }
    });

    // ── WS command messages (cross-thread safe via PostMessage) ──
    // WM_USER+1  — drain APEX WebSocket command queue
    // WM_USER+10 — GoTo settle timer expired → start centroid acquisition
    window.SetUserMessageHandler([&](UINT msg) {
        if (msg == WM_USER + 11) {
            // Phase 39: framing shot elapsed — put the camera back.
            if (framing.active.exchange(false) && cam.IsOpen()) {
                cam.SetGain(framing.savedGain);
                cam.SetExposure(framing.savedExpUs / 1000.0);
                Log("[FRAME] Restored: gain=%d exp=%.1fms\n",
                    framing.savedGain, framing.savedExpUs / 1000.0);
                slog.Camera("Framing shot ended - settings restored");
                notifBar.NotifyInfo(L"Framing shot ended - settings restored");
            }
            return;
        }
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
                // Phase 65: same one-shot path as the CENTROID button, so
                // the remote and the UI cannot drift apart.
                if (tracker.Stage() == raduk::core::AcquisitionStage::Off) {
                    tracker.SetMount(mount);
                    std::string whyNot;
                    if (tracker.StartAutoAcquire(whyNot))
                        Log("[WS] Remote centroid_start: auto-acquire\n");
                    else
                        Log("[WS] Remote centroid_start refused: %s\n",
                            whyNot.c_str());
                }
            } else if (rc.cmd == "centroid_lock") {
                if (tracker.Stage() == raduk::core::AcquisitionStage::Searching && tracker.IsStable()) {
                    tracker.StartCalibration();
                    Log("[WS] Remote centroid_lock (calibrates first)\n");
                }
            } else if (rc.cmd == "centroid_roi") {
                doROILock();
                Log("[WS] Remote centroid_roi\n");
            } else if (rc.cmd == "cables_dressed") {
                // The one thing that legitimately zeroes the count. It
                // cannot be inferred -- the app cannot see a cable.
                cordWrap.CablesDressed();
                Log("[CORDWRAP] Reset - cables reported dressed\n");
                slog.Mount("Cord wrap reset");
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
            } else if (rc.cmd == "load_profile") {
                // Phase 57 (review G3): this block previously sat INSIDE the
                // jog handler — load_profile did nothing, and every jog also
                // tried to load a profile named "N"/"S"/"E"/"W".
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
                                 mount, slog, profileHooks);
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

    // ── Phase 102: the saved panel state, re-asserted ─────────
    //
    // Second and final pass, after every piece of startup wiring has
    // run and immediately before the render loop. The first pass has
    // to precede Place for the maximise ordering, but startup carries
    // on for thousands of lines afterwards and anything in there that
    // shows a window beats it silently. Exactly that happened: a loop
    // in the toolbar wiring force-showed the GoTo, help and AI chat
    // windows, so those three came back open however they were saved,
    // and the restore looked broken to the owner with no hint in the
    // log as to why.
    //
    // That loop now defers to the saved state, so this pass should be
    // a no-op -- and the log says so either way, which is the point.
    // It costs one Show or Hide per panel on a state that already
    // matches, and it means the next force-show somebody adds during
    // startup cannot quietly undo the restore.
    applySavedPanelState("final");

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
        // Virtual on IMount: NexStarMount feeds its watchdog, the ASCOM and
        // emulator backends no-op. (Previously a static_cast to NexStarMount
        // here corrupted ASCOMMount instances on the CPWI path.)
        if (mount && mount->IsConnected())
            mount->Heartbeat();

        FrameSlot* frame = ring.GetLatestFrame();
        const auto& stats = capture.Stats();

        // Phases 27 & 28: feed a master build from the live stream.
        // Before the gate and before SubmitFrame, because a master is
        // built from what the sensor reports and must not be filtered
        // by a quality gate meant for pictures of planets.
        if (frame && frame->data && calib.Building()) {
            const bool done = calib.AddFrame(frame->data,
                                             (int)frame->width,
                                             (int)frame->height,
                                             frame->bitDepth);
            if (done) {
                // Read which master this was BEFORE Finish clears the
                // build flag, or the wrong file gets written.
                const bool wasDark = calib.BuildingDark();
                if (calib.Finish()) {
                    calib.SaveMaster(wasDark, calPath(wasDark));
                    Log("[CAL] Master %s built from %d frames and saved\n",
                        wasDark ? "dark" : "flat",
                        wasDark ? calib.DarkMeta().averaged
                                : calib.FlatMeta().averaged);
                    notifBar.NotifyInfo(wasDark
                        ? L"Master dark built - uncover the telescope"
                        : L"Master flat built");
                } else {
                    notifBar.NotifyWarn(L"Master build failed");
                }
                calStatus();
            } else if ((calib.BuildProgress() % 8) == 0) {
                calStatus();   // progress, not every frame
            }
        }

        // Submit to capture manager — Phase 10 quality gate
        // If GATE is enabled: only record frames meeting the seeing threshold.
        // The gate is checked against the rolling seeing score (updated ~2Hz).
        if (capturer.IsRecording()) {
            bool gateEnabled   = controls.GateEnabled();
            float gateThresh   = controls.GateThreshold();
            bool seenOk        = !gateEnabled
                                 || !lastAnalysis.valid
                                 || lastAnalysis.seeingScore >= gateThresh;

            // Publish the policy; the CAPTURE thread does the
            // submitting now, once per frame the camera delivers.
            //
            // This loop used to call SubmitFrame() with the frame it had
            // just taken from GetLatestFrame() -- one per pass. That
            // made the recorded frame rate a property of the UI:
            // Window::Run() ends in Sleep(1), 16 ms at default Windows
            // timer granularity, so the loop tops out near 62 passes a
            // second before any rendering work at all. Everything the
            // camera produced in between was overwritten in the ring and
            // lost without a trace.
            //
            // Nothing is lost by publishing instead of deciding per
            // frame: the gate keys off a seeing score that updates at
            // about 2 Hz, and the centroid is only a hint telling the
            // writer where to measure sharpness.
            capturer.SetGateOpen(seenOk);
            capturer.SetFrameHints(
                (lastAnalysis.valid && lastAnalysis.centroidValid)
                    ? (uint32_t)lastAnalysis.centroidX : 0u,
                (lastAnalysis.valid && lastAnalysis.centroidValid)
                    ? (uint32_t)lastAnalysis.centroidY : 0u,
                lastAnalysis.valid ? lastAnalysis.sharpnessPct : 0.0f);

            if (gateEnabled) controls.SetGateActive(!seenOk);

            // The capture thread raises this and stops there, because
            // StopRecording() joins the writer thread and a buffer sized
            // to a whole run can take seconds to drain -- far too long
            // to stall frame acquisition for.
            //
            // Phase 98: through endRecording, so the limit path restores
            // USB polling like every other stop. It did not, and the EAF
            // and mount stayed throttled afterwards.
            if (capturer.LimitReached()) {
                const bool derot = capturer.DerotationStopped();
                endRecording(true, derot ? "derotation window"
                                         : "frame/time limit reached");
                if (derot)
                    notifBar.NotifyInfo(L"Run stopped at the derotation window");
            } else if (derotWindowSec > 0.0 && !derotWarned &&
                       capturer.ElapsedSec() >= derotWindowSec) {
                derotWarned = true;
                Log("[DEROT] window passed at %.0f s\n", capturer.ElapsedSec());
                notifBar.NotifyWarn(L"Past the derotation window - rotation is "
                                    L"blurring this run, unless you derotate it");
            }
        }

        // Submit to frame analyzer (non-blocking, always)
        if (frame)
            analyzer.Submit(frame);

        // B3: dashboard preview. The timer is checked first so the
        // subscriber count (a lock) is asked twice a second, not per pass.
        // RGB24 frames are skipped: DecimateFrame reads one sample a pixel.
        if (frame && frame->data && frame->imgType != ASI_IMG_RGB24 &&
            std::chrono::steady_clock::now() - lastPreview >= kPreviewInterval) {
            lastPreview = std::chrono::steady_clock::now();
            if (wsServer.PreviewSubscriberCount() > 0) {
                raduk::net::PreviewImage pimg;
                const bool bayer = frame->imgType == ASI_IMG_RAW8 ||
                                   frame->imgType == ASI_IMG_RAW16;
                if (raduk::net::DecimateFrame(frame->data, (int)frame->width,
                                              (int)frame->height, frame->bitDepth,
                                              bayer, pimg))
                    previewStreamer.Submit(std::move(pimg));
            }
        }

        // ── ADC alignment (Phase 35) ──────────────────────────
        //
        // Here rather than in the readout block below, because the
        // readout runs on a slower tick and GetLatestFrame() has usually
        // been consumed by then -- measured against it, the window sat
        // on "No frame - start the camera" with the camera streaming.
        //
        // Only while the window is up: a full pass over both Bayer
        // planes per frame is not worth spending on a window nobody is
        // looking at.
        if (adcAlignWindow.IsVisible() && frame && frame->data &&
            frame->width > 16 && frame->height > 16) {
            adcAlign.Push(raduk::core::ADCAlignment::MeasureBayer(
                frame->data, (int)frame->width, (int)frame->height,
                (int)frame->bitDepth,
                raduk::core::RigConfig::Get().plateScaleArcsecPx));
            adcHaveFrame = true;
        }

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
        preview.SetSkyMapVisible(skyMap.IsVisible() || skyMap2.IsVisible());

        // Grab latest analysis result — push to ControlPanel for GDI histogram
        bool freshAnalysis = false;
        if (analyzer.HasResult()) {
            lastAnalysis = analyzer.GetResult();
            // Phase 95: the star focus figure to the HUD. Pushed every
            // result, including the invalid ones, so a switch from a
            // star to a planet CLEARS it rather than leaving the last
            // star's HFD on screen reading as defocus.
            {
                raduk::gui::sharpness::StarFigure sf;
                sf.valid       = lastAnalysis.starValid;
                sf.hfdPx       = lastAnalysis.hfdPx;
                sf.fwhmPx      = lastAnalysis.fwhmPx;
                sf.fwhmValid   = lastAnalysis.fwhmValid;
                sf.saturated   = lastAnalysis.starSaturated;
                sf.arcsecPerPx = raduk::core::RigConfig::Get().plateScaleArcsecPx;
                preview.SetStarFigure(sf);
            }
            freshAnalysis = true;
        }

        if (lastAnalysis.valid)
            controls.UpdateAnalysis(lastAnalysis);

        // Phase 21: feed clip percentage to preview for clipping overlay
        if (lastAnalysis.valid)
            preview.SetClipPct(lastAnalysis.clipPct);

        // Phase 25a: push sharpness sample to quality bar graph
        if (lastAnalysis.valid)
            preview.PushQualitySample(lastAnalysis.sharpnessPct);

        // Phase 75: camera source for the motion plot. The planet centroid
        // resolves a few arcseconds of mount movement that the 500ms alt/az
        // poll cannot see at all, which is the whole point at FINE rate.
        // Only on a FRESH analysis - re-pushing the same centroid every
        // render tick would fake a stationary trail.
        if (freshAnalysis && lastAnalysis.valid && lastAnalysis.centroidValid) {
            mountCtrl.FeedCameraMotion(
                lastAnalysis.centroidX + roiFollow.OffsetX(),
                lastAnalysis.centroidY + roiFollow.OffsetY(),
                raduk::core::RigConfig::Get().plateScaleArcsecPx,
                std::chrono::duration<double>(
                    std::chrono::steady_clock::now()
                        .time_since_epoch()).count());
        }

        // Phase 23: sync manual offset slider → preview each frame
        if (preview.RGBAlignEnabled())
            preview.SetRGBAlignManual(controls.GetRGBManual());

        // Phase 9: feed sharpness to autofocus scan if running
        // Submit ONLY on a fresh analyzer result — the same fix the tracker
        // got below, which autofocus never received. Submitting every render
        // tick (~60Hz) pushed the SAME stale sharpnessPct repeatedly, and
        // AutofocusRoutine counts pushes, not distinct measurements: its
        // framesPerStep=8 was satisfied in ~8 ticks (~130ms) by 8 copies of
        // one analysis. So each V-curve point carried full single-frame
        // seeing noise instead of an 8-frame average — exactly why the fit
        // is unreliable in poor seeing.
        if (freshAnalysis && lastAnalysis.valid && autofocus.IsRunning())
            autofocus.SubmitSharpness(lastAnalysis.sharpnessPct,
                                      lastAnalysis.centroidValid,
                                      (int)lastAnalysis.centroidPeak);

        // Phase 107: following runs only while LOCKED, on a crop.
        // Calibration measures how the image moves, so a moving window
        // would corrupt it, and a full-sensor frame has nowhere to go.
        {
            const bool want = raduk::core::RigConfig::Get().roiFollow &&
                              !followGaveUp && cam.IsOpen() &&
                              tracker.Stage() == raduk::core::AcquisitionStage::Locked &&
                              (roiWidth < cam.MaxWidth() || roiHeight < cam.MaxHeight());
            if (roiFollow.Armed()) {
                roiStartX = roiFollow.OriginX();
                roiStartY = roiFollow.OriginY();
            }
            if (want && !roiFollow.Armed()) {
                roiFollow.Arm(roiStartX, roiStartY, roiWidth, roiHeight,
                              cam.MaxWidth(), cam.MaxHeight());
                Log("[FOLLOW] armed at (%d,%d) on %dx%d\n",
                    roiStartX, roiStartY, roiWidth, roiHeight);
            } else if (!want && roiFollow.Armed()) {
                roiFollow.Disarm();
                const auto s = roiFollow.Snapshot();
                Log("[FOLLOW] stopped: %d moves, %d landed, slowest %d frames\n",
                    s.Moves(), s.Landed(), s.MaxLandingFrames());
            }
            if (roiFollow.GaveUp() && !followGaveUp) {
                followGaveUp = true;   // want goes false until the button
                const auto s = roiFollow.Snapshot();
                Log("[FOLLOW] gave up: the camera did not apply %d moves in a row\n",
                    s.Moves() - s.Landed());
                notifBar.NotifyWarn(L"ROI following stopped - the camera is not "
                                    L"applying moves during capture");
            }
        }

        // Feed centroid to tracker for all active stages
        // Stage 1 (Searching): SubmitCentroid updates stable counter, no corrections
        // Stage 2 (Centering): updates dwell counter + fires corrections
        // Stage 3 (Locked):    corrections only
        {
            auto stage = tracker.Stage();
            // Submit ONLY on a fresh analyzer result. Re-submitting the stale
            // lastAnalysis every render tick (the previous behavior) refreshed
            // the tracker's centroid-loss timestamp with old data — so if the
            // camera stalled, the 2s loss cutoff never fired and corrections
            // continued against a frozen centroid. It also made the stable/
            // dwell counters count render ticks (~60Hz) instead of analyses.
            // Phase 70: report the stage and what is holding it up, every
            // tick. The state machine used to announce itself only when a
            // button was pressed, so a user watching nothing happen had no
            // way to tell which of five stages they were in or why it was
            // not advancing.
            {
                std::wstring acq;
                switch (stage) {
                case raduk::core::AcquisitionStage::Off:
                    break;                       // nothing to say when idle
                case raduk::core::AcquisitionStage::Searching: {
                    std::string why = tracker.CentroidBlockReason();
                    if (!why.empty())
                        acq = L"SEARCHING - " + std::wstring(why.begin(), why.end());
                    else
                        acq = L"SEARCHING - locking on";
                    break;
                }
                case raduk::core::AcquisitionStage::Calibrating:
                    acq = L"CALIBRATING - measuring pulse response";
                    break;
                case raduk::core::AcquisitionStage::Centering:
                    acq = L"CENTRING - pulling the planet to the middle";
                    break;
                case raduk::core::AcquisitionStage::Locked:
                    acq = L"LOCKED - guiding";
                    break;
                }
                controls.SetAcquisitionStatus(acq);
            }

            if (stage != raduk::core::AcquisitionStage::Off && freshAnalysis) {
                if (lastAnalysis.valid && lastAnalysis.centroidValid)
                    // Plus the window's offset, so the tracker sees the
                    // true error on the sensor and the mount keeps
                    // walking the window back towards home.
                    tracker.SubmitCentroid(lastAnalysis.centroidX + roiFollow.OffsetX(),
                                           lastAnalysis.centroidY + roiFollow.OffsetY(),
                                           lastAnalysis.width,
                                           lastAnalysis.height);
                else
                    // Pass the reason through: a frame under the centroid
                    // gate is a different problem from an empty one.
                    tracker.SubmitCentroidLost(lastAnalysis.centroidTooFaint,
                                               (int)lastAnalysis.centroidPeak);

                // Stages advance only on explicit user button press ([LOCK] and [ROI]).
                // Auto-advance removed — user must confirm each stage transition.
                // toolbar.UpdateStates() below lights up the buttons when ready.
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
                skyMap.IsVisible() || skyMap2.IsVisible(),
                gotoPanel.IsVisible(),
                preview.ReticleVisible(),
                preview.ClipVisible(),
                helpWindow.IsVisible(),
                aiChat.IsVisible(),
                preview.GhostEnabled());
            // Phase 46: and which devices are live. Same reasoning as
            // UpdateFormat below -- it returns immediately unless the
            // state changed, and calling it every tick means a device
            // that drops on its own (a USB unplug, not a button press)
            // still shows up on the button.
            toolbar.UpdateHardware(currentLinks());
            // Cheap: returns immediately unless the format actually
            // changed. Called every tick so a window resize, which
            // rebuilds the buttons with plain labels, gets its active
            // markers back.
            toolbar.UpdateFormat(roiWidth, roiHeight, roiBits);

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
                // Phase 86: the real target and the real profile. Both of
                // these were the capture file prefix, so an assistant
                // asked what the rig was pointing at answered with a
                // filename -- and after Phase 85 the active profile is
                // authoritative, so there is no reason to guess.
                {
                    const std::string& tgt = gotoPanel.CurrentTarget();
                    aiState.targetName = tgt.empty() ? controls.GetFilePrefix()
                                                     : tgt;
                    const TargetProfile* ap = profiles.Active();
                    aiState.profileName = ap ? ap->name : std::string("none");
                    if (ap) {
                        aiState.limitType  = ap->limitType;
                        aiState.limitValue = ap->limitValue;
                    }
                }
                // Capture format, so "am I still cropped" is answerable.
                aiState.roiWidth     = roiWidth;
                aiState.roiHeight    = roiHeight;
                aiState.bitDepth     = roiBits;
                aiState.sensorWidth  = cam.MaxWidth();
                aiState.sensorHeight = cam.MaxHeight();
                // Phase 87: display modes. Narrowed to the non-default
                // ones at the far end, so only send what is actually on.
                {
                    using namespace raduk::gui;
                    aiState.debayerRaw =
                        (preview.GetDebayerMode() == DebayerMode::Raw);
                    aiState.nightVision = preview.NightVisionOn();
                    aiState.framingShot = framing.active.load();
                    auto narrow = [](const wchar_t* w) {
                        std::string out;
                        for (; w && *w; ++w) out += (char)*w;  // ASCII labels
                        return out;
                    };
                    if (preview.GetChannelMode() !=
                            PreviewRenderer::ChannelMode::All)
                        aiState.channelMode = narrow(preview.ChannelLabel());
                    if (preview.GetDisplayFX() != DisplayFX::Normal)
                        aiState.displayFX = narrow(preview.DisplayFXLabel());
                    if (preview.ReticleVisible())
                        aiState.reticle = narrow(preview.ReticleLabel());
                }
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
                    aiState.gain       = (int)cam.GetControlValue(ControlId::Gain);
                    aiState.exposureMs = (float)(cam.GetControlValue(ControlId::ExposureUs) / 1000.0);
                }
                aiState.fps        = (float)stats.fps.load();
                aiState.seeingScore= lastAnalysis.valid ? lastAnalysis.seeingScore : 0.0f;
                // Focuser
                if (focuser.IsOpen()) {
                    aiState.eafPos   = focuser.GetPosition();
                    aiState.eafTempC = focuser.GetTemperature();
                }
                // Weather, measured, from Leoben -- or nothing, and the
                // context then says it is unknown. No coordinates go in:
                // the site-precision rule covers this line too.
                {
                    const auto ls = leoben.Get();
                    const int64_t nowU = (int64_t)std::time(nullptr);
                    aiState.weather.clear();
                    if (raduk::net::LeobenFresh(ls.last, nowU)) {
                        const auto& c = ls.last;
                        char wb[320];
                        snprintf(wb, sizeof(wb),
                                 "%s, imaging score %d/100; cloud %.0f%%, air %.1f C, "
                                 "humidity %.0f%%, wind %.1f m/s gusting %.1f, "
                                 "dew point %.1f C (dew risk %s), pressure %.0f hPa at the site; "
                                 "reading %d min old",
                                 c.goNoGo.empty() ? "no verdict" : c.goNoGo.c_str(),
                                 c.imagingScore, c.cloudPct, c.TempC(), c.humidityPct,
                                 c.WindMs(), c.GustMs(), c.DewPointC(),
                                 c.dewRisk.empty() ? "unknown" : c.dewRisk.c_str(),
                                 raduk::net::StationPressureHpa(c.PressureMslHpa(),
                                     RigConfig::Get().siteElevationM, c.TempC()),
                                 (int)((nowU - c.fetchedUnix) / 60));
                        aiState.weather = wb;
                    }
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

                // Phase 12b: a compact digest, deliberately NOT the full
                // context the chat panel gets -- this runs unattended and
                // repeatedly, and shipping the whole help file every few
                // minutes to say "your target is low" would be absurd.
                //
                // Rounded on purpose. The loop only asks when the digest
                // CHANGES, so raw altitude or FPS would differ on every
                // tick and it would never stop talking. Rounding is what
                // turns "changed" into "changed meaningfully".
                if (advisory.Enabled()) {
                    // The concerns are decided HERE, in code, because they
                    // are deterministic and can be checked. The model is
                    // only asked to phrase one. The first version let it
                    // judge, and a 3B model confidently advised lowering
                    // gain to improve dispersion -- which is nonsense, and
                    // indistinguishable from real advice to the beginner
                    // this is for. With no concern found, it is not asked.
                    std::string concerns;
                    auto add = [&](const std::string& c) {
                        if (!concerns.empty()) concerns += "; ";
                        concerns += c;
                    };
                    char cb[256];
                    if (aiState.mountAlt > 0.5f && aiState.mountAlt < 30.0f) {
                        snprintf(cb, sizeof(cb),
                            "target is %.0f deg above the horizon - atmospheric "
                            "dispersion and poor seeing both worsen fast below 30",
                            aiState.mountAlt);
                        add(cb);
                    }
                    if (aiState.framesDropped > 100) {
                        snprintf(cb, sizeof(cb),
                            "%u frames have been dropped - the USB bandwidth "
                            "slider in the CAMERA section is what to lower",
                            (unsigned)aiState.framesDropped);
                        add(cb);
                    }
                    if (mount && mount->IsConnected() && !aiState.tracking)
                        add("the mount is not tracking - the target will drift "
                            "out of frame");
                    if (aiState.roiWidth > 0 && aiState.sensorWidth > 0 &&
                        aiState.roiWidth < aiState.sensorWidth / 4)
                        add("the sensor is cropped small - press FULL on the top "
                            "bar to get the whole field back");

                    if (!concerns.empty()) {
                        char dg[1024];
                        snprintf(dg, sizeof(dg),
                            "FACTS: target=%s alt=%d gain=%d exp=%.0fms "
                            "seeing=%d/10 format=%dx%d filter=%s\n"
                            "CONCERNS: %s",
                            aiState.targetName.empty() ? "none"
                                                       : aiState.targetName.c_str(),
                            (int)(aiState.mountAlt / 5.0f) * 5,
                            aiState.gain, (double)aiState.exposureMs,
                            (int)aiState.seeingScore,
                            aiState.roiWidth, aiState.roiHeight,
                            raduk::core::RigConfig::Get().filterName.c_str(),
                            concerns.c_str());
                        advisory.Tick(dg, aiState.recording);
                    }
                }
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

        // ── Phase 93: where the render pass actually goes ─────
        //
        // The full sensor previews at under 2 fps against 9.7 at
        // 1920x1080, and four full-frame passes are plausible causes.
        // Guessing between them is what produced the wrong diagnosis
        // for the emulator's frame rate, so this says which it is.
        //
        // Accumulated and logged every 120 passes, which at the rate
        // being investigated is about a minute. steady_clock::now() is
        // tens of nanoseconds; against passes that take hundreds of
        // milliseconds it is not worth gating behind a flag.
        using PClock = std::chrono::steady_clock;
        //
        // Off unless asked for. It settled P4.4 and it is the first
        // thing to reach for when a rate looks wrong, but a line in
        // raduk_log.txt every ten seconds on every night that nobody
        // is diagnosing anything is noise in the file you go to when
        // something HAS gone wrong. RADUK_PERF=1 turns it on.
        static const bool kPerfLog = [] {
            char b[8] = {};
            return GetEnvironmentVariableA("RADUK_PERF", b, 8) > 0
                   && b[0] != '0';
        }();
        static double tRender = 0, tPanels = 0, tEnd = 0, tLoop = 0;
        static int    tPasses = 0;
        static auto   tLast   = PClock::now();
        const auto    tPassStart = PClock::now();
        auto Elapsed = [](PClock::time_point a, PClock::time_point b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };

        preview.RenderFrame(
            frame, 0, previewW, windowH, stDIPs,
            stats.fps.load(),
            stats.framesCaptured.load(),
            stats.droppedFrames.load());
        const auto tAfterRender = PClock::now();

        // Unconditional, and told separately whether the analysis is
        // valid: the temperature half needs no frame at all, and the
        // histogram shows its channels greyed rather than disappearing.
        preview.DrawSidePanels(lastAnalysis, lastAnalysis.valid,
                               0, previewW, windowH, stDIPs);
        histWindow.Update(lastAnalysis);
        if (lastAnalysis.valid) sharpWindow.Push(lastAnalysis.sharpnessPct);

        if (!window.IsFullscreen())
            statusBar.Draw(previewW, windowH);

        const auto tAfterPanels = PClock::now();
        preview.EndFrame();
        const auto tAfterEnd = PClock::now();
        ring.ReleaseRead(frame);

        if (kPerfLog) {
        tRender += Elapsed(tPassStart,    tAfterRender);
        tPanels += Elapsed(tAfterRender,  tAfterPanels);
        tEnd    += Elapsed(tAfterPanels,  tAfterEnd);
        if (++tPasses >= 600) {
            const double wall = Elapsed(tLast, tAfterEnd);
            tLoop = wall / tPasses;
            Log("[PERF] %dx%d  pass %.1f ms (%.1f/s)  render %.1f  panels %.1f  "
                "present %.1f  other %.1f  | capture %.1f fps  dropped %llu\n",
                roiWidth, roiHeight, tLoop, tLoop > 0 ? 1000.0 / tLoop : 0.0,
                tRender / tPasses, tPanels / tPasses, tEnd / tPasses,
                tLoop - (tRender + tPanels + tEnd) / tPasses,
                stats.fps.load(),
                (unsigned long long)stats.droppedFrames.load());
#ifdef RADUK_EMULATOR
            // What the synthetic camera is really producing, beside the
            // rate we collect at. Without both numbers "the camera is
            // slow" and "we are slow at collecting" look the same.
            Log("[PERF]   emulator generating %.1f fps  (asked %.0f, "
                "exposure floor %.1f ms)\n",
                cam.GeneratedFps(), cam.TargetFps(),
                cam.ExposureMsForPacing());
            Log("[PERF]   emulator render %.1f ms/frame\n", cam.RenderMs());
#endif
            tRender = tPanels = tEnd = 0.0;
            tPasses = 0;
            tLast   = PClock::now();
        }
        }

        // Phase 56 (review G2): rubber-band auto-ROI is applied only here,
        // after the frame is back in the ring — the callback reallocates
        // the ring, which was a use-after-free when fired mid-render.
        preview.FireDeferredROIApply();

        // ── Readouts every 500ms, EAF/Mount gated by USB manager ──
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastReadout).count() > 500) {

            controls.UpdateReadouts();
            mountCtrl.UpdateReadouts();

            // Phase 12 — Auto-Exposure: feed analysis, tick, apply/display
            if (ae.IsEnabled() && lastAnalysis.valid && cam.IsOpen()) {
                long curGain = cam.GetControlValue(ControlId::Gain);
                long curExpUs = cam.GetControlValue(ControlId::ExposureUs);
                double curExpMs = curExpUs / 1000.0;

                ae.Feed(lastAnalysis.highlightDN,
                        lastAnalysis.meanBrightness,
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

            // GoTo panel — refresh planet positions and slew status.
            // Slew-completion detection must run even while the panel is
            // hidden: the sky-map-click GoTo flow (panel hidden by default)
            // relies on it to fire GoToComplete and arm auto-acquisition.
            if (gotoPanel.IsVisible())
                gotoPanel.Tick();
            else
                gotoPanel.TickSlewStatus();

            // Sky maps — update mount crosshair and redraw
            for (auto* m : kMaps)
                if (m->IsVisible()) m->Tick();

            // Centroid tracker — dispatch callbacks on main thread
            if (tracker.Stage() != raduk::core::AcquisitionStage::Off)
                tracker.Tick();

            // Camera — always update (never blocks)
            statusBar.SetCameraStats(
                stats.fps.load(),
                (int)stats.droppedFrames.load(),
                cam.IsOpen() ? cam.GetTemperatureC() : 0.0f,
                roiWidth, roiHeight, 8);

            // Phase 58: surface camera faults (USB drop / wedge) once per
            // transition — CaptureThread keeps retrying recovery itself.
            {
                static bool lastCamFault = false;
                bool camFault = stats.cameraFault.load();
                if (camFault != lastCamFault) {
                    lastCamFault = camFault;
                    if (camFault) {
                        Log("[CAMERA] FAULT — no frames (USB drop?); auto-recovery running\n");
                        slog.Camera("FAULT: frames stopped — auto-recovery running");
                    } else {
                        Log("[CAMERA] Recovered — frames flowing again\n");
                        slog.Camera("Recovered after fault (%u restarts)",
                                    stats.restarts.load());
                    }
                }
            }

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
                            // Phase 69: "UNKNOWN" when the mode could not be
                            // read. Showing OFF for an unreadable mount is a
                            // claim about the hardware we cannot support, and
                            // it sent a whole session chasing a tracking bug
                            // that was really a comms failure.
                            mount->TrackingModeKnown()
                                ? std::string(IMount::TrackingModeName(mount->GetTrackingMode()))
                                : std::string("UNKNOWN"),
                            true);
                    }
                    // GetAltAz is a serial round-trip — run it on a dedicated thread
                    // so it never stalls the render loop. Cached result read back here.
                    static std::atomic<float> cachedAlt{-1.0f};
                    // Phase 67: azimuth was fetched here and thrown away.
                    // It is what cord wrap is measured from, and without
                    // it the Az readout could never be anything but "--".
                    static std::atomic<float> cachedAz{-1.0f};
                    static std::atomic<bool>  altPending{false};
                    if (!altPending.load()) {
                        altPending.store(true);
                        std::thread([&]() {
                            double alt = 0, az = 0;
                            if (mount && mount->IsConnected() && mount->GetAltAz(alt, az)) {
                                cachedAlt.store((float)alt);
                                cachedAz.store((float)az);
                            }
                            altPending.store(false);
                        }).detach();
                    }
                    float alt = cachedAlt.load();
                    float az  = cachedAz.load();
                    // Phase 75: mount source for the motion plot. This is
                    // the 500ms poll (2s while capturing), so it is the
                    // coarse fallback - MotionMonitor ignores it whenever
                    // fresh centroid samples are arriving.
                    if (az >= 0.0f)
                        mountCtrl.FeedMountMotion(
                            alt, az,
                            std::chrono::duration<double>(
                                std::chrono::steady_clock::now()
                                    .time_since_epoch()).count());
                    // Phase 47b: the measured half of the rose. Set
                    // from the guide calibration whenever there is
                    // one, cleared when there is not, so a stale
                    // arrow cannot outlive the measurement it came
                    // from. Four floats, so it is cheaper to resend
                    // than to track whether it changed.
                    if (tracker.HasCalibration()) {
                        const auto cal = tracker.Calibration();
                        preview.SetJogResponse(cal.exPerMs, cal.eyPerMs,
                                               cal.nxPerMs, cal.nyPerMs);
                    } else {
                        preview.ClearJogResponse();
                    }

                    // Phase 47b: how fast this mount is rotating the
                    // field right now, rather than the flat default.
                    //
                    // 15.04 deg/hr * cos(lat) * cos(az) / cos(alt), the
                    // standard alt-az field rotation rate. Both inputs
                    // are already polled for the cord wrap and the
                    // airmass, so this costs nothing extra.
                    //
                    // fabs makes the azimuth convention moot: the rate
                    // peaks near both north and south and the sign only
                    // says which way the field turns, which a staleness
                    // budget does not care about.
                    //
                    // Near the zenith cos(alt) collapses and the rate
                    // runs away, which is physically true -- an alt-az
                    // mount really cannot track through the zenith --
                    // so it is capped rather than allowed to make the
                    // readout expire instantly on a rounding error.
                    if (alt >= 0.0f && az >= 0.0f) {
                        const double D2R = 3.14159265358979 / 180.0;
                        const double lat =
                            raduk::core::RigConfig::Get().siteLatDeg;
                        const double cAlt = std::cos(alt * D2R);
                        double rate = 90.0;
                        if (cAlt > 0.02)
                            rate = std::fabs(15.041 * std::cos(lat * D2R)
                                 * std::cos(az * D2R) / cAlt);
                        if (rate > 90.0) rate = 90.0;
                        preview.SetFieldRotationRate(rate);
                    }

                    if (az >= 0.0f) {
                        cordWrap.Sample(az);
                        controls.SetMountAltAz(alt, az, true,
                                               cordWrap.CumulativeDeg(),
                                               cordWrap.WorthShowing(),
                                               cordWrap.OverLimit());
                        // Warn once per crossing rather than every poll.
                        static bool warned = false;
                        if (cordWrap.OverLimit() && !warned) {
                            warned = true;
                            Log("[CORDWRAP] %+.0f deg of travel since the cables "
                                "were dressed (limit %.0f) - check them before "
                                "slewing further\n",
                                cordWrap.CumulativeDeg(), cordWrap.Limit());
                            slog.Mount("Cord wrap %+.0f deg - check cables",
                                       cordWrap.CumulativeDeg());
                            notifBar.NotifyWarn(L"Cord wrap - check the cables "
                                                L"before slewing further");
                        } else if (!cordWrap.OverLimit()) {
                            warned = false;
                        }
                    }
                    // How much atmosphere the scope is looking through.
                    // Measured from the mount's own reports rather than
                    // ephemeris, so it is right even pointing somewhere
                    // unplanned.
                    //
                    // Fed on ANY valid reading, including a negative
                    // altitude. The ADC readout below is gated on being
                    // above the horizon because dispersion means nothing
                    // underneath it; "you are pointing below the horizon"
                    // is the single most useful thing this row can say,
                    // and gating it the same way would have hidden it.
                    // az < 0 is the sentinel for no reading yet.
                    if (az >= 0.0f) {
                        skyTracker.Push(alt, az,
                            std::chrono::duration<double>(
                                std::chrono::steady_clock::now()
                                    .time_since_epoch()).count());
                    }
                    if (alt >= 0.0f) {
                        preview.SetMountAltitude(alt);
                        auto adcResult = raduk::core::ADCAssistant::Compute(alt);
                        auto adcText   = raduk::core::ADCAssistant::SuggestionText(adcResult);
                        wchar_t adcBuf[64];
                        MultiByteToWideChar(CP_UTF8, 0, adcText.c_str(), -1, adcBuf, 64);
                        // Only when there is a corrector to act on it.
                        // Without one the figure is still true -- the
                        // atmosphere disperses regardless -- but it
                        // reads as something to go and adjust, and the
                        // owner asked for all of it to stay dark until
                        // an ADC is in the train.
                        controls.SetADCStatus(
                            raduk::core::RigConfig::Get().adcFitted
                                ? adcBuf : L"ADC: not fitted");
                        // Log the band with the figure. The same
                        // number means different things through
                        // different filters, and nothing else on
                        // screen says which one is fitted.
                        Log("[ADC] alt=%.1f  %.2f arcsec  band %.0f-%.0fnm (%s)\n",
                            alt, adcResult.separationArcsec,
                            adcResult.bandLowNm, adcResult.bandHighNm,
                            raduk::core::RigConfig::Get().filterName.c_str());
                    }
                }
            }

            // ── FC-style temperature chart sample ─────────────
            float camT = cam.IsOpen()     ? cam.GetTemperatureC()        : 0.0f;
            float focT = focuser.IsOpen() ? focuser.GetTemperature() : 0.0f;
            preview.PushTempSample(camT, focT);
            tempWindow.Push(camT, focT);

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

                // Let the capture manager size its write buffer against
                // the rate actually being delivered rather than a guess.
                if (fps > 1.0f) capturer.SetObservedFps(fps);

                // Which drive tonight lands on, so the status window can
                // mark that row. Taken from the live capture config
                // rather than rig.ini, because the output directory can
                // be changed from the Capture card mid-session.
                const std::string& outDir = capturer.Config().outputDir;
                char capDrive = 0;
                if (outDir.size() >= 2 && outDir[1] == ':')
                    capDrive = (char)toupper((unsigned char)outDir[0]);

                // The rest of the preview's LIVE line. Not fps or lost
                // again -- the status window already carries those on
                // its REC and BUF rows.
                raduk::gui::StatusWindow::Data sd;
                sd.fps          = fps;
                sd.framesLost   = lost;
                sd.bufferPct    = bufPct;
                sd.bufferFrames = bufFr;
                sd.bufferTotal  = bufTot;
                sd.diskUsedGB   = diskGB;
                sd.recording    = capturer.IsRecording();
                sd.captureDrive = capDrive;
                sd.roiWidth     = roiWidth;
                sd.roiHeight    = roiHeight;
                sd.bitDepth     = roiBits;
                sd.frames       = capturer.FramesRecorded();
                sd.streaming    = cam.IsCapturing();

                // ── Phase 98: the recording clock ──────────────
                //
                // Elapsed is the capturer's own. Remaining is -1 when
                // no limit is set, exact on a time limit, and
                // estimated from the rate ACHIEVED SO FAR on a frame
                // limit -- the nominal frame rate is not what a run
                // with drops is delivering, and a figure derived from
                // it would quietly lie by however many frames were
                // lost. Withheld for the first second, because a
                // prediction off half a second of run is noise with a
                // decimal point.
                {
                    const auto& ccfg = capturer.Config();
                    sd.elapsedSec        = capturer.ElapsedSec();
                    sd.remainingSec      = -1.0;
                    sd.remainingEstimated = false;
                    if (capturer.IsRecording()) {
                        if (ccfg.limitType == LimitType::Time &&
                            ccfg.limitTimeSec > 0.0) {
                            double left = ccfg.limitTimeSec - sd.elapsedSec;
                            sd.remainingSec = (left > 0.0) ? left : 0.0;
                        } else if (ccfg.limitType == LimitType::Frames &&
                                   ccfg.limitFrames > 0 &&
                                   sd.elapsedSec > 1.0 && sd.frames > 0) {
                            const double rate = sd.frames / sd.elapsedSec;
                            const double togo = (ccfg.limitFrames > sd.frames)
                                ? (double)(ccfg.limitFrames - sd.frames) : 0.0;
                            sd.remainingSec = (rate > 0.0) ? togo / rate : 0.0;
                            sd.remainingEstimated = true;
                        }
                    }
                }

                // Phase 98: and the panel's own copy. This call had NO
                // CALLERS AT ALL -- the function that formats the
                // "REC mm:ss" line and the frames-against-limit line
                // was written and never wired, and its first statement
                // bails on a label that was never created. That one
                // dead function is why there was no timer and why the
                // RECORD button stayed depressed after a run stopped
                // on its own limit.
                controls.UpdateCaptureStatus(
                    capturer.IsRecording(), capturer.FramesRecorded(),
                    capturer.FramesDropped(), capturer.ElapsedSec(),
                    capturer.BufferPending(), capturer.BufferTotal(),
                    capturer.Config().limitFrames,
                    capturer.Config().limitTimeSec,
                    capturer.Config().limitType == LimitType::Frames,
                    capturer.Config().outputDir);
                sd.sky          = skyTracker.Evaluate();

                // Leoben's weather: into the status window, and -- when
                // fresh -- into the ADC model as the real air above the
                // scope, reduced from sea-level to station pressure.
                {
                    const auto ls = leoben.Get();
                    const int64_t nowU = (int64_t)std::time(nullptr);
                    auto& w = sd.wx;
                    w.configured = ls.state != raduk::net::LeobenClient::State::Off;
                    w.fresh = raduk::net::LeobenFresh(ls.last, nowU);
                    if (w.fresh) {
                        const auto& c = ls.last;
                        w.ageMin   = (int)((nowU - c.fetchedUnix) / 60);
                        w.score    = c.imagingScore;
                        w.cloudPct = c.cloudPct;
                        w.windMph  = c.windMph;
                        w.tempF    = c.tempF;
                        strncpy_s(w.verdict, c.goNoGo.c_str(), _TRUNCATE);
                        strncpy_s(w.dewRisk, c.dewRisk.c_str(), _TRUNCATE);
                        const double stHpa = raduk::net::StationPressureHpa(
                            c.PressureMslHpa(), RigConfig::Get().siteElevationM, c.TempC());
                        raduk::core::ADCAssistant::SetLiveAir(stHpa, c.TempC() + 273.15,
                                                              c.fetchedUnix);
                        if (!leobenAirLogged) {
                            Log("[LEOBEN] ADC now uses live air: %.1f hPa at the site "
                                "(%.1f at sea level), %.1f C\n",
                                stHpa, c.PressureMslHpa(), c.TempC());
                            leobenAirLogged = true;
                        }
                    } else {
                        strncpy_s(w.state, raduk::net::LeobenClient::StateText(ls.state), _TRUNCATE);
                        if (ls.last.valid) strncpy_s(w.state, "last reading is stale", _TRUNCATE);
                        if (leobenAirLogged) {
                            Log("[LEOBEN] No fresh reading - ADC back on the standard atmosphere\n");
                            leobenAirLogged = false;
                        }
                    }
                }

                // Both places, as asked: the status window for the
                // desk, the mount panel's lamp for the eyepiece.
                mountCtrl.SetSkyQuality(sd.sky);

                // Into the SER sidecar too, while recording. Altitude
                // is the biggest thing separating two runs of the same
                // target on the same night.
                if (sd.sky.valid) {
                    const auto adc = raduk::core::ADCAssistant::Compute(
                                         sd.sky.altitudeDeg);
                    capturer.NoteSky(sd.sky.altitudeDeg, sd.sky.azimuthDeg,
                                     sd.sky.airmass, sd.sky.seeingFactor,
                                     adc.separationArcsec);
                }
                statusWindow.Update(sd);

                // ── Phase 89: the share view's caption ─────────
                // Only when it is up. Everything here is already read
                // on this tick for something else, so the whole cost
                // of the feature on an ordinary night is this branch.
                if (shareView.IsVisible()) {
                    raduk::gui::ShareView::Caption sc;
                    // What it is, not what the files are called. The
                    // capture prefix defaults to "Capture", and a
                    // screen showing a crowd a planet labelled
                    // "Capture" is worse than one labelled nothing.
                    // The active profile IS the target on this rig --
                    // the profiles are per-object -- so it is the right
                    // thing to fall back to.
                    const std::string& tgt = gotoPanel.CurrentTarget();
                    if (!tgt.empty()) {
                        sc.target = tgt;
                    } else if (const auto* ap = profiles.Active()) {
                        sc.target = ap->name;
                    }
                    sc.rig       = "Celestron 8SE  \xc2\xb7  ZWO ASI678MC";
                    sc.fps       = fps;
                    sc.recording = sd.recording;
                    if (cam.IsOpen()) {
                        sc.gain       = (int)cam.GetControlValue(ControlId::Gain);
                        sc.exposureMs = cam.GetControlValue(ControlId::ExposureUs) / 1000.0;
                    }
                    sc.haveAltitude = sd.sky.valid;
                    sc.altitudeDeg  = sd.sky.altitudeDeg;
                    shareView.SetCaption(sc);
                }

                // ── Phase 92: the object panel's content ───────
                //
                // Same target string the share view's caption uses,
                // resolved against the knowledge base. Only while the
                // panel is up: a Lookup is four small queries and
                // there is no reason to run them for a window nobody
                // is looking at.
                if (objectPanel.IsVisible()) {
                    raduk::gui::ObjectPanel::Content oc;
                    const std::string& tgt2 = gotoPanel.CurrentTarget();
                    if (!tgt2.empty()) oc.target = tgt2;
                    else if (const auto* ap2 = profiles.Active())
                        oc.target = ap2->name;

                    // Resolve only when the name has changed. The
                    // record does not change while the telescope is
                    // pointed at the same thing, and the live figures
                    // below are what actually move.
                    static std::string lastResolved;
                    static raduk::core::ObjectKnowledgeDB::ObjectRecord lastRec;
                    if (oc.target != lastResolved) {
                        lastResolved = oc.target;
                        lastRec = aiChat.GetKnowledgeDB().Lookup(oc.target);
                        Log("[OBJECT] %s -> %s\n", oc.target.c_str(),
                            lastRec.found ? lastRec.name.c_str()
                                          : "not in the catalogue");
                    }
                    oc.rec = lastRec;

                    oc.haveAltitude = sd.sky.valid;
                    oc.altitudeDeg  = sd.sky.altitudeDeg;
                    oc.airmass      = sd.sky.airmass;
                    oc.plateScaleArcsecPx =
                        raduk::core::RigConfig::Get().plateScaleArcsecPx;
                    oc.roiWidth  = roiWidth;
                    oc.roiHeight = roiHeight;
                    objectPanel.SetContent(oc);
                }
            }

            // ── ADC alignment (Phase 35) ───────────────────────
            //
            // Only while the window is up. It is a full-frame pass over
            // two channels per frame, and there is no reason to spend it
            // on a window nobody is looking at.
            if (adcAlignWindow.IsVisible()) {
                raduk::gui::ADCAlignWindow::Data ad;
                ad.frameAvailable = adcHaveFrame;
                if (ad.frameAvailable) {
                    const float plate =
                        raduk::core::RigConfig::Get().plateScaleArcsecPx;
                    ad.measured = adcAlign.Smoothed(plate);
                    ad.samples  = adcAlign.SampleCount();

                    // What the model says SHOULD be there, so the
                    // measurement can be read as a fraction corrected
                    // rather than as a bare number.
                    const double altDeg = preview.MountAltitude();
                    if (altDeg > 0.0) {
                        auto adc = raduk::core::ADCAssistant::Compute(altDeg);
                        ad.predictedArcsec = adc.separationArcsec;
                        ad.altitudeDeg     = (float)altDeg;
                    }
                }
                adcAlignWindow.Update(ad);
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
            // Session average uses sharpnessPct, NOT seeingScore.
            // seeingScore became relative to the best recent frame, so it
            // self-normalises: averaging it yields roughly the same figure
            // every session whatever the conditions, which is worse than
            // useless in a summary someone compares across nights.
            // sharpnessPct is the absolute measure and is what belongs
            // here.
            if (lastAnalysis.valid && lastAnalysis.sharpnessPct > 0.0f) {
                summarySeeingAcc += lastAnalysis.sharpnessPct;
                ++summarySeeingCount;
            }

            // ── APEX WebSocket telemetry broadcast (M12) ───────────
            if (wsServer.IsRunning() && wsServer.ClientCount() > 0) {
                telemetry.rc_running     = true;
                telemetry.fps            = stats.fps.load();
                telemetry.exposure_ms    = cam.IsOpen() ? cam.GetControlValue(ControlId::ExposureUs)/1000.0 : 8.0;
                telemetry.gain           = cam.IsOpen() ? (int)cam.GetControlValue(ControlId::Gain)    : 100;
                telemetry.gamma          = cam.IsOpen() ? (int)cam.GetControlValue(ControlId::Gamma)   : 50;
                telemetry.wb_red         = cam.IsOpen() ? (int)cam.GetControlValue(ControlId::WhiteBalanceRed)    : 52;
                telemetry.wb_blue        = cam.IsOpen() ? (int)cam.GetControlValue(ControlId::WhiteBalanceBlue)    : 47;
                telemetry.usb_bw         = cam.IsOpen() ? (int)cam.GetControlValue(ControlId::UsbBandwidth) : 80;
                telemetry.sensor_temp    = cam.IsOpen() ? cam.GetTemperatureC() : 0.0f;
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

            // Phase 75: log the motion plot only while something is
            // actually moving. Logging every 500ms regardless would
            // bury the rest of the session log, and a line here is only
            // interesting as after-the-fact evidence that a command did
            // something - which is the whole question this answers.
            // Review pass 2: this used to print a fourth figure,
            // net = raw - noise, which Sharpness.h documents at length as
            // meaningless here and deliberately never subtracts: the crop
            // is filled by the planet, so the "noise floor" is the
            // darkest decile of the DISC and net comes out negative. It
            // was printed beside the real numbers as though it were the
            // answer, which is an invitation to try subtracting it a
            // fourth time. The diagnostic itself stays, renamed to what
            // it actually measures.
            // The sharpness ceiling checks itself. SHARP_CEIL scales
            // every reading, the seeing score, the capture gate and the
            // autofocus curve, and it has been wrong twice -- once
            // calibrated on a noiseless emulator frame, which cost 500
            // steps of focus error the moment real noise appeared. The
            // clear-night checklist asks the user to watch this log for
            // pinning or crowding; this watches instead, over a long
            // enough run that a focus sweep cannot trigger it, and says
            // so once.
            if (lastAnalysis.valid) {
                static int  sharpHigh = 0, sharpLow = 0, sharpSeen = 0;
                static bool sharpSaid = false;
                const float spct = lastAnalysis.sharpnessPct;
                sharpSeen++;
                if (spct >= 95.0f) sharpHigh++;
                if (spct <= 10.0f) sharpLow++;
                if (!sharpSaid && sharpSeen >= 400) {
                    if (sharpHigh * 2 > sharpSeen) {
                        Log("[SHARP] CEILING TOO LOW: %d of %d readings at "
                            "or above 95%%. The curve is clipped and "
                            "autofocus cannot find a peak - raise "
                            "SHARP_CEIL in core/Sharpness.h\n",
                            sharpHigh, sharpSeen);
                        sharpSaid = true;
                    } else if (sharpLow * 10 > sharpSeen * 9) {
                        Log("[SHARP] CEILING TOO HIGH: %d of %d readings "
                            "at or below 10%%. Almost all resolution is "
                            "being thrown away - lower SHARP_CEIL in "
                            "core/Sharpness.h\n", sharpLow, sharpSeen);
                        sharpSaid = true;
                    }
                }
            }
            if (lastAnalysis.valid)
                Log("[SHARP] pct=%.2f  seeing=%.1f/10  raw=%.1f  "
                    "darkest-decile-gradient=%.1f (diagnostic, not subtracted)\n",
                    lastAnalysis.sharpnessPct, lastAnalysis.seeingScore,
                    lastAnalysis.sharpRaw, lastAnalysis.sharpNoise);

            if (mountCtrl.MotionSpeed() > 0.5f)
                Log("[MOTION] src=%s  %.2f arcsec/s  samples=%d\n",
                    mountCtrl.MotionSourceName(), mountCtrl.MotionSpeed(),
                    mountCtrl.MotionSamples());

            // Phase 74: quick-look stacker. Both numbers come from the same
            // metric over the same window, so STACK above LIVE means the
            // alignment and the quality gate are genuinely buying detail.
            // Before this, the stack was an unaligned mean of every frame
            // and read BELOW the live frame — it was smearing, not stacking.
            if (preview.QuickLookActive()) {
                if (preview.QuickLookLearning())
                    Log("[QL] live=%.3f  learning (%d admitted)\n",
                        preview.QuickLookLive(), preview.QuickLookAdmitted());
                else
                    Log("[QL] live=%.3f  stack=%.3f  depth=%d  admitted=%d%s%s\n",
                        preview.QuickLookLive(), preview.QuickLookStack(),
                        preview.QuickLookDepth(), preview.QuickLookAdmitted(),
                        preview.QuickLookReason()[0] ? "  skip: " : "",
                        preview.QuickLookReason());
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

    leoben.Stop();
    previewStreamer.Stop();   // its sink sends through wsServer
    wsServer.Stop();

    // Shut down Phase 50 AI before hardware teardown
    aiChat.Destroy();

    // Unhook the recorder from the capture thread FIRST.
    //
    // `capture` is declared before `capturer`, so on scope exit
    // `capturer` is destroyed first and ~CaptureThread() then joins a
    // thread whose sink still points at it. The explicit capture.Stop()
    // further down covers the normal path, but clearing the sink here
    // makes the ordering requirement independent of it.
    capture.SetFrameSink(nullptr);

    if (capturer.IsRecording()) capturer.StopRecording(false);  // app shutting down - do not auto-stack
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
