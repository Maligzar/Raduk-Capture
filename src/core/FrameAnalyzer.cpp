// ============================================================
// FrameAnalyzer.cpp — Asynchronous frame analysis
// Out-of-line definitions for FrameAnalyzer.h.
// ============================================================

#include "FrameAnalyzer.h"

namespace raduk::core {

void FrameAnalyzer::Start() {
    if (m_running.load()) return;
    m_running.store(true);
    m_thread = std::thread(&FrameAnalyzer::Run, this);
    printf("[FrameAnalyzer] Started\n");
}

void FrameAnalyzer::Stop() {
    if (!m_running.load()) return;
    m_running.store(false);
    m_hasWork.store(true); // wake thread so it exits
    if (m_thread.joinable()) m_thread.join();
    printf("[FrameAnalyzer] Stopped\n");
}

void FrameAnalyzer::Submit(const FrameSlot* slot) {
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

AnalysisResult FrameAnalyzer::GetResult() {
    m_resultReady.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lk(m_resultMutex);
    return m_result;
}

void FrameAnalyzer::Run() {
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
        uint64_t sumGr = 0, sumGb = 0;   // the two greens, kept apart
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

                // The two greens kept apart as well as averaged, so a
                // Gr/Gb split can be stated. The averaged histG above
                // is what the display still shows; this only adds a
                // number beside it.
                sumGr += g1V; sumGb += g2V;

                // Luminance proxy: standard rec709 weights scaled to 8-bit
                uint8_t lum = (uint8_t)((rV * 77u + gV * 150u + bV * 29u) >> 8);
                r.histogram[lum]++;
                sumL += lum;
            }
        }

        uint32_t countL = cntR; // same number of superpixels
        if (cntR > 0) {
            r.meanGr = (float)sumGr / cntR;
            r.meanGb = (float)sumGb / cntR;
            const float gAvg = (r.meanGr + r.meanGb) * 0.5f;
            // Guarded: on a frame with no green signal at all the split
            // is undefined, not infinite.
            r.greenSplitPct = (gAvg > 0.5f)
                            ? (r.meanGr - r.meanGb) / gAvg * 100.0f : 0.0f;
        }
        if (cntR > 0) {
            r.meanR = (float)sumR / cntR;
            r.meanG = (float)sumG / cntG;
            r.meanB = (float)sumB / cntB;
            r.meanBrightness = (float)sumL / countL;
            r.clipR = (float)r.histR[255] / cntR * 100.0f;
            r.clipG = (float)r.histG[255] / cntG * 100.0f;
            r.clipB = (float)r.histB[255] / cntB * 100.0f;
            r.clipPct = (r.clipR + r.clipG + r.clipB) / 3.0f;

            // Highlight percentile, walked down from white. Counting
            // down rather than up means the answer is reached after a
            // handful of bins on a dark frame instead of 250.
            const uint32_t want = (uint32_t)((1.0f - AnalysisResult::HIGHLIGHT_PCTILE)
                                             * (float)countL);
            uint32_t seen = 0;
            r.highlightDN = 0.0f;
            for (int i = 255; i >= 0; i--) {
                seen += r.histogram[i];
                if (seen > want) { r.highlightDN = (float)i; break; }
            }

            // ── Headroom per channel (Phase 36) ───────────────
            //
            // How much more exposure each channel could take before it
            // clips, in stops, from its OWN 99.9th-percentile highlight
            // rather than its mean. The mean is the wrong statistic for
            // the same reason auto-exposure stopped using it: on a
            // planetary frame the mean is sky and barely moves however
            // the planet is exposed.
            //
            // Already clipping reads negative, by how far past 255 the
            // highlight would have had to sit.
            //
            // Phase 92: the percentile itself is now kept, not just the
            // stop count derived from it. It is the brightness of the
            // brightest real thing in that channel -- the planet -- and
            // it is the number an imager actually sets exposure by
            // ("get the histogram to about three quarters"). It was
            // being computed here and thrown away, while the display
            // showed the clipped fraction instead: a figure that reads
            // 0.0% on every correctly exposed frame.
            auto HighlightOf = [&](const std::array<uint32_t, 256>& h,
                                   uint32_t count) -> int {
                if (count == 0) return 0;
                const uint32_t w2 = (uint32_t)((1.0f - AnalysisResult::HIGHLIGHT_PCTILE)
                                               * (float)count);
                uint32_t acc = 0;
                for (int i = 255; i >= 0; i--) {
                    acc += h[i];
                    if (acc > w2) return i;
                }
                return 0;
            };

            auto HeadroomOf = [&](int hi, float clipPct) -> float {
                if (hi <= 0) return 8.0f;          // nothing there; all the room
                // A clipped channel has lost the information needed to
                // say how far past 255 it went, so there is no honest
                // stop count to give. The sign carries the meaning
                // instead: negative means clipping, and the magnitude
                // is the clipped FRACTION, not stops. Returning a
                // log-of-clip-percent here read as "lose 6.7 stops" on
                // a saturated frame, which is a number with no physical
                // meaning at all.
                if (clipPct > 0.1f) return -clipPct / 100.0f;
                return (float)(std::log(255.0 / (double)hi) / std::log(2.0));
            };

            r.highlightR = (float)HighlightOf(r.histR, cntR);
            r.highlightG = (float)HighlightOf(r.histG, cntG);
            r.highlightB = (float)HighlightOf(r.histB, cntB);

            r.headroomR = HeadroomOf((int)r.highlightR, r.clipR);
            r.headroomG = HeadroomOf((int)r.highlightG, r.clipG);
            r.headroomB = HeadroomOf((int)r.highlightB, r.clipB);

            // The channel that clips first is the one that decides the
            // exposure; the others have room to spare. On an RGGB
            // sensor under a warm-ish light that is usually red, but
            // which it is depends on the target and the filter, so it
            // is measured rather than assumed.
            r.limitingChannel = 'R';
            r.headroomStops   = r.headroomR;
            if (r.headroomG < r.headroomStops) {
                r.headroomStops = r.headroomG; r.limitingChannel = 'G';
            }
            if (r.headroomB < r.headroomStops) {
                r.headroomStops = r.headroomB; r.limitingChannel = 'B';
            }
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

            // Phase 76: one shared definition, in Sharpness.h, used by
            // this gauge AND by the SER writer's per-frame sidecar.
            // They had drifted apart once already - this kernel was
            // measuring a red-minus-green colour difference rather
            // than detail - and a second copy is how that happens.
            double dbgRaw = 0.0, dbgNoise = 0.0;
            const double variance = BayerLaplacianVariance(
                buf.data(), w, h, bd, cx, cy, 128, &dbgRaw, &dbgNoise);
            r.sharpRaw   = (float)dbgRaw;
            r.sharpNoise = (float)dbgNoise;
            if (variance > 0.0)
                r.sharpnessPct = SharpnessPercent(variance);
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
            // Seeing score, 1-10, RELATIVE to the best frame seen
            // recently rather than to an absolute sharpness.
            //
            // It used to be 1 + pct/100*9, which tied it to
            // SHARP_CEIL. When that ceiling was recalibrated the
            // score silently collapsed into a 2.5-2.9 band - it
            // can only reach 10 if sharpness reaches 100%, which
            // a real in-focus frame does not - and the capture
            // GATE compares against this number. A gate set to 6
            // would have rejected every frame.
            //
            // Relative is also the more honest definition. What
            // the gate wants to know is 'is this frame good
            // compared with what this target is currently giving
            // me', which is scale-free: immune to the next
            // recalibration, to the target's brightness, and to
            // how noisy the camera is. The cost is that a
            // uniformly poor night still reaches 10 at its best
            // moments - correct for gating, since you cannot
            // record frames better than the sky is offering, but
            // it means the score is NOT a measure of how good the
            // night is in absolute terms. Read sharpnessPct for
            // that.
            double best = 0.0;
            for (float v : m_sharpHistory) if (v > best) best = v;
            if (best > 1e-3) {
                double rel = r.sharpnessPct / best;
                if (rel > 1.0) rel = 1.0;
                r.seeingScore = 1.0f + (float)(rel * 9.0);
            } else {
                r.seeingScore = 1.0f;
            }
            (void)avg;
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

} // namespace raduk::core
