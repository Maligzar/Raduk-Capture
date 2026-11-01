// ============================================================
//  Following a target by moving the ROI
//
//  Phase 107. The simulation below is a sensor with a window on it,
//  a target that drifts, and a window that takes a few frames to
//  apply a move, as a camera does. What needs the real camera is
//  whether ASISetStartPos applies during capture at all, and how
//  long it takes. The follower logs that, and gives up if it never
//  happens, and both of those are gated here.
// ============================================================

#include "core/RoiFollower.h"

#include <chrono>
#include <cstdio>
#include <vector>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_fail;
}

static std::vector<uint8_t> Disc(int w, int h, float cx, float cy, float r,
                                 int bits, int level) {
    std::vector<uint8_t> f((size_t)w * h * (bits / 8), 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (std::hypot(x - cx, y - cy) > r) continue;
            if (bits == 16) f[((size_t)y * w + x) * 2 + 1] = (uint8_t)level;
            else            f[(size_t)y * w + x] = (uint8_t)level;
        }
    return f;
}

// A window on a sensor. Moves apply `latency` frames after they are made.
struct Sim {
    int  w = 640, h = 480, sensorW = 3840, sensorH = 2160;
    int  latency = 2;
    bool applies = true;
    double tx = 1920, ty = 1080;       // target on the sensor
    int  shownX = 1600, shownY = 840;  // the window frames actually come from
    int  pendX = 0, pendY = 0, pendIn = -1;

    void Command(int x, int y) { pendX = x; pendY = y; pendIn = latency; }
    void Step(double dx, double dy) {
        tx += dx; ty += dy;
        if (pendIn >= 0 && pendIn-- == 0 && applies) {
            shownX = pendX; shownY = pendY;
        }
    }
    float Cx() const { return (float)(tx - shownX); }
    float Cy() const { return (float)(ty - shownY); }
    bool  InFrame() const { return Cx() >= 0 && Cx() < w && Cy() >= 0 && Cy() < h; }
};

int main() {
    printf("ROI following\n");

    // ── The centroid ───────────────────────────────────────────
    for (int bits : { 8, 16 }) {
        const auto f = Disc(640, 480, 200.0f, 300.0f, 40.0f, bits, 200);
        float x = 0, y = 0;
        const bool ok = FastCentroid(f.data(), 640, 480, bits, 4, x, y);
        Check(ok && fabs(x - 200) < 2.0f && fabs(y - 300) < 2.0f,
              bits == 8 ? "finds a disc, RAW8" : "finds a disc, RAW16");
    }
    {
        std::vector<uint8_t> dark(640 * 480, 10);
        float x, y;
        Check(!FastCentroid(dark.data(), 640, 480, 8, 4, x, y),
              "dark noise is not a target");
        auto f = Disc(640, 480, 320.0f, 240.0f, 30.0f, 8, 220);
        for (auto& v : f) if (v == 0) v = 8;
        Check(FastCentroid(f.data(), 640, 480, 8, 4, x, y) &&
              fabs(x - 320) < 2.0f, "a background does not drag it");
        Check(!FastCentroid(nullptr, 640, 480, 8, 4, x, y), "no frame, no answer");
    }
    {
        const auto f = Disc(640, 480, 320.0f, 240.0f, 60.0f, 8, 200);
        float x, y;
        const auto a = std::chrono::steady_clock::now();
        volatile float keep = 0; for (int i = 0; i < 200; ++i) { FastCentroid(f.data(), 640, 480, 8, 4, x, y); keep = x; } (void)keep;
        const double us = std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - a).count() / 200.0;
        printf("        %.1f us per 640x480 frame\n", us);
        Check(us < 800.0, "cheap enough for the capture thread at 500 fps");
    }

    // ── Holding a drifting target ──────────────────────────────
    {
        Sim s;
        RoiFollower f;
        f.Arm(s.shownX, s.shownY, s.w, s.h, s.sensorW, s.sensorH);
        Check(f.Armed(), "arms on a crop");
        bool held = true;
        double worst = 0;
        for (int i = 0; i < 1500; ++i) {
            s.Step(0.9, -0.5);
            held = held && s.InFrame();
            worst = (std::max)(worst, (double)std::hypot(s.Cx() - 320.0f, s.Cy() - 240.0f));
            const auto m = f.Update(s.Cx(), s.Cy(), true);
            if (m.move) s.Command(m.x, m.y);
        }
        printf("        worst %.0f px off centre, %d moves, %d landed, up to %d frames\n",
               worst, f.Moves(), f.Landed(), f.MaxLandingFrames());
        Check(held, "a target drifting 1350 px never leaves a 640 px window");
        Check(f.Moves() > 10 && f.Landed() >= f.Moves() - 1, "every move lands");
        Check(f.MaxLandingFrames() >= s.latency &&
              f.MaxLandingFrames() <= s.latency + 2,
              "and the camera's latency is measured");
        Check(f.OffsetX() == f.OriginX() - 1600, "the offset is from home");
        Check(f.OriginX() % 8 == 0 && f.OriginY() % 2 == 0,
              "the origin stays on the camera's alignment");
    }
    {
        // Without waiting to land, stale frames would command the same
        // error again and the window would overshoot.
        Sim s;
        s.latency = 5;
        s.tx += 100;
        RoiFollower f;
        f.Arm(s.shownX, s.shownY, s.w, s.h, s.sensorW, s.sensorH);
        int commands = 0;
        for (int i = 0; i < 12; ++i) {
            s.Step(0, 0);
            const auto m = f.Update(s.Cx(), s.Cy(), true);
            if (m.move) { s.Command(m.x, m.y); ++commands; }
        }
        Check(commands == 1, "stale frames after a move do not move it again");
        Check(fabs(s.Cx() - 320.0f) <= 4.0f, "and one move is enough");
    }

    // ── The mount's view ───────────────────────────────────────
    {
        // The tracker sees frame position + offset. That has to equal the
        // target's true distance from home, or the mount stops offloading.
        Sim s;
        RoiFollower f;
        f.Arm(s.shownX, s.shownY, s.w, s.h, s.sensorW, s.sensorH);
        for (int i = 0; i < 400; ++i) {
            s.Step(1.0, 0.0);
            const auto m = f.Update(s.Cx(), s.Cy(), true);
            if (m.move) s.Command(m.x, m.y);
        }
        for (int i = 0; i < 10; ++i) { s.Step(0, 0); f.Update(s.Cx(), s.Cy(), true); }
        const double seen = s.Cx() + f.OffsetX() - 320.0;
        Check(f.OffsetX() > 300 && fabs(seen - 400.0) < 1.0,
              "frame position plus offset is the true error on the sensor");
    }

    // ── Refusing to do the wrong thing ─────────────────────────
    {
        RoiFollower f;
        f.Arm(0, 0, 3840, 2160, 3840, 2160);
        Check(!f.Armed(), "a full-sensor frame has nowhere to go");
        f.Arm(1600, 840, 640, 480, 3840, 2160);
        Check(!f.Update(330, 240, true).move, "inside the deadband, no move");
        Check(!f.Update(600, 240, false).move, "a lost target holds position");
    }
    {
        RoiFollower f;
        f.Arm(3200, 1680, 640, 480, 3840, 2160);    // already in the corner
        f.Update(320, 240, true);
        Check(!f.Update(420, 340, true).move, "at the sensor's edge it stops");
        Check(f.OriginX() == 3200 && f.OriginY() == 1680, "and stays on the sensor");
    }
    {
        RoiFollower f;
        f.Arm(1000, 800, 640, 480, 3840, 2160);
        f.Update(320, 240, true);
        Check(!f.Update(20, 20, true).move, "one wild frame does not move it");
        Check(!f.Update(320, 240, true).move, "and it settles when the frame was wrong");
        Check(!f.Update(20, 20, true).move && !f.Update(22, 21, true).move,
              "two in agreement still wait");
        Check(f.Update(21, 20, true).move, "three in agreement mean it is really there");
    }
    {
        // A camera that ignores moves mid-capture.
        Sim s;
        s.applies = false;
        s.tx += 100;
        RoiFollower f;
        f.Arm(s.shownX, s.shownY, s.w, s.h, s.sensorW, s.sensorH);
        int commands = 0;
        for (int i = 0; i < 400 && f.Armed(); ++i) {
            s.Step(0, 0);
            const auto m = f.Update(s.Cx(), s.Cy(), true);
            if (m.move) { s.Command(m.x, m.y); ++commands; }
        }
        Check(!f.Armed() && f.GaveUp(), "a camera that ignores moves stops it");
        Check(commands == RoiFollower::kMaxUnconfirmed, "after three tries");
        Check(f.OffsetX() == 0, "and it does not claim an offset it never got");
    }

    // ── The link ───────────────────────────────────────────────
    {
        RoiFollowLink link;
        int called = 0, sx = 0;
        link.SetMover([&](int x, int) { ++called; sx = x; return true; });
        const auto fr = Disc(640, 480, 500.0f, 240.0f, 30.0f, 8, 200);
        link.OnFrame(fr.data(), 640, 480, 8);
        Check(called == 0, "disarmed, it does nothing");
        link.Arm(1600, 840, 640, 480, 3840, 2160);
        link.OnFrame(fr.data(), 640, 480, 8);
        Check(called == 1 && sx > 1600, "armed, the frame moves the window");
        Check(link.OriginX() == sx && link.OffsetX() == sx - 1600,
              "and the render loop can read where it went");
        link.Disarm();
        Check(!link.Armed(), "it disarms");
        link.Reset();
        Check(!link.GaveUp() && link.OffsetX() == 0, "and a reset forgets the run");

        RoiFollowLink refused;
        refused.SetMover([](int, int) { return false; });
        refused.Arm(1600, 840, 640, 480, 3840, 2160);
        refused.OnFrame(fr.data(), 640, 480, 8);
        Check(refused.OffsetX() == 0, "a move the SDK refuses is not counted");
    }

    if (g_fail == 0) printf("PASS\n"); else printf("FAIL: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
