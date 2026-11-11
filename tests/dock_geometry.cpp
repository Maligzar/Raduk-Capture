// ============================================================
//  Phase 134.0: the geometry of panels that dock together
//
//  Snapping, groups, moving and resizing a group, filling a monitor,
//  and the dock window's split tree. All of it is arithmetic on
//  rectangles; the window stages (134.1 on) only hand it the rectangles
//  Windows proposes and apply what comes back.
// ============================================================

#include "gui/DockGeometry.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace raduk::gui::dock;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}
static std::string S(const Rect& r) {
    return std::to_string(r.l) + "," + std::to_string(r.t) + "," + std::to_string(r.r) + "," + std::to_string(r.b);
}

int main() {
    printf("\n=== Phase 134.0: docking geometry ===\n");
    const Rect work{ 0, 0, 1920, 1040 };
    const Rect A{ 100, 100, 500, 400 };   // 400 x 300

    printf("\n1. Snapping\n");
    {
        // B dragged to 10 px right of A's right edge, level with it.
        Rect b{ 510, 120, 810, 380 };
        auto r = Snap(b, { A }, work, kSnapPx, false, 0);
        Check(r.rect.l == 500 && r.rect.w() == 300, "within 12 px: flush against the neighbour", S(r.rect));
        Check(r.rect.t == 120 && r.rect.h() == 260, "  the other axis and the size kept");
        Check(r.joints.size() == 1 && r.joints[0].vertical && r.joints[0].pos == 500 &&
                  r.joints[0].from == 120 && r.joints[0].to == 380,
              "  and the joint it made is reported, for the accent line");

        Rect far{ 513, 120, 813, 380 };
        Check(Snap(far, { A }, work, kSnapPx, false, 0).rect == far, "13 px away: no snap");

        Rect below{ 505, 600, 805, 800 };   // to the right of A, but far below it
        Check(Snap(below, { A }, work, kSnapPx, false, 0).rect.l == 505,
              "no overlap on the other axis: no sideways jump");

        Rect near2{ 495, 120, 795, 380 };   // 5 px into A, and 8 px from C's left
        const Rect C{ 803, 0, 1200, 500 };
        auto r2 = Snap(near2, { A, C }, work, kSnapPx, false, 0);
        Check(r2.rect.l == 500, "the nearest of two targets wins", S(r2.rect));

        Rect edge{ 6, 500, 306, 700 };
        Check(Snap(edge, {}, work, kSnapPx, false, 0).rect.l == 0, "the work area's edge is a target");

        Rect aligned{ 105, 405, 400, 600 };   // just under A: 5 px right of its left edge, 5 px below
        auto r3 = Snap(aligned, { A }, work, kSnapPx, false, 0);
        Check(r3.rect.l == 100 && r3.rect.t == 400,
              "dropped just under: closes the gap and lines up the left edges", S(r3.rect));
        Check(r3.joints.size() == 1 && !r3.joints[0].vertical && r3.joints[0].pos == 400,
              "  one joint, the horizontal one; lining up is not a joint");
        Rect under{ 105, 420, 400, 600 };     // 20 px under: too far to be about to touch
        Check(Snap(under, { A }, work, kSnapPx, false, 0).rect == under,
              "20 px under: neither closes nor lines up");

        const Rect grow{ 509, 120, 900, 380 };   // resizing: its LEFT edge is being dragged
        auto r4 = Snap(grow, { A }, work, kSnapPx, true, Left);
        Check(r4.rect.l == 500 && r4.rect.r == 900, "resizing moves only the active edge", S(r4.rect));
        auto r5 = Snap(grow, { A }, work, kSnapPx, true, Right);
        Check(r5.rect == grow, "  and not an inactive one");
    }

    printf("\n1b. WM_SIZING's edge as a mask\n");
    {
        const int want[9] = { 0, Left, Right, Top, Top | Left, Top | Right, Bottom, Bottom | Left, Bottom | Right };
        bool all = true;
        for (int w = 0; w <= 8; ++w) all = all && EdgesFromSizing(w) == want[w];
        Check(all, "all eight WMSZ values, and 0 for anything else");
        Check(EdgesFromSizing(9) == 0 && EdgesFromSizing(-1) == 0, "  out of range is no edge");
    }

    printf("\n2. Groups\n");
    {
        const Rect B{ 500, 100, 800, 400 };    // right of A
        const Rect C{ 500, 400, 800, 600 };    // under B
        const Rect D{ 1200, 100, 1500, 400 };  // alone
        auto g = Groups({ A, B, C, D });
        Check(g[0] == g[1] && g[1] == g[2], "a chain A-B-C is one group");
        Check(g[3] != g[0] && g[3] == 3, "a panel alone is its own group");

        const Rect corner{ 500, 370, 800, 600 };   // meets A's side for only 30 px
        Check(!Touching(A, corner), "corner-only contact (30 px) does not join");
        const Rect gap{ 502, 100, 800, 400 };
        Check(!Touching(A, gap), "a 2 px gap does not join");
        const Rect near1{ 501, 100, 800, 400 };
        Check(Touching(A, near1), "1 px off is still flush (rounding)");

        auto g2 = Groups({ A, B, D, { 1500, 100, 1800, 400 } });
        Check(g2[0] == g2[1] && g2[2] == g2[3] && g2[0] != g2[2], "two separate groups");
    }

    printf("\n3. Moving a group\n");
    {
        const std::vector<Rect> rs{ A, { 500, 100, 800, 400 }, { 1200, 100, 1500, 400 } };
        const auto g = Groups(rs);
        auto m = MoveGroup(rs, g, 1, { 550, 150, 850, 450 });
        Check(m[1] == Rect{ 550, 150, 850, 450 }, "the leader goes where it was put");
        Check(m[0] == Rect{ 150, 150, 550, 450 }, "  its group follows by the same delta", S(m[0]));
        Check(m[2] == rs[2], "  a panel outside the group stays");
    }

    printf("\n3b. Moving a group that snaps as one block (134.2)\n");
    {
        // A | B joined; C outside, to the right of B with a 200 px gap.
        const std::vector<Rect> rs{ A, { 500, 100, 800, 400 }, { 1000, 100, 1300, 400 } };
        const auto g = Groups(rs);
        // Drag A right by 192: B would land 8 px short of C.
        auto m = MoveGroupSnapped(rs, g, 0, { 292, 100, 692, 400 }, work, kSnapPx);
        Check(m[1].r == 1000, "the group's outer edge snaps to a panel outside it", S(m[1]));
        Check(m[0].r == m[1].l && m[0].w() == 400 && m[1].w() == 300,
              "  every member keeps its offset and size", S(m[0]));
        Check(m[2] == rs[2], "  the outside panel does not move");

        // A tiny drag inside the group's own joint distance: members are not
        // targets, so the group moves by exactly what was asked.
        auto t = MoveGroupSnapped(rs, g, 0, { 103, 104, 503, 404 }, work, kSnapPx);
        Check(t[0] == Rect{ 103, 104, 503, 404 } && t[1] == Rect{ 503, 104, 803, 404 },
              "it never snaps to its own members");

        // Far from everything: the same as MoveGroup.
        const std::vector<Rect> lone{ { 600, 600, 900, 800 }, { 900, 600, 1100, 800 } };
        const auto gl = Groups(lone);
        Check(MoveGroupSnapped(lone, gl, 0, { 650, 620, 950, 820 }, work, kSnapPx) ==
                  MoveGroup(lone, gl, 0, { 650, 620, 950, 820 }),
              "nothing near: exactly MoveGroup");
    }

    printf("\n4. A shared edge resized like a splitter\n");
    {
        // A | B side by side, C under both, sharing A's and B's bottoms.
        const std::vector<Rect> rs{ { 0, 0, 400, 300 }, { 400, 0, 800, 300 }, { 0, 300, 800, 500 } };
        const auto g = Groups(rs);
        auto m = ResizeEdge(rs, g, 0, Right, 450);
        Check(m[0].r == 450 && m[1].l == 450, "the joint moves and stays closed", S(m[0]) + " | " + S(m[1]));
        Check(m[1].r == 800 && m[2] == rs[2], "  nothing else moves");

        auto clamp = ResizeEdge(rs, g, 0, Right, 700);
        Check(clamp[1].l == 800 - kMinW && clamp[0].r == clamp[1].l,
              "clamped so the neighbour keeps its minimum width", S(clamp[1]));

        auto h = ResizeEdge(rs, g, 2, Top, 260);
        Check(h[2].t == 260 && h[0].b == 260 && h[1].b == 260,
              "a horizontal joint under two panels moves all three");

        auto outer = ResizeEdge(rs, g, 0, Left, -40);
        Check(outer[0].l == -40 && outer[2].l == -40,
              "an outer edge: panels lined up with it follow, so the outline stays square");
    }

    printf("\n5. A group filling a monitor\n");
    {
        // A 2x2 grid of unequal panels, placed away from the origin.
        const std::vector<Rect> rs{ { 37, 51, 337, 251 }, { 337, 51, 737, 251 },
                                    { 37, 251, 537, 451 }, { 537, 251, 737, 451 } };
        for (const Rect mon : { Rect{ 0, 0, 1920, 1080 }, Rect{ 1920, -7, 3360 + 1, 893 } }) {
            const auto f = FillMonitor(rs, mon);
            long long area = 0;
            for (const Rect& r : f) area += (long long)r.w() * r.h();
            const bool tiles = area == (long long)mon.w() * mon.h();
            const bool joints = f[0].r == f[1].l && f[2].r == f[3].l && f[0].b == f[2].t && f[1].b == f[3].t;
            const bool outer = f[0].l == mon.l && f[0].t == mon.t && f[3].r == mon.r && f[3].b == mon.b;
            Check(tiles && joints && outer,
                  "fills " + std::to_string(mon.w()) + "x" + std::to_string(mon.h()) +
                      " exactly: no gap, no overlap, every joint shared",
                  S(f[0]) + " " + S(f[3]));
        }
        Check(FillMonitor({}, { 0, 0, 10, 10 }).empty(), "an empty group is left alone");
    }

    printf("\n6. The dock window's split tree\n");
    {
        const Node t = Split('H', 0.62, Split('V', 0.5, Leaf("histogram"), Leaf("sharpness")), Leaf("status"));
        const Rect area{ 0, 0, 1920, 1080 };
        const int sp = 6;
        const auto L = Layout(t, area, sp);
        long long sum = 0;
        for (const auto& [role, r] : L) sum += (long long)r.w() * r.h();
        const long long splitters = (long long)sp * area.h() + (long long)sp * L[0].second.w();
        Check(L.size() == 3 && L[0].first == "histogram" && L[2].first == "status", "every panel, depth first");
        Check(sum + splitters == (long long)area.w() * area.h(),
              "panels plus splitters tile the area exactly");
        Check(L[0].second.r + sp == L[2].second.l && L[0].second.b + sp == L[1].second.t,
              "  with the splitter between neighbours");

        const std::string text = ToText(t);
        Check(text == "H(0.62,V(0.5,histogram,sharpness),status)", "written in workspace.ini's form", text);
        const auto back = ParseText(text);
        Check(back && ToText(*back) == text, "and read back to the same tree");
        Check(ParseText("V(0.95,a,b)") && ParseText("V(0.95,a,b)")->ratio == kMaxRatio,
              "a ratio past the limits is clamped");
        bool rejected = true;
        for (const char* bad : { "", "H(", "H(0.5,a)", "H(0.5,a,b", "H(x,a,b)", "H(0.5,a,b)x",
                                 "a b", "(0.5,a,b)", "H(0.5,,b)" })
            rejected = rejected && !ParseText(bad);
        Check(rejected, "damaged text is refused, not half-read");
        std::string deep;
        for (int i = 0; i < 100; ++i) deep += "H(0.5,a,";
        Check(!ParseText(deep), "a pathologically deep line is refused");
    }

    printf("\n6b. Editing the dock's tree (134.4a)\n");
    {
        const Rect wide{ 0, 0, 1600, 900 };
        Node t = Append(std::nullopt, "histogram", wide);
        Check(t.IsLeaf() && t.role == "histogram", "the first panel is the whole tree");
        t = Append(t, "status", wide);
        Check(t.split == 'H' && t.ratio == 0.5, "the second sits beside it in a wide dock", ToText(t));
        t = Append(t, "sharpness", { 0, 0, 600, 900 });
        Check(t.split == 'V', "  and below in a tall one", ToText(t));
        std::vector<std::string> roles;
        Roles(t, roles);
        Check(roles.size() == 3 && roles[0] == "histogram" && roles[2] == "sharpness",
              "every role is still there, in order");

        auto L = Layout(t, wide, 6);
        long long sum = 0;
        for (const auto& [r, rc] : L) sum += (long long)rc.w() * rc.h();
        const auto bars = Splitters(t, wide, 6);
        for (const auto& b : bars) sum += (long long)b.bar.w() * b.bar.h();
        Check(bars.size() == 2 && sum == (long long)wide.w() * wide.h(),
              "panels plus splitter bars tile the dock exactly");
        Check(L[0].second.r == bars[1].bar.l && bars[1].bar.r == L[1].second.l,
              "  each bar sits in its split's gap");

        auto r = Remove(t, "status");
        Check(r && ToText(*r) == "V(0.5,histogram,sharpness)", "removing a middle leaf collapses its parent",
              r ? ToText(*r) : "none");
        Check(Remove(Leaf("x"), "x") == std::nullopt, "removing the last leaf leaves nothing");
        auto same = Remove(t, "nosuch");
        Check(same && ToText(*same) == ToText(t), "an unknown role changes nothing");

        // The outer split is 'V' (stacked): drag its bar to y = 600 and the
        // layout follows.
        Node d = t;
        const auto outer = Splitters(d, wide, 6)[0];
        Check(outer.dir == 'V' && SetRatio(d, outer.path, RatioAt(outer, 600, 6)),
              "a drag sets the split's ratio");
        const int barT = Splitters(d, wide, 6)[0].bar.t;
        Check(std::abs(barT - 600) <= 1 && Layout(d, wide, 6).back().second.t == barT + 6,
              "  and the bar lands where the mouse put it, the panel below it", std::to_string(barT));
        Check(RatioAt(outer, -500, 6) == kMinRatio && RatioAt(outer, 99999, 6) == kMaxRatio,
              "a drag past the ends is clamped");
        Check(!SetRatio(d, { 0, 0, 0 }, 0.5), "a path into a leaf is refused");
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
