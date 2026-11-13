#pragma once
// ============================================================
//  DropLedger.h -- phase 146.0: every frame a run did not keep, accounted
//
//  A frame can be lost in four places, and until now only two were
//  counted, and only as running totals:
//
//    the camera or USB   ASIGetDroppedFrames, cumulative since the camera
//                        opened (so a run is charged its own delta here)
//    by choice           the quality gate shut, or the run paused: skipped
//                        on purpose, never a loss
//    our buffer          the write buffer was full
//    nowhere reported    a hole in the frame timestamps that none of the
//                        above explains
//
//  The fourth is found from the timestamps the writer sees: an interval
//  over 1.5x the run's median is a gap, and it implies round(gap/median)-1
//  frames missing. Every missing frame then has a home:
//
//    implied missing = SDK drops + skipped + buffer drops + unexplained
//
//  so "unexplained" is what the first three cannot account for, floored
//  at 0. A run whose median interval is within 15% of its exposure was
//  limited by the exposure; otherwise by transfer (USB, the camera's read
//  out, or our side). Deep-sky runs (phase 149.3) are exposure-limited by
//  nature, and there only the drops and gaps matter, not the rate.
//
//  Per run and per camera (the caller keys it by CameraInfo.name, rule 1
//  of phase 149). Pure: tests/drop_ledger.cpp.
// ============================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace raduk::core {

struct DropReport {
    uint64_t framesWritten   = 0;
    uint64_t droppedSdk      = 0;   // this run's share of the SDK's count
    uint64_t droppedBuffer   = 0;
    uint64_t skippedByChoice = 0;   // gate shut or paused
    uint64_t gaps            = 0;   // intervals over 1.5x the median
    uint64_t impliedMissing  = 0;   // frames those gaps imply
    uint64_t unexplained     = 0;   // implied missing nobody else accounts for
    double   medianIntervalMs = 0.0;
    double   effectiveFps     = 0.0;   // frames written over the run's span
    double   nominalFps       = 0.0;   // 1000 / median interval
    bool     exposureLimited  = false;
    bool     haveTiming       = false; // at least 3 frames
};

class DropLedger {
public:
    static constexpr double kGapFactor = 1.5;
    static constexpr size_t kMaxIntervals = 2000000;   // ~11 h at 50 fps

    void Begin(int sdkDropsNow) {
        m_sdkAtStart = sdkDropsNow;
        m_frames = 0;
        m_lastUs = 0;
        m_firstUs = 0;
        m_intervals.clear();
    }

    // Each frame the writer kept, with its capture timestamp in us.
    void OnWritten(uint64_t timestampUs) {
        if (m_frames == 0) m_firstUs = timestampUs;
        else if (timestampUs > m_lastUs && m_intervals.size() < kMaxIntervals)
            m_intervals.push_back((uint32_t)(std::min<uint64_t>)(timestampUs - m_lastUs, 0xFFFFFFFFu));
        m_lastUs = timestampUs;
        ++m_frames;
    }

    DropReport Finish(int sdkDropsNow, uint64_t bufferDrops, uint64_t skipped,
                      double exposureMs) const {
        DropReport r;
        r.framesWritten   = m_frames;
        r.droppedSdk      = sdkDropsNow > m_sdkAtStart ? (uint64_t)(sdkDropsNow - m_sdkAtStart) : 0;
        r.droppedBuffer   = bufferDrops;
        r.skippedByChoice = skipped;
        if (m_intervals.size() >= 2) {
            std::vector<uint32_t> s(m_intervals);
            std::nth_element(s.begin(), s.begin() + (std::ptrdiff_t)(s.size() / 2), s.end());
            const double med = s[s.size() / 2];
            if (med > 0) {
                r.haveTiming = true;
                r.medianIntervalMs = med / 1000.0;
                r.nominalFps = 1e6 / med;
                for (uint32_t d : m_intervals) {
                    if (d > kGapFactor * med) {
                        ++r.gaps;
                        const double k = std::floor((double)d / med + 0.5) - 1.0;
                        if (k > 0) r.impliedMissing += (uint64_t)k;
                    }
                }
                const double spanS = (double)(m_lastUs - m_firstUs) / 1e6;
                if (spanS > 0) r.effectiveFps = (double)(m_frames - 1) / spanS;
                r.exposureLimited = exposureMs > 0 &&
                    std::fabs(r.medianIntervalMs - exposureMs) <= 0.15 * exposureMs;
            }
        }
        const uint64_t known = r.droppedSdk + r.skippedByChoice + r.droppedBuffer;
        r.unexplained = r.impliedMissing > known ? r.impliedMissing - known : 0;
        return r;
    }

    uint64_t Frames() const { return m_frames; }

private:
    int      m_sdkAtStart = 0;
    uint64_t m_frames = 0, m_lastUs = 0, m_firstUs = 0;
    std::vector<uint32_t> m_intervals;
};

// One log line. camera: CameraInfo.name, so each camera keeps its own figures.
inline std::string DropLine(const DropReport& r, const std::string& camera) {
    char b[400];
    if (r.haveTiming) {
        snprintf(b, sizeof(b),
                 "%s: %llu frames at %.1f fps (nominal %.1f, median %.2f ms, %s-limited); "
                 "dropped: camera/USB %llu, our buffer %llu; skipped by choice %llu; "
                 "timestamp gaps %llu implying %llu missing, %llu unexplained",
                 camera.empty() ? "camera" : camera.c_str(),
                 (unsigned long long)r.framesWritten, r.effectiveFps, r.nominalFps, r.medianIntervalMs,
                 r.exposureLimited ? "exposure" : "transfer",
                 (unsigned long long)r.droppedSdk, (unsigned long long)r.droppedBuffer,
                 (unsigned long long)r.skippedByChoice, (unsigned long long)r.gaps,
                 (unsigned long long)r.impliedMissing, (unsigned long long)r.unexplained);
    } else {
        snprintf(b, sizeof(b),
                 "%s: %llu frames (too few to time); dropped: camera/USB %llu, our buffer %llu; "
                 "skipped by choice %llu",
                 camera.empty() ? "camera" : camera.c_str(),
                 (unsigned long long)r.framesWritten, (unsigned long long)r.droppedSdk,
                 (unsigned long long)r.droppedBuffer, (unsigned long long)r.skippedByChoice);
    }
    return b;
}

}  // namespace raduk::core
