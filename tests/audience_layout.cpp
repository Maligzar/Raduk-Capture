// ============================================================
//  The audience surfaces' geometry, and whether the ladder
//  reaches them at all (phase 96.3)
//
//  96.1 fixed a measured inversion in the night palette: TEXT_DIM,
//  for labels you are meant to skim, was derived to 169 while
//  ACCENT4, for capture and danger, was derived to 103. The fix was
//  to give each token a designed luminance ordered by how loudly it
//  needs to speak.
//
//  96.2 then found the same defect class in size, spacing and depth,
//  and defined those axes.
//
//  This stage started by measuring whether any of it had reached the
//  two surfaces other people actually look at. It had not. The share
//  view and the object panel between them painted EIGHTEEN literal
//  colours and used ZERO palette tokens, and NightLuminance falls
//  through to the derived value for a colour it does not recognise.
//  So both surfaces were still running the pre-96.1 behaviour, and
//  the recording light on the screen pointed at the audience was
//  dimmer than the caption underneath it.
//
//  Section 5 pins that, because it is the finding and not the
//  polish. The rest pins the geometry the retrofit is built on --
//  the letterbox the brackets and scrim are positioned against, the
//  nesting of a hand-rolled shadow, and a row of cards that has to
//  be right for one figure and for five, since an absent fact is
//  never drawn.
//
//  "Looks better" is still not testable and this file still does not
//  pretend otherwise.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "gui/AudienceLayout.h"

#include <cstdio>
#include <cmath>
#include <vector>

using namespace raduk::gui;
using namespace raduk::gui::audience;

static int g_fail = 0;

static void Check(bool ok, const char* what) {
    if (ok) printf("  ok    %s\n", what);
    else  { printf("  FAIL  %s\n", what); ++g_fail; }
}
static bool Near(float a, float b, float eps = 0.01f) {
    return std::fabs(a - b) <= eps;
}

int main() {
    printf("Audience layout: the geometry, and whether the ladder arrived\n");

    // ───────────────────────────────────────────────────────────
    printf("\n1. The scaled ramp IS the fixed ramp\n");
    {
        namespace T = Theme::Type;
        bool identity = true, rising = true;
        for (int i = 0; i < T::STEPS; ++i) {
            const float got = T::Scaled((float)T::BODY, i);
            if (!Near(got, (float)T::kRamp[i])) identity = false;
            if (i && T::Ratio(i) <= T::Ratio(i - 1)) rising = false;
        }
        Check(identity,
              "at a body size of BODY it gives back the fixed pixels exactly");
        Check(rising, "the ratios rise, so no scaled step overtakes another");

        // The inverse, which is how the share view keeps a size that
        // was tuned by eye and derives the rest backwards from it.
        bool inverts = true;
        for (int i = 0; i < T::STEPS; ++i) {
            const float px = T::Scaled(31.7f, i);
            if (!Near(T::BodyForStep(px, i), 31.7f, 0.02f)) inverts = false;
        }
        Check(inverts, "BodyForStep inverts Scaled at every step");

        Check(Near(T::Ratio(-5), T::Ratio(0)) &&
              Near(T::Ratio(99), T::Ratio(T::STEPS - 1)),
              "an out-of-range step clamps rather than reading past the table");

        // The floor. Below this the scaled steps compress until
        // adjacent ones are under MIN_STEP apart, which is the
        // one-pixel-apart defect reappearing by arithmetic. Both
        // surfaces clamp to it; this is what they are clamping to.
        bool holdsAtAnchor = true, breaksBelow = false;
        for (int i = 1; i < T::STEPS; ++i) {
            const float gapAt = T::Scaled((float)T::BODY, i)
                              - T::Scaled((float)T::BODY, i - 1);
            if (gapAt < (float)T::MIN_STEP - 0.01f) holdsAtAnchor = false;
            const float gapUnder = T::Scaled((float)T::BODY - 4.0f, i)
                                 - T::Scaled((float)T::BODY - 4.0f, i - 1);
            if (gapUnder < (float)T::MIN_STEP) breaksBelow = true;
        }
        Check(holdsAtAnchor,
              "at the anchor every scaled gap still clears MIN_STEP");
        Check(breaksBelow,
              "and below it one does not, which is why both surfaces floor");

        printf("  --    DISPLAY/BODY = %.4f, the object panel's tuned"
               " 2.9/0.80 = %.4f\n", T::Ratio(T::S_DISPLAY), 2.9 / 0.80);
        Check(Near(T::Ratio(T::S_DISPLAY), 2.9f / 0.80f, 0.02f),
              "DISPLAY reproduces that surface's measured title ratio");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n2. Spacing: one rhythm, two expressions\n");
    {
        namespace S = Theme::Space;
        bool identity = true, rising = true;
        for (int i = 0; i < S::STEPS; ++i) {
            if (!Near(S::Scaled((float)Theme::Type::BODY, i),
                      (float)S::kSteps[i])) identity = false;
            if (i && S::Ems(i) <= S::Ems(i - 1)) rising = false;
        }
        Check(identity, "the scaled steps give back the fixed pixels at BODY");
        Check(rising, "and stay monotonic, so the rhythm cannot invert");
        Check(Near(S::Ems(-1), S::Ems(0)) &&
              Near(S::Ems(42), S::Ems(S::STEPS - 1)),
              "out-of-range clamps");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n3. The letterbox the rest is positioned against\n");
    {
        struct Case { float vw, vh, iw, ih; const char* what; };
        const Case cases[] = {
            { 1920, 1080,  640,  480, "4:3 ROI on a 16:9 screen"   },
            { 1920, 1080, 1920, 1080, "exact fit"                  },
            { 1080, 1920,  640,  480, "portrait screen"            },
            { 1920, 1080,  200, 1000, "a tall thin crop"           },
            { 3840, 2160,  320,  240, "a small ROI on 4K"          },
        };
        bool aspect = true, inside = true, centred = true, touches = true;
        for (const Case& c : cases) {
            const LRect f = LetterboxFit(c.vw, c.vh, c.iw, c.ih);
            const float gw = Width(f), gh = Height(f);
            if (!Near(gw / gh, c.iw / c.ih, 0.001f)) aspect = false;
            if (gw > c.vw + 0.01f || gh > c.vh + 0.01f) inside = false;
            if (!Near(f.l, c.vw - f.r, 0.01f) ||
                !Near(f.t, c.vh - f.b, 0.01f)) centred = false;
            // One dimension must reach the view, or there is slack
            // being thrown away.
            if (!Near(gw, c.vw, 0.02f) && !Near(gh, c.vh, 0.02f))
                touches = false;
            printf("  --    %-26s %.0fx%.0f at (%.0f,%.0f)\n",
                   c.what, gw, gh, f.l, f.t);
        }
        Check(aspect, "aspect ratio preserved, never stretched");
        Check(inside, "always inside the view");
        Check(centred, "centred: equal margins on both axes");
        Check(touches, "and fills the limiting dimension exactly");

        // It must enlarge as well as shrink. A 320x240 ROI shown at
        // 1:1 on a wall is a postage stamp.
        const LRect up = LetterboxFit(3840, 2160, 320, 240);
        Check(Width(up) > 320.0f, "a small ROI is scaled UP to the screen");

        const LRect bad = LetterboxFit(800, 600, 0, 0);
        Check(Near(Width(bad), 800.0f) && Near(Height(bad), 600.0f),
              "no frame yet gives the whole view and divides by nothing");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n4. Corner brackets cannot become a box\n");
    {
        const float body = 26.0f;
        bool bounded = true;
        // The clamp exists for exactly this: a narrow planetary crop
        // letterboxes to a tall thin rectangle, and two unclamped
        // arms on its short edge meet in the middle.
        for (float w = 10.0f; w < 2000.0f; w *= 1.35f) {
            for (float h = 10.0f; h < 2000.0f; h *= 1.35f) {
                const LRect k{ 0, 0, w, h };
                const float arm = BracketArm(k, body);
                if (arm * 2.0f > ShortEdge(k) + 0.001f) bounded = false;
                if (arm < 0.0f) bounded = false;
            }
        }
        Check(bounded,
              "two arms never meet, at any aspect ratio or size");

        Check(BracketArm(LRect{ 0, 0, 60, 2000 }, body) == 0.0f,
              "a strip under three arms across gets no marks at all");
        // The case the gate used to refuse: a 200x1000 crop
        // letterboxes to 216x1080 on a 1080p screen, and that strip
        // is precisely where a bystander cannot see the frame edge.
        Check(BracketArm(LRect{ 852, 0, 1068, 1080 }, body) > 0.0f,
              "but a thin letterboxed crop DOES, which the first gate"
              " refused");
        Check(BracketArm(LRect{ 0, 0, 1920, 1080 }, body) > 0.0f,
              "a full frame does");
        Check(BracketArm(LRect{ 0, 0, 1920, 1080 }, body * 2.0f) >
              BracketArm(LRect{ 0, 0, 1920, 1080 }, body),
              "the arm scales with the type, like everything else");
        Check(BracketArm(LRect{ 0, 0, 1920, 1080 }, 0.0f) == 0.0f,
              "and a zero body size paints nothing rather than dividing");

        // The clamp has to be reachable or it is not an invariant,
        // it is a comment. Between three and four body sizes the
        // fixed arm is longer than the edge allows and the clamp is
        // what decides.
        const float narrow = body * 3.2f;
        const LRect thin{ 0, 0, narrow, 1080 };
        Check(Near(BracketArm(thin, body), narrow * 0.40f),
              "just above the gate the clamp, not the type size, decides");
        const LRect wide{ 0, 0, body * 6.0f, 1080 };
        Check(Near(BracketArm(wide, body), body * 1.6f),
              "and above that the type size does");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n5. THE FINDING: 96.1's ladder had not reached these surfaces\n");
    {
        // Every one of these was a literal in the painting code until
        // this stage. NightLuminance recognises no literal, so each
        // fell through to the derived value -- which is the behaviour
        // 96.1 was written to replace.
        struct Lit { int r, g, b; const char* role; uint32_t token; };
        const Lit lits[] = {
            { 255,  38,  64, "share view REC light", Theme::ACCENT4  },
            { 140, 179, 217, "share view caption line", Theme::TEXT_DIM },
            { 140, 166, 191, "object panel dim labels", Theme::TEXT_DIM },
            { 235, 242, 250, "object panel body text", Theme::TEXT    },
        };
        bool anyWrong = false;
        for (const Lit& l : lits) {
            const int derived = Theme::NightLuminance(l.r, l.g, l.b);
            const int designed = Theme::NightLuminance(
                (int)((l.token >> 16) & 0xFF),
                (int)((l.token >> 8)  & 0xFF),
                (int)( l.token        & 0xFF));
            printf("  --    %-26s literal -> %3d   token -> %3d\n",
                   l.role, derived, designed);
            if (derived != designed) anyWrong = true;
        }
        Check(anyWrong,
              "the literals did NOT paint what the ladder designed");

        // The inversion itself, which is the whole of 96.1 undone.
        const int recLit = Theme::NightLuminance(255, 38, 64);
        const int dimLit = Theme::NightLuminance(140, 179, 217);
        Check(recLit < dimLit,
              "as literals the recording light was DIMMER than the"
              " caption beside it");

        const int danger = Theme::NightLuminance(0xFF, 0x3B, 0x5C);
        const int dim    = Theme::NightLuminance(0x8C, 0xAF, 0xBE);
        Check(danger > dim,
              "as tokens danger outranks a skimmed label, which is"
              " what 96.1 decided");

        // ── And the object panel's six per-type accents ─────
        //
        // The first draft of this test asserted that the galaxy
        // accent outshone primary text. It does not -- it derives to
        // 192 against TEXT's designed 200, and the 246 that claim
        // came from was my own arithmetic slip on the blue term. The
        // real defect is smaller and in a different accent, which is
        // the reason to measure all six rather than the one that
        // looked worst.
        struct Acc { int r, g, b; const char* kind; };
        const Acc accents[] = {
            { 255, 184,  89, "Planet"    }, { 217, 219, 204, "Satellite" },
            { 173, 204, 255, "Star"      }, { 217, 179, 255, "Galaxy"    },
            { 255, 140, 153, "Nebula"    }, { 102, 230, 242, "deep sky"  },
        };
        const int text = Theme::NightLuminance(0xC8, 0xE8, 0xF0);
        int over = 0, worst = 0, lo = 999, hi = 0;
        for (const Acc& a : accents) {
            const int L = Theme::NightLuminance(a.r, a.g, a.b);
            printf("  --    accent %-10s -> %3d%s\n", a.kind, L,
                   L > text ? "   over TEXT" : "");
            if (L > text) { ++over; if (L - text > worst) worst = L - text; }
            if (L < lo) lo = L;
            if (L > hi) hi = L;
        }
        printf("  --    TEXT %d, danger %d; accents span %d..%d\n",
               text, danger, lo, hi);
        Check(over == 3 && worst == 17,
              "three of the six outshine primary text, the worst by 17");
        Check(hi < danger,
              "none reaches danger, so this is a smaller inversion than"
              " 96.1's, not the same one");
        Check(hi - lo < 60,
              "and they collapse into each other anyway, so the hue was"
              " carrying nothing in this mode");
        Check(Theme::NightLuminance(0x00, 0xE5, 0xFF) < text,
              "the ACCENT token they take in night mode sits below TEXT");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n6. A hand-rolled shadow that keeps Theme's ordering\n");
    {
        Check(!ShadowVisible(Theme::ShadowFor(Theme::Elev::FLAT)),
              "FLAT paints nothing, so a flush element costs no fill");
        Check(ShadowVisible(Theme::ShadowFor(Theme::Elev::RAISED)) &&
              ShadowVisible(Theme::ShadowFor(Theme::Elev::OVERLAY)) &&
              ShadowVisible(Theme::ShadowFor(Theme::Elev::MODAL)),
              "the other three do");

        const LRect card{ 100, 100, 400, 200 };
        for (int lvl = Theme::Elev::RAISED; lvl <= Theme::Elev::MODAL; ++lvl) {
            const Theme::Shadow sh = Theme::ShadowFor(lvl);
            float sum = 0.0f;
            bool nested = true, below = true, darkening = true;
            float prevA = -1.0f;
            LRect prev{};
            for (int i = 0; i < SHADOW_RINGS; ++i) {
                const Ring g = ShadowRingAt(card, sh, i);
                sum += g.alpha;
                if (g.rect.b <= card.b) below = false;
                if (prevA >= 0.0f) {
                    if (g.alpha <= prevA) darkening = false;
                    // Each ring must sit strictly inside the one
                    // before it, or the "blur" is a set of stripes.
                    if (!(g.rect.l > prev.l && g.rect.r < prev.r &&
                          g.rect.t > prev.t && g.rect.b < prev.b))
                        nested = false;
                }
                prevA = g.alpha; prev = g.rect;
            }
            printf("  --    level %d  dy=%d blur=%2d alpha=%.2f"
                   "  rings sum to %.4f\n", lvl, sh.dy, sh.blur,
                   sh.alpha, sum);
            Check(Near(sum, sh.alpha, 0.0005f),
                  "ring weights sum to the level's stated alpha");
            Check(nested, "rings nest strictly inwards");
            Check(darkening, "and darken as they tighten");
            Check(below, "every ring falls below the card, never above it");
        }

        const Ring lo = ShadowRingAt(card, Theme::ShadowFor(Theme::Elev::MODAL), -3);
        const Ring hi = ShadowRingAt(card, Theme::ShadowFor(Theme::Elev::MODAL), 99);
        Check(Near(lo.alpha, ShadowRingAt(card,
                  Theme::ShadowFor(Theme::Elev::MODAL), 0).alpha) &&
              Near(hi.alpha, ShadowRingAt(card,
                  Theme::ShadowFor(Theme::Elev::MODAL),
                  SHADOW_RINGS - 1).alpha),
              "an out-of-range ring index clamps");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n7. A figure row, for however many figures exist\n");
    {
        // The count is whatever the data supports and an absent fact
        // is never drawn, so one card and five are both ordinary.
        const float body = 33.0f;
        const LRect row{ 87, 400, 87 + 1746, 400 + 120 };
        const float gut = Theme::Space::Scaled(body, Theme::Space::P_SM);
        bool equal = true, gutters = true, spans = true, ordered = true;

        for (int n = 1; n <= 6; ++n) {
            float w0 = -1.0f;
            for (int i = 0; i < n; ++i) {
                const LRect c = CardInRow(row, n, i, body);
                if (w0 < 0.0f) w0 = Width(c);
                else if (!Near(Width(c), w0, 0.02f)) equal = false;
                if (i) {
                    const LRect prev = CardInRow(row, n, i - 1, body);
                    if (!Near(c.l - prev.r, gut, 0.02f)) gutters = false;
                    if (c.l < prev.r) ordered = false;
                }
            }
            const LRect first = CardInRow(row, n, 0, body);
            const LRect last  = CardInRow(row, n, n - 1, body);
            if (!Near(first.l, row.l, 0.02f) ||
                !Near(last.r,  row.r, 0.05f)) spans = false;
            printf("  --    %d card%s  %.1f wide, gutter %.1f\n",
                   n, n == 1 ? " " : "s", Width(first), n > 1 ? gut : 0.0f);
        }
        Check(equal,   "every card in a row is the same width");
        Check(gutters, "and separated by exactly the shared rhythm");
        Check(ordered, "with no overlap");
        Check(spans,   "the row is filled edge to edge, 1 card or 6");

        Check(Width(CardInRow(row, 0, 0, body)) == 0.0f,
              "no figures gives a degenerate card, not a division by zero");
        const LRect clampLo = CardInRow(row, 3, -2, body);
        const LRect clampHi = CardInRow(row, 3, 9, body);
        Check(Near(clampLo.l, CardInRow(row, 3, 0, body).l) &&
              Near(clampHi.l, CardInRow(row, 3, 2, body).l),
              "an out-of-range index clamps to an end card");

        // A row narrower than its own gutters must not produce a
        // card of negative width, which would draw inside out.
        const LRect tiny{ 0, 0, 10, 40 };
        const LRect squeezed = CardInRow(tiny, 6, 3, body);
        Check(Width(squeezed) >= 0.0f,
              "a row too narrow for its gutters degenerates, never inverts");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n8. The caption sits on something\n");
    {
        const float body = 29.0f;
        const LRect band = CaptionScrim(1920, 1080, 900, body);
        Check(band.t <= 900.0f, "the scrim starts above the caption");
        Check(Near(band.b, 1080.0f), "and runs to the bottom edge");
        Check(Near(Width(band), 1920.0f), "across the full width");
        printf("  --    caption at 900 -> scrim from %.1f, %.1f tall\n",
               band.t, Height(band));

        const LRect high = CaptionScrim(1920, 1080, 5.0f, body);
        Check(high.t >= 0.0f, "a caption near the top does not scrim"
                              " off the top of the window");
        const LRect past = CaptionScrim(1920, 1080, 5000.0f, body);
        Check(past.t <= 1080.0f && Height(past) >= 0.0f,
              "a caption past the bottom degenerates rather than inverting");
        const LRect none = CaptionScrim(0, 0, 10, body);
        Check(Height(none) == 0.0f, "an empty window scrims nothing");
        Check(SCRIM_PEAK_ALPHA > 0.0f && SCRIM_PEAK_ALPHA < 1.0f,
              "the scrim is a scrim, not a black bar");
    }

    printf("\n%s\n", g_fail ? "FAILURES" : "all good");
    return g_fail ? 1 : 0;
}
