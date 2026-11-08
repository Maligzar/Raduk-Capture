// ============================================================
//  Rate-offset guiding on a simulated NexStar (2026-11-07)
//
//  The 8SE as it measured at the rig that night:
//    - a motion command REPLACES the axis's tracking while it runs;
//    - a stop leaves the axis still until tracking is re-asserted
//      (the real sender re-asserts it at once, so StopSlew resumes);
//    - azimuth tracks at ~16"/s, altitude ~5"/s, and the mount's
//      tracking is slightly wrong, so the planet drifts ~1"/s;
//    - rate 1 is ~3.8"/s, slower than azimuth tracking: the pulse
//      PulseGuide sends can never push the planet east.
//
//  Calibration and centring are run against it in real time (the
//  tracker has its own thread). With plain PulseGuide this model
//  reproduces the night: centring cannot converge. With RatePulse the
//  axes come out ~90 deg apart and the planet is centred and held.
//  The camera's axes are rotated 37 deg and one is mirrored, so no sign
//  can be right by accident.
// ============================================================

#include "core/CentroidTracker.h"
#include "core/CentroidTracker.cpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace raduk::core;

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("  FAIL %s:%d  ", __FILE__, __LINE__); \
    std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// A fake NexStar. Axis positions in degrees, advanced on demand.
class FakeNexStar : public IMount {
public:
    // What the sky needs (the planet's own motion in axis terms) and
    // what the hand control's tracking actually does.
    double skyAz = 16.3 / 3600, skyAlt = 5.2 / 3600;        // deg/s
    double trkAz = 16.0 / 3600, trkAlt = 5.0 / 3600;        // deg/s
    double rate1 = 3.8 / 3600;                              // fixed rate 1
    int    sign0[2] = { -1, +1 };   // which way "direction 0" turns each encoder

    double az = 200.0, alt = 50.0;          // mount
    double pAz = 200.0 + 60.0 / 3600, pAlt = 50.0 - 40.0 / 3600;   // planet: 60" and 40" off
    double cmd[2] = { 0, 0 };              // deg/s while commanded
    bool   commanded[2] = { false, false };

    std::mutex mu;
    std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();

    void Advance() {
        std::lock_guard<std::mutex> lk(mu);
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        az  += (commanded[0] ? cmd[0] : trkAz)  * dt;
        alt += (commanded[1] ? cmd[1] : trkAlt) * dt;
        pAz += skyAz * dt; pAlt += skyAlt * dt;
    }
    // Planet on the sensor: offset in axis arcsec -> sky -> rotated pixels.
    void Pixel(float& x, float& y) {
        Advance();
        std::lock_guard<std::mutex> lk(mu);
        const double dAz  = (pAz - az) * 3600 * std::cos(alt * 3.14159265 / 180);
        const double dAlt = (pAlt - alt) * 3600;
        const double px = dAz / 0.2, py = dAlt / 0.2;              // 0.2"/px
        const double c = std::cos(37 * 3.14159265 / 180), s = std::sin(37 * 3.14159265 / 180);
        x = (float)(640 + c * px - s * py);
        y = (float)(480 - (s * px + c * py));                       // mirrored
    }

    bool Connect(const std::string&, int) override { return true; }
    void Disconnect() override {}
    bool IsConnected() const override { return true; }
    bool GetRADec(double& r, double& d) override { r = 0; d = 0; return true; }
    bool GetAltAz(double& a, double& z) override {
        Advance(); std::lock_guard<std::mutex> lk(mu); a = alt; z = az; return true;
    }
    bool GoToRADec(double, double) override { return true; }
    bool IsSlewing() override { return false; }
    void CancelGoTo() override {}
    bool SetTrackingMode(TrackingMode) override { return true; }
    TrackingMode GetTrackingMode() override { return ALT_AZ; }
    bool SlewFixed(int axis, int dir, int rate) override {
        if (rate == 0) { StopSlew(axis); return true; }
        Advance(); std::lock_guard<std::mutex> lk(mu);
        cmd[axis] = (dir == 0 ? 1 : -1) * sign0[axis] * rate1 * rate;
        commanded[axis] = true; return true;
    }
    bool SlewVariable(int axis, int dir, double asps) override {
        Advance(); std::lock_guard<std::mutex> lk(mu);
        if (asps <= 0) { commanded[axis] = false; return true; }
        cmd[axis] = (dir == 0 ? 1 : -1) * sign0[axis] * asps / 3600.0;
        commanded[axis] = true; return true;
    }
    bool SupportsVariableRate() const override { return true; }
    void StopSlew(int axis) override {
        Advance(); std::lock_guard<std::mutex> lk(mu); commanded[axis] = false;
    }
    bool SyncRADec(double, double) override { return true; }
    bool IsAligned() override { return true; }
    bool PulseGuide(GuideDirection d, int ms) override {
        const int axis = (d == GuideDirection::East || d == GuideDirection::West) ? 0 : 1;
        const int dir  = (d == GuideDirection::East || d == GuideDirection::North) ? 0 : 1;
        SlewFixed(axis, dir, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        StopSlew(axis);
        return true;
    }
    bool IsPulseGuiding() override { return false; }
    bool GetVersion(int& a, int& b) override { a = 5; b = 0; return true; }
    const std::string& ConnectionId() const override { return id; }
    std::string id = "fake";
};

int main() {
    FakeNexStar m;
    CentroidTracker t;
    t.SetMount(&m);

    std::atomic<bool> run{true};
    std::thread feeder([&] {
        while (run.load()) {
            float x, y; m.Pixel(x, y);
            t.SubmitCentroid(x, y, 1280, 960);
            t.Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    std::printf("1. calibration\n");
    t.StartSearching();
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    t.StartCalibration();
    for (int i = 0; i < 900 && t.Stage() == AcquisitionStage::Calibrating; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto cal = t.Calibration();
    CHECK(t.Stage() == AcquisitionStage::Centering && cal.valid,
          "calibration did not succeed (stage %s)", AcquisitionStageName(t.Stage()));
    if (cal.valid) {
        const float cross = cal.exPerMs * cal.nyPerMs - cal.eyPerMs * cal.nxPerMs;
        const float em = std::hypot(cal.exPerMs, cal.eyPerMs), nm = std::hypot(cal.nxPerMs, cal.nyPerMs);
        const float ang = std::asin((std::min)(1.0f, std::fabs(cross) / (em * nm))) * 57.2958f;
        std::printf("   axes %.0f deg apart\n", ang);
        CHECK(ang > 80.0f, "axes only %.0f deg apart", ang);
    }

    std::printf("2. centring\n");
    float x = 0, y = 0;
    bool centred = false;
    for (int i = 0; i < 600; ++i) {          // 60 s
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        m.Pixel(x, y);
        if (std::hypot(x - 640, y - 480) < 10.0f) { centred = true; }
        if (t.Stage() != AcquisitionStage::Centering) break;
    }
    std::printf("   ended at (%.0f, %.0f), stage %s\n", x, y, AcquisitionStageName(t.Stage()));
    CHECK(centred, "never came within 10 px of centre");

    std::printf("3. held for 20 s\n");
    float worst = 0;
    for (int i = 0; i < 200; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        m.Pixel(x, y);
        worst = (std::max)(worst, std::hypot(x - 640, y - 480));
    }
    std::printf("   worst %.0f px from centre\n", worst);
    CHECK(worst < 40.0f, "wandered %.0f px while held", worst);

    t.Reset();
    run.store(false);
    feeder.join();
    if (g_fail) { std::printf("rate_guide: %d FAILED\n", g_fail); return 1; }
    std::printf("rate_guide: all passed\n");
    return 0;
}
