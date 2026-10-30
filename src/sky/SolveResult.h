#pragma once
// ============================================================
// SolveResult.h - what a plate solve hands back, and the one
//                 piece of arithmetic between the solver's
//                 keywords and that answer
//
// Split out of ASTAPSolver.h in phase 47b. Not for tidiness: the
// derivation below decides the PARITY of the image, and a parity
// error produces a coordinate readout that is confidently
// mirrored about the frame centre with nothing about it that
// looks wrong. That has to be testable, and ASTAPSolver.h needs
// windows.h and a running ASTAP, so nothing in it ever is.
//
// This header needs neither, so tests/pixel_wcs.cpp compiles it.
// ============================================================

#include <cmath>
#include <string>

namespace raduk::sky {

struct SolveResult {
    bool   solved   = false;
    double ra       = 0.0;  // hours, J2000
    double dec      = 0.0;  // degrees, J2000
    double fovW     = 0.0;  // field width, degrees (magnitude)
    double fovH     = 0.0;  // field height, degrees (magnitude)
    double rotation = 0.0;  // degrees E of N
    std::string error;

    // ── Phase 47b: the rest of the WCS ───────────────────
    //
    // fovW/fovH are magnitudes, which is all the panels ever
    // wanted. Naming the pixel under the cursor needs the part
    // that used to be thrown away: the sign of CDELT1 is the
    // parity of the optical train, and without it a mirrored
    // train reads east for west with no outward sign of trouble.
    bool   hasCd    = false;
    double cd1_1    = 0.0, cd1_2 = 0.0;
    double cd2_1    = 0.0, cd2_2 = 0.0;

    bool   hasCrpix = false;
    double crpix1   = 0.0, crpix2 = 0.0;   // FITS: 1-based, bottom-up

    // Signed, unlike fovW/fovH. Negative X is the ordinary sky
    // image; positive means mirrored.
    double degPerPxX = 0.0, degPerPxY = 0.0;
};

// The WCS keywords exactly as they came off the file, with a flag
// for each one that might be absent. No derivation, no defaults:
// the point of carrying "was it there" separately from the value
// is that a missing keyword and a keyword that really is zero are
// different, and only one of them is a reason to fall back.
struct WcsKeys {
    double crval1 = 0.0, crval2 = 0.0;
    double cdelt1 = 0.0, cdelt2 = 0.0;
    double crota1 = 0.0, crota2 = 0.0;
    double cd11 = 0.0, cd12 = 0.0, cd21 = 0.0, cd22 = 0.0;
    double crpix1 = 0.0, crpix2 = 0.0;

    bool hasCdelt  = false;
    bool hasCrota1 = false;
    bool hasCrota2 = false;
    int  cdSeen    = 0;      // all four, or none of them count
    int  crpixSeen = 0;
};

// Keywords into a result. Everything convention-sensitive about
// an ASTAP solve happens in these thirty lines.
//
// Three decisions, each the kind that goes unnoticed:
//
//   The CD matrix wins when all four of it are present. It
//   carries scale, rotation and parity as one object, so it needs
//   no agreement about which rotation keyword is the real one --
//   and there is no agreement: ASTAP's own consumers variously
//   read CROTA1 and CROTA2.
//
//   fovW/fovH come off the matrix when there is one, because a
//   solver that writes both can write a CDELT that disagrees, and
//   the matrix is what the readout will be drawing from. Two
//   numbers for the same field is how a panel and a cursor end up
//   telling different stories.
//
//   Parity falls out of the determinant when CDELT is absent. A
//   negative determinant is the ordinary sky image.
inline void ApplyWcsKeys(SolveResult& r, const WcsKeys& k,
                         unsigned imgW, unsigned imgH) {
    r.ra  = k.crval1 / 15.0;      // degrees -> hours
    r.dec = k.crval2;

    r.rotation = k.hasCrota2 ? k.crota2 : (k.hasCrota1 ? k.crota1 : 0.0);

    r.hasCd = (k.cdSeen == 4);
    if (r.hasCd) {
        r.cd1_1 = k.cd11; r.cd1_2 = k.cd12;
        r.cd2_1 = k.cd21; r.cd2_2 = k.cd22;
    }

    r.hasCrpix = (k.crpixSeen == 2);
    r.crpix1   = k.crpix1;
    r.crpix2   = k.crpix2;

    r.degPerPxX = k.cdelt1;       // signed: the parity
    r.degPerPxY = k.cdelt2;

    if (r.hasCd) {
        r.fovW = std::sqrt(k.cd11 * k.cd11 + k.cd21 * k.cd21) * (double)imgW;
        r.fovH = std::sqrt(k.cd12 * k.cd12 + k.cd22 * k.cd22) * (double)imgH;
        if (!k.hasCdelt) {
            const double det = k.cd11 * k.cd22 - k.cd12 * k.cd21;
            r.degPerPxX = (det > 0.0 ? 1.0 : -1.0)
                        * std::sqrt(k.cd11 * k.cd11 + k.cd21 * k.cd21);
            r.degPerPxY = std::sqrt(k.cd12 * k.cd12 + k.cd22 * k.cd22);
        }
    } else {
        r.fovW = std::abs(k.cdelt1) * (double)imgW;
        r.fovH = std::abs(k.cdelt2) * (double)imgH;
    }
}

} // namespace raduk::sky
