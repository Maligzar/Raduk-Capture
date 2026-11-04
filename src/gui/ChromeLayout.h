#pragma once
// ============================================================
// ChromeLayout.h — Phase 119.3: where everything in a title bar is
//
// Pure, as ToolbarLayout.h is, because a custom title bar's failures are
// geometric: a resize edge nobody can catch, a close button one pixel
// from a drag area. WindowChrome.h does the Windows half.
//
// Sizes are at 96 dpi and scale, so a 4K panel gets this right the day
// one arrives.
// ============================================================

namespace raduk::gui::chrome {

enum class Zone {
    Client,
    Caption,
    Minimize,
    Maximize,
    Close,
    Top,
    TopLeft,
    TopRight,
};

struct Metrics {
    int captionH = 32;
    int buttonW  = 42;
    int resize   = 6;     // the top edge's grab, inside the caption
    int inset    = 12;    // left of the title
};

inline Metrics ForDpi(int dpi) {
    if (dpi <= 0) dpi = 96;
    auto s = [dpi](int v) { return (v * dpi + 48) / 96; };
    Metrics m;
    m.captionH = s(32);
    m.buttonW  = s(42);
    m.resize   = s(6);
    m.inset    = s(12);
    return m;
}

struct Flags {
    bool minimize = true;
    bool maximize = true;
    bool resizable = true;
};

struct Box { int left = 0, top = 0, right = 0, bottom = 0; };

inline bool In(const Box& b, int x, int y) {
    return x >= b.left && x < b.right && y >= b.top && y < b.bottom;
}

struct Buttons { Box min, max, close; };

// Right to left: close, then maximise, then minimise. A button a window
// does not have is an empty box, which holds no point.
inline Buttons LayoutButtons(int clientW, const Metrics& m, const Flags& f) {
    Buttons b;
    int r = clientW;
    b.close = {r - m.buttonW, 0, r, m.captionH};
    r -= m.buttonW;
    if (f.maximize) { b.max = {r - m.buttonW, 0, r, m.captionH}; r -= m.buttonW; }
    if (f.minimize) { b.min = {r - m.buttonW, 0, r, m.captionH}; }
    return b;
}

// Where the title may run to, so a long one cannot go under the buttons.
inline int TitleRight(int clientW, const Metrics& m, const Flags& f) {
    const Buttons b = LayoutButtons(clientW, m, f);
    int l = b.close.left;
    if (f.maximize) l = b.max.left;
    if (f.minimize) l = b.min.left;
    return l - m.inset;
}

// In client pixels. Maximised there is no edge to grab, and the very top
// row must work as the button under it: throwing the mouse at the top
// right corner of a maximised window has to hit close.
inline Zone HitTest(int x, int y, int clientW, const Metrics& m,
                    const Flags& f, bool maximized) {
    if (y < 0 || y >= m.captionH) return Zone::Client;
    if (f.resizable && !maximized && y < m.resize) {
        if (x < m.resize * 2) return Zone::TopLeft;
        if (x >= clientW - m.resize * 2) return Zone::TopRight;
        return Zone::Top;
    }
    const Buttons b = LayoutButtons(clientW, m, f);
    if (In(b.close, x, y)) return Zone::Close;
    if (In(b.max, x, y))   return Zone::Maximize;
    if (In(b.min, x, y))   return Zone::Minimize;
    return Zone::Caption;
}

} // namespace raduk::gui::chrome
