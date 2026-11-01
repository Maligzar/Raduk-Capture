// ============================================================
//  ObjectPanel — what the thing is, on a screen other people read
//
//  Phase 92. The educational twin of the share view: that window is
//  the photograph, this one is the caption. Same room, same audience,
//  same rule -- the owner works on display 1 and everything else is
//  for other people.
//
//  THE DISCIPLINE, which matters more here than anywhere else in the
//  app: every element is a real measurement, a real fact, or a label.
//
//  Science-fiction interfaces are full of numbers that mean nothing --
//  scrolling hex, sweeping arcs, bars that animate because animation
//  looks busy. On a screen whose whole purpose is to teach somebody
//  something, that is worse than plain: it tells people the display is
//  decoration and they stop reading it. So nothing here is invented,
//  and a section with no content is NOT DRAWN AT ALL rather than shown
//  empty.
//
//  What the data can actually support, measured rather than assumed
//  (see docs/Phase92-Plan-Object-Panel.md §2a):
//
//    - distance_ly exists for NINE rows out of 14,520, and they are
//      exactly the Sun, the Moon and the seven planets. Light-travel
//      time is therefore a solar-system line, not a universal one --
//      which for a planetary rig is the best case rather than the
//      worst.
//    - size_arcmin covers 83%, so "how much of your frame does it
//      fill" is the line that works for nearly everything, and it is
//      the most useful one for an imager anyway.
//    - magnitude covers 79%.
//    - Named stars carry prose and almost no numbers; deep-sky rows
//      carry numbers and almost no prose. Neither layer is the
//      fallback. They cover for each other.
// ============================================================

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <algorithm>
#include "Theme.h"
#include "AudienceLayout.h"
#include "../core/ObjectKnowledgeDB.h"
#include "../sky/SkyObject.h"

namespace raduk::gui {

// ── The claims the panel makes, as free functions ─────────────
//
// These are the sentences a stranger reads off a screen and believes.
// They are out here rather than private to the class so that
// tests/object_panel_text.cpp can check them against values worked out
// by hand -- the same reason the histogram's numbers and the sky
// conditions are gated.
namespace objectpanel {

// Light travel time. Works for the nine solar-system rows and nothing
// else, which is exactly the set this rig points at.
inline std::string LightTravel(double ly) {
    if (ly <= 0.0) return "";
    const double minutes = ly * 365.25 * 24.0 * 60.0;
    char b[96];
    // One decimal under ten seconds. The Moon is 1.28 light-seconds,
    // and "1 seconds ago" is worse than useless on a screen people
    // are reading -- it was "0 seconds ago" before the database's
    // Moon distance was corrected, which is how the error was found.
    if (minutes * 60.0 < 10.0)
        snprintf(b, sizeof(b), "%.1f seconds ago", minutes * 60.0);
    else if (minutes < 1.0)   snprintf(b, sizeof(b), "%.0f seconds ago", minutes * 60.0);
    else if (minutes < 120.0) snprintf(b, sizeof(b), "%.0f minutes ago", minutes);
    else if (ly < 1.0)        snprintf(b, sizeof(b), "%.1f hours ago", minutes / 60.0);
    else if (ly < 1000.0)     snprintf(b, sizeof(b), "%.0f years ago", ly);
    else if (ly < 1.0e6)      snprintf(b, sizeof(b), "%.0f thousand years ago", ly / 1.0e3);
    // M31 is 2.54 million light-years, and "2541 thousand years ago"
    // is a number nobody says out loud.
    else if (ly < 1.0e9)      snprintf(b, sizeof(b), "%.1f million years ago", ly / 1.0e6);
    else                      snprintf(b, sizeof(b), "%.1f billion years ago", ly / 1.0e9);
    return b;
}

// How much of the sensor it covers, at tonight's plate scale and the
// live capture format. The line that works for 83% of the catalogue,
// and the most useful one for an imager.
inline std::string FrameFill(double sizeArcmin, double scaleArcsecPx,
                             int roiW, int roiH) {
    if (sizeArcmin <= 0.0 || scaleArcsecPx <= 0.0 || roiW <= 0 || roiH <= 0)
        return "";
    const double frameW = roiW * scaleArcsecPx / 60.0;   // arcmin
    const double frameH = roiH * scaleArcsecPx / 60.0;
    const double shorter = (frameW < frameH) ? frameW : frameH;
    // A whole clause, verb included. The caller prepends "It ", and
    // "It fills 30% of the frame" reads while "It 54.8 times wider
    // than the frame" does not -- which is what it said until
    // somebody read it back.
    char b[160];
    if (sizeArcmin > shorter)
        snprintf(b, sizeof(b), "is %.1f times wider than the frame at %dx%d",
                 sizeArcmin / shorter, roiW, roiH);
    else
        snprintf(b, sizeof(b), "fills %.0f%% of the frame at %dx%d",
                 100.0 * sizeArcmin / shorter, roiW, roiH);
    return b;
}

// Against the naked-eye limit, which is a comparison somebody standing
// in the dark can check for themselves.
inline std::string BrightnessLine(double mag) {
    const double EYE = 6.0;
    const double ratio = std::pow(2.512, EYE - mag);
    char b[160];
    if (mag <= EYE) {
        if (ratio < 2.0) return "about as faint as the eye can manage";
        snprintf(b, sizeof(b),
                 "%.0f times brighter than the faintest star the eye can see",
                 ratio);
    } else {
        snprintf(b, sizeof(b), "%.0f times too faint for the naked eye",
                 1.0 / ratio);
    }
    return b;
}

// The constellation column is not always a constellation.
//
// 408 of the 1001 named-star rows carry a MAGNITUDE here -- Achernar
// reads "0.45", Acrux "1.33" -- which is a column shift in the
// database builder, not something this panel can fix. What it can do
// is refuse to put it on a screen in front of people.
//
// "Various" and "N/A" are the catalogue's own placeholders for the
// solar system, and "in Various" under a planet's name is nonsense.
inline bool LooksLikeConstellation(const std::string& s) {
    if (s.empty() || s == "N/A" || s == "Various" || s == "various")
        return false;
    bool anyAlpha = false;
    for (char c : s) {
        if (c == '.') return false;              // "0.45"
        if (isalpha((unsigned char)c)) anyAlpha = true;
    }
    return anyAlpha;
}

// Catalogue type codes are not for reading aloud.
inline std::string TypeName(const std::string& t, const std::string& sub) {
    struct { const char* code; const char* name; } kMap[] = {
        {"Planet","Planet"}, {"Satellite","Moon"}, {"Star","Star"},
        {"Double Star","Double star"}, {"GCl","Globular cluster"},
        {"OCl","Open cluster"}, {"Cl+N","Cluster with nebulosity"},
        {"G","Galaxy"}, {"GPair","Pair of galaxies"},
        {"GTrpl","Triple galaxy"}, {"GGroup","Galaxy group"},
        {"Planetary Nebula","Planetary nebula"}, {"PN","Planetary nebula"},
        {"Neb","Nebula"}, {"HII","Emission nebula"},
        {"RfN","Reflection nebula"}, {"SNR","Supernova remnant"},
        {"DrkN","Dark nebula"}, {"*Ass","Stellar association"},
        {"Other","Deep-sky object"},
    };
    for (auto& e : kMap) if (t == e.code) return e.name;
    if (!sub.empty() && sub != t)
        for (auto& e : kMap) if (sub == e.code) return e.name;
    return t.empty() ? std::string() : t;
}


// ── Fitting body text to a box ─────────────────────────────────
//
// Phase 103. The object panel had an EstimateHeight member that was
// never called from anywhere, and whose comment ended "the text is
// clipped to the rect either way". That claim is false, and it is why
// Jupiter's narrative ran off the bottom of the window: a layout rect
// passed to ID2D1RenderTarget::DrawText governs wrapping and
// alignment, not clipping. Glyphs past the rect's height are still
// drawn unless D2D1_DRAW_TEXT_OPTIONS_CLIP is passed, and nothing
// passed it.
//
// So the panel had the function that would have prevented the overflow
// and the comment asserting the safety it did not have, and neither
// did anything. These are the arithmetic, pulled out where a test can
// reach them.
//
// An estimate, not a measurement: the body face is proportional, so
// this works from an average advance. That is the right trade for
// choosing a size -- a measured layout per candidate size would mean
// building and discarding a DWrite layout several times per frame to
// decide something a reader cannot perceive to the pixel. It errs on
// the side of predicting MORE height than the text takes, because
// being wrong that way shrinks the text slightly and being wrong the
// other way puts it through the bottom of the window.

// Phase 104: the implementation moved to namespace audience, where
// the share view could reach it too. These names stay because they
// read better at the object panel's call sites and because phase 103's
// test is written against them; they are one-line forwards, not a
// second copy of the arithmetic.
using audience::BODY_ADVANCE_EM;
using audience::BODY_LINE_EM;
using audience::BODY_FLOOR_PX;

inline float EstimatedLines(size_t chars, float width, float bodyPx) {
    return audience::EstimatedLines(chars, width, bodyPx);
}

inline float EstimatedHeight(size_t chars, float width, float bodyPx) {
    return audience::EstimatedHeight(chars, width, bodyPx);
}

inline float FitBodyPx(size_t chars, float width, float boxH,
                       float want, float floorPx = BODY_FLOOR_PX) {
    return audience::FitPx(chars, width, boxH, want,
                           audience::BODY_ADVANCE_EM, floorPx);
}

} // namespace objectpanel

class ObjectPanel {
public:
    ObjectPanel() = default;
    ~ObjectPanel() { Destroy(); }
    ObjectPanel(const ObjectPanel&)            = delete;
    ObjectPanel& operator=(const ObjectPanel&) = delete;

    // Everything the panel draws. Fed from the readout loop; the panel
    // does its own formatting and derivation so the main loop does not
    // grow another display's worth of string building.
    struct Content {
        std::string target;                 // what the app calls it
        raduk::core::ObjectKnowledgeDB::ObjectRecord rec;
        bool   haveAltitude  = false;
        double altitudeDeg   = 0.0;
        double airmass       = 0.0;
        double plateScaleArcsecPx = 0.203;  // live, from rig.ini
        int    roiWidth = 0, roiHeight = 0; // live capture format

        // Planets only. rec.sizeArcmin carries what has to FIT, which
        // for Saturn is the rings; these carry the globe, which is what
        // "apparent size" means in every book a visitor has read.
        double planetDiscArcmin = 0.0;
        bool   havePlanetDisc   = false;
        bool   ringsIncluded    = false;   // Saturn: the two differ
    };

    bool Create(HWND parent, int x, int y, int w, int h) {
        static bool s_registered = false;
        if (!s_registered) {
            WNDCLASSEXW wc = {};
            wc.cbSize        = sizeof(wc);
            wc.lpfnWndProc   = WndProc;
            wc.hInstance     = GetModuleHandleW(nullptr);
            wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = nullptr;
            wc.lpszClassName = L"RadukObjectPanel";
            RegisterClassExW(&wc);
            s_registered = true;
        }
        // No WS_EX_NOACTIVATE. Phase 89 learned that it makes a window
        // permanently unfocusable by any route, which on a borderless
        // fullscreen window leaves no way out at all. SW_SHOWNA is
        // what keeps focus on the control screen.
        m_hwnd = CreateWindowExW(
            0, L"RadukObjectPanel", L"RadukCapture — Object",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX | WS_MAXIMIZEBOX,
            x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), this);
        if (!m_hwnd) return false;
        if (!InitD2D()) { DestroyWindow(m_hwnd); m_hwnd = nullptr; return false; }
        ShowWindow(m_hwnd, SW_HIDE);
        return true;
    }

    void Destroy() {
        ReleaseD2D();
        if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    }

    HWND Handle()       const { return m_hwnd; }
    bool IsVisible()    const { return m_hwnd && IsWindowVisible(m_hwnd); }
    bool IsFullscreen() const { return m_full; }

    void Show() {
        if (!m_hwnd) return;
        ShowWindow(m_hwnd, SW_SHOWNA);
        if (m_wantFull) FillMonitor();
        Redraw();
    }
    void Hide()   { if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE); }
    void Toggle() { IsVisible() ? Hide() : Show(); }

    // A new target starts a cross-fade. Identical content is ignored,
    // so this is safe to call every readout tick.
    // ── A planet's size is a function of tonight, not a column ──
    //
    // size_arcmin is NULL for every planet in the knowledge database,
    // so this panel's two best lines -- the APPARENT SIZE figure and
    // the "does it fit in your frame" sentence -- were silently absent
    // for Jupiter, Saturn and Mars. For the objects this rig actually
    // points at, the education panel had nothing to say about size.
    //
    // The database is right to hold no value. Mars runs 3.5" to 25"
    // and back over its synodic cycle; the ephemeris here reproduces
    // that 7x swing. So it is computed from the Earth-planet distance,
    // which PlanetRADec was already working out and throwing away.
    //
    // For Saturn the figure is the RING SPAN, not the globe. The rings
    // are what is on the screen and what has to fit, and they are 2.27
    // times wider -- framing on the disc would understate the target
    // by more than double. The database value, where one exists, always
    // wins: this only fills a hole.
    static void FillPlanetSize(Content& c) {
        if (c.rec.hasSizeArcmin) return;              // a real value wins
        const std::string& n = c.rec.name.empty() ? c.target : c.rec.name;
        const auto s = raduk::sky::PlanetApparentSizeAt(
                           n, raduk::sky::CurrentJD());
        if (!s.valid) return;
        c.rec.sizeArcmin    = s.framingArcsec / 60.0; // the panel works in arcmin
        c.rec.hasSizeArcmin = true;
        c.planetDiscArcmin  = s.discArcsec / 60.0;
        c.havePlanetDisc    = true;
        c.ringsIncluded     = s.framingArcsec > s.discArcsec * 1.01;
    }

    void SetContent(const Content& c) {
        const bool changed = (c.target != m_content.target);
        m_content = c;
        FillPlanetSize(m_content);
        if (changed) {
            m_fadeStart = std::chrono::steady_clock::now();
            m_fading    = true;
            // Phase 96.5: the fade needs frames of its own. Before
            // this, the only thing that called Draw() again was the
            // 500 ms readout poll that feeds SetContent -- 2 s while
            // capturing -- so a 450 ms cross-fade got exactly one
            // frame, at alpha near zero. It did not fade. It blanked
            // the panel and then snapped back a poll later.
            if (m_hwnd && IsVisible())
                SetTimer(m_hwnd, TIMER_FADE, Theme::Motion::FRAME_MS, nullptr);
        }
        if (IsVisible()) Redraw();
    }

    void Redraw() { if (m_rt && IsVisible()) Draw(); }

    void FillMonitor() {
        if (!m_hwnd || m_full) return;
        m_savedPlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(m_hwnd, &m_savedPlacement);
        MONITORINFO mi = { sizeof(mi) };
        if (!GetMonitorInfo(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi))
            return;
        SetWindowLongW(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(m_hwnd, nullptr, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
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
    void ToggleFullscreen() {
        m_wantFull = !m_full;
        m_full ? RestoreWindowed() : FillMonitor();
        if (m_hwnd && IsWindowVisible(m_hwnd)) SetForegroundWindow(m_hwnd);
        Redraw();
    }

private:
    // The formatting lives in namespace objectpanel above, where a
    // test can reach it: these are the sentences a stranger reads off
    // a screen and believes, so they are gated like the histogram's
    // numbers and the sky conditions. Call sites qualify them.

    // ── The accent, and what it is allowed to mean ──────────
    //
    // One hue per kind of thing, in DAY mode, where it is a pleasant
    // redundancy: the kind is also written out in words directly
    // under the title, so nobody depends on the colour.
    //
    // In night vision it cannot mean anything, and worse, it was
    // breaking the ladder. These hues are not palette tokens, so
    // NightLuminance falls through to the derived value. Measured,
    // all six:
    //
    //     Satellite  217      Star     201      default  203
    //     Planet     192      Galaxy   192      Nebula   165
    //
    // TEXT -- primary readouts, the brightest thing on here that is
    // not a warning -- is designed at 200. So three of the six
    // accents come out brighter than the headline they sit under,
    // the Moon's by seventeen units. None reaches the 240 that 96.1
    // reserved for danger, so this is a smaller inversion than the
    // one that phase fixed, but it is the same kind: a content
    // accent reading louder than primary text because nobody
    // decided it should.
    //
    // So in night vision the accent IS a token. The hue is already
    // redundant and the mode collapses all six to within 52 units
    // of each other regardless; taking ACCENT puts it back inside
    // the ladder at 140, between the live-mount green above and the
    // secondary amber below, which is the right band for a content
    // accent.
    //
    // Returns UNTINTED channels: every call site here goes through
    // Brush(), which applies NVf itself, and tinting twice is not a
    // no-op (see Theme::Raw).
    D2D1_COLOR_F AccentPainted() const {
        if (Theme::NightVisionOn()) return Theme::Raw(Theme::ACCENT);
        return Accent();
    }

    // The day hue, for the backdrop gradient's channels.
    D2D1_COLOR_F Accent() const {
        const std::string& t = m_content.rec.type;
        if (t == "Planet")    return D2D1::ColorF(1.00f, 0.72f, 0.35f);
        if (t == "Satellite") return D2D1::ColorF(0.85f, 0.86f, 0.80f);
        if (t == "Star" || t == "Double Star")
                              return D2D1::ColorF(0.68f, 0.80f, 1.00f);
        if (t == "G" || t == "GPair" || t == "GTrpl")
                              return D2D1::ColorF(0.85f, 0.70f, 1.00f);
        if (t == "HII" || t.find('N') != std::string::npos)
                              return D2D1::ColorF(1.00f, 0.55f, 0.60f);
        return D2D1::ColorF(0.40f, 0.90f, 0.95f);
    }

    // ── D2D ───────────────────────────────────────────────────
    bool InitD2D() {
        ID2D1Factory* factory = nullptr;
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory)))
            return false;
        RECT rc; GetClientRect(m_hwnd, &rc);
        m_w = rc.right; m_h = rc.bottom;
        HRESULT hr = factory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(m_hwnd, D2D1::SizeU(m_w, m_h)), &m_rt);
        factory->Release();
        if (FAILED(hr)) return false;
        IDWriteFactory* dw = nullptr;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                            __uuidof(IDWriteFactory), (IUnknown**)&dw);
        if (dw) { m_dwrite = dw; MakeFonts(); }
        return true;
    }

    void ReleaseD2D() {
        if (m_fTitle) { m_fTitle->Release(); m_fTitle = nullptr; }
        if (m_fKind)  { m_fKind->Release();  m_fKind  = nullptr; }
        if (m_fLabel) { m_fLabel->Release(); m_fLabel = nullptr; }
        if (m_fValue) { m_fValue->Release(); m_fValue = nullptr; }
        if (m_fBody)  { m_fBody->Release();  m_fBody  = nullptr; }
        if (m_fBodyFit) { m_fBodyFit->Release(); m_fBodyFit = nullptr; }
        m_fBodyFitPx = 0.0f;
        if (m_rt)     { m_rt->Release();     m_rt     = nullptr; }
        // Last: MakeFonts creates the formats through it, and the sky
        // map crashed on every launch once for getting this order wrong.
        if (m_dwrite) { m_dwrite->Release(); m_dwrite = nullptr; }
        m_fontPx = 0.0f;
        m_bodyPx = 0.0f;
    }

    // ── Phase 96.3: five invented ratios become five steps ──
    //
    // This surface had its own set -- 2.9, 0.95, 0.52, 1.05, 0.80 --
    // and the share view, same room and same audience, had invented
    // 1.0 and 0.42 for the same jobs. Neither knew about the other,
    // so the two screens pointed at the same crowd agreed on not one
    // single size.
    //
    // The body size is the anchor, because the narrative columns and
    // EstimateHeight are laid out against it and that layout is
    // right. Everything else is derived from it through Theme::Type.
    //
    // What that changes: the labels grow (0.52 em of all-caps mono,
    // read from three metres back, was below the ramp's smallest
    // step for a reason), the figures grow, and the title does not
    // move -- Type::DISPLAY exists precisely so it would not.
    void MakeFonts() {
        if (!m_dwrite) return;
        const float px = (std::max)(14.0f, (std::min)(44.0f, (float)m_h / 26.0f));
        if (m_fTitle && px == m_fontPx) return;
        m_fontPx = px;
        // The tuned body size, kept -- floored at the ramp's anchor,
        // below which the scaled steps compress back under MIN_STEP
        // and the ramp stops being one.
        m_bodyPx = (std::max)((float)Theme::Type::BODY, px * 0.80f);
        if (m_fTitle) { m_fTitle->Release(); m_fTitle = nullptr; }
        if (m_fKind)  { m_fKind->Release();  m_fKind  = nullptr; }
        if (m_fLabel) { m_fLabel->Release(); m_fLabel = nullptr; }
        if (m_fValue) { m_fValue->Release(); m_fValue = nullptr; }
        if (m_fBody)  { m_fBody->Release();  m_fBody  = nullptr; }
        if (m_fBodyFit) { m_fBodyFit->Release(); m_fBodyFit = nullptr; }
        m_fBodyFitPx = 0.0f;

        auto mk = [&](const wchar_t* face, DWRITE_FONT_WEIGHT w, float size,
                      IDWriteTextFormat** out) {
            m_dwrite->CreateTextFormat(face, nullptr, w, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", out);
        };
        namespace T = Theme::Type;
        mk(Theme::FONT_LABEL, DWRITE_FONT_WEIGHT_BOLD,
           T::Scaled(m_bodyPx, T::S_DISPLAY), &m_fTitle);
        mk(Theme::FONT_LABEL, DWRITE_FONT_WEIGHT_NORMAL,
           T::Scaled(m_bodyPx, T::S_STRONG),  &m_fKind);
        mk(Theme::FONT_DATA,  DWRITE_FONT_WEIGHT_NORMAL,
           T::Scaled(m_bodyPx, T::S_MICRO),   &m_fLabel);
        mk(Theme::FONT_LABEL, DWRITE_FONT_WEIGHT_BOLD,
           T::Scaled(m_bodyPx, T::S_TITLE),   &m_fValue);
        mk(Theme::FONT_LABEL, DWRITE_FONT_WEIGHT_NORMAL,
           T::Scaled(m_bodyPx, T::S_BODY),    &m_fBody);
    }

    ID2D1SolidColorBrush* Brush(D2D1_COLOR_F c, float alpha) {
        c.a *= alpha;
        if (m_brush) { m_brush->SetColor(Theme::NVf(c)); return m_brush; }
        m_rt->CreateSolidColorBrush(Theme::NVf(c), &m_brush);
        return m_brush;
    }

    void Text(IDWriteTextFormat* f, const std::wstring& s, float x, float y,
              float w, float h, D2D1_COLOR_F c, float alpha,
              DWRITE_TEXT_ALIGNMENT al = DWRITE_TEXT_ALIGNMENT_LEADING) {
        if (!f || s.empty()) return;
        f->SetTextAlignment(al);
        // Phase 103: D2D1_DRAW_TEXT_OPTIONS_CLIP. Without it the
        // layout rect sets wrapping and alignment only, and anything
        // taller than the rect is still drawn -- which is how the
        // narrative got out of the panel entirely.
        m_rt->DrawText(s.c_str(), (UINT32)s.size(), f,
                       D2D1::RectF(x, y, x + w, y + h), Brush(c, alpha),
                       D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    // Enough to decide where the next block starts. The body face is
    // proportional, so this estimates from an average advance rather
    // than measuring.
    //
    // Phase 103: this had no callers at all, and its comment used to
    // end "the text is clipped to the rect either way", which was
    // false and was the whole reason Jupiter's narrative left the
    // window. Text() passes D2D1_DRAW_TEXT_OPTIONS_CLIP now, so the
    // claim is true -- and the narrative is sized to fit before it
    // gets there, so clipping is the backstop rather than the plan.
    //
    // The arithmetic moved to namespace objectpanel where a test can
    // reach it. m_bodyPx is the size the body text is actually drawn
    // at; the old m_fontPx * 0.80f was that same number before
    // m_bodyPx existed to hold it, and kept its own copy of the 0.80.
    float EstimateHeight(const std::string& text, float width) const {
        return objectpanel::EstimatedHeight(text.size(), width, m_bodyPx);
    }

    // A body format at a size chosen to fit, cached so a steady target
    // does not rebuild it every frame.
    IDWriteTextFormat* BodyAt(float px) {
        if (px >= m_bodyPx - 0.5f) return m_fBody;      // the normal size
        if (m_fBodyFit && std::fabs(px - m_fBodyFitPx) < 0.5f)
            return m_fBodyFit;
        if (m_fBodyFit) { m_fBodyFit->Release(); m_fBodyFit = nullptr; }
        if (!m_dwrite) return m_fBody;
        m_dwrite->CreateTextFormat(Theme::FONT_LABEL, nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, px, L"en-us", &m_fBodyFit);
        m_fBodyFitPx = px;
        return m_fBodyFit ? m_fBodyFit : m_fBody;
    }

    static std::wstring W(const std::string& s) {
        if (s.empty()) return L"";
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring o((size_t)(n > 0 ? n - 1 : 0), L'\0');
        if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &o[0], n);
        return o;
    }

    void Draw() {
        if (!m_rt) return;

        // The cross-fade. Motion that carries information: the panel
        // changes when the target changes, so it transitions rather
        // than cuts. Nothing else on here moves.
        float alpha = 1.0f;
        if (m_fading) {
            const double ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - m_fadeStart).count();
            alpha = Theme::Motion::FadeIn(ms, FADE_MS);
            if (alpha >= 1.0f) {
                m_fading = false;
                if (m_hwnd) KillTimer(m_hwnd, TIMER_FADE);
            }
        }

        m_rt->BeginDraw();
        DrawBackdrop();

        // Phase 103: the backstop. Every text block is sized to fit
        // and clipped to its own rect now, but this is the one surface
        // an audience looks at, and a glyph outside the window is the
        // kind of thing nobody notices until it is on a screen in
        // front of people. One clip over the whole panel means no
        // future block can get out, whatever it forgets to pass.
        m_rt->PushAxisAlignedClip(
            D2D1::RectF(0.0f, 0.0f, (float)m_w, (float)m_h),
            D2D1_ANTIALIAS_MODE_ALIASED);

        const auto& r = m_content.rec;
        // Tokens, so 96.1's night ladder reaches this surface. It did
        // not before: every colour on here was a literal, which falls
        // through NightLuminance to the DERIVED value and so sits
        // wherever the arithmetic puts it rather than where the
        // design says. The old dim literal (0.55, 0.65, 0.75)
        // derived to 164; TEXT_DIM's designed value is 80.
        const D2D1_COLOR_F ac  = AccentPainted();
        const D2D1_COLOR_F fg  = Theme::Raw(Theme::TEXT);
        const D2D1_COLOR_F dim = Theme::Raw(Theme::TEXT_DIM);

        const float pad = (std::max)(28.0f, (float)m_w / 22.0f);
        const float colW = (float)m_w - pad * 2.0f;
        float y = pad;

        // ── Headline ──
        // The catalogue stores alternatives comma-separated and
        // unspaced -- M42 is "Great Orion Nebula,Orion Nebula". Take
        // the first; the headline wants one name, not a list.
        std::string title = !r.commonName.empty() ? r.commonName
                          : (!m_content.target.empty() ? m_content.target
                                                       : r.name);
        {
            const size_t comma = title.find(',');
            if (comma != std::string::npos) title = title.substr(0, comma);
        }
        Text(m_fTitle, W(title), pad, y, colW, m_fontPx * 3.4f, fg, alpha);
        y += m_fontPx * 3.1f;

        // One line saying what kind of thing it is, and where.
        {
            std::string kind = objectpanel::TypeName(r.type, r.subtype);
            if (objectpanel::LooksLikeConstellation(r.constellation))
                kind += kind.empty() ? "" : "  ·  in " + r.constellation;
            // If the app's name and the catalogue's differ, show both.
            if (!r.name.empty() && r.name != title &&
                m_content.target.find(r.name) == std::string::npos)
                kind += kind.empty() ? r.name : "  ·  " + r.name;
            Text(m_fKind, W(kind), pad, y, colW,
                 Theme::Type::Scaled(m_bodyPx, Theme::Type::S_STRONG) * 1.5f,
                 ac, alpha);
            y += Theme::Type::Scaled(m_bodyPx, Theme::Type::S_STRONG) * 1.6f;
        }

        // Accent rule under the headline.
        m_rt->FillRectangle(
            D2D1::RectF(pad, y, pad + colW * 0.33f, y + (std::max)(2.0f, m_fontPx * 0.07f)),
            Brush(ac, alpha * 0.9f));
        y += m_fontPx * 0.9f;

        // ── Figures ──
        //
        // Only the ones that exist. An absent fact is not a blank
        // column with a heading over it.
        struct Fig { std::string label, value; };
        std::vector<Fig> figs;
        if (r.hasMagnitude) {
            char b[32]; snprintf(b, sizeof(b), "%+.1f", r.magnitude);
            figs.push_back({ "MAGNITUDE", b });
        }
        if (r.hasSizeArcmin) {
            // For Saturn rec.sizeArcmin is the ring span, because that
            // is what has to fit the frame. The figure under the words
            // APPARENT SIZE must still be the globe, or it disagrees
            // with every book and app a visitor might check it against.
            const double shown = m_content.havePlanetDisc
                               ? m_content.planetDiscArcmin : r.sizeArcmin;
            char b[32];
            if (shown >= 1.0) snprintf(b, sizeof(b), "%.1f'", shown);
            else snprintf(b, sizeof(b), "%.0f\"", shown * 60.0);
            figs.push_back({ "APPARENT SIZE", b });
        }
        if (m_content.haveAltitude) {
            char b[32]; snprintf(b, sizeof(b), "%.0f°", m_content.altitudeDeg);
            figs.push_back({ "ALTITUDE NOW", b });
        }
        // Airmass only while the target is actually up. SkyConditions
        // saturates it at 40 below the horizon, and "40.00 AIR MASSES"
        // on a public screen is a bound being displayed as a
        // measurement. Below the horizon the honest figure is the
        // altitude, which is already in the row beside it.
        if (m_content.haveAltitude && m_content.altitudeDeg > 0.0 &&
            m_content.airmass > 0.0 && m_content.airmass < 30.0) {
            char b[32]; snprintf(b, sizeof(b), "%.2f", m_content.airmass);
            figs.push_back({ "AIR MASSES", b });
        }
        // ── The figures, as cards ──────────────────────────
        //
        // These were four bare label-and-value pairs laid out by
        // dividing the column and subtracting eight pixels, with
        // nothing between them. Which value belongs to which label
        // was left to the reader's eye, and the reader is a stranger
        // at three metres in the dark.
        //
        // Giving each figure a surface fixes that by making it an
        // object, and it is the place on either audience surface
        // where depth does the most work -- the single biggest thing
        // the reference clients have that this did not. The gutter is
        // the shared rhythm rather than a literal, and the geometry
        // is audience::CardInRow so a test can check that one card
        // and five both come out right.
        if (!figs.empty()) {
            namespace T = Theme::Type;
            const float labelH = T::Scaled(m_bodyPx, T::S_MICRO) * 1.35f;
            const float valueH = T::Scaled(m_bodyPx, T::S_TITLE)  * 1.30f;
            const float inset  = Theme::Space::Scaled(m_bodyPx,
                                                      Theme::Space::P_MD);
            const float cardH  = inset * 2.0f + labelH + valueH;
            const float rad    = audience::CardRadius(m_bodyPx);
            const audience::LRect row{ pad, y, pad + colW, y + cardH };
            const int n = (int)figs.size();

            for (int i = 0; i < n; i++) {
                const audience::LRect c =
                    audience::CardInRow(row, n, i, m_bodyPx);
                const float tw = audience::Width(c) - inset * 2.0f;
                if (tw <= 0.0f) continue;
                DrawCard(c, rad, ac, alpha);
                Text(m_fLabel, W(figs[(size_t)i].label),
                     c.l + inset, c.t + inset, tw, labelH, dim, alpha);
                Text(m_fValue, W(figs[(size_t)i].value),
                     c.l + inset, c.t + inset + labelH, tw, valueH,
                     fg, alpha);
            }
            y += cardH + Theme::Space::Scaled(m_bodyPx,
                                              Theme::Space::P_LG);
        }

        // ── Derived ──
        std::vector<std::string> derived;
        if (r.hasDistanceLy) {
            const std::string lt = objectpanel::LightTravel(r.distanceLy);
            if (!lt.empty())
                derived.push_back("The light arriving now left " + lt + ".");
        }
        if (r.hasSizeArcmin) {
            const std::string ff = objectpanel::FrameFill(r.sizeArcmin,
                                             m_content.plateScaleArcsecPx,
                                             m_content.roiWidth,
                                             m_content.roiHeight);
            if (!ff.empty()) derived.push_back("It " + ff + ".");
        }
        if (m_content.ringsIncluded && m_content.planetDiscArcmin > 0.0) {
            char b[160];
            snprintf(b, sizeof(b),
                     "The rings span %.0f arcseconds, %.1f times the globe "
                     "itself - and that is what has to fit the frame.",
                     r.sizeArcmin * 60.0,
                     r.sizeArcmin / m_content.planetDiscArcmin);
            derived.push_back(b);
        }
        if (r.hasMagnitude)
            derived.push_back(objectpanel::BrightnessLine(r.magnitude) + ".");

        // ── The scale diagram ────────────────────────────
        //
        // Drawn to the right of the derived lines, so it is read
        // alongside them rather than instead of them. It answers the
        // question the owner asked about the moons: how much sky does
        // the sensor actually see tonight.
        //
        // No photograph. Licensing aside, a space-telescope image
        // beside a live view from the garden invites exactly the
        // comparison nobody wants. This is computed from the plate
        // scale and the live capture format, so it is true of THIS
        // rig on THIS night, which no stock picture would be.
        const float yDerivedTop = y;
        const float diagW = colW * 0.30f;
        const float diagX = pad + colW - diagW;
        DrawScaleDiagram(diagX, y, diagW, m_fontPx * 4.6f, alpha);
        const float derivedW = colW - diagW - m_fontPx;

        for (const auto& line : derived) {
            Text(m_fBody, W(line), pad, y, derivedW, m_fontPx * 1.3f, ac, alpha);
            y += m_fontPx * 1.15f;
        }
        // The diagram is taller than the derived lines when there are
        // only one or two, so the narrative starts below whichever
        // reaches further down.
        // The diagram box is 4.6 ems tall and its caption sits below
        // it, so the narrative has to clear about 6 before it starts
        // or it runs under the label -- which it did.
        // The diagram sits in the right 30%, so only the RIGHT text
        // column has to clear it. Pushing BOTH columns below it threw
        // away about a hundred and sixty pixels from the left one and
        // clipped the summary mid-sentence.
        const float afterDiag = yDerivedTop + m_fontPx * 6.1f;
        if (!derived.empty()) y += m_fontPx * 0.5f;
        const float yLeft  = y;
        const float yRight = (y < afterDiag) ? afterDiag : y;

        // ── Narrative ──
        //
        // Drawn only when there is one. No heading over an empty box,
        // no "no data available".
        if (r.HasNarrative()) {
            const float left = (float)m_h - yLeft - pad;
            if (left > m_fontPx * 2.0f) {
                // ── Two columns ──
                //
                // Not only to fit both paragraphs, though stacking
                // them did push the mythology off a 1080p screen. A
                // single column across 1920 pixels is about 110
                // characters a line, which is half as wide again as
                // anything comfortable to read; two columns bring it
                // back to roughly seventy and use the width that the
                // diagram does not.
                //
                // The second column takes the mythology, which is the
                // part about people rather than photometry and the
                // part an audience actually remembers.
                const std::string& aside =
                    !r.mythology.empty() ? r.mythology : r.discoveryNotes;
                const float gut = m_fontPx * 1.4f;
                const float cw2 = aside.empty() ? colW
                                                : (colW - gut) * 0.5f;
                const float rightH = (float)m_h - yRight - pad;

                // Phase 103. This is what ran off the bottom of the
                // window. Both columns were drawn at the full body
                // size into whatever height happened to be left, and
                // nothing clipped, so a long narrative carried on past
                // the edge. Jupiter has the longest summary and a
                // mythology beside it, so it had the most to spill.
                //
                // Both columns take the SAME fitted size, the smaller
                // of the two each needs. Sizing them independently
                // would put two different body sizes side by side,
                // which reads as a mistake even when both fit.
                const float fit =
                    (std::min)(objectpanel::FitBodyPx(r.summary.size(),
                                                      cw2, left, m_bodyPx),
                               aside.empty()
                                   ? m_bodyPx
                                   : objectpanel::FitBodyPx(aside.size(),
                                                 cw2, rightH, m_bodyPx));
                IDWriteTextFormat* bf = BodyAt(fit);

                Text(bf, W(r.summary), pad, yLeft, cw2, left,
                     fg, alpha * 0.92f);
                if (!aside.empty())
                    Text(bf, W(aside), pad + cw2 + gut, yRight, cw2,
                         rightH, ac, alpha * 0.85f);
            }
        } else if (derived.empty() && !r.found) {
            // Nothing at all is known. Say that plainly rather than
            // showing a polished screen with nothing true on it.
            Text(m_fBody, W("Not in the catalogue."), pad, yLeft, colW,
                 m_fontPx * 2.0f, dim, alpha);
        }

        m_rt->PopAxisAlignedClip();
        HRESULT hr = m_rt->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) { ReleaseD2D(); InitD2D(); }
    }

    // ── A figure card ───────────────────────────────────────
    //
    // A raised surface, a hairline, and one accent tick at the top
    // left. The tick is the smallest bit of the reference clients'
    // frame language that does work here: it points at where the
    // label starts, which is what tells a reader that this value and
    // this label are one thing.
    void DrawCard(const audience::LRect& c, float rad,
                  const D2D1_COLOR_F& ac, float alpha) {
        DrawElevation(c, rad, Theme::Elev::RAISED, alpha);
        m_rt->FillRoundedRectangle(
            D2D1::RoundedRect(R(c), rad, rad),
            Brush(Theme::Raw(Theme::PANEL), alpha * 0.90f));
        m_rt->DrawRoundedRectangle(
            D2D1::RoundedRect(R(c), rad, rad),
            Brush(Theme::Raw(Theme::BORDER), alpha), 1.0f);

        const float inset = Theme::Space::Scaled(m_bodyPx,
                                                 Theme::Space::P_MD);
        const float tick  = (std::max)(2.0f, m_bodyPx * 0.10f);
        m_rt->FillRectangle(
            D2D1::RectF(c.l + inset, c.t + inset * 0.45f,
                        c.l + inset + m_bodyPx * 1.1f,
                        c.t + inset * 0.45f + tick),
            Brush(ac, alpha * 0.85f));
    }

    // ── Depth, without a blur to do it with ─────────────────
    //
    // A plain hwnd render target has no Gaussian blur; that lives on
    // ID2D1DeviceContext, and rebuilding this window onto the Direct2D
    // 1.1 pipeline to soften a card edge would be a disproportionate
    // change to a window that works. Nested rings of flat fill
    // instead, weighted by audience::ShadowRingAt to sum to the
    // level's stated alpha so Theme's ordering survives the
    // approximation rather than being renegotiated here.
    void DrawElevation(const audience::LRect& card, float radius,
                       int level, float alpha) {
        const Theme::Shadow sh = Theme::ShadowFor(level);
        if (!audience::ShadowVisible(sh)) return;
        // Black at an alpha, and NOT through Brush(): a shadow is the
        // absence of light, and NVf would make it a red glow. One
        // brush recoloured per ring rather than three created and
        // released -- the cross-fade repaints this surface and a
        // four-card row would otherwise churn a dozen COM objects a
        // frame for no reason.
        ID2D1SolidColorBrush* b = nullptr;
        if (FAILED(m_rt->CreateSolidColorBrush(
                D2D1::ColorF(0, 0, 0, 1.0f), &b)) || !b) return;
        for (int i = 0; i < audience::SHADOW_RINGS; ++i) {
            const audience::Ring g = audience::ShadowRingAt(card, sh, i);
            b->SetColor(D2D1::ColorF(0, 0, 0, g.alpha * alpha));
            // The ring is wider than the card by its spread, so its
            // corner radius grows with it or the outer rings square
            // off against a rounded card.
            const float gr = radius + (card.l - g.rect.l);
            m_rt->FillRoundedRectangle(
                D2D1::RoundedRect(R(g.rect), gr, gr), b);
        }
        b->Release();
    }

    static D2D1_RECT_F R(const audience::LRect& k) {
        return D2D1::RectF(k.l, k.t, k.r, k.b);
    }

    // A dark field with a slow radial lift behind the headline, so the
    // type sits above the background rather than on it. Depth, not
    // decoration -- it carries no information and does not move.
    void DrawBackdrop() {
        m_rt->Clear(Theme::Color(Theme::BG_VOID));
        // The raw day hue here on purpose: the gradient needs real
        // channels to lift, and NVf is applied to each stop below.
        const D2D1_COLOR_F ac = Accent();
        ID2D1RadialGradientBrush* rg = nullptr;
        ID2D1GradientStopCollection* stops = nullptr;
        D2D1_GRADIENT_STOP gs[2];
        gs[0].position = 0.0f;
        gs[0].color = Theme::NVf(D2D1::ColorF(ac.r, ac.g, ac.b, 0.10f));
        gs[1].position = 1.0f;
        gs[1].color = Theme::NVf(D2D1::ColorF(ac.r, ac.g, ac.b, 0.0f));
        if (SUCCEEDED(m_rt->CreateGradientStopCollection(gs, 2, &stops)) && stops) {
            const float cx = (float)m_w * 0.22f, cy = (float)m_h * 0.18f;
            const float rad = (float)(m_w > m_h ? m_w : m_h) * 0.75f;
            if (SUCCEEDED(m_rt->CreateRadialGradientBrush(
                    D2D1::RadialGradientBrushProperties(
                        D2D1::Point2F(cx, cy), D2D1::Point2F(0, 0), rad, rad),
                    stops, &rg)) && rg) {
                m_rt->FillRectangle(
                    D2D1::RectF(0, 0, (float)m_w, (float)m_h), rg);
                rg->Release();
            }
            stops->Release();
        }
    }

    // ── The scale diagram ─────────────────────────────────────
    //
    // The sensor's field of view, drawn to scale, with the object in
    // it when its angular size is known. Everything here is computed
    // from the live plate scale and the live capture format, so it is
    // true of this rig tonight rather than true in general.
    //
    // When the size is NOT known -- which is every planet, because
    // size_arcmin is NULL for all of them -- the frame is drawn alone
    // with its real dimensions. That is still the answer to "how much
    // sky am I looking at", and it is better than drawing a disc whose
    // size nobody measured.
    void DrawScaleDiagram(float x, float y, float w, float h, float alpha) {
        const auto& r = m_content.rec;
        if (m_content.roiWidth <= 0 || m_content.roiHeight <= 0) return;
        if (w < 40.0f || h < 30.0f) return;

        const double scale = m_content.plateScaleArcsecPx;
        const double frameW = m_content.roiWidth  * scale / 60.0;   // arcmin
        const double frameH = m_content.roiHeight * scale / 60.0;
        if (frameW <= 0.0 || frameH <= 0.0) return;

        const D2D1_COLOR_F ac  = AccentPainted();
        const D2D1_COLOR_F dim = Theme::Raw(Theme::TEXT_DIM);

        // How much sky the drawing covers. Normally the frame fills
        // it; when the object is larger than the frame the view pulls
        // back so both fit, which is itself the useful picture.
        double viewW = frameW, viewH = frameH;
        const bool haveObj = r.hasSizeArcmin && r.sizeArcmin > 0.0;
        if (haveObj && r.sizeArcmin > frameH * 0.95) {
            const double need = r.sizeArcmin * 1.25;
            const double k = need / frameH;
            viewW = frameW * k;
            viewH = frameH * k;
        }

        // Fit the view into the box, preserving the sky's aspect.
        const float k = (std::min)((float)(w / viewW), (float)(h / viewH));
        const float fw = (float)(frameW * k), fh = (float)(frameH * k);
        const float cx = x + w * 0.5f, cy = y + h * 0.5f;

        // The object first, so the frame outline sits on top of it.
        if (haveObj) {
            const float rr = (float)(r.sizeArcmin * k * 0.5f);
            if (rr > 1.0f) {
                ID2D1SolidColorBrush* b = Brush(ac, alpha * 0.28f);
                m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rr, rr), b);
                m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rr, rr),
                                  Brush(ac, alpha * 0.75f), 1.2f);
            }
        }

        // The sensor's frame.
        const D2D1_RECT_F fr = D2D1::RectF(cx - fw * 0.5f, cy - fh * 0.5f,
                                           cx + fw * 0.5f, cy + fh * 0.5f);
        m_rt->DrawRectangle(fr, Brush(dim, alpha * 0.9f), 1.4f);

        // Label it with what it really is, under the box.
        char lab[96];
        snprintf(lab, sizeof(lab), "%.1f' x %.1f'  sensor field",
                 frameW, frameH);
        Text(m_fLabel, W(lab), x, fr.bottom + m_fontPx * 0.18f, w,
             m_fontPx * 0.9f, dim, alpha, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    void OnResize(int w, int h) {
        m_w = w; m_h = h;
        if (m_rt) m_rt->Resize(D2D1::SizeU(w, h));
        MakeFonts();
        Redraw();
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        ObjectPanel* self = reinterpret_cast<ObjectPanel*>(
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
                if (wp != SIZE_MINIMIZED) self->OnResize(LOWORD(lp), HIWORD(lp));
                return 0;
            case WM_PAINT: {
                PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
                self->Draw(); EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_ERASEBKGND: return 1;
            // Only ever armed while a transition is running; Draw()
            // kills it on the frame the fade completes.
            case WM_TIMER:
                if (wp == TIMER_FADE) { self->Redraw(); return 0; }
                break;
            // Claim only F11, the way the share view and the sky map
            // do. Escape stays global because it is the emergency stop
            // and must reach the mount from every window.
            case WM_GETDLGCODE:
                if (lp && wp == VK_F11) return DLGC_WANTMESSAGE;
                return 0;
            case WM_KEYDOWN:
                if (wp == VK_F11) { self->ToggleFullscreen(); return 0; }
                break;
            case WM_LBUTTONDOWN:
                SetForegroundWindow(hwnd);
                return 0;
            case WM_CLOSE: ShowWindow(hwnd, SW_HIDE); return 0;
            case WM_DESTROY: self->m_hwnd = nullptr; return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    // Rung 1 of the motion ladder: a surface whose content changes
    // identity while the surface itself stays put.
    static constexpr int FADE_MS    = Theme::Motion::CONTENT;
    static constexpr UINT_PTR TIMER_FADE = 1;

    HWND                   m_hwnd   = nullptr;
    ID2D1HwndRenderTarget* m_rt     = nullptr;
    IDWriteFactory*        m_dwrite = nullptr;
    ID2D1SolidColorBrush*  m_brush  = nullptr;
    IDWriteTextFormat*     m_fTitle = nullptr;
    IDWriteTextFormat*     m_fKind  = nullptr;
    IDWriteTextFormat*     m_fLabel = nullptr;
    IDWriteTextFormat*     m_fValue = nullptr;
    IDWriteTextFormat*     m_fBody  = nullptr;

    int   m_w = 0, m_h = 0;
    float m_fontPx = 0.0f;
    float m_bodyPx = 0.0f;      // the ramp's anchor on this surface
    bool  m_full = false;
    bool  m_wantFull = true;
    bool  m_fading = false;
    IDWriteTextFormat*     m_fBodyFit   = nullptr;  // narrative, fitted
    float                  m_fBodyFitPx = 0.0f;
    std::chrono::steady_clock::time_point m_fadeStart{};
    WINDOWPLACEMENT m_savedPlacement = { sizeof(WINDOWPLACEMENT) };
    Content m_content;
};

} // namespace raduk::gui
