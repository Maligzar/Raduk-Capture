#pragma once
// ============================================================
// FadeWatch.h -- phase 133.2: say so when the target is fading
//
// On 2026-11-08 Saturn faded steadily through an autofocus sweep, its
// peak falling from 255 to 154 in 90 s (dew on the corrector, or haze).
// Every position's focus score fell with it, and the routine's "got
// worse" check reverted on the fade alone. #105 made autofocus tolerate
// a fade; nothing told the operator one was happening, which is the
// thing to act on: wipe the corrector, switch the dew heater on, wait
// out the cloud.
//
// ── What is watched ─────────────────────────────────────────
//
// The target's TOTAL flux above the sky in the focus aperture
// (StarMetrics' fluxTotal, through FocusMetric). Not the peak: defocus
// spreads the light and lowers the peak with no change in the sky, and
// an autofocus sweep defocuses on purpose. The total is the same in or
// out of focus, so a trend in it is the sky or the optics.
//
// Exposure and gain changes reset the window, since a new exposure is a
// step in flux that is nobody's fault. Flux is not normalised for them:
// the gain curve is the camera's, and a reset is the honest answer.
//
// ── The rule ────────────────────────────────────────────────
//
// Over the last kWindowSec, a straight line through ln(flux) against
// time. A fade is a fit that explains the data (R^2 >= kMinR2, so seeing
// scatter does not trip it) and loses at least kMinDropFrac across the
// window. Then it waits kCooldownSec before it may say it again.
//
// Pure, no Windows: tests/fade_watch.cpp feeds it the night's numbers.
// ============================================================

#include <cmath>
#include <cstdio>
#include <deque>
#include <string>

namespace raduk::core {

struct FadeAlert {
    bool   fired     = false;
    double dropFrac  = 0.0;    // 0.4 = down 40% across the window
    double windowSec = 0.0;
};

class FadeWatch {
public:
    static constexpr double kWindowSec   = 90.0;   // the night's fade took ~90 s
    static constexpr double kMinSpanSec  = 45.0;   // say nothing on less
    static constexpr int    kMinSamples  = 12;
    static constexpr double kMinDropFrac = 0.25;
    static constexpr double kMinR2       = 0.6;
    static constexpr double kCooldownSec = 120.0;

    void Reset() {
        m_pts.clear();
        m_exposureUs = -1;
        m_gain = -1;
    }

    // One measured frame. `t` in seconds on any steady clock.
    FadeAlert Add(double t, double flux, long exposureUs, int gain) {
        FadeAlert a;
        if (!(flux > 0.0) || !std::isfinite(flux)) return a;
        if (exposureUs != m_exposureUs || gain != m_gain) {
            m_pts.clear();
            m_exposureUs = exposureUs;
            m_gain = gain;
        }
        m_pts.push_back({t, std::log(flux)});
        while (!m_pts.empty() && t - m_pts.front().t > kWindowSec) m_pts.pop_front();

        const double span = m_pts.back().t - m_pts.front().t;
        if ((int)m_pts.size() < kMinSamples || span < kMinSpanSec) return a;
        if (t - m_lastAlert < kCooldownSec) return a;

        // Least squares, ln(flux) = a + b t.
        double st = 0, sy = 0;
        for (const P& p : m_pts) { st += p.t; sy += p.y; }
        const double n = (double)m_pts.size(), mt = st / n, my = sy / n;
        double stt = 0, sty = 0, syy = 0;
        for (const P& p : m_pts) {
            const double dt = p.t - mt, dy = p.y - my;
            stt += dt * dt; sty += dt * dy; syy += dy * dy;
        }
        if (stt <= 0.0 || syy <= 0.0) return a;
        const double slope = sty / stt;                 // per second, in ln units
        const double r2 = (sty * sty) / (stt * syy);
        const double drop = 1.0 - std::exp(slope * span);
        if (slope < 0.0 && r2 >= kMinR2 && drop >= kMinDropFrac) {
            a.fired = true;
            a.dropFrac = drop;
            a.windowSec = span;
            m_lastAlert = t;
        }
        return a;
    }

private:
    struct P { double t, y; };
    std::deque<P> m_pts;
    long   m_exposureUs = -1;
    int    m_gain = -1;
    double m_lastAlert = -1e18;
};

// The words for the notification bar. `dewRisk` is the weather feed's
// "safe" / "elevated" / "warning", or empty when there is no fresh
// reading; dew is named only when the feed supports it.
inline std::wstring FadeMessage(const FadeAlert& a, const std::string& dewRisk) {
    wchar_t b[200];
    const int pct = (int)std::lround(a.dropFrac * 100.0);
    const int sec = (int)std::lround(a.windowSec);
    if (dewRisk == "warning" || dewRisk == "elevated")
        swprintf(b, 200, L"Target down %d%% in %d s, and dew risk is %hs: check the corrector",
                 pct, sec, dewRisk.c_str());
    else
        swprintf(b, 200, L"Target down %d%% in %d s: dew on the corrector, or cloud?", pct, sec);
    return b;
}

} // namespace raduk::core
