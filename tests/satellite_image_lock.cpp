// ============================================================
//  satellite_image_lock -- phase 117
//
//  The loop from sensor to mount, with a camera that is rotated and
//  mirrored, because a lock that only works with the camera square
//  to the axes has not been tested. The image model below is the only
//  place that knows the rotation: the lock gets a calibration measured
//  from it, exactly as phase 51 measures one, and nothing else.
// ============================================================

#include "../src/core/SatImageLock.h"

#include <cstdio>
#include <deque>
#include <vector>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) ++g_fail;
}

static constexpr double PLATE = 0.203;       // arcsec/px
static constexpr double GUIDE = 7.5;         // arcsec/s
static constexpr int W = 3840, H = 2160;

struct Camera {
    double rotDeg = 0.0;
    bool   mirror = false;
    // px on the sensor for a sky offset of the target from the boresight.
    void Project(double eastDeg, double northDeg, double& px, double& py) const {
        const double r = rotDeg * 0.017453292519943295;
        double e = eastDeg * 3600.0 / PLATE, n = northDeg * 3600.0 / PLATE;
        double x = e * std::cos(r) - n * std::sin(r);
        double y = e * std::sin(r) + n * std::cos(r);
        if (mirror) x = -x;
        px = W * 0.5 + x;
        py = H * 0.5 + y;
    }
    // What phase 51 would measure: an image displacement for a pulse.
    // Moving the MOUNT east moves the target west on the sensor.
    GuideResponse Measure(double altDeg) const {
        const double asPerMs = GUIDE / 1000.0;
        double ax, ay, bx, by, cx, cy;
        Project(0, 0, cx, cy);
        const double deg = asPerMs / 3600.0;
        Project(-deg * std::cos(altDeg * 0.017453292519943295), 0, ax, ay);
        Project(0, -deg, bx, by);
        GuideResponse r;
        r.exPerMs = (float)(ax - cx); r.eyPerMs = (float)(ay - cy);
        r.nxPerMs = (float)(bx - cx); r.nyPerMs = (float)(by - cy);
        return r;
    }
};

// Aiming errors, in axis degrees: where the satellite really is against
// where the prediction says, and a perfect mount that goes to prediction
// plus bias a couple of ticks later.
static double Run(const Camera& cam, double altDeg, double truthAz, double truthAlt,
                  SatImageLock& lock, int ticks, int delayTicks, double* lastPx = nullptr) {
    std::deque<std::pair<double, double>> img;
    double mountAz = 0.0, mountAlt = 0.0;
    double err = 0.0;
    for (int i = 0; i < ticks; ++i) {
        const double c = std::cos(altDeg * 0.017453292519943295);
        double px, py;
        cam.Project((truthAz - mountAz) * c, truthAlt - mountAlt, px, py);
        img.push_back({px, py});
        if ((int)img.size() > delayTicks) {
            const auto f = img.front();
            img.pop_front();
            lock.Update(i * 0.2, true, (float)f.first, (float)f.second, W, H, (float)altDeg);
        }
        mountAz  = lock.BiasAzDeg();
        mountAlt = lock.BiasAltDeg();
        err = std::hypot(px - W * 0.5, py - H * 0.5);
    }
    if (lastPx) *lastPx = err;
    return err;
}

int main() {
    printf("Satellite image lock: from the sensor back to the mount\n");

    printf("\n1. It closes, however the camera is turned\n");
    for (double rot : {0.0, 37.0, 90.0, 133.0, 200.0, 311.0}) {
        for (bool mirror : {false, true}) {
            Camera cam{rot, mirror};
            SatImageLock l;
            l.Configure(cam.Measure(45.0), 45.0f, (float)GUIDE);
            const double e = Run(cam, 45.0, 0.06, -0.03, l, 120, 2);
            char b[100];
            snprintf(b, sizeof(b), "rot %3.0f mirror %d: %.1f px off centre", rot, (int)mirror, e);
            Check(e < 6.0, b);
        }
    }

    printf("\n2. It holds through the delay\n");
    {
        Camera cam{37.0, true};
        for (int d : {1, 2, 3, 5, 7}) {
            SatImageLock l;
            l.Configure(cam.Measure(45.0), 45.0f, (float)GUIDE);
            double peakAfter = 0.0;
            double px = 0.0;
            for (int i = 0; i < 3; ++i) {
                SatImageLock c = l;
                Run(cam, 45.0, 0.06, -0.03, c, 150 + i * 10, d, &px);
                if (px > peakAfter) peakAfter = px;
            }
            char b[100];
            snprintf(b, sizeof(b), "%d ticks late: settles, %.1f px", d, px);
            Check(peakAfter < 8.0, b);
        }
    }

    printf("\n3. Altitude\n");
    {
        // Calibrated at 30, flown at 70: GuideSolve's rescale.
        Camera cam{37.0, false};
        SatImageLock l;
        l.Configure(cam.Measure(30.0), 30.0f, (float)GUIDE);
        const double e = Run(cam, 70.0, 0.05, 0.02, l, 150, 2);
        char b[100];
        snprintf(b, sizeof(b), "a calibration from 30 deg still closes at 70: %.1f px", e);
        Check(e < 6.0, b);
    }

    printf("\n4. What it will not do\n");
    {
        SatImageLock l;
        Check(!l.Calibrated(), "no calibration, no lock");
        l.Update(0, true, 100, 100, W, H, 45);
        l.Update(1, true, 100, 100, W, H, 45);
        Check(l.BiasAzDeg() == 0.0 && l.BiasAltDeg() == 0.0, "and no bias");

        GuideResponse parallel;
        parallel.exPerMs = 1; parallel.eyPerMs = 1;
        parallel.nxPerMs = 1; parallel.nyPerMs = 1;
        l.Configure(parallel, 45.0f, (float)GUIDE);
        Check(!l.Calibrated(), "a calibration that cannot tell the axes apart is refused");

        Camera cam{0.0, false};
        SatImageLock j;
        j.Configure(cam.Measure(45.0), 45.0f, (float)GUIDE);
        j.Update(0.0, true, W * 0.5f, H * 0.5f, W, H, 45);
        j.Update(0.2, true, 100.0f, 100.0f, W, H, 45);     // one frame's jump
        Check(j.BiasAzDeg() == 0.0 && j.BiasAltDeg() == 0.0, "one frame's jump is not believed");
        Check(j.Rejected() >= 1, "and is counted");

        SatImageLock k;
        k.Configure(cam.Measure(45.0), 45.0f, (float)GUIDE);
        for (int i = 0; i < 2000; ++i)
            k.Update(i * 0.2, true, 10.0f, 10.0f, W, H, 45);
        Check(std::fabs(k.BiasAzDeg()) <= SatImageLock::kMaxBiasDeg &&
              std::fabs(k.BiasAltDeg()) <= SatImageLock::kMaxBiasDeg,
              "a stuck centroid moves the aim a degree and no more");

        SatImageLock m;
        m.Configure(cam.Measure(45.0), 45.0f, (float)GUIDE);
        m.Update(0.0, true, W * 0.5f + 2, H * 0.5f, W, H, 45);
        m.Update(0.2, true, W * 0.5f + 2, H * 0.5f, W, H, 45);
        Check(m.BiasAzDeg() == 0.0, "inside the deadband it leaves the mount alone");
        Check(m.Locked(0.3), "while calling itself locked");
        Check(!m.Locked(5.0), "and it knows when the fixes stop");
        m.Update(5.0, false, 0, 0, W, H, 45);
        Check(!m.Locked(5.0), "a frame with no target is not a fix");
    }

    printf("\n5. The tap\n");
    {
        std::vector<uint8_t> frame((size_t)64 * 64, 0);
        for (int y = 30; y < 34; ++y)
            for (int x = 40; x < 44; ++x) frame[(size_t)y * 64 + x] = 200;
        SatFrameTap t;
        float x = 0, y = 0; int w = 0, h = 0;
        t.OnFrame(frame.data(), 64, 64, 8);
        Check(!t.Take(x, y, w, h), "disarmed, it does not look");
        t.Arm(true);
        t.OnFrame(frame.data(), 64, 64, 8);
        Check(t.Take(x, y, w, h), "armed, it finds the bright thing");
        Check(std::fabs(x - 41.0f) < 2.0f && std::fabs(y - 31.0f) < 2.0f && w == 64,
              "in the right place");
        Check(!t.Take(x, y, w, h), "and gives each fix once");
        t.Arm(false);
        std::vector<uint8_t> empty((size_t)64 * 64, 3);
        t.Arm(true);
        t.OnFrame(empty.data(), 64, 64, 8);
        Check(!t.Take(x, y, w, h), "an empty sky gives no fix");
    }

    printf("\n%s\n", g_fail ? "FAILED" : "All passed");
    return g_fail ? 1 : 0;
}
