// ============================================================
//  MosaicPlan — framing a target bigger than the sensor
//
//  Phase 97, roadmap row 34, and the direct answer to something the
//  owner raised while planning 94: "the moon will be harder because
//  the size of my sensor will make the moon very large so will need
//  to account for that."
//
//  Measured on this rig -- 0.203 arcsec/px, 3840x2160 -- the full
//  sensor sees 12.99' x 7.31'. The Moon runs 29.4' to 33.5', so it is
//  two and a half frames wide and four and a half tall. It has never
//  fitted and the application has never said so.
//
//  ── The finding that decides the design ────────────────────
//
//  A mosaic is not just "more panels". The Moon moves 13.176 degrees
//  a day eastward against the stars, which a sidereal-tracking mount
//  loses at 0.55 arcsec/s -- 2.7 px/s here. The consequence depends
//  entirely on the timescale, and conflating the three is how people
//  end up believing sidereal tracking blurs lunar frames:
//
//    per EXPOSURE   20 ms  -> 0.05 px. Nothing at all.
//    per RUN        60 s   -> 163 px, 4% of frame width. The stacker
//                             takes that out.
//    per MOSAIC     8 min  -> 277 arcsec of drift against an overlap
//                             of 156 arcsec at 20%.
//
//  **The drift exceeds the entire overlap by a factor of 1.8.** A
//  mosaic planned in fixed offsets under sidereal tracking does not
//  close -- not because the panels are blurred but because the last
//  one is pointing at a different part of the Moon than the plan
//  thinks. That is what MosaicCloses() exists to say before the
//  owner spends eight minutes finding out.
//
//  Pure arithmetic in a header a test can reach, for the reason
//  PixelWCS.h gives: a mosaic grid off by one panel, or an RA offset
//  missing its cos(dec), produces a plan that looks entirely
//  reasonable and does not cover the target.
// ============================================================

#pragma once

#include "LunarSolarSize.h"

#include <cmath>

namespace raduk::sky {

struct MosaicPlan {
    int    cols = 1;
    int    rows = 1;
    int    panels = 1;
    double stepWArcmin = 0.0;    // between panel centres
    double stepHArcmin = 0.0;
    double coveredWArcmin = 0.0; // what the grid actually spans
    double coveredHArcmin = 0.0;
    double overlapWArcsec = 0.0; // the shared strip between neighbours
    double overlapHArcsec = 0.0;
    bool   singleFrame = false;  // the target fits; there is no mosaic
};

// Overlap is clamped rather than trusted. Zero is legal and means
// butt-joined panels, which no stacker will register; above a half the
// step goes to nothing and the panel count runs away.
inline constexpr double MOSAIC_MIN_OVERLAP = 0.0;
inline constexpr double MOSAIC_MAX_OVERLAP = 0.45;

// Panels needed along one axis to cover `span` with frames of `frame`
// stepping by `step`.
//
// NOT ceil(span / step). With n panels the covered extent is
// (n - 1) * step + frame, because the first and last panels each
// contribute their whole width and only the gaps between them are
// steps. Using ceil(span/step) asks for an extra panel whenever the
// target is close to one frame -- it says two panels for a target
// exactly one frame wide.
inline int PanelsAcross(double span, double frame, double step) {
    if (frame <= 0.0 || step <= 0.0) return 1;
    if (span <= frame) return 1;
    return (int)std::ceil((span - frame) / step) + 1;
}

inline MosaicPlan PlanMosaic(double targetArcmin, const FieldOfView& fov,
                             double overlapFrac) {
    MosaicPlan p;
    if (targetArcmin <= 0.0 || fov.widthArcmin <= 0.0 ||
        fov.heightArcmin <= 0.0) return p;

    if (overlapFrac < MOSAIC_MIN_OVERLAP) overlapFrac = MOSAIC_MIN_OVERLAP;
    if (overlapFrac > MOSAIC_MAX_OVERLAP) overlapFrac = MOSAIC_MAX_OVERLAP;

    p.stepWArcmin = fov.widthArcmin  * (1.0 - overlapFrac);
    p.stepHArcmin = fov.heightArcmin * (1.0 - overlapFrac);
    p.cols = PanelsAcross(targetArcmin, fov.widthArcmin,  p.stepWArcmin);
    p.rows = PanelsAcross(targetArcmin, fov.heightArcmin, p.stepHArcmin);
    p.panels = p.cols * p.rows;
    p.coveredWArcmin = (p.cols - 1) * p.stepWArcmin + fov.widthArcmin;
    p.coveredHArcmin = (p.rows - 1) * p.stepHArcmin + fov.heightArcmin;
    p.overlapWArcsec = fov.widthArcmin  * overlapFrac * 60.0;
    p.overlapHArcsec = fov.heightArcmin * overlapFrac * 60.0;
    p.singleFrame = (p.cols == 1 && p.rows == 1);
    return p;
}

// ── Where each panel points ─────────────────────────────────
//
// Offsets on the TANGENT PLANE, east and north of the target centre,
// in arcsec. Not RA and Dec: see RaDecOffset below for why that
// conversion is a separate step and not an afterthought.
struct Panel {
    int    col = 0, row = 0;
    double eastArcsec  = 0.0;
    double northArcsec = 0.0;
};

// Serpentine order: left to right along one row, right to left along
// the next. Raster order would send the mount flying back across the
// whole disc at the end of every row, which on an 8SE is several
// seconds of slew and settle per row for nothing -- and every second
// spent is drift, which is the one budget this plan is short of.
inline Panel PanelAt(const MosaicPlan& p, int index) {
    Panel q;
    if (p.cols <= 0 || p.rows <= 0) return q;
    if (index < 0) index = 0;
    if (index >= p.cols * p.rows) index = p.cols * p.rows - 1;

    const int row = index / p.cols;
    int col = index % p.cols;
    if (row % 2 == 1) col = p.cols - 1 - col;      // the serpentine

    q.col = col;
    q.row = row;
    // Centred on the target: the grid spans (n-1) steps, so the
    // middle of it is at (n-1)/2.
    q.eastArcsec  = (col - (p.cols - 1) * 0.5) * p.stepWArcmin * 60.0;
    // North is up, and row 0 is the top, so row increases southward.
    q.northArcsec = ((p.rows - 1) * 0.5 - row) * p.stepHArcmin * 60.0;
    return q;
}

// Does this panel contain any of the disc at all?
//
// A grid covers the target's bounding square, and the target is a
// disc, so the corner panels of a tall mosaic hold nothing but sky.
// On this rig the Moon in a 3x6 grid leaves real corners empty, and
// skipping them is not merely tidy: every panel skipped is time not
// spent, and time is drift.
//
// Tested as a rectangle against a circle, which is the honest test
// for a square frame on a round target -- a panel whose corner clips
// the limb does contain Moon.
inline bool PanelTouchesDisc(const MosaicPlan& p, const Panel& q,
                             const FieldOfView& fov, double targetArcmin) {
    const double r = targetArcmin * 30.0;               // radius, arcsec
    if (r <= 0.0) return false;
    const double hw = fov.widthArcmin  * 30.0;          // half-frame
    const double hh = fov.heightArcmin * 30.0;
    // Closest point of the frame rectangle to the disc centre.
    const double dx = std::fabs(q.eastArcsec)  - hw;
    const double dy = std::fabs(q.northArcsec) - hh;
    const double cx = dx > 0.0 ? dx : 0.0;
    const double cy = dy > 0.0 ? dy : 0.0;
    return (cx * cx + cy * cy) <= r * r;
}

inline int PanelsTouchingDisc(const MosaicPlan& p, const FieldOfView& fov,
                              double targetArcmin) {
    int n = 0;
    for (int i = 0; i < p.cols * p.rows; ++i)
        if (PanelTouchesDisc(p, PanelAt(p, i), fov, targetArcmin)) ++n;
    return n;
}

// ── Tangent plane to mount coordinates ──────────────────────
//
// The cos(dec) term, which is the single easiest thing to leave out
// of a mosaic and the hardest to spot afterwards. An offset of one
// arcsec EAST ON THE SKY is one arcsec of RA only on the equator; at
// declination 60 it is two. Omit it and every panel is too close
// together in RA by a factor that happens to be 1.0 for the targets
// you tested near the equator.
//
// The Moon stays within about 28 degrees of the equator, where the
// factor runs to 1.13 -- small enough to look like a tracking error
// and large enough to lose the overlap.
struct MountOffset {
    double raArcsec  = 0.0;     // along the RA axis, cos(dec) applied
    double decArcsec = 0.0;
};

inline MountOffset RaDecOffset(double eastArcsec, double northArcsec,
                               double decDeg) {
    MountOffset m;
    m.decArcsec = northArcsec;
    double c = std::cos(DegToRad(decDeg));
    // At the pole the RA axis has no extent on the sky and the offset
    // is meaningless. Clamped rather than infinite: a planetary rig
    // never points there, and returning inf would propagate silently.
    const double MIN_COS = 0.01;                 // dec +/-89.43
    if (c < MIN_COS) c = MIN_COS;
    m.raArcsec = eastArcsec / c;
    return m;
}

// ── The time and drift budget ───────────────────────────────

inline double MosaicSeconds(int panels, double captureSec, double settleSec) {
    if (panels <= 0) return 0.0;
    // Every panel is captured; only the moves between them settle.
    return panels * captureSec + (panels - 1) * settleSec;
}

inline double DriftArcsec(double arcsecPerSec, double seconds) {
    return arcsecPerSec * seconds;
}

// Does the mosaic close?
//
// The question is not whether the frames are sharp -- at 20 ms they
// are, drift or no drift. It is whether the LAST panel is pointing
// where the plan says. Fixed offsets plus an untracked drift means the
// target has walked, so the overlap between the first row and the last
// is eaten from one side.
//
// It closes while the total drift stays inside the overlap, with a
// margin: an overlap entirely consumed is an overlap of zero, and a
// stacker needs real shared detail to register on, not a shared edge.
inline constexpr double OVERLAP_MARGIN = 0.5;   // keep half of it

// Which overlap the drift actually eats, and why this is the smaller
// of the two rather than the horizontal one.
//
// The Moon's motion is along its own orbit, which is inclined about 5
// degrees to the ecliptic, so the drift is PREDOMINANTLY east and
// consumes the horizontal overlap. On that reading the budget should
// be overlapWArcsec, which on this rig is 156 arcsec against the
// vertical's 88.
//
// The declination component is real, varies over the month, and is
// NOT modelled here -- MOON_DRIFT_ARCSEC_PER_SEC is a mean motion
// along the path, not a vector. So taking the smaller overlap is a
// deliberately conservative bound on an unmodelled direction, not an
// oversight, and it is stated rather than left to be inferred from
// the arithmetic. If the dec component is ever modelled this becomes
// a proper vector comparison and the bound can relax.
//
// It does not change the verdict on this rig: 272 arcsec of drift
// exceeds half of either overlap by more than three times.
inline double DriftBudgetArcsec(const MosaicPlan& p) {
    const double smaller = p.overlapWArcsec < p.overlapHArcsec
                         ? p.overlapWArcsec : p.overlapHArcsec;
    return smaller * OVERLAP_MARGIN;
}

inline bool MosaicCloses(const MosaicPlan& p, double driftArcsecPerSec,
                         double captureSec, double settleSec) {
    if (p.singleFrame) return true;
    const double secs  = MosaicSeconds(p.panels, captureSec, settleSec);
    const double drift = DriftArcsec(driftArcsecPerSec, secs);
    return drift <= DriftBudgetArcsec(p);
}

// How long the mosaic may take before the overlap is gone. Useful the
// other way round: the owner has a panel count and wants to know the
// deadline.
inline double SecondsBeforeOverlapLost(const MosaicPlan& p,
                                       double driftArcsecPerSec) {
    if (driftArcsecPerSec <= 0.0) return 1.0e9;
    return DriftBudgetArcsec(p) / driftArcsecPerSec;
}

// No mosaic has been attempted with this. The grid, the serpentine,
// the cos(dec) and the drift budget are checked against hand
// arithmetic; whether an 8SE's settle time makes the budget reachable
// is a question for a clear night.
inline constexpr bool MOSAIC_PROVEN_ON_SKY = false;

} // namespace raduk::sky
