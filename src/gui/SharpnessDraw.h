// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once
// ============================================================
// SharpnessDraw.h — Phase 42c
//
// One implementation of the rolling sharpness graph, drawn into any D2D
// render target at any size. Shared by the small overlay in the corner
// of the preview and the stand-alone sharpness window, for the same
// reason the histogram is shared: two copies drift, and what drifts is
// the thresholds, which are the part that matters.
//
// The colour bands (green >= 60, amber >= 30, red below) are the same
// judgement the autofocus routine and the capture quality gate make, so
// they must read identically wherever they are drawn.
//
// It takes the samples as a plain span rather than owning them, because
// the two callers keep different amounts of history on purpose: the
// overlay shows the last few seconds, the window shows minutes.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <cmath>
#include <cwchar>

namespace raduk::gui::sharpness {

struct Brushes {
    ID2D1SolidColorBrush* green = nullptr;
    ID2D1SolidColorBrush* amber = nullptr;
    ID2D1SolidColorBrush* red   = nullptr;
    ID2D1SolidColorBrush* dim   = nullptr;
    ID2D1SolidColorBrush* bg    = nullptr;   // optional backing
};

// The thresholds. Named once, here, because three different places act
// on them: this graph, the autofocus peak search and the capture gate.
inline ID2D1SolidColorBrush* BandBrush(float pct, const Brushes& br) {
    if (pct >= 60.0f) return br.green;
    if (pct >= 30.0f) return br.amber;
    return br.red;
}

// samples: oldest first. count may be 0.
//
// barW/gap set how much history fits: the graph shows the newest
// floor(plotW / (barW + gap)) samples and drops the rest off the left,
// so a wide window with thin bars shows a much longer run than the
// overlay without either of them needing to know that.
inline void Draw(ID2D1RenderTarget* rt, IDWriteTextFormat* font,
                 const Brushes& br,
                 const float* samples, int count,
                 float x, float y, float w, float h,
                 float labelW, float barW, float gap,
                 bool drawGridlines) {
    if (!rt) return;

    const float plotW = w - labelW;
    if (plotW < 10.0f || h < 10.0f) return;

    // ── Gridlines at the two thresholds ───────────────────────
    // Only worth drawing when there is room; on the overlay they would
    // be noise across a 48px strip. They turn the graph from a shape
    // into something you can read a number off.
    if (drawGridlines && br.dim) {
        for (float lvl : { 30.0f, 60.0f }) {
            const float ly = y + h - (lvl / 100.0f) * h;
            rt->DrawLine(D2D1::Point2F(x, ly), D2D1::Point2F(x + plotW, ly),
                         br.dim, 0.5f);
        }
    }

    const int maxBars = (int)(plotW / (barW + gap));
    const int start   = (count > maxBars) ? count - maxBars : 0;
    float peak = 0.0f, sum = 0.0f;
    int   n    = 0;

    for (int i = start; i < count; i++) {
        const float val = samples[i];
        if (val > peak) peak = val;
        sum += val; n++;

        const float barH = (val / 100.0f) * h;
        const float bx   = x + (float)(i - start) * (barW + gap);
        const float by   = y + h - barH;
        if (ID2D1SolidColorBrush* b = BandBrush(val, br))
            rt->FillRectangle(D2D1::RectF(bx, by, bx + barW, y + h), b);
    }

    if (br.dim)
        rt->DrawLine(D2D1::Point2F(x, y + h),
                     D2D1::Point2F(x + plotW, y + h), br.dim, 0.5f);

    // ── Labels ────────────────────────────────────────────────
    if (!font || labelW < 20.0f) return;
    const float lx   = x + plotW + 4.0f;
    const float mean = (n > 0) ? sum / n : 0.0f;
    const float lh   = font->GetFontSize() * 1.3f;

    ID2D1SolidColorBrush* labelBrush = BandBrush(peak, br);
    if (!labelBrush) labelBrush = br.dim;
    if (!labelBrush) return;

    font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    wchar_t buf[24];

    rt->DrawText(L"SHARP", 5, font,
                 D2D1::RectF(lx, y, x + w, y + lh),
                 br.dim ? br.dim : labelBrush);

    swprintf(buf, 24, L"PK %.0f%%", peak);
    rt->DrawText(buf, (UINT32)wcslen(buf), font,
                 D2D1::RectF(lx, y + lh, x + w, y + lh * 2.0f), labelBrush);

    swprintf(buf, 24, L"AV %.0f%%", mean);
    rt->DrawText(buf, (UINT32)wcslen(buf), font,
                 D2D1::RectF(lx, y + lh * 2.0f, x + w, y + lh * 3.0f),
                 br.dim ? br.dim : labelBrush);
}

} // namespace raduk::gui::sharpness
