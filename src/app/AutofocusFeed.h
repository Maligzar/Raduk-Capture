#pragma once
// ============================================================
//  AutofocusFeed -- Phase 153.0: what the live stream gives autofocus
//
//  Moved out of main.cpp's render loop unchanged in behaviour (it was
//  main.cpp:7114-7138 and :7259-7263). Two feeds:
//    - Sharpness: every fresh analysis with a measured half-flux
//      diameter, as the coarse sweep's score (1000/HFD).
//    - FastScore: during the fine pass (phase 145.2), the newest raw
//      frame scored on the edge metric, at most every 33 ms, over a
//      region whose size is fixed for the run because the score is
//      normalised by it.
//
//  A template over the routine, so a test can drive it with a fake and
//  the app with AutofocusRoutine. The frame and the analysis come in as
//  plain views, so nothing here needs Windows, the ring or the analyzer.
// ============================================================

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

#include "../core/FocusMetric.h"
#include "Clock.h"
#include "UiSink.h"

namespace raduk::app {

struct FrameView {
    const uint8_t* data = nullptr;
    uint32_t width = 0, height = 0;
    uint8_t  bitDepth = 8;
    bool     bayerRaw = false;
};

struct AnalysisView {
    bool  valid = false;
    bool  centroidValid = false;
    float centroidX = 0.0f, centroidY = 0.0f;
    float centroidBgDN = 0.0f;
    uint8_t centroidPeak = 0;
    bool  surfaceTracked = false;
    bool  focusValid = false;
    float focusScore = 0.0f;
    float focusHfdPx = 0.0f;
};

class AutofocusFeed {
public:
    static constexpr double kFastIntervalSec = 0.033;

    // The fine pass's edge region, half its side in pixels: the whole
    // surface when the target fills the frame, else sized from the disc
    // (a uniform disc's half-flux diameter is sqrt(2) x its radius).
    static int EdgeRegionHalf(bool filled, float hfdPx) {
        if (filled) return 128;
        const float r = hfdPx / 1.41421356f;
        return (std::max)(48, (std::min)(200, (int)(r * 1.3f) + 24));
    }

    template <class Routine>
    void FastScore(Routine& af, const FrameView& f, const AnalysisView& a,
                   const IClock& clock, IUiSink* ui) {
        if (!af.IsRunning()) m_edgeHalf = -1;
        if (!(f.data && af.WantsFastScores() && f.bayerRaw)) return;
        const double now = clock.Now();
        if (now - m_lastFast < kFastIntervalSec) return;
        m_lastFast = now;
        const bool filled = af.FineOnly() || (a.valid && a.surfaceTracked);
        if (m_edgeHalf <= 0) {
            m_edgeHalf = EdgeRegionHalf(filled, a.focusHfdPx);
            if (ui) {
                char b[128];
                std::snprintf(b, sizeof(b), "[AF] fine pass region: %d px each side of the target%s\n",
                              m_edgeHalf, filled ? " (the surface)" : "");
                ui->Line(b);
            }
        }
        float ecx = f.width * 0.5f, ecy = f.height * 0.5f;
        if (a.valid && a.centroidValid) { ecx = a.centroidX; ecy = a.centroidY; }
        const float bg = filled ? 0.0f : a.centroidBgDN;
        const raduk::core::EdgeFocus ef = raduk::core::MeasureEdgeFocus(
            f.data, f.width, f.height, f.bitDepth, ecx, ecy, m_edgeHalf, bg, m_scratch);
        if (ef.valid) af.SubmitFastScore(ef.score);
    }

    template <class Routine>
    static void Sharpness(Routine& af, const AnalysisView& a, bool fresh) {
        if (fresh && a.valid && af.IsRunning() && a.focusValid)
            af.SubmitSharpness(a.focusScore, a.centroidValid, (int)a.centroidPeak);
    }

    int EdgeHalf() const { return m_edgeHalf; }

private:
    int    m_edgeHalf = -1;
    double m_lastFast = -1e18;
    raduk::core::EdgeScratch m_scratch;
};

} // namespace raduk::app
