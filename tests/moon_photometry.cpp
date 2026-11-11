// ============================================================
//  Phase 141.2: a mutual event's light curve, from synthetic frames
//
//  A field of three Gaussian "moons" and part of a big bright "Jupiter"
//  at the edge, with sky noise:
//
//    1. FindSources finds the three moons, brightest first, and leaves
//       Jupiter out by its size.
//    2. Aperture: flux is recovered with the background removed, and a
//       clipped pixel is reported.
//    3. An occultation: moon B passes in front of moon A, the pair's
//       combined flux falls by A's share and recovers, while the
//       reference stays flat. The measured depth matches the injected
//       one, through the merge, with the mount drifting.
//    4. Frames with a clipped moon are dropped and counted.
//    5. The dates: offsets before and after are interpolated and added;
//       without them the times are left as they are, and the result says.
//    6. Binning keeps IMCCE's window (5 a second to one per 2 s) and the
//       10,000-line limit; the .dat has a header line then date flux flux.
// ============================================================

#include "core/MoonPhotometry.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace raduk::core::moonphot;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}
static std::string F(double v, int d = 3) { char b[64]; snprintf(b, sizeof(b), "%.*f", d, v); return b; }

static const int W = 320, H = 240;

struct Scene {
    double ax, ay, bx, by, rx, ry;       // moon centres
    double fa = 400000, fb = 300000, fr = 350000;   // total fluxes
    double sigma = 2.0;                  // seeing, px
    double occFrac = 0;                  // fraction of A hidden by B
    bool   clip = false;
};

static unsigned g_seed = 1;
static double Noise() { g_seed = g_seed * 1103515245u + 12345u; return ((int)((g_seed >> 8) % 2001) - 1000) / 1000.0; }

static void Render(const Scene& s, std::vector<uint16_t>& out) {
    out.assign((size_t)W * H, 0);
    const double norm = 1.0 / (2 * 3.14159265358979 * s.sigma * s.sigma);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            double v = 2000.0 + 60.0 * Noise();                       // sky
            auto g = [&](double cx, double cy, double f) {
                const double d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                return f * norm * std::exp(-d2 / (2 * s.sigma * s.sigma));
            };
            v += g(s.ax, s.ay, s.fa * (1.0 - s.occFrac));
            v += g(s.bx, s.by, s.fb);
            v += g(s.rx, s.ry, s.fr);
            // Jupiter's limb, off the right edge: a big bright region.
            const double dj = std::hypot(x - 380.0, y - 120.0);
            if (dj < 70) v += 30000;
            if (s.clip && std::hypot(x - s.rx, y - s.ry) < 1.5) v = 65535;
            out[(size_t)y * W + x] = (uint16_t)std::clamp(v, 0.0, 65535.0);
        }
}

int main() {
    printf("Moon photometry (phase 141.2)\n");

    Scene base{80, 120, 140, 120, 200, 60};

    printf("\n1. Finding the moons\n");
    {
        std::vector<uint16_t> f; Render(base, f);
        Plane p{f.data(), W, H};
        const auto src = FindSources(p);
        Check(src.size() == 3, "three moons found, Jupiter left out", std::to_string(src.size()));
        if (src.size() == 3) {
            Check(std::hypot(src[0].x - 80, src[0].y - 120) < 0.5, "brightest first: A at (80,120)",
                  F(src[0].x, 1) + "," + F(src[0].y, 1));
            Check(std::hypot(src[2].x - 140, src[2].y - 120) < 0.5, "faintest last: B at (140,120)");
        }
    }

    printf("\n2. One aperture\n");
    {
        std::vector<uint16_t> f; Render(base, f);
        Plane p{f.data(), W, H};
        const Measure m = Aperture(p, 200, 60, 8);
        Check(m.ok && std::fabs(m.flux / 350000.0 - 1.0) < 0.03, "the reference's flux, sky removed",
              F(m.flux, 0) + " of 350000");
        Check(std::fabs(m.bgPerPx - 2000.0) < 20.0, "the annulus median is the sky", F(m.bgPerPx, 1));
        Scene c = base; c.clip = true;
        Render(c, f);
        Check(Aperture(p, 200, 60, 8).saturated, "a clipped pixel is reported");
        Check(!Aperture(p, 3, 3, 8).ok, "an aperture off the frame is refused");
    }

    printf("\n3. An occultation through the merge, with drift\n");
    // 400 frames, 0.1 s apart. B moves from 60 px right of A to 60 px
    // left of it; between frames 170 and 230 it hides up to 40% of A.
    // The whole field drifts 0.05 px/frame right and down.
    const int N = 400;
    const double jdStart = 2461388.5;
    auto sceneAt = [&](int64_t i) {
        Scene s = base;
        const double drift = 0.05 * (double)i;
        s.ax += drift; s.ay += drift * 0.5;
        s.bx = s.ax + 60.0 - 120.0 * (double)i / (N - 1);  s.by = s.ay;
        s.rx += drift; s.ry += drift * 0.5;
        const double t = (double)i;
        if (t > 170 && t < 230) s.occFrac = 0.40 * (1.0 - std::fabs(t - 200.0) / 30.0);
        if (i == 50 || i == 51) s.clip = true;       // two clipped frames
        return s;
    };
    FrameFn frame = [&](int64_t i, std::vector<uint16_t>& out) { Render(sceneAt(i), out); return true; };
    TimeFn when = [&](int64_t i) { return jdStart + (double)i * 0.1 / 86400.0; };
    Config cfg;
    cfg.ax = 80; cfg.ay = 120; cfg.bx = 140; cfg.by = 120; cfg.rx = 200; cfg.ry = 60;
    cfg.binSec = 0.2;
    cfg.haveOffsetBefore = cfg.haveOffsetAfter = true;
    cfg.offsetBeforeSec = 0.12; cfg.offsetAfterSec = 0.16;
    const Result r = Run(N, W, H, frame, when, cfg);
    Check(r.ok, "the run completes", r.why);
    if (r.ok) {
        const double injected = (400000.0 + 300000.0 - 0.40 * 400000.0) / (400000.0 + 300000.0);
        const double measured = r.minRatio / r.baseRatio;
        printf("   radius %.1f px, %zu bins, %d frames used, %d saturated\n",
               r.radius, r.bins.size(), r.framesUsed, r.framesSaturated);
        Check(std::fabs(measured - injected) < 0.03, "the depth: the pair fell to the injected fraction",
              F(measured) + " vs " + F(injected));
        // The reference stays flat: its spread across the run.
        double mn = 1e300, mx = 0;
        for (const auto& b : r.bins) { mn = (std::min)(mn, b.ref); mx = (std::max)(mx, b.ref); }
        Check((mx - mn) / mx < 0.03, "the reference stays flat through the event", F((mx - mn) / mx));
        Check(r.framesSaturated == 2, "the two clipped frames are dropped and counted",
              std::to_string(r.framesSaturated));
        Check(r.framesLost == 0, "no frame lost: the drift was followed", std::to_string(r.framesLost));

        printf("\n5. Dates\n");
        printf("   first bin: %d frames, jd-start %.4f s\n", r.bins.front().frames, (r.bins.front().jd-jdStart)*86400.0);
        Check(r.timesCorrected, "the offsets were applied");
        const double firstOff = (r.bins.front().jd - jdStart) * 86400.0;
        const double lastOff  = (r.bins.back().jd - (jdStart + (N - 1) * 0.1 / 86400.0)) * 86400.0;
        Check(firstOff > 0.12 && firstOff < 0.20, "the first bin carries about the 'before' offset", F(firstOff));
        Check(lastOff > 0.10 && lastOff < 0.17, "the last carries about the 'after' offset", F(lastOff));

        printf("\n6. Bins and the file\n");
        Check(r.bins.size() >= 195 && r.bins.size() <= 200, "0.2 s bins over 40 s", std::to_string(r.bins.size()));
        const std::string dat = DatText(r);
        Check(dat.rfind("  date(JD UTC)", 0) == 0, "a header line naming the columns");
        Check(dat.find("2461388.500") != std::string::npos, "dates as decimal Julian days");
    }

    printf("\n5b. No offsets, no correction\n");
    {
        Config c2 = cfg; c2.haveOffsetBefore = c2.haveOffsetAfter = false;
        const Result r2 = Run(N, W, H, frame, when, c2);
        Check(r2.ok && !r2.timesCorrected && std::fabs((r2.bins.front().jd - jdStart) * 86400.0) < 0.06,
              "times stay PC times, and the result says so");
    }

    printf("\n6b. Choosing a bin\n");
    Check(std::fabs(ChooseBin(600, 0.01, 10000) - 0.2) < 1e-9, "a 10-minute run at 100 fps: 0.2 s");
    Check(std::fabs(ChooseBin(7200, 0.01, 10000) - 0.72) < 1e-9, "two hours: longer bins to stay under 10,000 lines");
    Check(std::fabs(ChooseBin(30000, 0.01, 10000) - 2.0) < 1e-9, "never past IMCCE's 2 s");
    // 630822816000000000 is .NET's DateTime(2000, 1, 1).Ticks: midnight.
    Check(std::fabs(JdFromSerTicks(630822816000000000LL) - 2451544.5) < 1e-6,
          "SER ticks to JD: 2000-01-01 00:00 is JD 2451544.5", F(JdFromSerTicks(630822816000000000LL), 6));

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
