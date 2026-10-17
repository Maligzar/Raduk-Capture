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
#include <string>
#include <cstdio>
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

    // ── Sensor analysis (Phase 36) ────────────────────────────
    //
    // The two greens, kept apart.
    //
    // histG above is their MEAN, and that is what the display shows,
    // which hides two things the deep dive measured (see
    // docs/HISTOGRAM-ACCURACY.md):
    //
    //  - A Gr/Gb split is invisible. A frame with Gr=20 and Gb=180
    //    produces a G histogram occupying ONE bin, at 100, identical to
    //    a frame with Gr=Gb=100. Every colour sensor has some split,
    //    from crosstalk differing between the green next to red and the
    //    green next to blue; a large one means something is wrong, and
    //    nothing on screen could say so.
    //  - Averaging two samples cuts noise by about root two, and only G
    //    gets it. So the G row always looks tighter than R and B for a
    //    reason that is not the sensor's colour response -- which
    //    undercuts the whole point of per-channel rows, namely reading
    //    colour balance off how far each row extends.
    //
    // Keeping both means the split can be stated as a number without
    // changing a display in nightly use.
    float meanGr = 0.0f, meanGb = 0.0f;

    // Gr-Gb as a percentage of their mean. Signed: positive means the
    // green beside red reads higher.
    float greenSplitPct = 0.0f;

    // Headroom to clipping, in stops, per channel. Computed from the
    // 99.9th-percentile highlight rather than the mean, for the same
    // reason auto-exposure uses it: on a planetary frame the mean is
    // sky and barely moves however the planet is exposed.
    //
    // Negative means that channel is already clipping, and then the
    // magnitude is the clipped FRACTION rather than stops -- a clipped
    // channel has lost the information needed to say how far past 255
    // it went, so there is no honest stop count to give.
    float headroomR = 0.0f, headroomG = 0.0f, headroomB = 0.0f;

    // Phase 92: the per-channel highlight the headroom above is derived
    // FROM, in DN, 0-255. Each is that channel's HIGHLIGHT_PCTILE
    // level: the brightness of the brightest real thing in it, which on
    // a planetary target is the planet.
    //
    // Kept because it is the number an imager actually sets exposure by
    // -- "get the histogram up to about three quarters" -- and it was
    // being computed and discarded. The histogram panel showed the
    // CLIPPED FRACTION in its place, which reads 0.0% on every
    // correctly exposed frame and so said nothing about exposure at
    // all, for either channel balance or level.
    //
    // Not the maximum: a single hot pixel would own that. A percentile
    // over the whole frame survives one, and on a planetary frame the
    // mean cannot be used at all, for the reason set out above.
    float highlightR = 0.0f, highlightG = 0.0f, highlightB = 0.0f;

    // The channel that will clip first, and its headroom. This is the
    // one that decides the exposure -- the others have room to spare.
    char  limitingChannel = 'R';
    float headroomStops   = 0.0f;

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
// ── The smart part: what the numbers mean (Phase 36) ────────
//
// Free functions rather than members, so tests can drive them with a
// hand-built AnalysisResult and no camera.
//
// The histogram shows what IS. These say what to DO about it, which is
// the whole difference between a readout and an assistant -- and they
// are deliberately terse, because this is read at a glance in the dark
// between adjustments.

// Exposure verdict from the headroom of the channel that clips first.
//
// Thresholds in stops rather than DN. A stop is the unit the adjustment
// is actually made in: "half a stop under" tells you to move the
// exposure slider by a factor of 1.4, where "highlight at 181 DN" does
// not.
inline std::string ExposureAdvice(const AnalysisResult& a) {
    if (!a.valid) return "no frame";
    // Negative headroom is a flag, not a measurement: a clipped
    // channel cannot say how far past 255 it went. The magnitude is the
    // clipped fraction, so report that instead of inventing stops.
    if (a.headroomStops < 0.0f) {
        char b[96];
        snprintf(b, sizeof(b), "CLIPPING in %c - %.1f%% of pixels at 255",
                 a.limitingChannel, -a.headroomStops * 100.0f);
        return b;
    }
    if (a.headroomStops < 0.25f) return "at the limit - any more clips";
    if (a.headroomStops < 1.0f) {
        char b[96];
        snprintf(b, sizeof(b), "good - %.1f stops left in %c",
                 a.headroomStops, a.limitingChannel);
        return b;
    }
    if (a.headroomStops < 3.0f) {
        char b[96];
        snprintf(b, sizeof(b), "under by %.1f stops", a.headroomStops);
        return b;
    }
    return "far under - the planet is near the noise floor";
}

// What a Gr/Gb split means.
//
// Every colour sensor has some, from crosstalk differing between the
// green beside red and the green beside blue, and a few percent is
// normal and uninteresting. A large one is a real signal: it is what a
// failing column, a bad flat, or debayer applied to the wrong Bayer
// phase looks like, and until now nothing on screen could show it at
// all, because the histogram averages the two greens into one row.
inline std::string GreenSplitAdvice(const AnalysisResult& a) {
    if (!a.valid) return "no frame";
    const float m = (a.greenSplitPct < 0.0f) ? -a.greenSplitPct : a.greenSplitPct;
    if (m < 3.0f)  return "normal";
    if (m < 10.0f) return "slight - usually just crosstalk";
    if (m < 25.0f) return "high - suspect the flat, or the Bayer phase";
    return "very high - wrong Bayer phase, or a sensor fault";
}

// Colour balance, read off the per-channel means.
//
// Stated as a ratio against green rather than as absolute numbers,
// because green is the reference the other two are white-balanced
// against and the absolute level is a function of exposure.
inline std::string BalanceAdvice(const AnalysisResult& a) {
    if (!a.valid || a.meanG < 1.0f) return "no frame";
    const float r = a.meanR / a.meanG, b = a.meanB / a.meanG;
    char buf[96];
    snprintf(buf, sizeof(buf), "R/G %.2f  B/G %.2f%s", r, b,
             (r > 1.6f || b > 1.6f || r < 0.4f || b < 0.4f)
                 ? "  - check white balance" : "");
    return buf;
}

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
