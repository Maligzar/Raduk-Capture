#pragma once
// ============================================================
// ToolbarLayout.h - where every toolbar button goes (phase 111)
//
// One row had grown to 24 buttons needing 1,508 px, in a strip of
// about 1,360 on a 1920 monitor, with nothing to stop it: 8b, 16b
// and EXIT were painted past the edge and could not be clicked. So
// the layout is here, as arithmetic, where tests/toolbar_layout.cpp
// can hold it to the narrowest window the app allows. ToolbarStrip.h
// needs windows.h and a screen; this needs neither.
//
// The strip's height also lives here, because it was the literal 30
// in four files, two of them with a "must match" comment.
// ============================================================

namespace raduk::gui {

// Command IDs for toolbar buttons
enum ToolbarCmd : int {
    ID_TB_FIRST     = 3000,
    ID_TB_CENTROID  = 3000,
    ID_TB_LOCK      = 3001,
    ID_TB_ROI       = 3002,
    ID_TB_COLOR     = 3003,
    ID_TB_MAP       = 3004,
    ID_TB_GOTO      = 3005,
    ID_TB_RETICLE   = 3006,
    ID_TB_CLIP      = 3007,
    ID_TB_HELP      = 3008,
    ID_TB_AI        = 3009,
    ID_TB_EXIT      = 3010,
    ID_TB_GHOST     = 3011,
    // Capture format group. Resolution had no control at all: once the
    // ROI was cropped, by [ROI] or by dragging a box, nothing could widen
    // it again short of restarting. Bit depth was hardcoded RAW8 in every
    // call site, so the 16-bit the DSO profile asks for never happened.
    ID_TB_RES_FULL  = 3012,
    ID_TB_RES_1920  = 3013,
    ID_TB_RES_1280  = 3014,
    ID_TB_RES_640   = 3015,
    ID_TB_BIT8      = 3016,
    ID_TB_BIT16     = 3017,
    ID_TB_FRAME     = 3018,   // Phase 39: framing shot (boosted 5s preview)
    ID_TB_HW        = 3019,   // Phase 46: connect / disconnect all hardware
    ID_TB_DASH      = 3020,   // the browser dashboard, signed in
    ID_TB_STACK     = 3021,   // Phase 106: the last recording, in AutoStakkert
    ID_TB_FOLLOW    = 3022,   // Phase 107: move the ROI to hold the target
    ID_TB_DEROT     = 3023,   // Phase 108: warn or stop at the derotation window
    ID_TB_RUNS      = 3024,   // Phase 113: a sequence of runs
    ID_TB_LAST      = 3024,
};

static constexpr int kToolbarButtons = ID_TB_LAST - ID_TB_FIRST + 1;

// Physical pixels.
static constexpr int kToolbarBtnH   = 24;
static constexpr int kToolbarMargin = 6;   // strip edge to first button
static constexpr int kToolbarPad    = 4;   // between buttons
static constexpr int kToolbarGroup  = 8;   // extra, between groups in a row
static constexpr int kToolbarSplit  = 12;  // least between left and right
// Phase 120: the strip across the top is now the status strip, and the
// buttons live in the rail. The name stays because everything that sits
// below the strip already asks it.
static constexpr int TOOLBAR_HEIGHT = 32;
static constexpr int TOOLBAR_RAIL_W = 64;

// The narrowest strip there can be: WM_GETMINMAXINFO holds the preview
// to 600 px, which is 20 past the panel's 560 once the scrollbar gap is
// counted, and the strip spans everything left of the panel.
static constexpr int kToolbarMinWidth = 620;

// The monospace face's advance at Type::BODY, rounded up, and the
// least room a label needs beyond its text.
static constexpr int kToolbarCharW  = 8;
static constexpr int kToolbarTextIn = 8;

struct ToolbarItem {
    int            id;
    const wchar_t* label;    // what it says at rest
    const wchar_t* longest;  // the widest thing it ever says
    wchar_t        glyph;    // Segoe MDL2 Assets, or 0 for words alone
};

// Phase 120: one home for each control. The toolbar's two rows mixed
// four jobs, so they went where the job is.

// What opens something else, or leaves: the rail down the left edge.
static constexpr ToolbarItem kRailItems[] = {
    { ID_TB_MAP,   L"MAP",    L"MAP",    L'\xE909' },
    { ID_TB_GOTO,  L"GOTO",   L"GOTO",   L'\xE707' },
    { ID_TB_RUNS,  L"RUNS",   L"RUNS",   L'\xE8EE' },
    { ID_TB_STACK, L"STACK",  L"STACK",  L'\xE81E' },
    { ID_TB_DASH,  L"DASH",   L"DASH",   L'\xE9D9' },
    { ID_TB_AI,    L"AI",     L"AI",     L'\xE8BD' },
    { ID_TB_HW,    L"HW OFF", L"HW OFF", L'\xE950' },
    { ID_TB_HELP,  L"HELP",   L"HELP",   L'\xE9CE' },
    { ID_TB_EXIT,  L"EXIT",   L"EXIT",   L'\xE7E8' },   // always last, at the foot
};
// What changes how the image is shown: over the image.
static constexpr ToolbarItem kViewItems[] = {
    { ID_TB_COLOR,   L"COLOR",   L"COLOR",   0 },
    { ID_TB_RETICLE, L"RETICLE", L"RETICLE", 0 },
    { ID_TB_CLIP,    L"CLIP",    L"CLIP",    0 },
    { ID_TB_GHOST,   L"GHOST",   L"GHOST",   0 },
};
// In the panel's Camera tab.
static constexpr ToolbarItem kCameraItems[] = {
    { ID_TB_RES_FULL, L"FULL",  L"FULL",  0 },
    { ID_TB_RES_1920, L"1920",  L"1920",  0 },
    { ID_TB_RES_1280, L"1280",  L"1280",  0 },
    { ID_TB_RES_640,  L"640",   L"640",   0 },
    { ID_TB_BIT8,     L"8b",    L"8b",    0 },
    { ID_TB_BIT16,    L"16b",   L"16b",   0 },
    { ID_TB_FRAME,    L"FRAME", L"FRAME", 0 },
};
// In the panel's Mount tab.
static constexpr ToolbarItem kMountItems[] = {
    { ID_TB_CENTROID, L"CENTROID", L"CENTROID",       0 },
    { ID_TB_LOCK,     L"LOCK",     L"LOCK CAL\u2026", 0 },
    { ID_TB_ROI,      L"ROI",      L"ROI",            0 },
    { ID_TB_FOLLOW,   L"FOLLOW",   L"FOLLOW",         0 },
    { ID_TB_DEROT,    L"DEROT WARN", L"DEROT WARN",   0 },
};

template <int N>
constexpr int ItemCount(const ToolbarItem (&)[N]) { return N; }

static constexpr int kRailBtnH  = 56;
static constexpr int kRailGap   = 2;
static constexpr int kRailTop   = 6;
static constexpr int kViewBtnH  = 28;
static constexpr int kViewPad   = 2;

// Where a rail button goes. Exit stands apart at the foot, so a slip
// off AI or HELP cannot land on it.
inline int RailY(int index, int count, int railH) {
    if (index == count - 1) return railH - kRailBtnH - kRailTop;
    return kRailTop + index * (kRailBtnH + kRailGap);
}

inline int ItemTextWidth(const wchar_t* s) {
    int n = 0;
    while (s[n]) ++n;
    return n * kToolbarCharW + kToolbarTextIn;
}

inline int ViewStripWidth() {
    int w = 0;
    for (const ToolbarItem& d : kViewItems) w += ItemTextWidth(d.longest) + kViewPad;
    return w - kViewPad;
}

// "[HW] 3/3" is pinned by tests/hardware_link.cpp and the log and the
// notifications say it that way. The button does not need the brackets:
// they were how a stock button told itself apart from the others.
inline int StripBrackets(const char* in, wchar_t* out, int cap) {
    int n = 0;
    for (; *in && n < cap - 1; ++in) {
        if (*in == '[' || *in == ']') continue;
        out[n++] = (wchar_t)(unsigned char)*in;
    }
    out[n] = 0;
    return n;
}

// PreviewRenderer names the reticle "RETICLE CIRCLE". The word is
// redundant once it is lit, and CIRCLE is what fits.
inline const wchar_t* ReticleButtonLabel(const wchar_t* lbl) {
    const wchar_t* prefix = L"RETICLE ";
    const wchar_t* p = lbl;
    while (*prefix && *p == *prefix) { ++p; ++prefix; }
    return (*prefix == 0 && *p != 0) ? p : lbl;
}

} // namespace raduk::gui
