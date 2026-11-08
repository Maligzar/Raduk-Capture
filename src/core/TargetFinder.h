#pragma once
// ============================================================
// TargetFinder.h — where the bright object is, on a real sensor
//
// Field finding, 2026-11-07, Saturn on the ASI678MC at gain 353,
// 2.25 ms, 8-bit: the centroid never sat on the planet, in focus or
// out of it, and acquisition stalled in SEARCHING. The search took
// the single brightest PIXEL in the frame and kept everything above
// 70% of the way from the mean to it. At that gain the brightest
// pixel is a hot pixel or a noise spike, not the planet, so the
// threshold sat above the whole disc and the "centroid" was the
// centre of a few scattered spikes. The emulator renders no hot
// pixels, which is why it always locked.
//
// So the question is asked of light gathered over AREA, which is
// what a planet has and a hot pixel does not:
//
//   1. Sum the frame into CELL x CELL blocks (all four Bayer colours,
//      so the answer does not depend on the target's colour).
//   2. Box-smooth the cells 3x3. A hot pixel is diluted 576 times; a
//      disc tens of pixels across is not diluted at all.
//   3. Background and noise from the median and MAD of the smoothed
//      cells: robust to the target, to hot pixels and to a few stars.
//   4. The brightest smoothed cell, if it stands clear of the noise,
//      is the target. Its connected bright cells are its extent, so a
//      moon or a star elsewhere does not pull the centre.
//   5. The centre is the background-subtracted, brightness-weighted
//      mean of the PIXELS in that extent, for sub-pixel precision.
//
// A defocused, obstructed donut is a ring of cells connected round
// its dark middle, and the weighted centre of a ring is its centre.
//
// Pure: no Windows, no camera, so tests/target_finder.cpp drives it.
// ============================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace raduk::core {

struct TargetFind {
    bool  found   = false;
    float x       = 0.0f;   // frame pixels
    float y       = 0.0f;
    float snr     = 0.0f;   // peak smoothed cell above background, in noise units
    int   cells   = 0;      // cells in the target's extent
    float bgDN    = 0.0f;   // background, mean DN per pixel
    float noiseDN = 0.0f;   // per-pixel noise estimate, DN
    float peakDN  = 0.0f;   // brightest cell's mean DN, minus background
};

struct TargetFinderParams {
    int   cell      = 8;      // pixels per cell side; even keeps Bayer phase
    float minSnr    = 10.0f;  // smoothed-cell peak must clear this
    float extentFrac = 0.20f; // extent: cells above bg + this of (peak - bg)
    float extentSnr = 4.0f;   // ...and above bg + this many noise
};

// RAW16 is left-aligned, so its high byte is on the same scale as RAW8,
// as everywhere else in the analyzer.
inline TargetFind FindBrightTarget(const uint8_t* data, int width, int height,
                                   int bitDepth,
                                   const TargetFinderParams& p = {}) {
    TargetFind out;
    if (!data || width <= 0 || height <= 0) return out;
    int C = (std::max)(2, p.cell & ~1);
    // A small frame still needs a few cells each way.
    while (C > 2 && (width / C < 6 || height / C < 6)) C -= 2;
    const int nx = width / C, ny = height / C;
    if (nx < 3 || ny < 3) return out;

    const int bytes = (bitDepth == 16) ? 2 : 1;
    const int high  = bytes - 1;
    const size_t rowBytes = (size_t)width * bytes;
    const auto px = [&](int x, int y) -> int {
        return data[(size_t)y * rowBytes + (size_t)x * bytes + high];
    };

    // 1. Cell sums.
    std::vector<uint32_t> cell((size_t)nx * ny, 0u);
    for (int y = 0; y < ny * C; ++y) {
        const uint8_t* row = data + (size_t)y * rowBytes + high;
        uint32_t* crow = cell.data() + (size_t)(y / C) * nx;
        for (int cx = 0; cx < nx; ++cx) {
            const uint8_t* q = row + (size_t)cx * C * bytes;
            uint32_t s = 0;
            for (int k = 0; k < C; ++k) s += q[(size_t)k * bytes];
            crow[cx] += s;
        }
    }

    // 2. 3x3 box smoothing; edges rescaled to a full 9-cell sum.
    std::vector<float> sm((size_t)nx * ny, 0.0f);
    for (int cy = 0; cy < ny; ++cy) {
        for (int cx = 0; cx < nx; ++cx) {
            uint32_t s = 0; int n = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                const int yy = cy + dy;
                if (yy < 0 || yy >= ny) continue;
                for (int dx = -1; dx <= 1; ++dx) {
                    const int xx = cx + dx;
                    if (xx < 0 || xx >= nx) continue;
                    s += cell[(size_t)yy * nx + xx]; ++n;
                }
            }
            sm[(size_t)cy * nx + cx] = (float)s * 9.0f / (float)n;
        }
    }

    // 3. Background and noise, robustly, and LOCALLY: twilight, moonlight
    // and amp glow put a gradient across the frame several times the
    // noise, and against one global level the far corner outshines a
    // dim planet. A coarse grid of blocks ~40 cells square takes the
    // 25th percentile of each (low enough that a large defocused donut
    // covering much of a block does not lift it), interpolated
    // bilinearly between block centres. The constant offset that a low
    // percentile leaves is removed by the global median of the residual.
    const int gx = (std::max)(1, (int)std::lround(nx / 40.0));
    const int gy = (std::max)(1, (int)std::lround(ny / 40.0));
    std::vector<float> blk((size_t)gx * gy, 0.0f);
    {
        std::vector<float> vals;
        for (int by = 0; by < gy; ++by)
            for (int bx = 0; bx < gx; ++bx) {
                const int x0 = bx * nx / gx, x1 = (bx + 1) * nx / gx;
                const int y0 = by * ny / gy, y1 = (by + 1) * ny / gy;
                vals.clear();
                for (int cy = y0; cy < y1; ++cy)
                    for (int cx = x0; cx < x1; ++cx)
                        vals.push_back(sm[(size_t)cy * nx + cx]);
                const size_t q = vals.size() / 4;
                std::nth_element(vals.begin(), vals.begin() + q, vals.end());
                blk[(size_t)by * gx + bx] = vals[q];
            }
    }
    std::vector<float> bgMap((size_t)nx * ny, 0.0f);
    for (int cy = 0; cy < ny; ++cy) {
        // Block-centre coordinates, clamped at the outer half-blocks.
        float fy = ((cy + 0.5f) * gy / (float)ny) - 0.5f;
        fy = (std::min)((std::max)(fy, 0.0f), (float)(gy - 1));
        const int by0 = (int)fy, by1 = (std::min)(by0 + 1, gy - 1);
        const float ty = fy - by0;
        for (int cx = 0; cx < nx; ++cx) {
            float fx = ((cx + 0.5f) * gx / (float)nx) - 0.5f;
            fx = (std::min)((std::max)(fx, 0.0f), (float)(gx - 1));
            const int bx0 = (int)fx, bx1 = (std::min)(bx0 + 1, gx - 1);
            const float tx = fx - bx0;
            const float a = blk[(size_t)by0 * gx + bx0] * (1 - tx) + blk[(size_t)by0 * gx + bx1] * tx;
            const float b = blk[(size_t)by1 * gx + bx0] * (1 - tx) + blk[(size_t)by1 * gx + bx1] * tx;
            bgMap[(size_t)cy * nx + cx] = a * (1 - ty) + b * ty;
        }
    }
    std::vector<float> res((size_t)nx * ny);
    for (size_t i = 0; i < res.size(); ++i) res[i] = sm[i] - bgMap[i];
    std::vector<float> tmp(res);
    const size_t mid = tmp.size() / 2;
    std::nth_element(tmp.begin(), tmp.begin() + mid, tmp.end());
    const float off = tmp[mid];
    for (size_t i = 0; i < res.size(); ++i) { res[i] -= off; bgMap[i] += off; }
    for (size_t i = 0; i < tmp.size(); ++i) tmp[i] = std::fabs(res[i]);
    std::nth_element(tmp.begin(), tmp.begin() + mid, tmp.end());
    const float pixPerSm = 9.0f * (float)C * (float)C;
    // Floor: 8-bit quantisation alone is ~0.29 DN per pixel, so a
    // perfectly flat synthetic frame does not divide by zero and a
    // target a couple of DN above it still counts.
    const float noiseFloor = 0.3f * std::sqrt(pixPerSm);
    const float noise = (std::max)(1.4826f * tmp[mid], noiseFloor);

    out.noiseDN = noise / std::sqrt(pixPerSm);   // white-noise scaling

    // 4. The peak, and its connected extent.
    size_t peakI = 0;
    for (size_t i = 1; i < res.size(); ++i) if (res[i] > res[peakI]) peakI = i;
    const float peak = res[peakI];
    out.bgDN = bgMap[peakI] / pixPerSm;
    out.snr  = peak / noise;
    if (out.snr < p.minSnr) return out;

    const float thr = (std::max)(p.extentFrac * peak, p.extentSnr * noise);
    std::vector<uint8_t> in((size_t)nx * ny, 0);
    std::vector<int> stack;
    stack.push_back((int)peakI);
    in[peakI] = 1;
    float peakCell = 0.0f;   // brightest cell's mean DN above background
    while (!stack.empty()) {
        const int i = stack.back(); stack.pop_back();
        ++out.cells;
        peakCell = (std::max)(peakCell, (float)cell[(size_t)i] / (float)(C * C)
                                        - bgMap[(size_t)i] / pixPerSm);
        const int cx = i % nx, cy = i / nx;
        for (int dy = -1; dy <= 1; ++dy) {
            const int yy = cy + dy;
            if (yy < 0 || yy >= ny) continue;
            for (int dx = -1; dx <= 1; ++dx) {
                const int xx = cx + dx;
                if (xx < 0 || xx >= nx) continue;
                const size_t j = (size_t)yy * nx + xx;
                if (in[j] || res[j] < thr) continue;
                in[j] = 1;
                stack.push_back((int)j);
            }
        }
    }
    out.peakDN = peakCell;

    // 5. Pixel-level weighted centre over the extent, dilated one cell
    // so the limb is not clipped.
    //
    // Weight = pixel minus local background minus 3 sigma, floored at 0.
    // Weighting every masked pixel unthresholded is unbiased but lets the
    // noise of hundreds of background pixels around a small star move
    // the answer by a pixel; above 3 sigma almost no noise survives, and
    // what is cut from a symmetric target is cut symmetrically.
    //
    // Each pixel is also clipped at a ceiling well above the brightest
    // cell's mean, so a hot pixel sitting on the disc counts as a bright
    // disc pixel rather than as 255. A star's core is clipped too, but
    // symmetrically, which leaves its centre where it was.
    std::vector<uint8_t> mask(in);
    for (int cy = 0; cy < ny; ++cy)
        for (int cx = 0; cx < nx; ++cx) {
            if (!in[(size_t)cy * nx + cx]) continue;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int xx = cx + dx, yy = cy + dy;
                    if (xx >= 0 && xx < nx && yy >= 0 && yy < ny)
                        mask[(size_t)yy * nx + xx] = 1;
                }
        }
    // Except for an extended target, whose surface can be fainter than
    // the noise per pixel (a far-defocused donut is): a 3-sigma cut would
    // keep only noise excursions. There the target's flux over thousands
    // of pixels dominates, so every masked pixel is weighted, negative
    // ones included, which keeps the noise zero-mean and unbiased.
    const bool extended = out.cells > 30;   // a star smooths to ~3x3 to 5x5 cells
    const double cut  = extended ? 0.0 : 3.0 * out.noiseDN;
    const double ceil = 2.0 * out.peakDN + 6.0 * out.noiseDN;
    double sw = 0.0, sx = 0.0, sy = 0.0;
    for (int cy = 0; cy < ny; ++cy)
        for (int cx = 0; cx < nx; ++cx) {
            if (!mask[(size_t)cy * nx + cx]) continue;
            const double bgPix = bgMap[(size_t)cy * nx + cx] / pixPerSm;
            for (int y = cy * C; y < cy * C + C; ++y)
                for (int x = cx * C; x < cx * C + C; ++x) {
                    double w = (double)px(x, y) - bgPix;
                    if (w > ceil) w = ceil;
                    w -= cut;
                    if (!extended && w <= 0.0) continue;
                    sw += w; sx += w * x; sy += w * y;
                }
        }
    if (sw <= 0.0) return out;
    // The 2x2 Bayer cell's centre is half a pixel in from its first
    // pixel, and every cell holds whole Bayer cells, so the weighting
    // is symmetric up to colour: no correction is applied.
    out.x = (float)(sx / sw);
    out.y = (float)(sy / sw);
    if (out.x < 0.0f || out.y < 0.0f ||
        out.x >= (float)width || out.y >= (float)height) return out;
    out.found = true;
    return out;
}

} // namespace raduk::core
