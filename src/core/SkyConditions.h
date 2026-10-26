// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// ============================================================
//  SkyConditions — how much atmosphere the scope is looking through
//
//  Altitude is the single biggest thing the observer controls on a
//  planetary night, and the mount already reports it. What it does not
//  report is what that altitude MEANS: how much air is in the way, how
//  much that costs in resolution, and whether to shoot now or wait.
//
//  This is useful with or without a corrector in the train. An ADC
//  cancels the colour smear; nothing cancels the extra air.
//
//  Deliberately free of Windows and D2D so tests/sky_conditions.cpp can
//  drive it against hand-worked numbers.
// ============================================================

#pragma once

#include <cmath>
#include <cstdint>
#include <deque>
#include <string>

namespace raduk::core {

// The imaging window, in the owner's terms: climb to 60 degrees, shoot
// through the top, stop on the way back down past 60.
enum class SkyWindow { Prime, Good, Marginal, Poor, TooLow };

struct SkyQuality {
    bool      valid      = false;
    double    altitudeDeg = 0.0;
    double    azimuthDeg  = 0.0;

    // Air masses of atmosphere in the line of sight. 1.00 at the
    // zenith by definition.
    double    airmass    = 0.0;

    // Resolution cost relative to the zenith. Seeing-limited FWHM grows
    // roughly as airmass^0.6, so this is "how much worse than overhead"
    // -- 1.00 at the zenith, about 1.09 at 60 degrees, about 1.52 at 30.
    double    seeingFactor = 1.0;

    SkyWindow window      = SkyWindow::TooLow;

    // Degrees per minute, from observed altitude. Positive is climbing.
    // Zero until enough history has been seen.
    double    trendDegPerMin = 0.0;

    // Minutes until the prime window is entered, or left. Negative when
    // unknown or not applicable.
    double    minutesToPrime = -1.0;
    double    minutesLeftInPrime = -1.0;
};

// ── Airmass, Kasten & Young (1989) ──────────────────────────
//
// Not the schoolbook 1/sin(h). That diverges at the horizon and is
// already 2% out by 20 degrees, which is exactly the altitude range
// where a planetary imager is deciding whether to bother. This form is
// within 1% all the way to the horizon.
inline double Airmass(double altitudeDeg) {
    if (altitudeDeg >= 90.0) return 1.0;
    if (altitudeDeg <= -1.0) return 40.0;          // below the horizon
    const double h = altitudeDeg;
    const double rad = h * 3.14159265358979323846 / 180.0;
    const double denom = std::sin(rad) + 0.50572 * std::pow(h + 6.07995, -1.6364);
    if (denom <= 1e-9) return 40.0;
    const double x = 1.0 / denom;
    return (x > 40.0) ? 40.0 : x;
}

// Seeing-limited resolution cost relative to the zenith.
//
// The 0.6 exponent is the standard approximation for how a turbulent
// column scales with path length. It is a rule of thumb, not a
// measurement, and it is here to turn an airmass into something an
// observer can act on -- "a fifth worse than overhead" means more than
// "1.4 air masses".
inline double SeeingFactor(double altitudeDeg) {
    return std::pow(Airmass(altitudeDeg), 0.6);
}

inline SkyWindow WindowFor(double altitudeDeg) {
    if (altitudeDeg >= 60.0) return SkyWindow::Prime;
    if (altitudeDeg >= 45.0) return SkyWindow::Good;
    if (altitudeDeg >= 30.0) return SkyWindow::Marginal;
    if (altitudeDeg >= 20.0) return SkyWindow::Poor;
    return SkyWindow::TooLow;
}

inline const char* WindowName(SkyWindow w) {
    switch (w) {
        case SkyWindow::Prime:    return "PRIME";
        case SkyWindow::Good:     return "GOOD";
        case SkyWindow::Marginal: return "MARGINAL";
        case SkyWindow::Poor:     return "POOR";
        default:                  return "TOO LOW";
    }
}

// One line an observer can act on.
inline std::string WindowAdvice(const SkyQuality& q) {
    if (!q.valid) return "no mount altitude";
    switch (q.window) {
        case SkyWindow::Prime:
            if (q.minutesLeftInPrime > 0.0 && q.minutesLeftInPrime < 600.0) {
                char b[96];
                snprintf(b, sizeof(b), "PRIME - about %.0f min left",
                         q.minutesLeftInPrime);
                return b;
            }
            return "PRIME - shoot now";
        case SkyWindow::Good:
            if (q.trendDegPerMin > 0.002 && q.minutesToPrime > 0.0 &&
                q.minutesToPrime < 600.0) {
                char b[96];
                snprintf(b, sizeof(b), "GOOD - prime in about %.0f min",
                         q.minutesToPrime);
                return b;
            }
            return q.trendDegPerMin < -0.002 ? "GOOD - past best, descending"
                                             : "GOOD";
        case SkyWindow::Marginal:
            return q.trendDegPerMin > 0.002 ? "MARGINAL - climbing, worth waiting"
                                            : "MARGINAL - descending";
        case SkyWindow::Poor:
            return "POOR - twice the air of overhead";
        default:
            return "TOO LOW - not worth the frames";
    }
}

// ── Tracking altitude over time ─────────────────────────────
//
// The trend is measured rather than computed from ephemeris: the mount
// reports where it is pointing, and the difference between two readings
// a while apart is the answer. No site, no epoch, no target catalogue,
// and it is right even when slewing to somewhere unexpected.
class SkyTracker {
public:
    // Keep roughly five minutes of history. Shorter than that and
    // mount jitter dominates a rate of a tenth of a degree a minute;
    // longer and a slew to a new target poisons the fit for ages.
    static constexpr double WINDOW_SEC = 300.0;
    static constexpr double MIN_SPAN_SEC = 45.0;   // before trusting a slope

    // altDeg/azDeg as the mount reports them; nowSec any monotonic clock.
    void Push(double altDeg, double azDeg, double nowSec) {
        // A jump means a slew, not a drift. Fitting across one would
        // report a rate of degrees per second and predict nonsense.
        if (!m_samples.empty()) {
            const double dAlt = std::fabs(altDeg - m_samples.back().alt);
            const double dT   = nowSec - m_samples.back().t;
            if (dT > 0.0 && dAlt / dT > 0.5)       // >30 deg/min: a slew
                m_samples.clear();
        }
        m_samples.push_back({ altDeg, azDeg, nowSec });
        while (m_samples.size() > 2 &&
               nowSec - m_samples.front().t > WINDOW_SEC)
            m_samples.pop_front();
    }

    void Reset() { m_samples.clear(); }
    int  SampleCount() const { return (int)m_samples.size(); }

    SkyQuality Evaluate() const {
        SkyQuality q;
        if (m_samples.empty()) return q;

        const Sample& last = m_samples.back();
        q.valid        = true;
        q.altitudeDeg  = last.alt;
        q.azimuthDeg   = last.az;
        q.airmass      = Airmass(last.alt);
        q.seeingFactor = SeeingFactor(last.alt);
        q.window       = WindowFor(last.alt);

        // Least squares on (t, alt). A plain endpoint difference would
        // ride on whichever two readings happened to land at the ends.
        const double span = last.t - m_samples.front().t;
        if (m_samples.size() >= 3 && span >= MIN_SPAN_SEC) {
            double n = 0, st = 0, sa = 0, stt = 0, sta = 0;
            for (const Sample& s : m_samples) {
                const double t = s.t - m_samples.front().t;
                n++; st += t; sa += s.alt; stt += t * t; sta += t * s.alt;
            }
            const double denom = n * stt - st * st;
            if (std::fabs(denom) > 1e-9) {
                const double slopePerSec = (n * sta - st * sa) / denom;
                q.trendDegPerMin = slopePerSec * 60.0;
            }
        }

        // Project the boundary, but only while actually moving. A
        // stationary mount would otherwise be told it reaches 60 degrees
        // in several thousand minutes, which is true and useless.
        const double PRIME = 60.0;
        if (q.trendDegPerMin > 0.002 && q.altitudeDeg < PRIME)
            q.minutesToPrime = (PRIME - q.altitudeDeg) / q.trendDegPerMin;
        if (q.trendDegPerMin < -0.002 && q.altitudeDeg > PRIME)
            q.minutesLeftInPrime = (q.altitudeDeg - PRIME) / -q.trendDegPerMin;

        return q;
    }

private:
    struct Sample { double alt, az, t; };
    std::deque<Sample> m_samples;
};

} // namespace raduk::core
