#pragma once
// ============================================================
// WebPalette.h -- phase 132.2: Theme.h's tokens as a web::Palette
//
// The one place the HTML side reads the window chrome's colours.
// WebTheme.h is pure and knows nothing of Theme.h; this is the bridge,
// kept in gui/ because Theme.h needs the Windows headers.
//
// tests/web_theme.cpp checks every role here against its token, and
// every night luminance in WebTheme.h against Theme's ladder, so the
// pages cannot drift from the app again.
// ============================================================

#include <vector>

#include "core/WebTheme.h"
#include "gui/Theme.h"

namespace raduk::gui {

inline core::web::Palette CurrentWebPalette() {
    namespace w = core::web;
    w::Palette p;
    p.name = "Today";
    p.note = "The palette the app paints with now.";
    p.day[w::Void]   = Theme::BG_VOID;
    p.day[w::Deep]   = Theme::DEEP;
    p.day[w::Panel]  = Theme::PANEL;
    p.day[w::Line]   = Theme::BORDER;
    p.day[w::Text]   = Theme::TEXT;
    p.day[w::Dim]    = Theme::TEXT_DIM;
    p.day[w::Accent] = Theme::ACCENT;
    p.day[w::Warn]   = Theme::AMBER;
    p.day[w::Fault]  = Theme::RED;
    p.day[w::Live]   = Theme::GREEN;
    return p;
}

// Phase 132.9: the four bundled fonts, as @font-face rules, from the
// exe's own resources (RadukCapture.rc: 301-304, RCDATA). A page the app
// writes then carries the app's type and needs nothing from the network.
// A font that is missing is skipped, and the page falls back.
inline std::string EmbeddedFontFacesCss() {
    struct F { int id; const char* family; int weight; };
    static const F fonts[] = { {301, core::web::kLabelFont, 400}, {302, core::web::kLabelFont, 700},
                               {303, core::web::kDataFont,  400}, {304, core::web::kDataFont,  700} };
    std::string css;
    const HMODULE mod = GetModuleHandleW(nullptr);
    for (const F& f : fonts) {
        HRSRC r = FindResourceW(mod, MAKEINTRESOURCEW(f.id), MAKEINTRESOURCEW(10));   // RT_RCDATA
        if (!r) continue;
        HGLOBAL g = LoadResource(mod, r);
        const uint8_t* p = g ? static_cast<const uint8_t*>(LockResource(g)) : nullptr;
        const DWORD n = SizeofResource(mod, r);
        if (!p || !n) continue;
        css += core::web::FontFaceCss(f.family, f.weight, std::vector<uint8_t>(p, p + n));
    }
    return css;
}

// Everything a page needs to look like the app: fonts, both palettes,
// and the shared base rules.
inline std::string AppPageCss() {
    return EmbeddedFontFacesCss() + core::web::PaletteCss(CurrentWebPalette()) +
           core::web::BaseCss();
}

} // namespace raduk::gui
