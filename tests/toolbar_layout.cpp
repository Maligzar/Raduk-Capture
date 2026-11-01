// ============================================================
//  The toolbar's two rows (phase 111)
//
//  One row had reached 1,508 px in a strip of about 1,360 on a 1920
//  monitor, and nothing noticed: 8b, 16b and EXIT were painted past
//  the edge and could not be pressed. A button that is off the edge
//  looks exactly like a button that was never there, so the thing
//  worth pinning is that every id has a place, inside the strip, at
//  the narrowest window the app allows.
//
//  What this cannot see is whether it looks right, or whether the
//  real font fits. kToolbarCharW is Space Mono's advance rounded up;
//  a fallback font is someone looking at the screen.
// ============================================================

#include "gui/ToolbarLayout.h"

#include <cstdio>
#include <cwchar>

using namespace raduk::gui;

static int g_fail = 0;

static void Check(bool ok, const char* what) {
    if (ok) printf("  ok    %s\n", what);
    else  { printf("  FAIL  %s\n", what); ++g_fail; }
}

static void TestEveryButtonHasOnePlace() {
    printf("every id appears exactly once\n");
    int seen[kToolbarButtons] = {};
    int total = 0;
    for (const ToolbarRow& row : kToolbarRows) {
        for (int i = 0; i < row.count; i++) {
            const int k = row.items[i].id - ID_TB_FIRST;
            if (k >= 0 && k < kToolbarButtons) seen[k]++;
            total++;
        }
    }
    bool once = true;
    for (int k = 0; k < kToolbarButtons; k++) {
        if (seen[k] != 1) {
            printf("    id %d appears %d times\n", ID_TB_FIRST + k, seen[k]);
            once = false;
        }
    }
    Check(once, "no id missing and none doubled");
    Check(total == kToolbarButtons, "and nothing else in the rows");
}

static void TestFits(int stripW, const char* what) {
    printf("at %d px: %s\n", stripW, what);
    for (int r = 0; r < 2; r++) {
        const ToolbarRow& row = kToolbarRows[r];
        int x[16] = {};
        PlaceToolbarRow(row, stripW, x);
        bool inside = true, ordered = true;
        for (int i = 0; i < row.count; i++) {
            if (x[i] < kToolbarMargin ||
                x[i] + row.items[i].width > stripW - kToolbarMargin)
                inside = false;
            if (i > 0 && x[i] < x[i - 1] + row.items[i - 1].width + kToolbarPad)
                ordered = false;
        }
        Check(inside,  r == 0 ? "top row inside the strip"
                              : "bottom row inside the strip");
        Check(ordered, r == 0 ? "top row does not overlap"
                              : "bottom row does not overlap");
    }
}

static void TestRightEdge() {
    printf("the right group stays on the right\n");
    for (int r = 0; r < 2; r++) {
        const ToolbarRow& row = kToolbarRows[r];
        int x[16] = {};
        PlaceToolbarRow(row, 1360, x);
        const int last = row.count - 1;
        Check(x[last] + row.items[last].width == 1360 - kToolbarMargin,
              "the last button sits on the margin");
        Check(x[0] == kToolbarMargin, "the first sits on the other");
    }

    int x[16] = {};
    const ToolbarRow& row = kToolbarRows[0];
    PlaceToolbarRow(row, 300, x);
    const int lastLeft = row.firstRight - 1;
    Check(x[row.firstRight] ==
              x[lastLeft] + row.items[lastLeft].width + kToolbarSplit,
          "too narrow: the groups keep apart and run off the end");
}

static void TestGroupGaps() {
    printf("a group gap is a real gap\n");
    const ToolbarRow& row = kToolbarRows[0];
    int x[16] = {};
    PlaceToolbarRow(row, 1360, x);
    Check(x[3] == x[2] + row.items[2].width + kToolbarPad + kToolbarGroup,
          "left group");
    Check(x[9] == x[8] + row.items[8].width + kToolbarPad + kToolbarGroup,
          "right group");
    Check(x[1] == x[0] + row.items[0].width + kToolbarPad,
          "and an ordinary neighbour has none");
}

static void TestLabelsFit() {
    printf("the widest label each button ever shows fits it\n");
    bool fits = true;
    for (const ToolbarRow& row : kToolbarRows) {
        for (int i = 0; i < row.count; i++) {
            const int need = (int)wcslen(row.items[i].longest) * kToolbarCharW
                           + kToolbarTextIn;
            if (need > row.items[i].width) {
                printf("    %ls needs %d, has %d\n",
                       row.items[i].longest, need, row.items[i].width);
                fits = false;
            }
        }
    }
    Check(fits, "every button");
    const int circle = (int)wcslen(L"CIRCLE") * kToolbarCharW + kToolbarTextIn;
    Check(circle <= 58, "the reticle's longest shape, which is not in the table");
    const int stop = (int)wcslen(L"DEROT STOP") * kToolbarCharW + kToolbarTextIn;
    Check(stop <= 80, "and DEROT STOP");
}

static void TestHeight() {
    printf("the strip holds both rows\n");
    Check(kToolbarRowY[0] >= 1, "top row clear of the edge");
    Check(kToolbarRowY[1] >= kToolbarRowY[0] + kToolbarBtnH,
          "rows do not overlap");
    Check(kToolbarRowY[1] + kToolbarBtnH < TOOLBAR_HEIGHT - 1,
          "bottom row clear of the strip's border line");
}

static void TestLabels() {
    printf("labels\n");
    wchar_t w[24];
    StripBrackets("[HW] 3/3", w, 24);
    Check(wcscmp(w, L"HW 3/3") == 0, "brackets go, the rest stays");
    StripBrackets("[HW] OFF", w, 4);
    Check(wcscmp(w, L"HW ") == 0, "a short buffer truncates and terminates");
    Check(wcscmp(ReticleButtonLabel(L"RETICLE CIRCLE"), L"CIRCLE") == 0,
          "a lit reticle names its shape");
    Check(wcscmp(ReticleButtonLabel(L"RETICLE"), L"RETICLE") == 0,
          "an unlit one keeps its name");
    Check(wcscmp(ReticleButtonLabel(L"RETICLE "), L"RETICLE ") == 0,
          "nothing after the prefix leaves it alone");
}

int main() {
    TestEveryButtonHasOnePlace();
    TestFits(kToolbarMinWidth, "the narrowest window");
    TestFits(1360, "a 1920 monitor");
    TestFits(3280, "a 3840 monitor");
    TestRightEdge();
    TestGroupGaps();
    TestLabelsFit();
    TestHeight();
    TestLabels();
    printf("\n%s (%d failed)\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
