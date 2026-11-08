// ============================================================
//  Phase 133.1: the emulated mount behaves like the measured 8SE
//
//  With RADUK_EMU_NEXSTAR=1 the emulator is meant to reproduce what
//  the NexStar did on 2026-11-07, because the guiding fixes from that
//  night can only be gated indoors against a mount that misbehaves the
//  same way. Each check reads the encoders (GetAltAz) over a second or
//  two of the emulator's own 10 Hz loop, so this runs in real time.
//
//    1. Tracking on: the axes move, a little short of the sky.
//    2. Tracking off and no command: the axes stand still.
//    3. A command REPLACES tracking: azimuth at the commanded rate,
//       mirrored (direction 0 turns the encoder backwards).
//    4. Altitude is not mirrored.
//    5. With tracking on, a guide-scale variable rate is overwritten.
//    6. A jog-speed command is not.
// ============================================================

#include "core/MountEmulator.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-60s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static std::string F(double v) {
    char b[48];
    snprintf(b, sizeof(b), "%.2f\"/s", v);
    return b;
}

// Encoder rates, arcsec/s, over `sec` seconds.
static void Rates(MountEmulator& m, double sec, double& azRate, double& altRate) {
    double a0 = 0, z0 = 0, a1 = 0, z1 = 0;
    m.GetAltAz(a0, z0);
    const auto t0 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds((int)(sec * 1000)));
    m.GetAltAz(a1, z1);
    const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    azRate  = ShortestTurnDeg(z0, z1) * 3600.0 / dt;
    altRate = (a1 - a0) * 3600.0 / dt;
}

int main() {
    printf("\n=== Phase 133.1: the emulated NexStar ===\n");
#ifdef _WIN32
    _putenv("RADUK_EMU_NEXSTAR=1");
#else
    setenv("RADUK_EMU_NEXSTAR", "1", 1);
#endif
    MountEmulator m;
    m.Connect("TEST");
    // Somewhere the sky moves visibly on both axes: alt 50, az 120.
    double ra = 0, dec = 0;
    AltAzToRADecNow(50.0, 120.0, ra, dec);
    m.SyncRADec(ra, dec);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    printf("\n1. Tracking on\n");
    m.SetTrackingMode(IMount::ALT_AZ);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    // The sky's own rate at the mount's pointing, over the same window as
    // the encoders: what perfect tracking would do there.
    double skyAz = 0, skyAlt = 0, az = 0, alt = 0;
    {
        double pra = 0, pdec = 0, s0a, s0z, s1a, s1z, e0a, e0z, e1a, e1z;
        m.GetRADec(pra, pdec);
        RADecToAltAzNow(pra, pdec, s0a, s0z);
        m.GetAltAz(e0a, e0z);
        const auto t0 = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        RADecToAltAzNow(pra, pdec, s1a, s1z);
        m.GetAltAz(e1a, e1z);
        const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        skyAz  = ShortestTurnDeg(s0z, s1z) * 3600.0 / dt;
        skyAlt = (s1a - s0a) * 3600.0 / dt;
        az     = ShortestTurnDeg(e0z, e1z) * 3600.0 / dt;
        alt    = (e1a - e0a) * 3600.0 / dt;
    }
    Check(std::fabs(az) > 1.0, "the azimuth encoder moves", F(az) + " (sky " + F(skyAz) + ")");
    // Measured: 16.0 against 16.3 in azimuth (0.98), 5.0 against 5.2 in
    // altitude (0.96). Up to a tick (0.1 s) of the window is lost to the
    // emulator's 10 Hz loop, so the band is generous.
    Check(az / skyAz > 0.88 && az / skyAz < 1.0,
          "azimuth a little short of the sky: tracking is mis-set", F(az) + " vs " + F(skyAz));
    Check(alt / skyAlt > 0.86 && alt / skyAlt < 1.0,
          "and altitude too", F(alt) + " vs " + F(skyAlt));

    printf("\n2. Tracking off, nothing commanded\n");
    m.SetTrackingMode(IMount::OFF);
    Rates(m, 1.0, az, alt);
    Check(std::fabs(az) < 0.5 && std::fabs(alt) < 0.5, "the axes stand still while the sky turns",
          F(az) + ", " + F(alt));

    printf("\n3-4. A command replaces tracking\n");
    m.SlewVariable(0, 0, 20.0);
    m.SlewVariable(1, 0, 20.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    Rates(m, 1.5, az, alt);
    Check(az < -15.0 && az > -25.0, "azimuth runs at the command, mirrored", F(az));
    Check(alt > 15.0 && alt < 25.0, "altitude runs at the command, not mirrored", F(alt));
    m.StopSlew(0);
    m.StopSlew(1);

    printf("\n5. The hand control overwrites a guide-scale rate\n");
    m.SetTrackingMode(IMount::ALT_AZ);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    m.SlewVariable(1, 0, 20.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    Rates(m, 1.0, az, alt);
    Check(std::fabs(alt - skyAlt) < 5.0, "altitude is back on tracking within a tick",
          F(alt) + " (sky " + F(skyAlt) + ")");

    printf("\n6. A jog is not overwritten\n");
    m.SlewVariable(1, 0, 240.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    Rates(m, 1.0, az, alt);
    Check(alt > 200.0, "a FINE jog keeps its rate with tracking on", F(alt));
    m.StopSlew(1);

    m.Disconnect();
    printf("\n%s\n", g_fail ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return g_fail ? 1 : 0;
}
