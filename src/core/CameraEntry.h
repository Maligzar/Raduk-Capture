// ============================================================
//  CameraEntry -- what a typed camera value means
//
//  Phase 112, from the owner's report: every adjustment should take a
//  typed value the way exposure does. Phase 98 gave exposure a box;
//  gain, gamma, white balance and USB stayed read-only labels.
//
//  One rule for all six boxes, pure so a test can reach it:
//
//    - a number inside the range is that number
//    - a number outside it clamps, and the box shows the clamp
//    - anything else is not a value, so the box goes back to what the
//      camera is doing
//
//  Trailing text is rejected rather than ignored, apart from the units
//  and marks the boxes themselves show. wcstod stops at the first
//  character it cannot use, so "1,5" would otherwise be 1, and a typo
//  would reach the camera looking like a decision.
// ============================================================

#pragma once

#include <cmath>
#include <cwchar>

namespace raduk::core {

struct CameraEntry {
    bool   parsed  = false;  // false: leave the camera alone
    bool   clamped = false;  // the value is not what was typed
    double value   = 0.0;
};

inline CameraEntry ParseCameraEntry(const wchar_t* text, double lo, double hi) {
    CameraEntry e;
    if (!text) return e;

    wchar_t* end = nullptr;
    const double v = std::wcstod(text, &end);
    if (end == text || !std::isfinite(v)) return e;

    // "+" is the mark the exposure box carries when the camera is past
    // the slider's end, so a box read back has to parse.
    while (*end == L' ') ++end;
    if (*end == L'+' || *end == L'%') ++end;
    else if (end[0] == L'm' && end[1] == L's') end += 2;
    while (*end == L' ') ++end;
    if (*end != L'\0') return e;

    e.parsed  = true;
    e.value   = v;
    if (v < lo) { e.value = lo; e.clamped = true; }
    if (v > hi) { e.value = hi; e.clamped = true; }
    return e;
}

} // namespace raduk::core
