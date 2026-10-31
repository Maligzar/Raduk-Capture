// ============================================================
//  The palette still has a hierarchy in the dark (phase 96.1)
//
//  Night vision collapses every painted colour to luminance and
//  pushes it into red, because short wavelengths destroy dark
//  adaptation. Hue carries no information after that, so the only
//  thing left to design is how bright each token is -- and until
//  this phase nothing designed it. The luminance was DERIVED from
//  the day colour, which is a hue-to-brightness mapping, and a
//  hue-to-brightness mapping means nothing.
//
//  Measured before the fix:
//
//    cyan 182, amber 186      4 units apart out of 255
//    green 209, TEXT 226      17 apart
//    TEXT_DIM 169             brighter than...
//    ACCENT4 danger 103       ...the colour for capture and danger
//
//  That last pair is the one that matters. On a rig that spends
//  its working life in night mode, the token that must shout was
//  sixty per cent dimmer than the token for labels you skim past.
//
//  So this file does not test that the colours are pretty. It
//  tests the two properties a palette needs in order to carry
//  meaning without hue:
//
//    ORDER. Danger above primary text, above the device accents,
//    above the skimmed labels, above the surfaces. Stated as the
//    semantic ladder, so a later edit that breaks it fails here
//    rather than at 2am.
//
//    SEPARATION. Adjacent steps far enough apart to be told
//    apart. Four units is not a hierarchy.
//
//  It also pins what must NOT change: day colours. The fix is a
//  night-only lookup, so every token in daylight is byte for byte
//  what it was.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "gui/Theme.h"

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

using namespace raduk::gui;

static int g_fail = 0;

static void Check(bool ok, const char* what) {
    if (ok) printf("  ok    %s\n", what);
    else  { printf("  FAIL  %s\n", what); ++g_fail; }
}

static void Near(double got, double want, double tol, const char* what) {
    if (std::fabs(got - want) <= tol) printf("  ok    %s (%.3f)\n", what, got);
    else { printf("  FAIL  %s: got %.3f, wanted %.3f +/- %.3f\n",
                  what, got, want, tol); ++g_fail; }
}

// The red channel of a token after the night transform, which is
// the only channel carrying anything.
static int NightOf(uint32_t argb) {
    const int r = (argb >> 16) & 0xFF;
    const int g = (argb >> 8)  & 0xFF;
    const int b = (argb)       & 0xFF;
    return GetRValue(Theme::NV(RGB(r, g, b)));
}

int main() {
    printf("Night vision: does the palette still have a hierarchy\n");

    struct Tok { const char* name; uint32_t argb; };
    // In the order they MUST appear, brightest first. This list is
    // the design; the numbers in Theme.h serve it.
    const Tok ladder[] = {
        { "ACCENT4 danger/capture", Theme::ACCENT4  },
        { "TEXT primary",           Theme::TEXT     },
        { "ACCENT3 mount/live",     Theme::ACCENT3  },
        { "ACCENT camera/active",   Theme::ACCENT   },
        { "ACCENT2 EAF/secondary",  Theme::ACCENT2  },
        { "TEXT_DIM skimmed",       Theme::TEXT_DIM },
        { "BORDER separators",      Theme::BORDER   },
        { "PANEL raised",           Theme::PANEL    },
        { "DEEP panel bg",          Theme::DEEP     },
        { "BG_VOID deepest",        Theme::BG_VOID  },
    };
    const int N = (int)(sizeof(ladder)/sizeof(ladder[0]));

    // ---- day is untouched --------------------------------------
    printf("\n1. Daylight is byte for byte what it was\n");
    {
        Theme::SetNightVision(false);
        Check(!Theme::NightVisionOn(), "night vision is off");
        bool allSame = true;
        for (const auto& t : ladder) {
            const int r = (t.argb >> 16) & 0xFF;
            const int g = (t.argb >> 8)  & 0xFF;
            const int b = (t.argb)       & 0xFF;
            const COLORREF in  = RGB(r, g, b);
            if (Theme::NV(in) != in) { allSame = false;
                printf("  FAIL  %s changed in daylight\n", t.name); ++g_fail; }
        }
        Check(allSame, "every token passes through unchanged with the mode off");

        // The D2D path too, which is a separate function.
        const D2D1_COLOR_F c = D2D1::ColorF(0.5f, 0.25f, 0.75f, 1.0f);
        const D2D1_COLOR_F out = Theme::NVf(c);
        Check(out.r == c.r && out.g == c.g && out.b == c.b,
              "and so does the Direct2D path");
    }

    // ---- the inversion that started this -----------------------
    printf("\n2. The inversion is gone\n");
    {
        Theme::SetNightVision(true);
        Check(Theme::NightVisionOn(), "night vision is on");

        const int danger = NightOf(Theme::ACCENT4);
        const int dim    = NightOf(Theme::TEXT_DIM);
        printf("  --    danger %d, dim %d (was 103 and 169)\n", danger, dim);
        Check(danger > dim,
              "the colour for capture and danger outshines the one for "
              "labels you skim past");
        Check(danger - dim >= 100,
              "and by a margin nobody could mistake");
    }

    // ---- the whole ladder --------------------------------------
    printf("\n3. The semantic order holds, top to bottom\n");
    {
        Theme::SetNightVision(true);
        int prev = 256;
        bool ordered = true;
        for (int i = 0; i < N; ++i) {
            const int v = NightOf(ladder[i].argb);
            printf("  --    %-24s %3d\n", ladder[i].name, v);
            if (v >= prev) {
                printf("  FAIL  %s is not below the step above it\n",
                       ladder[i].name);
                ordered = false; ++g_fail;
            }
            prev = v;
        }
        Check(ordered, "every step is strictly below the one above");

        // Danger must be the brightest thing the UI can paint, not
        // merely high up: that is the whole point of the ladder.
        int brightest = -1;
        for (int i = 0; i < N; ++i) {
            const int v = NightOf(ladder[i].argb);
            if (v > brightest) brightest = v;
        }
        Check(NightOf(Theme::ACCENT4) == brightest,
              "and danger is the brightest token in the palette");
    }

    // ---- separation --------------------------------------------
    printf("\n4. Signals are far enough apart; surfaces need not be\n");
    {
        Theme::SetNightVision(true);
        // The two bands want opposite things. SIGNALS exist to be told
        // apart and need real separation. SURFACES exist to recede,
        // and holding them 24 apart would turn the window into a
        // staircase of greys -- they need order and nothing more.
        //
        // The first version of this test applied the floor to all ten
        // steps and failed on the 6-unit gap between the two darkest
        // backgrounds, which is correct design and a wrong rule.
        const int firstSurface = Theme::detail::kFirstSurfaceStep;

        int worst = 999; const char* worstPair = "";
        for (int i = 1; i < firstSurface; ++i) {
            const int gap = NightOf(ladder[i-1].argb) - NightOf(ladder[i].argb);
            if (gap < worst) { worst = gap; worstPair = ladder[i].name; }
        }
        printf("  --    smallest gap among signals: %d, above %s\n",
               worst, worstPair);
        Check(worst >= Theme::detail::kMinNightSeparation,
              "every signal is clear of its neighbour by the stated minimum");

        // Surfaces: ordered, and allowed to be close.
        bool surfacesOrdered = true;
        for (int i = firstSurface; i < N; ++i)
            if (NightOf(ladder[i].argb) >= NightOf(ladder[i-1].argb))
                surfacesOrdered = false;
        Check(surfacesOrdered, "surfaces are ordered, which is all they owe");
        Check(NightOf(ladder[firstSurface].argb)
                  < NightOf(ladder[firstSurface-1].argb)
                    - Theme::detail::kMinNightSeparation,
              "and the brightest surface is well below the quietest signal");

        // The specific collision this phase existed to fix.
        const int cyan  = NightOf(Theme::ACCENT);
        const int amber = NightOf(Theme::ACCENT2);
        printf("  --    cyan %d, amber %d (were 182 and 186)\n", cyan, amber);
        Check(std::abs(cyan - amber) >= Theme::detail::kMinNightSeparation,
              "cyan and amber are no longer four units apart");
    }

    // ---- surfaces stay dark ------------------------------------
    printf("\n5. Surfaces stay surfaces\n");
    {
        Theme::SetNightVision(true);
        Check(NightOf(Theme::BG_VOID) < 20,
              "the deepest surface is still nearly black");
        Check(NightOf(Theme::PANEL) < NightOf(Theme::TEXT_DIM),
              "a raised panel is darker than the quietest text on it");
        Check(NightOf(Theme::BG_VOID) < NightOf(Theme::PANEL),
              "and the preview background is darker than a panel");
    }

    // ---- colours outside the ladder ----------------------------
    printf("\n6. A colour with no semantic still works\n");
    {
        Theme::SetNightVision(true);
        // Decorative colours are not in the table and fall through to
        // the derived luminance, which is the right answer: there is
        // no meaning to honour.
        const COLORREF odd = RGB(123, 45, 200);
        const int want = (int)(0.2126f*123 + 0.7152f*45 + 0.0722f*200);
        Check(GetRValue(Theme::NV(odd)) == want,
              "an unlisted colour still derives from its luminance");
        Check(GetBValue(Theme::NV(odd)) == 0,
              "with no blue left in it at all");
        Check(GetGValue(Theme::NV(odd)) == want / 10,
              "and a tenth in green, so near-blacks do not crush");
    }

    // ---- the two painting stacks agree -------------------------
    printf("\n7. GDI and Direct2D reach the same answer\n");
    {
        Theme::SetNightVision(true);
        // The look lands unevenly if they disagree, and the project
        // paints panels with one and the preview with the other.
        bool agree = true;
        for (const auto& t : ladder) {
            const int r = (t.argb >> 16) & 0xFF;
            const int g = (t.argb >> 8)  & 0xFF;
            const int b = (t.argb)       & 0xFF;
            const int gdi = GetRValue(Theme::NV(RGB(r, g, b)));
            const D2D1_COLOR_F f = Theme::NVf(
                D2D1::ColorF(r/255.0f, g/255.0f, b/255.0f, 1.0f));
            const int d2d = (int)(f.r * 255.0f + 0.5f);
            if (std::abs(gdi - d2d) > 1) {
                printf("  FAIL  %s: GDI %d, D2D %d\n", t.name, gdi, d2d);
                agree = false; ++g_fail;
            }
        }
        Check(agree, "every token matches within a rounding step");
    }

    // ---- the cache must still be safe --------------------------
    printf("\n8. NV stays a pure function, which the brush cache needs\n");
    {
        // Theme caches GDI objects keyed on the DAY colour plus a
        // generation counter. If NV returned different answers for the
        // same input it would hand back a brush in the wrong shade,
        // and the cache would never notice.
        Theme::SetNightVision(true);
        const COLORREF c = RGB(0xFF, 0x3B, 0x5C);
        Check(Theme::NV(c) == Theme::NV(c), "same input, same output");
        const unsigned g1 = Theme::PaletteGeneration();
        Theme::SetNightVision(false);
        const unsigned g2 = Theme::PaletteGeneration();
        Check(g2 != g1, "and toggling the mode bumps the generation");
        Theme::SetNightVision(true);
        Check(Theme::PaletteGeneration() != g2, "in both directions");
    }

    Theme::SetNightVision(false);
    if (g_fail == 0) printf("\nPASS\n"); else printf("\nFAIL: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
