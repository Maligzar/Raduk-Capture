#pragma once
// ============================================================
//  DisplayStretch.h -- phase 140.3: a view bright enough for the eye
//
//  Short exposures at the HCG gain floor (140.2) give a planet that is
//  correctly exposed for the data and dim on the screen: its bright side
//  may sit at a third of full scale. The fix belongs to the DISPLAY,
//  never to the camera: the preview's stretch (phase 37) is a lookup
//  table on the shown pixels only, and the histogram, the analyser and
//  the recording all see raw data.
//
//  This chooses that stretch from the target meter (140.1): the black
//  point just above the sky, the white point so the target's bright
//  side shows at about 90% of the screen's range. It never darkens a
//  view (a target already bright is left at full range) and never
//  amplifies more than 8 times, so a faint target does not become a
//  field of noise.
//
//  Pure: tests/display_stretch.cpp gates it.
// ============================================================

#include <algorithm>
#include <cmath>

namespace raduk::core {

struct StretchPoints {
    bool apply = false;   // false: leave the stretch as it is
    int  black = 0, white = 255;
};

// skyDN and highlightDN are the target meter's sky median and target
// highlight, 0-255 raw. showAt is where the highlight should appear on
// screen; maxGain the most the view is ever amplified.
inline StretchPoints AutoStretchFor(bool targetFound, float skyDN, float highlightDN,
                                    float showAt = 230.0f, float maxGain = 8.0f) {
    StretchPoints s;
    if (!targetFound || highlightDN <= skyDN) return s;
    const int black = std::clamp((int)std::lround(skyDN) + 2, 0, 200);
    // out = (v - black) * 255 / (white - black); the highlight lands on showAt.
    double white = black + (highlightDN - black) * 255.0 / showAt;
    white = (std::max)(white, black + 255.0 / maxGain);   // at most maxGain times
    s.white = (int)std::lround((std::min)(white, 255.0));
    s.black = black;
    if (s.white < s.black + 4) s.white = (std::min)(255, s.black + 4);
    s.apply = true;
    return s;
}

}  // namespace raduk::core
