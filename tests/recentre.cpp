// ============================================================
//  Click to recentre: does the mount go the way the click asked?
//
//  Phase 47 turns a clicked pixel into two guide pulses, using the
//  phase 51 calibration matrix. That matrix is the only thing in the
//  app that knows which way is north in the frame -- it was measured
//  by pulsing and watching where the centroid went -- so the
//  arithmetic that inverts it is where a sign error would hide. And a
//  sign error here does not look like a bug: the telescope moves
//  smoothly and confidently the wrong way, and the target leaves the
//  frame.
//
//  The strongest check is the round trip. Solve for the pulses, then
//  push them back through the forward matrix the calibration measured
//  and see whether the clicked feature lands in the middle. That
//  catches a transpose, a swapped sign and a determinant mistake
//  without needing a telescope or a clear night.
// ============================================================

#include "core/GuideSolve.h"

#include <cstdio>
#include <cmath>
#include <string>

using namespace raduk::core;

static int g_fail = 0;

static void Check(bool ok, const std::string& what, const std::string& detail) {
    printf("  [%s] %-54s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

// Where the image ends up if these pulses are sent, according to the
// same response the solve was given. The forward direction:
// pixels = M * milliseconds.
static void Forward(const GuideResponse& r, float tE, float tN,
                    float& dx, float& dy) {
    dx = r.exPerMs * tE + r.nxPerMs * tN;
    dy = r.eyPerMs * tE + r.nyPerMs * tN;
}

int main() {
    printf("\n=== Click to recentre ===\n");

    const int FW = 1920, FH = 1080;
    const float SCALE = 0.203f;           // arcsec/px, as this rig is set up

    // A camera square in the focuser: an East pulse slides the image
    // along +x, a North pulse along +y, both at 0.1 px/ms.
    const GuideResponse square{ 0.1f, 0.0f, 0.0f, 0.1f };

    printf("\n1. The simple case, camera square\n");
    {
        char b[160];
        // A click 50 px right of centre. To put it in the middle the
        // image must travel 50 px LEFT, which is a West pulse.
        RecentrePlan p = PlanRecentre(FW * 0.5f + 50.0f, FH * 0.5f,
                                      FW, FH, square, SCALE, 10000);
        Check(p.valid, "a click right of centre plans a move", p.why);
        Check(p.eastMs < 0, "and it is westward, not eastward",
              (snprintf(b, sizeof(b), "E/W %dms", p.eastMs), b));
        Check(p.eastMs == -500 && p.northMs == 0,
              "500 ms of it, with nothing on the other axis",
              (snprintf(b, sizeof(b), "E/W %dms  N/S %dms", p.eastMs, p.northMs), b));
        Check(std::fabs(p.offsetArcsec - 50.0f * SCALE) < 0.01f,
              "the offset is reported in arcsec, via the plate scale",
              (snprintf(b, sizeof(b), "%.2f\"", p.offsetArcsec), b));

        // Below centre on screen: y grows downward, so the image has
        // to travel up the screen, which is -y.
        RecentrePlan q = PlanRecentre(FW * 0.5f, FH * 0.5f + 80.0f,
                                      FW, FH, square, SCALE, 10000);
        Check(q.valid && q.northMs == -800 && q.eastMs == 0,
              "a click below centre moves only the other axis",
              (snprintf(b, sizeof(b), "E/W %dms  N/S %dms", q.eastMs, q.northMs), b));
    }

    printf("\n2. A rotated camera -- where an assumption would break\n");
    {
        // Camera turned 90 degrees in the focuser: an East pulse now
        // moves the image DOWN the screen and a North pulse moves it
        // left. Nothing in the app is told this; the calibration
        // measured it, and the solve just uses it.
        const GuideResponse rot90{ 0.0f, 0.1f, -0.1f, 0.0f };
        char b[160];
        RecentrePlan p = PlanRecentre(FW * 0.5f + 50.0f, FH * 0.5f,
                                      FW, FH, rot90, SCALE, 10000);
        Check(p.valid, "a rotated calibration still plans a move", p.why);
        Check(p.eastMs == 0 && p.northMs == 500,
              "the same click now uses the OTHER axis entirely",
              (snprintf(b, sizeof(b), "E/W %dms  N/S %dms", p.eastMs, p.northMs), b));
    }

    printf("\n3. The round trip, over the whole frame\n");
    {
        // Four calibrations: square, rotated, mirrored (a negative
        // determinant, which an optical train with an odd number of
        // reflections really produces) and a skewed one.
        struct Case { const char* name; GuideResponse r; };
        const Case cases[] = {
            { "square",   { 0.100f,  0.000f,  0.000f, 0.100f } },
            { "rotated",  { 0.000f,  0.100f, -0.100f, 0.000f } },
            { "mirrored", { 0.100f,  0.000f,  0.000f,-0.100f } },
            { "skewed",   { 0.082f, -0.031f,  0.028f, 0.090f } },
        };
        for (const Case& c : cases) {
            float worst = 0.0f;
            int planned = 0, refused = 0;
            for (int gx = 1; gx < 10; gx++) {
                for (int gy = 1; gy < 10; gy++) {
                    const float cx = (float)FW * gx / 10.0f;
                    const float cy = (float)FH * gy / 10.0f;
                    RecentrePlan p = PlanRecentre(cx, cy, FW, FH, c.r,
                                                  SCALE, 60000);
                    if (!p.valid) { refused++; continue; }
                    planned++;
                    float dx = 0.0f, dy = 0.0f;
                    Forward(c.r, (float)p.eastMs, (float)p.northMs, dx, dy);
                    // Where the clicked feature ends up.
                    const float ex = cx + dx - FW * 0.5f;
                    const float ey = cy + dy - FH * 0.5f;
                    const float err = std::sqrt(ex * ex + ey * ey);
                    if (err > worst) worst = err;
                }
            }
            char b[160];
            Check(planned >= 80 && worst < 1.0f,
                  std::string(c.name) + ": every click lands in the middle",
                  (snprintf(b, sizeof(b),
                            "%d clicks, %d refused, worst miss %.3f px",
                            planned, refused, worst), b));
        }
    }

    printf("\n4. What it refuses, and why\n");
    {
        char b[160];
        RecentrePlan centre = PlanRecentre(FW * 0.5f, FH * 0.5f, FW, FH,
                                           square, SCALE, 10000);
        Check(!centre.valid && std::string(centre.why) == "already centred",
              "a click on the centre is not a move", centre.why);

        // Parallel responses: the calibration cannot tell the axes
        // apart. Solving it would hand back enormous pulses in
        // confident directions.
        const GuideResponse parallel{ 0.1f, 0.1f, 0.2f, 0.2f };
        RecentrePlan deg = PlanRecentre(FW * 0.5f + 100.0f, FH * 0.5f,
                                        FW, FH, parallel, SCALE, 10000);
        Check(!deg.valid, "a degenerate calibration is refused, not solved",
              deg.why);

        // Too far for a pulse. It must REFUSE rather than clamp: a
        // clamped pulse lands somewhere nobody asked for, and at
        // 0.2"/px the target then leaves the frame.
        const GuideResponse slow{ 0.002f, 0.0f, 0.0f, 0.002f };
        RecentrePlan far = PlanRecentre(FW * 0.5f + 900.0f, FH * 0.5f,
                                        FW, FH, slow, SCALE, 10000);
        Check(!far.valid, "a move beyond the pulse limit is refused", far.why);
        Check(far.wantedMs > 10000.0f,
              "and it reports how long it would have taken",
              (snprintf(b, sizeof(b), "%.0f ms wanted", far.wantedMs), b));

        // A move shorter than any mount will honour.
        const GuideResponse fast{ 50.0f, 0.0f, 0.0f, 50.0f };
        RecentrePlan tiny = PlanRecentre(FW * 0.5f + 4.0f, FH * 0.5f,
                                         FW, FH, fast, SCALE, 10000);
        Check(!tiny.valid, "so is one below the shortest pulse", tiny.why);

        RecentrePlan off = PlanRecentre(-5.0f, 10.0f, FW, FH, square,
                                        SCALE, 10000);
        Check(!off.valid, "a click outside the frame is not a target", off.why);

        RecentrePlan noframe = PlanRecentre(10.0f, 10.0f, 0, 0, square,
                                            SCALE, 10000);
        Check(!noframe.valid, "and neither is a click with no frame",
              noframe.why);
    }

    printf("\n5. Altitude rescale on an alt-az mount\n");
    {
        char b[160];
        const GuideResponse base{ 0.1f, 0.02f, 0.0f, 0.1f };
        // Calibrated at 30 deg, now at 60: cos(60)/cos(30) = 0.577.
        const GuideResponse up = RescaleForAltitude(base, 30.0f, 60.0f);
        const float k = std::cos(60.0f * 0.01745329f) /
                        std::cos(30.0f * 0.01745329f);
        Check(std::fabs(up.exPerMs - base.exPerMs * k) < 1e-6f &&
              std::fabs(up.eyPerMs - base.eyPerMs * k) < 1e-6f,
              "the azimuth column scales by cos(now)/cos(calibrated)",
              (snprintf(b, sizeof(b), "x%.3f", k), b));
        Check(up.nxPerMs == base.nxPerMs && up.nyPerMs == base.nyPerMs,
              "and the declination column is left alone", "");

        const GuideResponse unknown = RescaleForAltitude(base, -1.0f, 60.0f);
        Check(unknown.exPerMs == base.exPerMs,
              "an unknown calibration altitude rescales nothing", "");

        // Near the zenith cos goes to zero, and rescaling by it would
        // divide the measured response away entirely.
        const GuideResponse zenith = RescaleForAltitude(base, 89.9f, 45.0f);
        Check(zenith.exPerMs == base.exPerMs,
              "a calibration taken at the zenith is not trusted to scale", "");
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
