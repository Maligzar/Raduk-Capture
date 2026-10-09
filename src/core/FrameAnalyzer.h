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

    // ── Sharpness / seeing ─────────────────────────────────────
    float sharpnessPct = 0.0f;   // Laplacian variance 0-100%
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

    void Start() {
        if (m_running.load()) return;
        m_running.store(true);
        m_thread = std::thread(&FrameAnalyzer::Run, this);
        printf("[FrameAnalyzer] Started\n");
    }

    void Stop() {
        if (!m_running.load()) return;
        m_running.store(false);
        m_hasWork.store(true); // wake thread so it exits
        if (m_thread.joinable()) m_thread.join();
        printf("[FrameAnalyzer] Stopped\n");
    }

    // Non-blocking submit. If the analyzer is busy with the previous
    // frame, this call returns immediately (the frame is skipped).
    // That's fine — we always want the LATEST result, never queue up work.
    void Submit(const FrameSlot* slot) {
        if (!slot || !slot->data || slot->width == 0) return;
        if (m_hasWork.load(std::memory_order_relaxed)) return; // busy, skip

        // Copy frame into our local buffer
        size_t bytes = (size_t)slot->width * slot->height *
                       (slot->bitDepth == 16 ? 2 : 1);
        {
            std::lock_guard<std::mutex> lk(m_frameMutex);
            if (m_frameBuffer.size() < bytes) m_frameBuffer.resize(bytes);
            memcpy(m_frameBuffer.data(), slot->data, bytes);
            m_pendingW        = slot->width;
            m_pendingH        = slot->height;
            m_pendingBitDepth = slot->bitDepth;
        }
        m_hasWork.store(true, std::memory_order_release);
    }

    bool HasResult() const {
        return m_resultReady.load(std::memory_order_acquire);
    }

    // Returns the latest analysis result. Clears the "ready" flag.
    AnalysisResult GetResult() {
        m_resultReady.store(false, std::memory_order_release);
        std::lock_guard<std::mutex> lk(m_resultMutex);
        return m_result;
    }

private:
    void Run() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

        while (m_running.load()) {
            // Wait for work
            while (!m_hasWork.load(std::memory_order_acquire)) {
                if (!m_running.load()) return;
                Sleep(1);
            }
            if (!m_running.load()) return;
            m_hasWork.store(false, std::memory_order_release);

            // Grab pending frame
            std::vector<uint8_t> buf;
            uint32_t w, h; uint8_t bd;
            {
                std::lock_guard<std::mutex> lk(m_frameMutex);
                buf = m_frameBuffer;
                w   = m_pendingW;
                h   = m_pendingH;
                bd  = m_pendingBitDepth;
            }

            if (buf.empty() || w == 0 || h == 0) continue;

            AnalysisResult r;
            r.valid  = true;
            r.width  = w;
            r.height = h;
            r.histogram.fill(0);

            // ── Per-channel histogram from RGGB Bayer pattern ─────
            // ASI678MC Bayer layout (RGGB):
            //   Even row, even col = R
            //   Even row, odd  col = G1
            //   Odd  row, even col = G2
            //   Odd  row, odd  col = B
            // Step by 2 in both axes — every other superpixel for speed.
            r.histR.fill(0); r.histG.fill(0); r.histB.fill(0);
            r.histogram.fill(0);

            uint64_t sumR = 0, sumG = 0, sumB = 0, sumL = 0;
            uint32_t cntR = 0, cntG = 0, cntB = 0;

            // ASI RAW16 data is left-aligned in the 16-bit word (full 0-65535
            // range), so reducing to 8 bits needs >> 8. The previous >> 4 left
            // 0-4095, which the uint8_t cast then wrapped (4096 -> 0, ...),
            // garbling histograms and the centroid threshold in 16-bit mode.
            auto getP = [&](uint32_t py, uint32_t px) -> uint8_t {
                if (bd == 8)
                    return buf[py * w + px];
                else
                    return (uint8_t)(reinterpret_cast<const uint16_t*>(
                        buf.data())[py * w + px] >> 8);
            };

            // Walk in 4-pixel superpixel steps (every other superpixel for speed)
            for (uint32_t sy = 0; sy + 1 < h; sy += 4) {
                for (uint32_t sx = 0; sx + 1 < w; sx += 4) {
                    uint8_t rV  = getP(sy,   sx);      // R
                    uint8_t g1V = getP(sy,   sx + 1);  // G1
                    uint8_t g2V = getP(sy+1, sx);      // G2
                    uint8_t bV  = getP(sy+1, sx + 1);  // B
                    uint8_t gV  = (uint8_t)(((uint16_t)g1V + g2V) >> 1); // avg G

                    r.histR[rV]++; sumR += rV; cntR++;
                    r.histG[gV]++; sumG += gV; cntG++;
                    r.histB[bV]++; sumB += bV; cntB++;

                    // Luminance proxy: standard rec709 weights scaled to 8-bit
                    uint8_t lum = (uint8_t)((rV * 77u + gV * 150u + bV * 29u) >> 8);
                    r.histogram[lum]++;
                    sumL += lum;
                }
            }

            uint32_t countL = cntR; // same number of superpixels
            if (cntR > 0) {
                r.meanR = (float)sumR / cntR;
                r.meanG = (float)sumG / cntG;
                r.meanB = (float)sumB / cntB;
                r.meanBrightness = (float)sumL / countL;
                r.clipR = (float)r.histR[255] / cntR * 100.0f;
                r.clipG = (float)r.histG[255] / cntG * 100.0f;
                r.clipB = (float)r.histB[255] / cntB * 100.0f;
                r.clipPct = (r.clipR + r.clipG + r.clipB) / 3.0f;
            }

            // ── Sharpness: Laplacian variance on a 256x256 crop ──
            // Laplacian kernel: [ 0, -1, 0; -1, 4, -1; 0, -1, 0 ]
            // Applied to a downsampled green channel. Variance of the result
            // correlates strongly with focus quality and atmospheric seeing.
            {
                // Sample a 128x128 region (every other pixel = 256x256 native)
                // Phase 61: follow the planet rather than the geometric
                // centre. The centroid search already scans the FULL frame
                // on purpose, so the analyzer would locate an off-centre
                // planet and simultaneously report it had no sharpness --
                // 256 px is only ~52 arcsec at 0.203"/px, so a planet a
                // minute of arc off centre measured pure background. AUTO
                // FOCUS reads this same number, which is why it refused
                // with "no usable star/planet" while the planet sat plainly
                // on screen.
                //
                // Uses the PREVIOUS frame's centroid, since this frame's is
                // computed further down; at video rates one frame of lag
                // shifts the target a pixel or two inside a 256 px window.
                uint32_t cx = w / 2, cy = h / 2;
                if (m_lastCentroidValid) {
                    const uint32_t halfW = 128, halfH = 128;
                    // Clamp so the window stays wholly inside the frame: a
                    // planet near an edge must still measure its own light
                    // rather than a window half-filled with background.
                    if (w > 2 * halfW && h > 2 * halfH) {
                        uint32_t lx = (m_lastCentroidX < 0.0f) ? 0u : (uint32_t)m_lastCentroidX;
                        uint32_t ly = (m_lastCentroidY < 0.0f) ? 0u : (uint32_t)m_lastCentroidY;
                        cx = (lx < halfW) ? halfW : ((lx > w - halfW) ? w - halfW : lx);
                        cy = (ly < halfH) ? halfH : ((ly > h - halfH) ? h - halfH : ly);
                    }
                }
                uint32_t rw = 128, rh = 128;
                uint32_t x0 = (cx > rw) ? cx - rw : 0;
                uint32_t y0 = (cy > rh) ? cy - rh : 0;
                uint32_t x1 = (cx + rw < w - 1) ? cx + rw : w - 1;
                uint32_t y1 = (cy + rh < h - 1) ? cy + rh : h - 1;

                double lapSum = 0, lapSumSq = 0;
                uint32_t lapCount = 0;

                auto getPixel = [&](uint32_t px, uint32_t py) -> float {
                    if (bd == 8)
                        return (float)buf[py * w + px];
                    else // RAW16 is left-aligned: scale to 8-bit range
                        return (float)(reinterpret_cast<const uint16_t*>(buf.data())[py * w + px] >> 8);
                };

                for (uint32_t py = y0 + 1; py < y1 - 1; py += 2) {
                    for (uint32_t px = x0 + 1; px < x1 - 1; px += 2) {
                        float lap = 4.0f * getPixel(px, py)
                                  - getPixel(px-1, py) - getPixel(px+1, py)
                                  - getPixel(px, py-1) - getPixel(px, py+1);
                        lapSum   += lap;
                        lapSumSq += (double)lap * lap;
                        lapCount++;
                    }
                }

                if (lapCount > 1) {
                    double mean = lapSum / lapCount;
                    double variance = (lapSumSq / lapCount) - (mean * mean);
                    // Normalise: variance of ~5000 = perfect focus for typical planetary
                    // Scale to 0-100% with a reasonable ceiling
                    constexpr double SHARP_CEIL = 8000.0;
                    double pct = (variance / SHARP_CEIL * 100.0 < 100.0)
                        ? variance / SHARP_CEIL * 100.0 : 100.0;
                    r.sharpnessPct = (float)pct;
                }
            }

            // ── Rolling seeing score ────────────────────────────────
            // Maintain a ~10-second window of sharpness readings
            // at ~2Hz update rate = ~20 samples
            m_sharpHistory.push_back(r.sharpnessPct);
            if (m_sharpHistory.size() > SEEING_WINDOW) m_sharpHistory.pop_front(); // O(1)
            if (!m_sharpHistory.empty()) {
                double avg = 0;
                for (float v : m_sharpHistory) avg += v;
                avg /= m_sharpHistory.size();
                // Map 0-100% sharpness to 1-10 seeing scale
                r.seeingScore = 1.0f + (float)(avg / 100.0 * 9.0);
            }

            // ── Centroid: brightness-weighted centre of mass ────────
            // Search the FULL frame: Stage 1 (Searching) must find the
            // planet wherever the GoTo pointing error left it — a central
            // 50% window misses anything in the outer region. Threshold
            // isolates the bright planet disk from background and stars.
            {
                uint32_t x0 = 0, x1 = w;
                uint32_t y0 = 0, y1 = h;

                // First pass: find mean and max in search region
                uint64_t rsum = 0; uint32_t rcount = 0;
                uint8_t  rmax = 0;
                for (uint32_t py = y0; py < y1; py += 2) {
                    for (uint32_t px = x0; px < x1; px += 2) {
                        uint8_t v = (bd == 8)
                            ? buf[py * w + px]
                            : (uint8_t)(reinterpret_cast<const uint16_t*>
                                (buf.data())[py * w + px] >> 8);
                        rsum += v; rcount++;
                        if (v > rmax) rmax = v;
                    }
                }

                r.centroidPeak = rmax;
                // The gate exists so a dark frame does not produce a
                // centroid out of noise. It used to fail silently, so a
                // planet dimmer than this threshold looked identical to
                // no planet at all -- and the acquisition chain above
                // simply never became ready, with nothing said.
                if (rcount > 0 && rmax <= AnalysisResult::CENTROID_MIN_PEAK) {
                    r.centroidTooFaint = true;
                }
                if (rcount > 0 && rmax > AnalysisResult::CENTROID_MIN_PEAK) {
                    float rmean = (float)rsum / rcount;
                    // Threshold: 70% of the way from mean to max
                    // This isolates the bright planet disk
                    float threshold = rmean + 0.7f * (rmax - rmean);

                    double wx = 0, wy = 0, wt = 0;
                    for (uint32_t py = y0; py < y1; py += 2) {
                        for (uint32_t px = x0; px < x1; px += 2) {
                            float v = (bd == 8)
                                ? (float)buf[py * w + px]
                                : (float)(reinterpret_cast<const uint16_t*>
                                    (buf.data())[py * w + px] >> 8);
                            if (v >= threshold) {
                                wx += (double)px * v;
                                wy += (double)py * v;
                                wt += v;
                            }
                        }
                    }

                    if (wt > 0) {
                        r.centroidX     = (float)(wx / wt);
                        r.centroidY     = (float)(wy / wt);
                        r.centroidValid = true;
                        m_lastCentroidX     = r.centroidX;
                        m_lastCentroidY     = r.centroidY;
                        m_lastCentroidValid = true;
                    }
                }
            }

            // Phase 61: hold the last centroid briefly when the planet
            // drops out, rather than snapping the sharpness crop back to
            // frame centre on the first dim frame. This matters most during
            // an AUTO FOCUS sweep: at the ends of the sweep the planet is
            // spread thin and can fall under the centroid threshold, and
            // measuring background instead of its (genuinely soft) light
            // would put a false floor in the V-curve. A short hold keeps
            // the crop over the planet; a sustained loss falls back.
            if (r.centroidValid) {
                m_centroidMissFrames = 0;
            } else if (m_lastCentroidValid) {
                if (++m_centroidMissFrames > CENTROID_HOLD_FRAMES)
                    m_lastCentroidValid = false;
            }

            // Copy rolling seeing history into result for graph drawing
            {
                r.seeingHistoryCount = (int)m_sharpHistory.size();
                int copyStart = 0;
                if (r.seeingHistoryCount > AnalysisResult::SEEING_HISTORY) {
                    copyStart = r.seeingHistoryCount - AnalysisResult::SEEING_HISTORY;
                    r.seeingHistoryCount = AnalysisResult::SEEING_HISTORY;
                }
                for (int si = 0; si < r.seeingHistoryCount; si++)
                    r.seeingHistory[si] = m_sharpHistory[copyStart + si];
            }

            // Publish result
            {
                std::lock_guard<std::mutex> lk(m_resultMutex);
                m_result = r;
            }
            m_resultReady.store(true, std::memory_order_release);
        }
    }

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
