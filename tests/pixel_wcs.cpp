// ============================================================
//  What is under the cursor, and which way is which (phase 47b)
//
//  Phase 47 moved a clicked pixel to the middle without ever
//  knowing what the pixel was, because the guide calibration is
//  a relative transform. Naming the pixel is the absolute
//  question, and it waits on a plate solve.
//
//  Every way of getting this wrong produces a coordinate that
//  looks entirely plausible, which is why it is worth a bench:
//
//    Parity. A mirrored train puts east on the other side. The
//    readout is then wrong by twice the offset and reads like a
//    pointing error, so it survives a casual look at the sky.
//
//    Row order. FITS counts rows up from the bottom and a frame
//    buffer counts them down from the top. Getting it backwards
//    reflects the answer about the centre line, which again
//    looks like a plausible coordinate.
//
//    cos(dec). The same pixel offset is a much larger RA step
//    near the pole. A readout that forgets it agrees with the
//    catalogue near the equator and drifts away as you go north,
//    which is the worst possible way to be wrong.
//
//    Carrying in the sexagesimal split. 59.96 seconds rounds to
//    60.0, and rounding each part on its own prints it.
//
//  The jog rose is not tested against the sky, because it owes
//  nothing to a convention: it is the measured guide response in
//  screen axes. What IS tested is the agreement check between it
//  and the solve, since that is the thing that will catch the
//  one assumption this file cannot settle on a bench.
// ============================================================

#include "sky/PixelWCS.h"

#include <cstdio>
#include <cmath>
#include <string>

using namespace raduk::sky;

static int g_fail = 0;

static void Near(double got, double want, double tol, const char* what) {
    if (std::fabs(got - want) <= tol) {
        printf("  ok    %s (%.6f)\n", what, got);
    } else {
        printf("  FAIL  %s: got %.6f, wanted %.6f +/- %.6f\n", what, got, want, tol);
        ++g_fail;
    }
}

static void Check(bool ok, const char* what) {
    if (ok) printf("  ok    %s\n", what);
    else  { printf("  FAIL  %s\n", what); ++g_fail; }
}

static void Same(const std::string& got, const char* want, const char* what) {
    if (got == want) printf("  ok    %s (%s)\n", what, got.c_str());
    else { printf("  FAIL  %s: got \"%s\", wanted \"%s\"\n",
                  what, got.c_str(), want); ++g_fail; }
}

// A plain, un-rotated, un-mirrored solve: 1 arcsec per pixel on a
// 1000x800 frame, reference at the centre, pointed at 10h +40.
// 1 arcsec = 1/3600 deg; CDELT1 negative is the ordinary sky image.
static PlateSolution Plain() {
    PlateSolution s;
    s.valid   = true;
    s.raHours = 10.0;
    s.decDeg  = 40.0;
    s.frameW  = 1000;
    s.frameH  = 800;
    s.refX    = 499.5;
    s.refY    = 399.5;
    SetCdFromScaleRotation(s, -1.0 / 3600.0, 1.0 / 3600.0, 0.0);
    return s;
}

int main() {
    printf("Pixel WCS: naming the pixel under the cursor\n");

    // ---- the reference pixel -----------------------------------
    printf("\n1. The reference pixel is the solved centre\n");
    {
        const PlateSolution s = Plain();
        const SkyPoint p = PixelToSky(s, s.refX, s.refY);
        Check(p.valid, "the centre of the frame has an answer");
        Near(p.raHours, 10.0, 1e-12, "and it is the solved RA exactly");
        Near(p.decDeg,  40.0, 1e-12, "and the solved Dec exactly");
    }

    // ---- scale and direction -----------------------------------
    printf("\n2. One pixel is one arcsecond, in the right direction\n");
    {
        const PlateSolution s = Plain();
        Near(ArcsecPerPixel(s), 1.0, 1e-9, "the matrix reports 1\"/px");
        Check(!IsMirrored(s), "and an ordinary sky image is not mirrored");

        // 100 px right of centre. CDELT1 is negative, so moving right
        // moves WEST: RA decreases. The step is 100" of arc, which is
        // 100/cos(40) arcsec of RA.
        const SkyPoint r = PixelToSky(s, s.refX + 100.0, s.refY);
        const double dRaArcsec = (r.raHours - 10.0) * 15.0 * 3600.0;
        Check(dRaArcsec < 0.0, "rightwards is west, so RA goes down");
        Near(std::fabs(dRaArcsec), 100.0 / std::cos(40.0 * 3.14159265358979 / 180.0),
             0.05, "by 100 arcsec of arc, which is more than 100s of RA at +40");

        // Up the screen is up the sky here: y is flipped into FITS
        // rows, and CDELT2 is positive.
        const SkyPoint u = PixelToSky(s, s.refX, s.refY - 100.0);
        Near((u.decDeg - 40.0) * 3600.0, 100.0, 0.05,
             "100 px up the screen is 100 arcsec north");
    }

    // ---- cos(dec) is not optional ------------------------------
    printf("\n3. The same pixel step is a bigger RA step near the pole\n");
    {
        PlateSolution lo = Plain(); lo.decDeg =  0.0;
        PlateSolution hi = Plain(); hi.decDeg = 80.0;
        const double dLo = std::fabs(PixelToSky(lo, lo.refX + 100.0, lo.refY).raHours - 0.0 - 10.0);
        const double dHi = std::fabs(PixelToSky(hi, hi.refX + 100.0, hi.refY).raHours - 10.0);
        Check(dHi > dLo * 5.0,
              "at +80 the same 100 px is over five times the RA of the equator");
        // 1/cos(80) is 5.759; the tangent plane adds a little on top.
        Near(dHi / dLo, 1.0 / std::cos(80.0 * 3.14159265358979 / 180.0), 0.02,
             "and the ratio is 1/cos(dec)");
    }

    // ---- parity -------------------------------------------------
    printf("\n4. A mirrored train puts east on the other side\n");
    {
        PlateSolution m = Plain();
        SetCdFromScaleRotation(m, +1.0 / 3600.0, 1.0 / 3600.0, 0.0);
        Check(IsMirrored(m), "a positive CDELT1 is flagged as mirrored");
        Near(ArcsecPerPixel(m), 1.0, 1e-9, "the scale is unchanged by the flip");

        const double plainRa = PixelToSky(Plain(), 599.5, 399.5).raHours;
        const double mirrRa  = PixelToSky(m,       599.5, 399.5).raHours;
        Check((plainRa - 10.0) * (mirrRa - 10.0) < 0.0,
              "and the same pixel lands on the opposite side of centre");

        DirectionRose rp, rm;
        FillSkyRose(rp, Plain());
        FillSkyRose(rm, m);
        Check(rp.skyEast.valid && rm.skyEast.valid, "both roses resolve east");
        Near(rp.skyEast.dx, -1.0, 1e-6, "unmirrored, east is screen-left");
        Near(rm.skyEast.dx, +1.0, 1e-6, "mirrored, east is screen-right");
        Near(rp.skyNorth.dy, -1.0, 1e-6, "north is up the screen either way");
        Near(rm.skyNorth.dy, -1.0, 1e-6, "including when mirrored");
    }

    // ---- rotation ------------------------------------------------
    printf("\n5. Rotating the camera rotates the rose with it\n");
    {
        PlateSolution s = Plain();
        SetCdFromScaleRotation(s, -1.0 / 3600.0, 1.0 / 3600.0, 90.0);
        DirectionRose r;
        FillSkyRose(r, s);
        Check(r.skyNorth.valid && r.skyEast.valid, "a rotated solve still has a rose");
        // 90 degrees of CROTA puts north along the screen x axis.
        Near(std::fabs(r.skyNorth.dx), 1.0, 1e-6, "north now runs across the screen");
        Near(std::fabs(r.skyNorth.dy), 0.0, 1e-6, "and no longer up it");
        Near(r.skyNorth.dx * r.skyEast.dx + r.skyNorth.dy * r.skyEast.dy, 0.0, 1e-6,
             "north and east stay perpendicular");
    }

    // ---- row order ------------------------------------------------
    printf("\n6. Row order is a flag, and it is the one that flips the answer\n");
    {
        PlateSolution up = Plain();
        PlateSolution dn = Plain();  dn.fitsBottomUp = false;
        const double decUp = PixelToSky(up, up.refX, up.refY - 100.0).decDeg;
        const double decDn = PixelToSky(dn, dn.refX, dn.refY - 100.0).decDeg;
        Check(decUp > 40.0 && decDn < 40.0,
              "the two conventions send the same pixel opposite ways in Dec");
        Near(decUp - 40.0, 40.0 - decDn, 1e-9,
             "by the same amount, so it is a reflection and nothing else");

        DirectionRose ru, rd;
        FillSkyRose(ru, up);
        FillSkyRose(rd, dn);
        Near(ru.skyNorth.dy, -rd.skyNorth.dy, 1e-6,
             "and the rose follows the flag rather than contradicting it");
    }

    // ---- wrap ------------------------------------------------------
    printf("\n7. RA wraps at 0h instead of going negative\n");
    {
        PlateSolution s = Plain();
        s.raHours = 0.0;
        s.decDeg  = 0.0;
        // CDELT1 negative, so screen-left is east: RA increases
        // leftward and DEcreases rightward. The wrap is the rightward
        // step, which would otherwise come out negative.
        //
        // 100 arcsec at dec 0 is 100/3600/15 h exactly, so both sides
        // are pinned to the value rather than to a range: a range here
        // would pass for an answer that had silently lost cos(dec).
        const double step = 100.0 / 3600.0 / 15.0;      // 0.00185185 h

        const SkyPoint e = PixelToSky(s, s.refX - 100.0, s.refY);
        Check(e.valid, "a pixel east of 0h resolves");
        Near(e.raHours, step, 1e-8, "and reads just over 0h");

        const SkyPoint w = PixelToSky(s, s.refX + 100.0, s.refY);
        Check(w.valid, "a pixel west of 0h resolves");
        Near(w.raHours, 24.0 - step, 1e-8,
             "and wraps to just under 24h rather than going negative");
    }

    // ---- refusals ---------------------------------------------------
    printf("\n8. What it refuses to answer\n");
    {
        PlateSolution s = Plain();
        Check(!PixelToSky(s, -1.0, 400.0).valid,   "a pixel left of the frame");
        Check(!PixelToSky(s, 1001.0, 400.0).valid, "a pixel right of the frame");
        Check(!PixelToSky(s, 500.0, 801.0).valid,  "a pixel below the frame");

        PlateSolution none;
        Check(!PixelToSky(none, 0.0, 0.0).valid, "no solve at all");
        Check(FormatPixelSky(PixelToSky(none, 0.0, 0.0)).empty(),
              "and the line is empty rather than a row of zeroes");

        PlateSolution deg = Plain();
        deg.cd[0][0] = deg.cd[0][1] = deg.cd[1][0] = deg.cd[1][1] = 0.0;
        DirectionRose r;
        FillSkyRose(r, deg);
        Check(!r.skyNorth.valid && !r.skyEast.valid,
              "a degenerate matrix produces no rose rather than a random one");
    }

    // ---- the jog rose, and whether it agrees -------------------------
    printf("\n9. The measured rose, and the check against the solve\n");
    {
        DirectionRose r;
        FillSkyRose(r, Plain());          // east is screen-left: (-1, 0)

        // An East pulse drives the scope east, so the image slides
        // west: screen-right, (+1, 0). That agrees.
        FillJogRose(r, 1.0f, 0.0f, 0.0f, 1.0f);
        Near(AgreementAngleDeg(r), 0.0, 1e-4,
             "a jog that pushes the image opposite to sky-east agrees");

        // The same calibration with its sign inverted must not pass.
        FillJogRose(r, -1.0f, 0.0f, 0.0f, -1.0f);
        Near(AgreementAngleDeg(r), 180.0, 1e-4,
             "an inverted convention reads as a full reversal");

        // Half a turn off, which is what a parity error looks like
        // once a rotation is involved.
        FillJogRose(r, 0.0f, 1.0f, 1.0f, 0.0f);
        Near(AgreementAngleDeg(r), 90.0, 1e-4, "and a quarter turn reads as 90");

        DirectionRose bare;
        Check(AgreementAngleDeg(bare) < 0.0,
              "with no solve the question is refused, not answered zero");
        FillJogRose(bare, 1.0f, 0.0f, 0.0f, 1.0f);
        Check(AgreementAngleDeg(bare) < 0.0,
              "and a jog rose alone is still not enough to check anything");

        DirectionRose jonly;
        FillJogRose(jonly, 0.0f, 0.0f, 0.0f, 0.0f);
        Check(!jonly.jogEast.valid,
              "an uncalibrated response gives no jog arrow");
    }

    // ---- wording ------------------------------------------------------
    printf("\n10. The coordinate reads the way a catalogue writes it\n");
    {
        Same(FormatRA(0.0),        "00h00m00.00s", "zero RA pads out");
        Same(FormatRA(10.5),       "10h30m00.00s", "half an hour is 30 minutes");
        Same(FormatDec(0.0),       "+00\xC2\xB0""00'00.0\"", "zero Dec keeps its sign");
        Same(FormatDec(-5.25),     "-05\xC2\xB0""15'00.0\"", "a southern declination");
        Same(FormatDec(41.26917),  "+41\xC2\xB0""16'09.0\"", "M31's declination");

        // The carry. 10h59m59.996s must not print as 59m60.00s.
        Same(FormatRA(10.0 + (59.0 * 60.0 + 59.996) / 3600.0),
             "11h00m00.00s", "a rounding carry rolls the hour over");
        Same(FormatRA(23.0 + (59.0 * 60.0 + 59.999) / 3600.0),
             "00h00m00.00s", "and at 24h it wraps rather than printing 24");
        Same(FormatDec(-(0.0 + (59.0 * 60.0 + 59.99) / 3600.0)),
             "-01\xC2\xB0""00'00.0\"", "the same carry southward keeps the minus");

        const PlateSolution s = Plain();
        const std::string line = FormatPixelSky(PixelToSky(s, s.refX, s.refY));
        Check(line.find("10h00m00.00s") == 0, "the tooltip line leads with RA");
        Check(line.find("+40") != std::string::npos, "and carries the declination");
    }

    // ---- the fallback path ----------------------------------------------
    printf("\n11. The scale-and-rotation fallback lands in the same place\n");
    {
        PlateSolution a = Plain();
        PlateSolution b = Plain();
        // What a solver reporting only CDELT/CROTA would give, built by
        // hand into the matrix: a 30 degree rotation.
        SetCdFromScaleRotation(a, -1.0 / 3600.0, 1.0 / 3600.0, 30.0);
        const double r = 30.0 * 3.14159265358979 / 180.0;
        b.cd[0][0] = -1.0 / 3600.0 * std::cos(r);
        b.cd[0][1] = -1.0 / 3600.0 * std::sin(r);
        b.cd[1][0] = -1.0 / 3600.0 * std::sin(r);
        b.cd[1][1] =  1.0 / 3600.0 * std::cos(r);
        const SkyPoint pa = PixelToSky(a, 600.0, 300.0);
        const SkyPoint pb = PixelToSky(b, 600.0, 300.0);
        Near(pa.raHours, pb.raHours, 1e-12, "the helper matches the matrix in RA");
        Near(pa.decDeg,  pb.decDeg,  1e-12, "and in Dec");
        Check(IsMirrored(a) == false, "a rotation alone does not mirror anything");
    }

    // ---- the reference pixel conversion --------------------------------
    printf("\n12. CRPIX is 1-based and bottom-up; the buffer is neither\n");
    {
        PlateSolution s;
        s.frameW = 1000; s.frameH = 800;

        // The true centre of an 800-row frame is FITS row 400.5, which
        // is buffer row 399.5. Off by one here is half the frame in the
        // readout before anybody notices.
        SetReferencePixelFromCrpix(s, 500.5, 400.5);
        Near(s.refX, 499.5, 1e-12, "CRPIX1 500.5 is buffer column 499.5");
        Near(s.refY, 399.5, 1e-12, "CRPIX2 400.5 is buffer row 399.5");

        // The very first FITS row is the LAST buffer row.
        SetReferencePixelFromCrpix(s, 1.0, 1.0);
        Near(s.refX,   0.0, 1e-12, "CRPIX1 1 is buffer column 0");
        Near(s.refY, 799.0, 1e-12, "CRPIX2 1 is the bottom buffer row, not the top");

        s.fitsBottomUp = false;
        SetReferencePixelFromCrpix(s, 1.0, 1.0);
        Near(s.refY, 0.0, 1e-12, "and with the flag off it is the top row");
    }

    printf("\n13. Building a solution, with and without the keywords\n");
    {
        // No CRPIX: the reference is the middle of the frame, and the
        // middle of an even-sided frame is a half pixel.
        const PlateSolution a = MakePlateSolution(
            10.0, 40.0, 1000, 800,
            false, 0, 0, 0, 0,
            -1.0 / 3600.0, 1.0 / 3600.0, 0.0,
            false, 0, 0);
        Check(a.valid, "a CDELT-only solve still builds");
        Near(a.refX, 499.5, 1e-12, "and centres on the frame in x");
        Near(a.refY, 399.5, 1e-12, "and in y");
        Near(ArcsecPerPixel(a), 1.0, 1e-9, "at the scale it was given");

        // The CD matrix wins when it is there, even if CDELT disagrees.
        const PlateSolution b = MakePlateSolution(
            10.0, 40.0, 1000, 800,
            true, -2.0 / 3600.0, 0.0, 0.0, 2.0 / 3600.0,
            -1.0 / 3600.0, 1.0 / 3600.0, 0.0,
            false, 0, 0);
        Near(ArcsecPerPixel(b), 2.0, 1e-9,
             "the matrix wins over a CDELT that disagrees with it");

        // Degenerate input must not produce a confident solution.
        const PlateSolution z = MakePlateSolution(
            10.0, 40.0, 1000, 800, true, 0, 0, 0, 0, 0, 0, 0, false, 0, 0);
        Check(!z.valid, "an all-zero matrix is refused rather than built");
        const PlateSolution n = MakePlateSolution(
            10.0, 40.0, 0, 0, false, 0, 0, 0, 0,
            -1.0 / 3600.0, 1.0 / 3600.0, 0.0, false, 0, 0);
        Check(!n.valid, "and so is a frame with no size");
    }

    printf("\n14. The solver's keywords, including the ones it may omit\n");
    {
        // CD matrix present: parity comes off the determinant, and the
        // field comes off the matrix rather than off CDELT.
        WcsKeys k;
        k.crval1 = 150.0; k.crval2 = 40.0;
        k.cd11 = -1.0 / 3600.0; k.cd12 = 0.0;
        k.cd21 =  0.0;          k.cd22 = 1.0 / 3600.0;
        k.cdSeen = 4;
        SolveResult r; r.solved = true;
        ApplyWcsKeys(r, k, 1000, 800);
        Near(r.ra,  10.0, 1e-12, "CRVAL1 in degrees becomes RA in hours");
        Near(r.dec, 40.0, 1e-12, "and CRVAL2 is Dec as it stands");
        Check(r.hasCd, "four CD keys count as a matrix");
        Near(r.fovW, 1000.0 / 3600.0, 1e-12, "the field width comes off the matrix");
        Check(r.degPerPxX < 0.0,
              "a negative determinant derives an ordinary, unmirrored scale");

        // Three of the four is not a matrix.
        WcsKeys partial = k; partial.cdSeen = 3;
        SolveResult rp; rp.solved = true;
        partial.cdelt1 = -1.0 / 3600.0; partial.cdelt2 = 1.0 / 3600.0;
        partial.hasCdelt = true;
        ApplyWcsKeys(rp, partial, 1000, 800);
        Check(!rp.hasCd, "three CD keys are not a matrix");
        Near(rp.fovW, 1000.0 / 3600.0, 1e-12, "and the field falls back to CDELT");

        // Mirrored: positive determinant.
        WcsKeys m = k; m.cd11 = +1.0 / 3600.0;
        SolveResult rm; rm.solved = true;
        ApplyWcsKeys(rm, m, 1000, 800);
        Check(rm.degPerPxX > 0.0, "a positive determinant is flagged as mirrored");
        Near(rm.fovW, 1000.0 / 3600.0, 1e-12, "with the field unchanged by the flip");

        // CDELT present alongside the matrix: it is kept as reported,
        // rather than overwritten by the derivation.
        WcsKeys both = k;
        both.cdelt1 = -3.0 / 3600.0; both.cdelt2 = 3.0 / 3600.0;
        both.hasCdelt = true;
        SolveResult rb; rb.solved = true;
        ApplyWcsKeys(rb, both, 1000, 800);
        Near(rb.degPerPxX, -3.0 / 3600.0, 1e-15,
             "a reported CDELT is kept as reported");
        Near(rb.fovW, 1000.0 / 3600.0, 1e-12,
             "but the field still comes off the matrix");

        // The rotation keyword nobody agrees on.
        WcsKeys r1; r1.crota1 = 17.0; r1.hasCrota1 = true;
        r1.cdelt1 = -1.0 / 3600.0; r1.cdelt2 = 1.0 / 3600.0; r1.hasCdelt = true;
        SolveResult rr1; rr1.solved = true;
        ApplyWcsKeys(rr1, r1, 1000, 800);
        Near(rr1.rotation, 17.0, 1e-12, "CROTA1 alone is taken");

        WcsKeys r2 = r1; r2.crota2 = 23.0; r2.hasCrota2 = true;
        SolveResult rr2; rr2.solved = true;
        ApplyWcsKeys(rr2, r2, 1000, 800);
        Near(rr2.rotation, 23.0, 1e-12, "but CROTA2 wins when both are written");

        WcsKeys r0 = r1; r0.hasCrota1 = false;
        SolveResult rr0; rr0.solved = true;
        ApplyWcsKeys(rr0, r0, 1000, 800);
        Near(rr0.rotation, 0.0, 1e-12, "and neither means no rotation, not a stale one");
    }

    printf("\n15. Solve to solution, end to end\n");
    {
        SolveResult r;
        r.solved = true;
        WcsKeys k;
        k.crval1 = 150.0; k.crval2 = 40.0;
        k.cd11 = -1.0 / 3600.0; k.cd22 = 1.0 / 3600.0; k.cdSeen = 4;
        k.crpix1 = 500.5; k.crpix2 = 400.5; k.crpixSeen = 2;
        ApplyWcsKeys(r, k, 1000, 800);

        const PlateSolution s = PlateSolutionFromSolve(r, 1000, 800);
        Check(s.valid, "a solved frame becomes a usable solution");
        const SkyPoint c = PixelToSky(s, 499.5, 399.5);
        Near(c.raHours, 10.0, 1e-9, "and the reference pixel reads the solved RA");
        Near(c.decDeg,  40.0, 1e-9, "and the solved Dec");

        SolveResult unsolved;
        Check(!PlateSolutionFromSolve(unsolved, 1000, 800).valid,
              "a failed solve produces nothing to measure against");
    }

    printf("\n16. A solve expires, and an alt-az one expires by rotating\n");
    {
        const double rate = DefaultFieldRotationDegPerHour();   // 10 deg/hr

        const SolveAge fresh = AgeSolve(5.0, rate);
        Check(fresh.usable && !fresh.stale, "a five second old solve is simply good");

        // A degree of field rotation at 10 deg/hr takes six minutes.
        const SolveAge edge = AgeSolve(6.0 * 60.0 - 1.0, rate);
        Check(edge.usable && !edge.stale, "just under a degree of turn is still good");
        const SolveAge turned = AgeSolve(6.0 * 60.0 + 1.0, rate);
        Check(turned.usable && turned.stale,
              "just over it is usable but flagged, not hidden");
        Near(AgeSolve(3600.0, rate).fieldTurnedDeg, 10.0, 1e-9,
             "an hour at 10 deg/hr is ten degrees of field rotation");

        // The ceiling bites regardless of rotation.
        const SolveAge old1 = AgeSolve(3601.0, rate);
        Check(!old1.usable, "past an hour nothing is drawn at all");
        const SolveAge eq = AgeSolve(3601.0, 0.0);
        Check(!eq.usable,
              "and an equatorial mount with no field rotation is no exception");

        // An equatorial mount ages only against the ceiling.
        const SolveAge eqOk = AgeSolve(1800.0, 0.0);
        Check(eqOk.usable && !eqOk.stale,
              "but inside the hour it never goes stale by rotation");
        Near(eqOk.fieldTurnedDeg, 0.0, 1e-12, "because there is none");

        Check(!AgeSolve(-1.0, rate).usable,
              "a negative age is refused rather than treated as fresh");
    }

    printf("\n17. The age reads as a duration\n");
    {
        Same(FormatAge(0.0),    "0s",     "a fresh solve");
        Same(FormatAge(45.0),   "45s",    "under a minute stays in seconds");
        Same(FormatAge(59.6),   "1m00s",  "and rounds up into the next minute");
        Same(FormatAge(252.0),  "4m12s",  "minutes and seconds, zero padded");
        Same(FormatAge(3600.0), "1h00m",  "an hour switches to hours");
        Same(FormatAge(4215.0), "1h10m",  "and keeps the minutes");
        Same(FormatAge(-1.0),   "?",      "a nonsensical age says so");
    }

    if (g_fail == 0) printf("\nPASS\n"); else printf("\nFAIL: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
