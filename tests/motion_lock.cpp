// ============================================================
//  motion_lock -- phase 122
//
//  Tracking from the image alone, closed through a simulated mount and
//  a rotated, mirrored camera. The target is a rocket: it climbs and
//  speeds up, which is the case a filter without acceleration loses.
//  The camera model is the only place that knows the rotation; the lock
//  gets a calibration measured from it, as phase 51 measures one. Each
//  centroid carries 3 px of noise, as seeing gives a real one.
// ============================================================

#include "../src/core/MotionLock.h"

#include <cstdio>
#include <array>
#include <deque>
#include <random>
#include <string>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) ++g_fail;
}

static constexpr double PLATE = 0.203, GUIDE = 7.5;
static constexpr int W = 3840, H = 2160;
static constexpr double D2R = 0.017453292519943295;

struct Camera {
    double rotDeg = 37.0;
    bool mirror = true;
    // Sensor pixels for an object east and north of the boresight.
    void Project(double eastDeg, double northDeg, double& px, double& py) const {
        const double r = rotDeg * D2R;
        const double e = eastDeg * 3600.0 / PLATE, n = northDeg * 3600.0 / PLATE;
        double x = e * std::cos(r) - n * std::sin(r);
        const double y = e * std::sin(r) + n * std::cos(r);
        if (mirror) x = -x;
        px = W * 0.5 + x; py = H * 0.5 + y;
    }
    GuideResponse Measure(double altDeg) const {
        const double deg = GUIDE / 1000.0 / 3600.0;
        double ax, ay, bx, by, cx, cy;
        Project(0, 0, cx, cy);
        Project(-deg * std::cos(altDeg * D2R), 0, ax, ay);
        Project(0, -deg, bx, by);
        GuideResponse g;
        g.exPerMs = (float)(ax - cx); g.eyPerMs = (float)(ay - cy);
        g.nxPerMs = (float)(bx - cx); g.nyPerMs = (float)(by - cy);
        return g;
    }
};

struct Mount {
    double alt = 0, az = 0, rAlt = 0, rAz = 0, cAlt = 0, cAz = 0;
    void Step(double dt) {
        auto go = [&](double& r, double c) {
            const double a = 3.0 * dt, d = c - r;
            r += d > a ? a : (d < -a ? -a : d);
            r = r > 3.0 ? 3.0 : (r < -3.0 ? -3.0 : r);
        };
        go(rAlt, cAlt); go(rAz, cAz);
        alt += rAlt * dt; az += rAz * dt;
    }
};

// A rocket: low in the east, climbing and turning, rate growing to about
// 0.8 deg/s on the sky.
static void Rocket(double t, double& alt, double& az) {
    alt = 20.0 + 0.10 * t + 0.0025 * t * t;
    az  = 100.0 + 0.15 * t + 0.003 * t * t;
}

struct Run {
    SatTracker::Phase end = SatTracker::Phase::Idle;
    std::string reason;
    double worstAfterLock = 0, meanAfterLock = 0, worstSettled = 0;
    bool began = false, stopped = false;
};

template <class Hide>
static Run Fly(const Camera& cam, int delayTicks, double seconds, Hide hide) {
    MotionLock lock;
    lock.Configure(cam.Measure(30.0), 30.0f, (float)GUIDE);

    SatTracker::Config c;
    c.startSec = 0.0;
    c.endSec = seconds;
    c.skipSlew = true;
    c.maxAltDeg = 85.0;
    SatTracker tr;
    tr.Configure(c);

    Mount m;
    Rocket(0, m.alt, m.az);
    // Brought into frame by hand: inside the 7' height, not centred.
    m.alt -= 0.03; m.az += 0.03;

    std::deque<std::array<double, 2>> pending;
    std::mt19937 rng(7);
    std::normal_distribution<double> jitter(0.0, 3.0);
    Run out;
    double sum = 0; int n = 0;
    bool started = false;
    double lockedAt = 1e9;
    for (double now = -1.0; now < seconds + 2.0; now += 0.2) {
        double ta, tz;
        Rocket(now, ta, tz);
        double px, py;
        cam.Project((tz - m.az) * std::cos(m.alt * D2R), ta - m.alt, px, py);
        pending.push_back({px, py});
        bool fix = false;
        double fx = 0, fy = 0;
        if ((int)pending.size() > delayTicks) {
            // Seeing moves a real centroid by a few pixels every frame.
            fx = pending.front()[0] + jitter(rng); fy = pending.front()[1] + jitter(rng);
            pending.pop_front();
            fix = !hide(now) && fx > 0 && fx < W && fy > 0 && fy < H;
        }
        lock.Update(now, m.alt, m.az, fix, now - delayTicks * 0.2, (float)fx, (float)fy, W, H);
        if (!started && lock.Ready()) { tr.Start(now); started = true; }
        if (started && tr.Running()) {
            if (lock.Lost(now)) tr.Abort("lost the target in the image");
            // As SatDesk does: position now, rate 0.1 s ahead.
            SatTracker::Sky sky = lock.SkyAt(now);
            const SatTracker::Sky ahead = lock.SkyAt(now + 0.1);
            sky.azRate = ahead.azRate; sky.altRate = ahead.altRate;
            const auto o = tr.Tick(now, sky, true, m.alt, m.az, false);
            if (o.rates) { m.cAz = o.azRate; m.cAlt = o.altRate; }
            if (o.stop) { out.stopped = true; m.cAz = m.cAlt = 0; }
            if (o.beginCapture) { out.began = true; lockedAt = now; }
            if (tr.State() == SatTracker::Phase::Tracking) {
                const double e = std::hypot((tz - m.az) * std::cos(m.alt * D2R), ta - m.alt);
                if (e > out.worstAfterLock) out.worstAfterLock = e;
                if (now > lockedAt + 5.0 && e > out.worstSettled) out.worstSettled = e;
                sum += e; ++n;
            }
        }
        for (int i = 0; i < 20; ++i) m.Step(0.01);
    }
    out.end = tr.State();
    out.reason = tr.Reason();
    out.meanAfterLock = n ? sum / n : 0;
    return out;
}

int main() {
    printf("Motion lock: tracking from the image alone\n");

    printf("\n1. Pixels to sky\n");
    {
        for (double rot : {0.0, 90.0, 211.0}) for (bool mir : {false, true}) {
            Camera cam{rot, mir};
            double px, py, dAz, dAlt;
            cam.Project(0.05 * std::cos(40 * D2R), -0.03, px, py);
            PixelOffsetToAxisDeg(cam.Measure(40), 40, (float)GUIDE, W * 0.5 - px, H * 0.5 - py,
                                 40, dAz, dAlt);
            char b[100];
            snprintf(b, sizeof(b), "rot %.0f mirror %d: the object is found where it is", rot, (int)mir);
            Check(std::fabs(dAz - 0.05) < 1e-4 && std::fabs(dAlt + 0.03) < 1e-4, b);
        }
    }

    printf("\n2. The filter\n");
    {
        AxisFilter f;
        for (int i = 0; i <= 50; ++i) { const double t = i * 0.1; f.Update(t, 10 + 0.5 * t + 0.1 * t * t); }
        const double t = 5.0;
        Check(std::fabs(f.Rate(t) - (0.5 + 0.2 * t)) < 0.02, "it follows an accelerating rate");
        Check(std::fabs(f.Position(t + 0.5) - (10 + 0.5 * 5.5 + 0.1 * 5.5 * 5.5)) < 0.02,
              "and predicts half a second ahead");

        MotionLock l;
        Camera cam;
        l.Configure(cam.Measure(30), 30, (float)GUIDE);
        // Across north: 359.9 to 0.1 must not read as a jump.
        for (int i = 0; i < 6; ++i) {
            double px, py;
            cam.Project(0, 0, px, py);
            l.Update(i * 0.2, 30.0, std::fmod(359.9 + 0.04 * i, 360.0), true, i * 0.2, (float)px, (float)py, W, H);
        }
        const auto s = l.SkyAt(1.0);
        Check(s.ok && std::fabs(s.azRate - 0.2) < 0.05, "crossing north is not a 360 degree leap");
    }

    printf("\n3. Flying a rocket\n");
    auto never = [](double) { return false; };
    for (int d : {1, 2, 3}) {
        const Run r = Fly(Camera{}, d, 60.0, never);
        printf("  %d ticks late: ended %d, mean %.4f deg, worst %.4f, worst once settled %.4f\n",
               d, (int)r.end, r.meanAfterLock, r.worstAfterLock, r.worstSettled);
        char b[140];
        // Half the frame's 7.3' height is 0.061 degrees.
        snprintf(b, sizeof(b), "%d ticks late: on the sensor throughout, within 1' once settled", d);
        Check(r.end == SatTracker::Phase::Complete && r.began &&
              r.worstAfterLock < 0.061 && r.worstSettled < 0.017, b);
    }
    {
        const Run r = Fly(Camera{0.0, false}, 2, 60.0, never);
        Check(r.end == SatTracker::Phase::Complete && r.worstSettled < 0.017,
              "and with the camera square to the axes");
    }

    printf("\n4. Losing it\n");
    {
        // A cloud for one second: it coasts through.
        const Run r = Fly(Camera{}, 2, 60.0, [](double t) { return t > 20.0 && t < 21.0; });
        printf("  one second hidden: ended %d, worst %.4f deg\n", (int)r.end, r.worstAfterLock);
        Check(r.end == SatTracker::Phase::Complete, "a second's cloud is coasted through");
    }
    {
        const Run r = Fly(Camera{}, 2, 60.0, [](double t) { return t > 20.0; });
        printf("  gone for good: %s\n", r.reason.c_str());
        Check(r.end == SatTracker::Phase::Aborted && r.stopped, "a target gone for good stops the mount");
    }
    {
        MotionLock l;
        Check(!l.Calibrated(), "no calibration, no lock");
        l.Update(0, 30, 100, true, 0, 100, 100, W, H);
        Check(!l.Ready() && !l.SkyAt(0).ok, "and no sky");
    }

    printf("\n%s\n", g_fail ? "FAILED" : "All passed");
    return g_fail ? 1 : 0;
}
