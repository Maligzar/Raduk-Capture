#pragma once
// ============================================================
// PlayerWindow.h -- PLAY: look through a SER recording
//
// 2026-11-10. PIPP's test preview showed frame 1 of an 11,836-frame
// recording as noise, with no way to see whether the planet was in any
// of the others. This window answers that from inside the app:
//
//   - the frame, debayered and stretched the way the live preview does
//   - a slider that is also a quality trace: every frame scored in the
//     background (core/SerScan.h, TargetFinder + the planet focus
//     metric autofocus uses). Peaks are the sharp stretches; red ticks
//     along the bottom are frames with no target in them
//   - play/pause, step, speed, jump to the best frame, zoom and pan
//
// Opened from [PLAY] beside [TO SER] in CAPTURE, by dropping a .ser on
// the main window or this one, or by OPEN / O here.
//
// Keys stay here. The main window routes keys from every window of the
// app to its own hotkeys (Window::RouteThreadKey) unless the focused
// window asks for them, and here the arrow keys step frames: without
// WM_GETDLGCODE answering DLGC_WANTALLKEYS they would jog the mount.
//
// Keys: Space play/pause; Left/Right step (Shift x10, Ctrl x100);
// Home/End; Up/Down speed; B best frame; S stretch; C colour/RAW;
// O open; wheel zoom, drag pan, double-click fit; F11 full screen;
// Esc leaves full screen, then closes.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "WindowChrome.h"
#include "BundledFonts.h"
#include "Theme.h"
#include "../core/SerReader.h"
#include "../core/SerScan.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

namespace raduk::gui {

class PlayerWindow {
public:
    // Hidden until a file is opened.
    bool Create(HWND parent) {
        m_parent = parent;
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_DBLCLKS;
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(Theme::NV(Theme::Rgb::BG_VOID));
        wc.lpszClassName = L"RadukPlayer";
        RegisterClassExW(&wc);

        m_hwnd = CreateWindowExW(
            WS_EX_ACCEPTFILES, L"RadukPlayer", L"Play",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX | WS_MAXIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT, 1100, 780, parent, nullptr,
            GetModuleHandle(nullptr), this);
        if (!m_hwnd) return false;

        BOOL dark = TRUE;
        DwmSetWindowAttribute(m_hwnd, 20, &dark, sizeof(dark));
        DwmSetWindowAttribute(m_hwnd, 19, &dark, sizeof(dark));
        if (!InitD2D()) { DestroyWindow(m_hwnd); m_hwnd = nullptr; return false; }
        chrome::Flags f;
        f.minimize = false;
        m_chrome.Attach(m_hwnd, f);
        return true;
    }

    void Destroy() {
        m_scan.Stop();
        m_reader.Close();
        m_chrome.Release();
        ReleaseD2D();
        if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    }

    HWND Handle() const { return m_hwnd; }
    bool IsVisible() const { return m_hwnd && IsWindowVisible(m_hwnd); }
    void Hide() { Pause(); if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE); }
    void Show() {
        if (!m_hwnd) return;
        if (!m_placed) PlaceNearParent();
        ShowWindow(m_hwnd, SW_SHOW);
        SetForegroundWindow(m_hwnd);
        SetFocus(m_hwnd);
    }

    // "[PLAY] ..." lines for the session log, which scenarios read.
    void SetLog(std::function<void(const std::string&)> f) { m_log = std::move(f); }
    // Where OPEN starts looking.
    void SetFolderSource(std::function<std::wstring()> f) { m_folder = std::move(f); }

    // Path in the ANSI code page, as the rest of the app's fopen paths.
    bool OpenFile(const std::string& path) {
        Pause();
        m_scan.Stop();
        if (!m_reader.Open(path)) {
            Say("[PLAY] Cannot open " + path + ": " + m_reader.GetInfo().error);
            m_error = Widen("Cannot open " + Leaf(path) + ": " + m_reader.GetInfo().error);
            Show();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return false;
        }
        m_error.clear();
        const auto& in = m_reader.GetInfo();
        m_fps = m_reader.FrameRate();
        m_frame = 0;
        m_pos = 0.0;
        m_zoom = 1.0f; m_panX = m_panY = 0.5f;
        if (m_bitmap) { m_bitmap->Release(); m_bitmap = nullptr; }
        ComputeAutoStretch();
        m_scan.Start(path, in.frames);
        SetWindowTextW(m_hwnd, (L"Play - " + Widen(Leaf(path))).c_str());
        char b[400];
        snprintf(b, sizeof(b), "[PLAY] Opened %s: %lld frames, %dx%d, %d-bit %s, %s%s%s",
                 Leaf(path).c_str(), (long long)in.frames, in.width, in.height, in.depth,
                 raduk::core::ser::ColorName(in.colorId),
                 m_fps > 0 ? (std::to_string((int)std::lround(m_fps)) + " fps").c_str() : "no timestamps",
                 in.recovered ? ", unclosed: count from file size" : "",
                 in.swap16 ? ", big-endian samples" : "");
        Say(b);
        LoadFrame(0);
        Show();
        return true;
    }

    // OPEN: a file dialog in the output folder.
    void OpenWithDialog() {
        std::vector<wchar_t> buf(4096, 0);
        const std::wstring dir = m_folder ? m_folder() : std::wstring();
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner   = IsVisible() ? m_hwnd : m_parent;
        ofn.lpstrFilter = L"SER recordings (*.ser)\0*.ser\0All files\0*.*\0";
        ofn.lpstrFile   = buf.data();
        ofn.nMaxFile    = (DWORD)buf.size();
        ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
        ofn.lpstrTitle  = L"Play a SER recording";
        ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
        if (GetOpenFileNameW(&ofn)) OpenFile(Narrow(buf.data()));
    }

    // Scenario steps: "STEP", "BACK", "PLAY", "PAUSE", "BEST", "END",
    // "HOME", "GOTO <n>" (1-based, as shown), "WAIT SCAN" (true once the
    // scan has finished). False if not understood or nothing is open.
    bool ScenarioCommand(const std::string& c) {
        if (!m_reader.IsOpen()) return false;
        if (c == "STEP")  { Step(1);  return true; }
        if (c == "BACK")  { Step(-1); return true; }
        if (c == "HOME")  { GoTo(0, true); return true; }
        if (c == "END")   { GoTo(m_reader.GetInfo().frames - 1, true); return true; }
        if (c == "PLAY")  { Play(); return true; }
        if (c == "PAUSE") { Pause(); return true; }
        if (c == "BEST")  { JumpBest(); return true; }
        if (c.rfind("GOTO ", 0) == 0) { GoTo(_atoi64(c.c_str() + 5) - 1, true); return true; }
        return false;
    }
    bool ScanFinished() const { return m_reader.IsOpen() && !m_scan.Running(); }

    // ── Drops from the main window go through here too ──────────
    bool OpenDropped(HDROP drop) {
        const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        std::wstring pick;
        for (UINT i = 0; i < n && pick.empty(); ++i) {
            wchar_t p[MAX_PATH * 2] = {};
            DragQueryFileW(drop, i, p, (UINT)std::size(p));
            const size_t len = wcslen(p);
            if (len > 4 && _wcsicmp(p + len - 4, L".ser") == 0) pick = p;
        }
        DragFinish(drop);
        if (pick.empty()) return false;
        return OpenFile(Narrow(pick));
    }

private:
    // ── Layout ─────────────────────────────────────────────────
    static constexpr float kPad = 10.0f;
    static constexpr float kInfoH = 22.0f;
    static constexpr float kTraceH = 70.0f;
    static constexpr float kBtnH = 30.0f;

    struct Btn { const wchar_t* id; std::wstring label; D2D1_RECT_F r; bool lit; };

    D2D1_RECT_F BarRect() const {
        const float barH = kInfoH * 2 + kTraceH + kBtnH + kPad * 4;
        return D2D1::RectF(0, (float)m_h - barH, (float)m_w, (float)m_h);
    }
    D2D1_RECT_F ImageArea() const {
        const float top = (float)m_chrome.CaptionHeight();
        return D2D1::RectF(0, top, (float)m_w, BarRect().top);
    }
    D2D1_RECT_F TraceRect() const {
        const D2D1_RECT_F b = BarRect();
        const float y = b.top + kPad + kInfoH * 2;
        return D2D1::RectF(kPad, y, (float)m_w - kPad, y + kTraceH);
    }
    std::vector<Btn> Buttons() const {
        wchar_t sp[32];
        const double s = kSpeedTable[m_speedIdx];
        if (s < 1) swprintf(sp, 32, L"SPEED %.2gx", s); else swprintf(sp, 32, L"SPEED %gx", s);
        std::vector<Btn> v = {
            { L"open",    L"OPEN",  {}, false },
            { L"home",    L"|<",    {}, false },
            { L"back",    L"<",     {}, false },
            { L"play",    m_playing ? L"PAUSE" : L"PLAY", {}, m_playing },
            { L"step",    L">",     {}, false },
            { L"end",     L">|",    {}, false },
            { L"best",    L"BEST",  {}, false },
            { L"speed",   sp,       {}, false },
            { L"stretch", m_autoStretch ? L"STRETCH AUTO" : L"STRETCH OFF", {}, m_autoStretch },
            { L"color",   m_color ? L"COLOUR" : L"RAW", {}, m_color },
        };
        const float y = TraceRect().bottom + kPad;
        float x = kPad;
        for (Btn& b : v) {
            const float w = b.label.size() <= 2 ? 44.0f : 18.0f + 9.0f * (float)b.label.size();
            b.r = D2D1::RectF(x, y, x + w, y + kBtnH);
            x += w + 6.0f;
        }
        return v;
    }

    // ── Frames ─────────────────────────────────────────────────
    static constexpr double kSpeedTable[8] = { 0.1, 0.25, 0.5, 1, 2, 4, 8, 16 };

    void ComputeAutoStretch() {
        const auto& in = m_reader.GetInfo();
        std::vector<std::vector<uint16_t>> planes(5);
        std::vector<const std::vector<uint16_t>*> ptrs;
        std::vector<uint8_t> raw;
        for (int k = 0; k < 5; ++k) {
            const int64_t i = in.frames <= 1 ? 0 : (in.frames - 1) * k / 4;
            if (!m_reader.ReadFrame(i, raw)) continue;
            raduk::core::ser::ToPlane16(raw.data(), in, planes[k]);
            ptrs.push_back(&planes[k]);
        }
        m_stretch = raduk::core::ser::AutoStretch(ptrs);
    }

    void LoadFrame(int64_t i) {
        const auto& in = m_reader.GetInfo();
        if (!m_reader.IsOpen() || !m_rt) return;
        i = (std::clamp)(i, (int64_t)0, in.frames - 1);
        if (!m_reader.ReadFrame(i, m_raw)) return;
        m_frame = i;
        raduk::core::ser::ToBgra(m_raw.data(), in, m_color,
                                 m_autoStretch ? m_stretch : raduk::core::ser::Stretch{},
                                 m_bgra);
        const D2D1_SIZE_U sz = D2D1::SizeU((UINT32)in.width, (UINT32)in.height);
        if (m_bitmap) {
            const D2D1_SIZE_U have = m_bitmap->GetPixelSize();
            if (have.width != sz.width || have.height != sz.height) { m_bitmap->Release(); m_bitmap = nullptr; }
        }
        if (!m_bitmap) {
            m_rt->CreateBitmap(sz, m_bgra.data(), (UINT32)in.width * 4,
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                         D2D1_ALPHA_MODE_IGNORE)),
                &m_bitmap);
        } else {
            m_bitmap->CopyFromMemory(nullptr, m_bgra.data(), (UINT32)in.width * 4);
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void GoTo(int64_t i, bool log) {
        if (!m_reader.IsOpen()) return;
        LoadFrame(i);
        m_pos = (double)m_frame;
        if (log) LogFrame();
    }
    void Step(int64_t d) { Pause(); GoTo(m_frame + d, true); }

    void LogFrame() {
        const float s = m_scan.Score(m_frame);
        char b[160];
        snprintf(b, sizeof(b), "[PLAY] frame %lld of %lld%s", (long long)m_frame + 1,
                 (long long)m_reader.GetInfo().frames,
                 s < 0 ? "" : s == 0 ? ", no target" : (", score " + std::to_string((int)std::lround(s))).c_str());
        Say(b);
    }

    void Play() {
        if (!m_reader.IsOpen() || m_playing) return;
        m_playing = true;
        m_pos = (double)m_frame;
        m_lastTick = std::chrono::steady_clock::now();
        Say("[PLAY] playing");
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    void Pause() {
        if (!m_playing) return;
        m_playing = false;
        Say("[PLAY] paused");
        LogFrame();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void JumpBest() {
        int64_t best = -1; float bs = 0.0f;
        for (int64_t i = 0; i < m_scan.Frames(); ++i) {
            const float s = m_scan.Score(i);
            if (s > bs) { bs = s; best = i; }
        }
        Pause();
        if (best < 0) { Say("[PLAY] no frame with a target yet"); return; }
        GoTo(best, true);
    }

    void OnTimer() {
        if (m_playing) {
            const auto now = std::chrono::steady_clock::now();
            const double dt = std::chrono::duration<double>(now - m_lastTick).count();
            m_lastTick = now;
            const double fps = (m_fps > 0 ? m_fps : 30.0) * kSpeedTable[m_speedIdx];
            m_pos += dt * fps;
            const int64_t n = m_reader.GetInfo().frames;
            if (m_pos >= (double)n) m_pos = std::fmod(m_pos, (double)n);   // loops
            const int64_t f = (int64_t)m_pos;
            if (f != m_frame) LoadFrame(f);
        }
        // The trace fills in while the scan runs; a few repaints a second.
        if (m_scan.Running() || m_scanWasRunning) {
            if (++m_scanTicks >= 15) { m_scanTicks = 0; InvalidateRect(m_hwnd, nullptr, FALSE); }
            if (!m_scan.Running() && m_scanWasRunning) LogScanSummary();
            m_scanWasRunning = m_scan.Running();
        }
    }

    void LogScanSummary() {
        int64_t found = 0, best = -1; float bs = 0;
        const int64_t n = m_scan.Frames();
        for (int64_t i = 0; i < n; ++i) {
            const float s = m_scan.Score(i);
            if (s > 0) { ++found; if (s > bs) { bs = s; best = i; } }
        }
        char b[200];
        snprintf(b, sizeof(b), "[PLAY] Scan done: target in %lld of %lld frames%s", (long long)found,
                 (long long)n, best >= 0 ? (", best frame " + std::to_string(best + 1)).c_str() : "");
        Say(b);
    }

    // ── Input ──────────────────────────────────────────────────
    void Press(const std::wstring& id) {
        if (id == L"open") OpenWithDialog();
        else if (id == L"home") { Pause(); GoTo(0, true); }
        else if (id == L"back") Step(-1);
        else if (id == L"play") m_playing ? Pause() : Play();
        else if (id == L"step") Step(1);
        else if (id == L"end") { Pause(); GoTo(m_reader.GetInfo().frames - 1, true); }
        else if (id == L"best") JumpBest();
        else if (id == L"speed") { m_speedIdx = (m_speedIdx + 1) % 8; }
        else if (id == L"stretch") { m_autoStretch = !m_autoStretch; LoadFrame(m_frame); }
        else if (id == L"color") { m_color = !m_color; LoadFrame(m_frame); }
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void ScrubTo(float x) {
        const D2D1_RECT_F t = TraceRect();
        const int64_t n = m_reader.GetInfo().frames;
        if (n <= 0) return;
        const float f = (std::clamp)((x - t.left) / (t.right - t.left), 0.0f, 1.0f);
        const int64_t i = (std::min)(n - 1, (int64_t)(f * (float)n));
        if (i != m_frame) { LoadFrame(i); m_pos = (double)i; }
    }

    void OnMouseDown(int x, int y) {
        SetFocus(m_hwnd);
        const float fx = (float)x, fy = (float)y;
        for (const Btn& b : Buttons())
            if (fx >= b.r.left && fx < b.r.right && fy >= b.r.top && fy < b.r.bottom) { Press(b.id); return; }
        const D2D1_RECT_F t = TraceRect();
        if (m_reader.IsOpen() && fy >= t.top - 4 && fy < t.bottom + 4) {
            Pause();
            m_drag = Drag::Scrub;
            SetCapture(m_hwnd);
            ScrubTo(fx);
            return;
        }
        const D2D1_RECT_F a = ImageArea();
        if (fy >= a.top && fy < a.bottom && m_zoom > 1.0f) {
            m_drag = Drag::Pan;
            m_dragX = x; m_dragY = y;
            SetCapture(m_hwnd);
        }
    }
    void OnMouseMove(int x, int y) {
        if (m_drag == Drag::Scrub) ScrubTo((float)x);
        else if (m_drag == Drag::Pan) {
            const D2D1_RECT_F d = ImageRect();
            if (d.right > d.left && d.bottom > d.top) {
                m_panX -= (float)(x - m_dragX) / (d.right - d.left);
                m_panY -= (float)(y - m_dragY) / (d.bottom - d.top);
                ClampPan();
            }
            m_dragX = x; m_dragY = y;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }
    void OnMouseUp() {
        if (m_drag == Drag::Scrub) LogFrame();
        m_drag = Drag::None;
        ReleaseCapture();
    }
    void OnWheel(int delta, int sx, int sy) {
        POINT p = { sx, sy };
        ScreenToClient(m_hwnd, &p);
        const D2D1_RECT_F before = ImageRect();
        if (before.right <= before.left) return;
        // The image point under the cursor stays under it.
        const float u = ((float)p.x - before.left) / (before.right - before.left);
        const float v = ((float)p.y - before.top) / (before.bottom - before.top);
        const float z = m_zoom * std::pow(1.25f, (float)delta / WHEEL_DELTA);
        m_zoom = (std::clamp)(z, 1.0f, 32.0f);
        const D2D1_RECT_F a = ImageArea();
        const D2D1_RECT_F after = ImageRect();
        const float wA = after.right - after.left, hA = after.bottom - after.top;
        // Solve for the pan that puts (u, v) at the cursor.
        const float cx = (a.left + a.right) / 2.0f, cy = (a.top + a.bottom) / 2.0f;
        m_panX = u - ((float)p.x - cx) / wA;
        m_panY = v - ((float)p.y - cy) / hA;
        ClampPan();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    void ClampPan() {
        const float half = 0.5f / m_zoom;
        m_panX = (std::clamp)(m_panX, half, 1.0f - half);
        m_panY = (std::clamp)(m_panY, half, 1.0f - half);
    }

    bool OnKey(WPARAM vk) {
        const bool shift = GetKeyState(VK_SHIFT) < 0, ctrl = GetKeyState(VK_CONTROL) < 0;
        const int64_t step = ctrl ? 100 : shift ? 10 : 1;
        switch (vk) {
            case VK_SPACE: m_playing ? Pause() : Play(); return true;
            case VK_RIGHT: Step(step);  return true;
            case VK_LEFT:  Step(-step); return true;
            case VK_HOME:  Press(L"home"); return true;
            case VK_END:   Press(L"end");  return true;
            case VK_UP:    m_speedIdx = (std::min)(7, m_speedIdx + 1); InvalidateRect(m_hwnd, nullptr, FALSE); return true;
            case VK_DOWN:  m_speedIdx = (std::max)(0, m_speedIdx - 1); InvalidateRect(m_hwnd, nullptr, FALSE); return true;
            case 'B': JumpBest(); return true;
            case 'S': Press(L"stretch"); return true;
            case 'C': Press(L"color"); return true;
            case 'O': OpenWithDialog(); return true;
            case VK_F11: ToggleFullscreen(); return true;
            case VK_ESCAPE: if (m_fullscreen) ToggleFullscreen(); else Hide(); return true;
        }
        return false;
    }

    void ToggleFullscreen() {
        if (!m_fullscreen) {
            m_savedPlacement.length = sizeof(WINDOWPLACEMENT);
            GetWindowPlacement(m_hwnd, &m_savedPlacement);
            MONITORINFO mi = { sizeof(mi) };
            if (!GetMonitorInfo(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi)) return;
            SetWindowLongW(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(m_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            m_fullscreen = true;
        } else {
            SetWindowLongW(m_hwnd, GWL_STYLE, WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX |
                                              WS_MAXIMIZEBOX | WS_VISIBLE);
            SetWindowPlacement(m_hwnd, &m_savedPlacement);
            SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            m_fullscreen = false;
        }
        SetFocus(m_hwnd);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    // First show: centred on the monitor the main window is on.
    void PlaceNearParent() {
        m_placed = true;
        MONITORINFO mi = { sizeof(mi) };
        if (!GetMonitorInfo(MonitorFromWindow(m_parent ? m_parent : m_hwnd, MONITOR_DEFAULTTONEAREST), &mi)) return;
        const RECT& w = mi.rcWork;
        const int ww = (std::min)(1100, (int)(w.right - w.left) - 40);
        const int hh = (std::min)(780, (int)(w.bottom - w.top) - 40);
        SetWindowPos(m_hwnd, nullptr, w.left + ((w.right - w.left) - ww) / 2,
                     w.top + ((w.bottom - w.top) - hh) / 2, ww, hh, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // ── Drawing ────────────────────────────────────────────────
    D2D1_RECT_F ImageRect() const {
        const auto& in = m_reader.GetInfo();
        const D2D1_RECT_F a = ImageArea();
        const float aw = a.right - a.left - 2 * kPad, ah = a.bottom - a.top - 2 * kPad;
        if (!m_reader.IsOpen() || in.width <= 0 || aw <= 0 || ah <= 0) return D2D1::RectF(0, 0, 0, 0);
        const float fit = (std::min)(aw / (float)in.width, ah / (float)in.height);
        const float s = fit * m_zoom;
        const float w = in.width * s, h = in.height * s;
        const float cx = (a.left + a.right) / 2.0f, cy = (a.top + a.bottom) / 2.0f;
        const float left = cx - m_panX * w, top = cy - m_panY * h;
        return D2D1::RectF(left, top, left + w, top + h);
    }

    void Draw() {
        if (!m_rt) return;
        if (m_brushGen != Theme::PaletteGeneration()) { ReleaseBrushes(); CreateBrushes(); }
        m_rt->BeginDraw();
        m_rt->Clear(Theme::Color(Theme::BG_VOID));

        const D2D1_RECT_F area = ImageArea();
        if (m_bitmap) {
            const D2D1_RECT_F d = ImageRect();
            const float scale = (d.right - d.left) / (float)m_reader.GetInfo().width;
            m_rt->PushAxisAlignedClip(area, D2D1_ANTIALIAS_MODE_ALIASED);
            m_rt->DrawBitmap(m_bitmap, d, 1.0f,
                             scale >= 2.0f ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                                           : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            m_rt->PopAxisAlignedClip();
        } else {
            const std::wstring msg = !m_error.empty() ? m_error
                : L"Drop a .ser here, or press OPEN";
            Text(msg, area, m_error.empty() ? m_dim : m_red, m_label, DWRITE_TEXT_ALIGNMENT_CENTER);
        }

        const D2D1_RECT_F bar = BarRect();
        m_rt->FillRectangle(bar, m_panel);
        m_rt->DrawLine(D2D1::Point2F(0, bar.top), D2D1::Point2F((float)m_w, bar.top), m_line, 1.0f);
        DrawInfo(bar);
        DrawTrace();
        for (const Btn& b : Buttons()) {
            m_rt->FillRectangle(b.r, m_deep);
            m_rt->DrawRectangle(b.r, b.lit ? m_accent : m_line, 1.0f);
            Text(b.label, b.r, b.lit ? m_accent : m_text, m_label, DWRITE_TEXT_ALIGNMENT_CENTER);
        }

        m_chrome.Draw(m_rt, m_dwrite, m_w);
        if (m_rt->EndDraw() == D2DERR_RECREATE_TARGET) { ReleaseTarget(); CreateTarget(); LoadFrame(m_frame); }
    }

    void DrawInfo(const D2D1_RECT_F& bar) {
        if (!m_reader.IsOpen()) return;
        const auto& in = m_reader.GetInfo();
        const float y = bar.top + kPad;
        wchar_t b[400];
        const std::string t = raduk::core::ser::TimeOfDay(m_reader.Timestamp(m_frame));
        swprintf(b, 400, L"FRAME %lld / %lld    %hs%hs    %dx%d  %d-bit %hs    %hs",
                 (long long)m_frame + 1, (long long)in.frames, t.c_str(), t.empty() ? "" : " UTC",
                 in.width, in.height, in.depth, raduk::core::ser::ColorName(in.colorId),
                 m_fps > 0 ? (std::to_string((int)std::lround(m_fps)) + " fps").c_str() : "no timestamps");
        Text(b, D2D1::RectF(kPad, y, (float)m_w - kPad, y + kInfoH), m_text, m_data, DWRITE_TEXT_ALIGNMENT_LEADING);

        // Second line: this frame's score, the scan, and anything unusual
        // about the file.
        std::wstring s;
        const float sc = m_scan.Score(m_frame);
        if (sc < 0) s = L"not scored yet";
        else if (sc == 0) s = L"NO TARGET in this frame";
        else { wchar_t q[64]; swprintf(q, 64, L"score %.0f  (HFD %.1f px)", sc, sc > 0.01f ? 1000.0f / sc : 0.0f); s = q; }
        const int64_t done = m_scan.Scanned(), n = m_scan.Frames();
        if (m_scan.Running() && n > 0) {
            wchar_t q[64]; swprintf(q, 64, L"    scanning %lld%%", (long long)(done * 100 / n)); s += q;
        } else if (n > 0) {
            int64_t found = 0;
            for (int64_t i = 0; i < n; ++i) if (m_scan.Score(i) > 0) ++found;
            wchar_t q[96]; swprintf(q, 96, L"    target in %lld%% of frames", (long long)(found * 100 / n)); s += q;
        }
        if (in.recovered) s += L"    UNCLOSED: count from file size";
        if (in.swap16)    s += L"    big-endian samples";
        Text(s, D2D1::RectF(kPad, y + kInfoH, (float)m_w - kPad, y + 2 * kInfoH),
             sc == 0 ? m_red : m_dim, m_data, DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    // One column per pixel: the bar is the mean score of the frames in
    // that column, against the best column; a red tick along the bottom
    // is how many of them had no target. Unscanned columns stay empty.
    void DrawTrace() {
        const D2D1_RECT_F t = TraceRect();
        m_rt->FillRectangle(t, m_deep);
        m_rt->DrawRectangle(t, m_line, 1.0f);
        const int64_t n = m_scan.Frames();
        if (!m_reader.IsOpen() || n <= 0) return;
        const int cols = (std::max)(1, (int)(t.right - t.left));
        m_cols.assign((size_t)cols, Col{});
        // Each column takes the frames under it, and at least one: a
        // short file spreads each frame across several columns instead
        // of drawing a comb with gaps between them.
        for (int x = 0; x < cols; ++x) {
            const int64_t a = (int64_t)x * n / cols;
            const int64_t b = (std::max)(a + 1, (int64_t)(x + 1) * n / cols);
            Col& c = m_cols[(size_t)x];
            for (int64_t i = a; i < b && i < n; ++i) {
                const float s = m_scan.Score(i);
                if (s < 0) continue;
                ++c.scanned;
                if (s == 0) ++c.empty; else { c.sum += s; ++c.found; }
            }
        }
        // Scaled from just under the worst column to the best, not from
        // zero: seeing moves the score by tens of percent, and against a
        // zero baseline that is a row of nearly equal bars.
        float top = 0, low = 1e30f;
        for (const Col& c : m_cols)
            if (c.found) { top = (std::max)(top, c.sum / c.found); low = (std::min)(low, c.sum / c.found); }
        const float floorV = (std::max)(0.0f, low - 0.15f * (top - low));
        const float span = top - floorV;
        const float tickH = 6.0f, h = t.bottom - t.top - tickH - 2;
        for (int x = 0; x < cols; ++x) {
            const Col& c = m_cols[(size_t)x];
            if (!c.scanned) continue;
            const float fx = t.left + x + 0.5f;
            if (c.found && top > 0) {
                // All alike (span ~0): full height. Otherwise never below
                // a sliver, so a found target always shows.
                const float v = span < top * 0.02f ? 1.0f
                    : (std::max)(0.08f, ((c.sum / c.found) - floorV) / span);
                m_rt->DrawLine(D2D1::Point2F(fx, t.bottom - tickH - 1), D2D1::Point2F(fx, t.bottom - tickH - 1 - v * h), m_accent, 1.0f);
            }
            if (c.empty) {
                const float f = (float)c.empty / (float)c.scanned;
                m_rt->DrawLine(D2D1::Point2F(fx, t.bottom - 1), D2D1::Point2F(fx, t.bottom - 1 - tickH * (0.3f + 0.7f * f)), m_red, 1.0f);
            }
        }
        const float px = t.left + (float)((double)(m_frame + 0.5) / (double)n * (t.right - t.left));
        m_rt->DrawLine(D2D1::Point2F(px, t.top), D2D1::Point2F(px, t.bottom), m_text, 2.0f);
    }

    void Text(const std::wstring& s, const D2D1_RECT_F& r, ID2D1SolidColorBrush* br,
              IDWriteTextFormat* f, DWRITE_TEXT_ALIGNMENT align) {
        if (!f || !br) return;
        f->SetTextAlignment(align);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_rt->DrawText(s.c_str(), (UINT32)s.size(), f, r, br, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    // ── Direct2D plumbing ──────────────────────────────────────
    bool InitD2D() {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_factory))) return false;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(&m_dwrite)))) return false;
        if (!CreateTarget()) return false;
        auto font = [&](const wchar_t* fam, float size, IDWriteTextFormat** out) {
            m_dwrite->CreateTextFormat(Fonts::FamilyFor(fam), Fonts::CollectionFor(fam),
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                size, L"en-us", out);
            if (*out) (*out)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        };
        font(L"Space Mono", 13.0f, &m_data);
        font(L"Barlow Condensed", 15.0f, &m_label);
        return true;
    }
    bool CreateTarget() {
        RECT rc; GetClientRect(m_hwnd, &rc);
        m_w = rc.right; m_h = rc.bottom;
        if (FAILED(m_factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
                D2D1::HwndRenderTargetProperties(m_hwnd, D2D1::SizeU((UINT32)m_w, (UINT32)m_h)), &m_rt)))
            return false;
        CreateBrushes();
        return true;
    }
    void ReleaseTarget() {
        ReleaseBrushes();
        if (m_bitmap) { m_bitmap->Release(); m_bitmap = nullptr; }
        if (m_rt) { m_rt->Release(); m_rt = nullptr; }
    }
    void CreateBrushes() {
        if (!m_rt) return;
        m_rt->CreateSolidColorBrush(Theme::Color(Theme::TEXT), &m_text);
        m_rt->CreateSolidColorBrush(Theme::Color(Theme::TEXT_DIM), &m_dim);
        m_rt->CreateSolidColorBrush(Theme::Color(Theme::ACCENT), &m_accent);
        m_rt->CreateSolidColorBrush(Theme::Color(Theme::RED), &m_red);
        m_rt->CreateSolidColorBrush(Theme::Color(Theme::PANEL), &m_panel);
        m_rt->CreateSolidColorBrush(Theme::Color(Theme::DEEP), &m_deep);
        m_rt->CreateSolidColorBrush(Theme::Color(Theme::BORDER), &m_line);
        m_brushGen = Theme::PaletteGeneration();
    }
    void ReleaseBrushes() {
        for (ID2D1SolidColorBrush** b : { &m_text, &m_dim, &m_accent, &m_red, &m_panel, &m_deep, &m_line })
            if (*b) { (*b)->Release(); *b = nullptr; }
    }
    void ReleaseD2D() {
        ReleaseTarget();
        if (m_data)    { m_data->Release();    m_data = nullptr; }
        if (m_label)   { m_label->Release();   m_label = nullptr; }
        if (m_dwrite)  { m_dwrite->Release();  m_dwrite = nullptr; }
        if (m_factory) { m_factory->Release(); m_factory = nullptr; }
    }
    void OnResize(int w, int h) {
        if (w <= 0 || h <= 0) return;
        m_w = w; m_h = h;
        if (m_rt) m_rt->Resize(D2D1::SizeU((UINT32)w, (UINT32)h));
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    // ── Small helpers ──────────────────────────────────────────
    void Say(const std::string& s) { if (m_log) m_log(s); }
    static std::string Leaf(const std::string& p) {
        const size_t k = p.find_last_of("\\/");
        return k == std::string::npos ? p : p.substr(k + 1);
    }
    static std::wstring Widen(const std::string& s) {
        const int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
        std::wstring w(n > 1 ? n - 1 : 0, L'\0');
        if (n > 1) MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, &w[0], n);
        return w;
    }
    static std::string Narrow(const std::wstring& w) {
        const int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string s(n > 1 ? n - 1 : 0, '\0');
        if (n > 1) WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
        return s;
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        PlayerWindow* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<PlayerWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->m_hwnd = hwnd;
        } else {
            self = reinterpret_cast<PlayerWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

        LRESULT chromeResult = 0;
        if (self->m_chrome.Filter(hwnd, msg, wp, lp, chromeResult)) return chromeResult;

        switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                BeginPaint(hwnd, &ps);
                self->Draw();
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_SIZE:        self->OnResize(LOWORD(lp), HIWORD(lp)); return 0;
            case WM_SHOWWINDOW:
                if (wp) SetTimer(hwnd, 1, 15, nullptr); else KillTimer(hwnd, 1);
                break;
            case WM_TIMER:       self->OnTimer(); return 0;
            case WM_GETDLGCODE:  return DLGC_WANTALLKEYS;   // see the header: arrows step, not jog
            case WM_KEYDOWN:     if (self->OnKey(wp)) return 0; break;
            case WM_LBUTTONDOWN: self->OnMouseDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0;
            case WM_MOUSEMOVE:   self->OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0;
            case WM_LBUTTONUP:   self->OnMouseUp(); return 0;
            case WM_CAPTURECHANGED: self->m_drag = Drag::None; return 0;
            case WM_LBUTTONDBLCLK:
                if (GET_Y_LPARAM(lp) < (int)self->BarRect().top) {
                    self->m_zoom = 1.0f; self->m_panX = self->m_panY = 0.5f;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                self->OnMouseDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
                return 0;
            case WM_MOUSEWHEEL:  self->OnWheel(GET_WHEEL_DELTA_WPARAM(wp), GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0;
            case WM_DROPFILES:   self->OpenDropped(reinterpret_cast<HDROP>(wp)); return 0;
            case WM_ERASEBKGND:  return 1;
            case WM_CLOSE:       self->Hide(); return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    enum class Drag { None, Scrub, Pan };
    struct Col { int scanned = 0, empty = 0, found = 0; float sum = 0; };

    HWND                    m_hwnd = nullptr, m_parent = nullptr;
    ID2D1Factory*           m_factory = nullptr;
    IDWriteFactory*         m_dwrite = nullptr;
    ID2D1HwndRenderTarget*  m_rt = nullptr;
    ID2D1Bitmap*            m_bitmap = nullptr;
    IDWriteTextFormat*      m_data = nullptr;
    IDWriteTextFormat*      m_label = nullptr;
    ID2D1SolidColorBrush   *m_text = nullptr, *m_dim = nullptr, *m_accent = nullptr, *m_red = nullptr,
                           *m_panel = nullptr, *m_deep = nullptr, *m_line = nullptr;
    unsigned                m_brushGen = 0;
    WindowChrome            m_chrome;
    int                     m_w = 1100, m_h = 780;
    bool                    m_placed = false;
    bool                    m_fullscreen = false;
    WINDOWPLACEMENT         m_savedPlacement = {};

    raduk::core::ser::Reader  m_reader;
    raduk::core::ser::Scan    m_scan;
    raduk::core::ser::Stretch m_stretch;
    std::vector<uint8_t>    m_raw, m_bgra;
    std::vector<Col>        m_cols;
    std::wstring            m_error;
    int64_t                 m_frame = 0;
    double                  m_pos = 0.0, m_fps = 0.0;
    bool                    m_playing = false, m_autoStretch = true, m_color = true;
    int                     m_speedIdx = 3;   // 1x
    std::chrono::steady_clock::time_point m_lastTick;
    int                     m_scanTicks = 0;
    bool                    m_scanWasRunning = false;
    float                   m_zoom = 1.0f, m_panX = 0.5f, m_panY = 0.5f;
    Drag                    m_drag = Drag::None;
    int                     m_dragX = 0, m_dragY = 0;

    std::function<void(const std::string&)> m_log;
    std::function<std::wstring()>           m_folder;
};

} // namespace raduk::gui
