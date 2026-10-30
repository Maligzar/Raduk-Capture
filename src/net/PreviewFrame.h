// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once
// ============================================================
// PreviewFrame.h -- B3
//
// The dashboard's preview canvas sat black from the first public
// release until 2026-10-25: nothing drew into it and the app sent no
// image at all. This is the pixel half of the fix: a raw frame in, a
// small 8-bit BGR picture out, cheap enough to run on the render loop
// once or twice a second. PreviewStreamer does the rest -- JPEG and
// sending -- on a thread of its own.
//
// Two decisions worth knowing:
//
//   NEAREST SUPERPIXEL, NOT A BOX AVERAGE. Each output pixel reads one
//   2x2 Bayer cell, so a 3840x2160 frame costs about 230 thousand cell
//   reads for a 640-wide preview instead of the eight million a box
//   filter would touch. It aliases noise; it does not lose a planet,
//   which spans many cells. This runs on the render loop, so the
//   cheap one is the right one.
//
//   AUTO-LEVELLED. A planet on a dark sky in raw 8 bits is a dim smudge
//   at whatever exposure the histogram wants. The 99.9th percentile of
//   the preview is stretched to white, as the eye wants for framing. It
//   is a monitoring picture, not data: raw frames never cross the
//   network, and nothing here changes what is recorded.
//
// Pure arithmetic, no Windows: tests/preview_frame.cpp gates it.
// ============================================================

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace raduk::net {

struct PreviewImage {
    int width  = 0;
    int height = 0;
    std::vector<uint8_t> bgr;   // width * height * 3, top row first
    int levelFrom = 0;          // the 8-bit value stretched to 255
};

// Defaults chosen for a dashboard tile: 640 wide is a few tens of KB
// as JPEG, and the canvas it lands in is about that size.
constexpr int kPreviewMaxWidth = 640;

// `data` is one raw frame: 8 bits a pixel, or 16 left-aligned bits (the
// SDK's RAW16), little-endian. `bayer` true means RGGB, which is what
// the ASI678MC and the emulator deliver; false treats it as mono.
// Returns false, and leaves `out` empty, for a frame too small to
// make a picture of or with a bit depth it does not know.
inline bool DecimateFrame(const uint8_t* data, int w, int h, int bitDepth,
                          bool bayer, PreviewImage& out,
                          int maxWidth = kPreviewMaxWidth,
                          bool autoLevel = true)
{
    out = PreviewImage{};
    if (!data || w < 2 || h < 2 || maxWidth < 1) return false;
    if (bitDepth != 8 && bitDepth != 16) return false;

    // The grid we sample from: 2x2 cells for Bayer, pixels for mono.
    const int cell  = bayer ? 2 : 1;
    const int gridW = w / cell;
    const int gridH = h / cell;
    if (gridW < 1 || gridH < 1) return false;
    const int step = (gridW + maxWidth - 1) / maxWidth;   // >= 1
    const int ow = gridW / step;
    const int oh = gridH / step;
    if (ow < 1 || oh < 1) return false;

    const uint16_t* d16 = reinterpret_cast<const uint16_t*>(data);
    auto px = [&](int x, int y) -> int {
        const size_t i = (size_t)y * (size_t)w + (size_t)x;
        // RAW16 is left-aligned: the top byte is the 8-bit value.
        return bitDepth == 16 ? (d16[i] >> 8) : data[i];
    };

    out.width = ow;
    out.height = oh;
    out.bgr.resize((size_t)ow * oh * 3);
    int hist[256] = {};
    for (int oy = 0; oy < oh; ++oy) {
        const int y = oy * step * cell;
        for (int ox = 0; ox < ow; ++ox) {
            const int x = ox * step * cell;
            int r, g, b;
            if (bayer) {
                r = px(x, y);
                g = (px(x + 1, y) + px(x, y + 1) + 1) >> 1;
                b = px(x + 1, y + 1);
            } else {
                r = g = b = px(x, y);
            }
            uint8_t* o = &out.bgr[((size_t)oy * ow + ox) * 3];
            o[0] = (uint8_t)b; o[1] = (uint8_t)g; o[2] = (uint8_t)r;
            ++hist[std::max(r, std::max(g, b))];
        }
    }

    // 99.9th percentile of the brightest channel. A floor of 16 keeps a
    // frame of pure noise from being stretched into a grey snowstorm
    // that reads as a target.
    const size_t n = (size_t)ow * oh;
    const size_t keep = n - n / 1000;
    size_t acc = 0;
    int p = 255;
    for (int v = 0; v < 256; ++v) {
        acc += (size_t)hist[v];
        if (acc >= keep) { p = v; break; }
    }
    out.levelFrom = std::max(p, 16);
    if (autoLevel && out.levelFrom < 255) {
        uint8_t lut[256];
        for (int v = 0; v < 256; ++v)
            lut[v] = (uint8_t)std::min(255, (v * 255 + out.levelFrom / 2) / out.levelFrom);
        for (auto& c : out.bgr) c = lut[c];
    } else {
        out.levelFrom = 255;
    }
    return true;
}

} // namespace raduk::net
