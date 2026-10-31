#pragma once
// ============================================================
// Theme.h — Elite Raduk design tokens
// Deep-space palette: void black base, cyan primary accent,
// amber secondary, green for live/mount, red for capture.
// Typography: Space Mono (data), Barlow Condensed (labels).
// ============================================================

#include <windows.h>
#include <d2d1.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#include <cstdint>
#include <vector>

namespace raduk::gui {
namespace Theme {

    // ── Core palette ───────────────────────────────────────────
    //   0xFFRRGGBB (alpha always FF — fully opaque)

    constexpr uint32_t BG_VOID   = 0xFF04080F;  // --bg-void  (preview bg, deepest surface)
    constexpr uint32_t DEEP      = 0xFF07080E;  // --bg-deep  (panel background)
    constexpr uint32_t PANEL     = 0xFF0A1520;  // --bg-panel (section headers, raised)
    constexpr uint32_t BORDER    = 0xFF0E1D2E;  // --bg-raised (subtle separators)
    constexpr uint32_t ACCENT    = 0xFF00E5FF;  // --cyan     (primary — camera, active)
    constexpr uint32_t ACCENT2   = 0xFFFFB900;  // --amber    (EAF, secondary)
    constexpr uint32_t ACCENT3   = 0xFF4DFF91;  // --green    (mount, live indicators)
    constexpr uint32_t ACCENT4   = 0xFFFF3B5C;  // --red      (capture, danger)
    constexpr uint32_t TEXT      = 0xFFC8E8F0;  // --white    (primary text)
    constexpr uint32_t TEXT_DIM  = 0xFF8CAFBE;  // --dim      (secondary labels)
    constexpr uint32_t RED       = 0xFFFF3B5C;  // --red
    constexpr uint32_t AMBER     = 0xFFFFB900;  // --amber
    constexpr uint32_t GREEN     = 0xFF4DFF91;  // --green

    // ── Font hierarchy ─────────────────────────────────────────
    //   FONT_BANNER : Space Mono — large readouts, title bar, HUD
    //   FONT_LABEL  : Barlow Condensed — all labels, section headers
    //   FONT_DATA   : Space Mono — numeric values, coordinates

    constexpr const wchar_t* FONT_BANNER = L"Space Mono";
    constexpr const wchar_t* FONT_LABEL  = L"Barlow Condensed";
    constexpr const wchar_t* FONT_DATA   = L"Space Mono";

    // ── Helper: Convert 0xAARRGGBB to D2D1::ColorF ────────────

    inline D2D1_COLOR_F NVf(D2D1_COLOR_F c);   // defined below

    inline D2D1_COLOR_F Color(uint32_t argb) {
        float a = ((argb >> 24) & 0xFF) / 255.0f;
        float r = ((argb >> 16) & 0xFF) / 255.0f;
        float g = ((argb >> 8)  & 0xFF) / 255.0f;
        float b = ((argb)       & 0xFF) / 255.0f;
        // Tinted here so every caller of the central D2D helper is
        // covered without touching the call site.
        return NVf(D2D1::ColorF(r, g, b, a));
    }


    // ── Night vision (Phase 26) ───────────────────────────────
    //
    // Dark adaptation is destroyed by short wavelengths, so every colour
    // the UI paints is collapsed to its luminance and pushed into the
    // red channel -- the same transform the preview already applies to
    // the image, so the panel and the picture look like one thing.
    //
    // ── Phase 96.1: the hues converge, the LADDER does not ───
    //
    // This comment used to say the accents converging was inherent
    // rather than an oversight, on the grounds that keeping them apart
    // would mean reintroducing the short wavelengths night vision
    // exists to remove, or inventing a hue-to-brightness mapping that
    // means nothing.
    //
    // The first half is right and the second is wrong, and measuring
    // it showed why. Derived from luminance, cyan lands at 182 and
    // amber at 186: four units apart out of 255. Worse, the hierarchy
    // INVERTED. TEXT_DIM, for labels you skim past, derived to 169.
    // ACCENT4, for capture and danger, derived to 103. On a rig that
    // spends its working life in this mode, the colour that must
    // shout was sixty per cent dimmer than the one that must whisper.
    //
    // A hue-to-brightness mapping does mean nothing. But a
    // SEMANTIC-to-brightness mapping means a great deal, and it is the
    // only thing left to design once hue is gone. So each token
    // declares the luminance it should have in the dark, ordered by
    // how much it needs to be noticed, and the derived value is kept
    // only as the fallback for colours not in the ladder.
    //
    // No day colour changes. The ladder is consulted only when night
    // vision is on.
    //
    // The mode lives here, as process state, because 200-odd painting
    // calls across seventeen files need to see it and threading it
    // through every one of them would be worse than a global.
    namespace detail {
        inline bool  g_nightVision = false;
        // Bumped on every change. Cached GDI objects compare against it
        // and rebuild when it moves, which is the whole reason this is a
        // counter and not just the bool.
        inline unsigned g_paletteGen = 1;
    }

    inline bool NightVisionOn() { return detail::g_nightVision; }

    inline void SetNightVision(bool on) {
        if (detail::g_nightVision == on) return;
        detail::g_nightVision = on;
        detail::g_paletteGen++;
    }
    inline unsigned PaletteGeneration() { return detail::g_paletteGen; }

    // ── The night ladder ────────────────────────────────────
    //
    // Keyed on the day colour's RGB bytes, so both painting stacks
    // reach the same answer and NV() stays a pure function of its
    // argument -- which it must, because the GDI object cache keys on
    // the COLORREF and would otherwise hand back a brush in the wrong
    // shade.
    //
    // Ordered by how loudly a thing needs to speak, with roughly 30
    // units between neighbours, which is a separation the eye resolves
    // at these levels. The order, not the exact numbers, is the
    // design: danger above everything, then primary readouts, then the
    // device accents, then the labels you are meant to skim.
    namespace detail {
        struct NightStep { uint8_t r, g, b; int lum; };
        inline constexpr NightStep kNightLadder[] = {
            // day colour            night   role
            { 0xFF, 0x3B, 0x5C,      240 },  // RED / ACCENT4  capture, danger
            { 0xC8, 0xE8, 0xF0,      200 },  // TEXT           primary readouts
            { 0x4D, 0xFF, 0x91,      170 },  // GREEN/ACCENT3  mount, live
            { 0x00, 0xE5, 0xFF,      140 },  // ACCENT cyan    camera, active
            { 0xFF, 0xB9, 0x00,      110 },  // AMBER/ACCENT2  EAF, secondary
            { 0x8C, 0xAF, 0xBE,       80 },  // TEXT_DIM       skimmed labels
            { 0x0E, 0x1D, 0x2E,       34 },  // BORDER         separators
            { 0x0A, 0x15, 0x20,       24 },  // PANEL          raised surface
            { 0x07, 0x08, 0x0E,       14 },  // DEEP           panel background
            { 0x04, 0x08, 0x0F,        8 },  // BG_VOID        deepest surface
        };

        // The ladder has two bands and they want opposite things.
        //
        // SEMANTIC tokens -- danger, text, the device accents, the
        // skimmed labels -- exist to be told apart, so they need real
        // separation. 24 units is the floor, asserted by
        // tests/night_hierarchy.cpp so a later edit cannot quietly
        // reintroduce the four-unit collision this phase fixed.
        //
        // SURFACE tokens -- borders and the three backgrounds -- exist
        // to recede. They need ORDER, so a raised panel still reads as
        // raised, but holding them 24 apart would turn the window into
        // a staircase of greys. They are ordered and close, on purpose.
        inline constexpr int kMinNightSeparation = 24;

        // The first index in kNightLadder that is a surface rather
        // than a signal. Everything from here down is exempt from the
        // separation floor and subject only to ordering.
        inline constexpr int kFirstSurfaceStep = 6;   // BORDER onward
    }

    // Designed luminance for a day colour, or the derived one when the
    // colour is not a token. Unknown colours are rare and mostly
    // decorative, and deriving is the right answer for them: there is
    // no semantic to honour.
    inline int NightLuminance(int r, int g, int b) {
        for (const auto& s : detail::kNightLadder)
            if (s.r == r && s.g == g && s.b == b) return s.lum;
        return (int)(0.2126f * r + 0.7152f * g + 0.0722f * b);
    }

    // The tint itself. Pass every colour through this at the point of
    // use, never at the point of definition -- a value tinted once and
    // stored would keep whatever the mode was when it was stored.
    inline COLORREF NV(COLORREF c) {
        if (!detail::g_nightVision) return c;
        const int lum = NightLuminance(GetRValue(c), GetGValue(c), GetBValue(c));
        // A tenth of the luminance in green keeps near-black greys from
        // reading as pure black, which is what the preview does.
        return RGB(lum, lum / 10, 0);
    }

    // The Direct2D counterpart. Same transform, different type: the
    // preview, the status bar and the sky map all paint through D2D,
    // which takes floats rather than a packed COLORREF.
    inline D2D1_COLOR_F NVf(D2D1_COLOR_F c) {
        if (!detail::g_nightVision) return c;
        // Rounded back to bytes so the ladder lookup matches. The
        // token constants are exact eighths-of-a-bit values divided by
        // 255, so this round-trips without drift; anything else falls
        // through to the derived luminance, which is what it would
        // have had anyway.
        const int r = (int)(c.r * 255.0f + 0.5f);
        const int g = (int)(c.g * 255.0f + 0.5f);
        const int b = (int)(c.b * 255.0f + 0.5f);
        const float lum = (float)NightLuminance(r, g, b) / 255.0f;
        return D2D1::ColorF(lum, lum * 0.1f, 0.0f, c.a);
    }

    // ── Mode-aware GDI object cache ───────────────────────────
    //
    // This exists because of a specific failure, not for speed. The
    // painting code was full of
    //
    //     static HBRUSH bg = CreateSolidBrush(RGB(10, 21, 32));
    //
    // which creates the brush on first paint and keeps it forever.
    // Wrapping the colour in NV() there would have captured whichever
    // mode was active at that first paint, so toggling night vision
    // afterwards would leave those surfaces unchanged -- and it would
    // have compiled, looked right on a first toggle, and shown up as
    // half a panel refusing to change colour. Thirty-six of them.
    //
    // These helpers cache per colour AND per palette generation, so the
    // objects survive repainting and are rebuilt exactly once when the
    // mode changes. Entries are not freed on invalidation while a
    // caller might still hold the handle for the current paint; the
    // cache is bounded by the number of distinct colours the UI uses
    // (a few dozen) and is created once per process, so it does not
    // grow without limit.
    //
    // UI THREAD ONLY. Every caller is a WM_PAINT or WM_CTLCOLOR*
    // handler, which Win32 delivers on the thread that owns the window.
    namespace detail {
        struct GdiCache {
            struct Entry { COLORREF key; unsigned gen; int w; HGDIOBJ obj; };
            std::vector<Entry> brushes, pens;
        };
        inline GdiCache& Cache() { static GdiCache c; return c; }
    }

    // A brush in the current palette. Do NOT DeleteObject the result.
    inline HBRUSH Brush(COLORREF c) {
        auto& v = detail::Cache().brushes;
        const unsigned gen = detail::g_paletteGen;
        for (auto& e : v)
            if (e.key == c && e.gen == gen) return (HBRUSH)e.obj;
        HBRUSH b = CreateSolidBrush(NV(c));
        v.push_back({ c, gen, 0, (HGDIOBJ)b });
        return b;
    }

    // A pen in the current palette. Do NOT DeleteObject the result.
    inline HPEN Pen(COLORREF c, int width = 1) {
        auto& v = detail::Cache().pens;
        const unsigned gen = detail::g_paletteGen;
        for (auto& e : v)
            if (e.key == c && e.gen == gen && e.w == width) return (HPEN)e.obj;
        HPEN p = CreatePen(PS_SOLID, width, NV(c));
        v.push_back({ c, gen, width, (HGDIOBJ)p });
        return p;
    }

    // ── D2: the chrome Windows draws for us ───────────────────
    //
    // Every colour the app paints itself goes through NV(). What stays
    // bright with night vision on is what the OS draws: title bars,
    // list-view backgrounds, tab strips, the inside of edit boxes.
    // Measured when this was filed: the GoTo panel came out 98%
    // non-red, almost all of it the object catalogue's white list.
    //
    // The expensive half of the original fix -- subclassing and
    // painting every push button, because a de-themed classic button
    // takes its face from COLOR_BTNFACE and ignores the brush
    // WM_CTLCOLORBTN hands back -- stays out. These are the cheap
    // parts, and they cover the large areas.

    // Dark title bar. Attribute 20 is DWMWA_USE_IMMERSIVE_DARK_MODE;
    // 19 is what it was called before 20H1, and setting both covers
    // either. Not night-vision dependent: this app is dark whatever
    // mode it is in, and a white title bar was never right.
    inline void DarkTitleBar(HWND h) {
        if (!h) return;
        BOOL dark = TRUE;
        DwmSetWindowAttribute(h, 20, &dark, sizeof(dark));
        DwmSetWindowAttribute(h, 19, &dark, sizeof(dark));
    }

    // Re-tint the stock controls under a window, following the current
    // mode. Call again on a night-vision toggle: unlike the title bar,
    // these colours DO change with it.
    //
    // Found by class rather than by a list of handles, for the same
    // reason D3 gave up its list -- a control added later should not
    // have to be registered somewhere to be coloured.
    inline void StyleStockControls(HWND top) {
        if (!top) return;
        EnumChildWindows(top, [](HWND c, LPARAM) -> BOOL {
            wchar_t cls[64] = {};
            if (GetClassNameW(c, cls, 64) <= 0) return TRUE;

            if (_wcsicmp(cls, WC_LISTVIEWW) == 0) {
                const COLORREF bg = NV(RGB(10, 18, 28));
                const COLORREF fg = NV(RGB(190, 214, 230));
                ListView_SetBkColor(c, bg);
                ListView_SetTextBkColor(c, bg);
                ListView_SetTextColor(c, fg);
                // The header is deliberately left alone. It is a
                // separate control that takes its face from
                // COLOR_BTNFACE once de-themed, so stripping its theme
                // trades a themed grey band for a classic grey one and
                // changes how it looks in DAY mode for no night-vision
                // gain. Colouring it properly means NM_CUSTOMDRAW,
                // which is the same custom-draw work this entry
                // deferred for the buttons.
                SetWindowTheme(c, L"", L"");
                InvalidateRect(c, nullptr, TRUE);
            } else if (_wcsicmp(cls, L"Edit") == 0 ||
                       _wcsicmp(cls, L"ComboBox") == 0) {
                // Tab controls are not in this list for the same
                // reason as the header: de-theming one makes it
                // classic grey rather than dark.
                // De-theme so the app's WM_CTLCOLOR* brushes are what
                // actually gets used. The control panel has always done
                // this; the other windows never did.
                SetWindowTheme(c, L"", L"");
                InvalidateRect(c, nullptr, TRUE);
            }
            return TRUE;
        }, 0);
    }

} // namespace Theme
} // namespace raduk::gui
