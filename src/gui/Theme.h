#pragma once
// ============================================================
// Theme.h — Elite Raduk design tokens
// Deep-space palette: void black base, cyan primary accent,
// amber secondary, green for live/mount, red for capture.
// Typography: Space Mono (data), Barlow Condensed (labels).
// ============================================================

#include <d2d1.h>
#include <cstdint>

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

    inline D2D1_COLOR_F Color(uint32_t argb) {
        float a = ((argb >> 24) & 0xFF) / 255.0f;
        float r = ((argb >> 16) & 0xFF) / 255.0f;
        float g = ((argb >> 8)  & 0xFF) / 255.0f;
        float b = ((argb)       & 0xFF) / 255.0f;
        return D2D1::ColorF(r, g, b, a);
    }

} // namespace Theme
} // namespace raduk::gui
