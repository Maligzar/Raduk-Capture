#pragma once
// ============================================================
// StatusWindow.h — Phase 42f
//
// HDD / BUF / FPS / LOST, in a window of its own.
//
// It used to be a section of the control panel, and it was the last
// thing left there that the panel painted ITSELF rather than building
// out of child controls. That turned out to be three bugs wearing one
// coat:
//
//   * It sat on a different plane. Child controls are clipped and
//     composited over the parent; the status strip was painted onto the
//     parent's own DC, underneath them. Every collapse toggle runs
//     RebuildControls, which destroys and recreates every child, and
//     the parent's already-painted strip stayed visible in the space
//     the children vacated. That was the "bleeding into everything
//     else".
//
//   * Its header was drawn by DrawSectionHeader in GDI while every
//     other header is a child control, so collapsed it did not line up
//     with the rest and its font was different.
//
//   * Its repaint invalidated the whole panel twice a second, and an
//     older invalidation aimed at the since-departed histogram used
//     unscrolled coordinates, so once the panel scrolled the clip
//     region and the drawing were in different places.
//
// None of that is worth repairing in place. The readout belongs beside
// the other telemetry, on the screen with the charts.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include "Theme.h"

#pragma comment(lib, "dwmapi.lib")

namespace raduk::gui {

class StatusWindow {
public:
    struct Data {
        float fps          = 0.0f;
        int   framesLost   = 0;
        float bufferPct    = 0.0f;
        int   bufferFrames = 0;
        int   bufferTotal  = 512;
        float diskUsedGB   = 0.0f;
        bool  recording    = false;
        char  captureDrive = 0;      // letter captures are written to, 0 if unknown

        // The rest of what the preview's LIVE line carries. FPS and
        // dropped frames are deliberately NOT here: this window already
        // shows them, on the REC and BUF rows, and a second copy would
        // be one more thing to keep in step for no gain.
        int      roiWidth  = 0;
        int      roiHeight = 0;
        int      bitDepth  = 8;
        uint64_t frames    = 0;      // recorded this run
        bool     streaming = false;  // camera actually delivering
    };

    // One fixed drive, as the scanner last saw it.
    struct DriveInfo {
        char               letter    = 0;
        unsigned long long totalBytes = 0;
        unsigned long long freeBytes  = 0;
    };

    bool Create(HWND parent, int x, int y, int w, int h) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;              // painted whole in WM_PAINT
        wc.lpszClassName = L"RadukStatus";
        RegisterClassExW(&wc);

        m_hwnd = CreateWindowExW(
            0, L"RadukStatus", L"Status",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX | WS_MAXIMIZEBOX,
            x, y, w, h, parent, nullptr, GetModuleHandle(nullptr), this);
        if (!m_hwnd) return false;

        BOOL dark = TRUE;
        DwmSetWindowAttribute(m_hwnd, 20, &dark, sizeof(dark));
        DwmSetWindowAttribute(m_hwnd, 19, &dark, sizeof(dark));
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        StartDriveScanner();
        return true;
    }

    void Destroy() {
        StopDriveScanner();
        if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    }

    ~StatusWindow() { StopDriveScanner(); }

    HWND Handle() const { return m_hwnd; }
    bool IsVisible() const { return m_hwnd && IsWindowVisible(m_hwnd); }
    void Show()   { if (m_hwnd) { ShowWindow(m_hwnd, SW_SHOW); SetForegroundWindow(m_hwnd); } }
    void Hide()   { if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE); }
    void Toggle() { if (!m_hwnd) return; IsVisible() ? Hide() : Show(); }

    void Update(const Data& d) {
        m_data = d;
        if (IsVisible()) InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void ToggleFullscreen() {
        if (!m_hwnd) return;
        if (!m_fullscreen) {
            m_savedPlacement.length = sizeof(WINDOWPLACEMENT);
            GetWindowPlacement(m_hwnd, &m_savedPlacement);
            MONITORINFO mi = { sizeof(mi) };
            if (!GetMonitorInfo(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi))
                return;
            SetWindowLongW(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(m_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            m_fullscreen = true;
        } else {
            SetWindowLongW(m_hwnd, GWL_STYLE,
                           WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX |
                           WS_MAXIMIZEBOX | WS_VISIBLE);
            SetWindowPlacement(m_hwnd, &m_savedPlacement);
            SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            m_fullscreen = false;
        }
        SetFocus(m_hwnd);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

private:
    static HFONT MakeFont(int px, bool bold, const char* face) {
        return CreateFontA(-px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                           FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
    }

    static void Fill(HDC hdc, int x, int y, int w, int h, COLORREF c) {
        RECT r = { x, y, x + w, y + h };
        HBRUSH b = Theme::Brush(c);
        FillRect(hdc, &r, b);
    }

    // A labelled progress bar. Same shape as the one the control panel
    // drew, scaled to whatever size this window is given.
    static void Bar(HDC hdc, int x, int y, int w, int h, float pct,
                    COLORREF fill, COLORREF bg, const char* label,
                    COLORREF labelClr, int fontPx) {
        Fill(hdc, x, y, w, h, bg);
        if (pct > 0.0f) {
            if (pct > 100.0f) pct = 100.0f;
            Fill(hdc, x, y, (int)(w * pct / 100.0f), h, fill);
        }
        if (label && *label) {
            HFONT f = MakeFont(fontPx, false, "Space Mono");
            HGDIOBJ old = SelectObject(hdc, f);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, labelClr);
            RECT tr = { x + 4, y, x + w - 4, y + h };
            DrawTextA(hdc, label, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, old);
            DeleteObject(f);
        }
    }

    // ── Drive scanning ──────────────────────────────────────────
    //
    // On a worker thread, not in Draw(), and not in Update() either.
    // Update() runs on the UI thread once per frame; GetDiskFreeSpaceEx
    // against a sleeping external USB drive can take seconds to return
    // while the platter spins up, and a stall there would stall the
    // preview and the capture loop with it. A drive that is slow to
    // answer here just shows stale numbers for a few seconds instead.
    void StartDriveScanner() {
        if (m_scanner.joinable()) return;
        m_scanStop = false;
        m_scanner = std::thread([this] {
            // Without this a removable drive with no media in it pops a
            // system "no disk" dialog, from a background thread, over a
            // telescope that is mid-exposure.
            SetThreadErrorMode(SEM_FAILCRITICALERRORS, nullptr);
            while (!m_scanStop) {
                ScanDrivesOnce();
                // Sleep in slices so closing the window does not wait
                // out the whole interval.
                for (int i = 0; i < 50 && !m_scanStop; i++)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }

    void StopDriveScanner() {
        m_scanStop = true;
        if (m_scanner.joinable()) m_scanner.join();
    }

    void ScanDrivesOnce() {
        std::vector<DriveInfo> found;
        const DWORD mask = GetLogicalDrives();
        for (int i = 0; i < 26; i++) {
            if (!(mask & (1u << i))) continue;
            wchar_t root[4] = { (wchar_t)(L'A' + i), L':', L'\\', 0 };

            // Fixed drives only. A network share can hang far longer
            // than a spin-up, a CD-ROM's "free space" means nothing,
            // and a RAM disk is not where anyone keeps a night's data.
            if (GetDriveTypeW(root) != DRIVE_FIXED) continue;

            ULARGE_INTEGER freeAvail{}, total{}, freeTotal{};
            if (!GetDiskFreeSpaceExW(root, &freeAvail, &total, &freeTotal)) continue;
            if (total.QuadPart == 0) continue;

            DriveInfo d;
            d.letter     = (char)('A' + i);
            d.totalBytes = total.QuadPart;
            // Free space available to THIS user, not the raw free total.
            // Under a disk quota they differ, and the number that
            // decides whether tonight's capture fits is this one.
            d.freeBytes  = freeAvail.QuadPart;
            found.push_back(d);
        }
        std::lock_guard<std::mutex> lk(m_drivesMx);
        m_drives.swap(found);
    }

    std::vector<DriveInfo> DrivesSnapshot() {
        std::lock_guard<std::mutex> lk(m_drivesMx);
        return m_drives;
    }

    void Draw(HDC hdc, const RECT& cr) {
        const int W = cr.right, H = cr.bottom;
        Fill(hdc, 0, 0, W, H, RGB(4, 8, 15));
        if (W < 120 || H < 50) return;

        // ── What the preview's LIVE line says ───────────────────
        //
        // The capture's identity -- state, geometry, format, frame
        // count -- rather than its rates. Put here so the one window
        // that gets a screen to itself can answer "what is this
        // recording" without anyone squinting at the status line across
        // the room.
        //
        // Only the parts this window did not already have. FPS is on
        // the REC row and dropped frames are on the BUF row as LOST;
        // repeating them here would be two places to read the same
        // number and two places for it to disagree.
        const int hdrH = (std::max)(16, (std::min)(34, H / 8));
        {
            const int hp = (std::max)(6, W / 80);
            char hdr[128];
            const char* state = !m_data.streaming ? "IDLE"
                              : m_data.recording  ? "REC" : "LIVE";
            if (m_data.roiWidth > 0)
                snprintf(hdr, sizeof(hdr), "%s   %dx%d   RAW%d",
                         state, m_data.roiWidth, m_data.roiHeight,
                         m_data.bitDepth);
            else
                snprintf(hdr, sizeof(hdr), "%s", state);

            const int hf = (std::max)(11, (std::min)(22, hdrH * 2 / 3));
            HFONT hfont = MakeFont(hf, true, "Barlow Condensed");
            HGDIOBJ old = SelectObject(hdc, hfont);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, m_data.recording ? Theme::NV(RGB(255, 60, 90))
                                               : Theme::NV(RGB(0, 229, 255)));
            RECT hr = { hp, 0, W / 2, hdrH };
            DrawTextA(hdc, hdr, -1, &hr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Frames, right-aligned. Shown only while there is a
            // recording to count -- a FRAMES 0 sitting there between
            // captures is noise.
            if (m_data.recording || m_data.frames > 0) {
                snprintf(hdr, sizeof(hdr), "FRAMES  %llu",
                         (unsigned long long)m_data.frames);
                SetTextColor(hdc, Theme::NV(RGB(200, 232, 240)));
                RECT fr = { W / 2, 0, W - hp, hdrH };
                DrawTextA(hdc, hdr, -1, &fr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }
            SelectObject(hdc, old);
            DeleteObject(hfont);
        }

        struct Row {
            char     label[8]      = {};
            float    pct           = 0.0f;
            COLORREF fill          = 0;
            char     barText[64]   = {};
            char     rightLabel[8] = {};
            char     rightValue[24]= {};
            COLORREF rightClr      = 0;
        };
        std::vector<Row> rows;

        const std::vector<DriveInfo> drives = DrivesSnapshot();

        // Free space on the drive tonight is landing on, for scaling the
        // REC bar below.
        unsigned long long captureFree = 0;
        for (const DriveInfo& d : drives)
            if (d.letter == m_data.captureDrive) captureFree = d.freeBytes;

        // ── The two capture rows ────────────────────────────────
        {
            Row r;
            // Called REC, not HDD. The figure is bytes written by THIS
            // recording -- it is not disk usage and never was. That
            // mattered less when it sat alone; directly above a list of
            // real drive capacities, a row labelled HDD showing 0.188 GB
            // reads as a disk with 0.188 GB on it.
            strcpy_s(r.label, "REC");

            // Scaled against the free space on the capture drive, so the
            // bar answers the question actually being asked during a
            // run: how much of tonight's headroom is gone. The old 500
            // GB constant was arbitrary and meant nothing on any
            // particular machine.
            const double writtenB = (double)m_data.diskUsedGB * 1e9;
            const double headroom = writtenB + (double)captureFree;
            r.pct = (m_data.recording && headroom > 0.0)
                  ? (float)(writtenB / headroom * 100.0) : 0.0f;

            r.fill = (r.pct > 90.0f) ? Theme::NV(RGB(255, 60, 90))
                   : (r.pct > 75.0f) ? Theme::NV(RGB(255, 170, 0))
                                     : Theme::NV(RGB(0, 200, 110));
            if (captureFree > 0)
                sprintf_s(r.barText, "%.3f GB written to %c:",
                          m_data.diskUsedGB, m_data.captureDrive);
            else
                sprintf_s(r.barText, "%.3f GB written", m_data.diskUsedGB);
            strcpy_s(r.rightLabel, "FPS");
            sprintf_s(r.rightValue, "%.1f", m_data.fps);
            r.rightClr = Theme::NV(RGB(0, 229, 255));
            rows.push_back(r);
        }
        {
            Row r;
            strcpy_s(r.label, "BUF");
            r.pct  = m_data.bufferPct;
            r.fill = (m_data.bufferPct > 80.0f) ? Theme::NV(RGB(255, 60, 90))
                   : (m_data.bufferPct > 50.0f) ? Theme::NV(RGB(255, 170, 0))
                                                : Theme::NV(RGB(0, 200, 110));
            sprintf_s(r.barText, "%d / %d slots",
                      m_data.bufferFrames,
                      m_data.bufferTotal > 0 ? m_data.bufferTotal : 512);
            strcpy_s(r.rightLabel, "LOST");
            sprintf_s(r.rightValue, "%d", m_data.framesLost);
            r.rightClr = m_data.framesLost > 0 ? Theme::NV(RGB(255, 60, 90))
                                               : Theme::NV(RGB(120, 136, 156));
            rows.push_back(r);
        }

        // ── One row per fixed drive, in letter order ────────────
        //
        // The bar is space USED, so a drive filling up grows a bar, the
        // same direction as every other bar in the app. The number that
        // decides whether tonight fits is FREE, so that is the one in
        // words and in the right-hand column.
        for (const DriveInfo& d : drives) {
            const double totalGB = (double)d.totalBytes / 1e9;
            const double freeGB  = (double)d.freeBytes  / 1e9;
            const double freePct = (d.totalBytes > 0)
                                 ? (double)d.freeBytes / (double)d.totalBytes * 100.0
                                 : 0.0;
            Row r;
            // The capture drive is marked, because on a five-drive
            // machine knowing which row is the one tonight lands on
            // matters more than any single number in it.
            if (d.letter == m_data.captureDrive)
                sprintf_s(r.label, "%c: *", d.letter);
            else
                sprintf_s(r.label, "%c:", d.letter);

            r.pct = (float)(100.0 - freePct);

            // Thresholds in GB rather than percent. A 2% margin is
            // ample on a 4 TB drive and nothing at all on a 256 GB one,
            // and what matters is whether a night's SER files fit --
            // those are tens of GB regardless of how big the drive is.
            r.fill = (freeGB < 20.0)  ? Theme::NV(RGB(255, 60, 90))
                   : (freeGB < 100.0) ? Theme::NV(RGB(255, 170, 0))
                                      : Theme::NV(RGB(0, 200, 110));

            // Both figures in the same unit. "1448 GB free of 2.00 TB"
            // is two units in one sentence and needs arithmetic to read;
            // the whole point of the row is to be read at a glance.
            if (totalGB >= 1000.0)
                sprintf_s(r.barText, "%.2f TB free of %.2f TB",
                          freeGB / 1000.0, totalGB / 1000.0);
            else
                sprintf_s(r.barText, "%.0f GB free of %.0f GB", freeGB, totalGB);

            strcpy_s(r.rightLabel, "FREE");
            sprintf_s(r.rightValue, "%.0f%%", freePct);
            r.rightClr = r.fill;
            rows.push_back(r);
        }

        // Scale off the window AND the row count, because this gets a
        // screen of its own and the number of drives is not known until
        // the scanner has run.
        const int n     = (int)rows.size();
        const int bodyH = H - hdrH;
        const int pad   = (std::max)(4,  (std::min)(W / 60, (bodyH / (n + 1)) / 4));
        const int rowH  = (std::max)(14, (bodyH - pad * (n + 1)) / n);
        const int fontPx= (std::max)(10, (std::min)(30, rowH / 2));
        const int lblW  = fontPx * 4;
        const int rightW= fontPx * 8;
        const int barW  = W - pad * 2 - lblW - rightW - 8;
        if (barW < 40) return;

        HFONT lf = MakeFont(fontPx, true,  "Barlow Condensed");
        HFONT vf = MakeFont(fontPx, false, "Space Mono");
        SetBkMode(hdc, TRANSPARENT);

        int y = hdrH + pad;
        for (const Row& r : rows) {
            HGDIOBJ old = SelectObject(hdc, lf);
            SetTextColor(hdc, Theme::NV(RGB(120, 136, 156)));
            RECT lr = { pad, y, pad + lblW, y + rowH };
            DrawTextA(hdc, r.label, -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, old);

            const int bx = pad + lblW;
            Bar(hdc, bx, y, barW, rowH, r.pct, r.fill, Theme::NV(RGB(4, 10, 18)),
                r.barText, Theme::NV(RGB(200, 232, 240)), fontPx);

            old = SelectObject(hdc, lf);
            SetTextColor(hdc, Theme::NV(RGB(120, 136, 156)));
            RECT rl = { bx + barW + 6, y, bx + barW + 6 + rightW / 2, y + rowH };
            DrawTextA(hdc, r.rightLabel, -1, &rl, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, old);

            old = SelectObject(hdc, vf);
            SetTextColor(hdc, r.rightClr);
            RECT rv = { bx + barW + 6 + rightW / 2, y, W - pad, y + rowH };
            DrawTextA(hdc, r.rightValue, -1, &rv, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, old);

            y += rowH + pad;
        }

        DeleteObject(lf);
        DeleteObject(vf);
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        StatusWindow* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<StatusWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->m_hwnd = hwnd;
        } else {
            self = reinterpret_cast<StatusWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

        switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT cr; GetClientRect(hwnd, &cr);
                self->Draw(hdc, cr);
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_SIZE:
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case WM_KEYDOWN:
                if (wp == VK_F11) { self->ToggleFullscreen(); return 0; }
                if (wp == VK_ESCAPE) {
                    if (self->m_fullscreen) self->ToggleFullscreen();
                    else                    self->Hide();
                    return 0;
                }
                break;
            case WM_CLOSE:
                self->Hide();
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    HWND            m_hwnd = nullptr;
    Data            m_data;
    bool            m_fullscreen = false;
    WINDOWPLACEMENT m_savedPlacement = {};

    // Written by the scanner thread, read by Draw() on the UI thread.
    std::vector<DriveInfo> m_drives;
    std::mutex             m_drivesMx;
    std::thread            m_scanner;
    std::atomic<bool>      m_scanStop{ false };
};

} // namespace raduk::gui
