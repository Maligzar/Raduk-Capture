// ============================================================
//  The variable-rate slew command's wire encoding
//
//  Phase 93. NexStar's P 3 <axis> <dir> <hi> <lo> carries the rate as
//  arcseconds per second TIMES FOUR, big-endian across two bytes.
//
//  This is checked against values worked out by hand because the
//  failure mode is not a wrong pixel on a screen. A rate encoded
//  wrongly moves a telescope at a speed nobody asked for, in the
//  dark, possibly into its own tripod -- and it is exactly the kind
//  of arithmetic that looks right.
// ============================================================

#include "core/NexStarMount.h"
#include "core/IMount.h"

#include <cstdio>
#include <string>
#include <cmath>

using namespace raduk::core;
using raduk::core::nexstar::EncodeVariableRate;

static int g_fail = 0;

static void Check(bool ok, const std::string& what, const std::string& detail) {
    printf("  [%s] %-56s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static void CheckBytes(double rate, unsigned hi, unsigned lo,
                       const std::string& what) {
    const auto r = EncodeVariableRate(rate);
    char d[128];
    snprintf(d, sizeof(d), "%.3f\"/s -> %u/%u (wanted %u/%u)",
             rate, (unsigned)r.hi, (unsigned)r.lo, hi, lo);
    Check(r.hi == hi && r.lo == lo, what, d);
}

int main() {
    printf("\n=== Variable-rate slew encoding ===\n");

    // ── 1. Against hand arithmetic ───────────────────────────
    printf("\n1. The wire bytes\n");
    {
        // 1x sidereal is 15.041 arcsec/s. x4 = 60.16, rounds to 60.
        // 60 fits in the low byte, so hi is 0.
        CheckBytes(15.041, 0, 60, "one times sidereal is 0/60");

        // 240.7 arcsec/s (16x, the FINE preset). x4 = 962.8 -> 963.
        // 963 = 3*256 + 195.
        CheckBytes(240.7, 3, 195, "the FINE preset is 3/195");

        // 3600 arcsec/s = 1 deg/s. x4 = 14400 = 56*256 + 64.
        CheckBytes(3600.0, 56, 64, "one degree a second is 56/64");

        // 10800 arcsec/s = 3 deg/s. x4 = 43200 = 168*256 + 192.
        CheckBytes(10800.0, 168, 192, "three degrees a second is 168/192");
    }

    // ── 2. Edges ─────────────────────────────────────────────
    printf("\n2. Edges\n");
    {
        CheckBytes(0.0, 0, 0, "a zero rate encodes to zero, which stops it");

        // Direction is a separate byte in the command, so a negative
        // rate here is a caller mistake rather than a reversal. Take
        // the magnitude; the alternative is wrapping into an enormous
        // unsigned value and slewing at full speed.
        CheckBytes(-15.041, 0, 60, "a negative rate is treated as its size");

        // Two bytes cap at 65535 quarter-arcseconds = 16383.75 "/s,
        // which is 4.55 deg/s and faster than this mount can move.
        const auto hi = EncodeVariableRate(1.0e9);
        Check(hi.hi == 255 && hi.lo == 255,
              "an absurd rate saturates rather than wrapping",
              std::to_string(hi.hi) + "/" + std::to_string(hi.lo));
        Check(hi.actualArcsecPerSec <= 16384.0,
              "  and reports what it will really do",
              std::to_string(hi.actualArcsecPerSec));
    }

    // ── 3. What the mount will actually do ───────────────────
    //
    // The wire unit is a quarter of an arcsecond per second, so any
    // request is rounded to that. Saying so lets a caller log the real
    // figure instead of the one it asked for.
    printf("\n3. Quantisation is reported, not hidden\n");
    {
        const auto r = EncodeVariableRate(15.041);
        char d[96];
        snprintf(d, sizeof(d), "asked 15.041, sending %.2f", r.actualArcsecPerSec);
        Check(std::fabs(r.actualArcsecPerSec - 15.0) < 0.001,
              "1x sidereal quantises to 15.00 arcsec/s", d);

        const auto f = EncodeVariableRate(0.1);
        Check(f.actualArcsecPerSec == 0.0 || f.actualArcsecPerSec == 0.25,
              "and a rate under a quarter-arcsecond rounds honestly",
              std::to_string(f.actualArcsecPerSec));
    }

    // ── 4. The preset table, in one place ────────────────────
    //
    // These numbers lived inside MountEmulator and nowhere else, so
    // the emulator knew what a rate meant and the code talking to the
    // real mount did not.
    printf("\n4. The shared preset table\n");
    {
        Check(JogRateArcsecPerSec(0) == 0.0, "preset 0 is stopped", "");
        Check(std::fabs(JogRateArcsecPerSec(2) - 15.04) < 0.01,
              "preset 2 is one times sidereal",
              std::to_string(JogRateArcsecPerSec(2)));
        Check(JogRateArcsecPerSec(9) > JogRateArcsecPerSec(8),
              "and the table is monotonic at the top", "");
        Check(JogRateArcsecPerSec(-5) == 0.0 && JogRateArcsecPerSec(99) ==
              JogRateArcsecPerSec(9),
              "an out-of-range preset clamps rather than reading past the end",
              "");

        // The gap this exists to close: between preset 7 and 8 the
        // fixed scale jumps by nearly four times, and there is no way
        // to ask for anything in between.
        const double gap = JogRateArcsecPerSec(8) / JogRateArcsecPerSec(7);
        Check(gap > 3.0,
              "the fixed presets jump by 3.7x near the top, which is the point",
              std::to_string(gap));
        const auto mid = EncodeVariableRate(
            (JogRateArcsecPerSec(7) + JogRateArcsecPerSec(8)) / 2.0);
        Check(mid.actualArcsecPerSec > JogRateArcsecPerSec(7) &&
              mid.actualArcsecPerSec < JogRateArcsecPerSec(8),
              "  and a rate between them is now expressible",
              std::to_string(mid.actualArcsecPerSec));
    }

    printf("\n");
    if (g_fail == 0) printf("=== the rate on the wire is the rate asked for ===\n\n");
    else             printf("=== %d CHECK(S) FAILED ===\n\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
