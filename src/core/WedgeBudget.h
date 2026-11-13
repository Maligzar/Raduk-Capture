#pragma once
// ============================================================
// WedgeBudget.h -- phase 135.3: a stalled camera, measured in time
//
// CaptureThread restarted the camera after 20 consecutive GetFrame
// timeouts and said that was "~500 ms". Each timeout is 2 x exposure +
// 500 ms, so it was at least 10 s -- ten seconds of a planet's best
// seeing lost before anything was done.
//
// Now the loop keeps the time of the last good frame and declares a
// wedge when nothing has arrived for the budget:
//
//     budget = max(1.5 s, 10 x expected frame interval)
//
// where the expected interval is the exposure or 1/fps, whichever is
// longer. The per-call SDK timeout stays as ZWO advise (2 x exposure +
// 500 ms) but is capped at the budget, so one call cannot outlast it.
// At planetary exposures that is about 1.5 s to a restart instead of 10;
// at seconds-long lunar or deep-sky exposures the budget grows with the
// exposure, so a slow frame is never mistaken for a wedge.
//
// Pure, so tests/capture_wedge.cpp checks it with a fake clock; 1.5 s
// against a real USB hiccup is a clear-night question.
// ============================================================

#include <algorithm>
#include <cstdint>

namespace raduk::core {

// UNVERIFIED(item 56): count 'Pipeline wedge' lines while frames flow on the real camera
inline constexpr double kWedgeFloorMs = 1500.0;
inline constexpr double kWedgeIntervals = 10.0;

inline double WedgeBudgetMs(double exposureMs, double fps) {
    const double fromFps = fps > 0.0 ? 1000.0 / fps : 0.0;
    const double interval = (std::max)((std::max)(exposureMs, 0.0), fromFps);
    return (std::max)(kWedgeFloorMs, kWedgeIntervals * interval);
}

inline int CallTimeoutMs(double exposureMs, double budgetMs) {
    const double zwo = 2.0 * (std::max)(exposureMs, 0.0) + 500.0;
    return (int)(std::min)(zwo, budgetMs);
}

// The time since the last good frame, in microseconds of whatever clock
// the caller uses. OnTimeout says whether this one completes a wedge; if
// so the clock restarts, so the next restart needs a full budget more.
class WedgeClock {
public:
    void OnFrame(uint64_t nowUs) { m_lastGoodUs = nowUs; }
    void Reset(uint64_t nowUs) { m_lastGoodUs = nowUs; }
    bool OnTimeout(uint64_t nowUs, double budgetMs) {
        if (nowUs < m_lastGoodUs) { m_lastGoodUs = nowUs; return false; }   // clock went back
        if (double(nowUs - m_lastGoodUs) / 1000.0 < budgetMs) return false;
        m_lastGoodUs = nowUs;
        return true;
    }
    uint64_t LastGoodUs() const { return m_lastGoodUs; }

private:
    uint64_t m_lastGoodUs = 0;
};

} // namespace raduk::core
