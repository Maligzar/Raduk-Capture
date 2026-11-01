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
#include <cmath>
#include <vector>
#include <chrono>
#include <algorithm>
#include "Theme.h"
#include "AudienceLayout.h"

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
                    + std::chrono::milliseconds(Theme::Motion::DWELL_NOTICE);
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
                    + std::chrono::milliseconds(Theme::Motion::DWELL_NOTICE);
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
        if (m_bigFit)      { m_bigFit->Release();      m_bigFit      = nullptr; }
        m_bigFitPx = 0.0f;
        if (m_smallFormat) { m_smallFormat->Release(); m_smallFormat = nullptr; }
        if (m_microFormat) { m_microFormat->Release(); m_microFormat = nullptr; }
        if (m_bitmap)      { m_bitmap->Release();      m_bitmap      = nullptr; }
        if (m_rt)          { m_rt->Release();          m_rt          = nullptr; }
        // Last: MakeFonts() creates the formats through it and must
        // never see it null while they are being rebuilt. The sky map
        // crashed on every launch for exactly this ordering.
        if (m_dwrite)      { m_dwrite->Release();      m_dwrite      = nullptr; }
        m_haveFrame = false;
        m_bmpW = m_bmpH = 0;
        m_fontPx = 0.0f;
        m_bodyPx = 0.0f;
    }

    // Caption text sized from the window, because this one is read from
    // across a room rather than from a chair. Off height, and generous:
    // the target name is the single thing a bystander wants.
    //
    // ── Phase 96.3: the ramp, anchored on the tuned size ────
    //
    // The target name at h/18 was arrived at by eye and it reads
    // correctly across a lawn, so it is kept and everything else is
    // derived BACK from it through Theme::Type. Starting from a body
    // size instead would have thrown away a real measurement to make
    // the arithmetic tidier.
    //
    // What that replaces is the 0.42 below it, which was this
    // window's own invented ratio. The object panel -- same room,
    // same audience, same night -- had invented 0.52 for the same
    // job, and neither knew about the other.
    // A caption format at a size chosen to fit, cached so a steady
    // target does not rebuild it every frame.
    IDWriteTextFormat* BigAt(float px) {
        if (px >= m_fontPx - 0.5f) return m_bigFormat;
        if (m_bigFit && std::fabs(px - m_bigFitPx) < 0.5f) return m_bigFit;
        if (m_bigFit) { m_bigFit->Release(); m_bigFit = nullptr; }
        if (!m_dwrite) return m_bigFormat;
        m_dwrite->CreateTextFormat(Theme::FONT_LABEL, nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, px, L"en-us", &m_bigFit);
        m_bigFitPx = px;
        return m_bigFit ? m_bigFit : m_bigFormat;
    }

    void MakeFonts() {
        if (!m_dwrite) return;
        const float px = (std::max)(28.0f, (std::min)(72.0f, (float)m_h / 18.0f));
        if (m_bigFormat && px == m_fontPx) return;
        m_fontPx = px;
        // Floored at the ramp's anchor. Below a body size of
        // Type::BODY the scaled steps compress until adjacent ones
        // are under MIN_STEP apart, which is the one-pixel-apart
        // defect 96.2 fixed reappearing by arithmetic rather than by
        // choice. Dragged that small the big type will clip, and
        // clipped type in a window nobody is using at 300 pixels
        // tall beats seven-pixel type on one that is.
        m_bodyPx = (std::max)((float)Theme::Type::BODY,
                   Theme::Type::BodyForStep(px, Theme::Type::S_HERO));

        if (m_bigFormat)   { m_bigFormat->Release();   m_bigFormat   = nullptr; }
        if (m_bigFit)      { m_bigFit->Release();      m_bigFit      = nullptr; }
        m_bigFitPx = 0.0f;
        if (m_smallFormat) { m_smallFormat->Release(); m_smallFormat = nullptr; }
        if (m_microFormat) { m_microFormat->Release(); m_microFormat = nullptr; }

        m_dwrite->CreateTextFormat(Theme::FONT_LABEL, nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, px, L"en-us", &m_bigFormat);
        m_dwrite->CreateTextFormat(Theme::FONT_DATA, nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            Theme::Type::Scaled(m_bodyPx, Theme::Type::S_BODY),
            L"en-us", &m_smallFormat);
        // The hint and the REC label are the two things on here that
        // are NOT for the audience -- one is instructions for the
        // owner, the other is a state light. They take the smallest
        // step so they stay out of the photograph.
        m_dwrite->CreateTextFormat(Theme::FONT_DATA, nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            Theme::Type::Scaled(m_bodyPx, Theme::Type::S_MICRO),
            L"en-us", &m_microFormat);
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
    //
    // The arithmetic moved to audience::LetterboxFit so a test can
    // reach it: everything the brackets and the scrim do is
    // positioned against this rectangle, and a test that cannot see
    // it is checking its own guess at where the image is.
    audience::LRect FitLRect() const {
        return audience::LetterboxFit((float)m_w, (float)m_h,
                                      (float)m_bmpW, (float)m_bmpH);
    }
    static D2D1_RECT_F R(const audience::LRect& k) {
        return D2D1::RectF(k.l, k.t, k.r, k.b);
    }
    D2D1_RECT_F FitRect() const { return R(FitLRect()); }

    void Draw() {
        if (!m_rt) return;
        m_rt->BeginDraw();
        m_rt->Clear(Theme::NVf(D2D1::ColorF(0.0f, 0.0f, 0.0f)));

        if (m_haveFrame && m_bitmap) {
            // The image itself is NOT passed through Theme::NVf. Night
            // vision tints the chrome; tinting the sensor data would be
            // showing people a false colour of the planet, and the main
            // preview does not do it either.
            const audience::LRect fit = FitLRect();
            m_rt->DrawBitmap(m_bitmap, R(fit), 1.0f,
                             D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            DrawBrackets(fit);
        } else {
            DrawCentredNotice(L"waiting for the camera");
        }

        if (m_showCaption) DrawCaption();
        DrawHint();

        HRESULT hr = m_rt->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) { ReleaseD2D(); InitD2D(); }
    }

    // ── Where the sensor frame actually ends ─────────────────
    //
    // Four corner marks on the image rectangle. The one bit of the
    // reference clients' framing that both survives night vision --
    // it is a shape, and a shape has no hue to lose -- and does real
    // work here.
    //
    // The work: a letterboxed planetary frame is a dark image
    // centred in a black field, so from three metres back there is
    // no visible edge and a bystander cannot tell the whole frame
    // from a crop of it. These say where it stops. On a frame that
    // already fills the window they land on the window edge and cost
    // nothing.
    void DrawBrackets(const audience::LRect& fit) {
        const float arm = audience::BracketArm(fit, m_bodyPx);
        if (arm <= 0.0f) return;              // too small to frame

        // The accent TOKEN, not a literal cyan. Every colour on this
        // window was a literal before 96.3, which meant 96.1's night
        // ladder -- the whole point of which was that danger must be
        // the brightest thing the UI paints -- did not apply to the
        // one surface other people look at.
        D2D1_COLOR_F c = Theme::Color(Theme::ACCENT);
        c.a = 0.55f;
        ID2D1SolidColorBrush* b = nullptr;
        if (FAILED(m_rt->CreateSolidColorBrush(c, &b)) || !b) return;

        const float wgt = (std::max)(1.5f, m_bodyPx * 0.09f);
        // Half a stroke in from the image edge, so the mark sits ON
        // the boundary rather than outside it.
        const audience::LRect k = audience::Inset(fit, wgt * 0.5f);

        struct Corner { float x, y, dx, dy; };
        const Corner corners[4] = {
            { k.l, k.t,  1.0f,  1.0f },   // top-left
            { k.r, k.t, -1.0f,  1.0f },   // top-right
            { k.l, k.b,  1.0f, -1.0f },   // bottom-left
            { k.r, k.b, -1.0f, -1.0f },   // bottom-right
        };
        for (const Corner& q : corners) {
            m_rt->DrawLine(D2D1::Point2F(q.x, q.y),
                           D2D1::Point2F(q.x + q.dx * arm, q.y), b, wgt);
            m_rt->DrawLine(D2D1::Point2F(q.x, q.y),
                           D2D1::Point2F(q.x, q.y + q.dy * arm), b, wgt);
        }
        b->Release();
    }

    // ── The caption sits on something ────────────────────────
    //
    // White at 92% over a letterboxed Jupiter is white on black and
    // reads perfectly. Over a full-frame Moon, or any target whose
    // bright limb reaches the bottom of the sensor, it is white on
    // white. A gradient scrim fixes that, and it is the clearest
    // example of why depth was the axis to build this look from: a
    // scrim is alpha over whatever is beneath it, and NV() does not
    // touch alpha, so it reads identically in both modes where a
    // colour treatment would read in neither.
    void DrawScrim(float captionTop) {
        const audience::LRect band =
            audience::CaptionScrim((float)m_w, (float)m_h,
                                   captionTop, m_bodyPx);
        if (audience::Height(band) <= 0.0f) return;

        ID2D1GradientStopCollection* stops = nullptr;
        D2D1_GRADIENT_STOP gs[2];
        gs[0].position = 0.0f;                 // top: gone
        gs[0].color    = D2D1::ColorF(0, 0, 0, 0.0f);
        gs[1].position = 1.0f;                 // bottom: carrying text
        gs[1].color    = D2D1::ColorF(0, 0, 0, audience::SCRIM_PEAK_ALPHA);
        // Black at varying alpha, deliberately NOT through NVf. The
        // transform maps every colour to a red of the same luminance,
        // and black is luminance zero either way -- but routing it
        // through would put a red cast on a scrim whose entire job is
        // to be the absence of one.
        if (FAILED(m_rt->CreateGradientStopCollection(gs, 2, &stops)) || !stops)
            return;
        ID2D1LinearGradientBrush* lg = nullptr;
        if (SUCCEEDED(m_rt->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(
                    D2D1::Point2F(0.0f, band.t),
                    D2D1::Point2F(0.0f, band.b)),
                stops, &lg)) && lg) {
            m_rt->FillRectangle(R(band), lg);
            lg->Release();
        }
        stops->Release();
    }

    // Top-left, dim, and only for the first few seconds after the view
    // is shown or its fullscreen state changes. A borderless window
    // with no caption and no focus gives no clue how to leave it, and
    // the answer has to be on the window itself -- the owner reading
    // it is standing at a different screen from the help file.
    void DrawHint() {
        if (!m_full || !m_microFormat) return;
        const auto now = std::chrono::steady_clock::now();
        if (now >= m_hintUntil) return;

        // Phase 96.5: the same curve ObjectPanel fades in on, read
        // from the other end. Was a linear ramp, which held the hint
        // at near-full apparent lightness and then dropped it in the
        // last instant; squared, it reads as leaving.
        const double leftMs = std::chrono::duration<double, std::milli>(
                                  m_hintUntil - now).count();
        const float a = Theme::Motion::FadeOut(leftMs, Theme::Motion::LONG)
                      * HINT_PEAK_ALPHA;

        D2D1_COLOR_F c = Theme::Color(Theme::TEXT_DIM);
        c.a = a;
        ID2D1SolidColorBrush* b = nullptr;
        m_rt->CreateSolidColorBrush(c, &b);
        if (!b) return;
        const wchar_t* t = L"Ctrl+Shift+V on the control screen for a window"
                           L"  ·  Shift+V closes it"
                           L"  ·  or click here, then F11 / C";
        const float pad = Pad();
        m_microFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_rt->DrawText(t, (UINT32)wcslen(t), m_microFormat,
                       D2D1::RectF(pad, pad, (float)m_w - pad,
                                   pad + m_fontPx), b);
        b->Release();
    }

    void DrawCentredNotice(const wchar_t* text) {
        if (!m_smallFormat) return;
        D2D1_COLOR_F c = Theme::Color(Theme::TEXT_DIM);
        c.a = 0.75f;
        ID2D1SolidColorBrush* b = nullptr;
        m_rt->CreateSolidColorBrush(c, &b);
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

        const std::wstring targetFull = audience::Wide(m_cap.target);

        // One quiet line of what it is and how it is being taken. The
        // rig string first, because at a star party the second question
        // is always what the scope is.
        const std::wstring rigStr = audience::Wide(m_cap.rig);
        const wchar_t* rig = rigStr.c_str();

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

        const float pad   = Pad();
        const float bigH  = m_fontPx * 1.25f;
        const float smlH  = Theme::Type::Scaled(m_bodyPx, Theme::Type::S_BODY)
                          * 1.6f;
        const float top = (float)m_h - pad - smlH - bigH;
        float y = top;

        // Depth before text, so the caption is on a surface rather
        // than on the planet.
        DrawScrim(top);

        ID2D1SolidColorBrush* b = nullptr;

        if (!targetFull.empty()) {
            // Phase 104. This block is exactly two lines tall by
            // construction -- top is m_h - pad - smlH - bigH -- and
            // the scrim behind it is sized from that same top. So a
            // target name that wraps does three wrong things at once:
            // its second line lands on the detail line below, it
            // leaves the scrim that is meant to back it, and it runs
            // at the bottom edge of the screen.
            //
            // Nothing stopped it. No SetWordWrapping call exists in
            // this file, so DWrite's default applies and the default
            // wraps; and a layout rect does not clip, it only sets
            // where the wrapping happens.
            //
            // So: shrink to fit one line, and when the floor is
            // reached, shorten with an ellipsis. The name stays on
            // one line either way, which is what the geometry and the
            // scrim both already assumed.
            const float availW = (float)m_w - pad * 2.0f;
            // FitPxOneLine, not FitPx: the constraint here is width,
            // because the block is one line tall by construction and
            // a second line lands on the detail line below.
            const float fitPx  = audience::FitPxOneLine(
                targetFull.size(), availW, m_fontPx,
                audience::DISPLAY_ADVANCE_EM);
            IDWriteTextFormat* bigf = BigAt(fitPx);
            const std::wstring shown = audience::Ellipsize(
                targetFull,
                audience::CharsThatFit(availW, fitPx,
                                       audience::DISPLAY_ADVANCE_EM));

            D2D1_COLOR_F c = Theme::Color(Theme::TEXT);
            c.a = 0.95f;
            m_rt->CreateSolidColorBrush(c, &b);
            if (b && bigf) {
                bigf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                m_rt->DrawText(shown.c_str(), (UINT32)shown.size(), bigf,
                               D2D1::RectF(pad, y, (float)m_w - pad, y + bigH),
                               b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            if (b) { b->Release(); b = nullptr; }
        }
        y += bigH;

        if (line[0]) {
            // TEXT_DIM, and this is the measurement that made 96.3
            // more than a tidy-up. As a literal (0.55, 0.70, 0.85)
            // this line derived to a night luminance of 172. The
            // ladder puts TEXT_DIM -- labels you are MEANT to skim --
            // at 80. It was painting at better than twice the
            // brightness its own role allows, on the surface aimed
            // at people who are not reading it.
            D2D1_COLOR_F c = Theme::Color(Theme::TEXT_DIM);
            c.a = 0.90f;
            m_rt->CreateSolidColorBrush(c, &b);
            if (b) {
                m_smallFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                m_rt->DrawText(line, (UINT32)wcslen(line), m_smallFormat,
                               D2D1::RectF(pad, y, (float)m_w - pad, y + smlH),
                               b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                b->Release(); b = nullptr;
            }
        }

        // ── The recording light ─────────────────────────────
        //
        // Not a diagnostic. It is there so nobody has to ask whether
        // this one is being kept.
        //
        // It is also the sharpest case of what 96.3 is fixing. The
        // literal (1.0, 0.15, 0.25) derives to a night luminance of
        // 86. ACCENT4 -- the same red, as a token -- is 240, because
        // 96.1 put danger at the top of the ladder on the grounds
        // that the thing which must shout cannot be the quietest
        // thing on screen. That fix landed on every operator surface
        // and reached none of this one, because this one painted
        // literals. In night vision the recording light was dimmer
        // than the caption beneath it.
        //
        // A capsule rather than a bare dot and some text, at OVERLAY
        // elevation: over a bright limb the old dot had nothing
        // behind it either.
        if (m_cap.recording) DrawRecPill(pad);
    }

    void DrawRecPill(float pad) {
        if (!m_microFormat) return;

        const float r    = (std::max)(6.0f, m_bodyPx * 0.26f);
        const float padX = Theme::Space::Scaled(m_bodyPx, Theme::Space::P_SM);
        const float h    = r * 2.0f + padX;
        const float w    = padX * 2.0f + r * 2.0f +
                           Theme::Space::Scaled(m_bodyPx, Theme::Space::P_SM) +
                           m_bodyPx * 2.4f;          // room for "REC"
        const audience::LRect pill{ (float)m_w - pad - w, pad,
                                    (float)m_w - pad,     pad + h };
        const float rad = h * 0.5f;

        DrawElevation(pill, rad, Theme::Elev::OVERLAY);

        ID2D1SolidColorBrush* b = nullptr;
        D2D1_COLOR_F face = Theme::Color(Theme::PANEL);
        face.a = 0.82f;
        if (SUCCEEDED(m_rt->CreateSolidColorBrush(face, &b)) && b) {
            m_rt->FillRoundedRectangle(
                D2D1::RoundedRect(R(pill), rad, rad), b);
            b->Release(); b = nullptr;
        }

        D2D1_COLOR_F hot = Theme::Color(Theme::ACCENT4);
        if (FAILED(m_rt->CreateSolidColorBrush(hot, &b)) || !b) return;
        const float cx = pill.l + padX + r;
        const float cy = (pill.t + pill.b) * 0.5f;
        m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), b);
        m_microFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_rt->DrawText(L"REC", 3, m_microFormat,
                       D2D1::RectF(cx + r + padX * 0.75f, cy - m_bodyPx * 0.72f,
                                   pill.r - padX * 0.5f, cy + m_bodyPx * 0.72f), b);
        b->Release();
    }

    // ── Depth, without a blur to do it with ─────────────────
    //
    // A plain hwnd render target has no Gaussian blur -- that lives
    // on ID2D1DeviceContext, and rebuilding this window onto the
    // Direct2D 1.1 pipeline to soften one card edge would be a
    // disproportionate change to a window that works. So the shadow
    // is nested rings of flat fill, weighted to sum to the level's
    // stated alpha so Theme's ordering survives the approximation.
    void DrawElevation(const audience::LRect& card, float radius, int level) {
        const Theme::Shadow sh = Theme::ShadowFor(level);
        if (!audience::ShadowVisible(sh)) return;
        ID2D1SolidColorBrush* b = nullptr;
        if (FAILED(m_rt->CreateSolidColorBrush(
                D2D1::ColorF(0, 0, 0, 1.0f), &b)) || !b) return;
        for (int i = 0; i < audience::SHADOW_RINGS; ++i) {
            const audience::Ring g = audience::ShadowRingAt(card, sh, i);
            // Black at an alpha, like the scrim: not through NVf,
            // because a shadow is the absence of light and tinting it
            // red would make it a glow.
            b->SetColor(D2D1::ColorF(0, 0, 0, g.alpha));
            // The ring is wider than the card by its spread, so its
            // corner radius grows by the same amount or the outer
            // rings square off against a rounded card.
            const float gr = radius + (card.l - g.rect.l);
            m_rt->FillRoundedRectangle(
                D2D1::RoundedRect(R(g.rect), gr, gr), b);
        }
        b->Release();
    }

    // The window inset, on the shared rhythm rather than w/60.
    float Pad() const {
        return (std::max)(24.0f,
            Theme::Space::Scaled(m_bodyPx, Theme::Space::P_LG));
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
    IDWriteTextFormat*     m_bigFit      = nullptr;  // caption, fitted
    float                  m_bigFitPx    = 0.0f;
    IDWriteTextFormat*     m_smallFormat = nullptr;
    IDWriteTextFormat*     m_microFormat = nullptr;
    ID2D1Bitmap*           m_bitmap = nullptr;

    int   m_w = 0, m_h = 0;
    int   m_bmpW = 0, m_bmpH = 0;
    bool  m_haveFrame   = false;
    bool  m_showCaption = true;
    bool  m_full        = false;   // what the window is
    bool  m_wantFull    = true;    // what the owner last asked for
    float m_fontPx      = 0.0f;   // the HERO step, tuned by eye
    float m_bodyPx      = 0.0f;   // derived back through Theme::Type
    double m_maxFps     = 30.0;

    Caption          m_cap;
    WINDOWPLACEMENT  m_savedPlacement = { sizeof(WINDOWPLACEMENT) };
    std::chrono::steady_clock::time_point m_lastPresent{};
    // The hint never paints at full strength: it is a reminder laid
    // over someone else's picture, not a label.
    static constexpr float HINT_PEAK_ALPHA = 0.6f;
    std::chrono::steady_clock::time_point m_hintUntil{};
};

} // namespace raduk::gui
