// ============================================================
//  full_frame_lock -- phase 126
//
//  The real FrameAnalyzer, fed a synthetic lunar surface that fills the
//  frame and moves the way a guide pulse moves it. Before phase 126 the
//  centroid of such a frame stayed at the middle whatever happened,
//  which is why guide calibration on the Moon reported "North response
//  too small" forever. Now it must follow the surface: report movement
//  in the right direction and size, hold still when the mount does,
//  and fall back to the ordinary centroid for a disc inside the frame.
// ============================================================

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>
#include "core/FrameAnalyzer.h"
#include "core/FrameSlot.h"

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) ++g_fail;
}

// Craters and maria: smooth noise at a few scales, bright overall, so
// every edge pixel is well over half the peak (the frame reads as full).
static float Terrain(float x, float y) {
    float v = 0.0f;
    v += 18.0f * std::sin(x * 0.031f + 1.3f) * std::cos(y * 0.027f + 0.4f);
    v += 12.0f * std::sin(x * 0.083f + y * 0.051f);
    v += 9.0f  * std::cos(x * 0.13f - y * 0.11f + 2.0f);
    v += 6.0f  * std::sin(x * 0.29f + 0.7f) * std::sin(y * 0.23f + 1.1f);
    return 170.0f + v;
}

// The 640x480 crop looking at the surface from (ox, oy) on it.
static std::vector<uint8_t> Frame(float ox, float oy, int w = 640, int h = 480) {
    std::vector<uint8_t> f((size_t)w * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float v = Terrain(x + ox, y + oy);
            f[(size_t)y * w + x] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
    return f;
}

static std::vector<uint8_t> Disc(float cx, float cy, int w = 640, int h = 480) {
    std::vector<uint8_t> f((size_t)w * h, 8);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float d = std::hypot(x - cx, y - cy);
            if (d < 60.0f) f[(size_t)y * w + x] = 200;
        }
    return f;
}

static AnalysisResult Run(FrameAnalyzer& a, const std::vector<uint8_t>& data, int w = 640, int h = 480) {
    FrameSlot slot{};
    slot.data = const_cast<uint8_t*>(data.data());
    slot.dataSize = data.size();
    slot.width = (uint32_t)w; slot.height = (uint32_t)h; slot.bitDepth = 8;
    for (int i = 0; i < 400; ++i) {
        a.Submit(&slot);
        for (int j = 0; j < 100; ++j) {
            if (a.HasResult()) return a.GetResult();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    printf("  !! analyzer never produced a result\n");
    return {};
}

int main() {
    printf("Full-frame lock: the analyzer follows a surface that fills the frame\n");
    FrameAnalyzer a;
    a.Start();

    printf("\n1. A still surface\n");
    AnalysisResult r0 = Run(a, Frame(1000, 1000));
    Check(r0.surfaceTracked, "a frame the surface fills is tracked as a surface");
    Check(r0.centroidValid && std::fabs(r0.centroidX - 320) < 1 && std::fabs(r0.centroidY - 240) < 1,
          "it starts at the frame centre");
    AnalysisResult r1 = Run(a, Frame(1000, 1000));
    Check(r1.centroidValid && std::fabs(r1.centroidX - 320) < 0.5f && std::fabs(r1.centroidY - 240) < 0.5f,
          "and stays there while nothing moves");

    printf("\n2. A guide pulse moves it\n");
    // The scene shifts by +12 px in x on the sensor: the view origin moves
    // the other way.
    float lastX = 320, lastY = 240;
    for (int step = 1; step <= 4; ++step) {
        AnalysisResult r = Run(a, Frame(1000 - 3.0f * step, 1000));
        lastX = r.centroidX; lastY = r.centroidY;
    }
    printf("    after +12 px east: (%.2f, %.2f)\n", lastX, lastY);
    Check(std::fabs(lastX - 332) < 1.5f && std::fabs(lastY - 240) < 1.5f,
          "a 12 px move reads as 12 px, the right way, within a pixel and a half");
    for (int step = 1; step <= 4; ++step) {
        AnalysisResult r = Run(a, Frame(988, 1000 - 2.5f * step));
        lastX = r.centroidX; lastY = r.centroidY;
    }
    printf("    then +10 px south: (%.2f, %.2f)\n", lastX, lastY);
    Check(std::fabs(lastX - 332) < 1.5f && std::fabs(lastY - 250) < 1.5f,
          "and a move on the other axis reads on that axis alone");
    Check(std::fabs(lastX - 320) > 2.5f, "which is well over phase 51's 2.5 px minimum response");

    printf("\n3. Back to a disc\n");
    AnalysisResult rd = Run(a, Disc(200, 150));
    Check(!rd.surfaceTracked, "a disc inside the frame is not a surface");
    Check(rd.centroidValid && std::fabs(rd.centroidX - 200) < 3 && std::fabs(rd.centroidY - 150) < 3,
          "and gets its ordinary centroid");
    AnalysisResult rs = Run(a, Frame(500, 500));
    Check(rs.surfaceTracked && std::fabs(rs.centroidX - 320) < 1,
          "a new full frame starts fresh at the centre, not where the old one was");

    printf("\n4. A featureless full frame\n");
    std::vector<uint8_t> flat(640 * 480, 180);
    AnalysisResult rf = Run(a, flat);
    Check(rf.surfaceTracked && !rf.centroidValid,
          "no detail means no position, never the meaningless middle");

    a.Stop();
    printf("\n%s\n", g_fail ? "FAILED" : "All passed");
    return g_fail ? 1 : 0;
}
