// ============================================================
//  ExposureScale — the exposure slider's mapping
//
//  Phase 98, from the owner's report: "the camera sliders are
//  difficult to interact with."
//
//  ── What was wrong, measured ───────────────────────────────
//
//  The exposure slider was LINEAR over 1 to 60,000 microseconds, on
//  425 pixels of travel. That is 141 us per pixel, and the
//  consequences are not subtle:
//
//    the Moon, 1-5 ms      28 px of 425 --  6.7% of the travel
//    Jupiter,  5-20 ms    106 px        -- 25%
//    Saturn,  20-60 ms    283 px        -- 67%
//
//  Two thirds of the control was spent on the longest exposures and
//  the short end was a few pixels wide. 3.00 ms could not be set at
//  all: the nearest reachable positions were 2.966 and 3.107.
//
//  ── Why logarithmic is the answer and not a preference ─────
//
//  Exposure is judged in STOPS. Going from 2 ms to 4 ms matters
//  exactly as much as going from 20 ms to 40 ms -- both double the
//  light -- and a linear scale gives the second one ten times the
//  travel of the first. A logarithmic scale gives every doubling the
//  same distance, which is what the eye and the histogram both care
//  about.
//
//  It also makes the precision uniform: ~2.6% per step across the
//  whole range rather than 141 us everywhere, so the error is
//  proportional to the value, which is the only sensible thing for a
//  quantity spanning five decades.
//
//  Pure arithmetic in a header a test can reach, because a mapping
//  that is off at one end still looks completely reasonable in the
//  middle, and the endpoints are exactly where an off-by-one hides.
// ============================================================

#pragma once

#include <cmath>

namespace raduk::core {

// The range the slider covers. Longer exposures are set in the
// profile editor; this is the lucky-imaging band plus headroom.
inline constexpr double EXP_MIN_US = 100.0;      // 0.1 ms
inline constexpr double EXP_MAX_US = 60000.0;    // 60 ms

// Slider positions. 256 steps over a 600:1 range is 2.57% per step,
// which is about a twenty-fifth of a stop -- finer than any camera's
// own exposure quantisation matters at these lengths, and coarse
// enough that dragging the full width is 256 values rather than 425.
inline constexpr int EXP_SLIDER_STEPS = 256;

// ── The mapping ─────────────────────────────────────────────
//
// position 0 is EXP_MIN_US and EXP_SLIDER_STEPS is EXP_MAX_US, with
// the decades spread evenly between. Both ends are exact, which a
// test checks: an exposure scale whose maximum is 59,994 us because
// of a rounding error is a scale that cannot reach its own documented
// ceiling.
inline double ExposureUsAt(int pos) {
    if (pos <= 0) return EXP_MIN_US;
    if (pos >= EXP_SLIDER_STEPS) return EXP_MAX_US;
    const double t = (double)pos / (double)EXP_SLIDER_STEPS;
    return EXP_MIN_US * std::pow(EXP_MAX_US / EXP_MIN_US, t);
}

// The inverse, rounded to the nearest step. Used when the camera, a
// profile or the auto-exposure assistant hands us a value and the
// thumb has to go somewhere sensible.
inline int ExposurePosFor(double us) {
    if (us <= EXP_MIN_US) return 0;
    if (us >= EXP_MAX_US) return EXP_SLIDER_STEPS;
    const double t = std::log(us / EXP_MIN_US) /
                     std::log(EXP_MAX_US / EXP_MIN_US);
    return (int)(t * EXP_SLIDER_STEPS + 0.5);
}

// True when the value is outside what the slider can show, so the
// readout can mark itself rather than sit pegged and lying. The DSO
// profile asks for 30 SECONDS; the slider stops at 60 ms.
inline bool ExposureOutOfRange(double us) {
    return us < EXP_MIN_US || us > EXP_MAX_US;
}

// One arrow-key press, in steps. The slider never set a line size at
// all, so the default of 1 applied -- on the old linear scale that
// was ONE MICROSECOND, or sixty thousand presses to cross the range.
// One step here is 2.6%, which is a usable nudge.
inline constexpr int EXP_LINE_STEPS = 1;
// One page press. A sixth of the range was 12 ms on the old scale,
// with nothing in between it and 1 us. 16 steps is about half a stop.
inline constexpr int EXP_PAGE_STEPS = 16;

// How often a drag may write to the camera.
//
// The old handler committed only on release, so the preview could not
// be judged until the thumb was let go -- guess, release, look,
// re-grab, on a slider where one pixel was 141 us. Committing on
// every drag message instead would be 50 to 100 USB control transfers
// a second competing with the frame stream, which on this rig is a
// bandwidth the USB manager exists to protect.
//
// Eight a second is enough to see the preview respond and cheap
// enough to be safe.
inline constexpr double EXP_LIVE_APPLY_MS = 120.0;

} // namespace raduk::core
