#pragma once
// ============================================================
// WebTheme.h -- phase 132.2: the app's look, as CSS
//
// The HTML the app writes (session stats, the dashboard, the shot diff,
// the specimen page) each carried its own palette, typed by hand, and
// they had already drifted from Theme.h. This makes the CSS from the
// same numbers the window chrome paints with, so a token changed in one
// place changes everywhere.
//
// ── What lives here and what does not ───────────────────────
//
// Pure C++, no Windows headers, so it is tested anywhere. It knows
// nothing about Theme.h: it takes a Palette, a plain table of ten role
// colours. src/gui/WebPalette.h builds that table FROM Theme.h, and
// tests/web_theme.cpp checks the two agree byte for byte. Keeping the
// adapter apart is what lets a candidate palette be rendered on the
// specimen page before it is a token.
//
// ── Night ───────────────────────────────────────────────────
//
// Night vision is not a second palette, it is a transform: each role
// collapses to a designed luminance in red only (Theme's night ladder,
// phase 96.1, red-only since 132.1). The luminances here are the
// ladder's, per role. CSS gets them as a [data-mode="night"] block, so
// a page can follow the app's mode with one attribute.
//
// ── Fonts ───────────────────────────────────────────────────
//
// The app carries Barlow Condensed and Space Mono (phase 119.1). A page
// opened in Edge does not have them -- the dashboard has been silently
// rendering in a fallback since it was written. FontFaceCss embeds the
// font bytes as base64, so a page is self-contained and makes no
// network request.
// ============================================================

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace raduk::core::web {

// ── Roles ───────────────────────────────────────────────────
//
// What a colour is FOR, not what it looks like. Ten, matching the ten
// steps of Theme's night ladder.
enum Role {
    Void,    // deepest surface: the preview's background
    Deep,    // panel background
    Panel,   // raised surface
    Line,    // hairlines and separators
    Text,    // primary readouts
    Dim,     // labels you skim
    Accent,  // the one signal colour: active, selected, camera
    Warn,    // caution
    Fault,   // danger, recording
    Live,    // mount, tracking, connected
    kRoles
};

inline const char* RoleVar(int r) {
    static const char* const k[kRoles] = {
        "--void", "--deep", "--panel", "--line", "--text",
        "--dim",  "--accent", "--warn", "--fault", "--live" };
    return (r >= 0 && r < kRoles) ? k[r] : "--unknown";
}

// Theme's night ladder, by role (Theme.h, detail::kNightLadder).
// tests/web_theme.cpp checks these against the ladder itself.
inline constexpr int kNightLum[kRoles] = {
    8,    // Void
    14,   // Deep
    24,   // Panel
    34,   // Line
    200,  // Text
    80,   // Dim
    140,  // Accent
    110,  // Warn
    240,  // Fault
    170,  // Live
};

struct Palette {
    std::string name;
    std::string note;             // one line: what it is for
    uint32_t day[kRoles] = {};    // 0xAARRGGBB, Theme's spelling
};

// ── Colour arithmetic ───────────────────────────────────────

inline std::string Hex(uint32_t argb) {
    char b[8];
    snprintf(b, sizeof(b), "#%02x%02x%02x",
             (unsigned)((argb >> 16) & 0xFF), (unsigned)((argb >> 8) & 0xFF), (unsigned)(argb & 0xFF));
    return b;
}

// Red only, as Theme::NV gives since 132.1.
inline uint32_t NightArgb(int lum) {
    if (lum < 0) lum = 0;
    if (lum > 255) lum = 255;
    return 0xFF000000u | ((uint32_t)lum << 16);
}

// WCAG 2 relative luminance and contrast ratio. Used to hold a
// candidate palette to a floor before anyone has to squint at it.
inline double RelativeLuminance(uint32_t argb) {
    auto lin = [](unsigned v) {
        const double c = v / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin((argb >> 16) & 0xFF) + 0.7152 * lin((argb >> 8) & 0xFF) +
           0.0722 * lin(argb & 0xFF);
}

inline double Contrast(uint32_t a, uint32_t b) {
    double la = RelativeLuminance(a), lb = RelativeLuminance(b);
    if (la < lb) std::swap(la, lb);
    return (la + 0.05) / (lb + 0.05);
}

// The floors a palette is held to, against its panel background.
// Primary text at AAA for body text; every other foreground role at AA.
inline constexpr double kMinTextContrast = 7.0;
inline constexpr double kMinRoleContrast = 4.5;

struct ContrastCheck { int role; double ratio; double floor; bool ok; };

inline std::vector<ContrastCheck> CheckContrast(const Palette& p) {
    std::vector<ContrastCheck> out;
    for (int r : {Text, Dim, Accent, Warn, Fault, Live}) {
        const double f = r == Text ? kMinTextContrast : kMinRoleContrast;
        const double c = Contrast(p.day[r], p.day[Deep]);
        out.push_back({r, c, f, c >= f});
    }
    return out;
}

inline bool PassesContrast(const Palette& p) {
    for (const ContrastCheck& c : CheckContrast(p)) if (!c.ok) return false;
    return true;
}

// ── CSS ─────────────────────────────────────────────────────

// "sel{--void:#..;...}" for day, or for night with `night` set.
inline std::string VarsCss(const Palette& p, const std::string& selector, bool night) {
    std::string o = selector + "{";
    for (int r = 0; r < kRoles; ++r) {
        o += RoleVar(r);
        o += ':';
        o += Hex(night ? NightArgb(kNightLum[r]) : p.day[r]);
        o += ';';
    }
    o += "color-scheme:dark}";
    return o;
}

// Day on :root, night under [data-mode="night"] on the root element.
inline std::string PaletteCss(const Palette& p) {
    return VarsCss(p, ":root", false) + VarsCss(p, ":root[data-mode=\"night\"]", true);
}

inline std::string Base64(const uint8_t* data, size_t n) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((n + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < n; i += 3) {
        const uint32_t v = (uint32_t)data[i] << 16 | (uint32_t)data[i + 1] << 8 | data[i + 2];
        o += k[v >> 18]; o += k[(v >> 12) & 63]; o += k[(v >> 6) & 63]; o += k[v & 63];
    }
    if (i < n) {
        uint32_t v = (uint32_t)data[i] << 16;
        if (i + 1 < n) v |= (uint32_t)data[i + 1] << 8;
        o += k[v >> 18]; o += k[(v >> 12) & 63];
        o += (i + 1 < n) ? k[(v >> 6) & 63] : '=';
        o += '=';
    }
    return o;
}

// One @font-face with the font inline. Empty bytes give an empty
// string, so a page whose font failed to load falls back rather than
// declaring a face with no source.
inline std::string FontFaceCss(const std::string& family, int weight,
                               const std::vector<uint8_t>& ttf) {
    if (ttf.empty()) return {};
    return "@font-face{font-family:'" + family + "';font-weight:" + std::to_string(weight) +
           ";font-style:normal;font-display:block;src:url(data:font/ttf;base64," +
           Base64(ttf.data(), ttf.size()) + ") format('truetype')}";
}

// The family names the CSS uses. The app's DirectWrite name for Barlow
// Condensed is "Barlow" at a condensed width (BundledFonts.h); a page
// declares its own face, so it can use the name people know.
inline constexpr const char* kLabelFont = "Barlow Condensed";
inline constexpr const char* kDataFont  = "Space Mono";

// The instrument look, shared by every page the app writes.
//
//   Labels   Barlow Condensed, upper case, tracked: an engraved legend,
//            not a heading.
//   Readouts Space Mono with tabular figures, so columns of numbers
//            line up and a changing value does not jitter.
//   Rules    a 1 px hairline in --line with a 1 px highlight above it,
//            which reads as a groove cut into the surface.
//   Signals  shape and text first, colour second (.sig): a state still
//            reads in night vision, where every colour is red.
//   Motion   none that is not asked for; reduced-motion removes the rest.
inline std::string BaseCss() {
    return
        "*{box-sizing:border-box}"
        "html{background:var(--void);-webkit-text-size-adjust:100%}"
        "body{margin:0;background:var(--deep);color:var(--text);"
        "font:15px/1.45 'Segoe UI',system-ui,sans-serif}"
        ".label,h2,h3,th{font-family:'Barlow Condensed','Segoe UI',sans-serif;"
        "text-transform:uppercase;letter-spacing:.09em;font-weight:400;color:var(--dim)}"
        "h1{font-family:'Barlow Condensed','Segoe UI',sans-serif;font-weight:700;"
        "letter-spacing:.02em;font-size:28px;margin:0 0 4px;color:var(--text)}"
        "h2{font-size:15px;margin:32px 0 10px;padding-bottom:6px;"
        "border-bottom:1px solid var(--line);box-shadow:0 1px 0 rgba(255,255,255,.03)}"
        "h3{font-size:13px;margin:22px 0 8px}"
        ".readout,td.n,.mono{font-family:'Space Mono',ui-monospace,monospace;"
        "font-variant-numeric:tabular-nums;font-feature-settings:'tnum' 1}"
        ".dim{color:var(--dim)}"
        ".surface{background:var(--panel);border:1px solid var(--line);border-radius:3px;"
        "box-shadow:inset 0 1px 0 rgba(255,255,255,.035),0 1px 2px rgba(0,0,0,.45)}"
        "table{border-collapse:collapse;width:100%;font-size:14px}"
        "th{text-align:left;font-size:13px;padding:6px 10px;border-bottom:1px solid var(--line)}"
        "td{padding:6px 10px;border-bottom:1px solid var(--line)}"
        ".sig{display:inline-flex;align-items:center;gap:6px;padding:2px 8px 2px 6px;"
        "border:1px solid currentColor;border-radius:2px;font-family:'Barlow Condensed',sans-serif;"
        "text-transform:uppercase;letter-spacing:.08em;font-size:13px;line-height:18px}"
        ".sig::before{content:'';width:7px;height:7px;background:currentColor}"
        ".sig.fault{color:var(--fault)}.sig.fault::before{border-radius:50%}"
        ".sig.warn{color:var(--warn)}"
        ".sig.warn::before{width:0;height:0;background:none;border-left:4px solid transparent;"
        "border-right:4px solid transparent;border-bottom:7px solid currentColor}"
        ".sig.live{color:var(--live)}.sig.live::before{border-radius:1px}"
        ".sig.accent{color:var(--accent)}.sig.accent::before{transform:rotate(45deg);width:6px;height:6px}"
        "a{color:var(--accent)}"
        ":focus-visible{outline:1px solid var(--accent);outline-offset:2px}"
        "@media (prefers-reduced-motion:reduce){*{transition:none!important;animation:none!important}}";
}

} // namespace raduk::core::web
