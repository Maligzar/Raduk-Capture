#pragma once
// ============================================================
// DockGeometry.h -- phase 134.0: the arithmetic of docking panels
//
// Phase 134 lets the operator panels snap together like Lego, move as a
// group, resize along shared edges, fill a monitor as one instrument,
// and sit in a dock window laid out by a split tree. Every one of those
// is geometry, and geometry is what a bench test can reach where a
// window cannot. So it all lives here, pure, and the Win32 stages
// (134.1 to 134.5) only translate messages into these calls.
//
// No Windows headers. Rect is a RECT in all but name (left, top,
// right, bottom; right and bottom exclusive). (std::min) and (std::max)
// are parenthesised, because windows.h's macros would expand them when
// a later stage includes this from main.cpp.
//
// Groups are not stored anywhere. Two panels belong together when they
// touch: an edge flush (within a pixel) against the other's opposite
// edge, overlapping along it by at least 40 px. A group is everything
// connected that way, worked out from the rectangles each time, so
// there is no membership list to drift from what is on screen.
//
// Gated by tests/dock_geometry.cpp.
// ============================================================

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace raduk::gui::dock {

struct Rect {
    int l = 0, t = 0, r = 0, b = 0;
    int w() const { return r - l; }
    int h() const { return b - t; }
    bool operator==(const Rect& o) const { return l == o.l && t == o.t && r == o.r && b == o.b; }
};

// Edges, as a mask (several at once for a corner resize).
enum Edge { Left = 1, Top = 2, Right = 4, Bottom = 8 };

// The owner's decisions, 2026-11-11.
inline constexpr int kSnapPx      = 12;    // at 96 DPI; scale for others
inline constexpr int kMinOverlap  = 40;    // contact shorter than this is a corner, not a joint
inline constexpr int kMinW        = 240;   // WM_GETMINMAXINFO in 134.1
inline constexpr int kMinH        = 120;

inline int Overlap(int a0, int a1, int b0, int b1) {
    return (std::min)(a1, b1) - (std::max)(a0, b0);
}

// ── Snapping ─────────────────────────────────────────────────

// A joint made by a snap: the line where two panels now meet, for the
// accent line 134.1 draws while the mouse is down. `vertical` means a
// line x = pos running from `from` to `to` in y.
struct Joint {
    bool vertical = true;
    int pos = 0, from = 0, to = 0;
};

struct SnapResult {
    Rect rect;
    std::vector<Joint> joints;
};

namespace detail {
// The nearest target within `th` of `edge`, or nullopt.
inline std::optional<int> Nearest(int edge, const std::vector<int>& targets, int th) {
    std::optional<int> best;
    for (int t : targets) {
        const int d = std::abs(t - edge);
        if (d <= th && (!best || d < std::abs(*best - edge))) best = t;
    }
    return best;
}
} // namespace detail

// `moving` is where the window manager proposes to put the panel (the
// WM_MOVING or WM_SIZING rectangle). Each axis snaps on its own: an edge
// within `th` of another panel's edge, on either side, jumps to it, but
// only where the two overlap on the other axis, or come within `th` of
// it -- a panel far below
// another does not jump sideways to line up with it. The work area's
// edges are targets whatever the overlap. Moving keeps the size, and
// the nearest of the panel's two edges decides the shift. Resizing
// moves only the edges in `activeEdges`.
inline SnapResult Snap(Rect moving, const std::vector<Rect>& others, const Rect& workArea,
                       int th, bool resizing, int activeEdges) {
    std::vector<int> xs = { workArea.l, workArea.r }, ys = { workArea.t, workArea.b };
    for (const Rect& o : others) {
        // Overlapping, or about to: within the snap distance on the other
        // axis counts, so a panel dropped just under another lines up
        // with it in the same move that closes the gap.
        if (Overlap(moving.t, moving.b, o.t, o.b) > -th) { xs.push_back(o.l); xs.push_back(o.r); }
        if (Overlap(moving.l, moving.r, o.l, o.r) > -th) { ys.push_back(o.t); ys.push_back(o.b); }
    }
    Rect m = moving;
    auto axis = [&](int& lo, int& hi, const std::vector<int>& tg, int loBit, int hiBit) {
        const auto a = detail::Nearest(lo, tg, th), b = detail::Nearest(hi, tg, th);
        if (!resizing) {
            int shift = 0;
            if (a && (!b || std::abs(*a - lo) <= std::abs(*b - hi))) shift = *a - lo;
            else if (b) shift = *b - hi;
            lo += shift; hi += shift;
        } else {
            if (a && (activeEdges & loBit)) lo = *a;
            if (b && (activeEdges & hiBit)) hi = *b;
        }
    };
    axis(m.l, m.r, xs, Left, Right);
    axis(m.t, m.b, ys, Top, Bottom);

    SnapResult out{ m, {} };
    for (const Rect& o : others) {
        const int oy = Overlap(m.t, m.b, o.t, o.b), ox = Overlap(m.l, m.r, o.l, o.r);
        if (oy > 0 && (m.r == o.l || m.l == o.r))
            out.joints.push_back({ true, m.r == o.l ? m.r : m.l, (std::max)(m.t, o.t), (std::min)(m.b, o.b) });
        if (ox > 0 && (m.b == o.t || m.t == o.b))
            out.joints.push_back({ false, m.b == o.t ? m.b : m.t, (std::max)(m.l, o.l), (std::min)(m.r, o.r) });
    }
    return out;
}

// WM_SIZING's wParam (WMSZ_LEFT 1 ... WMSZ_BOTTOMRIGHT 8) as an Edge
// mask, so a corner drag snaps both its edges. The values are Windows'
// and fixed; written out here so this header needs no windows.h.
inline int EdgesFromSizing(int wmsz) {
    switch (wmsz) {
    case 1: return Left;            case 2: return Right;
    case 3: return Top;             case 4: return Top | Left;
    case 5: return Top | Right;     case 6: return Bottom;
    case 7: return Bottom | Left;   case 8: return Bottom | Right;
    default: return 0;
    }
}

// ── Adjacency and groups ─────────────────────────────────────

inline bool Touching(const Rect& a, const Rect& b, int minOverlap = kMinOverlap, int tol = 1) {
    const bool side = std::abs(a.r - b.l) <= tol || std::abs(b.r - a.l) <= tol;
    const bool stack = std::abs(a.b - b.t) <= tol || std::abs(b.b - a.t) <= tol;
    return (side && Overlap(a.t, a.b, b.t, b.b) >= minOverlap) ||
           (stack && Overlap(a.l, a.r, b.l, b.r) >= minOverlap);
}

// A group id per panel: the smallest index in its group, so ids are
// stable and a panel alone is its own group.
inline std::vector<int> Groups(const std::vector<Rect>& rects) {
    std::vector<int> p(rects.size());
    std::iota(p.begin(), p.end(), 0);
    auto find = [&](int i) { while (p[i] != i) i = p[i] = p[p[i]]; return i; };
    for (size_t i = 0; i < rects.size(); ++i)
        for (size_t j = i + 1; j < rects.size(); ++j)
            if (Touching(rects[i], rects[j])) {
                const int a = find((int)i), b = find((int)j);
                p[(std::max)(a, b)] = (std::min)(a, b);
            }
    std::vector<int> id(rects.size());
    for (size_t i = 0; i < rects.size(); ++i) id[i] = find((int)i);
    return id;
}

// ── Moving a group ───────────────────────────────────────────

// The leader goes where it was proposed; the rest of its group follows by
// the same delta. Panels outside the group do not move.
inline std::vector<Rect> MoveGroup(std::vector<Rect> rects, const std::vector<int>& groupIds,
                                   int leader, const Rect& proposed) {
    const int dx = proposed.l - rects[leader].l, dy = proposed.t - rects[leader].t;
    for (size_t i = 0; i < rects.size(); ++i)
        if (groupIds[i] == groupIds[leader]) {
            rects[i].l += dx; rects[i].r += dx; rects[i].t += dy; rects[i].b += dy;
        }
    return rects;
}

// Phase 134.2: the leader dragged to `proposed`, its group following,
// and the group snapping as ONE block -- its bounding box against the
// panels outside it and the work area, never against its own members,
// which would otherwise pin it where it already is.
inline std::vector<Rect> MoveGroupSnapped(const std::vector<Rect>& rects,
                                          const std::vector<int>& groupIds, int leader,
                                          const Rect& proposed, const Rect& work, int th) {
    const int g = groupIds[leader];
    Rect bb{};
    bool first = true;
    std::vector<Rect> outside;
    for (size_t i = 0; i < rects.size(); ++i) {
        if (groupIds[i] != g) { outside.push_back(rects[i]); continue; }
        const Rect& r = rects[i];
        if (first) { bb = r; first = false; continue; }
        bb.l = (std::min)(bb.l, r.l); bb.t = (std::min)(bb.t, r.t);
        bb.r = (std::max)(bb.r, r.r); bb.b = (std::max)(bb.b, r.b);
    }
    const int dx = proposed.l - rects[leader].l, dy = proposed.t - rects[leader].t;
    const Rect moved{ bb.l + dx, bb.t + dy, bb.r + dx, bb.b + dy };
    const Rect snapped = Snap(moved, outside, work, th, false, 0).rect;
    const Rect lead = rects[leader];
    const int fx = dx + (snapped.l - moved.l), fy = dy + (snapped.t - moved.t);
    return MoveGroup(rects, groupIds, leader, { lead.l + fx, lead.t + fy, lead.r + fx, lead.b + fy });
}

// ── A shared edge, resized like a splitter ───────────────────

// Drags edge `e` (one of Left, Top, Right, Bottom) of panel `index` to
// `newPos`. Every member of its group with an edge on the same line moves
// with it: those with the same side (the dragged panel, and any lined up
// with it, so the outline stays square) and those with the opposite side
// (the neighbours across the joint, so it stays closed). The position is
// clamped so that no member it touches goes below the minimum size.
inline std::vector<Rect> ResizeEdge(std::vector<Rect> rects, const std::vector<int>& groupIds,
                                    int index, Edge e, int newPos,
                                    int minW = kMinW, int minH = kMinH, int tol = 1) {
    const bool vert = (e == Left || e == Right);
    auto get = [](const Rect& r, Edge s) { return s == Left ? r.l : s == Right ? r.r : s == Top ? r.t : r.b; };
    auto set = [](Rect& r, Edge s, int v) { (s == Left ? r.l : s == Right ? r.r : s == Top ? r.t : r.b) = v; };
    const Edge opp = e == Left ? Right : e == Right ? Left : e == Top ? Bottom : Top;
    const int old = get(rects[index], e);

    std::vector<std::pair<int, Edge>> moving;   // (panel, which of its edges)
    for (size_t i = 0; i < rects.size(); ++i) {
        if (groupIds[i] != groupIds[index]) continue;
        if (std::abs(get(rects[i], e) - old) <= tol)   moving.push_back({ (int)i, e });
        if (std::abs(get(rects[i], opp) - old) <= tol) moving.push_back({ (int)i, opp });
    }
    int lo = -1000000000, hi = 1000000000;
    for (const auto& [i, s] : moving) {
        const Rect& r = rects[i];
        const int min = vert ? minW : minH;
        if (s == Right)  lo = (std::max)(lo, r.l + min);
        if (s == Bottom) lo = (std::max)(lo, r.t + min);
        if (s == Left)   hi = (std::min)(hi, r.r - min);
        if (s == Top)    hi = (std::min)(hi, r.b - min);
    }
    const int pos = lo > hi ? old : (std::max)(lo, (std::min)(hi, newPos));
    for (const auto& [i, s] : moving) set(rects[i], s, pos);
    return rects;
}

// ── A group filling a monitor ────────────────────────────────

// Maps the group's bounding box onto `monitor`, in proportion. Every edge
// goes through the same function of its own coordinate, so edges that
// met still meet after rounding -- no hairline gaps, no overlaps -- and
// the outer edges land exactly on the monitor's.
inline std::vector<Rect> FillMonitor(std::vector<Rect> rects, const Rect& monitor) {
    if (rects.empty()) return rects;
    Rect bb = rects[0];
    for (const Rect& r : rects) {
        bb.l = (std::min)(bb.l, r.l); bb.t = (std::min)(bb.t, r.t);
        bb.r = (std::max)(bb.r, r.r); bb.b = (std::max)(bb.b, r.b);
    }
    if (bb.w() <= 0 || bb.h() <= 0) return rects;
    auto map = [](int v, int s0, int sw, int d0, int dw) {
        const long long n = (long long)(v - s0) * dw;
        return d0 + (int)((2 * n + sw) / (2LL * sw));   // rounded, for v >= s0
    };
    for (Rect& r : rects) {
        r.l = map(r.l, bb.l, bb.w(), monitor.l, monitor.w());
        r.r = map(r.r, bb.l, bb.w(), monitor.l, monitor.w());
        r.t = map(r.t, bb.t, bb.h(), monitor.t, monitor.h());
        r.b = map(r.b, bb.t, bb.h(), monitor.t, monitor.h());
    }
    return rects;
}

// ── The dock window's split tree ─────────────────────────────

// A leaf names a panel by its workspace role ("histogram"); a split
// divides its area between two children, 'H' side by side and 'V' one
// above the other, the first child taking `ratio` of the space.
struct Node {
    std::string role;
    char split = 0;          // 0 for a leaf, 'H' or 'V'
    double ratio = 0.5;
    std::vector<Node> kids;  // two for a split, none for a leaf
    bool IsLeaf() const { return split == 0; }
};

inline constexpr double kMinRatio = 0.1, kMaxRatio = 0.9;

inline Node Leaf(const std::string& role) { Node n; n.role = role; return n; }
inline Node Split(char dir, double ratio, Node a, Node b) {
    Node n; n.split = dir;
    n.ratio = (std::max)(kMinRatio, (std::min)(kMaxRatio, ratio));
    n.kids.push_back(std::move(a)); n.kids.push_back(std::move(b));
    return n;
}

// The rectangle for every panel, depth first. A split leaves `splitter`
// pixels between its children, so the pieces and the splitters tile the
// area exactly.
inline void LayoutInto(const Node& n, const Rect& a, int splitter,
                       std::vector<std::pair<std::string, Rect>>& out) {
    if (n.IsLeaf()) { out.push_back({ n.role, a }); return; }
    if (n.kids.size() != 2) return;
    if (n.split == 'H') {
        const int avail = (std::max)(0, a.w() - splitter);
        const int first = (int)std::lround(avail * n.ratio);
        LayoutInto(n.kids[0], { a.l, a.t, a.l + first, a.b }, splitter, out);
        LayoutInto(n.kids[1], { a.l + first + splitter, a.t, a.r, a.b }, splitter, out);
    } else {
        const int avail = (std::max)(0, a.h() - splitter);
        const int first = (int)std::lround(avail * n.ratio);
        LayoutInto(n.kids[0], { a.l, a.t, a.r, a.t + first }, splitter, out);
        LayoutInto(n.kids[1], { a.l, a.t + first + splitter, a.r, a.b }, splitter, out);
    }
}
inline std::vector<std::pair<std::string, Rect>> Layout(const Node& n, const Rect& area, int splitter) {
    std::vector<std::pair<std::string, Rect>> out;
    LayoutInto(n, area, splitter, out);
    return out;
}

// workspace.ini's form: H(0.62,V(0.5,histogram,sharpness),status).
inline std::string ToText(const Node& n) {
    if (n.IsLeaf()) return n.role;
    if (n.kids.size() != 2) return std::string();
    char r[32];
    snprintf(r, sizeof(r), "%.3f", n.ratio);
    std::string s(r);
    while (s.size() > 1 && s.back() == '0') s.pop_back();   // 0.500 -> 0.5
    if (s.back() == '.') s.pop_back();
    return std::string(1, n.split) + "(" + s + "," + ToText(n.kids[0]) + "," + ToText(n.kids[1]) + ")";
}

namespace detail {
inline bool RoleChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}
inline std::optional<Node> ParseAt(const std::string& s, size_t& i, int depth) {
    if (depth > 32 || i >= s.size()) return std::nullopt;
    if ((s[i] == 'H' || s[i] == 'V') && i + 1 < s.size() && s[i + 1] == '(') {
        const char dir = s[i];
        i += 2;
        const char* start = s.c_str() + i;
        char* end = nullptr;
        const double ratio = strtod(start, &end);
        if (end == start || !std::isfinite(ratio)) return std::nullopt;
        i += (size_t)(end - start);
        if (i >= s.size() || s[i] != ',') return std::nullopt;
        ++i;
        auto a = ParseAt(s, i, depth + 1);
        if (!a || i >= s.size() || s[i] != ',') return std::nullopt;
        ++i;
        auto b = ParseAt(s, i, depth + 1);
        if (!b || i >= s.size() || s[i] != ')') return std::nullopt;
        ++i;
        return Split(dir, ratio, std::move(*a), std::move(*b));
    }
    const size_t from = i;
    while (i < s.size() && RoleChar(s[i])) ++i;
    if (i == from) return std::nullopt;
    return Leaf(s.substr(from, i - from));
}
} // namespace detail

// The whole string must be one tree; anything else is nullopt, so a
// damaged workspace.ini line costs a dock its layout, not the app.
inline std::optional<Node> ParseText(const std::string& s) {
    size_t i = 0;
    auto n = detail::ParseAt(s, i, 0);
    if (!n || i != s.size()) return std::nullopt;
    return n;
}

} // namespace raduk::gui::dock
