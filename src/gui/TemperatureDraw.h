// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once
// ============================================================
// TemperatureDraw.h — Phase 42d
//
// One implementation of the camera/focuser temperature trace, drawn
// into any D2D render target at any size. Shared by the small panel on
// the preview and the stand-alone temperature window, the same way the
// histogram and sharpness graphs are.
//
// The part worth not duplicating here is the vertical scaling. The two
// sensors sit within a degree or two of each other all night, so an
// auto-range with no floor magnifies sensor quantisation into a chart
// that looks like the temperature is swinging wildly. The 2°C minimum
// span is what makes a steady night read as a flat line, and it has to
// behave identically wherever the trace is drawn or the two displays
// would disagree about whether anything is happening.
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
#include <cwchar>

namespace raduk::gui::temperature {

struct Sample { float camC; float focC; };

struct Brushes {
    ID2D1SolidColorBrush* cam  = nullptr;   // camera trace
    ID2D1SolidColorBrush* foc  = nullptr;   // focuser trace
    ID2D1SolidColorBrush* dim  = nullptr;
};

// samples: oldest first. historySpan is the number of slots the x axis
// represents, so a part-filled history draws from the left and grows
// rightwards rather than stretching to fill.
inline void Draw(ID2D1RenderTarget* rt, IDWriteTextFormat* font,
                 const Brushes& br,
                 const Sample* samples, int count, int historySpan,
                 float camNow, float focNow,
                 float x, float y, float w, float h,
                 float labelH, float traceWidth) {
    if (!rt) return;

    const float chartTop = y;
    const float chartH   = h - labelH;
    if (chartH < 12.0f || w < 20.0f) return;

    if (count < 2) {
        if (font && br.dim)
            rt->DrawText(L"TEMP  collecting...", 19, font,
                         D2D1::RectF(x, y, x + w, y + h), br.dim);
        return;
    }

    float tMin = 999.0f, tMax = -999.0f;
    for (int i = 0; i < count; i++) {
        const Sample& s = samples[i];
        if (s.camC < tMin) tMin = s.camC;
        if (s.focC < tMin) tMin = s.focC;
        if (s.camC > tMax) tMax = s.camC;
        if (s.focC > tMax) tMax = s.focC;
    }
    float tRange = tMax - tMin;
    // Floor the span. Without it, a still night is magnified into
    // apparent chaos -- the sensors quantise to a tenth of a degree and
    // an auto-range would stretch that across the whole chart.
    if (tRange < 2.0f) { tMin -= 1.0f; tRange = 2.0f; }

    auto toY = [&](float t) -> float {
        return chartTop + chartH - 1.0f - ((t - tMin) / tRange) * (chartH - 2.0f);
    };
    auto toX = [&](int i) -> float {
        const int span = (historySpan > 1) ? historySpan - 1 : 1;
        return x + (float)i / (float)span * w;
    };

    struct Trace { ID2D1SolidColorBrush* brush; bool isFoc; } traces[2] = {
        { br.cam, false },
        { br.foc, true  },
    };
    for (const Trace& tr : traces) {
        if (!tr.brush) continue;
        for (int i = 1; i < count; i++) {
            const float t0 = tr.isFoc ? samples[i-1].focC : samples[i-1].camC;
            const float t1 = tr.isFoc ? samples[i].focC   : samples[i].camC;
            rt->DrawLine(D2D1::Point2F(toX(i - 1), toY(t0)),
                         D2D1::Point2F(toX(i),     toY(t1)), tr.brush, traceWidth);
        }
    }

    if (!font || labelH < 8.0f) return;
    const float labY = y + h - labelH;
    const float fs   = font->GetFontSize();
    wchar_t buf[64];

    font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    if (br.dim) {
        swprintf(buf, 64, L"%.0f°-%.0f°", tMin, tMin + tRange);
        rt->DrawText(buf, (UINT32)wcslen(buf), font,
                     D2D1::RectF(x, labY, x + fs * 7.0f, labY + labelH), br.dim);
    }
    if (br.cam) {
        swprintf(buf, 64, L"CAM %.1f°", camNow);
        rt->DrawText(buf, (UINT32)wcslen(buf), font,
                     D2D1::RectF(x + w - fs * 13.0f, labY,
                                 x + w - fs * 6.5f, labY + labelH), br.cam);
    }
    if (br.foc) {
        swprintf(buf, 64, L"FOC %.1f°", focNow);
        rt->DrawText(buf, (UINT32)wcslen(buf), font,
                     D2D1::RectF(x + w - fs * 6.3f, labY, x + w, labY + labelH),
                     br.foc);
    }
}

} // namespace raduk::gui::temperature
