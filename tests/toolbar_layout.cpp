// ============================================================
//  Where every toolbar control lives (phases 111 and 120)
//
//  Phase 120 moved the toolbar's controls to the rail, over the image,
//  and onto the panel's tabs. A control left out of those tables has no
//  button and no error, so the thing worth pinning is that every id has
//  exactly one home and that each fits it.
//
//  What this cannot see is whether it looks right. kToolbarCharW is
//  Space Mono's advance rounded up.
// ============================================================

#include "gui/ToolbarLayout.h"

#include <cstdio>

using namespace raduk::gui;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_fail;
}

template <int N>
static void Count(const ToolbarItem (&items)[N], int* seen) {
    for (const ToolbarItem& d : items) {
        const int k = d.id - ID_TB_FIRST;
        if (k >= 0 && k < kToolbarButtons) seen[k]++;
    }
}

int main() {
    printf("Toolbar controls: one home each\n");

    int seen[kToolbarButtons] = {};
    Count(kRailItems, seen);
    Count(kViewItems, seen);
    Count(kCameraItems, seen);
    Count(kMountItems, seen);
    bool once = true;
    for (int i = 0; i < kToolbarButtons; i++)
        if (seen[i] != 1) { once = false; printf("  id %d seen %d\n", ID_TB_FIRST + i, seen[i]); }
    Check(once, "every control has exactly one home");

    const int n = ItemCount(kRailItems);
    Check(kRailItems[n - 1].id == ID_TB_EXIT, "exit is last");
    bool icons = true;
    for (const ToolbarItem& d : kRailItems) if (!d.glyph) icons = false;
    Check(icons, "every rail button has an icon");

    bool fits = true;
    for (const ToolbarItem& d : kRailItems)
        if (ItemTextWidth(d.longest) > TOOLBAR_RAIL_W - 4) fits = false;
    Check(fits, "every rail label fits the rail");

    // The shortest client area WM_GETMINMAXINFO allows is well above this.
    const int railH = 600;
    bool stacked = true;
    for (int i = 1; i < n; i++)
        if (RailY(i, n, railH) < RailY(i - 1, n, railH) + kRailBtnH) stacked = false;
    Check(stacked, "no rail buttons overlap at 600 px");
    Check(RailY(n - 1, n, railH) + kRailBtnH <= railH, "and exit stays on screen");
    Check(RailY(n - 1, n, 1080) - RailY(n - 2, n, 1080) > kRailBtnH * 2,
          "on a full screen exit stands apart");

    // The panel's content width.
    const int panelW = 520;
    bool row = true;
    const int cw = (panelW - 2 * (ItemCount(kCameraItems) - 1)) / ItemCount(kCameraItems);
    for (const ToolbarItem& d : kCameraItems) if (ItemTextWidth(d.longest) > cw) row = false;
    const int mw = (panelW - 2 * (ItemCount(kMountItems) - 1)) / ItemCount(kMountItems);
    for (const ToolbarItem& d : kMountItems) if (ItemTextWidth(d.longest) > mw) row = false;
    Check(row, "the panel rows hold their longest labels");

    Check(ViewStripWidth() < 400, "the view strip leaves the image alone");

    wchar_t out[24] = {};
    StripBrackets("[HW] 3/3", out, 24);
    Check(out[0] == L'H' && out[1] == L'W' && out[2] == L' ', "brackets come off");
    Check(ReticleButtonLabel(L"RETICLE CIRCLE")[0] == L'C', "the reticle says what fits");

    printf("\n%s\n", g_fail ? "FAILED" : "all passed");
    return g_fail ? 1 : 0;
}
