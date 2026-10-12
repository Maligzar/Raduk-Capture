#pragma once
// ============================================================
// QuickLookStacker.h — Phase 74
//
// Decides which preview frames are worth stacking, and by how much to
// shift each one so the planet lands in the same place. Holds NO pixel
// buffers: PreviewRenderer owns those, so the dimension-change teardown
// in SetFrameSize stays the single place that has to get reallocation
// right (that was a heap overflow once already — Phase 56 review G1).
//
// Why this exists. DisplayFX::Stack3/5/10 averaged every recent frame at
// fixed pixel indices. Seeing jitter is 2-5", and at 0.203"/px that is
// 10-25 px of smear, so the result was SOFTER than a single frame while
// the HUD said "STACK 10f". Lucky imaging exists to reject the mean;
// that code computed it.
//
// Two things make this honest:
//   - Only frames in the top ~15% of the recent distribution are
//     admitted, so the stack is the good frames, not all of them.
//   - Each admitted frame is shifted so the planet registers, so
//     detail adds instead of smearing.
//
// Display only. Nothing here touches what is written to the SER.
// ============================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace raduk::core {

struct QuickLookVerdict {
    bool        admit  = false;
    int         dx     = 0;      // shift to apply, BUFFER pixels, always even
    int         dy     = 0;
    float       score  = 0.0f;   // this frame's normalised score
    const char* reason = "";     // why it was not admitted
};

class QuickLookStacker {
public:
    // Scored-frame history feeding the percentile gate. ~1.5-4s at the
    // 30-100 fps the preview actually sees.
    static constexpr int   HISTORY      = 128;
    // Admit every frame until the distribution means something. The ring
    // fills in well under a second, and those frames are already ALIGNED,
    // so even the cold-start display beats the old code.
    static constexpr int   COLD_START   = 32;
    static constexpr float PERCENTILE   = 0.85f;   // keep the top ~15%
    // Further than seeing can account for: a jog, a slew, a bumped tripod
    // or a failed centroid. 64 buffer px is ~13" at the real plate scale.
    static constexpr int   MAX_SHIFT    = 64;
    // Consecutive agreeing outliers needed before the reference moves,
    // and how close they must be to count as agreeing (superpixels).
    // Three at ~40 FPS is under a tenth of a second, so a genuine slew
    // is still picked up immediately in human terms, while a lone bad
    // centroid cannot move the reference at all.
    static constexpr int   JUMP_CONFIRM = 3;
    static constexpr int   JUMP_AGREE   = 8;
    // Below this the frame is noise and a centroid from it is a random
    // walk. Matches AnalysisResult::CENTROID_MIN_PEAK.
    static constexpr int   MIN_PEAK     = 20;
    // Reference tracks mount drift (~0.3"/s) but NOT seeing jitter, which
    // decorrelates in 0.5-1s. At 0.02 the time constant is ~50 admitted
    // frames (~6s), five times longer than the stack's own window, so the
    // reference cannot chase the frames being aligned to it.
    static constexpr float REF_ALPHA    = 0.02f;

    void Reset() {
        m_hist.clear();
        m_haveRef = false;
        m_refX = m_refY = 0.0f;
        m_admitted = 0;
        m_lastScore = m_threshold = 0.0f;
    }

    // ── The metric ────────────────────────────────────────────
    // S = mean|grad G| / mean G over a window centred on (cx,cy).
    //
    // Dimensionless and exactly invariant to G -> k*G: numerator and
    // denominator both scale by k. That matters because a Laplacian
    // VARIANCE scales as k^2, so it reads a brightness change as a
    // sharpness change — one capture peaking at 29k ADU against 54k for
    // the others would look 3.4x softer when nothing about focus moved.
    //
    // Differences are taken at +/-1 SUPERPIXEL (2 buffer px). DebayerRGGB
    // writes each 2x2 block as four identical values, so a 1-px
    // difference is identically zero and would measure the block lattice
    // rather than the image.
    //
    // Sampled on green: it is the Bayer-averaged channel and has the best
    // SNR of the three.
    static float ScoreWindow(const uint8_t* rgb, int w, int h,
                             int cx, int cy) {
        if (!rgb || w < 8 || h < 8) return 0.0f;
        const int sw = w / 2, sh = h / 2;          // superpixel extents
        const int R  = 128;                         // half-width, superpixels
        int ci = cx / 2, cj = cy / 2;
        int i0 = ci - R, i1 = ci + R;
        int j0 = cj - R, j1 = cj + R;
        if (i0 < 1) i0 = 1;  if (i1 > sw - 2) i1 = sw - 2;
        if (j0 < 1) j0 = 1;  if (j1 > sh - 2) j1 = sh - 2;
        if (i1 - i0 < 4 || j1 - j0 < 4) return 0.0f;

        auto G = [rgb, w](int i, int j) -> int {
            return rgb[((size_t)(2 * j) * w + (size_t)(2 * i)) * 3 + 1];
        };

        // Pass 1: exact histogram of the green byte, for robust levels.
        uint32_t hist[256] = {};
        double   sumG = 0.0;
        uint32_t cnt  = 0;
        for (int j = j0; j <= j1; j++)
            for (int i = i0; i <= i1; i++) {
                const int g = G(i, j);
                hist[g]++; sumG += g; cnt++;
            }
        if (cnt == 0) return 0.0f;
        const double meanG = sumG / cnt;
        if (meanG < 1.0) return 0.0f;

        auto pct = [&](double frac) -> int {
            uint32_t want = (uint32_t)(frac * cnt), acc = 0;
            for (int b = 0; b < 256; b++) {
                acc += hist[b];
                if (acc >= want) return b;
            }
            return 255;
        };
        const double bg = pct(0.10);    // sky level, robust if the disk is large
        const double pk = pct(0.995);   // disk level, robust against a hot pixel
        const double bgCut = bg + 0.10 * (pk - bg);

        // Pass 2: squared gradient energy over the window, and separately
        // over background-only samples, which carry noise and no detail.
        double eAll = 0.0, eBg = 0.0;
        uint32_t nAll = 0, nBg = 0;
        for (int j = j0; j <= j1; j++)
            for (int i = i0; i <= i1; i++) {
                const double gx = G(i + 1, j) - G(i - 1, j);
                const double gy = G(i, j + 1) - G(i, j - 1);
                const double e  = gx * gx + gy * gy;
                eAll += e; nAll++;
                if (G(i, j) < bgCut) { eBg += e; nBg++; }
            }
        if (nAll == 0) return 0.0f;

        // Subtracting the noise floor is what makes a stack comparable with
        // a single frame. Noise contributes to gradient energy, and
        // averaging N aligned frames cuts noise by sqrt(N) — so an
        // uncorrected gradient score goes DOWN on a stack that is genuinely
        // better, and the display would say stacking made things worse.
        const double eMean = eAll / nAll;
        const double eNoise = (nBg > 32) ? (eBg / nBg) : 0.0;
        const double eSig  = (eMean > eNoise) ? (eMean - eNoise) : 0.0;

        // sqrt puts the result back in amplitude units. Divide by the disk's
        // amplitude ABOVE SKY, not by the absolute mean: amplitude is
        // invariant to an additive offset as well as a multiplicative one,
        // so G -> k*G + b leaves the score unchanged. Dividing by meanG
        // instead made the score fall as a sensor bias pedestal rose, since
        // the pedestal lifts the mean but contributes no gradient.
        const double amp = (pk - bg > 8.0) ? (pk - bg) : 8.0;
        return (float)(std::sqrt(eSig) / amp);
    }

    // Offer a freshly debayered frame. Call ONCE per genuinely new camera
    // frame — never on a held redraw, or the same frame is stacked twice.
    QuickLookVerdict Offer(const uint8_t* rgb, int w, int h) {
        QuickLookVerdict v;
        if (!rgb || w < 16 || h < 16) { v.reason = "no frame"; return v; }

        float ccx = 0.0f, ccy = 0.0f;
        if (!Centroid(rgb, w, h, ccx, ccy)) {
            v.reason = "planet too faint to register";
            return v;
        }

        v.score = ScoreWindow(rgb, w, h, (int)ccx, (int)ccy);
        m_lastScore = v.score;

        // Score history drives the gate. Every scored frame goes in, not
        // only admitted ones — a gate fed its own output ratchets upward
        // and quietly stops admitting anything.
        if ((int)m_hist.size() >= HISTORY) m_hist.erase(m_hist.begin());
        m_hist.push_back(v.score);

        if (!m_haveRef) { m_refX = ccx; m_refY = ccy; m_haveRef = true; }

        // Snap the shift to even pixels: an odd shift only moves the
        // debayer replication phase and adds no information.
        int dx = (int)lroundf(m_refX - ccx) & ~1;
        int dy = (int)lroundf(m_refY - ccy) & ~1;

        if (std::abs(dx) > MAX_SHIFT || std::abs(dy) > MAX_SHIFT) {
            // The planet is further from the reference than seeing
            // explains. Re-anchoring is right when the planet really has
            // moved -- a slew, a nudge, a recentre -- but it used to
            // happen on the FIRST such frame, which meant one bad
            // centroid was enough to move the reference onto a wrong
            // position. Every good frame afterwards then measured as
            // jumped relative to that, so the next one re-anchored too,
            // and the stack could oscillate without admitting anything.
            //
            // A real move keeps the planet in its new place, so the test
            // is whether the jump PERSISTS. Consecutive outliers that
            // agree with each other re-anchor; a single wild centroid is
            // discarded and the reference is left where it was.
            const bool agrees =
                m_jumpCount > 0 &&
                std::abs((int)lroundf(ccx - m_jumpX)) <= JUMP_AGREE &&
                std::abs((int)lroundf(ccy - m_jumpY)) <= JUMP_AGREE;
            if (agrees) m_jumpCount++;
            else      { m_jumpCount = 1; m_jumpX = ccx; m_jumpY = ccy; }

            if (m_jumpCount >= JUMP_CONFIRM) {
                m_refX = ccx; m_refY = ccy;
                m_jumpCount = 0;
                v.reason = "planet moved - re-anchored";
            } else {
                v.reason = "planet jumped - ignoring one outlier";
            }
            return v;
        }
        // Back inside the window, so whatever the outlier was, it was not
        // a move. Forget it, or an old jump would combine with a later
        // unrelated one and re-anchor on neither.
        m_jumpCount = 0;

        const int n = (int)m_hist.size();
        if (n < COLD_START) {
            m_threshold = 0.0f;                 // still learning
        } else {
            std::vector<float> tmp(m_hist);
            size_t k = (size_t)(PERCENTILE * (float)(n - 1));
            std::nth_element(tmp.begin(), tmp.begin() + k, tmp.end());
            m_threshold = tmp[k];
        }

        if (n >= COLD_START && v.score < m_threshold) {
            v.reason = "below the recent quality threshold";
            return v;
        }

        v.admit = true;
        v.dx = dx;
        v.dy = dy;
        m_refX += REF_ALPHA * (ccx - m_refX);
        m_refY += REF_ALPHA * (ccy - m_refY);
        m_admitted++;
        return v;
    }

    float Threshold()  const { return m_threshold; }
    float LastScore()  const { return m_lastScore; }
    int   Admitted()   const { return m_admitted; }
    bool  Learning()   const { return (int)m_hist.size() < COLD_START; }
    // Where the stack expects the planet, in buffer px — the point to
    // score the composed stack at so its number is comparable.
    int   RefX() const { return (int)m_refX; }
    int   RefY() const { return (int)m_refY; }

private:
    // Brightness-weighted centroid on green, same shape as the one
    // FrameAnalyzer already uses: mean/max pass, threshold at
    // mean + 0.7*(max-mean), then a weighted sum of what survives.
    bool Centroid(const uint8_t* rgb, int w, int h,
                  float& outX, float& outY) const {
        const int sw = w / 2, sh = h / 2;
        auto G = [rgb, w](int i, int j) -> int {
            return rgb[((size_t)(2 * j) * w + (size_t)(2 * i)) * 3 + 1];
        };

        // Search near the previous reference once we have one, so a moon
        // drifting into frame cannot pull the centroid off the planet.
        int i0 = 0, i1 = sw - 1, j0 = 0, j1 = sh - 1;
        if (m_haveRef) {
            const int R = 192;
            i0 = (int)m_refX / 2 - R;  i1 = (int)m_refX / 2 + R;
            j0 = (int)m_refY / 2 - R;  j1 = (int)m_refY / 2 + R;
            if (i0 < 0) i0 = 0;  if (i1 > sw - 1) i1 = sw - 1;
            if (j0 < 0) j0 = 0;  if (j1 > sh - 1) j1 = sh - 1;
        }
        if (i1 - i0 < 4 || j1 - j0 < 4) return false;

        long  sum = 0;
        int   peak = 0, count = 0;
        for (int j = j0; j <= j1; j++)
            for (int i = i0; i <= i1; i++) {
                const int g = G(i, j);
                sum += g; count++;
                if (g > peak) peak = g;
            }
        if (count == 0 || peak < MIN_PEAK) return false;

        const float mean = (float)sum / (float)count;
        const float T    = mean + 0.7f * ((float)peak - mean);

        double wsum = 0.0, wx = 0.0, wy = 0.0;
        for (int j = j0; j <= j1; j++)
            for (int i = i0; i <= i1; i++) {
                const float wgt = (float)G(i, j) - T;
                if (wgt <= 0.0f) continue;
                wsum += wgt;
                wx   += (double)wgt * i;
                wy   += (double)wgt * j;
            }
        if (wsum < 1.0) return false;

        outX = (float)(wx / wsum) * 2.0f;   // superpixels -> buffer px
        outY = (float)(wy / wsum) * 2.0f;
        return true;
    }

    std::vector<float> m_hist;
    bool  m_haveRef   = false;
    int   m_jumpCount = 0;
    float m_jumpX = 0.0f, m_jumpY = 0.0f;
    float m_refX      = 0.0f, m_refY = 0.0f;
    float m_lastScore = 0.0f;
    float m_threshold = 0.0f;
    int   m_admitted  = 0;
};

} // namespace raduk::core
