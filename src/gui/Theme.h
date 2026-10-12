#pragma once
// ============================================================
// Theme.h — Elite Raduk design tokens
// Deep-space palette: void black base, cyan primary accent,
// amber secondary, green for live/mount, red for capture.
// Typography: Space Mono (data), Barlow Condensed (labels).
// ============================================================

#include <windows.h>
#include <d2d1.h>
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
    // ACCENT HUES CONVERGE, and that is inherent rather than an
    // oversight: cyan 0,229,255 and amber 255,185,0 have luminances of
    // 182 and 186, so in a red-only palette they are the same red. Any
    // transform that kept them apart would have to reintroduce the short
    // wavelengths this exists to remove, or invent a hue-to-brightness
    // mapping that means nothing. Section headers carry their names in
    // text, which is what distinguishes them when the colour cannot.
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

    // The tint itself. Pass every colour through this at the point of
    // use, never at the point of definition -- a value tinted once and
    // stored would keep whatever the mode was when it was stored.
    inline COLORREF NV(COLORREF c) {
        if (!detail::g_nightVision) return c;
        const int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
        const int lum = (int)(0.2126f * r + 0.7152f * g + 0.0722f * b);
        // A tenth of the luminance in green keeps near-black greys from
        // reading as pure black, which is what the preview does.
        return RGB(lum, lum / 10, 0);
    }

    // The Direct2D counterpart. Same transform, different type: the
    // preview, the status bar and the sky map all paint through D2D,
    // which takes floats rather than a packed COLORREF.
    inline D2D1_COLOR_F NVf(D2D1_COLOR_F c) {
        if (!detail::g_nightVision) return c;
        const float lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
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

} // namespace Theme
} // namespace raduk::gui
