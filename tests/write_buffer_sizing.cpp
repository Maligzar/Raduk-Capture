// ============================================================
//  How big does the write buffer get, and is that defensible?
//
//  The pipeline's whole design is: buffer to memory, drain to disk,
//  so the camera never waits on the drive. That only works if the
//  buffer is sized against the run and the machine rather than against
//  a constant -- it was 512 slots, chosen when 47 fps was the assumed
//  rate.
//
//  Measured on this rig 2026-10-14: the capture drive sustains 292 MB/s
//  but dips to 141 MB/s within a single run, and 1280x960 RAW16 at 120
//  fps wants 295 MB/s. Absorbing exactly that is the buffer's job, so
//  how it is sized is worth a test.
// ============================================================

#include "core/WriteBuffer.h"

#include <cstdio>
#include <string>

using raduk::core::PlanWriteBufferSlots;

static int g_fail = 0;

static void Check(bool ok, const std::string& what, const std::string& detail) {
    printf("  [%s] %-58s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static void CheckEq(int got, int want, const std::string& what) {
    char d[96];
    snprintf(d, sizeof(d), "got %d, expected %d", got, want);
    Check(got == want, what, d);
}

int main() {
    printf("\n=== Write buffer sizing ===\n\n");

    const size_t RAW8  = 1280ull * 960;          // 1.23 MB
    const size_t RAW16 = RAW8 * 2;               // 2.46 MB
    const uint64_t GB  = 1024ull * 1024 * 1024;

    // ── The case this rig actually runs ──────────────────────
    printf("1. The configured run\n");
    {
        // saturn.ini: 3000 frames, 1280x960 RAW8, on a machine with
        // ~57 GB free. The whole run is 3.7 GB, so it should all fit
        // and the disk should never be on the critical path.
        const int n = PlanWriteBufferSlots(3000, 0.0, 120.0, RAW8, 57 * GB);
        CheckEq(n, 3000, "a 3000-frame run is buffered whole");

        const double gb = n * (double)RAW8 / 1e9;
        char d[96];
        snprintf(d, sizeof(d), "%.2f GB, %.1f s at 120 fps", gb, n / 120.0);
        Check(gb < 4.0, "and costs under 4 GB to do it", d);
    }
    {
        // RAW16 doubles it and should still fit.
        const int n = PlanWriteBufferSlots(3000, 0.0, 120.0, RAW16, 57 * GB);
        CheckEq(n, 3000, "the same run in RAW16 also fits");
    }

    // ── Never worse than the constant it replaced ────────────
    printf("\n2. Never worse than the old fixed 512\n");
    {
        const int n = PlanWriteBufferSlots(100, 0.0, 120.0, RAW8, 57 * GB);
        CheckEq(n, 512, "a short 100-frame run still gets the old floor");
    }
    {
        const int n = PlanWriteBufferSlots(0, 0.0, 0.0, RAW8, 57 * GB);
        Check(n >= 512, "an unbounded run gets at least the floor",
              std::to_string(n));
    }

    // ── Memory is the binding constraint, not the floor ──────
    printf("\n3. Memory wins over every other rule\n");
    {
        // 400 MB free. Half of that is 200 MB, which is 170 slots --
        // fewer than the floor. Allocating the floor anyway is how you
        // get a failed allocation and no recording at all, so the
        // memory limit has to win.
        const uint64_t tight = 400ull * 1024 * 1024;
        const int n = PlanWriteBufferSlots(3000, 0.0, 120.0, RAW8, tight);
        Check(n < 512, "a machine short on RAM gets less than the floor",
              std::to_string(n) + " slots");
        Check(n * (double)RAW8 <= tight * 0.5 + RAW8,
              "and never more than half of free memory", "");
        Check(n >= 1, "but never zero", std::to_string(n));
    }
    {
        // Absurdly tight: one slot is still better than refusing.
        const int n = PlanWriteBufferSlots(3000, 0.0, 120.0, RAW8, RAW8);
        Check(n >= 1, "even near-exhausted memory yields a usable buffer",
              std::to_string(n));
    }

    // ── A time-limited run ───────────────────────────────────
    printf("\n4. Time limits, converted through the frame rate\n");
    {
        const int n = PlanWriteBufferSlots(0, 20.0, 120.0, RAW8, 57 * GB);
        CheckEq(n, 2400, "20 s at 120 fps asks for 2400 slots");
    }
    {
        // The same 20 seconds at a slower rate needs fewer frames, and
        // the floor catches it.
        const int n = PlanWriteBufferSlots(0, 20.0, 15.0, RAW8, 57 * GB);
        CheckEq(n, 512, "20 s at 15 fps falls back to the floor");
    }

    // ── The ceiling ──────────────────────────────────────────
    printf("\n5. The backstop on an unbounded run\n");
    {
        const int n = PlanWriteBufferSlots(1000000, 0.0, 120.0, RAW8, 57 * GB);
        Check(n <= 4096, "a million-frame limit is capped", std::to_string(n));
        char d[96];
        snprintf(d, sizeof(d), "%.1f s of total disk silence at 120 fps",
                 n / 120.0);
        Check(n / 120.0 > 30.0, "and the cap is still far past any real stall", d);
    }

    // ── Degenerate input ─────────────────────────────────────
    printf("\n6. Nonsense in, something usable out\n");
    {
        Check(PlanWriteBufferSlots(3000, 0.0, 120.0, 0, 57 * GB) == 512,
              "a zero frame size does not divide by zero", "");
        Check(PlanWriteBufferSlots(0, 20.0, 0.0, RAW8, 57 * GB) >= 512,
              "a time limit with no frame rate still returns a buffer", "");
    }

    printf("\n");
    if (g_fail == 0) printf("=== the buffer is sized to the run and the machine ===\n\n");
    else             printf("=== %d CHECK(S) FAILED ===\n\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
