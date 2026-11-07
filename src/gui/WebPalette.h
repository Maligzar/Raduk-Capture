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

} // namespace raduk::gui
