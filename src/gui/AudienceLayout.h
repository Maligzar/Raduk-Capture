// ============================================================
//  AudienceLayout — the geometry the audience surfaces share
//
//  Phase 96.3. The share view and the object panel are the two
//  surfaces built to be looked at by other people rather than worked
//  by the owner, and phase 92 wrote the rule down: "the owner works
//  on display 1 and everything else is for other people."
//
//  96.2 defined the axes night vision cannot flatten -- size,
//  spacing, depth -- and deliberately changed nothing. This stage is
//  where two surfaces actually adopt them, and the arithmetic of
//  doing so lives here rather than in either window for the reason
//  PixelWCS.h and StarMetrics.h live where they do: a fit, an inset
//  and a nest of rectangles are exactly the kind of thing that looks
//  obviously right and is off by one, and neither ShareView.h nor
//  ObjectPanel.h can be reached by a test because both need a window.
//
//  WHAT THE REFERENCE MATERIAL ACTUALLY GIVES US
//
//  The owner's League-client screenshot is, very roughly, forty per
//  cent colour -- the gold ring, the teal, the warm-on-cool contrast
//  that makes the frame read as rich. None of that survives here:
//  night vision collapses every painted colour into luminance, so
//  copying it faithfully yields a uniform red smear. What survives is
//  the other sixty per cent, and it is the part built from geometry:
//
//    - depth, because a shadow is alpha over whatever is beneath it
//      and NV() does not touch alpha;
//    - the bracketed frame, because a silhouette is a shape;
//    - a type ramp, because size is size in any colour.
//
//  So those three are what this header provides, and nothing else.
//
//  AND WHAT IT MUST NOT GIVE US
//
//  ObjectPanel.h's own opening comment forbids ornament that carries
//  no information, on the grounds that a decorative display teaches
//  people the screen is decoration and they stop reading it. That
//  rule binds this file too, which is why there is no sweeping arc
//  and no bracket that is merely a bracket. The corner marks exist
//  because a letterboxed image against a black field gives a
//  bystander no way to see where the sensor's frame ends, and the
//  scrim exists because white text at 92% over Jupiter's limb is
//  genuinely hard to read. Both are doing work.
// ============================================================

#pragma once

#include "Theme.h"

#include <algorithm>

namespace raduk::gui {
namespace audience {

// A rectangle in the layout arithmetic. Deliberately not
// D2D1_RECT_F: this is the only part of the audience surfaces a test
// can reach, and the moment it needs a Direct2D type it stops being
// reachable. The field order is the same, so the call sites convert
// in one line.
struct LRect {
    float l = 0.0f, t = 0.0f, r = 0.0f, b = 0.0f;
};

inline float Width (const LRect& k) { return k.r - k.l; }
inline float Height(const LRect& k) { return k.b - k.t; }
inline float ShortEdge(const LRect& k) {
    return (std::min)(Width(k), Height(k));
}
inline LRect Inset(const LRect& k, float d) {
    return LRect{ k.l + d, k.t + d, k.r - d, k.b - d };
}

// ── The image, letterboxed ────────────────────────────────────
//
// Aspect-preserved and centred, never stretched. A planet with the
// wrong aspect ratio is not a planet and nobody in the audience knows
// to discount it.
//
// It DOES scale up as well as down: the capture ROI is routinely a
// 640x480 crop of the sensor and the screen it is being shown on is
// not, so refusing to enlarge would put a postage stamp on a wall.
//
// Lifted out of ShareView::FitRect unchanged in behaviour. It is here
// because everything else in this file is positioned relative to it,
// so a test that cannot see this one is testing against a guess.
inline LRect LetterboxFit(float viewW, float viewH,
                          float imgW,  float imgH) {
    if (imgW <= 0.0f || imgH <= 0.0f || viewW <= 0.0f || viewH <= 0.0f)
        return LRect{ 0.0f, 0.0f, viewW, viewH };
    const float s  = (std::min)(viewW / imgW, viewH / imgH);
    const float dw = imgW * s, dh = imgH * s;
    const float ox = (viewW - dw) * 0.5f, oy = (viewH - dh) * 0.5f;
    return LRect{ ox, oy, ox + dw, oy + dh };
}

// ── Corner brackets ───────────────────────────────────────────
//
// Four L-shaped marks at the corners of the image, which is the one
// piece of the reference clients' frame language that both survives
// night vision and earns its place here.
//
// It earns it because of the letterbox. The image is centred in a
// black field and the sky at the edge of a planetary frame is also
// black, so from three metres back there is no visible boundary --
// a bystander cannot tell whether they are looking at the whole
// frame or a crop of it. The brackets answer that, and they are the
// shape language of a viewfinder, which is what this window is.
//
// Arm length, or zero when the image is too small to bracket. The
// clamp is the whole point: an arm is a fraction of an em at a tuned
// size, but on a narrow ROI -- a 200-pixel-wide planet crop stretched
// across a monitor is a tall thin rectangle -- two unclamped arms on
// the short edge meet in the middle and the bracket becomes a box.
inline float BracketArm(const LRect& image, float bodyPx) {
    const float edge = ShortEdge(image);

    // The gate and the clamp answer different questions and the first
    // draft conflated them.
    //
    // The gate asks whether marks read as corners at all. Below some
    // size four of them around a thumbnail is noise, not a frame.
    //
    // The clamp asks whether two arms on the same edge can meet.
    //
    // It was gated at eight body sizes, which got both wrong at once.
    // A tall thin planetary crop letterboxes to a strip a couple of
    // hundred pixels wide -- the test's 200x1000 ROI comes out
    // 216x1080 on a 1080p screen -- and that is the case where a
    // bystander most needs telling where the frame ends, because the
    // strip is the one thing on screen that does not look like a
    // sensor frame. Eight body sizes refused to mark it. And since
    // 1.6 is a fifth of 8, the clamp below could never fire: it was
    // unreachable code standing in for the invariant while the gate
    // silently enforced it.
    //
    // Three, so a bracketed edge is at least three arms across, and
    // the clamp is live for the band between three and four where the
    // fixed arm would be too long for the edge it is marking.
    if (bodyPx <= 0.0f || edge < bodyPx * 3.0f) return 0.0f;
    const float want = bodyPx * 1.6f;
    // Never more than 40% of the short edge, so the two arms on it
    // always leave a clear fifth of it between them.
    return (std::min)(want, edge * 0.40f);
}

// ── The caption scrim ─────────────────────────────────────────
//
// Depth, and the reason to lead with it rather than with colour.
//
// The caption is white at 92% drawn straight onto the image. On a
// letterboxed Jupiter that is white on black and perfectly readable;
// on a full-frame Moon, or any frame where the bright limb reaches
// the bottom of the sensor, it is white on white. A gradient scrim
// behind it fixes that, and because a scrim is alpha over whatever is
// beneath it, it reads identically with night vision on -- which is
// the property that made depth the right axis to build the look from.
//
// Returns the band to fill. The gradient runs dark at the bottom edge
// to clear at the top, so the image is untouched above it.
inline LRect CaptionScrim(float viewW, float viewH,
                          float captionTop, float bodyPx) {
    if (viewW <= 0.0f || viewH <= 0.0f) return LRect{};
    // Start the fade a clear section above the text, so the top of
    // the first glyph is already sitting on something.
    float top = captionTop - Theme::Space::Scaled(bodyPx, Theme::Space::P_XL);
    if (top < 0.0f) top = 0.0f;
    if (top > viewH) top = viewH;
    return LRect{ 0.0f, top, viewW, viewH };
}

// How dark the scrim gets at the very bottom. Enough to carry white
// text over a bright limb, not enough to look like a black bar: the
// image is still the content and the caption is still a caption.
inline constexpr float SCRIM_PEAK_ALPHA = 0.72f;

// ── Elevation, with no blur to do it with ─────────────────────
//
// Theme::ShadowFor gives an offset, a blur radius and a weight.
// ID2D1HwndRenderTarget has no blur: that lives on ID2D1DeviceContext
// as an effect, and neither audience window has one -- both are plain
// hwnd targets, and rebuilding them onto the Direct2D 1.1 pipeline to
// soften a card edge would be a disproportionate change to windows
// that already work.
//
// So the blur is approximated by a few nested rings of flat fill,
// darkest and tightest innermost. Three is enough to read as soft at
// these radii and cheap enough to paint on a surface that redraws on
// a target change rather than per frame.
//
// The ring weights sum to exactly the level's stated alpha, so the
// ordering Theme defines -- FLAT lighter than RAISED lighter than
// OVERLAY lighter than MODAL -- survives the approximation rather
// than being quietly renegotiated here.
inline constexpr int SHADOW_RINGS = 3;

struct Ring {
    LRect rect;
    float alpha = 0.0f;
};

// True when the level paints anything at all. FLAT does not, and a
// caller that skips the check paints a zero-alpha rectangle for every
// flush element on the surface.
inline bool ShadowVisible(const Theme::Shadow& s) {
    return s.alpha > 0.0f && (s.blur > 0 || s.dy > 0);
}

// Ring i, outermost first. i == 0 is the widest and faintest.
inline Ring ShadowRingAt(const LRect& card, const Theme::Shadow& s, int i) {
    if (i < 0) i = 0;
    if (i >= SHADOW_RINGS) i = SHADOW_RINGS - 1;

    // Spread shrinks towards the card as the rings get darker.
    const float spread = (float)s.blur *
                         (float)(SHADOW_RINGS - i) / (float)SHADOW_RINGS;

    // Weights 1:2:3 over a denominator of 6, so the three sum to the
    // level's alpha exactly.
    const float denom = (float)(SHADOW_RINGS * (SHADOW_RINGS + 1) / 2);
    const float w     = (float)(i + 1) / denom;

    Ring out;
    out.rect  = LRect{ card.l - spread, card.t - spread + (float)s.dy,
                       card.r + spread, card.b + spread + (float)s.dy };
    out.alpha = s.alpha * w;
    return out;
}

// ── A row of equal cards ──────────────────────────────────────
//
// The object panel's figures -- MAGNITUDE, APPARENT SIZE, ALTITUDE
// NOW, AIR MASSES -- were four bare label-and-value pairs laid out by
// dividing the column width and subtracting eight pixels. Four of
// them side by side with nothing between them leaves which value
// belongs to which label to the reader's eye, which on a screen being
// read by a stranger from across a room is a real failure and not a
// refinement.
//
// Cards fix it by being objects: each figure gets a surface, and the
// surfaces are separated by the shared rhythm rather than by a
// literal.
//
// The count is whatever the data supports and is never fixed -- an
// absent fact is not a blank card with a heading over it -- so this
// has to be right for one card and for five.
inline LRect CardInRow(const LRect& row, int count, int index,
                       float bodyPx) {
    if (count <= 0) return LRect{};
    if (index < 0) index = 0;
    if (index >= count) index = count - 1;

    const float gut   = Theme::Space::Scaled(bodyPx, Theme::Space::P_SM);
    const float total = Width(row);
    // With one card there is no gutter, with n cards there are n-1.
    const float cw    = (total - gut * (float)(count - 1)) / (float)count;
    if (cw <= 0.0f) return LRect{ row.l, row.t, row.l, row.b };

    const float x = row.l + ((float)index) * (cw + gut);
    return LRect{ x, row.t, x + cw, row.b };
}

// Corner radius for a card, on the same ramp as everything else so a
// card on one audience surface is the same object as a card on the
// other.
inline float CardRadius(float bodyPx) {
    return Theme::Space::Scaled(bodyPx, Theme::Space::P_XS);
}

} // namespace audience
} // namespace raduk::gui
