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
