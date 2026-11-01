// ============================================================
//  The solved plate scale against rig.ini
//
//  The emulator has no field that solves, so this line cannot be
//  seen before a real solve under the sky. What can be settled on a
//  bench is that the number is right when it arrives:
//
//    The solve is fed through ApplyWcsKeys, the path ASTAP's output
//    takes, so the scale here comes off a CD matrix or a CDELT and
//    not off a fovW the test made up.
//
//    A crop measures the same scale as the full sensor. Dividing by
//    the sensor width instead would read a 640 px frame as a sixth
//    of the truth and tell the operator to write that to rig.ini.
//
//    A long focal length is a SMALL scale. The two move opposite
//    ways, and copying the scale's percentage onto the focal length
//    gets the sign wrong.
// ============================================================

#include "sky/PlateScaleCheck.h"

#include <cmath>
#include <cstdio>
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

static bool Has(const std::string& s, const char* want) {
    return s.find(want) != std::string::npos;
}

// A solved result as ASTAP's keywords would produce it, rotated by
// rotDeg, at scale arcsec/px.
static SolveResult FromCd(double scale, double rotDeg, unsigned w, unsigned h,
                          bool mirrored = false) {
    const double a = rotDeg * 3.14159265358979 / 180.0;
    const double s = scale / 3600.0;
    const double flip = mirrored ? 1.0 : -1.0;
    WcsKeys k;
    k.cd11 = flip * s * std::cos(a);  k.cd12 = s * std::sin(a);
    k.cd21 = flip * s * std::sin(a);  k.cd22 = -s * std::cos(a);
    k.cdSeen = 4;
    SolveResult r;
    ApplyWcsKeys(r, k, w, h);
    r.solved = true;
    return r;
}

static SolveResult FromCdelt(double scale, unsigned w, unsigned h) {
    WcsKeys k;
    k.cdelt1 = -scale / 3600.0;
    k.cdelt2 =  scale / 3600.0;
    k.hasCdelt = true;
    SolveResult r;
    ApplyWcsKeys(r, k, w, h);
    r.solved = true;
    return r;
}

static void TestScale() {
    printf("the scale comes off the solve\n");
    Near(CheckPlateScale(FromCd(0.2150, 0, 3840, 2160), 3840, 0.203, 2.0).solved,
         0.2150, 1e-9, "from a CD matrix");
    Near(CheckPlateScale(FromCd(0.2150, 37, 3840, 2160), 3840, 0.203, 2.0).solved,
         0.2150, 1e-9, "unchanged by rotation");
    Near(CheckPlateScale(FromCd(0.2150, 37, 3840, 2160, true), 3840, 0.203, 2.0).solved,
         0.2150, 1e-9, "unchanged by a mirrored train");
    Near(CheckPlateScale(FromCdelt(0.2150, 3840, 2160), 3840, 0.203, 2.0).solved,
         0.2150, 1e-9, "from the CDELT fallback");
    Near(CheckPlateScale(FromCd(0.2150, 0, 640, 480), 640, 0.203, 2.0).solved,
         0.2150, 1e-9, "a crop measures the same scale as the sensor");
}

static void TestFlag() {
    printf("the flag\n");
    const auto at = [](double scale) {
        return CheckPlateScale(FromCd(scale, 0, 3840, 2160), 3840, 0.2, 2.0);
    };
    Near(at(0.2060).offPct, 3.0, 1e-6, "percent of rig.ini, not of the solve");
    Near(at(0.1940).offPct, 3.0, 1e-6, "a magnitude, either side");
    Check(!at(0.2099).flagged, "4.95% is not flagged");
    Check( at(0.2101).flagged, "5.05% is flagged");
    Check(!at(0.1901).flagged, "4.95% low is not flagged");
    Check( at(0.1899).flagged, "5.05% low is flagged");
    Check(!at(0.2000).flagged, "a corrected rig.ini reads clean");
    Near(at(0.2000).offPct, 0.0, 1e-9, "and reads zero");

    const PlateScaleCheck noAssumed =
        CheckPlateScale(FromCd(0.2, 0, 3840, 2160), 3840, 0.0, 2.0);
    Near(noAssumed.offPct, 0.0, 1e-9, "no rig.ini figure divides by nothing");
}

static void TestFocalLength() {
    printf("the implied focal length\n");
    Near(CheckPlateScale(FromCd(206.265 * 2.0 / 2032.0, 0, 3840, 2160), 3840, 0.203, 2.0).focalMm,
         2032.0, 1e-6, "the 8SE's nominal figure at the nominal scale");
    const double lo = CheckPlateScale(FromCd(0.190, 0, 3840, 2160), 3840, 0.203, 2.0).focalMm;
    const double hi = CheckPlateScale(FromCd(0.220, 0, 3840, 2160), 3840, 0.203, 2.0).focalMm;
    Check(lo > hi, "a smaller scale is a longer focal length");
    Near(CheckPlateScale(FromCd(0.2, 0, 3840, 2160), 3840, 0.203, 2.9).focalMm,
         206.265 * 2.9 / 0.2, 1e-6, "the pixel is the camera's, not a literal");
    Near(CheckPlateScale(FromCd(0.2, 0, 3840, 2160), 3840, 0.203, 0.0).focalMm,
         0.0, 1e-12, "an unknown pixel gives no focal length");
}

static void TestLine() {
    printf("the line\n");
    const std::string ok = FormatPlateScale(
        CheckPlateScale(FromCd(0.2061, 0, 3840, 2160), 3840, 0.203, 2.0));
    Check(Has(ok, "Plate scale: solved 0.2061\"/px vs rig.ini 0.2030\"/px"),
          "both figures, as the checklist shows them");
    Check(Has(ok, "(1.53% out)"), "two decimals on the percentage");
    Check(Has(ok, ", focal length 2002 mm"), "the focal length");
    Check(!Has(ok, "<-"), "no instruction under the threshold");

    const std::string bad = FormatPlateScale(
        CheckPlateScale(FromCd(0.2480, 0, 3840, 2160), 3840, 0.203, 2.0));
    Check(Has(bad, "<- set PlateScaleArcsecPx to the solved value"),
          "the instruction over it");

    const std::string noPixel = FormatPlateScale(
        CheckPlateScale(FromCd(0.2061, 0, 3840, 2160), 3840, 0.203, 0.0));
    Check(!Has(noPixel, "focal length"), "no focal length without a pixel size");
}

static void TestNoAnswer() {
    printf("nothing to say\n");
    SolveResult failed = FromCd(0.2, 0, 3840, 2160);
    failed.solved = false;
    Check(FormatPlateScale(CheckPlateScale(failed, 3840, 0.203, 2.0)).empty(),
          "a failed solve prints nothing");
    SolveResult noField;
    noField.solved = true;
    Check(FormatPlateScale(CheckPlateScale(noField, 3840, 0.203, 2.0)).empty(),
          "a solve with no field prints nothing");
    Check(FormatPlateScale(CheckPlateScale(FromCd(0.2, 0, 3840, 2160), 0, 0.203, 2.0)).empty(),
          "a zero-width frame prints nothing");
}

int main() {
    TestScale();
    TestFlag();
    TestFocalLength();
    TestLine();
    TestNoAnswer();
    printf("\n%s (%d failed)\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
