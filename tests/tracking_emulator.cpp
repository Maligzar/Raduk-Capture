// ============================================================
//  Tracking against the emulated mount, as the owner runs it
//
//  The owner's sequence (2026-11-12): GoTo from the map, PLANET LOCK,
//  then reset the centroid and lock again. Driven here end to end: the
//  real CentroidTracker against MountEmulator, with the planet's place
//  in the frame computed from where the mount points. Nothing faked
//  between them but the camera.
//
//  Against the measured 8SE model (RADUK_EMU_NEXSTAR=1), which the
//  app's emulator now runs by default. Two faults found this way:
//  the older RA/Dec model cannot be guided at all (pass "default" to
//  see its calibration fail), and a calibration reused after a GoTo
//  left the planet off centre with nothing correcting it (step 4).
//
//    1. First lock: calibrates, centres, reaches LOCKED.
//    2. Held: within 25 px over 20 s of guiding.
//    3. Reset, then lock again: LOCKED again and centred.
//    4. A new GoTo, then lock: LOCKED and centred, not stuck centring.
//    5. A GoTo while guiding hands the mount back first.
//
//  Real time: about three minutes; progress is printed as it goes.
// ============================================================

#include "core/MountEmulator.h"
#include "core/CentroidTracker.h"
#include "core/CentroidTracker.cpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using namespace raduk::core;
using clk = std::chrono::steady_clock;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-58s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    fflush(stdout);
    if (!ok) g_fail++;
}

static constexpr uint32_t W = 1280, H = 960;

struct Rig {
    MountEmulator m;
    CentroidTracker t;
    double pRA = 0, pDec = 0;            // the planet, fixed on the sky
    std::atomic<bool> feed{true};
    std::atomic<float> ex{0}, ey{0};     // last error from centre, px
    std::atomic<bool> inFrame{false};
    std::thread feeder;
    std::string calMsg; bool calDone = false, calOk = false;
    std::string abortMsg;

    // Where the planet lands on the sensor, from the mount's axes.
    bool Pixel(float& x, float& y) {
        double aM = 0, zM = 0, aP = 0, zP = 0;
        if (!m.GetAltAz(aM, zM)) return false;
        RADecToAltAzNow(pRA, pDec, aP, zP);
        const double scale = RigConfig::Get().plateScaleArcsecPx;
        const double dx = ShortestTurnDeg(zM, zP) * std::cos(aM * 0.0174533) * 3600.0 / scale;
        const double dy = -(aP - aM) * 3600.0 / scale;
        x = (float)(W * 0.5 + dx); y = (float)(H * 0.5 + dy);
        return x > 0 && x < W && y > 0 && y < H;
    }
    void Start() {
        feeder = std::thread([this] {
            while (feed.load()) {
                float x, y;
                const bool ok = Pixel(x, y);
                inFrame.store(ok);
                if (ok) { t.SubmitCentroid(x, y, W, H); ex.store(x - W * 0.5f); ey.store(y - H * 0.5f); }
                else t.SubmitCentroidLost();
                std::this_thread::sleep_for(std::chrono::milliseconds(200));   // ~5 analyses/s
            }
        });
    }
    void Stop() { feed.store(false); if (feeder.joinable()) feeder.join(); }
    float Err() const { return std::hypot(ex.load(), ey.load()); }

    // Tick the tracker as main.cpp does until LOCKED, failure or timeout.
    AcquisitionStage RunUntilLocked(int timeoutS) {
        const auto end = clk::now() + std::chrono::seconds(timeoutS);
        while (clk::now() < end) {
            t.Tick();
            const auto s = t.Stage();
            if (s == AcquisitionStage::Locked) return s;
            if (calDone && !calOk) return s;
            if (!abortMsg.empty()) return s;
            if (s == AcquisitionStage::Off) return s;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        return t.Stage();
    }
};

static void RunModel(bool nexstar) {
    printf("\n== %s ==\n", nexstar ? "RADUK_EMU_NEXSTAR=1 (the measured 8SE)" : "default emulator");
#ifdef _WIN32
    _putenv(nexstar ? "RADUK_EMU_NEXSTAR=1" : "RADUK_EMU_NEXSTAR=0");
#else
    setenv("RADUK_EMU_NEXSTAR", nexstar ? "1" : "0", 1);
#endif
    Rig r;
    r.m.Connect("COM9");
    // A planet at altitude 45, due south, now.
    AltAzToRADecNow(45.0, 180.0, r.pRA, r.pDec);
    r.m.GoToRADec(r.pRA, r.pDec);
    for (int i = 0; i < 600 && r.m.IsSlewing(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    r.t.SetMount(&r.m);
    r.t.SetCalibrationCallback([&](bool ok, const std::string& msg) {
        r.calDone = true; r.calOk = ok; r.calMsg = msg;
        printf("     calibration %s: %s\n", ok ? "OK" : "FAILED", msg.c_str());
    });
    r.t.SetAbortCallback([&](const std::string& msg) {
        r.abortMsg = msg; printf("     guiding abandoned: %s\n", msg.c_str());
    });
    r.Start();
    std::this_thread::sleep_for(std::chrono::seconds(2));
    {
        float x, y;
        const bool in = r.Pixel(x, y);
        printf("     after the GoTo: planet at (%.0f, %.0f)%s\n", x, y, in ? "" : " - OUT OF FRAME");
    }

    printf("  1. first lock\n");
    std::string why;
    const bool started = r.t.StartAutoAcquire(why);
    Check(started, "auto-acquire starts", why);
    auto st = r.RunUntilLocked(150);
    Check(st == AcquisitionStage::Locked, "reaches LOCKED", AcquisitionStageName(st));
    Check(r.inFrame.load() && r.Err() <= 15.0f, "centred", std::to_string((int)r.Err()) + " px");

    if (st == AcquisitionStage::Locked) {
        printf("  2. held for 20 s\n");
        float worst = 0;
        for (int i = 0; i < 40; ++i) {
            r.t.Tick();
            worst = (std::max)(worst, r.Err());
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        Check(r.inFrame.load() && worst <= 25.0f, "stays within 25 px", "worst " + std::to_string((int)worst) + " px");
    }

    printf("  3. reset the centroid, then lock again\n");
    r.t.Reset();
    r.calDone = false; r.abortMsg.clear();
    std::this_thread::sleep_for(std::chrono::seconds(2));
    const bool again = r.t.StartAutoAcquire(why);
    Check(again, "auto-acquire starts again", why);
    st = r.RunUntilLocked(150);
    Check(st == AcquisitionStage::Locked, "reaches LOCKED again", AcquisitionStageName(st));
    Check(r.inFrame.load() && r.Err() <= 15.0f, "centred again", std::to_string((int)r.Err()) + " px");

    printf("  4. a new GoTo from the map, then lock (calibration under 15 min old)\n");
    r.t.Reset();                          // what main.cpp's GoTo-complete does
    r.m.GoToRADec(r.pRA, r.pDec);         // lands with the emulator's 45"/25" error
    for (int i = 0; i < 600 && r.m.IsSlewing(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::this_thread::sleep_for(std::chrono::seconds(3));
    printf("     after the GoTo: %d px from centre\n", (int)r.Err());
    r.calDone = false; r.abortMsg.clear();
    const bool third = r.t.StartAutoAcquire(why);
    Check(third, "auto-acquire starts after the GoTo", why);
    st = r.RunUntilLocked(150);
    Check(st == AcquisitionStage::Locked, "reaches LOCKED after the GoTo", AcquisitionStageName(st));
    Check(r.inFrame.load() && r.Err() <= 15.0f, "centred after the GoTo", std::to_string((int)r.Err()) + " px");

    if (st == AcquisitionStage::Locked) {
        printf("  5. a GoTo while guiding: the mount is handed back first\n");
        bool lost = false;
        r.t.SetLostCallback([&] { lost = true; });
        r.t.YieldMount("a GoTo started");       // what MountProxy does before every GoTo
        Check(r.m.GetTrackingMode() != IMount::OFF, "the hand control is tracking again",
              IMount::TrackingModeName(r.m.GetTrackingMode()));
        r.t.Tick();
        Check(lost, "the main thread is told to reset");
        r.t.Reset();
    }
    r.t.Reset();
    r.Stop();
    r.m.Disconnect();
}

int main(int argc, char** argv) {
    printf("Tracking against the emulated mount\n");
    // The app's emulator runs the measured 8SE (main.cpp, unless
    // RADUK_EMU_NEXSTAR=0), so that is the gate. "default" runs the older
    // RA/Dec model instead, to show why it cannot guide: its first
    // calibration fails, because rate commands move RA and Dec there,
    // not the azimuth and altitude motors guiding drives.
    const std::string only = argc > 1 ? argv[1] : "";
    RunModel(only != "default");
    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
