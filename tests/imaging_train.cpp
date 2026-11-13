// ============================================================
//  Phase 149.0: one description of the camera and optics (ImagingTrain.h)
//
//    1. The ASI678MC at 2032 mm gives the 0.203"/px every figure used,
//       and its full-sensor field (0.217 x 0.122 deg).
//    2. A 26 MP APS-C camera (3.76 um) gives its own scale and field.
//    3. A reducer and a Barlow scale it; a measured figure overrides it.
//    4. The rig line, the focal ratio, and nothing on bad input.
// ============================================================

#include "core/ImagingTrain.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-58s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}
static std::string F(double v, int d = 4) { char b[48]; snprintf(b, sizeof(b), "%.*f", d, v); return b; }

int main() {
    printf("Imaging train (phase 149.0)\n");
    ImagingTrain t;
    t.camera = "ZWO ASI678MC"; t.telescope = "Celestron 8SE";
    t.sensorW = 3840; t.sensorH = 2160; t.pixelUm = 2.0; t.focalMm = 2032.0;
    Check(std::fabs(t.ArcsecPx() - 0.2030) < 0.0005, "678 at 2032 mm: 0.203\"/px", F(t.ArcsecPx()));
    Check(std::string(t.ScaleSource()).find("derived") != std::string::npos, "and says it is derived");
    Check(std::fabs(t.FovWidthDeg() - 0.2165) < 0.001 && std::fabs(t.FovHeightDeg() - 0.1218) < 0.001,
          "full-sensor field 0.217 x 0.122 deg", F(t.FovWidthDeg(), 3) + " x " + F(t.FovHeightDeg(), 3));
    Check(std::fabs(t.FovDiagDeg() - 0.2484) < 0.001, "and 0.248 deg diagonal (not the 0.13 once typed in)",
          F(t.FovDiagDeg(), 3));
    Check(std::fabs(t.FovWidthDeg(1280) - 1280 * 0.20302 / 3600.0) < 1e-4, "the field of a crop");

    ImagingTrain a = t;
    a.camera = "APS-C 26 MP"; a.sensorW = 6248; a.sensorH = 4176; a.pixelUm = 3.76; a.color = false;
    Check(std::fabs(a.ArcsecPx() - 0.3817) < 0.0005, "a 26 MP APS-C (3.76 um): 0.382\"/px", F(a.ArcsecPx()));
    Check(std::fabs(a.FovWidthDeg() - 0.6624) < 0.002, "and its own field", F(a.FovWidthDeg(), 3));

    ImagingTrain r = t; r.reducer = 0.63;
    Check(std::fabs(r.ArcsecPx() - 0.20302 / 0.63) < 0.0005, "a 0.63 reducer: 0.322\"/px", F(r.ArcsecPx()));
    ImagingTrain b = t; b.reducer = 2.0;
    Check(std::fabs(b.ArcsecPx() - 0.10151) < 0.0005, "a 2x Barlow: 0.102\"/px", F(b.ArcsecPx()));
    ImagingTrain m = t; m.measuredArcsecPx = 0.2104;
    Check(m.ArcsecPx() == 0.2104 && std::string(m.ScaleSource()) == "measured", "a measured figure wins");

    Check(t.RigLine() == "Celestron 8SE  \xc2\xb7  ZWO ASI678MC", "the rig line names both");
    ImagingTrain n = t; n.telescope.clear();
    Check(n.RigLine() == "ZWO ASI678MC", "with no telescope, the camera alone");
    Check(std::fabs(t.FocalRatio(203.2) - 10.0) < 0.01, "f/10 at 203.2 mm", F(t.FocalRatio(203.2), 2));
    ImagingTrain z; z.focalMm = 2032;
    Check(z.ArcsecPx() == 0.0 && std::string(z.ScaleSource()) == "unknown", "no pixel size: no scale, said so");
    Check(t.Describe().find("0.203\"/px") != std::string::npos, "the log line gives the scale", t.Describe().substr(0, 60));

    printf(g_fail ? "\n%d FAILED\n" : "\nall passed\n", g_fail);
    return g_fail ? 1 : 0;
}
