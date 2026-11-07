// ============================================================
// specimen -- phase 132.2: the palettes, side by side, before any
// window changes
//
//   specimen <assets\fonts folder> <out.html>
//
// Writes one self-contained page: today's palette and the candidates
// for the "precision instrument" direction, each as a sample instrument
// panel in day and in night vision, with its contrast against the
// floors in WebTheme.h. The owner picks one here; 132.3 then puts it
// into Theme.h. Nothing in the application reads this file.
//
// Built and run by tests\specimen.ps1. Windows only, because it reads
// today's palette through WebPalette.h, which reads Theme.h.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "gui/WebPalette.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace raduk;
using namespace raduk::core::web;

// ── The candidates ──────────────────────────────────────────
//
// All three share the direction the owner chose: warm graphite rather
// than blue-black, one muted signal colour rather than four neon ones,
// and warning, fault and live kept for state only. They differ in the
// signal colour, which is the one decision left. Every one clears the
// contrast floors; the page shows the numbers.
static std::vector<Palette> Candidates() {
    auto make = [](const char* name, const char* note, uint32_t v, uint32_t d, uint32_t pa,
                   uint32_t l, uint32_t t, uint32_t dim, uint32_t a, uint32_t w, uint32_t f,
                   uint32_t live) {
        Palette p;
        p.name = name;
        p.note = note;
        p.day[Void] = v; p.day[Deep] = d; p.day[Panel] = pa; p.day[Line] = l;
        p.day[Text] = t; p.day[Dim] = dim; p.day[Accent] = a; p.day[Warn] = w;
        p.day[Fault] = f; p.day[Live] = live;
        return p;
    };
    return {
        make("A. Brass",
             "Warm graphite with a brass signal. The most instrument-like: it reads as an "
             "engraved dial rather than a screen. Warning moves to burnt orange so it "
             "cannot be mistaken for the signal.",
             0xFF0B0C0D, 0xFF111214, 0xFF18191C, 0xFF2A2C30, 0xFFE4E1DA, 0xFF8F8B83,
             0xFFC9A35B, 0xFFE0873A, 0xFFE5484D, 0xFF7DB38E),
        make("B. Verdigris",
             "The same graphite with a muted blue-green signal. Keeps a family resemblance "
             "to today's cyan, so the app still looks like itself, without the glare.",
             0xFF0B0C0D, 0xFF111214, 0xFF18191C, 0xFF2A2C30, 0xFFE4E1DA, 0xFF8F8B83,
             0xFF5FA89C, 0xFFD9A441, 0xFFE5484D, 0xFF8DB87A),
        make("C. Steel",
             "A slightly cooler graphite with a desaturated steel-blue signal. The quietest "
             "of the three: the signal recedes until something is active.",
             0xFF0C0D0F, 0xFF121316, 0xFF191B1F, 0xFF2B2E33, 0xFFE2E2DF, 0xFF8E9096,
             0xFF8FA7C4, 0xFFD9A441, 0xFFE5484D, 0xFF7DB38E),
    };
}

static std::vector<uint8_t> ReadFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

static std::string Esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '"') o += "&quot;";
        else o += c;
    }
    return o;
}

static const char* RoleName(int r) {
    static const char* const k[kRoles] = { "Void", "Deep", "Panel", "Line", "Text",
                                           "Dim", "Signal", "Warning", "Fault", "Live" };
    return k[r];
}

// One sample panel. The class names are BaseCss's plus a few for the
// controls, which on Win32 are owner-drawn and here only show intent.
static std::string Mock(const std::string& id) {
    return
        "<div class=\"mock\" id=\"" + id + "\">"
        "<div class=\"mbar\"><span class=\"label\">Camera</span>"
        "<span class=\"sig live\">Connected</span></div>"
        "<div class=\"cells\">"
        "<div class=\"cell\"><div class=\"label\">Exposure</div><div class=\"readout v\">12.00<span class=\"u\">ms</span></div></div>"
        "<div class=\"cell\"><div class=\"label\">Gain</div><div class=\"readout v\">280</div></div>"
        "<div class=\"cell\"><div class=\"label\">Frame rate</div><div class=\"readout v\">118.4<span class=\"u\">fps</span></div></div>"
        "<div class=\"cell\"><div class=\"label\">Sensor</div><div class=\"readout v\">&minus;4.2<span class=\"u\">&deg;C</span></div></div>"
        "</div>"
        "<div class=\"row\"><button class=\"btn primary\">Record</button>"
        "<button class=\"btn\">Autofocus</button><button class=\"btn\" disabled>Plate solve</button></div>"
        "<div class=\"row\"><span class=\"sig fault\">Rec 00:42</span><span class=\"sig warn\">Dew risk</span>"
        "<span class=\"sig accent\">Jupiter</span></div>"
        "<table><tr><th>Target</th><th>Alt</th><th>Size</th></tr>"
        "<tr><td>Jupiter</td><td class=\"n\">41.2&deg;</td><td class=\"n\">44.1&Prime;</td></tr>"
        "<tr><td>Saturn</td><td class=\"n\">18.7&deg;</td><td class=\"n\">18.3&Prime;</td></tr>"
        "<tr><td class=\"dim\">Mars</td><td class=\"n dim\">&minus;6.0&deg;</td><td class=\"n dim\">5.9&Prime;</td></tr>"
        "</table></div>";
}

static std::string Section(const Palette& p, const std::string& id, bool today) {
    std::string o;
    o += "<style>" + VarsCss(p, "#" + id + "-day", false) + VarsCss(p, "#" + id + "-night", true) +
         "</style>";
    o += "<section class=\"pal" + std::string(today ? " today" : "") + "\"><header>";
    o += "<h2>" + Esc(p.name) + "</h2><p class=\"note\">" + Esc(p.note) + "</p>";
    o += "<div class=\"swatches\">";
    for (int r = 0; r < kRoles; ++r)
        o += "<div class=\"sw\"><i style=\"background:" + Hex(p.day[r]) + "\"></i><span class=\"label\">" +
             RoleName(r) + "</span><span class=\"mono\">" + Hex(p.day[r]) + "</span></div>";
    o += "</div><div class=\"contrast\">";
    for (const ContrastCheck& c : CheckContrast(p)) {
        char b[96];
        snprintf(b, sizeof(b), "%s %.1f:1", RoleName(c.role), c.ratio);
        o += std::string("<span class=\"cc ") + (c.ok ? "ok" : "no") + "\" title=\"floor " +
             std::to_string((int)c.floor) + (c.floor == 7.0 ? "" : ".5") + ":1\">" + b + "</span>";
    }
    o += "</div></header><div class=\"pair\">";
    o += "<figure><figcaption class=\"label\">Day</figcaption>" + Mock(id + "-day") + "</figure>";
    o += "<figure><figcaption class=\"label\">Night vision</figcaption>" + Mock(id + "-night") +
         "</figure></div></section>";
    return o;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: specimen <assets\\fonts folder> <out.html>\n");
        return 2;
    }
    const std::filesystem::path fonts = argv[1];
    std::string faces;
    int loaded = 0;
    struct F { const char* file; const char* family; int weight; };
    for (const F& f : { F{"BarlowCondensed-Regular.ttf", kLabelFont, 400},
                        F{"BarlowCondensed-Bold.ttf",    kLabelFont, 700},
                        F{"SpaceMono-Regular.ttf",       kDataFont,  400},
                        F{"SpaceMono-Bold.ttf",          kDataFont,  700} }) {
        const std::string face = FontFaceCss(f.family, f.weight, ReadFile(fonts / f.file));
        if (!face.empty()) ++loaded;
        faces += face;
    }

    const std::vector<Palette> cands = Candidates();
    const Palette today = gui::CurrentWebPalette();

    std::string o;
    o += "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
         "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
         "<title>Palette specimen</title><style>";
    o += faces;
    o += PaletteCss(cands[0]);   // the page's own chrome, in the direction chosen
    o += BaseCss();
    o += "main{max-width:1240px;margin:0 auto;padding:28px 16px 64px}"
         ".lede{color:var(--dim);max-width:760px;margin:0 0 8px}"
         ".pal{margin:28px 0 0;padding:20px 0 0;border-top:1px solid var(--line)}"
         ".pal h2{border:0;box-shadow:none;margin:0 0 4px;font-size:22px;color:var(--text);"
         "letter-spacing:.06em;padding:0}"
         ".pal .note{color:var(--dim);margin:0 0 12px;max-width:820px}"
         ".swatches{display:grid;grid-template-columns:repeat(auto-fill,minmax(112px,1fr));gap:6px 10px;margin:0 0 10px}"
         ".sw{display:grid;grid-template-columns:18px 1fr;column-gap:8px;align-items:center;font-size:12px}"
         ".sw i{grid-row:span 2;width:18px;height:28px;border:1px solid rgba(255,255,255,.12);border-radius:2px}"
         ".sw .label{font-size:12px}.sw .mono{font-size:11px;color:var(--dim)}"
         ".contrast{display:flex;flex-wrap:wrap;gap:6px;margin:0 0 14px}"
         ".cc{font:12px 'Space Mono',monospace;padding:1px 7px;border:1px solid var(--line);border-radius:2px}"
         ".cc.ok{color:var(--dim)}.cc.no{color:var(--fault);border-color:var(--fault)}"
         ".pair{display:grid;grid-template-columns:1fr 1fr;gap:16px}"
         "figure{margin:0}figcaption{font-size:12px;margin:0 0 6px}"
         ".mock{background:var(--deep);color:var(--text);border:1px solid var(--line);border-radius:3px;"
         "padding:14px;box-shadow:inset 0 1px 0 rgba(255,255,255,.03)}"
         ".mock .label{color:var(--dim);font-size:13px}"
         ".mbar{display:flex;justify-content:space-between;align-items:center;padding-bottom:8px;"
         "margin-bottom:12px;border-bottom:1px solid var(--line);box-shadow:0 1px 0 rgba(255,255,255,.03)}"
         ".cells{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-bottom:12px}"
         ".cell{background:var(--panel);border:1px solid var(--line);border-radius:2px;padding:8px 10px;"
         "box-shadow:inset 0 1px 0 rgba(255,255,255,.035)}"
         ".cell .v{font-size:20px;line-height:1.2;margin-top:2px;color:var(--text);white-space:nowrap}"
         ".cell .u{font-size:12px;color:var(--dim);margin-left:4px}"
         ".row{display:flex;flex-wrap:wrap;gap:8px;margin:0 0 12px}"
         ".btn{font:13px/1 'Barlow Condensed',sans-serif;text-transform:uppercase;letter-spacing:.09em;"
         "color:var(--text);background:var(--panel);border:1px solid var(--line);border-radius:2px;"
         "padding:8px 14px;box-shadow:inset 0 1px 0 rgba(255,255,255,.05),0 1px 1px rgba(0,0,0,.5)}"
         ".btn.primary{color:var(--accent);border-color:var(--accent)}"
         ".btn:disabled{color:var(--dim);opacity:.55;box-shadow:none}"
         ".mock table{font-size:13px}.mock th{font-size:12px}"
         ".ramp{display:flex;flex-wrap:wrap;align-items:baseline;gap:6px 22px;margin:8px 0 0}"
         ".ramp span{color:var(--text)}.ramp em{font:11px 'Space Mono',monospace;color:var(--dim);"
         "font-style:normal;margin-left:6px}"
         "footer{margin-top:40px;color:var(--dim);font-size:13px}"
         "@media (max-width:860px){.pair{grid-template-columns:1fr}.cells{grid-template-columns:repeat(2,1fr)}}"
         "</style></head><body><main>";
    o += "<h1>Palette specimen</h1>";
    o += "<p class=\"lede\">Phase 132.2. Today's palette, then three candidates for the precision-instrument "
         "direction, each as a sample panel in day mode and in night vision. Night vision is the same for "
         "all of them by design: it keeps Theme's luminance ladder, in red only. So the choice is a daytime "
         "one. Pick a letter; 132.3 puts it into the app.</p>";
    o += "<div class=\"ramp\">";
    for (int px : {11, 13, 16, 20, 28})
        o += "<span class=\"label\" style=\"font-size:" + std::to_string(px) + "px\">Exposure<em>" +
             std::to_string(px) + "</em></span>";
    for (int px : {13, 20})
        o += "<span class=\"readout\" style=\"font-size:" + std::to_string(px) + "px\">118.4<em>" +
             std::to_string(px) + "</em></span>";
    o += "</div>";
    o += Section(today, "today", true);
    char id[8];
    for (size_t i = 0; i < cands.size(); ++i) {
        snprintf(id, sizeof(id), "p%zu", i);
        o += Section(cands[i], id, false);
    }
    o += "<footer>Written by tools/specimen.cpp from src/core/WebTheme.h. Fonts embedded: " +
         std::to_string(loaded) + " of 4. Contrast is WCAG 2 against each palette's panel background; "
         "floors are 7:1 for text and 4.5:1 for every other role.</footer>";
    o += "</main></body></html>\n";

    std::ofstream out(std::filesystem::path(argv[2]), std::ios::binary);
    out << o;
    if (!out) { fprintf(stderr, "specimen: could not write %s\n", argv[2]); return 1; }
    printf("specimen: %zu palettes, %d of 4 fonts, %zu KB -> %s\n", cands.size() + 1, loaded,
           o.size() / 1024, argv[2]);
    for (const Palette& p : cands)
        if (!PassesContrast(p)) printf("specimen: %s does NOT clear the contrast floors\n", p.name.c_str());
    return 0;
}
