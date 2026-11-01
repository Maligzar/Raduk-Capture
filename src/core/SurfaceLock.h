#pragma once
// ============================================================
// SurfaceLock.h — Phase 109: following a surface instead of a disc
//
// A centroid needs a disc inside the frame. On the Moon or the Sun at
// this focal length the crop is full, the centroid has nothing to hold
// and it sits in the middle of the frame whatever the image does. So
// this holds a patch of the surface itself: remember one, find it again
// in each frame by correlation, and report where it has gone.
//
// The answer has the same meaning as a centroid, so RoiFollower needs
// no change: the position the target has reached, starting from the
// frame centre. After the window moves, the patch appears shifted the
// other way, which is what the follower's landing check looks for.
//
// The frame is averaged into cells about 160 across first. That mixes
// the Bayer colours into one brightness, and averages down the noise.
// Correlation is zero-mean and normalised, so seeing and a changing
// exposure do not read as movement.
//
// A MOVE MUST BE LOOKED FOR IN BOTH PLACES. Until it lands, a frame can
// come from either window, and a move is usually wider than the search,
// so ExpectMove adds the second place to look.
//
// THE REFERENCE AGES. An alt-az field rotates and features change under
// the seeing, so it is retaken when the match weakens or after
// kRefreshFrames, never during a move. Each retake keeps the reported
// position continuous, and adds at most a small part of a cell of error.
//
// Pure, and capture thread only, behind RoiFollowLink's lock.
// ============================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace raduk::core {

// Is the target too big for a centroid? Bright pixels along the frame's
// edge are the sign: a disc inside the frame has none, and one drifting
// out touches a short stretch. Same sampling as FastCentroid.
inline bool FrameIsFilled(const uint8_t* data, int width, int height,
                          int bitDepth, int stride) {
    if (!data || width <= 0 || height <= 0 || stride < 1) return false;
    const int bytes = (bitDepth == 16) ? 2 : 1;
    const int high  = bytes - 1;
    const size_t rowBytes = (size_t)width * bytes;
    const auto at = [&](int x, int y) -> int {
        return data[(size_t)y * rowBytes + (size_t)x * bytes + high];
    };

    int peak = 0;
    for (int y = 0; y < height; y += stride)
        for (int x = 0; x < width; x += stride)
            peak = (std::max)(peak, at(x, y));
    constexpr int kMinPeak = 32;
    if (peak < kMinPeak) return false;

    const int floorV = peak / 2;
    int edge = 0, bright = 0;
    const auto sample = [&](int x, int y) {
        ++edge;
        if (at(x, y) > floorV) ++bright;
    };
    for (int x = 0; x < width; x += stride) {
        sample(x, 0);
        sample(x, height - 1);
    }
    for (int y = 0; y < height; y += stride) {
        sample(0, y);
        sample(width - 1, y);
    }
    return bright * 4 >= edge;     // a quarter of the edge
}

class SurfaceLock {
public:
    static constexpr int   kTargetCells   = 160;   // across the frame
    static constexpr int   kPatch         = 48;    // cells square
    static constexpr int   kSearch        = 6;     // cells each way, per frame
    static constexpr int   kRetryEvery    = 8;     // lost frames per wide search
    static constexpr int   kRefreshFrames = 1000;
    static constexpr float kMinScore      = 0.60f; // below this it is lost
    static constexpr float kRefreshScore  = 0.85f;
    static constexpr float kMinDetail     = 1.0f;  // mean gradient, of 255

    void Reset() { *this = SurfaceLock{}; }

    // Where the remembered patch has got to, in frame pixels, starting
    // from the frame centre. False holds the window: no detail to lock
    // on to yet, or the patch has been lost.
    bool Measure(const uint8_t* data, int width, int height, int bitDepth,
                 float& outX, float& outY) {
        if (!data || width <= 0 || height <= 0) return false;
        Decimate(data, width, height, bitDepth);
        if (m_dw < kSearch * 2 + 4 || m_dh < kSearch * 2 + 4) return false;
        m_patch = (std::min)({ kPatch, m_dw - 2, m_dh - 2 });

        if (!m_haveRef) {
            int px = 0, py = 0;
            if (!PickPatch(px, py)) return false;
            TakeReference(px, py);
            m_baseX = m_baseY = 0.0f;
            m_fx = (float)px; m_fy = (float)py;
        } else if (!Find()) {
            return false;
        }

        outX = width  * 0.5f + (m_fx - m_refX + m_baseX) * m_cell;
        outY = height * 0.5f + (m_fy - m_refY + m_baseY) * m_cell;
        Maintain();
        return true;
    }

    // The window is about to move by this many pixels, so the patch may
    // appear shifted the other way from any frame on.
    void ExpectMove(int dxPx, int dyPx) {
        if (m_cell <= 0) return;
        m_expect  = true;
        m_expectX = -(float)dxPx / m_cell;
        m_expectY = -(float)dyPx / m_cell;
    }
    void ClearExpect() { m_expect = false; }

    bool  HasReference() const { return m_haveRef; }
    float Score()        const { return m_score; }
    int   Refreshes()    const { return m_refreshes; }
    int   LostFrames()   const { return m_lostTotal; }
    int   Cell()         const { return m_cell; }

private:
    void Decimate(const uint8_t* data, int width, int height, int bitDepth) {
        // Even, so a cell always holds whole Bayer quads.
        const int c = (std::max)(4, (width / kTargetCells + 1) & ~1);
        const int dw = width / c, dh = height / c;
        if (c != m_cell || dw != m_dw || dh != m_dh) {
            m_haveRef = false;
            m_cell = c; m_dw = dw; m_dh = dh;
            m_cur.assign((size_t)dw * dh, 0.0f);
        }
        const int bytes = (bitDepth == 16) ? 2 : 1;
        const int high  = bytes - 1;
        const size_t rowBytes = (size_t)width * bytes;
        const float scale = 1.0f / (float)(c * c);
        m_sums.resize((size_t)dw);   // kept: this runs on the capture thread
        auto& sums = m_sums;
        for (int dy = 0; dy < dh; ++dy) {
            std::fill(sums.begin(), sums.end(), 0u);
            for (int y = 0; y < c; ++y) {
                const uint8_t* p = data + (size_t)(dy * c + y) * rowBytes + high;
                for (int dx = 0; dx < dw; ++dx) {
                    uint32_t s = 0;
                    for (int x = 0; x < c; ++x, p += bytes) s += *p;
                    sums[(size_t)dx] += s;
                }
            }
            float* out = m_cur.data() + (size_t)dy * dw;
            for (int dx = 0; dx < dw; ++dx) out[dx] = sums[(size_t)dx] * scale;
        }
    }

    float Cur(int x, int y) const { return m_cur[(size_t)y * m_dw + x]; }

    // The most detailed spot near the middle, measured along BOTH axes,
    // because a limb or a terminator is a strong edge that says nothing
    // about movement along itself.
    bool PickPatch(int& outX, int& outY) const {
        const int maxX = m_dw - m_patch, maxY = m_dh - m_patch;
        const int stepX = (std::max)(1, maxX / 6);
        const int stepY = (std::max)(1, maxY / 6);
        float best = -1.0f;
        for (int py = 0; py <= maxY; py += stepY) {
            for (int px = 0; px <= maxX; px += stepX) {
                float gx = 0.0f, gy = 0.0f;
                for (int y = 0; y + 1 < m_patch; ++y)
                    for (int x = 0; x + 1 < m_patch; ++x) {
                        const float v = Cur(px + x, py + y);
                        gx += std::fabs(Cur(px + x + 1, py + y) - v);
                        gy += std::fabs(Cur(px + x, py + y + 1) - v);
                    }
                const float n = (float)((m_patch - 1) * (m_patch - 1));
                const float detail = (std::min)(gx, gy) / n;
                // Prefer the middle, so the patch has room on every side.
                const float dx = (px - maxX * 0.5f) / (maxX + 1.0f);
                const float dy = (py - maxY * 0.5f) / (maxY + 1.0f);
                const float score = detail * (1.0f - dx * dx - dy * dy);
                if (detail >= kMinDetail && score > best) {
                    best = score; outX = px; outY = py;
                }
            }
        }
        return best >= 0.0f;
    }

    void TakeReference(int px, int py) {
        const int n = m_patch;
        m_ref.resize((size_t)n * n);
        float mean = 0.0f;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const float v = Cur(px + x, py + y);
                m_ref[(size_t)y * n + x] = v;
                mean += v;
            }
        mean /= (float)(n * n);
        for (auto& v : m_ref) v -= mean;
        m_refX = px; m_refY = py;
        m_haveRef = true;
        m_score = 1.0f;
        m_sinceRef = 0;
    }

    // Zero-mean normalised correlation, reading every `step`th cell.
    float Correlate(int px, int py, int step) const {
        const int n = m_patch;
        float sum = 0.0f, sumSq = 0.0f, cross = 0.0f, refSq = 0.0f;
        int count = 0;
        for (int y = 0; y < n; y += step) {
            const float* c = m_cur.data() + (size_t)(py + y) * m_dw + px;
            const float* r = m_ref.data() + (size_t)y * n;
            for (int x = 0; x < n; x += step) {
                sum += c[x]; sumSq += c[x] * c[x];
                cross += c[x] * r[x]; refSq += r[x] * r[x];
                ++count;
            }
        }
        const float var = sumSq - sum * sum / (float)count;
        if (var <= 1e-3f || refSq <= 1e-3f) return -1.0f;
        return cross / std::sqrt(var * refSq);
    }

    void SearchAround(int cx, int cy, int radius, int step, int patchStep,
                      float& best, int& bestX, int& bestY) const {
        const int maxX = m_dw - m_patch, maxY = m_dh - m_patch;
        for (int y = (std::max)(0, cy - radius);
             y <= (std::min)(maxY, cy + radius); y += step)
            for (int x = (std::max)(0, cx - radius);
                 x <= (std::min)(maxX, cx + radius); x += step) {
                const float s = Correlate(x, y, patchStep);
                if (s > best) { best = s; bestX = x; bestY = y; }
            }
    }

    static float Vertex(float a, float b, float c) {
        const float d = a - 2.0f * b + c;
        if (d >= -1e-6f) return 0.0f;
        return (std::clamp)(0.5f * (a - c) / d, -0.5f, 0.5f);
    }

    bool Find() {
        const int lastX = (int)std::lround(m_fx);
        const int lastY = (int)std::lround(m_fy);
        float best = -2.0f;
        int bx = lastX, by = lastY;
        SearchAround(lastX, lastY, kSearch, 1, 2, best, bx, by);
        if (m_expect)
            SearchAround((int)std::lround(m_fx + m_expectX),
                         (int)std::lround(m_fy + m_expectY),
                         kSearch, 1, 2, best, bx, by);
        if (best < kMinScore && m_lost > 0 && m_lost % kRetryEvery == 0)
            SearchAround(m_dw / 2, m_dh / 2, (std::max)(m_dw, m_dh), 2, 3,
                         best, bx, by);

        // Again on every cell, around whatever that found.
        best = -2.0f;
        int fx = bx, fy = by;
        SearchAround(bx, by, 1, 1, 1, best, fx, fy);
        if (best < kMinScore) {
            m_score = (std::max)(best, 0.0f);
            ++m_lost; ++m_lostTotal;
            return false;
        }
        m_lost = 0;
        m_score = best;

        float sx = 0.0f, sy = 0.0f;
        if (fx > 0 && fx < m_dw - m_patch)
            sx = Vertex(Correlate(fx - 1, fy, 1), best, Correlate(fx + 1, fy, 1));
        if (fy > 0 && fy < m_dh - m_patch)
            sy = Vertex(Correlate(fx, fy - 1, 1), best, Correlate(fx, fy + 1, 1));
        m_fx = fx + sx;
        m_fy = fy + sy;
        return true;
    }

    // Retake the reference, or move it, keeping the reported position.
    void Rebase(int px, int py) {
        m_baseX += m_fx - m_refX;
        m_baseY += m_fy - m_refY;
        TakeReference(px, py);
        // This frame's patch is exactly the reference, wherever the
        // feature sat within its cell.
        m_fx = (float)px; m_fy = (float)py;
        ++m_refreshes;
    }

    void Maintain() {
        ++m_sinceRef;
        if (m_expect || m_lost > 0) return;
        const int margin = kSearch + 1;
        const bool nearEdge = m_fx < margin || m_fy < margin ||
                              m_fx > m_dw - m_patch - margin ||
                              m_fy > m_dh - m_patch - margin;
        if (nearEdge) {
            int px = 0, py = 0;
            if (PickPatch(px, py)) { Rebase(px, py); return; }
        }
        if (m_score < kRefreshScore || m_sinceRef >= kRefreshFrames)
            Rebase((int)std::lround(m_fx), (int)std::lround(m_fy));
    }

    int   m_cell = 0, m_dw = 0, m_dh = 0, m_patch = 0;
    std::vector<float> m_cur, m_ref;
    std::vector<uint32_t> m_sums;
    bool  m_haveRef = false, m_expect = false;
    int   m_refX = 0, m_refY = 0;
    float m_baseX = 0.0f, m_baseY = 0.0f;   // cells, from earlier references
    float m_fx = 0.0f, m_fy = 0.0f;         // cells, where the patch is now
    float m_expectX = 0.0f, m_expectY = 0.0f;
    float m_score = 0.0f;
    int   m_sinceRef = 0, m_lost = 0, m_lostTotal = 0, m_refreshes = 0;
};

} // namespace raduk::core
