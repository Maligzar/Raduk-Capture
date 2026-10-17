#pragma once

#include <string>
#include <algorithm>
// ============================================================
// HistogramDraw.h — Phase 42b
//
// One implementation of the per-channel histogram, drawn into any D2D
// render target at any size.
//
// It exists because the histogram now has two homes: the small permanent
// panel on the live preview, and a window of its own on a spare monitor.
// Two copies of this would drift, and the parts most likely to drift are
// the parts that were hardest to get right -- the log scale, the skipped
// bins, the clip colouring. There is one copy and both callers use it.
//
// Everything scales from rowH, so the same code produces the 24px
// overlay rows and a row a third of a 1080p screen tall. At rowH = 24
// the derived widths come out at exactly the overlay's original
// numbers, so the small version is unchanged by the extraction.
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
#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include "../core/FrameAnalyzer.h"

namespace raduk::gui::histogram {

// The caller owns these; this header only borrows them for the call.
struct Brushes {
    ID2D1SolidColorBrush* r     = nullptr;
    ID2D1SolidColorBrush* g     = nullptr;
    ID2D1SolidColorBrush* b     = nullptr;
    ID2D1SolidColorBrush* bed   = nullptr;   // bar bed behind each row
    ID2D1SolidColorBrush* mean  = nullptr;   // mean marker
    ID2D1SolidColorBrush* dim   = nullptr;
    ID2D1SolidColorBrush* amber = nullptr;
    ID2D1SolidColorBrush* red   = nullptr;
    ID2D1SolidColorBrush* green = nullptr;
};

// labW / pctW are the channel-letter and clip-percentage columns.
// They are the CALLER'S to set, and deliberately not derived from rowH:
// they hold text, so they scale with the font, and the two part company
// badly once the rows get large. Derived from rowH, a window with 195px
// rows reserved 357px for the string "0.0%" and took all of it off the
// bars -- the display's whole content squeezed to make room for four
// characters.
inline void Draw(ID2D1RenderTarget* rt, IDWriteTextFormat* font,
                 const Brushes& br, const raduk::core::AnalysisResult& a,
                 bool valid, float x, float y, float w, float h,
                 float rowH, float labW, float pctW) {
    if (!rt || !font) return;

    const float PAD  = 6.0f;
    const float LABW = labW;
    const float PCTW = pctW;
    const float barX = x + PAD + LABW + 4.0f;
    const float barW = w - PAD * 2.0f - LABW - 4.0f - PCTW - 4.0f;
    if (barW < 20.0f) return;

    font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    struct Row {
        const wchar_t*                   label;
        ID2D1SolidColorBrush*            brush;
        const std::array<uint32_t, 256>* hist;
        float                            clip;
        float                            highlight;   // DN, 0-255
    } rows[3] = {
        {L"R", br.r, &a.histR, a.clipR, a.highlightR},
        {L"G", br.g, &a.histG, a.clipG, a.highlightG},
        {L"B", br.b, &a.histB, a.clipB, a.highlightB},
    };

    // The band to aim for, drawn on the bars as well as decided in the
    // numbers, so the panel says where the histogram should reach
    // without anyone having to be told.
    //
    // Converted from the SAME thresholds the footer's advice uses, not
    // chosen here. Picked independently they disagreed: the rows called
    // a frame good at 86% while the footer directly beneath called it
    // "at the limit", which is one fact contradicting itself on one
    // panel. 84.1 / 50.0 / 12.5 per cent, from 0.25 / 1 / 3 stops.
    using raduk::core::exposure::LevelPctForStops;
    const float AIM_HI  = LevelPctForStops(raduk::core::exposure::LIMIT_STOPS);
    const float AIM_LO  = LevelPctForStops(raduk::core::exposure::GOOD_STOPS);
    const float AIM_FAR = LevelPctForStops(raduk::core::exposure::UNDER_STOPS);
    const float AIM_TARGET = raduk::core::exposure::AIM_PCT;

    for (int ch = 0; ch < 3; ch++) {
        Row& row = rows[ch];
        const float ry = y + PAD + ch * rowH;
        if (!row.brush) continue;

        rt->DrawText(row.label, 1, font,
                     D2D1::RectF(x + PAD, ry, x + PAD + LABW, ry + rowH),
                     row.brush);

        // Bar bed, so an empty channel still reads as a channel.
        if (br.bed)
            rt->FillRectangle(
                D2D1::RectF(barX, ry + 2, barX + barW, ry + rowH - 2), br.bed);

        if (valid) {
            uint32_t peak = 1;
            for (int i = 1; i < 255; i++)
                if ((*row.hist)[i] > peak) peak = (*row.hist)[i];

            // LOG counts, not linear.
            //
            // A planetary frame is mostly sky, so the low bins hold
            // orders of magnitude more pixels than the bins the planet
            // occupies. Scaled linearly against the tallest bin,
            // everything except the sky spike works out under one pixel
            // and is dropped -- the rows read as empty while the data is
            // sitting right there.
            //
            // log(1+v)/log(1+peak) puts a single-count bin at about 6%
            // of row height against a peak of 100k, so the shape of the
            // distribution shows while the sky still reads as the
            // tallest thing. Read noise becomes visible too, which is
            // the honest picture rather than a flattering one.
            const float logPeak = logf(1.0f + (float)peak);

            // Bins 0 and 1 skipped: dark current, not signal, and on a
            // planetary frame it is most of the pixels.
            const float bedH = rowH - 4.0f;
            for (int i = 2; i < 256; i++) {
                const uint32_t v = (*row.hist)[i];
                if (!v) continue;
                const float bh = logf(1.0f + (float)v) / logPeak * bedH;
                // Skip, do not clamp, a bin under a pixel. Clamping drew
                // every non-zero bin as a 1px mark -- on a planetary
                // frame nearly all 254 of them -- which made a flat line
                // that looked like a histogram and carried nothing.
                if (bh < 1.0f) continue;
                const float bx  = barX + (float)i / 256.0f * barW;
                float       bx2 = barX + (float)(i + 1) / 256.0f * barW;
                if (bx2 <= bx) bx2 = bx + 1.0f;
                rt->FillRectangle(
                    D2D1::RectF(bx, ry + 2.0f + bedH - bh, bx2, ry + 2.0f + bedH),
                    (i >= 248 && br.red) ? br.red : row.brush);
            }

            // The marker sits at the HIGHLIGHT, not the mean.
            //
            // It used to mark the mean, which on a planetary frame is
            // sky: a small bright disc on black leaves the mean down
            // around 5 DN however the planet is exposed, so the marker
            // hugged the left edge at every exposure and moved for
            // reasons that were not the exposure. The analyzer makes
            // this same argument where it refuses to use the mean for
            // headroom.
            //
            // At the highlight it marks the thing the number beside it
            // reports, so the bar and the figure explain each other.
            if (row.highlight > 2.0f && br.mean) {
                float mx = barX + row.highlight / 255.0f * barW;
                if (mx < barX) mx = barX;
                if (mx > barX + barW - 1.0f) mx = barX + barW - 1.0f;
                rt->DrawLine(D2D1::Point2F(mx, ry + 3.0f),
                             D2D1::Point2F(mx, ry + rowH - 3.0f),
                             br.mean, 1.4f);
            }
        }

        // Where it should reach. A single faint tick, drawn whether or
        // not there is a frame -- it is a property of the display, not
        // of the data, and it is most useful on the pass where you are
        // still winding the exposure up.
        if (br.dim) {
            const float tx = barX + AIM_TARGET / 100.0f * barW;
            rt->DrawLine(D2D1::Point2F(tx, ry + 2.0f),
                         D2D1::Point2F(tx, ry + rowH - 2.0f),
                         br.dim, 0.7f);
        }

        // ── The number ────────────────────────────────────────
        //
        // PEAK LEVEL, as a percentage of full scale. This column used
        // to carry the clipped fraction, which is the proportion of
        // pixels sitting at 255 -- a good alarm and a useless readout,
        // because on every correctly exposed frame it reads 0.0%. Three
        // rows of 0.0% is what a working rig looked like, and nothing
        // on screen said what the figure even was.
        //
        // This is the number an imager actually sets exposure by: get
        // the histogram up to about three quarters, and stop before the
        // bright channel reaches the end. It comes from the analyzer's
        // per-channel highlight percentile, not the maximum -- one hot
        // pixel would own a maximum -- and not the mean, which on a
        // planetary frame is sky.
        //
        // Clipping has not been dropped, it has been folded in. A
        // clipped channel's highlight IS at 255, so it reads 100%, and
        // the colour says what that means. No separate figure is needed
        // to state twice what one number already says.
        const float lvl = valid ? row.highlight / 255.0f * 100.0f : 0.0f;
        const bool  blown = valid && (row.clip > 0.05f || lvl >= 99.0f);

        wchar_t pct[16];
        if (valid) swprintf(pct, 16, L"%.0f%%", lvl);
        else       swprintf(pct, 16, L"--");

        ID2D1SolidColorBrush* pctBrush =
            (!valid)         ? br.dim   :
            (blown)          ? br.red   :   // at the ceiling, detail lost
            (lvl >= AIM_HI)  ? br.amber :   // "at the limit"
            (lvl >= AIM_LO)  ? br.green :   // "good"
            (lvl >= AIM_FAR) ? br.amber :   // "under"
                               br.dim;      // "far under", or nothing there
        if (!pctBrush) pctBrush = br.dim;
        if (pctBrush) {
            font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            rt->DrawText(pct, (UINT32)wcslen(pct), font,
                         D2D1::RectF(barX + barW + 2.0f, ry, x + w - PAD, ry + rowH),
                         pctBrush);
            font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
    }

    // Seeing score. Not shown anywhere else -- the preview's bottom-left
    // graph reports sharpness, which is a different question.
    wchar_t line[48];
    if (valid) swprintf(line, 48, L"SEEING  %.1f/10", a.seeingScore);
    else       swprintf(line, 48, L"SEEING  --");
    ID2D1SolidColorBrush* sBrush =
        (!valid)                ? br.dim :
        (a.seeingScore >= 7.0f) ? br.green :
        (a.seeingScore >= 4.0f) ? br.amber : br.red;
    if (!sBrush) sBrush = br.dim;
    // Bounded to its own row rather than running to the bottom of the
    // panel. The font centres text vertically in whatever rect it is
    // given, so a rect reaching the bottom edge put this line in the
    // middle of the empty space below the bars -- and once the footer
    // below was added, straight on top of it.
    const D2D1_RECT_F seeingRect =
        D2D1::RectF(x + PAD, y + PAD + 3 * rowH, x + w - PAD,
                    (std::min)(y + h, y + PAD + 4 * rowH));
    if (sBrush)
        rt->DrawText(line, (UINT32)wcslen(line), font, seeingRect, sBrush);

    // ── What the column is ────────────────────────────────────
    //
    // Right-aligned on the seeing row, directly beneath the three
    // figures, so it reads as their heading. Underneath rather than
    // above because there is nothing above: the overlay copy of this
    // panel is 104 px tall and the three rows plus this one fill it
    // exactly. A label below a column still names the column, and an
    // unlabelled number is half of what was wrong here -- a correct
    // figure nobody can identify is no better than the clipped
    // fraction was.
    if (br.dim) {
        font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        rt->DrawText(L"PEAK", 4, font, seeingRect, br.dim);
        font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    // ── Sensor analysis (Phase 36) ──────────────────────────
    //
    // Only where there is room for it. The overlay version of this
    // panel is 104 px tall and already full; the stand-alone window has
    // a screen to itself. Drawn from the same painter so the two cannot
    // drift, gated on height so the overlay is untouched.
    const float footer = y + PAD + 4 * rowH;
    // The overlay copy of this panel has no room for any of it, and
    // says so by being short. 1.6 rather than 2 because the last line
    // only needs its text to fit, not a whole row below it.
    if (!valid || footer + rowH * 1.6f > y + h) return;

    // Exposure headroom, in stops, for the channel that clips first --
    // the one that decides the exposure. The others have room to spare.
    {
        std::string adv = raduk::core::ExposureAdvice(a);
        wchar_t w1[96];
        MultiByteToWideChar(CP_UTF8, 0, adv.c_str(), -1, w1, 96);
        ID2D1SolidColorBrush* eb =
            (a.headroomStops < 0.0f)  ? br.red   :
            (a.headroomStops < 0.25f) ? br.amber :
            (a.headroomStops < 3.0f)  ? br.green : br.amber;
        if (!eb) eb = br.dim;
        if (eb)
            rt->DrawText(w1, (UINT32)wcslen(w1), font,
                         D2D1::RectF(x + PAD, footer, x + w - PAD, footer + rowH),
                         eb);
    }

    // The Gr/Gb split. Invisible in the rows above by construction --
    // they show the MEAN of the two greens, so a frame with Gr=20 and
    // Gb=180 draws exactly like one with Gr=Gb=100. Stating it here is
    // the only place it can be seen at all.
    {
        std::string adv = raduk::core::GreenSplitAdvice(a);
        char utf[128];
        snprintf(utf, sizeof(utf), "Gr/Gb  %+.1f%%  %s",
                 a.greenSplitPct, adv.c_str());
        wchar_t w2[128];
        MultiByteToWideChar(CP_UTF8, 0, utf, -1, w2, 128);
        const float m = (a.greenSplitPct < 0.0f) ? -a.greenSplitPct
                                                 : a.greenSplitPct;
        ID2D1SolidColorBrush* gb = (m < 3.0f)  ? br.dim :
                                   (m < 10.0f) ? br.green :
                                   (m < 25.0f) ? br.amber : br.red;
        if (!gb) gb = br.dim;
        if (gb)
            rt->DrawText(w2, (UINT32)wcslen(w2), font,
                         D2D1::RectF(x + PAD, footer + rowH,
                                     x + w - PAD, footer + rowH * 2),
                         gb);
    }
}

} // namespace raduk::gui::histogram
