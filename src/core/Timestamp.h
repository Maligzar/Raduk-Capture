#pragma once
// ============================================================
// Timestamp.h — QPC-based microsecond timing for RadukCapture
// ============================================================
#include <windows.h>
#include <cstdint>
#include <cstdio>

namespace raduk::core {

class Timestamp {
public:
    static void Init() {
        (void)Frequency();
        printf("[Timestamp] QPC frequency: %lld Hz\n", Frequency());
    }

    static uint64_t Now() {
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        int64_t freq = Frequency();
        if (freq == 0) return 0;
        // div/mod form: counter*1e6 overflows uint64 after ~21 days of
        // uptime at a 10MHz QPC (sooner at TSC rates)
        uint64_t c = static_cast<uint64_t>(counter.QuadPart);
        uint64_t f = static_cast<uint64_t>(freq);
        return (c / f) * 1000000ULL + ((c % f) * 1000000ULL) / f;
    }

    // The midpoint of integration, estimated from the moment the frame
    // arrived.
    //
    // A SER per-frame timestamp is meant to say when the light was
    // collected, and the honest answer for a frame integrated over
    // `exposureMs` is the middle of that window, not either end. The
    // capture thread can only observe arrival, so the estimate is
    // arrival minus half the exposure.
    //
    // What this does NOT remove, and the reason this is an estimate
    // rather than a measurement: arrival is later than the end of
    // exposure by the sensor readout and the USB transfer, and neither
    // is observable from here. The correction takes out the one term
    // that is known and leaves that one in, so the result is still
    // biased late. At a 10 ms exposure it removes 5 ms of a bias that
    // may be a few ms larger again; at 1 ms it removes 0.5 ms and the
    // residual dominates. Better than arrival, and not the truth.
    //
    // The real fix is hardware: ASIGetVideoDataGPS returns an
    // ASI_DATE_TIME taken at the sensor, but only on the GPS models,
    // and the ASI678MC is not one of them.
    //
    // Saturating at zero is not defensive dressing: QPC is measured
    // from boot, so the subtraction only underflows in a test that
    // passes a small arrival value, which is exactly where a wrapped
    // uint64 would be hardest to read.
    static uint64_t ExposureMidpointUs(uint64_t arrivalUs, double exposureMs) {
        if (!(exposureMs > 0.0)) return arrivalUs;   // also catches NaN
        const uint64_t halfUs = static_cast<uint64_t>(exposureMs * 500.0);
        return (arrivalUs > halfUs) ? arrivalUs - halfUs : 0;
    }

    static double ElapsedMs(uint64_t startUs, uint64_t endUs) {
        return static_cast<double>(endUs - startUs) / 1000.0;
    }

    static double ElapsedSec(uint64_t startUs, uint64_t endUs) {
        return static_cast<double>(endUs - startUs) / 1000000.0;
    }

private:
    // Function-local static: initialized exactly once on first call,
    // shared across ALL translation units — no ODR issue.
    static int64_t Frequency() {
        static int64_t freq = []() -> int64_t {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            return f.QuadPart;
        }();
        return freq;
    }
};

} // namespace raduk::core
