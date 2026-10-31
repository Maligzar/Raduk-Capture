// ============================================================
//  The visual system's invariants (phase 96.2)
//
//  Night vision collapses colour to luminance, so hue carries no
//  meaning. What survives is everything geometric: size, spacing,
//  depth. Until this phase none of those was designed -- the
//  palette was the whole system -- and the two axes that did
//  exist by accident had the same defect as the palette did.
//
//  Measured before this stage:
//
//    SIZE. Sixteen font creations across six sizes, 11 through
//    16, adjacent steps ONE PIXEL apart. That is not a
//    hierarchy, for the same reason cyan and amber four
//    luminance units apart were not one. And the two painting
//    stacks disagreed: Direct2D used 11/13/15, GDI used all six.
//
//    SPACING. Literals of 4, 6, 8, 10, 12, 14 chosen locally in
//    each panel, so surfaces written months apart do not line up.
//
//    DEPTH. None at all.
//
//  "Looks better" is not testable and this file does not
//  pretend otherwise. What is testable is whether the system
//  can still CARRY a hierarchy, and that is what decays silently
//  when someone adds a sixth spacing value or a size one pixel
//  from an existing one. So these are the invariants, not the
//  aesthetics.
//
//  Nothing here asserts that any surface USES the tokens yet.
//  96.2 is definitions only, deliberately: retrofitting moves
//  layout, and layout here is hard pixel offsets that have to be
//  adjusted where the result can be looked at.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "gui/Theme.h"

#include <cstdio>
#include <cmath>
#include <vector>

using namespace raduk::gui;

static int g_fail = 0;

static void Check(bool ok, const char* what) {
    if (ok) printf("  ok    %s\n", what);
    else  { printf("  FAIL  %s\n", what); ++g_fail; }
}

int main() {
    printf("Visual tokens: can the system still carry a hierarchy\n");

    // ---- the type ramp -----------------------------------------
    printf("\n1. The type ramp is a ramp\n");
    {
        const int ramp[] = { Theme::Type::MICRO, Theme::Type::BODY,
                             Theme::Type::STRONG, Theme::Type::TITLE,
                             Theme::Type::HERO };
        const char* names[] = { "MICRO", "BODY", "STRONG", "TITLE", "HERO" };
        const int N = 5;

        bool rising = true, spaced = true;
        int minStep = 9999;
        for (int i = 1; i < N; ++i) {
            const int step = ramp[i] - ramp[i-1];
            printf("  --    %-6s %2d -> %-6s %2d   (+%d)\n",
                   names[i-1], ramp[i-1], names[i], ramp[i], step);
            if (step <= 0) rising = false;
            if (step < Theme::Type::MIN_STEP) spaced = false;
            if (step < minStep) minStep = step;
        }
        Check(rising, "every step is larger than the one below it");
        Check(spaced, "and at least MIN_STEP apart, so no two read alike");
        Check(minStep >= 2,
              "which rules out the one-pixel steps this replaced");

        // Growing increments: a title must not be a near-miss for a
        // heading at the top of the ramp where sizes are largest.
        bool growing = true;
        for (int i = 2; i < N; ++i)
            if ((ramp[i] - ramp[i-1]) < (ramp[i-1] - ramp[i-2])) growing = false;
        Check(growing, "and the ramp separates more as it climbs");

        Check(Theme::Type::MICRO >= 11,
              "the smallest step is still legible on this rig's displays");
    }

    // ---- spacing -----------------------------------------------
    printf("\n2. Spacing is a scale, not a pile of numbers\n");
    {
        const int sc[] = { Theme::Space::XS, Theme::Space::SM,
                           Theme::Space::MD, Theme::Space::LG,
                           Theme::Space::XL, Theme::Space::XXL };
        const char* names[] = { "XS", "SM", "MD", "LG", "XL", "XXL" };
        const int N = 6;

        bool rising = true, onGrid = true;
        for (int i = 0; i < N; ++i) {
            if (sc[i] % Theme::Space::BASE != 0) {
                printf("  FAIL  %s (%d) is off the %d px grid\n",
                       names[i], sc[i], Theme::Space::BASE);
                onGrid = false; ++g_fail;
            }
            if (i > 0 && sc[i] <= sc[i-1]) rising = false;
        }
        printf("  --    %d %d %d %d %d %d, base %d\n",
               sc[0], sc[1], sc[2], sc[3], sc[4], sc[5], Theme::Space::BASE);
        Check(onGrid, "every step is a multiple of the base");
        Check(rising, "and the scale is strictly increasing");

        // The old literals included 6, 10 and 14: off a 4px grid, so
        // nothing written against them could line up with anything
        // written against 8 or 12.
        Check(6 % Theme::Space::BASE != 0 && 10 % Theme::Space::BASE != 0,
              "and the odd old literals 6 and 10 are off it, which is why "
              "surfaces written apart never aligned");
    }

    // ---- elevation ---------------------------------------------
    printf("\n3. Depth is ordered, and each level has one definition\n");
    {
        const char* names[] = { "FLAT", "RAISED", "OVERLAY", "MODAL" };
        Theme::Shadow prev = Theme::ShadowFor(Theme::Elev::FLAT);
        Check(prev.dy == 0 && prev.blur == 0 && prev.alpha == 0.0f,
              "FLAT casts no shadow at all");

        bool ordered = true;
        for (int i = 1; i < Theme::Elev::LEVELS; ++i) {
            const Theme::Shadow s = Theme::ShadowFor(i);
            printf("  --    %-8s dy %d, blur %2d, alpha %.2f\n",
                   names[i], s.dy, s.blur, s.alpha);
            if (s.dy <= prev.dy || s.blur <= prev.blur || s.alpha <= prev.alpha)
                ordered = false;
            prev = s;
        }
        Check(ordered,
              "each level sits further off its surface than the one below, "
              "in offset, blur AND weight");

        // Totality: the function must answer for every level and must
        // not read past the table when handed nonsense.
        bool total = true;
        for (int i = 0; i < Theme::Elev::LEVELS; ++i) {
            const Theme::Shadow a = Theme::ShadowFor(i);
            const Theme::Shadow b = Theme::ShadowFor(i);
            if (a.dy != b.dy || a.blur != b.blur || a.alpha != b.alpha)
                total = false;
        }
        Check(total, "and the same level always gives the same shadow");

        const Theme::Shadow low  = Theme::ShadowFor(-5);
        const Theme::Shadow high = Theme::ShadowFor(999);
        Check(low.dy == Theme::ShadowFor(Theme::Elev::FLAT).dy,
              "a level below the range clamps to FLAT");
        Check(high.dy == Theme::ShadowFor(Theme::Elev::MODAL).dy,
              "and one above it clamps to MODAL, rather than reading past "
              "the end of the table");
    }

    // ---- the axis that survives --------------------------------
    printf("\n4. Depth survives night vision, which is the whole point\n");
    {
        // A shadow is alpha over whatever is beneath it. NV() collapses
        // colour but does not touch alpha, so depth reads identically
        // in both modes -- unlike hue, which is why depth is where the
        // look has to come from.
        Theme::SetNightVision(false);
        const D2D1_COLOR_F dayShadow =
            Theme::NVf(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.45f));
        Theme::SetNightVision(true);
        const D2D1_COLOR_F nightShadow =
            Theme::NVf(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.45f));
        Theme::SetNightVision(false);

        Check(std::fabs(dayShadow.a - nightShadow.a) < 1e-6f,
              "a shadow's weight is identical in both modes");
        Check(std::fabs(nightShadow.r) < 1e-6f,
              "and black stays black, so it darkens rather than tints");

        // Every elevation's alpha survives, not just the one above.
        bool allSurvive = true;
        Theme::SetNightVision(true);
        for (int i = 0; i < Theme::Elev::LEVELS; ++i) {
            const float a = Theme::ShadowFor(i).alpha;
            const D2D1_COLOR_F c =
                Theme::NVf(D2D1::ColorF(0.0f, 0.0f, 0.0f, a));
            if (std::fabs(c.a - a) > 1e-6f) allSurvive = false;
        }
        Theme::SetNightVision(false);
        Check(allSurvive, "and that holds at every level of the scale");
    }

    // ---- the two systems agree about what matters --------------
    printf("\n5. Size and spacing are integers both stacks can use\n");
    {
        // GDI takes pixel heights, Direct2D takes float sizes. The
        // tokens are integers so neither stack has to round, which is
        // how 11 in one place became 11.0 in another and then drifted.
        Check(Theme::Type::BODY == (int)(float)Theme::Type::BODY,
              "a type step survives the trip through float");
        Check(Theme::Space::MD == (int)(float)Theme::Space::MD,
              "and so does a spacing step");
        Check(Theme::Type::MICRO == 11,
              "MICRO matches the 11 both stacks already used, so the "
              "smallest text does not move when surfaces adopt the ramp");
    }

    if (g_fail == 0) printf("\nPASS\n"); else printf("\nFAIL: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
