#pragma once
// ============================================================
// FrameAnalyzer.h — Phase 3: per-frame image analysis
// ============================================================

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "FrameSlot.h"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <thread>
#include <atomic>
#include <mutex>
#include <array>
#include <vector>
#include <deque>
#include <chrono>
#include <windows.h>
#include "Sharpness.h"

namespace raduk::core {

struct AnalysisResult {
    bool valid = false;

    // ── Luminance histogram (brightness-weighted from Bayer) ──
    std::array<uint32_t, 256> histogram{};

    // ── Per-channel histograms (RGGB Bayer demosaic) ──────────
    // R = even row, even col
    // G = average of Gr (even row, odd col) and Gb (odd row, even col)
    // B = odd row, odd col
    std::array<uint32_t, 256> histR{};
    std::array<uint32_t, 256> histG{};
    std::array<uint32_t, 256> histB{};

    // Per-channel clip % (fraction of pixels at 255)
    float clipR = 0.0f, clipG = 0.0f, clipB = 0.0f;
    float clipPct = 0.0f;  // luminance clip

    // Per-channel mean (0-255)
    float meanR = 0.0f, meanG = 0.0f, meanB = 0.0f;
    float meanBrightness = 0.0f;

    // Luminance below which HIGHLIGHT_PCTILE of superpixels fall: the
    // brightness of the brightest real thing in the frame, which on a
    // planetary target is the planet.
    //
    // The frame mean cannot answer that question. A planet is a small
    // bright disc on black sky, so the mean is dominated by sky and
    // barely moves however the planet is exposed -- which is why
    // auto-exposure, which used to drive the mean to 50-70%, could not
    // settle: the only way to lift a mostly-black frame's mean to 152 DN
    // is to blow the planet out entirely.
    static constexpr float HIGHLIGHT_PCTILE = 0.999f;
    float highlightDN = 0.0f;

    // ── Sharpness / seeing ─────────────────────────────────────
    float sharpnessPct = 0.0f;   // Laplacian variance 0-100%
    float sharpRaw     = 0.0f;   // diagnostic: variance before noise subtraction
    float sharpNoise   = 0.0f;   // diagnostic: the subtracted noise term
    float seeingScore  = 0.0f;   // rolling 1-10

    // Rolling seeing history for graph — last 60 samples
    static constexpr int SEEING_HISTORY = 60;
    std::array<float, 60> seeingHistory{};
    int seeingHistoryCount = 0;

    // Minimum frame peak (0-255) before a centroid is attempted. Below
    // this a frame is noise, and a centroid computed from it would be
    // a random walk. 20/255 is about 8%.
    static constexpr uint8_t CENTROID_MIN_PEAK = 20;

    // ── Centroid (planet lock) ─────────────────────────────────
    bool  centroidValid = false;
    float centroidX     = 0.0f;
    float centroidY     = 0.0f;
    // Phase 69: why there is no centroid, not merely that there is
    // none. "Too faint" and "nothing in frame" need opposite responses
    // from the user -- raise exposure versus go and find the planet --
    // and the gate below could not tell them apart.
    bool  centroidTooFaint = false;   // brightest pixel under the gate
    uint8_t centroidPeak   = 0;       // frame peak, 0-255, for the message

    // ── Frame info ────────────────────────────────────────────
    uint32_t width  = 0;
    uint32_t height = 0;
};
class FrameAnalyzer {
public:
    FrameAnalyzer() = default;
    ~FrameAnalyzer() { Stop(); }

    FrameAnalyzer(const FrameAnalyzer&) = delete;
    FrameAnalyzer& operator=(const FrameAnalyzer&) = delete;

    void Start();

    void Stop();

    // Non-blocking submit. If the analyzer is busy with the previous
    // frame, this call returns immediately (the frame is skipped).
    // That's fine — we always want the LATEST result, never queue up work.
    void Submit(const FrameSlot* slot);

    bool HasResult() const {
        return m_resultReady.load(std::memory_order_acquire);
    }

    // Returns the latest analysis result. Clears the "ready" flag.
    AnalysisResult GetResult();

private:
    void Run();

    static constexpr size_t SEEING_WINDOW = 20; // samples in rolling window

    std::thread          m_thread;
    std::atomic<bool>    m_running{false};
    std::atomic<bool>    m_hasWork{false};
    std::atomic<bool>    m_resultReady{false};

    // Pending frame (written by main thread, read by analyzer thread)
    std::mutex           m_frameMutex;
    std::vector<uint8_t> m_frameBuffer;
    uint32_t             m_pendingW        = 0;
    uint32_t             m_pendingH        = 0;
    uint8_t              m_pendingBitDepth = 8;

    // Result (written by analyzer thread, read by main thread)
    std::mutex           m_resultMutex;
    AnalysisResult       m_result;

    // Seeing score rolling window
    // Phase 61: last valid centroid, used to position the sharpness crop
    // on the next frame. Reset when the planet is lost so the crop falls
    // back to frame centre rather than chasing a stale position.
    // Frames the centroid may stay lost before the sharpness crop
    // returns to frame centre (~0.7 s at 43 FPS).
    static constexpr int CENTROID_HOLD_FRAMES = 30;
    int                  m_centroidMissFrames = 0;

    float                m_lastCentroidX     = -1.0f;
    float                m_lastCentroidY     = -1.0f;
    bool                 m_lastCentroidValid = false;

    std::deque<float>    m_sharpHistory; // deque: O(1) pop_front vs O(N) vector erase
};

} // namespace raduk::core
