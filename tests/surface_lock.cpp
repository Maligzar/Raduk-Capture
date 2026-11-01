// ============================================================
//  Following a surface
//
//  Phase 109. A synthetic surface bigger than the sensor, seen through a
//  window that lags as a camera does, with noise and a brightness that
//  wobbles the way seeing makes it. What needs the real Moon is whether
//  its contrast at the terminator and in the seeing keeps the
//  correlation above the floor: checklist 33.
// ============================================================

#include "core/RoiFollower.h"

#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_fail;
}

// Layers of smooth random detail, from craters down to fine texture.
struct Surface {
    int w = 4400, h = 2800;
    std::vector<float> v;
    explicit Surface(unsigned seed, bool fineOnly = false) : v((size_t)w * h, 110.0f) {
        std::mt19937 rng(seed);
        const std::vector<int> cells = fineOnly ? std::vector<int>{ 16 }
                                                : std::vector<int>{ 96, 40, 16 };
        for (int cell : cells) {
            const int gw = w / cell + 2, gh = h / cell + 2;
            std::vector<float> g((size_t)gw * gh);
            std::uniform_real_distribution<float> d(-1.0f, 1.0f);
            for (auto& a : g) a = d(rng);
            const float amp = cell == 96 ? 30.0f : cell == 40 ? 24.0f : 16.0f;
            for (int y = 0; y < h; ++y) {
                const float fy = (float)y / cell;
                const int iy = (int)fy;
                float ty = fy - iy; ty = ty * ty * (3 - 2 * ty);
                for (int x = 0; x < w; ++x) {
                    const float fx = (float)x / cell;
                    const int ix = (int)fx;
                    float tx = fx - ix; tx = tx * tx * (3 - 2 * tx);
                    const float* r0 = &g[(size_t)iy * gw + ix];
                    const float* r1 = r0 + gw;
                    const float a = r0[0] + (r0[1] - r0[0]) * tx;
                    const float b = r1[0] + (r1[1] - r1[0]) * tx;
                    v[(size_t)y * w + x] += amp * (a + (b - a) * ty);
                }
            }
        }
    }
    float At(double x, double y) const {
        const int ix = (int)x, iy = (int)y;
        const float tx = (float)(x - ix), ty = (float)(y - iy);
        const float* p = &v[(size_t)iy * w + ix];
        const float a = p[0] + (p[1] - p[0]) * tx;
        const float b = p[w] + (p[w + 1] - p[w]) * tx;
        return a + (b - a) * ty;
    }
};

// A window on the sensor, on a surface sliding underneath it.
struct Sim {
    const Surface& s;
    int w = 640, h = 480, bits = 8;
    double sx = 300.0, sy = 200.0;     // the surface's offset under the sensor
    int  shownX = 1600, shownY = 840;
    int  pendX = 0, pendY = 0, pendIn = -1, latency = 2;
    float noise = 5.0f, gain = 1.0f;
    std::mt19937 rng{7};
    std::vector<uint8_t> frame;
    explicit Sim(const Surface& surf) : s(surf) {}

    void Command(int x, int y) { pendX = x; pendY = y; pendIn = latency; }
    void Step(double dx, double dy) {
        sx += dx; sy += dy;
        if (pendIn >= 0 && pendIn-- == 0) { shownX = pendX; shownY = pendY; }
    }
    // Where the point that was at the window's centre at home has got to,
    // relative to the centre of the window now.
    void Render() {
        const int bytes = bits / 8;
        frame.resize((size_t)w * h * bytes);
        std::normal_distribution<float> n(0.0f, noise);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                float v = gain * s.At(shownX + x - sx + 1200.0, shownY + y - sy + 800.0);
                if (x % 7 == 0) v += n(rng);   // noise, without the cost of a draw per pixel
                const int i = (std::clamp)((int)v, 0, 255);
                if (bytes == 2) {
                    frame[((size_t)y * w + x) * 2]     = 0;
                    frame[((size_t)y * w + x) * 2 + 1] = (uint8_t)i;
                } else {
                    frame[(size_t)y * w + x] = (uint8_t)i;
                }
            }
    }
};

int main() {
    printf("Surface lock\n");
    const Surface surf(11);

    // ── Which kind of target ───────────────────────────────────
    {
        std::vector<uint8_t> f(640 * 480, 6);
        for (int y = 0; y < 480; ++y)
            for (int x = 0; x < 640; ++x)
                if (std::hypot(x - 320.0, y - 240.0) < 110.0) f[(size_t)y * 640 + x] = 200;
        Check(!FrameIsFilled(f.data(), 640, 480, 8, 4), "a disc inside the frame is not filled");

        std::vector<uint8_t> g(640 * 480, 6);
        for (int y = 0; y < 480; ++y)
            for (int x = 0; x < 640; ++x)
                if (std::hypot(x - 640.0, y - 240.0) < 60.0) g[(size_t)y * 640 + x] = 200;
        Check(!FrameIsFilled(g.data(), 640, 480, 8, 4), "nor is a disc leaving by one edge");

        Sim s(surf);
        s.Render();
        Check(FrameIsFilled(s.frame.data(), 640, 480, 8, 4), "a surface fills it");

        std::vector<uint8_t> h(640 * 480, 8);
        for (int y = 0; y < 480; ++y)
            for (int x = 0; x < 640; ++x)
                if (x < 300) h[(size_t)y * 640 + x] = 180;
        Check(FrameIsFilled(h.data(), 640, 480, 8, 4), "so does a limb across the frame");

        std::vector<uint8_t> dark(640 * 480, 10);
        Check(!FrameIsFilled(dark.data(), 640, 480, 8, 4), "darkness is not a target");
        Check(!FrameIsFilled(nullptr, 640, 480, 8, 4), "no frame, no answer");
    }

    // ── Measuring ──────────────────────────────────────────────
    for (int bits : { 8, 16 }) {
        Sim s(surf);
        s.bits = bits;
        SurfaceLock lock;
        float x = 0, y = 0;
        s.Render();
        const bool first = lock.Measure(s.frame.data(), s.w, s.h, bits, x, y);
        Check(first && std::fabs(x - 320) < 0.01f && std::fabs(y - 240) < 0.01f,
              bits == 8 ? "starts at the centre, RAW8" : "starts at the centre, RAW16");

        double worst = 0;
        double wantX = 0, wantY = 0;
        for (int i = 0; i < 12; ++i) {
            s.Step(3.3, -2.1); wantX += 3.3; wantY -= 2.1;
            s.Render();
            const bool ok = lock.Measure(s.frame.data(), s.w, s.h, bits, x, y);
            if (ok) worst = (std::max)(worst, std::hypot(x - 320 - wantX, y - 240 - wantY));
            else    worst = 1e9;
        }
        printf("        worst %.2f px after 40 px of drift\n", worst);
        Check(worst < 1.5, "a shift is measured to about a pixel");
    }
    {
        Sim s(surf);
        SurfaceLock lock;
        float x = 0, y = 0;
        s.Render();
        lock.Measure(s.frame.data(), s.w, s.h, 8, x, y);
        double worst = 0;
        for (int i = 0; i < 20; ++i) {
            s.gain = (i % 2) ? 0.8f : 1.15f;
            s.Render();
            if (lock.Measure(s.frame.data(), s.w, s.h, 8, x, y))
                worst = (std::max)(worst, std::hypot(x - 320.0, y - 240.0));
            else worst = 1e9;
        }
        Check(worst < 1.0, "a changing brightness does not read as movement");
    }
    {
        // Bland, and featureless: no patch, so the window holds.
        std::vector<uint8_t> flat(640 * 480, 120);
        SurfaceLock lock;
        float x = 0, y = 0;
        Check(!lock.Measure(flat.data(), 640, 480, 8, x, y), "nothing to lock on to");
        Check(!lock.HasReference(), "and nothing remembered");

        // Vertical stripes: strong detail that cannot say where along
        // itself anything is.
        std::vector<uint8_t> edge(640 * 480);
        for (int y2 = 0; y2 < 480; ++y2)
            for (int x2 = 0; x2 < 640; ++x2)
                edge[(size_t)y2 * 640 + x2] = (uint8_t)(x2 / 32 % 2 ? 200 : 60);
        Check(!lock.Measure(edge.data(), 640, 480, 8, x, y),
              "an edge running one way is not enough");
    }
    {
        Sim s(surf);
        SurfaceLock lock;
        float x = 0, y = 0;
        s.Render();
        lock.Measure(s.frame.data(), s.w, s.h, 8, x, y);
        const Surface other(99);
        Sim t(other);
        t.Render();
        Check(!lock.Measure(t.frame.data(), t.w, t.h, 8, x, y),
              "a different surface is lost, not matched");
        Check(lock.LostFrames() == 1, "and counted");
        s.Render();
        bool back = false;
        for (int i = 0; i < 12 && !back; ++i)
            back = lock.Measure(s.frame.data(), s.w, s.h, 8, x, y);
        Check(back && std::hypot(x - 320.0, y - 240.0) < 2.0, "it finds its patch again");
    }
    {
        // Past the search in one frame: the wide search has to find it.
        Sim s(surf);
        SurfaceLock lock;
        float x = 0, y = 0;
        s.Render();
        lock.Measure(s.frame.data(), s.w, s.h, 8, x, y);
        s.Step(70, 40);
        s.Render();
        bool back = false;
        for (int i = 0; i < 12 && !back; ++i)
            back = lock.Measure(s.frame.data(), s.w, s.h, 8, x, y);
        Check(back && std::hypot(x - 390.0, y - 280.0) < 3.0,
              "a jump past the search is recovered");
    }
    {
        // A retake must not move the answer, or the window would jump.
        Sim s(surf);
        SurfaceLock lock;
        float x = 0, y = 0;
        s.Render();
        lock.Measure(s.frame.data(), s.w, s.h, 8, x, y);
        double worst = 0, wantX = 0;
        for (int i = 0; i < SurfaceLock::kRefreshFrames + 20; ++i) {
            if (i < 40) { s.Step(1.3, 0.0); wantX += 1.3; }
            if (i >= 40) s.noise = 0.0f;
            if (i < 40 || i > SurfaceLock::kRefreshFrames - 10) s.Render();
            if (lock.Measure(s.frame.data(), s.w, s.h, 8, x, y))
                worst = (std::max)(worst, (double)std::fabs(x - 320 - wantX));
            else worst = 1e9;
        }
        printf("        worst %.2f px across a retake, %d retakes\n", worst, lock.Refreshes());
        Check(lock.Refreshes() >= 1, "the reference is retaken");
        Check(worst < 2.0, "and the reported position stays where it was");
    }

    // ── Timing ─────────────────────────────────────────────────
    {
        Sim s(surf);
        SurfaceLock lock;
        float x = 0, y = 0;
        s.Render();
        lock.Measure(s.frame.data(), s.w, s.h, 8, x, y);
        const auto a = std::chrono::steady_clock::now();
        for (int i = 0; i < 100; ++i) lock.Measure(s.frame.data(), s.w, s.h, 8, x, y);
        const double us = std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - a).count() / 100.0;
        printf("        %.0f us per 640x480 frame\n", us);
        Check(us < 2000.0, "cheap enough for the capture thread at 500 fps");
    }

    // ── Through the follower, as the link drives it ────────────
    {
        Sim s(surf);
        RoiFollowLink link;
        link.SetMover([&s](int x, int y) { s.Command(x, y); return true; });
        link.Arm(s.shownX, s.shownY, s.w, s.h, 3840, 2160);
        double worst = 0;
        double drift = 0;
        for (int i = 0; i < 400; ++i) {
            s.Step(3.0, -1.2);
            drift += 3.0;
            s.Render();
            link.OnFrame(s.frame.data(), s.w, s.h, 8);
            // How far the first patch of surface is from the window's centre.
            const double ex = (s.sx - 300.0) - (s.shownX - 1600);
            const double ey = (s.sy - 200.0) - (s.shownY - 840);
            worst = (std::max)(worst, std::hypot(ex, ey));
        }
        const auto f = link.Snapshot();
        printf("        worst %.0f px off, %.0f px of drift, %d moves, %d landed, up to %d frames, %d lost\n",
               worst, drift, f.Moves(), f.Landed(), f.MaxLandingFrames(), link.SurfaceLostFrames());
        Check(link.CurrentMode() == RoiFollowLink::Mode::Surface, "a full frame follows the surface");
        Check(worst < 120.0, "the same patch stays near the middle of a 640 px window");
        Check(f.Moves() > 10 && f.Landed() >= f.Moves() - 1, "every move lands");
        Check(!f.GaveUp(), "and it does not give up");
        Check(link.SurfaceLostFrames() == 0, "nothing is lost across the moves");
    }
    {
        // Fine detail only, so nothing is similar a move away: without
        // looking in both places the patch is simply gone after each one.
        const Surface fine(23, true);
        Sim s(fine);
        RoiFollowLink link;
        link.SetMover([&s](int x, int y) { s.Command(x, y); return true; });
        link.Arm(s.shownX, s.shownY, s.w, s.h, 3840, 2160);
        for (int i = 0; i < 200; ++i) {
            s.Step(3.0, -1.2);
            s.Render();
            link.OnFrame(s.frame.data(), s.w, s.h, 8);
        }
        const auto f = link.Snapshot();
        Check(f.Moves() > 5, "fine detail is followed");
        Check(link.SurfaceLostFrames() == 0, "and a move does not lose it");

        // Arming again must forget the old patch.
        link.Disarm();
        const Surface other(31);
        Sim t(other);
        link.Arm(t.shownX, t.shownY, t.w, t.h, 3840, 2160);
        for (int i = 0; i < 3; ++i) {
            t.Render();
            link.OnFrame(t.frame.data(), t.w, t.h, 8);
        }
        Check(link.SurfaceLostFrames() == 0, "arming again starts from a new patch");
    }
    {
        // A camera that ignores moves is the same failure as on a disc.
        Sim s(surf);
        RoiFollowLink link;
        link.SetMover([](int, int) { return true; });
        link.Arm(s.shownX, s.shownY, s.w, s.h, 3840, 2160);
        for (int i = 0; i < 700 && link.Armed(); ++i) {
            s.Step(0.5, 0.0);
            s.Render();
            link.OnFrame(s.frame.data(), s.w, s.h, 8);
        }
        Check(link.GaveUp(), "a camera that ignores moves is given up on");
    }
    {
        // A disc stays on the centroid.
        RoiFollowLink link;
        link.SetMover([](int, int) { return true; });
        link.Arm(1600, 840, 640, 480, 3840, 2160);
        std::vector<uint8_t> f(640 * 480, 6);
        for (int y = 0; y < 480; ++y)
            for (int x = 0; x < 640; ++x)
                if (std::hypot(x - 320.0, y - 240.0) < 110.0) f[(size_t)y * 640 + x] = 200;
        Check(link.CurrentMode() == RoiFollowLink::Mode::Unknown, "undecided until it sees a frame");
        link.OnFrame(f.data(), 640, 480, 8);
        Check(link.CurrentMode() == RoiFollowLink::Mode::Disc, "a disc follows the centroid");
        link.Disarm();
        link.Arm(1600, 840, 640, 480, 3840, 2160);
        Check(link.CurrentMode() == RoiFollowLink::Mode::Unknown, "arming decides afresh");
    }

    printf("\n%s\n", g_fail ? "FAILED" : "PASS");
    return g_fail ? 1 : 0;
}
