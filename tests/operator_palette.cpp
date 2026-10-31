// ============================================================
//  The operator chrome's palette, and depth without alpha
//  (phase 96.4)
//
//  96.1 gave each token a designed night luminance, ordered by how
//  loudly it needs to speak, with danger at the top: "the thing that
//  should shout" must not be "quieter than the thing that should
//  whisper."
//
//  96.3 found that fix had never reached the two audience surfaces,
//  because they painted literal colours and NightLuminance
//  recognises no literal.
//
//  96.4 measured the operator chrome and found the same thing, worse.
//  Of 156 painting sites across nine surfaces, 82 matched a ladder
//  entry and 74 did not -- fifty-three per cent. And the misses were
//  not decoration:
//
//    RGB(255, 60, 90), one green unit and two blue units from
//    ACCENT4, painted ELEVEN recording and fault indicators at a
//    night luminance of 103. One hundred and three is the exact
//    figure 96.1's plan cited as the defect it existed to fix.
//
//    The status window's whole three-tier warning system was off the
//    ladder -- critical, caution AND good -- and in night vision the
//    four read 103, 175, 150 and 134. CRITICAL WAS THE DIMMEST OF
//    THE FOUR.
//
//    Theme::DEEP was painted by nothing at all. Eighteen sites spelled
//    RGB(7, 13, 22) and commented it `--bg-deep`; the token was
//    (7, 8, 14), a different colour.
//
//  Section 1 pins that, because it is the defect. Sections 4 and 5
//  pin the depth primitives, whose one subtle property is the order
//  of the blend and the night transform -- get it backwards and it
//  compiles, looks plausible, and silently renegotiates the ratio
//  every elevation level states.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "gui/Theme.h"

#include <cstdio>
#include <cmath>

using namespace raduk::gui;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (ok) printf("  ok    %s\n", what);
    else  { printf("  FAIL  %s\n", what); ++g_fail; }
}
static int NL(COLORREF c) {
    return Theme::NightLuminance(GetRValue(c), GetGValue(c), GetBValue(c));
}
static int Derived(int r, int g, int b) {
    return (int)(0.2126f * r + 0.7152f * g + 0.0722f * b);
}

int main() {
    printf("Operator palette: the ladder's coverage, and depth with no alpha\n");

    // ───────────────────────────────────────────────────────────
    printf("\n1. THE DEFECT: the warning tiers were inverted in the dark\n");
    {
        // The four colours the status window painted, as literals.
        const int critLit = Derived(255,  60,  90);
        const int cautLit = Derived(255, 170,   0);
        const int goodLit = Derived(  0, 200, 110);
        const int dimLit  = Derived(120, 136, 156);
        printf("  --    as literals:  critical %3d  caution %3d  good %3d"
               "  labels %3d\n", critLit, cautLit, goodLit, dimLit);
        Check(critLit < cautLit && critLit < goodLit && critLit < dimLit,
              "critical was the DIMMEST of the four, which is the whole"
              " of 96.1 undone");
        Check(critLit == 103,
              "at 103 -- the exact figure 96.1's plan named as the defect");

        const int crit = NL(Theme::Rgb::ACCENT4);
        const int caut = NL(Theme::Rgb::ACCENT2);
        const int good = NL(Theme::Rgb::ACCENT3);
        const int dim  = NL(Theme::Rgb::TEXT_DIM);
        printf("  --    as tokens:    critical %3d  caution %3d  good %3d"
               "  labels %3d\n", crit, caut, good, dim);
        Check(crit > caut && crit > good && crit > dim,
              "as tokens critical is the loudest thing on the surface");
        Check(crit - good >= Theme::detail::kMinNightSeparation,
              "and clears the separation floor against the tier below it");

        // Stated rather than asserted silently: using the device-role
        // accents as warning tiers borrows semantics they were not
        // designed for, and it shows here. The ladder has no caution
        // rung, and inserting one means re-spacing all six signals.
        printf("  --    NOTE: good (%d) still outranks caution (%d).\n",
               good, caut);
        Check(good > caut,
              "which is backwards for a warning tier, and is the"
              " owner's call -- see the PR, not a silent re-spacing");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n2. The two spellings of every token agree\n");
    {
        struct P { uint32_t argb; COLORREF ref; const char* name; };
        const P pairs[] = {
            { Theme::BG_VOID,  Theme::Rgb::BG_VOID,  "BG_VOID"  },
            { Theme::DEEP,     Theme::Rgb::DEEP,     "DEEP"     },
            { Theme::PANEL,    Theme::Rgb::PANEL,    "PANEL"    },
            { Theme::BORDER,   Theme::Rgb::BORDER,   "BORDER"   },
            { Theme::ACCENT,   Theme::Rgb::ACCENT,   "ACCENT"   },
            { Theme::ACCENT2,  Theme::Rgb::ACCENT2,  "ACCENT2"  },
            { Theme::ACCENT3,  Theme::Rgb::ACCENT3,  "ACCENT3"  },
            { Theme::ACCENT4,  Theme::Rgb::ACCENT4,  "ACCENT4"  },
            { Theme::TEXT,     Theme::Rgb::TEXT,     "TEXT"     },
            { Theme::TEXT_DIM, Theme::Rgb::TEXT_DIM, "TEXT_DIM" },
        };
        bool agree = true, onLadder = true;
        for (const P& p : pairs) {
            if (GetRValue(p.ref) != (int)((p.argb >> 16) & 0xFF) ||
                GetGValue(p.ref) != (int)((p.argb >> 8)  & 0xFF) ||
                GetBValue(p.ref) != (int)( p.argb        & 0xFF)) {
                printf("  --    %s disagrees\n", p.name);
                agree = false;
            }
            // Each must be a LADDER entry, not a derived fallback. A
            // token whose own bytes miss the ladder is the whole bug
            // in a single constant.
            if (NL(p.ref) == Derived(GetRValue(p.ref), GetGValue(p.ref),
                                     GetBValue(p.ref))
                && NL(p.ref) != 0) {
                // Equal by coincidence is possible; flag only if the
                // colour is genuinely absent from the table.
                bool found = false;
                for (const auto& s : Theme::detail::kNightLadder)
                    if (s.r == GetRValue(p.ref) && s.g == GetGValue(p.ref) &&
                        s.b == GetBValue(p.ref)) found = true;
                if (!found) { printf("  --    %s is OFF the ladder\n", p.name);
                              onLadder = false; }
            }
        }
        Check(agree, "the COLORREF spelling matches the 0xAARRGGBB one"
                     " byte for byte");
        Check(onLadder, "and every token's own bytes are a ladder entry,"
                        " so no token falls through to derived");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n3. DEEP: the token nothing painted\n");
    {
        // It was (7, 8, 14); eighteen sites painted (7, 13, 22) and
        // called it --bg-deep. The token moved to the code, because
        // the code is what has shipped and nothing painted the token.
        Check(GetRValue(Theme::Rgb::DEEP) == 7 &&
              GetGValue(Theme::Rgb::DEEP) == 13 &&
              GetBValue(Theme::Rgb::DEEP) == 22,
              "DEEP is now the colour the panels actually paint");
        printf("  --    old token (7,8,14) would derive to %d; the painted"
               " colour derived to %d; designed is %d\n",
               Derived(7, 8, 14), Derived(7, 13, 22), NL(Theme::Rgb::DEEP));
        Check(NL(Theme::Rgb::DEEP) == 14,
              "and keeps the designed luminance 96.1 gave it");

        // The surface order still has to hold, or a raised panel stops
        // reading as raised.
        Check(NL(Theme::Rgb::BG_VOID) < NL(Theme::Rgb::DEEP) &&
              NL(Theme::Rgb::DEEP)    < NL(Theme::Rgb::PANEL) &&
              NL(Theme::Rgb::PANEL)   < NL(Theme::Rgb::BORDER),
              "void < deep < panel < border, in the dark");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n4. Mix, and the order that is easy to get backwards\n");
    {
        Check(Theme::Mix(RGB(0,0,0), RGB(100,200,250), 0.0f) == RGB(0,0,0) &&
              Theme::Mix(RGB(0,0,0), RGB(100,200,250), 1.0f) == RGB(100,200,250),
              "the endpoints are exact, not almost");
        const COLORREF half = Theme::Mix(RGB(0,0,0), RGB(100,200,250), 0.5f);
        Check(GetRValue(half) == 50 && GetGValue(half) == 100 &&
              GetBValue(half) == 125,
              "and the midpoint rounds rather than truncating");
        Check(Theme::Mix(RGB(10,20,30), RGB(200,0,0), -1.0f) == RGB(10,20,30) &&
              Theme::Mix(RGB(10,20,30), RGB(200,0,0),  9.0f) == RGB(200,0,0),
              "out-of-range t clamps to an endpoint");

        // THE PROPERTY. NV() is a table for the tokens and linear only
        // for everything else, so blending first and tinting after
        // gives (1-alpha) of the token's DERIVED luminance while the
        // surface beside it is painted at its DESIGNED one.
        Theme::SetNightVision(true);
        const int surface = NL(Theme::Rgb::PANEL);              // designed
        const COLORREF right = Theme::ShadowOver(Theme::Rgb::PANEL, 0.35f);
        const int gotRight = GetRValue(right);

        // What the wrong order would have produced.
        const COLORREF pre = Theme::Mix(Theme::Rgb::PANEL, RGB(0,0,0), 0.35f);
        const int gotWrong = GetRValue(Theme::NV(pre));

        printf("  --    PANEL designed %d; shadow at 0.35 -> %d"
               "  (ratio %.2f, asked for %.2f)\n",
               surface, gotRight, (float)gotRight / (float)surface, 0.65f);
        printf("  --    blending BEFORE the transform would give %d"
               "  (ratio %.2f)\n", gotWrong, (float)gotWrong / (float)surface);
        Check(std::fabs((float)gotRight / (float)surface - 0.65f) < 0.03f,
              "composited after the transform, the ratio asked for is"
              " the ratio that appears");
        Check(gotWrong < gotRight,
              "and the other order is measurably darker, which is why"
              " it is worth a test rather than a comment");
        Theme::SetNightVision(false);
    }

    // ───────────────────────────────────────────────────────────
    printf("\n5. The highlight adds no blue to a night-vision display\n");
    {
        // The mount controller's state lamp had a rim of
        // Blend(lit, RGB(255,255,255), 0.35f), where lit was already
        // through NV(). That is a third of the way back to the short
        // wavelengths phase 26 exists to remove, on the one surface
        // read from the eyepiece.
        Theme::SetNightVision(true);
        const COLORREF lit  = Theme::NV(Theme::Rgb::ACCENT3);
        const COLORREF bad  = Theme::Mix(lit, RGB(255,255,255), 0.35f);
        const COLORREF good = Theme::HighlightOver(Theme::Rgb::ACCENT3, 0.35f);
        printf("  --    toward white: blue %3d      scaled up: blue %3d\n",
               GetBValue(bad), GetBValue(good));
        Check(GetBValue(bad) > 80,
              "blending toward white put real blue back on the screen");
        // The first replacement scaled each channel TOWARD 255, which
        // is the same mistake with different arithmetic: it gave this
        // channel 45. Multiplying preserves the ratios between the
        // channels, and zero times anything is zero.
        Check(GetBValue(good) == 0,
              "multiplying keeps a channel night vision emptied empty");
        Check(GetRValue(good) > GetRValue(lit),
              "while still brightening, which is the point of a highlight");

        // And it must be weaker than its own shadow, or it reads as a
        // bevel rather than as depth.
        const int up   = GetRValue(good) - GetRValue(lit);
        const COLORREF sh = Theme::ShadowOver(Theme::Rgb::ACCENT3, 0.35f);
        const int down = GetRValue(lit) - GetRValue(sh);
        printf("  --    lifts %d, shades %d\n", up, down);
        Check(up < down, "the lit edge is weaker than the shade below it");
        Theme::SetNightVision(false);

        // Day mode: lighten toward white is correct there, and must
        // not overflow.
        const COLORREF dayLift = Theme::HighlightOver(Theme::Rgb::TEXT, 1.0f);
        Check(GetRValue(dayLift) <= 255 && GetGValue(dayLift) <= 255 &&
              GetBValue(dayLift) <= 255,
              "a full-weight highlight on a near-white token cannot"
              " overflow a byte");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n6. Elevation keeps 96.2's order through the GDI approximation\n");
    {
        int prev = 1 << 30;
        bool ordered = true;
        for (int lvl = Theme::Elev::FLAT; lvl < Theme::Elev::LEVELS; ++lvl) {
            const COLORREF c = Theme::ShadowForLevel(Theme::Rgb::PANEL, lvl);
            const int v = GetRValue(c) + GetGValue(c) + GetBValue(c);
            printf("  --    level %d -> channel sum %d\n", lvl, v);
            if (lvl > Theme::Elev::FLAT && v >= prev) ordered = false;
            prev = v;
        }
        Check(ordered,
              "a higher level is strictly darker beneath it, FLAT to MODAL");
        Check(Theme::ShadowForLevel(Theme::Rgb::PANEL, Theme::Elev::FLAT)
              == Theme::NV(Theme::Rgb::PANEL),
              "and FLAT is the face itself, so a flush control is flush");
        Check(Theme::ShadowForLevel(Theme::Rgb::PANEL, -5) ==
              Theme::ShadowForLevel(Theme::Rgb::PANEL, Theme::Elev::FLAT) &&
              Theme::ShadowForLevel(Theme::Rgb::PANEL, 99) ==
              Theme::ShadowForLevel(Theme::Rgb::PANEL, Theme::Elev::MODAL),
              "an out-of-range level clamps");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n7. Day mode is untouched, which 96.1 also promised\n");
    {
        Theme::SetNightVision(false);
        Check(Theme::NV(Theme::Rgb::ACCENT4) == Theme::Rgb::ACCENT4 &&
              Theme::NV(Theme::Rgb::DEEP)    == Theme::Rgb::DEEP,
              "with night vision off a token is its own bytes");
        const COLORREF s = Theme::ShadowOver(Theme::Rgb::PANEL, 0.35f);
        Check(GetRValue(s) < GetRValue(Theme::Rgb::PANEL) ||
              GetGValue(s) < GetGValue(Theme::Rgb::PANEL) ||
              GetBValue(s) < GetBValue(Theme::Rgb::PANEL),
              "and a shadow still darkens in daylight");
    }

    printf("\n%s\n", g_fail ? "FAILURES" : "all good");
    return g_fail ? 1 : 0;
}
