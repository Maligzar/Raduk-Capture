#pragma once
// ============================================================
// PixelWCS.h - what is under the cursor, and which way is which
//             (phase 47b)
//
// Phase 47 shipped click-to-recentre without a plate solve,
// because the guide calibration already knew how the image
// responds to a pulse. That is a RELATIVE transform: it can move
// a pixel to the middle without ever knowing what the pixel is.
//
// This half is the absolute question. "What is at [412, 300]"
// has no answer that a relative transform can give, so it waits
// on a solve, and that is why it was split out.
//
// Two things live here, and they have different pedigrees, which
// matters more than it looks:
//
//   The sky rose and the coordinate readout come from the plate
//   solve. They are as good as the solver's WCS and the
//   conventions below.
//
//   The jog rose comes from the guide calibration, which was
//   MEASURED on this rig by pulsing the mount and watching the
//   centroid. It owes nothing to any convention. If the two
//   disagree, the measured one is right.
//
// AgreementAngleDeg exists to say so out loud rather than let a
// wrong convention quietly point somebody at the wrong star.
//
// ── The one assumption, stated where it can be found ──────
//
// FITS counts rows from the BOTTOM of the image; a frame buffer
// counts them from the top. Somewhere between the frame and the
// solver's answer, that flip either happens or does not, and the
// answer depends on what ASTAP does with the top-down BMP it is
// handed. That is not something a bench without ASTAP can settle,
// so it is a named flag (PlateSolution::fitsBottomUp), defaulted
// to the FITS convention, with both settings pinned by tests. If
// the sky says otherwise it is one field to change, not a hunt.
// ============================================================

#include "SolveResult.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace raduk::sky {

// ── The frame's geometry, as a solve describes it ──────────
//
// Held as the WCS CD matrix rather than a scale and an angle.
// The CD matrix carries the plate scale, the rotation and the
// parity together, and parity is the one that bites: a mirrored
// train (a star diagonal, an odd number of reflections) flips
// east and west, and a readout that is confidently backwards is
// worse than no readout. CDELT plus CROTA can express the same
// thing, but only if the sign of CDELT1 survives, and it is the
// first thing a parser drops.
struct PlateSolution {
    bool   valid    = false;

    double raHours  = 0.0;     // reference point, J2000
    double decDeg   = 0.0;

    // Reference pixel, in frame-buffer coordinates: x right from
    // the left edge, y DOWN from the top. Not FITS 1-based
    // CRPIX; the solver layer converts.
    double refX     = 0.0;
    double refY     = 0.0;

    // Degrees of intermediate world coordinate per pixel step.
    // cd[0][0] is d(xi)/dx, cd[0][1] is d(xi)/dy, and so on.
    double cd[2][2] = {{0.0, 0.0}, {0.0, 0.0}};

    int    frameW   = 0;
    int    frameH   = 0;

    // See the header note. True means the solver's y axis counts
    // up from the bottom, so a frame-buffer row must be flipped
    // before the CD matrix is applied.
    bool   fitsBottomUp = true;
};

struct SkyPoint {
    bool   valid   = false;
    double raHours = 0.0;
    double decDeg  = 0.0;
};

// Determinant of the CD matrix. Negative is the ordinary sky
// image, where RA increases to the left; positive means the
// image is mirrored.
inline double CdDeterminant(const PlateSolution& s) {
    return s.cd[0][0] * s.cd[1][1] - s.cd[0][1] * s.cd[1][0];
}

inline bool IsMirrored(const PlateSolution& s) {
    return CdDeterminant(s) > 0.0;
}

// Plate scale, arcseconds per pixel, from the matrix rather than
// from whatever CDELT the solver also wrote. Averaged over the
// two axes: they differ only for a non-square sensor or a bad
// solve, and a single number is what the rest of the app wants.
inline double ArcsecPerPixel(const PlateSolution& s) {
    const double cx = std::sqrt(s.cd[0][0] * s.cd[0][0] + s.cd[1][0] * s.cd[1][0]);
    const double cy = std::sqrt(s.cd[0][1] * s.cd[0][1] + s.cd[1][1] * s.cd[1][1]);
    return (cx + cy) * 0.5 * 3600.0;
}

// Build a CD matrix the old way, from a scale and a rotation,
// for a solver that reports no CD matrix. degPerPxX carries the
// parity in its sign, exactly as FITS CDELT1 does.
//
// This is the lossy path and exists only as a fallback; the
// rotation keyword is not even agreed between ASTAP's own
// consumers (some read CROTA1, some CROTA2), which is the whole
// argument for preferring the matrix.
inline void SetCdFromScaleRotation(PlateSolution& s,
                                   double degPerPxX, double degPerPxY,
                                   double rotationDeg) {
    const double r = rotationDeg * 3.14159265358979 / 180.0;
    const double c = std::cos(r), n = std::sin(r);
    s.cd[0][0] =  degPerPxX * c;
    s.cd[0][1] = -degPerPxY * n;
    s.cd[1][0] =  degPerPxX * n;
    s.cd[1][1] =  degPerPxY * c;
}

// Frame-buffer row to the row the CD matrix expects.
inline double WcsRow(const PlateSolution& s, double frameY) {
    return s.fitsBottomUp ? ((double)(s.frameH - 1) - frameY) : frameY;
}

// ── From a solver's keywords into the struct above ──────────
//
// The 1-based, bottom-up FITS reference pixel into the 0-based,
// top-down one the frame buffer uses. Two off-by-ones and a flip,
// which is three chances to be half a pixel or a whole frame out,
// so it lives here where tests/pixel_wcs.cpp can hold it still
// rather than inline in the solver where nothing compiles it.
inline void SetReferencePixelFromCrpix(PlateSolution& s,
                                       double crpix1, double crpix2) {
    s.refX = crpix1 - 1.0;
    s.refY = s.fitsBottomUp ? ((double)s.frameH - crpix2)
                            : (crpix2 - 1.0);
}

// Everything a solve has to hand over, in one call.
//
// hasCd decides the route. The CD matrix is preferred because it
// carries scale, rotation and parity as one object; the fallback
// reassembles them from CDELT and CROTA, and can only be as good
// as the sign of CDELT1, which is exactly what a parser drops.
// ASTAP's own consumers do not even agree on the rotation keyword
// (some read CROTA1, some CROTA2), so the fallback is a fallback.
inline PlateSolution MakePlateSolution(double raHours, double decDeg,
                                       int frameW, int frameH,
                                       bool hasCd,
                                       double cd11, double cd12,
                                       double cd21, double cd22,
                                       double degPerPxX, double degPerPxY,
                                       double rotationDeg,
                                       bool hasCrpix,
                                       double crpix1, double crpix2) {
    PlateSolution s;
    if (frameW <= 0 || frameH <= 0) return s;

    s.raHours = raHours;
    s.decDeg  = decDeg;
    s.frameW  = frameW;
    s.frameH  = frameH;

    if (hasCd) {
        s.cd[0][0] = cd11; s.cd[0][1] = cd12;
        s.cd[1][0] = cd21; s.cd[1][1] = cd22;
    } else {
        SetCdFromScaleRotation(s, degPerPxX, degPerPxY, rotationDeg);
    }
    if (std::abs(CdDeterminant(s)) < 1e-16) return s;   // nothing usable

    // No CRPIX means the solve is about the middle of the frame,
    // which is what every solver assumes when it does not say.
    // Expressed in FITS terms so there is one conversion, not two.
    if (hasCrpix) SetReferencePixelFromCrpix(s, crpix1, crpix2);
    else          SetReferencePixelFromCrpix(s, (frameW + 1) * 0.5,
                                                (frameH + 1) * 0.5);

    s.valid = true;
    return s;
}

// The whole journey, from a solve to something the cursor can be
// measured against. frameW/frameH are the dimensions of the frame
// as it sits in the buffer, which is what the solver was handed
// and what ScreenToFramePixel returns coordinates in.
inline PlateSolution PlateSolutionFromSolve(const SolveResult& r,
                                            int frameW, int frameH) {
    if (!r.solved) return PlateSolution{};
    return MakePlateSolution(r.ra, r.dec, frameW, frameH,
                             r.hasCd, r.cd1_1, r.cd1_2, r.cd2_1, r.cd2_2,
                             r.degPerPxX, r.degPerPxY, r.rotation,
                             r.hasCrpix, r.crpix1, r.crpix2);
}

// ── Pixel to sky ────────────────────────────────────────────
//
// Gnomonic (TAN) deprojection, which is what every plate solver
// means by a WCS unless it says otherwise.
//
// A linear approximation would be invisible over this rig's
// 0.13 degree planetary field, and would be wrong by arcminutes
// on a finder or a wide-field frame. The exact form is six more
// lines, so there is no reason to carry a field-size caveat
// around with the answer.
inline SkyPoint PixelToSky(const PlateSolution& s, double frameX, double frameY) {
    SkyPoint out;
    if (!s.valid || s.frameW <= 0 || s.frameH <= 0) return out;
    if (frameX < 0.0 || frameY < 0.0 ||
        frameX > (double)s.frameW || frameY > (double)s.frameH) return out;

    const double D2R = 3.14159265358979 / 180.0;
    const double R2D = 180.0 / 3.14159265358979;

    const double dx = frameX - s.refX;
    const double dy = WcsRow(s, frameY) - WcsRow(s, s.refY);

    // Intermediate world coordinates, degrees then radians.
    const double xi  = (s.cd[0][0] * dx + s.cd[0][1] * dy) * D2R;
    const double eta = (s.cd[1][0] * dx + s.cd[1][1] * dy) * D2R;

    const double ra0  = s.raHours * 15.0 * D2R;
    const double dec0 = s.decDeg * D2R;
    const double sd = std::sin(dec0), cd0 = std::cos(dec0);

    const double denom = cd0 - eta * sd;
    double ra  = ra0 + std::atan2(xi, denom);
    const double dec = std::atan2(sd + eta * cd0,
                                  std::sqrt(xi * xi + denom * denom));

    double raH = ra * R2D / 15.0;
    raH = std::fmod(raH, 24.0);
    if (raH < 0.0) raH += 24.0;

    out.valid   = true;
    out.raHours = raH;
    out.decDeg  = dec * R2D;
    return out;
}

// ── The rose ────────────────────────────────────────────────
//
// Unit vectors in screen space: x right, y DOWN, because that is
// what Direct2D draws in and converting at the draw site is how
// a sign gets lost.
struct RoseArrow {
    bool  valid = false;
    float dx    = 0.0f;
    float dy    = 0.0f;
};

struct DirectionRose {
    RoseArrow skyNorth;   // increasing declination
    RoseArrow skyEast;    // increasing right ascension
    RoseArrow jogEast;    // where an East pulse pushes the IMAGE
    RoseArrow jogNorth;   // where a North pulse pushes the IMAGE
};

inline RoseArrow UnitArrow(double dx, double dy) {
    RoseArrow a;
    const double m = std::sqrt(dx * dx + dy * dy);
    if (!(m > 1e-12)) return a;          // also catches NaN
    a.valid = true;
    a.dx = (float)(dx / m);
    a.dy = (float)(dy / m);
    return a;
}

// Which way north and east run across the screen.
//
// Found by inverting the CD matrix rather than by reading a
// rotation angle, so parity comes out on its own: in a mirrored
// train east lands on the other side with no special case and no
// flag to remember.
inline void FillSkyRose(DirectionRose& rose, const PlateSolution& s) {
    if (!s.valid) return;
    const double det = CdDeterminant(s);
    if (std::abs(det) < 1e-16) return;   // degenerate solve

    // Inverse CD: a step of one degree in (xi, eta) as pixels.
    // Column one is east (xi grows east), column two is north.
    const double exPx =  s.cd[1][1] / det;
    const double eyPx = -s.cd[1][0] / det;
    const double nxPx = -s.cd[0][1] / det;
    const double nyPx =  s.cd[0][0] / det;

    // Back from the solver's row order into screen rows.
    const double flip = s.fitsBottomUp ? -1.0 : 1.0;
    rose.skyEast  = UnitArrow(exPx, eyPx * flip);
    rose.skyNorth = UnitArrow(nxPx, nyPx * flip);
}

// Which way a jog pushes the image.
//
// The arguments are the guide calibration's measured response in
// pixels per millisecond of pulse, which is already image motion
// in screen axes. Nothing is converted, because there is nothing
// to convert: this is the one direction in the app that was
// observed rather than derived.
inline void FillJogRose(DirectionRose& rose,
                        float exPerMs, float eyPerMs,
                        float nxPerMs, float nyPerMs) {
    rose.jogEast  = UnitArrow(exPerMs, eyPerMs);
    rose.jogNorth = UnitArrow(nxPerMs, nyPerMs);
}

// ── Do the two agree? ───────────────────────────────────────
//
// An East pulse drives the telescope east, so the field slides
// west across the sensor: the image moves OPPOSITE to the screen
// direction of increasing RA. Both arrows are known
// independently, so the angle between the jog arrow and the
// reversed sky arrow is a real check on the solve's parity and on
// the row-order assumption at the top of this file.
//
// Near zero means they agree. Near 180 is a sign convention
// inverted. Anything in between on a mirrored-looking solve is
// the parity. The number is returned rather than acted on: this
// is a readout, and silently "correcting" a disagreement would
// hide the very fault it was added to catch.
//
// Negative means the question cannot be asked yet.
inline double AgreementAngleDeg(const DirectionRose& rose) {
    if (!rose.skyEast.valid || !rose.jogEast.valid) return -1.0;
    const double ax = -(double)rose.skyEast.dx;
    const double ay = -(double)rose.skyEast.dy;
    const double bx =  (double)rose.jogEast.dx;
    const double by =  (double)rose.jogEast.dy;
    double dot = ax * bx + ay * by;
    if (dot >  1.0) dot =  1.0;
    if (dot < -1.0) dot = -1.0;
    return std::acos(dot) * 180.0 / 3.14159265358979;
}

// ── How long an answer is worth having ──────────────────────
//
// This is the part that decides whether the readout is useful or
// actively harmful, because a stale solution does not degrade: it
// keeps naming a coordinate, confidently, long after that
// coordinate stopped being under the cursor.
//
// Two things invalidate it, and they are not the same thing.
//
// POINTING. Any commanded move -- a GoTo, a jog, a recentre pulse
// -- means the frame is no longer the frame that was solved. That
// is not a decay, it is a break, and the caller reports it by
// clearing the solution outright. Nothing here can detect it;
// only the app knows it commanded a move.
//
// ROTATION. This rig is an alt-az 8SE, and an alt-az mount
// rotates the field as it tracks even when the pointing stays
// perfect. So the rose and the coordinates go stale on a tracking
// mount that has not moved at all, which is the case a
// pointing-only check would pass with flying colours.
//
// The field rotation rate is 15.04 deg/hr * cos(lat) * cos(az) /
// cos(alt), largest near the zenith, and the honest answer is
// that it depends on where the scope is looking. Rather than
// recompute it per frame, the budget is expressed the other way
// round: how long until the field has turned by one tolerable
// amount, at a rate the caller supplies or at a default that is
// the rate at a middling altitude for a mid-latitude site.
//
// A wedge or an EQ mount has no field rotation; pass 0 and the
// solution ages only against the ceiling.
struct SolveAge {
    bool  usable  = false;   // false: do not draw coordinates
    bool  stale   = false;   // usable, but say so
    double ageSec = 0.0;
    double fieldTurnedDeg = 0.0;
};

// Degrees of field rotation per hour at a middling pointing for a
// mid-northern site. Deliberately a single conservative number
// and not a model: the model needs alt, az and latitude, and the
// caller that has them can pass its own rate.
inline double DefaultFieldRotationDegPerHour() { return 10.0; }

// One hour, whatever the rotation says. A solve older than this
// predates something -- a filter change, a focus run, a refocus
// nudge, somebody bumping the tripod -- often enough that
// trusting it is not worth the one time it is wrong.
inline double MaxSolveAgeSec() { return 3600.0; }

// Tolerable field rotation before the rose is worth distrusting.
// A degree is about the width of the arrow head at the sizes
// these are drawn, so it is the point at which the picture starts
// disagreeing with itself.
inline double TolerableFieldTurnDeg() { return 1.0; }

inline SolveAge AgeSolve(double ageSec, double fieldRotDegPerHour) {
    SolveAge a;
    if (!(ageSec >= 0.0)) return a;                 // also catches NaN
    a.ageSec = ageSec;
    const double rate = (fieldRotDegPerHour > 0.0) ? fieldRotDegPerHour : 0.0;
    a.fieldTurnedDeg = rate * ageSec / 3600.0;

    if (ageSec > MaxSolveAgeSec()) return a;        // usable stays false
    a.usable = true;
    a.stale  = (a.fieldTurnedDeg > TolerableFieldTurnDeg());
    return a;
}

// "4m12s", for a line that already has coordinates on it.
inline std::string FormatAge(double ageSec) {
    if (!(ageSec >= 0.0)) return "?";
    const long long t = (long long)(ageSec + 0.5);
    char buf[32];
    if (t < 60)       snprintf(buf, sizeof(buf), "%llds", (long long)t);
    else if (t < 3600) snprintf(buf, sizeof(buf), "%lldm%02llds", t / 60, t % 60);
    else               snprintf(buf, sizeof(buf), "%lldh%02lldm", t / 3600, (t % 3600) / 60);
    return buf;
}

// ── Wording ─────────────────────────────────────────────────
//
// Sexagesimal, because that is how every catalogue, every finder
// chart and every other person at the eyepiece writes a
// coordinate, and the readout exists to be compared against one
// of those.
//
// Rounding is done once, on the total seconds, before the parts
// are split out. Rounding each part on its own is how 10h 59m
// 59.96s prints as 10h 59m 60.0s.
inline std::string FormatRA(double raHours) {
    double h = std::fmod(raHours, 24.0);
    if (h < 0.0) h += 24.0;
    long long t = (long long)(h * 3600.0 * 100.0 + 0.5);   // centiseconds
    if (t >= 24LL * 3600 * 100) t = 0;
    const int cs = (int)(t % 100); t /= 100;
    const int ss = (int)(t % 60);  t /= 60;
    const int mm = (int)(t % 60);  t /= 60;
    char buf[32];
    snprintf(buf, sizeof(buf), "%02dh%02dm%02d.%02ds", (int)t, mm, ss, cs);
    return buf;
}

inline std::string FormatDec(double decDeg) {
    const bool neg = decDeg < 0.0;
    double d = neg ? -decDeg : decDeg;
    long long t = (long long)(d * 3600.0 * 10.0 + 0.5);    // tenths of arcsec
    const int ts = (int)(t % 10); t /= 10;
    const int ss = (int)(t % 60); t /= 60;
    const int mm = (int)(t % 60); t /= 60;
    char buf[32];
    snprintf(buf, sizeof(buf), "%c%02d\xC2\xB0%02d'%02d.%d\"",
             neg ? '-' : '+', (int)t, mm, ss, ts);
    return buf;
}

// The tooltip's coordinate line. Empty when there is no solve,
// so the caller shows the pixel readout alone rather than a row
// of zeroes that looks like a position.
inline std::string FormatPixelSky(const SkyPoint& p) {
    if (!p.valid) return "";
    return FormatRA(p.raHours) + "  " + FormatDec(p.decDeg);
}

} // namespace raduk::sky
