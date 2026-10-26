// ============================================================
//  What a SER per-frame timestamp claims
//
//  The trailer is supposed to say when the light was collected.
//  Until now it said when the frame arrived, which is the end of
//  the exposure plus whatever the readout and the USB transfer
//  cost. Phase 59 had already fixed a worse version of this, where
//  the stamp was the moment the WRITER got to the frame and could
//  be seconds late; arrival was the fix for that and is not the
//  end of the story.
//
//  SharpCap's author has said in public that SharpCap does the
//  same thing for SER while its FITS path does subtract the
//  exposure, so this was parity, not a deficit. Subtracting half
//  the exposure is a bounded move past parity.
//
//  What is tested here is the arithmetic and its edges, because
//  that is all that can be tested on a bench: whether the estimate
//  is right in the one term it can remove. The term it cannot
//  remove (readout and transfer) needs a camera, and belongs in
//  CLEAR-NIGHT-CHECKLIST.md, not here.
// ============================================================

#include "core/Timestamp.h"

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <limits>

using namespace raduk::core;

static int g_fail = 0;

static void Check(bool ok, const char* what, uint64_t got, uint64_t want) {
    if (ok) {
        printf("  ok    %s\n", what);
    } else {
        printf("  FAIL  %s: got %llu, wanted %llu\n",
               what, (unsigned long long)got, (unsigned long long)want);
        ++g_fail;
    }
}

int main() {
    printf("SER timestamp: midpoint of integration\n");

    // A plain planetary exposure. 10 ms means the middle of the
    // window is 5000 us before the frame landed.
    {
        const uint64_t arrival = 1'000'000'000ULL;
        uint64_t got = Timestamp::ExposureMidpointUs(arrival, 10.0);
        Check(got == arrival - 5000, "10 ms exposure backs up 5000 us", got, arrival - 5000);
    }

    // Sub-millisecond, where the correction is small but must not
    // round to nothing: 0.5 ms is 250 us.
    {
        const uint64_t arrival = 1'000'000'000ULL;
        uint64_t got = Timestamp::ExposureMidpointUs(arrival, 0.5);
        Check(got == arrival - 250, "0.5 ms exposure backs up 250 us", got, arrival - 250);
    }

    // A long exposure, to prove nothing is clamped at some small limit
    // and that the scale is linear rather than capped.
    {
        const uint64_t arrival = 1'000'000'000ULL;
        uint64_t got = Timestamp::ExposureMidpointUs(arrival, 30'000.0);  // 30 s
        Check(got == arrival - 15'000'000ULL, "30 s exposure backs up 15 s",
              got, arrival - 15'000'000ULL);
    }

    // Zero and negative exposures leave the stamp alone rather than
    // inventing a correction. A zero can arrive from a profile that
    // has not been applied yet.
    {
        const uint64_t arrival = 1'000'000'000ULL;
        Check(Timestamp::ExposureMidpointUs(arrival, 0.0) == arrival,
              "zero exposure is left alone", Timestamp::ExposureMidpointUs(arrival, 0.0), arrival);
        Check(Timestamp::ExposureMidpointUs(arrival, -5.0) == arrival,
              "negative exposure is left alone", Timestamp::ExposureMidpointUs(arrival, -5.0), arrival);
    }

    // NaN must not propagate into a timestamp. The guard is written
    // as !(x > 0) precisely so NaN takes this branch.
    {
        const uint64_t arrival = 1'000'000'000ULL;
        const double nan = std::numeric_limits<double>::quiet_NaN();
        uint64_t got = Timestamp::ExposureMidpointUs(arrival, nan);
        Check(got == arrival, "NaN exposure is left alone", got, arrival);
    }

    // The subtraction must not wrap. QPC counts from boot so this
    // cannot happen in the app, which is exactly why it would be
    // missed if it ever did.
    {
        uint64_t got = Timestamp::ExposureMidpointUs(100, 10.0);  // wants -4900
        Check(got == 0, "underflow saturates at zero rather than wrapping", got, 0);
    }

    // The property that actually matters to WinJUPOS: the interval
    // between two frames is unchanged while the exposure is steady,
    // because both stamps move by the same amount.
    {
        const uint64_t a1 = 1'000'000'000ULL, a2 = a1 + 33'333ULL;
        uint64_t m1 = Timestamp::ExposureMidpointUs(a1, 12.0);
        uint64_t m2 = Timestamp::ExposureMidpointUs(a2, 12.0);
        Check((m2 - m1) == (a2 - a1), "a steady exposure leaves frame intervals intact",
              m2 - m1, a2 - a1);
    }

    // And the one case where it does not: a changed exposure shifts
    // one interval by half the change, once. Documented at the call
    // site, asserted here so the size of it is on the record.
    {
        const uint64_t a1 = 1'000'000'000ULL, a2 = a1 + 33'333ULL;
        uint64_t m1 = Timestamp::ExposureMidpointUs(a1, 12.0);   // -6000
        uint64_t m2 = Timestamp::ExposureMidpointUs(a2, 20.0);   // -10000
        Check((m2 - m1) == (a2 - a1) - 4000, "changing the exposure shifts one interval by half the change",
              m2 - m1, (a2 - a1) - 4000);
    }

    if (g_fail == 0) printf("PASS\n"); else printf("FAIL: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
