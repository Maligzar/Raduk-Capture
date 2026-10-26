// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// ============================================================
//  Does a jog actually move the mount, in the direction asked?
//
//  It did not, westward or southward, from 2026-10-17. The emulated
//  mount sat still while the app logged a correctly formed command,
//  so every bench test of jogging, recentring and tracking was
//  half-blind without anything failing.
//
//  The cause is the shape of defect this suite exists for. eef6c25
//  lifted the jog rate table out of MountEmulator into IMount.h so the
//  app and the mount would share one idea of what "rate 9" means --
//  a good change. The emulator's own lookup had taken the MAGNITUDE of
//  a signed rate:
//
//      int r = rate < 0 ? -rate : rate;
//
//  The shared one takes a PRESET INDEX and clamps anything below zero
//  to kRate[0], which is "stopped". The call site kept passing its
//  signed value, so direction 1 -- west and south -- looked up 0
//  arcsec/s and the mount did nothing. Nothing threw, nothing logged,
//  and the command bytes on the wire were right the whole time.
//
//  tests/slew_variable.cpp already gates the ENCODER: that the rate on
//  the wire is the rate asked for. Nothing gated the other half --
//  that a mount told to move, moves. This is that half.
// ============================================================

#include "core/MountEmulator.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>

using namespace raduk::core;

static int g_fail = 0;

static void Check(bool ok, const std::string& what, const std::string& detail) {
    printf("  [%s] %-50s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

// The sim loop runs at 10 Hz, so a jog needs a few ticks to show.
// 10 Hz sim loop. A 400 ms window catches three ticks or four
// depending on where it starts, which is a 33% swing -- enough to make
// an equal-magnitude comparison fail for no reason. A full second
// catches ten, so the quantisation is a tenth.
static void Settle(int ms = 1000) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Jog one axis for a while and report how far each coordinate moved.
static void JogAndMeasure(MountEmulator& m, int axis, int dir, int rate,
                          double& dRaHours, double& dDecDeg) {
    double ra0 = 0, dec0 = 0, ra1 = 0, dec1 = 0;
    m.GetRADec(ra0, dec0);
    m.SlewFixed(axis, dir, rate);
    Settle();
    m.StopSlew(axis);
    m.GetRADec(ra1, dec1);
    dRaHours = ra1 - ra0;
    // RA wraps at 24h; a jog never crosses it, so fold a wrap back.
    if (dRaHours >  12.0) dRaHours -= 24.0;
    if (dRaHours < -12.0) dRaHours += 24.0;
    dDecDeg = dec1 - dec0;
}

int main() {
    printf("\n=== Mount jog: does it move, and which way? ===\n");

    MountEmulator m;
    if (!m.Connect("EMU")) { printf("[FAIL] emulator would not connect\n"); return 1; }
    // Tracking would add sidereal drift to every measurement below.
    m.SetTrackingMode(IMount::OFF);
    Settle(200);

    char buf[160];

    // A jog has to clear the IDLE DRIFT, not merely zero. With tracking
    // off the sky turns under a parked mount and the emulator models
    // that -- about 0.00025 h of RA per second -- so a threshold of
    // 1e-4 is satisfied by a mount standing perfectly still. Rate 9
    // moves ~0.18 h in the same second, so these sit well above the
    // drift and well below the signal.
    //
    // Found by restoring the defect and watching "west moves RA" PASS
    // on drift alone. A check that cannot fail is worse than none.
    const double RA_MOVED  = 0.01;   // hours
    const double DEC_MOVED = 0.10;   // degrees

    printf("\n1. Every direction moves\n");
    {
        // Rate 9 is 3 deg/s, so 0.4 s is about 1.2 degrees -- far above
        // any drift still in the loop, and unmistakable in sign.
        double dRa = 0, dDec = 0;

        JogAndMeasure(m, 0, 0, 9, dRa, dDec);
        const double east = dRa;
        Check(std::fabs(dRa) > RA_MOVED, "east (axis 0, dir 0) moves RA",
              (snprintf(buf, sizeof(buf), "%+.4f h", dRa), buf));

        JogAndMeasure(m, 0, 1, 9, dRa, dDec);
        const double west = dRa;
        // THE REGRESSION. This read drift-only, never the jog.
        Check(std::fabs(dRa) > RA_MOVED, "west (axis 0, dir 1) moves RA",
              (snprintf(buf, sizeof(buf), "%+.4f h", dRa), buf));

        JogAndMeasure(m, 1, 0, 9, dRa, dDec);
        const double north = dDec;
        Check(std::fabs(dDec) > DEC_MOVED, "north (axis 1, dir 0) moves Dec",
              (snprintf(buf, sizeof(buf), "%+.4f deg", dDec), buf));

        JogAndMeasure(m, 1, 1, 9, dRa, dDec);
        const double south = dDec;
        // THE REGRESSION, other axis.
        Check(std::fabs(dDec) > DEC_MOVED, "south (axis 1, dir 1) moves Dec",
              (snprintf(buf, sizeof(buf), "%+.4f deg", dDec), buf));

        printf("\n2. Opposite directions are opposite, and equal\n");
        Check(east * west < 0.0, "east and west move RA opposite ways",
              (snprintf(buf, sizeof(buf), "%+.4f vs %+.4f h", east, west), buf));
        Check(north * south < 0.0, "north and south move Dec opposite ways",
              (snprintf(buf, sizeof(buf), "%+.4f vs %+.4f deg", north, south), buf));
        // Same rate, same duration: the magnitudes must match. A
        // direction that moves at a DIFFERENT speed is the subtler
        // version of the same defect, and would pass the sign checks.
        Check(std::fabs(std::fabs(east) - std::fabs(west)) < std::fabs(east) * 0.15,
              "  and by the same amount, not merely the same sign",
              (snprintf(buf, sizeof(buf), "|%.4f| vs |%.4f| h", east, west), buf));
        Check(std::fabs(std::fabs(north) - std::fabs(south)) < std::fabs(north) * 0.15,
              "  likewise on declination",
              (snprintf(buf, sizeof(buf), "|%.4f| vs |%.4f| deg", north, south), buf));
    }

    printf("\n3. The rate is honoured, not just the direction\n");
    {
        double dRa = 0, dDec = 0;
        JogAndMeasure(m, 0, 0, 9, dRa, dDec);
        const double fast = std::fabs(dRa);
        JogAndMeasure(m, 0, 0, 5, dRa, dDec);
        const double fine = std::fabs(dRa);
        // Rate 9 is 10800 arcsec/s, rate 5 is 240.7 -- a factor of ~45.
        Check(fast > fine * 10.0, "rate 9 travels much further than rate 5",
              (snprintf(buf, sizeof(buf), "%.4f h vs %.4f h", fast, fine), buf));
        // And in the same direction: a rate change must not flip sign.
        Check(dRa > 0.0, "and rate 5 still goes the same way as rate 9",
              (snprintf(buf, sizeof(buf), "%+.5f h", dRa), buf));
    }

    printf("\n4. Stopping stops\n");
    {
        // NOT "is it perfectly still". With tracking off, a parked
        // mount's RA genuinely moves -- the sky turns under it, and the
        // emulator models that. The question is whether the JOG stopped,
        // so measure the idle rate first and compare against it.
        double ra0 = 0, dec0 = 0, ra1 = 0, dec1 = 0;
        m.GetRADec(ra0, dec0);
        Settle();
        m.GetRADec(ra1, dec1);
        double idle = ra1 - ra0;
        if (idle >  12.0) idle -= 24.0;
        if (idle < -12.0) idle += 24.0;

        m.SlewFixed(0, 1, 9);
        Settle(300);
        m.StopSlew(0);
        Settle(150);                       // let the last tick land
        m.GetRADec(ra0, dec0);
        Settle();
        m.GetRADec(ra1, dec1);
        double after = ra1 - ra0;
        if (after >  12.0) after -= 24.0;
        if (after < -12.0) after += 24.0;

        // Rate 9 is 3 deg/s; a jog still running would show ~0.2 h in a
        // second, four orders of magnitude above the idle rate.
        Check(std::fabs(after - idle) < 1e-4,
              "after StopSlew only the idle drift remains",
              (snprintf(buf, sizeof(buf), "idle %+.6f h, after stop %+.6f h",
                        idle, after), buf));
    }

    printf("\n5. A variable rate moves too, and both ways\n");
    {
        // eef6c25's other half: a rate in arcsec/s, not a preset.
        double ra0 = 0, dec0 = 0, ra1 = 0, dec1 = 0;
        m.GetRADec(ra0, dec0);
        m.SlewVariable(0, 0, 3600.0);     // 1 deg/s
        Settle();
        m.StopSlew(0);
        m.GetRADec(ra1, dec1);
        double up = ra1 - ra0;
        if (up >  12.0) up -= 24.0;
        if (up < -12.0) up += 24.0;

        m.GetRADec(ra0, dec0);
        m.SlewVariable(0, 1, 3600.0);
        Settle();
        m.StopSlew(0);
        m.GetRADec(ra1, dec1);
        double down = ra1 - ra0;
        if (down >  12.0) down -= 24.0;
        if (down < -12.0) down += 24.0;

        Check(std::fabs(up) > RA_MOVED && std::fabs(down) > RA_MOVED,
              "variable rate moves in both directions",
              (snprintf(buf, sizeof(buf), "%+.5f h / %+.5f h", up, down), buf));
        Check(up * down < 0.0, "  and they are opposite", "");
    }

    m.Disconnect();
    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
