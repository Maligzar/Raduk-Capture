#pragma once
// ============================================================
// ShotDiff.h -- phase 132.0: did a UI change touch only what it meant to?
//
// The scenario runner's `shot NAME` (phase 125, WindowShot.h) writes one
// PNG per visible window, NAME_<title>_<n>.png. This compares two such
// folders, a reference taken before a change and a run taken after, and
// says which windows changed.
//
// ── Why a noise floor ───────────────────────────────────────
//
// Two runs of the same build are not pixel-identical. Clocks tick, the
// sky map turns, telemetry graphs scroll and the emulated seeing moves
// the planet. A plain "any pixel differs" verdict would flag every live
// window on every run, which teaches the reader to ignore the report.
//
// So the reference is taken twice, and the share of pixels that differs
// between those two runs of the SAME build is each window's noise floor.
// A window counts as changed only when it differs from the reference by
// clearly more than it differs from itself. A window whose size changed
// always counts.
//
// ── Why the index is dropped ────────────────────────────────
//
// The trailing _<n> is the window's place in EnumWindows order, which
// is z-order and moves between runs. Matching by it would pair the
// histogram with the sky map. Windows are matched by shot and title;
// two windows with the same title are told apart by order of index,
// with the size as a check.
//
// Pure C++ with no Windows headers, so it is tested anywhere. Loading
// and writing PNGs is tools/shotdiff.cpp's job.
// ============================================================

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace raduk::core::shotdiff {

// ── Thresholds ──────────────────────────────────────────────

// A channel must move by more than this to count. Below it is dithering,
// ClearType re-rasterising a glyph a subpixel over, and gradient banding:
// nothing a person would call a change.
inline constexpr int kChannelTolerance = 8;

// A window is changed when its differing share exceeds
//     noise * kNoiseFactor + kMinFraction
// The factor gives a live window room to be busier on one run than it
// was on the two reference runs; the floor keeps a static window from
// being called changed over a handful of pixels (a blinking caret).
inline constexpr double kNoiseFactor = 2.0;
inline constexpr double kMinFraction = 0.001;   // 0.1 per cent

// ── File names ──────────────────────────────────────────────

struct ShotName {
    std::string shot;    // "day", "night"
    std::string title;   // the sanitised window title
    int index = -1;      // EnumWindows order; unstable, used only to split ties
};

// "day_GoTo_Panel_3.png" -> {day, GoTo_Panel, 3}. False for anything that
// is not a window shot, including the renderer's NAME_preview.png, which
// is a live camera frame and would be noise by construction.
inline bool ParseShotName(const std::string& file, ShotName& out) {
    std::string s = file;
    const size_t slash = s.find_last_of("\\/");
    if (slash != std::string::npos) s = s.substr(slash + 1);
    if (s.size() < 5) return false;
    std::string ext = s.substr(s.size() - 4);
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    if (ext != ".png") return false;
    s = s.substr(0, s.size() - 4);

    const size_t first = s.find('_');
    const size_t last  = s.find_last_of('_');
    if (first == std::string::npos || last == first) return false;   // no title
    const std::string idx = s.substr(last + 1);
    if (idx.empty() || !std::all_of(idx.begin(), idx.end(),
                                    [](char c) { return std::isdigit((unsigned char)c) != 0; }))
        return false;
    out.shot  = s.substr(0, first);
    out.title = s.substr(first + 1, last - first - 1);
    out.index = std::atoi(idx.c_str());
    return !out.shot.empty() && !out.title.empty();
}

// Stable keys for one folder's files: "shot/title", and "shot/title#2"
// for a second window with the same title, by order of index.
// Returns key -> file, skipping anything that is not a window shot.
inline std::map<std::string, std::string> KeyFiles(const std::vector<std::string>& files) {
    struct Item { ShotName n; std::string file; };
    std::vector<Item> items;
    for (const std::string& f : files) {
        ShotName n;
        if (ParseShotName(f, n)) items.push_back({n, f});
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.n.shot != b.n.shot)   return a.n.shot < b.n.shot;
        if (a.n.title != b.n.title) return a.n.title < b.n.title;
        return a.n.index < b.n.index;
    });
    std::map<std::string, std::string> out;
    std::map<std::string, int> seen;
    for (const Item& it : items) {
        const std::string base = it.n.shot + "/" + it.n.title;
        const int k = ++seen[base];
        out[k == 1 ? base : base + "#" + std::to_string(k)] = it.file;
    }
    return out;
}

// ── Pixels ──────────────────────────────────────────────────

// Four bytes a pixel, any channel order, as long as both sides agree.
// The fourth byte is ignored: WindowShot forces it opaque.
struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    bool Valid() const { return w > 0 && h > 0 && px.size() == (size_t)w * h * 4; }
};

struct Diff {
    bool   sizeChanged = false;
    long   changed = 0;      // pixels past the tolerance
    long   total   = 0;
    double fraction = 0.0;   // changed / total; 1 when the size changed
    int x0 = 0, y0 = 0, x1 = -1, y1 = -1;   // bounding box of the change, inclusive
    std::vector<uint8_t> mask;              // w*h, 1 where changed; empty on a size change
};

inline Diff Compare(const Image& a, const Image& b, int tolerance = kChannelTolerance) {
    Diff d;
    if (!a.Valid() || !b.Valid() || a.w != b.w || a.h != b.h) {
        d.sizeChanged = true;
        d.fraction = 1.0;
        return d;
    }
    d.total = (long)a.w * a.h;
    d.mask.assign((size_t)d.total, 0);
    d.x0 = a.w; d.y0 = a.h;
    for (int y = 0; y < a.h; ++y) {
        for (int x = 0; x < a.w; ++x) {
            const size_t i = ((size_t)y * a.w + x) * 4;
            bool hit = false;
            for (int c = 0; c < 3 && !hit; ++c)
                hit = std::abs((int)a.px[i + c] - (int)b.px[i + c]) > tolerance;
            if (!hit) continue;
            d.mask[(size_t)y * a.w + x] = 1;
            ++d.changed;
            d.x0 = std::min(d.x0, x); d.y0 = std::min(d.y0, y);
            d.x1 = std::max(d.x1, x); d.y1 = std::max(d.y1, y);
        }
    }
    if (d.changed == 0) { d.x0 = d.y0 = 0; d.x1 = d.y1 = -1; }
    d.fraction = (double)d.changed / (double)d.total;
    return d;
}

// The "after" image with every changed pixel painted solid and the rest
// dimmed to a quarter, so the change is the only thing that reads.
// Channel order follows the input. Empty on a size change.
inline Image Heatmap(const Image& after, const Diff& d,
                     uint8_t c0 = 0x3C, uint8_t c1 = 0x3B, uint8_t c2 = 0xFF) {
    Image out;
    if (d.sizeChanged || !after.Valid() || d.mask.size() != (size_t)after.w * after.h) return out;
    out.w = after.w; out.h = after.h;
    out.px = after.px;
    for (size_t p = 0; p < d.mask.size(); ++p) {
        uint8_t* q = &out.px[p * 4];
        if (d.mask[p]) { q[0] = c0; q[1] = c1; q[2] = c2; }
        else           { q[0] /= 4; q[1] /= 4; q[2] /= 4; }
        q[3] = 255;
    }
    return out;
}

inline bool IsChanged(const Diff& d, double noise) {
    if (d.sizeChanged) return true;
    return d.fraction > noise * kNoiseFactor + kMinFraction;
}

// ── The noise file ──────────────────────────────────────────
//
// One line a window: key, a tab, the differing share between the two
// reference runs. Lines starting with '#' are comments.

inline std::string FormatNoise(const std::map<std::string, double>& noise) {
    std::string s = "# ShotDiff noise floor: share of pixels that differ between two\n"
                    "# runs of the same build. Written by ui-baseline.ps1 -Save.\n";
    char buf[64];
    for (const auto& [k, v] : noise) {
        snprintf(buf, sizeof(buf), "\t%.6f\n", v);
        s += k + buf;
    }
    return s;
}

inline std::map<std::string, double> ParseNoise(const std::string& text) {
    std::map<std::string, double> out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string ln = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        if (ln.empty() || ln[0] == '#') continue;
        const size_t tab = ln.find('\t');
        if (tab == std::string::npos || tab == 0) continue;
        out[ln.substr(0, tab)] = std::atof(ln.c_str() + tab + 1);
    }
    return out;
}

// ── What the stage said it would change ─────────────────────
//
// A comma list of title fragments, case-insensitive: "GoTo,Sky_Map".
// An empty list expects nothing to change.

inline std::vector<std::string> SplitExpect(const std::string& list) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        size_t a = cur.find_first_not_of(" \t"), b = cur.find_last_not_of(" \t");
        if (a != std::string::npos) out.push_back(cur.substr(a, b - a + 1));
        cur.clear();
    };
    for (char c : list) { if (c == ',') flush(); else cur += c; }
    flush();
    return out;
}

inline bool Expected(const std::string& key, const std::vector<std::string>& expect) {
    std::string k = key;
    for (char& c : k) c = (char)std::tolower((unsigned char)c);
    for (std::string e : expect) {
        for (char& c : e) c = (char)std::tolower((unsigned char)c);
        if (!e.empty() && k.find(e) != std::string::npos) return true;
    }
    return false;
}

// ── One window's row in the report ──────────────────────────

enum class Verdict { Same, Changed, Expected, Missing, Added };

struct Row {
    std::string key;
    Verdict verdict = Verdict::Same;
    double fraction = 0.0;
    double noise = 0.0;
    Diff diff;               // mask dropped before reporting
    std::string before, after, heat;   // URLs or paths for the page; may be empty
};

inline const char* VerdictName(Verdict v) {
    switch (v) {
        case Verdict::Same:     return "same";
        case Verdict::Changed:  return "CHANGED";
        case Verdict::Expected: return "changed, expected";
        case Verdict::Missing:  return "MISSING";
        case Verdict::Added:    return "NEW";
    }
    return "?";
}

// Unexpected outcomes: what makes the run fail.
inline bool IsProblem(Verdict v) {
    return v == Verdict::Changed || v == Verdict::Missing || v == Verdict::Added;
}

inline Verdict Judge(const std::string& key, const Diff& d, double noise,
                     const std::vector<std::string>& expect) {
    if (!IsChanged(d, noise)) return Verdict::Same;
    return Expected(key, expect) ? Verdict::Expected : Verdict::Changed;
}

// Problems first, then expected changes, then the rest; by key within each.
inline void SortRows(std::vector<Row>& rows) {
    auto rank = [](Verdict v) {
        return IsProblem(v) ? 0 : v == Verdict::Expected ? 1 : 2;
    };
    std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
        if (rank(a.verdict) != rank(b.verdict)) return rank(a.verdict) < rank(b.verdict);
        return a.key < b.key;
    });
}

// ── The page ────────────────────────────────────────────────

inline std::string HtmlEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;";  break;
            case '<': o += "&lt;";   break;
            case '>': o += "&gt;";   break;
            case '"': o += "&quot;"; break;
            default:  o += c;
        }
    }
    return o;
}

inline std::string Percent(double f) {
    char b[32];
    snprintf(b, sizeof(b), f >= 0.1 ? "%.0f%%" : f >= 0.001 ? "%.2f%%" : "%.3f%%", f * 100.0);
    return b;
}

// A contact sheet: one section a window, before / after / where it
// changed. Deliberately plain; 132.2 gives the app's HTML its own look,
// and this page is a tool, not a surface.
inline std::string Report(const std::vector<Row>& rows, const std::string& title,
                          const std::string& expectList) {
    int problems = 0, expected = 0;
    for (const Row& r : rows) {
        if (IsProblem(r.verdict)) ++problems;
        else if (r.verdict == Verdict::Expected) ++expected;
    }
    std::string o;
    o += "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
         "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
         "<title>" + HtmlEscape(title) + "</title><style>"
         ":root{color-scheme:dark;--bg:#111214;--panel:#1a1b1e;--line:#2a2c30;--text:#d8d9db;"
         "--dim:#8a8d93;--bad:#ff6b6b;--ok:#7fd18b;--exp:#e0b45a}"
         "body{margin:0;background:var(--bg);color:var(--text);font:14px/1.45 system-ui,sans-serif}"
         "main{max-width:1500px;margin:0 auto;padding:24px 16px}"
         "h1{font-size:20px;margin:0 0 4px}.dim{color:var(--dim)}"
         "table{border-collapse:collapse;margin:16px 0 28px;font-variant-numeric:tabular-nums}"
         "th,td{text-align:left;padding:5px 12px;border-bottom:1px solid var(--line)}"
         "th{color:var(--dim);font-weight:500}"
         ".bad{color:var(--bad);font-weight:600}.ok{color:var(--ok)}.exp{color:var(--exp)}"
         "section{background:var(--panel);border:1px solid var(--line);border-radius:6px;"
         "padding:12px 14px;margin:0 0 16px}"
         "section h2{font-size:15px;margin:0 0 10px}"
         ".trio{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}"
         ".trio figure{margin:0}.trio figcaption{color:var(--dim);font-size:12px;margin-bottom:4px}"
         ".trio img{max-width:100%;border:1px solid var(--line);image-rendering:pixelated}"
         "@media (max-width:900px){.trio{grid-template-columns:1fr}}"
         "</style></head><body><main>";
    o += "<h1>" + HtmlEscape(title) + "</h1>";
    o += "<p class=\"dim\">" + std::to_string(rows.size()) + " windows. ";
    o += problems ? "<span class=\"bad\">" + std::to_string(problems) + " unexpected</span>"
                  : std::string("<span class=\"ok\">nothing unexpected</span>");
    o += ", " + std::to_string(expected) + " changed as expected. Expected: ";
    o += expectList.empty() ? "nothing" : HtmlEscape(expectList);
    o += ".</p>";

    o += "<table><tr><th>Window</th><th>Verdict</th><th>Changed</th><th>Noise</th>"
         "<th>Where</th></tr>";
    for (const Row& r : rows) {
        const char* cls = IsProblem(r.verdict) ? "bad"
                        : r.verdict == Verdict::Expected ? "exp" : "ok";
        std::string where = "&ndash;";
        if (r.diff.sizeChanged && r.verdict != Verdict::Missing && r.verdict != Verdict::Added)
            where = "size changed";
        else if (r.diff.x1 >= r.diff.x0)
            where = "x " + std::to_string(r.diff.x0) + "&ndash;" + std::to_string(r.diff.x1) +
                    ", y " + std::to_string(r.diff.y0) + "&ndash;" + std::to_string(r.diff.y1);
        o += "<tr><td><a href=\"#" + HtmlEscape(r.key) + "\">" + HtmlEscape(r.key) + "</a></td>";
        o += "<td class=\"" + std::string(cls) + "\">" + VerdictName(r.verdict) + "</td>";
        o += "<td>" + Percent(r.fraction) + "</td><td>" + Percent(r.noise) + "</td>";
        o += "<td>" + where + "</td></tr>";
    }
    o += "</table>";

    for (const Row& r : rows) {
        if (r.verdict == Verdict::Same) continue;   // the table says enough
        o += "<section id=\"" + HtmlEscape(r.key) + "\"><h2>" + HtmlEscape(r.key) +
             " <span class=\"dim\">" + VerdictName(r.verdict) + ", " + Percent(r.fraction) +
             "</span></h2><div class=\"trio\">";
        auto fig = [&](const char* cap, const std::string& src) {
            o += "<figure><figcaption>";
            o += cap;
            o += "</figcaption>";
            o += src.empty() ? "<p class=\"dim\">none</p>"
                             : "<img loading=\"lazy\" src=\"" + HtmlEscape(src) + "\" alt=\"\">";
            o += "</figure>";
        };
        fig("Before", r.before);
        fig("After", r.after);
        fig("Where it changed", r.heat);
        o += "</div></section>";
    }
    o += "</main></body></html>\n";
    return o;
}

} // namespace raduk::core::shotdiff
