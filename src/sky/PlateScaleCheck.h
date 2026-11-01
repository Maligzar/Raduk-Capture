#pragma once
// ============================================================
// PlateScaleCheck.h - the solved plate scale against rig.ini
//
// PlateScaleArcsecPx is focal length over pixel size, calculated,
// and every arcsecond figure in the app derives from it. A solve
// is the one thing that measures it.
//
// This used to be eleven lines inside AutoSolveOnLock, which meant
// three of the four solve paths never printed it, and the checklist
// told the operator to read a line the [GOTO] solve button could
// not produce. It is here so all four share one copy and so the
// arithmetic can run on a bench: GoToPanel.h needs windows.h and a
// running ASTAP, and the emulator has no field that solves.
//
// The comparison is against rig.ini's figure and not against focal
// length times pixel size, because rig.ini's is the number the app
// is actually using. Once corrected it must read 0% out.
//
// A binned frame reads as twice the scale, since rig.ini's figure
// is per sensor pixel and this one is per frame pixel. Every stock
// profile runs at bin 1, so the checklist says bin 1 rather than
// this growing a parameter nothing would set.
// ============================================================

#include <cmath>
#include <cstdio>
#include <string>

#include "SolveResult.h"

namespace raduk::sky {

// Over this, the line tells the operator to act.
constexpr double kPlateScaleFlagPct = 5.0;

struct PlateScaleCheck {
    bool   valid       = false;
    double solved      = 0.0;   // arcsec/px, off the solve
    double assumed     = 0.0;   // arcsec/px, rig.ini
    double offPct      = 0.0;   // magnitude, percent of assumed
    double focalMm     = 0.0;   // implied; 0 when the pixel is unknown
    bool   flagged     = false;
};

// frameW is the width of the frame that was solved, which on a crop
// is not the sensor's: fovW is the width of that same frame, so the
// two cancel and a ROI measures the same scale as the full sensor.
// pixelUm of zero means the camera has not said, and the focal
// length is left out rather than guessed from a literal.
inline PlateScaleCheck CheckPlateScale(const SolveResult& r, unsigned frameW,
                                       double assumed, double pixelUm) {
    PlateScaleCheck c;
    if (!r.solved || r.fovW <= 0.0 || frameW == 0) return c;

    c.valid   = true;
    c.solved  = r.fovW * 3600.0 / (double)frameW;
    c.assumed = assumed;
    c.offPct  = (assumed > 0.0001)
              ? std::fabs(c.solved - assumed) / assumed * 100.0 : 0.0;
    c.flagged = c.offPct > kPlateScaleFlagPct;
    c.focalMm = 206.265 * pixelUm / c.solved;
    return c;
}

// Two decimals on the percentage: the checklist compares three
// solves against each other, and "0% out" three times says nothing
// about whether they agreed.
inline std::string FormatPlateScale(const PlateScaleCheck& c) {
    if (!c.valid) return {};
    char focal[48] = "";
    if (c.focalMm > 0.0)
        snprintf(focal, sizeof(focal), ", focal length %.0f mm", c.focalMm);
    char sb[200];
    snprintf(sb, sizeof(sb),
             "Plate scale: solved %.4f\"/px vs rig.ini %.4f\"/px "
             "(%.2f%% out)%s%s",
             c.solved, c.assumed, c.offPct, focal,
             c.flagged ? " <- set PlateScaleArcsecPx to the solved value" : "");
    return sb;
}

} // namespace raduk::sky
