#pragma once
// ============================================================
// Theme.h — Elite Raduk design tokens
// Deep-space palette: void black base, cyan primary accent,
// amber secondary, green for live/mount, red for capture.
// Typography: Space Mono (data), Barlow Condensed (labels).
// ============================================================

#include <windows.h>
#include <d2d1.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#include <cstdint>
#include <vector>

namespace raduk::gui {
namespace Theme {

    // ── Core palette ───────────────────────────────────────────
    //   0xFFRRGGBB (alpha always FF — fully opaque)

    constexpr uint32_t BG_VOID   = 0xFF04080F;  // --bg-void  (preview bg, deepest surface)
    // ── Phase 96.4 moved this ───────────────────────────────
    //
    // DEEP was 0xFF07080E, which is (7, 8, 14), and NOTHING IN THE
    // APPLICATION EVER PAINTED IT. Eighteen painting sites across the
    // operator chrome -- the most-painted colour there after nothing
    // -- spell `RGB(7, 13, 22)` and comment it `// --bg-deep`. That
    // is a different colour: more blue, and lighter.
    //
    // So the token was dead and a near-miss literal was doing its
    // job, which meant 96.1 gave DEEP a designed night luminance of
    // 14 that no pixel has ever been. Those eighteen sites fell
    // through to the derived value of 12 instead.
    //
    // The token moves to the code rather than the code to the token.
    // The code is what has shipped and what the owner has looked at;
    // repainting eighteen surfaces a different colour to vindicate a
    // constant nobody used would be changing the application to
    // match its documentation. Moving the token changes no pixel --
    // nothing painted it -- and puts eighteen sites on the ladder.
    constexpr uint32_t DEEP      = 0xFF070D16;  // --bg-deep  (panel background)
    constexpr uint32_t PANEL     = 0xFF0A1520;  // --bg-panel (section headers, raised)
    constexpr uint32_t BORDER    = 0xFF0E1D2E;  // --bg-raised (subtle separators)
    constexpr uint32_t ACCENT    = 0xFF00E5FF;  // --cyan     (primary — camera, active)
    constexpr uint32_t ACCENT2   = 0xFFFFB900;  // --amber    (EAF, secondary)
    constexpr uint32_t ACCENT3   = 0xFF4DFF91;  // --green    (mount, live indicators)
    constexpr uint32_t ACCENT4   = 0xFFFF3B5C;  // --red      (capture, danger)
    constexpr uint32_t TEXT      = 0xFFC8E8F0;  // --white    (primary text)
    constexpr uint32_t TEXT_DIM  = 0xFF8CAFBE;  // --dim      (secondary labels)
    constexpr uint32_t RED       = 0xFFFF3B5C;  // --red
    constexpr uint32_t AMBER     = 0xFFFFB900;  // --amber
    constexpr uint32_t GREEN     = 0xFF4DFF91;  // --green

    // ── Font hierarchy ─────────────────────────────────────────
    //   FONT_BANNER : Space Mono — large readouts, title bar, HUD
    //   FONT_LABEL  : Barlow Condensed — all labels, section headers
    //   FONT_DATA   : Space Mono — numeric values, coordinates

    // ── Phase 96.4: the tokens, as COLORREF ─────────────────
    //
    // Why this exists. The operator chrome paints with GDI, which
    // wants a COLORREF -- 0x00BBGGRR, the reverse byte order of the
    // 0xAARRGGBB constants above -- so not one of those tokens can be
    // handed to `Theme::Brush` directly. Every site therefore spelled
    // its colour as an `RGB(...)` literal, and measured across the
    // nine operator surfaces: 156 painting sites, 82 of them matching
    // a ladder entry by coincidence of spelling and 74 of them in 46
    // distinct colours that do not. **Fifty-three per cent.**
    //
    // A near-miss is invisible in review and silent at run time,
    // because `NightLuminance` keys on the RGB bytes and quietly
    // falls through to the derived value for anything it does not
    // recognise. That is how `RGB(255, 60, 90)` -- one green unit and
    // two blue units from ACCENT4 -- came to paint eleven recording
    // and fault indicators at a night luminance of 103, which is the
    // exact figure phase 96.1 was written to fix.
    //
    // Named, that cannot happen: a typo is a compile error instead of
    // a colour that is 137 luminance units wrong in the dark.
    // tests/operator_palette.cpp asserts these agree with the
    // 0xAARRGGBB tokens byte for byte, so the two spellings cannot
    // drift.
    namespace Rgb {
        inline constexpr COLORREF FromArgb(uint32_t argb) {
            return RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
        }
        inline constexpr COLORREF BG_VOID  = FromArgb(0xFF04080F);
        inline constexpr COLORREF DEEP     = FromArgb(0xFF070D16);
        inline constexpr COLORREF PANEL    = FromArgb(0xFF0A1520);
        inline constexpr COLORREF BORDER   = FromArgb(0xFF0E1D2E);
        inline constexpr COLORREF ACCENT   = FromArgb(0xFF00E5FF);
        inline constexpr COLORREF ACCENT2  = FromArgb(0xFFFFB900);
        inline constexpr COLORREF ACCENT3  = FromArgb(0xFF4DFF91);
        inline constexpr COLORREF ACCENT4  = FromArgb(0xFFFF3B5C);
        inline constexpr COLORREF TEXT     = FromArgb(0xFFC8E8F0);
        inline constexpr COLORREF TEXT_DIM = FromArgb(0xFF8CAFBE);
        inline constexpr COLORREF RED      = ACCENT4;
        inline constexpr COLORREF AMBER    = ACCENT2;
        inline constexpr COLORREF GREEN    = ACCENT3;
    }

    constexpr const wchar_t* FONT_BANNER = L"Space Mono";
    constexpr const wchar_t* FONT_LABEL  = L"Barlow Condensed";
    constexpr const wchar_t* FONT_DATA   = L"Space Mono";

    // ════════════════════════════════════════════════════════
    //  Phase 96.2: the axes that survive the dark
    // ════════════════════════════════════════════════════════
    //
    // Night vision collapses colour to luminance, so hue can carry no
    // meaning (see the ladder below). What survives the transform is
    // everything geometric: size, spacing, depth. Those were the only
    // axes left to design, and until this phase none of them was
    // designed at all -- the palette was the whole system.
    //
    // ── What was there, measured ────────────────────────────
    //
    // SIZE. Sixteen font creations across six sizes: 11, 12, 13, 14,
    // 15, 16. Adjacent steps one pixel apart. A one-pixel difference
    // is not a hierarchy -- it is the same defect as cyan and amber
    // landing four luminance units apart, in a different axis. Worse,
    // the two painting stacks disagreed: Direct2D used 11, 13, 15
    // while GDI used all six.
    //
    // SPACING. Literals of 4, 6, 8, 10, 12 and 14 scattered across the
    // panels, each chosen locally. No rhythm, so nothing lines up
    // between surfaces that were written at different times.
    //
    // DEPTH. Nothing. No elevation, no shadow, no layering, which is
    // the single biggest thing the reference clients have and this
    // does not.
    //
    // ── These are definitions only ──────────────────────────
    //
    // Nothing here changes a pixel today, on purpose. Retrofitting the
    // call sites moves layout, and layout in this project is hard
    // pixel offsets that will need adjusting alongside. That is 96.3
    // and 96.4's work, surface by surface, where the result can be
    // looked at. Defining the system first is what stops those stages
    // being a series of local opinions.

    // ── Type ramp ───────────────────────────────────────────
    //
    // Five steps, each at least two pixels from its neighbour, which
    // is the minimum that reads as a different size at these sizes.
    // Growing increments, so the ramp separates more as it climbs and
    // a title cannot be mistaken for a heading.
    namespace Type {
        inline constexpr int MICRO  = 11;  // dense readouts, HUD corners
        inline constexpr int BODY   = 13;  // default label and value text
        inline constexpr int STRONG = 16;  // section headers, emphasis
        inline constexpr int TITLE  = 20;  // panel and window titles
        inline constexpr int HERO   = 28;  // audience surfaces only

        // ── Phase 96.3 added this step ──────────────────────
        //
        // 96.2 derived the ramp from the sizes the code was already
        // using, and every one of those was on an OPERATOR surface,
        // where nothing is a hero -- the largest thing a control
        // panel has is a section header. So the ramp it produced
        // spans 2.15:1 from body to top, which is a document ramp.
        //
        // The object panel has an actual display headline: the
        // object's name across the top of a screen a stranger is
        // reading from three metres back. Its own ratio, arrived at
        // by eye and working, is 3.63:1. Put on the 96.2 ramp
        // unchanged, that name would have shrunk by forty per cent.
        //
        // One of the two numbers had to be wrong and it was the
        // ramp's. A display surface wants a wider span than a
        // document; 2.15:1 is timid for a name meant to be read
        // across a room, and the surface that does that job had
        // already measured the answer. So the step is added and the
        // measurement is kept, rather than the measurement being
        // discarded to keep the ramp tidy.
        //
        // 47 rather than 48 because 47/BODY reproduces the object
        // panel's tuned title to within a third of a pixel at 1080p,
        // which is the whole reason for the number.
        //
        // Audience surfaces only, and more strictly than HERO: a
        // 47-pixel fixed size has no business anywhere in the
        // operator chrome.
        inline constexpr int DISPLAY = 47;

        // The smallest gap the ramp guarantees. Asserted by
        // tests/visual_tokens.cpp.
        inline constexpr int MIN_STEP = 2;

        // ── Phase 96.3: the same ramp, scaled ───────────────
        //
        // The audience surfaces cannot use the fixed sizes. A share
        // view is read from three metres back on whatever monitor is
        // pointed at the lawn, so its type has to scale with the
        // window -- 11 pixels on a 4K display across a garden is not
        // small type, it is absent type.
        //
        // They were already doing that, and that was the problem:
        // each one had invented its own multipliers (2.9, 0.95, 0.52,
        // 1.05, 0.80 in the object panel; 1.0 and 0.42 in the share
        // view), so the two surfaces built for the same room and the
        // same audience did not agree with each other on a single
        // size. Scaling the ramp rather than replacing it is what
        // makes them one system.
        //
        // Indexed, because a scaled step is computed rather than
        // named.
        inline constexpr int STEPS = 6;
        inline constexpr int kRamp[STEPS] = { MICRO, BODY, STRONG,
                                              TITLE, HERO, DISPLAY };
        enum : int { S_MICRO = 0, S_BODY, S_STRONG, S_TITLE, S_HERO,
                     S_DISPLAY };

        inline constexpr int Clamp(int step) {
            return step < 0 ? 0 : (step >= STEPS ? STEPS - 1 : step);
        }

        // A step as a multiple of BODY, which is the anchor because it
        // is the size most text is.
        inline constexpr float Ratio(int step) {
            return (float)kRamp[Clamp(step)] / (float)BODY;
        }

        // The ramp at an arbitrary body size.
        inline constexpr float Scaled(float bodyPx, int step) {
            return bodyPx * Ratio(step);
        }

        // The inverse, for a surface whose tuned size is NOT the body
        // one. The share view's large caption was sized by eye at
        // h/18 and reads correctly across a lawn, so that figure is
        // kept and the rest of the ramp is derived back from it
        // rather than the other way round -- the tuning was real and
        // throwing it away to start from a body size would be
        // replacing a measurement with a guess.
        inline constexpr float BodyForStep(float px, int step) {
            return px / Ratio(step);
        }
    }

    // ── Spacing ─────────────────────────────────────────────
    //
    // A 4-pixel base with a doubling-ish scale. Everything that pads,
    // insets or separates picks a step rather than a number, so
    // surfaces written months apart still line up.
    namespace Space {
        inline constexpr int BASE = 4;
        inline constexpr int XS   = 4;   // inside a control
        inline constexpr int SM   = 8;   // between related controls
        inline constexpr int MD   = 12;  // between groups
        inline constexpr int LG   = 16;  // panel inset
        inline constexpr int XL   = 24;  // between sections
        inline constexpr int XXL  = 32;  // around a focal element

        // ── Phase 96.3: the same rhythm, scaled ─────────────
        //
        // Fixed pixels are right for the operator chrome, which is a
        // known size on a known monitor at a known arm's length. They
        // are wrong for a surface whose type scales, because spacing
        // that does not scale with the type it separates stops being
        // rhythm and becomes a collision at one end and a gap at the
        // other.
        //
        // So the audience surfaces take the same steps expressed
        // against Type::BODY. At a body size of 13 these give back
        // exactly the fixed pixels, which is the point: one rhythm,
        // two expressions of it, and tests/audience_layout.cpp pins
        // the identity so they cannot drift apart.
        inline constexpr int STEPS = 6;
        inline constexpr int kSteps[STEPS] = { XS, SM, MD, LG, XL, XXL };
        enum : int { P_XS = 0, P_SM, P_MD, P_LG, P_XL, P_XXL };

        inline constexpr int Clamp(int step) {
            return step < 0 ? 0 : (step >= STEPS ? STEPS - 1 : step);
        }

        // The step as a multiple of body text, so a surface that
        // scales its type scales its rhythm with it.
        inline constexpr float Ems(int step) {
            return (float)kSteps[Clamp(step)] / (float)Type::BODY;
        }

        inline constexpr float Scaled(float bodyPx, int step) {
            return bodyPx * Ems(step);
        }
    }

    // ── Elevation ───────────────────────────────────────────
    //
    // Depth is the axis the reference clients lean on hardest and the
    // one this project had none of. It is also the axis night vision
    // cannot touch, because a shadow is alpha over whatever is beneath
    // it and NV() does not alter alpha.
    //
    // One definition per level, in one place. The alternative is what
    // happens otherwise: every surface guessing its own offset and
    // blur, and nothing looking like it belongs to the same object.
    struct Shadow {
        int   dy;       // vertical offset, pixels
        int   blur;     // blur radius, pixels
        float alpha;    // black, composited
    };

    namespace Elev {
        inline constexpr int FLAT    = 0;  // flush with its parent
        inline constexpr int RAISED  = 1;  // panels, cards
        inline constexpr int OVERLAY = 2;  // toasts, popups, tooltips
        inline constexpr int MODAL   = 3;  // dialogs over everything
        inline constexpr int LEVELS  = 4;
    }

    // Total and unique: every level has exactly one shadow, and an
    // out-of-range level clamps rather than reading past the array.
    inline constexpr Shadow ShadowFor(int level) {
        const Shadow table[Elev::LEVELS] = {
            { 0,  0, 0.00f },   // FLAT
            { 1,  3, 0.35f },   // RAISED
            { 3,  8, 0.45f },   // OVERLAY
            { 6, 18, 0.55f },   // MODAL
        };
        if (level < 0) level = 0;
        if (level >= Elev::LEVELS) level = Elev::LEVELS - 1;
        return table[level];
    }

    // ── Motion ────────────────────────────────────────────────
    //
    // Phase 96.5. Night vision flattens hue and compresses the
    // luminance ladder, but it cannot touch timing: a fade takes the
    // same 450 ms in a dark room as in a lit one. Motion is one of the
    // few channels that survives the red filter intact, so what little
    // of it this app has is worth getting right.
    //
    // DURATIONS. A half-decade ladder (x sqrt(10), so ~3.16 per rung)
    // anchored at 150 ms. The anchor is not a preference: the two
    // durations already in the app before this phase were ObjectPanel's
    // 450 ms target cross-fade and ShareView's 1500 ms hint ramp, and
    // those are rungs 1 and 2 of this ladder to within 5% (the true
    // rungs are 150 / 474 / 1500). 450 is kept over 474 because it is
    // the measured value and 24 ms is not perceptible.
    //
    // DWELLS are not motion. They are reading time, and they are listed
    // here only because they were three different numbers in three
    // files with nothing saying why. Now there are two, and the
    // difference is a decision: a confirmation tells you what you just
    // did, so you already know it; a notice tells you something you did
    // not know, so it gets longer.
    namespace Motion {
        inline constexpr int BASE_MS = 150;
        inline constexpr int STEPS   = 3;

        // Rung 0: a state edge becoming legible -- something took its
        //         pressed look, a transient surface arrived or left.
        inline constexpr int FAST    = 150;
        // Rung 1: a surface's content changing identity while the
        //         surface itself stays put. ObjectPanel's cross-fade.
        inline constexpr int CONTENT = 450;
        // Rung 2: a slow decay at the tail of a dwell, long enough that
        //         it reads as "this is leaving" and not as a glitch.
        inline constexpr int LONG    = 1500;

        inline constexpr int kRamp[STEPS] = { FAST, CONTENT, LONG };
        enum : int { D_FAST = 0, D_CONTENT, D_LONG };

        inline constexpr int StepAt(int step) {
            if (step < 0)      step = 0;
            if (step >= STEPS) step = STEPS - 1;
            return kRamp[step];
        }

        inline constexpr int DWELL_CONFIRM = 5000;   // you did this
        inline constexpr int DWELL_NOTICE  = 8000;   // you did not know this

        // ── Progress ──
        // Elapsed time as a fraction of a duration, clamped. A
        // zero or negative duration is complete at once rather than
        // dividing by zero: a caller that disables an animation by
        // zeroing its duration gets the end state, not a crash.
        inline constexpr float Progress(double elapsedMs, int durationMs) {
            if (durationMs <= 0)      return 1.0f;
            if (elapsedMs  <= 0.0)    return 0.0f;
            const double t = elapsedMs / (double)durationMs;
            return (t >= 1.0) ? 1.0f : (float)t;
        }

        // ── The fade curve ──
        //
        // Alpha compositing is linear in luminance; human lightness is
        // not. CIE L* goes as the cube root of luminance, so a fade run
        // on a linear alpha ramp does half of its *apparent* work in
        // the first quarter of its duration and then crawls. Measured
        // against this app's own tokens (see tests/motion.cpp):
        //
        //   night TEXT 200 over DEEP 14 : half-lightness at alpha 0.255
        //   day   TEXT over DEEP        : half-lightness at alpha 0.186
        //
        // The alpha ramp that makes L* rise linearly is a power curve,
        // and those two figures put its exponent at 1.97 and 2.42. Both
        // surfaces are near-black (the backdrop carries under 1% of the
        // foreground's luminance in both cases), which is why a plain
        // power law is adequate at all and no per-call backdrop
        // luminance is needed.
        //
        // So: a square. It is within 0.23 L* of ideal in the night case
        // -- below the ~1.0 L* just-noticeable difference, i.e. exact
        // for the condition this app is built for -- and 6.1 L* off in
        // the day case, where it errs bright, which for a fade-in means
        // arriving a shade early rather than lurking.
        //
        // The exponent is NOT 3. Alpha -> luminance -> L* suggests a
        // cube, but L* = 116*Y^(1/3) - 16 and night text only reaches
        // L* 42, so the -16 offset is a sixth of the range and pulls
        // the exponent back to roughly 2.
        inline constexpr float EaseIn(float t)  { return t * t; }

        // Fading in: 0 at the start, opaque at the end.
        inline constexpr float FadeIn(double elapsedMs, int durationMs) {
            return EaseIn(Progress(elapsedMs, durationMs));
        }

        // Fading out: opaque while time remains, 0 when it runs out.
        // The same curve read from the other end, so a surface that
        // fades in and out is symmetric.
        inline constexpr float FadeOut(double remainingMs, int durationMs) {
            return EaseIn(Progress(remainingMs, durationMs));
        }

        // Windows per-window opacity is a byte. Rounds rather than
        // truncating, so a fully opaque fade reaches 255 and not 254.
        inline constexpr unsigned char AlphaByte(float a) {
            if (a <= 0.0f) return 0;
            if (a >= 1.0f) return 255;
            return (unsigned char)(a * 255.0f + 0.5f);
        }

        // ── A transient surface's whole life ──
        //
        // Fade in over edgeMs, hold at full for dwellMs, fade out over
        // edgeMs. The dwell is reading time and sits between the two
        // edges rather than containing them, so raising a dwell never
        // shortens the fade that precedes it.
        //
        // alive goes false once the surface has finished leaving,
        // which is the signal to destroy it. One clock runs the whole
        // life: there is no separate dwell timer to disagree with the
        // fade, which is how the two could drift apart.
        inline constexpr float TransientAlpha(double elapsedMs,
                                              int dwellMs, int edgeMs,
                                              bool& alive) {
            alive = true;
            if (elapsedMs < (double)edgeMs)
                return FadeIn(elapsedMs, edgeMs);
            const double hold = (double)edgeMs + (double)dwellMs;
            if (elapsedMs < hold) return 1.0f;
            const double left = hold + (double)edgeMs - elapsedMs;
            if (left <= 0.0) { alive = false; return 0.0f; }
            return FadeOut(left, edgeMs);
        }

        // A fade needs frames. 60 Hz is one frame per 16.7 ms; this is
        // the repaint interval for a surface that is mid-transition,
        // and it is only ever armed while one is running.
        inline constexpr int FRAME_MS = 16;
    }

    // ── Helper: Convert 0xAARRGGBB to D2D1::ColorF ────────────

    inline D2D1_COLOR_F NVf(D2D1_COLOR_F c);   // defined below

    // The token's day channels, UNTINTED.
    //
    // For the surfaces that pass colours through their own brush
    // helper, which already applies NVf. Handing one of those a value
    // from Color() below would transform it twice, and NVf is not
    // idempotent -- a second pass takes the luminance of (lum,
    // lum/10, 0), which is 0.284 of the first, so the colour comes
    // out at roughly a quarter brightness and still looks plausible.
    // Exactly the kind of thing that ships.
    inline D2D1_COLOR_F Raw(uint32_t argb) {
        return D2D1::ColorF(((argb >> 16) & 0xFF) / 255.0f,
                            ((argb >> 8)  & 0xFF) / 255.0f,
                            ((argb)       & 0xFF) / 255.0f,
                            ((argb >> 24) & 0xFF) / 255.0f);
    }

    // The token, tinted for the current mode. For a call site that
    // paints with the result directly.
    inline D2D1_COLOR_F Color(uint32_t argb) {
        // Tinted here so every caller of the central D2D helper is
        // covered without touching the call site.
        return NVf(Raw(argb));
    }


    // ── Night vision (Phase 26) ───────────────────────────────
    //
    // Dark adaptation is destroyed by short wavelengths, so every colour
    // the UI paints is collapsed to its luminance and pushed into the
    // red channel -- the same transform the preview already applies to
    // the image, so the panel and the picture look like one thing.
    //
    // ── Phase 96.1: the hues converge, the LADDER does not ───
    //
    // This comment used to say the accents converging was inherent
    // rather than an oversight, on the grounds that keeping them apart
    // would mean reintroducing the short wavelengths night vision
    // exists to remove, or inventing a hue-to-brightness mapping that
    // means nothing.
    //
    // The first half is right and the second is wrong, and measuring
    // it showed why. Derived from luminance, cyan lands at 182 and
    // amber at 186: four units apart out of 255. Worse, the hierarchy
    // INVERTED. TEXT_DIM, for labels you skim past, derived to 169.
    // ACCENT4, for capture and danger, derived to 103. On a rig that
    // spends its working life in this mode, the colour that must
    // shout was sixty per cent dimmer than the one that must whisper.
    //
    // A hue-to-brightness mapping does mean nothing. But a
    // SEMANTIC-to-brightness mapping means a great deal, and it is the
    // only thing left to design once hue is gone. So each token
    // declares the luminance it should have in the dark, ordered by
    // how much it needs to be noticed, and the derived value is kept
    // only as the fallback for colours not in the ladder.
    //
    // No day colour changes. The ladder is consulted only when night
    // vision is on.
    //
    // The mode lives here, as process state, because 200-odd painting
    // calls across seventeen files need to see it and threading it
    // through every one of them would be worse than a global.
    namespace detail {
        inline bool  g_nightVision = false;
        // Bumped on every change. Cached GDI objects compare against it
        // and rebuild when it moves, which is the whole reason this is a
        // counter and not just the bool.
        inline unsigned g_paletteGen = 1;
    }

    inline bool NightVisionOn() { return detail::g_nightVision; }

    inline void SetNightVision(bool on) {
        if (detail::g_nightVision == on) return;
        detail::g_nightVision = on;
        detail::g_paletteGen++;
    }
    inline unsigned PaletteGeneration() { return detail::g_paletteGen; }

    // ── The night ladder ────────────────────────────────────
    //
    // Keyed on the day colour's RGB bytes, so both painting stacks
    // reach the same answer and NV() stays a pure function of its
    // argument -- which it must, because the GDI object cache keys on
    // the COLORREF and would otherwise hand back a brush in the wrong
    // shade.
    //
    // Ordered by how loudly a thing needs to speak, with roughly 30
    // units between neighbours, which is a separation the eye resolves
    // at these levels. The order, not the exact numbers, is the
    // design: danger above everything, then primary readouts, then the
    // device accents, then the labels you are meant to skim.
    namespace detail {
        struct NightStep { uint8_t r, g, b; int lum; };
        inline constexpr NightStep kNightLadder[] = {
            // day colour            night   role
            { 0xFF, 0x3B, 0x5C,      240 },  // RED / ACCENT4  capture, danger
            { 0xC8, 0xE8, 0xF0,      200 },  // TEXT           primary readouts
            { 0x4D, 0xFF, 0x91,      170 },  // GREEN/ACCENT3  mount, live
            { 0x00, 0xE5, 0xFF,      140 },  // ACCENT cyan    camera, active
            { 0xFF, 0xB9, 0x00,      110 },  // AMBER/ACCENT2  EAF, secondary
            { 0x8C, 0xAF, 0xBE,       80 },  // TEXT_DIM       skimmed labels
            { 0x0E, 0x1D, 0x2E,       34 },  // BORDER         separators
            { 0x0A, 0x15, 0x20,       24 },  // PANEL          raised surface
            { 0x07, 0x0D, 0x16,       14 },  // DEEP           panel background
            { 0x04, 0x08, 0x0F,        8 },  // BG_VOID        deepest surface
        };

        // The ladder has two bands and they want opposite things.
        //
        // SEMANTIC tokens -- danger, text, the device accents, the
        // skimmed labels -- exist to be told apart, so they need real
        // separation. 24 units is the floor, asserted by
        // tests/night_hierarchy.cpp so a later edit cannot quietly
        // reintroduce the four-unit collision this phase fixed.
        //
        // SURFACE tokens -- borders and the three backgrounds -- exist
        // to recede. They need ORDER, so a raised panel still reads as
        // raised, but holding them 24 apart would turn the window into
        // a staircase of greys. They are ordered and close, on purpose.
        inline constexpr int kMinNightSeparation = 24;

        // The first index in kNightLadder that is a surface rather
        // than a signal. Everything from here down is exempt from the
        // separation floor and subject only to ordering.
        inline constexpr int kFirstSurfaceStep = 6;   // BORDER onward
    }

    // Designed luminance for a day colour, or the derived one when the
    // colour is not a token. Unknown colours are rare and mostly
    // decorative, and deriving is the right answer for them: there is
    // no semantic to honour.
    inline int NightLuminance(int r, int g, int b) {
        for (const auto& s : detail::kNightLadder)
            if (s.r == r && s.g == g && s.b == b) return s.lum;
        return (int)(0.2126f * r + 0.7152f * g + 0.0722f * b);
    }

    // The tint itself. Pass every colour through this at the point of
    // use, never at the point of definition -- a value tinted once and
    // stored would keep whatever the mode was when it was stored.
    inline COLORREF NV(COLORREF c) {
        if (!detail::g_nightVision) return c;
        const int lum = NightLuminance(GetRValue(c), GetGValue(c), GetBValue(c));
        // A tenth of the luminance in green keeps near-black greys from
        // reading as pure black, which is what the preview does.
        return RGB(lum, lum / 10, 0);
    }

    // The Direct2D counterpart. Same transform, different type: the
    // preview, the status bar and the sky map all paint through D2D,
    // which takes floats rather than a packed COLORREF.
    inline D2D1_COLOR_F NVf(D2D1_COLOR_F c) {
        if (!detail::g_nightVision) return c;
        // Rounded back to bytes so the ladder lookup matches. The
        // token constants are exact eighths-of-a-bit values divided by
        // 255, so this round-trips without drift; anything else falls
        // through to the derived luminance, which is what it would
        // have had anyway.
        const int r = (int)(c.r * 255.0f + 0.5f);
        const int g = (int)(c.g * 255.0f + 0.5f);
        const int b = (int)(c.b * 255.0f + 0.5f);
        const float lum = (float)NightLuminance(r, g, b) / 255.0f;
        return D2D1::ColorF(lum, lum * 0.1f, 0.0f, c.a);
    }

    // ════════════════════════════════════════════════════════
    //  Phase 96.4: depth on a surface with no alpha
    // ════════════════════════════════════════════════════════
    //
    // 96.2 defined four elevation levels, each an offset, a blur and a
    // weight. 96.3 spent them on the two Direct2D surfaces, where a
    // weight is an alpha and a shadow is simply painted over whatever
    // is beneath it.
    //
    // The operator chrome paints with GDI, where `FillRect` is opaque
    // and there is no compositing without `AlphaBlend` and a source
    // DC. But it does not need one: the thing a shadow falls on here
    // is always a palette token, so the composite is known in advance
    // and can be emitted as a solid colour.
    //
    // ── The part that is easy to get wrong ──────────────────
    //
    // The order of the blend and the night transform is not
    // interchangeable, and getting it backwards compiles and looks
    // plausible.
    //
    // `NV()` is a TABLE for the tokens and linear only for everything
    // else. A blended colour is in no table, so blending first and
    // tinting after gives (1 - alpha) times the token's DERIVED
    // luminance -- while the surface the shadow sits on is painted at
    // the token's DESIGNED one. For PANEL those are 19 and 24, so a
    // 35% shadow would come out at 12 against a surface at 24: a
    // ratio of 0.50 where 0.65 was asked for.
    //
    // So the blend happens on the far side of the transform, in
    // whichever space is currently active. Then the ratio a level
    // states is the ratio that appears, in both modes.
    // tests/operator_palette.cpp pins exactly that.

    // Linear mix of two colours ALREADY in the active space. Both
    // arguments must have been through NV(), or the result mixes a
    // day colour into a night one.
    inline COLORREF Mix(COLORREF a, COLORREF b, float t) {
        if (t <= 0.0f) return a;
        if (t >= 1.0f) return b;
        auto m = [&](int x, int y) {
            return (int)((float)x + ((float)y - (float)x) * t + 0.5f);
        };
        return RGB(m(GetRValue(a), GetRValue(b)),
                   m(GetGValue(a), GetGValue(b)),
                   m(GetBValue(a), GetBValue(b)));
    }

    // A shadow of the given weight falling on `parent`. Takes the DAY
    // token and does the tinting itself, so a call site cannot get
    // the order wrong.
    //
    // Black is luminance zero in both spaces, which is why this one
    // needs no special case for night vision: darkening is darkening.
    inline COLORREF ShadowOver(COLORREF dayParent, float alpha) {
        return Mix(NV(dayParent), RGB(0, 0, 0), alpha);
    }

    // The lit top edge of a raised surface, the other half of how the
    // reference clients read as layered.
    //
    // NOT a blend toward white. In night vision `dayParent` has
    // already been collapsed to (lum, lum/10, 0), and mixing that
    // toward RGB(255,255,255) at even a third puts 89 of blue back
    // into a display whose entire purpose is to have none -- which is
    // what the mount controller's state-light highlight was doing,
    // found when this function was written to replace it.
    //
    // The first draft of the replacement scaled each channel TOWARD
    // 255, which is the same mistake with a different arithmetic: a
    // blue channel at 0 came out at 45, and tests/operator_palette.cpp
    // caught it. Pulling toward a bound you share with white IS
    // blending toward white, at half weight.
    //
    // Multiplying is the operation that actually preserves hue,
    // because it preserves the RATIOS between the channels -- and
    // zero times anything is zero, so a channel night vision has
    // emptied stays empty.
    //
    // Half weight, because a highlight matching its shadow in
    // strength reads as a bevel rather than as depth, and a bevel is
    // a different decade's design language.
    //
    // The argument is the DAY colour, like ShadowOver's: both tint
    // internally so a call site cannot get the order wrong, which
    // means neither may be handed an already-tinted value.
    inline COLORREF HighlightOver(COLORREF dayParent, float alpha) {
        const COLORREF p = NV(dayParent);
        if (alpha <= 0.0f) return p;
        const float a = (alpha >= 1.0f) ? 1.0f : alpha;
        const float k = 1.0f + a * 0.5f;
        auto up = [&](int x) {
            const int v = (int)((float)x * k + 0.5f);
            return v > 255 ? 255 : v;
        };
        return RGB(up(GetRValue(p)), up(GetGValue(p)), up(GetBValue(p)));
    }

    // The shadow colour for an elevation level, on a known parent.
    // One lookup, so a surface asks for RAISED rather than guessing a
    // weight, and the four levels keep the order 96.2 gave them.
    inline COLORREF ShadowForLevel(COLORREF dayParent, int level) {
        return ShadowOver(dayParent, ShadowFor(level).alpha);
    }

    // ── Elevation inside your own rect ──────────────────────
    //
    // The operator chrome cannot cast a shadow, and the reason is
    // structural rather than a matter of effort.
    //
    // The control panel is built from child controls under
    // WS_CLIPCHILDREN, so the parent's paint never touches a child's
    // pixels; and every collapse toggle runs RebuildControls, which
    // destroys and recreates all of them. A cast shadow would mean the
    // parent tracking every child's rect and repainting the bands
    // between them on each scroll and each rebuild.
    //
    // What an owner-drawn child CAN do is raise itself inside the rect
    // it already owns: one lit line along its top edge and a short
    // shade along its bottom. That reads as a raised plate, it needs
    // no knowledge of anything outside the control, and it survives a
    // rebuild because it is part of the control's own drawing.
    //
    // Both edges are drawn INSIDE the rect, so nothing moves and no
    // caller has to leave room.
    inline void RaiseInRect(HDC hdc, const RECT& r, COLORREF dayFace,
                            int level = 1) {
        if (!hdc || r.right <= r.left || r.bottom <= r.top) return;
        const Shadow sh = ShadowFor(level);
        if (sh.alpha <= 0.0f) return;

        // The lit top edge, one pixel, weaker than the shade below it:
        // matched strengths read as a bevel, which is a different
        // decade's design language.
        HBRUSH top = CreateSolidBrush(HighlightOver(dayFace, sh.alpha));
        RECT tr = { r.left, r.top, r.right, r.top + 1 };
        FillRect(hdc, &tr, top);
        DeleteObject(top);

        // The shade, as rows fading toward the face. Bounded to a
        // third of the control's height, so this is still depth on a
        // short button rather than a gradient filling it.
        int rows = sh.blur / 2;
        if (rows < 1) rows = 1;
        const int maxRows = (r.bottom - r.top) / 3;
        if (rows > maxRows) rows = maxRows;
        for (int i = 0; i < rows; ++i) {
            // Darkest at the very bottom edge, fading UP into the
            // face. The first draft had (rows - i), which put the
            // darkest row at the top of the band and the lightest on
            // the edge itself -- a plate lit from above, shaded as if
            // the light came from below.
            const float w = sh.alpha * (float)(i + 1) / (float)rows;
            HBRUSH b = CreateSolidBrush(ShadowOver(dayFace, w));
            RECT br = { r.left, r.bottom - rows + i,
                        r.right, r.bottom - rows + i + 1 };
            FillRect(hdc, &br, b);
            DeleteObject(b);
        }
    }

    // ── Mode-aware GDI object cache ───────────────────────────
    //
    // This exists because of a specific failure, not for speed. The
    // painting code was full of
    //
    //     static HBRUSH bg = CreateSolidBrush(RGB(10, 21, 32));
    //
    // which creates the brush on first paint and keeps it forever.
    // Wrapping the colour in NV() there would have captured whichever
    // mode was active at that first paint, so toggling night vision
    // afterwards would leave those surfaces unchanged -- and it would
    // have compiled, looked right on a first toggle, and shown up as
    // half a panel refusing to change colour. Thirty-six of them.
    //
    // These helpers cache per colour AND per palette generation, so the
    // objects survive repainting and are rebuilt exactly once when the
    // mode changes. Entries are not freed on invalidation while a
    // caller might still hold the handle for the current paint; the
    // cache is bounded by the number of distinct colours the UI uses
    // (a few dozen) and is created once per process, so it does not
    // grow without limit.
    //
    // UI THREAD ONLY. Every caller is a WM_PAINT or WM_CTLCOLOR*
    // handler, which Win32 delivers on the thread that owns the window.
    namespace detail {
        struct GdiCache {
            struct Entry { COLORREF key; unsigned gen; int w; HGDIOBJ obj; };
            std::vector<Entry> brushes, pens;
        };
        inline GdiCache& Cache() { static GdiCache c; return c; }
    }

    // A brush in the current palette. Do NOT DeleteObject the result.
    inline HBRUSH Brush(COLORREF c) {
        auto& v = detail::Cache().brushes;
        const unsigned gen = detail::g_paletteGen;
        for (auto& e : v)
            if (e.key == c && e.gen == gen) return (HBRUSH)e.obj;
        HBRUSH b = CreateSolidBrush(NV(c));
        v.push_back({ c, gen, 0, (HGDIOBJ)b });
        return b;
    }

    // A pen in the current palette. Do NOT DeleteObject the result.
    inline HPEN Pen(COLORREF c, int width = 1) {
        auto& v = detail::Cache().pens;
        const unsigned gen = detail::g_paletteGen;
        for (auto& e : v)
            if (e.key == c && e.gen == gen && e.w == width) return (HPEN)e.obj;
        HPEN p = CreatePen(PS_SOLID, width, NV(c));
        v.push_back({ c, gen, width, (HGDIOBJ)p });
        return p;
    }

    // ── D2: the chrome Windows draws for us ───────────────────
    //
    // Every colour the app paints itself goes through NV(). What stays
    // bright with night vision on is what the OS draws: title bars,
    // list-view backgrounds, tab strips, the inside of edit boxes.
    // Measured when this was filed: the GoTo panel came out 98%
    // non-red, almost all of it the object catalogue's white list.
    //
    // The expensive half of the original fix -- subclassing and
    // painting every push button, because a de-themed classic button
    // takes its face from COLOR_BTNFACE and ignores the brush
    // WM_CTLCOLORBTN hands back -- stays out. These are the cheap
    // parts, and they cover the large areas.

    // Dark title bar. Attribute 20 is DWMWA_USE_IMMERSIVE_DARK_MODE;
    // 19 is what it was called before 20H1, and setting both covers
    // either. Not night-vision dependent: this app is dark whatever
    // mode it is in, and a white title bar was never right.
    inline void DarkTitleBar(HWND h) {
        if (!h) return;
        BOOL dark = TRUE;
        DwmSetWindowAttribute(h, 20, &dark, sizeof(dark));
        DwmSetWindowAttribute(h, 19, &dark, sizeof(dark));
    }

    // Re-tint the stock controls under a window, following the current
    // mode. Call again on a night-vision toggle: unlike the title bar,
    // these colours DO change with it.
    //
    // Found by class rather than by a list of handles, for the same
    // reason D3 gave up its list -- a control added later should not
    // have to be registered somewhere to be coloured.
    inline void StyleStockControls(HWND top) {
        if (!top) return;
        EnumChildWindows(top, [](HWND c, LPARAM) -> BOOL {
            wchar_t cls[64] = {};
            if (GetClassNameW(c, cls, 64) <= 0) return TRUE;

            if (_wcsicmp(cls, WC_LISTVIEWW) == 0) {
                const COLORREF bg = NV(RGB(10, 18, 28));
                const COLORREF fg = NV(RGB(190, 214, 230));
                ListView_SetBkColor(c, bg);
                ListView_SetTextBkColor(c, bg);
                ListView_SetTextColor(c, fg);
                // The header is deliberately left alone. It is a
                // separate control that takes its face from
                // COLOR_BTNFACE once de-themed, so stripping its theme
                // trades a themed grey band for a classic grey one and
                // changes how it looks in DAY mode for no night-vision
                // gain. Colouring it properly means NM_CUSTOMDRAW,
                // which is the same custom-draw work this entry
                // deferred for the buttons.
                SetWindowTheme(c, L"", L"");
                InvalidateRect(c, nullptr, TRUE);
            } else if (_wcsicmp(cls, L"Edit") == 0 ||
                       _wcsicmp(cls, L"ComboBox") == 0) {
                // Tab controls are not in this list for the same
                // reason as the header: de-theming one makes it
                // classic grey rather than dark.
                // De-theme so the app's WM_CTLCOLOR* brushes are what
                // actually gets used. The control panel has always done
                // this; the other windows never did.
                SetWindowTheme(c, L"", L"");
                InvalidateRect(c, nullptr, TRUE);
            }
            return TRUE;
        }, 0);
    }

} // namespace Theme
} // namespace raduk::gui
