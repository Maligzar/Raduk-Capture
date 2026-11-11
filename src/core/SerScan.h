#pragma once
// ============================================================
// SerScan.h -- a quality score for every frame of a SER, in the background
//
// The PLAY window draws these along its slider, so a recording's good
// stretches stand out as peaks and frames with no target in them (the
// planet drifted out, cloud, a capture of nothing) as gaps. That is the
// question PIPP's frame-1 preview could not answer on 2026-11-10.
//
// Per frame: TargetFinder looks for the bright target; if there is one,
// FocusMetric's planet HFD score (1000 / HFD, larger is sharper) is
// measured on it. The same two the live app uses for autofocus, so a
// number here means what it means there. The score is:
//    -1  not scanned yet
//     0  no target in the frame
//    >0  target found; its focus score (a tiny value if it was found
//        but the HFD could not be measured)
//
// Runs on its own thread with its own Reader, so the window's reads for
// display never wait on it. Portable: tests/ser_reader.cpp runs it.
// ============================================================

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "SerReader.h"
#include "TargetFinder.h"
#include "FocusMetric.h"

namespace raduk::core::ser {

inline float ScoreFrame(const std::vector<uint16_t>& plane, int w, int h) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(plane.data());
    const TargetFind tf = FindBrightTarget(bytes, w, h, 16);
    if (!tf.found) return 0.0f;
    const FocusMeasure fm = MeasurePlanetFocus(bytes, (uint32_t)w, (uint32_t)h, 16, tf);
    return fm.valid ? fm.score : 0.001f;
}

class Scan {
public:
    ~Scan() { Stop(); }

    void Start(const std::string& path, int64_t frames) {
        Stop();
        m_n = frames;
        m_scores.reset(new std::atomic<float>[(size_t)frames]);
        for (int64_t i = 0; i < frames; ++i) m_scores[(size_t)i].store(-1.0f);
        m_done.store(0);
        m_cancel.store(false);
        m_running.store(true);
        m_thread = std::thread([this, path]() { Run(path); });
    }

    void Stop() {
        m_cancel.store(true);
        if (m_thread.joinable()) m_thread.join();
        m_running.store(false);
    }

    bool    Running() const { return m_running.load(); }
    int64_t Frames()  const { return m_n; }
    int64_t Scanned() const { return m_done.load(); }
    float   Score(int64_t i) const {
        return (m_scores && i >= 0 && i < m_n) ? m_scores[(size_t)i].load(std::memory_order_relaxed) : -1.0f;
    }

private:
    // Two passes: every 64th frame first, so the whole trace has a shape
    // within seconds, then the rest. A long file is several GB to read.
    void Run(const std::string& path) {
        Reader r;
        if (r.Open(path)) {
            const Info& in = r.GetInfo();
            std::vector<uint8_t> raw;
            std::vector<uint16_t> plane;
            const int64_t coarse = 64;
            for (int pass = 0; pass < 2 && !m_cancel.load(); ++pass) {
                for (int64_t i = 0; i < m_n && !m_cancel.load(); ++i) {
                    const bool first = (i % coarse) == 0;
                    if ((pass == 0) != first) continue;
                    if (!r.ReadFrame(i, raw)) { m_scores[(size_t)i].store(0.0f); m_done.fetch_add(1); continue; }
                    ToPlane16(raw.data(), in, plane);
                    m_scores[(size_t)i].store(ScoreFrame(plane, in.width, in.height),
                                              std::memory_order_relaxed);
                    m_done.fetch_add(1);
                }
            }
        }
        m_running.store(false);
    }

    std::thread                         m_thread;
    std::unique_ptr<std::atomic<float>[]> m_scores;
    int64_t                             m_n = 0;
    std::atomic<int64_t>                m_done{0};
    std::atomic<bool>                   m_cancel{false};
    std::atomic<bool>                   m_running{false};
};

} // namespace raduk::core::ser
