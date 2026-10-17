// ============================================================
//  ShareView — the image, large, on a screen pointed at other people
//
//  Phase 89. A star party is the first thing this app has had to do
//  with two audiences at once. The owner is on display 1 with the
//  controls, the histogram, the reticle and the numbers; everybody
//  else is standing three metres back in the dark and wants the planet
//  and nothing else.
//
//  So this is not "the preview on another screen". It is a second,
//  deliberately empty window whose only job is to be looked at:
//  borderless, filling its monitor, image centred and aspect-preserved,
//  with one quiet caption line and nothing to click.
//
//  THE RULE: it must cost the capture nothing. A star party is still
//  an imaging session, and if putting the image on the wall drops
//  frames then the show has been paid for with the data. Three things
//  follow:
//
//    - No second debayer. It is handed the finished RGBA the preview
//      just built -- after calibration, debayer, FX, stacking and RGB
//      alignment -- and uploads that. It also means it shows exactly
//      what the owner sees, which matters when someone points at their
//      own screen and says "that bright spot".
//    - Its own frame rate, capped well below the preview's. Nobody
//      three metres back can tell 30 fps from 60.
//    - Nothing at all when it is closed, which is almost every night.
// ============================================================

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>
#include "Theme.h"

namespace raduk::gui {

class ShareView {
public:
    ShareView() = default;
    ~ShareView() { Destroy(); }

    ShareView(const ShareView&)            = delete;
    ShareView& operator=(const ShareView&) = delete;

    // What the caption says. Set from the main loop; all of it is
    // optional and an empty target simply draws nothing.
    struct Caption {
        std::string target;        // "Jupiter"
        std::string rig;           // "8SE  ASI678MC"
        double      exposureMs = 0.0;
        int         gain       = 0;
        double      fps        = 0.0;
        bool        recording  = false;
        double      altitudeDeg = 0.0;
        bool        haveAltitude = false;
    };

    bool Create(HWND parent, int x, int y, int w, int h) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;      // D2D clears; a GDI erase would flash
        wc.lpszClassName = L"RadukShareView";
        RegisterClassExW(&wc);

        // No WS_EX_NOACTIVATE.
        //
        // It was here for a good reason: showing this must never move
        // focus off the control screen, because the owner presses
        // Shift+V with one hand while doing something else, and a
        // keystroke that silently redirects the NEXT keystroke --
        // including the arrow keys, which jog the mount -- is a safety
        // problem and not an annoyance.
        //
        // But NOACTIVATE does not mean "do not take focus when shown".
        // It means the window can NEVER be focused, by any route --
        // not by clicking it, not through Alt-Tab. Combined with a
        // borderless fullscreen window, which has no caption to drag
        // and no close button, that left the view with no way in and
        // no way out: F11, Escape and C could not arrive, and hiding
        // and reshowing it only made it fullscreen again.
        //
        // SW_SHOWNA on Show() is what actually keeps the promise --
        // it shows without activating. Focus is then available to
        // anyone who deliberately clicks on the window, which is the
        // only way it should ever be taken.
        m_hwnd = CreateWindowExW(
            0,
            L"RadukShareView", L"RadukCapture — View",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX | WS_MAXIMIZEBOX,
            x, y, w, h,
            parent, nullptr, GetModuleHandleW(nullptr), this);
        if (!m_hwnd) return false;

        if (!InitD2D()) { DestroyWindow(m_hwnd); m_hwnd = nullptr; return false; }

        ShowWindow(m_hwnd, SW_HIDE);     // hidden until Shift+V
        return true;
    }

    void Destroy() {
        ReleaseD2D();
        if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    }

    HWND Handle()      const { return m_hwnd; }
    bool IsVisible()   const { return m_hwnd && IsWindowVisible(m_hwnd); }
    bool IsFullscreen() const { return m_full; }

    void Show() {
        if (!m_hwnd) return;
        // SW_SHOWNA, not SW_SHOW: shows without activating, which is
        // what keeps focus on the control screen.
        ShowWindow(m_hwnd, SW_SHOWNA);
        // Only fill the screen if that is the state it was left in.
        // Filling unconditionally meant F11 could put it in a window
        // and the next Shift+V would undo that -- so a view that had
        // been made movable could not be kept movable.
        if (m_wantFull) FillMonitor();
        m_lastPresent = {};              // present the next frame at once
        m_hintUntil = std::chrono::steady_clock::now()
                    + std::chrono::seconds(8);
        Redraw();
    }
    void Hide()   { if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE); }
    void Toggle() { IsVisible() ? Hide() : Show(); }

    void SetCaption(const Caption& c) { m_cap = c; }

    // Presentation rate. 30 is the default and is already generous for
    // something being watched from across a lawn.
    void SetMaxFps(double fps) { m_maxFps = (fps > 1.0) ? fps : 1.0; }

    // ── The frame sink ─────────────────────────────────────────
    //
    // Called from inside PreviewRenderer::RenderFrame with the finished
    // RGBA, immediately before the preview uploads the same bytes to
    // its own bitmap. Nothing is copied here beyond the upload itself:
    // the pointer is valid only for the duration of this call, which is
    // why the work happens now rather than being queued.
    //
    // Returns immediately when the window is hidden or when the cap has
    // not elapsed, which is the whole of the cost on an ordinary night.
    void Present(const uint8_t* rgba, int w, int h) {
        if (!rgba || w <= 0 || h <= 0) return;
        if (!m_hwnd || !IsWindowVisible(m_hwnd)) return;
        if (!m_rt) return;

        const auto now = std::chrono::steady_clock::now();
        if (m_lastPresent.time_since_epoch().count() != 0) {
            const double since = std::chrono::duration<double>(
                                     now - m_lastPresent).count();
            if (since < 1.0 / m_maxFps) return;
        }
        m_lastPresent = now;

        if (!EnsureBitmap(w, h)) return;
        D2D1_RECT_U src = D2D1::RectU(0, 0, (UINT32)w, (UINT32)h);
        if (FAILED(m_bitmap->CopyFromMemory(&src, rgba, (UINT32)w * 4))) return;
        m_haveFrame = true;

        Draw();
    }

    // Repaint with whatever frame is already uploaded. Used by WM_PAINT
    // and after a resize, so the window is not black between frames --
    // which on a paused or disconnected camera would be the whole time.
    void Redraw() { if (m_rt && m_hwnd && IsWindowVisible(m_hwnd)) Draw(); }

    // Borderless over the whole monitor the window is currently on.
    // Called on Show(), and by F11.
    void FillMonitor() {
        if (!m_hwnd || m_full) return;
        m_savedPlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(m_hwnd, &m_savedPlacement);

        MONITORINFO mi = { sizeof(mi) };
        if (!GetMonitorInfo(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi))
            return;

        // rcMonitor, not rcWork: a taskbar across the bottom of a screen
        // that is showing the planet to a crowd is not wanted.
        SetWindowLongW(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(m_hwnd, nullptr,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right  - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        m_full = true;
    }

    void RestoreWindowed() {
        if (!m_hwnd || !m_full) return;
        SetWindowLongW(m_hwnd, GWL_STYLE,
                       WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX |
                       WS_MAXIMIZEBOX | WS_VISIBLE);
        SetWindowPlacement(m_hwnd, &m_savedPlacement);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        m_full = false;
    }

    // m_wantFull is what the owner last asked for, and survives hiding
    // and reshowing; m_full is what the window currently is.
    void ToggleFullscreen() {
        m_wantFull = !m_full;
        m_full ? RestoreWindowed() : FillMonitor();
        // Deliberate, and the one place focus is taken: the owner has
        // just asked for this window specifically. Without it, F11
        // pressed from the control screen would restore a window that
        // still could not receive the next key.
        if (m_hwnd && IsWindowVisible(m_hwnd)) SetForegroundWindow(m_hwnd);
        // Going fullscreen is the moment the caption and the close box
        // disappear, so it is the moment to say how to get them back.
        if (m_full) ShowHint(); else Redraw();
    }

    // Fullscreen without the caption, and not focused, is a window with
    // no visible way out. The hint says how, then gets out of the way
    // so the screen is clean for the people it is pointed at.
    void ShowHint() {
        m_hintUntil = std::chrono::steady_clock::now()
                    + std::chrono::seconds(8);
        Redraw();
    }

private:
    // ── D2D ────────────────────────────────────────────────────
    bool InitD2D() {
        ID2D1Factory* factory = nullptr;
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory)))
            return false;

        RECT rc; GetClientRect(m_hwnd, &rc);
        m_w = rc.right; m_h = rc.bottom;

        HRESULT hr = factory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(m_hwnd, D2D1::SizeU(m_w, m_h)),
            &m_rt);
        factory->Release();
        if (FAILED(hr)) return false;

        IDWriteFactory* dw = nullptr;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                            __uuidof(IDWriteFactory), (IUnknown**)&dw);
        if (dw) { m_dwrite = dw; MakeFonts(); }
        return true;
    }

    void ReleaseD2D() {
        if (m_bigFormat)   { m_bigFormat->Release();   m_bigFormat   = nullptr; }
        if (m_smallFormat) { m_smallFormat->Release(); m_smallFormat = nullptr; }
        if (m_bitmap)      { m_bitmap->Release();      m_bitmap      = nullptr; }
        if (m_rt)          { m_rt->Release();          m_rt          = nullptr; }
        // Last: MakeFonts() creates the formats through it and must
        // never see it null while they are being rebuilt. The sky map
        // crashed on every launch for exactly this ordering.
        if (m_dwrite)      { m_dwrite->Release();      m_dwrite      = nullptr; }
        m_haveFrame = false;
        m_bmpW = m_bmpH = 0;
        m_fontPx = 0.0f;
    }

    // Caption text sized from the window, because this one is read from
    // across a room rather than from a chair. Off height, and generous:
    // the target name is the single thing a bystander wants.
    void MakeFonts() {
        if (!m_dwrite) return;
        const float px = (std::max)(18.0f, (std::min)(72.0f, (float)m_h / 18.0f));
        if (m_bigFormat && px == m_fontPx) return;
        m_fontPx = px;

        if (m_bigFormat)   { m_bigFormat->Release();   m_bigFormat   = nullptr; }
        if (m_smallFormat) { m_smallFormat->Release(); m_smallFormat = nullptr; }

        m_dwrite->CreateTextFormat(L"Barlow Condensed", nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, px, L"en-us", &m_bigFormat);
        m_dwrite->CreateTextFormat(L"Space Mono", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, px * 0.42f, L"en-us", &m_smallFormat);
    }

    bool EnsureBitmap(int w, int h) {
        if (!m_rt) return false;
        if (m_bitmap && w == m_bmpW && h == m_bmpH) return true;
        if (m_bitmap) { m_bitmap->Release(); m_bitmap = nullptr; }
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        if (FAILED(m_rt->CreateBitmap(D2D1::SizeU((UINT32)w, (UINT32)h),
                                      props, &m_bitmap))) {
            m_bitmap = nullptr;
            m_bmpW = m_bmpH = 0;
            return false;
        }
        m_bmpW = w; m_bmpH = h;
        m_haveFrame = false;             // nothing in the new bitmap yet
        return true;
    }

    // Letterboxed, never stretched. A planet with the wrong aspect
    // ratio is not a planet, and nobody in the audience knows to
    // discount it.
    D2D1_RECT_F FitRect() const {
        const float vw = (float)m_w, vh = (float)m_h;
        const float iw = (float)m_bmpW, ih = (float)m_bmpH;
        if (iw <= 0.0f || ih <= 0.0f) return D2D1::RectF(0, 0, vw, vh);
        const float s = (std::min)(vw / iw, vh / ih);
        const float dw = iw * s, dh = ih * s;
        const float ox = (vw - dw) * 0.5f, oy = (vh - dh) * 0.5f;
        return D2D1::RectF(ox, oy, ox + dw, oy + dh);
    }

    void Draw() {
        if (!m_rt) return;
        m_rt->BeginDraw();
        m_rt->Clear(Theme::NVf(D2D1::ColorF(0.0f, 0.0f, 0.0f)));

        if (m_haveFrame && m_bitmap) {
            // The image itself is NOT passed through Theme::NVf. Night
            // vision tints the chrome; tinting the sensor data would be
            // showing people a false colour of the planet, and the main
            // preview does not do it either.
            m_rt->DrawBitmap(m_bitmap, FitRect(), 1.0f,
                             D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            DrawCentredNotice(L"waiting for the camera");
        }

        if (m_showCaption) DrawCaption();
        DrawHint();

        HRESULT hr = m_rt->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) { ReleaseD2D(); InitD2D(); }
    }

    // Top-left, dim, and only for the first few seconds after the view
    // is shown or its fullscreen state changes. A borderless window
    // with no caption and no focus gives no clue how to leave it, and
    // the answer has to be on the window itself -- the owner reading
    // it is standing at a different screen from the help file.
    void DrawHint() {
        if (!m_full || !m_smallFormat) return;
        const auto now = std::chrono::steady_clock::now();
        if (now >= m_hintUntil) return;

        const double left = std::chrono::duration<double>(
                                m_hintUntil - now).count();
        const float a = (float)((left < 1.5) ? left / 1.5 : 1.0) * 0.6f;

        ID2D1SolidColorBrush* b = nullptr;
        m_rt->CreateSolidColorBrush(
            Theme::NVf(D2D1::ColorF(0.60f, 0.75f, 0.90f, a)), &b);
        if (!b) return;
        const wchar_t* t = L"Ctrl+Shift+V on the control screen for a window"
                           L"  ·  Shift+V closes it"
                           L"  ·  or click here, then F11 / C";
        const float pad = (std::max)(24.0f, (float)m_w / 60.0f);
        m_smallFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_rt->DrawText(t, (UINT32)wcslen(t), m_smallFormat,
                       D2D1::RectF(pad, pad, (float)m_w - pad,
                                   pad + m_fontPx), b);
        b->Release();
    }

    void DrawCentredNotice(const wchar_t* text) {
        if (!m_smallFormat) return;
        ID2D1SolidColorBrush* b = nullptr;
        m_rt->CreateSolidColorBrush(
            Theme::NVf(D2D1::ColorF(0.35f, 0.45f, 0.55f, 0.9f)), &b);
        if (!b) return;
        m_smallFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawText(text, (UINT32)wcslen(text), m_smallFormat,
                       D2D1::RectF(0, (float)m_h * 0.5f - 20.0f,
                                   (float)m_w, (float)m_h * 0.5f + 20.0f), b);
        m_smallFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        b->Release();
    }

    void DrawCaption() {
        if (!m_bigFormat || !m_smallFormat) return;

        wchar_t target[96] = L"";
        if (!m_cap.target.empty())
            MultiByteToWideChar(CP_UTF8, 0, m_cap.target.c_str(), -1, target, 96);

        // One quiet line of what it is and how it is being taken. The
        // rig string first, because at a star party the second question
        // is always what the scope is.
        wchar_t rig[96] = L"";
        if (!m_cap.rig.empty())
            MultiByteToWideChar(CP_UTF8, 0, m_cap.rig.c_str(), -1, rig, 96);

        wchar_t line[256];
        int len = swprintf(line, 256, L"%s", rig);
        if (len < 0) len = 0;
        if (m_cap.exposureMs > 0.0)
            len += swprintf(line + len, 256 - len, L"%s%.1f ms",
                            len ? L"   ·   " : L"", m_cap.exposureMs);
        if (m_cap.gain > 0)
            len += swprintf(line + len, 256 - len, L"%sgain %d",
                            len ? L"   ·   " : L"", m_cap.gain);
        if (m_cap.fps > 0.0)
            len += swprintf(line + len, 256 - len, L"%s%.0f fps",
                            len ? L"   ·   " : L"", m_cap.fps);
        if (m_cap.haveAltitude)
            len += swprintf(line + len, 256 - len, L"%s%.0f° up",
                            len ? L"   ·   " : L"", m_cap.altitudeDeg);

        const float pad   = (std::max)(24.0f, (float)m_w / 60.0f);
        const float bigH  = m_fontPx * 1.25f;
        const float smlH  = m_fontPx * 0.42f * 1.6f;
        float y = (float)m_h - pad - smlH - bigH;

        ID2D1SolidColorBrush* b = nullptr;

        if (target[0]) {
            m_rt->CreateSolidColorBrush(
                Theme::NVf(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.92f)), &b);
            if (b) {
                m_bigFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                m_rt->DrawText(target, (UINT32)wcslen(target), m_bigFormat,
                               D2D1::RectF(pad, y, (float)m_w - pad, y + bigH), b);
                b->Release(); b = nullptr;
            }
        }
        y += bigH;

        if (line[0]) {
            m_rt->CreateSolidColorBrush(
                Theme::NVf(D2D1::ColorF(0.55f, 0.70f, 0.85f, 0.85f)), &b);
            if (b) {
                m_smallFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                m_rt->DrawText(line, (UINT32)wcslen(line), m_smallFormat,
                               D2D1::RectF(pad, y, (float)m_w - pad, y + smlH), b);
                b->Release(); b = nullptr;
            }
        }

        // A recording light, top-right. Not a diagnostic -- it is there
        // so nobody has to ask whether this one is being kept.
        if (m_cap.recording) {
            const float r = (std::max)(8.0f, m_fontPx * 0.22f);
            const float cxp = (float)m_w - pad - r;
            const float cyp = pad + r;
            m_rt->CreateSolidColorBrush(
                Theme::NVf(D2D1::ColorF(1.0f, 0.15f, 0.25f, 0.95f)), &b);
            if (b) {
                m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cxp, cyp), r, r), b);
                m_smallFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                m_rt->DrawText(L"REC", 3, m_smallFormat,
                               D2D1::RectF(pad, cyp - smlH * 0.5f,
                                           cxp - r - 10.0f, cyp + smlH * 0.5f), b);
                m_smallFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                b->Release();
            }
        }
    }

    void OnResize(int w, int h) {
        m_w = w; m_h = h;
        if (m_rt) m_rt->Resize(D2D1::SizeU(w, h));
        MakeFonts();
        Redraw();
    }

    // ── Window proc ────────────────────────────────────────────
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        ShareView* self = reinterpret_cast<ShareView*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

        switch (msg) {
            case WM_SIZE:
                if (wp != SIZE_MINIMIZED)
                    self->OnResize(LOWORD(lp), HIWORD(lp));
                return 0;

            case WM_PAINT: {
                PAINTSTRUCT ps;
                BeginPaint(hwnd, &ps);
                self->Draw();
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_ERASEBKGND: return 1;

            // A click is how this window gets focus, so it is also the
            // moment to say what the keys are -- and to make sure the
            // click that took focus actually raised it.
            case WM_LBUTTONDOWN:
                SetForegroundWindow(hwnd);
                self->ShowHint();
                return 0;

            // ── Claiming our own keys ─────────────────────────
            //
            // Window::RouteThreadKey intercepts every key message in
            // the thread before dispatch, so that the app's hotkeys
            // work whichever window holds focus. That is deliberate and
            // right -- but it means a secondary window's own key
            // handler never runs. F11 pressed here was going to the
            // MAIN window's fullscreen toggle, which is how the share
            // view ended up unable to leave fullscreen.
            //
            // The escape hatch already exists and is the one edit boxes
            // use: answer WM_GETDLGCODE. Claim ONLY the two keys this
            // window owns, so every other hotkey still works from here.
            //
            // Escape is deliberately NOT claimed. It is the emergency
            // stop, it must reach the mount from every window, and an
            // emergency stop conditional on which window has focus is
            // not one. F11 toggles both ways, so nothing is lost.
            case WM_GETDLGCODE:
                if (lp && (wp == VK_F11 || wp == 'C'))
                    return DLGC_WANTMESSAGE;
                return 0;

            // These arrive once the window has been clicked. That is
            // the only way it ever takes focus, and the reason the
            // hint's first words are "click here first".
            case WM_KEYDOWN:
                if (wp == VK_F11) { self->ToggleFullscreen(); return 0; }
                if (wp == 'C') {
                    self->m_showCaption = !self->m_showCaption;
                    self->Redraw();
                    return 0;
                }
                break;

            // Closing it is hiding it. Destroying would take the D2D
            // target with it and leave the sink pointing at nothing.
            case WM_CLOSE:
                ShowWindow(hwnd, SW_HIDE);
                return 0;

            case WM_DESTROY:
                self->m_hwnd = nullptr;
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    HWND                   m_hwnd   = nullptr;
    ID2D1HwndRenderTarget* m_rt     = nullptr;
    IDWriteFactory*        m_dwrite = nullptr;
    IDWriteTextFormat*     m_bigFormat   = nullptr;
    IDWriteTextFormat*     m_smallFormat = nullptr;
    ID2D1Bitmap*           m_bitmap = nullptr;

    int   m_w = 0, m_h = 0;
    int   m_bmpW = 0, m_bmpH = 0;
    bool  m_haveFrame   = false;
    bool  m_showCaption = true;
    bool  m_full        = false;   // what the window is
    bool  m_wantFull    = true;    // what the owner last asked for
    float m_fontPx      = 0.0f;
    double m_maxFps     = 30.0;

    Caption          m_cap;
    WINDOWPLACEMENT  m_savedPlacement = { sizeof(WINDOWPLACEMENT) };
    std::chrono::steady_clock::time_point m_lastPresent{};
    std::chrono::steady_clock::time_point m_hintUntil{};
};

} // namespace raduk::gui
