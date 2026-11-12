#pragma once
// ============================================================
//  AfReadiness.h -- phase 139.3: can autofocus work on what is in the
//  frame right now?
//
//  Autofocus has limits, and running it outside them costs a minute and
//  ends somewhere worse: a target too small to measure, one filling the
//  frame (the half-flux diameter needs a disc with sky around it), a
//  clipped or faint one, one near the edge that the mirror's shift will
//  push out during the sweep, a mount that is not tracking, a target
//  fading, or seeing too unsteady for a curve.
//
//  One answer, READY or NOT NOW with the first reason that applies,
//  shown on the AUTO FOCUS button and its status line. Pressing it when
//  NOT NOW asks for a second press rather than refusing: the owner may
//  know better.
//
//  Every input is something the app already measures. A value it does
//  not have is not held against it.
//
//  Pure: tests/af_readiness.cpp, one case per reason.
// ============================================================

#include <string>

namespace raduk::core {

struct AfReadyIn {
    bool  focuserOpen   = false;
    bool  focuserMoving = false;
    bool  afRunning     = false;
    bool  recording     = false;
    bool  targetFound   = false;   // the analyser found a target clear of the sky
    bool  focusValid    = false;   // and could measure its half-flux diameter
    float hfdPx         = 0;
    float framePct      = 0;       // the target's share of the frame
    float clipPct       = 0;       // share of the target's pixels clipped
    float peakDN        = 0;       // 0-255
    float cx = 0, cy = 0;          // target centre, px
    int   fw = 0, fh = 0;          // frame size, px
    bool  tracking      = false;   // the mount tracking, or the guider holding it
    bool  fading        = false;   // a fade warning in the last two minutes
    int   seeingSteady  = -1;      // 1 steady, 0 not, -1 not known
    // Phase 145.3: the target fills the frame (the Moon), and the fine
    // pass on the edge metric is on, which needs no disc.
    bool  filled        = false;
    bool  edgeOnlyOk    = false;
};

struct AfReadyOut {
    bool ready = false;
    std::string why;               // empty when ready
};

// The limits, with their reasons.
inline constexpr float kAfMinHfdPx     = 3.0f;   // fewer and HFD is quantisation
inline constexpr float kAfMaxFramePct  = 40.0f;  // the disc needs sky around it
inline constexpr float kAfMaxClipPct   = 1.0f;
inline constexpr float kAfMinPeakDN    = 40.0f;
inline constexpr float kAfEdgeFrac     = 0.10f;  // the mirror shifts the image during a sweep

inline AfReadyOut AutofocusReadiness(const AfReadyIn& in) {
    auto no = [](const char* w) { AfReadyOut o; o.why = w; return o; };
    if (!in.focuserOpen)  return no("no focuser connected");
    if (in.afRunning)     return no("autofocus is running");
    if (in.focuserMoving) return no("the focuser is moving");
    if (in.recording)     return no("a recording is running");
    if (in.filled) {
        // No disc, so no HFD checks: the edge metric reads the surface.
        if (!in.edgeOnlyOk)
            return no("the target fills the frame: autofocus needs a disc with sky around it "
                      "(or AutofocusFineStage=1, which can focus on the surface)");
        if (!in.tracking)         return no("the mount is not tracking");
        if (in.fading)            return no("the target is fading (dew or haze?)");
        if (in.seeingSteady == 0) return no("the seeing is too unsteady for a focus curve");
        AfReadyOut o; o.ready = true;
        return o;
    }
    if (!in.targetFound)  return no("no target in the frame");
    if (!in.focusValid)   return no("the target's sharpness cannot be measured");
    if (in.framePct > kAfMaxFramePct)
        return no("the target fills the frame: autofocus needs a disc with sky around it");
    if (in.hfdPx < kAfMinHfdPx) return no("the target is too small to measure");
    if (in.clipPct > kAfMaxClipPct) return no("the target is clipped: lower exposure or gain");
    if (in.peakDN < kAfMinPeakDN)   return no("the target is too faint");
    if (in.fw > 0 && in.fh > 0) {
        const float mx = in.fw * kAfEdgeFrac, my = in.fh * kAfEdgeFrac;
        if (in.cx < mx || in.cx > in.fw - mx || in.cy < my || in.cy > in.fh - my)
            return no("the target is near the edge, and the sweep shifts it: centre it");
    }
    if (!in.tracking)          return no("the mount is not tracking");
    if (in.fading)             return no("the target is fading (dew or haze?)");
    if (in.seeingSteady == 0)  return no("the seeing is too unsteady for a focus curve");
    AfReadyOut o; o.ready = true;
    return o;
}

}  // namespace raduk::core
