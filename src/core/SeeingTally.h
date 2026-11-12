#pragma once
// ============================================================
// SeeingTally.h -- phase 138: the seeing forecast against the sky
//
// Two forecasts of seeing are free: 7Timer's seeing class (1 best to 8
// worst) and the jet stream, Open-Meteo's wind at 200 and 250 hPa
// (above about 126 km/h the seeing is reliably bad). Whether either
// predicts THIS site is unknown, and a paid service waits on the answer.
//
// So once an hour the session log gets one SEEING line: the latest
// forecast Leoben passed on, beside the seeing the camera measured over
// that hour (SeeingMonitor's normalised sharpness: median, best, count,
// and the target, because the figure is only comparable within one
// target). After about ten nights the log says which source, if any,
// tracks the measured sky. Fetching the forecasts is Leoben's half.
//
// Pure: tests/seeing_tally.cpp.
// ============================================================

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace raduk::core {

struct SeeingForecast {
    double seeing7Timer = -1;   // 1 (best) to 8, -1 = not given
    double jet200Kmh = -1;      // wind at 200 hPa, -1 = not given
    double jet250Kmh = -1;
    bool Any() const { return seeing7Timer >= 0 || jet200Kmh >= 0 || jet250Kmh >= 0; }
};

inline constexpr double kJetBadKmh = 126.0;

class SeeingTally {
public:
    explicit SeeingTally(int64_t periodSec = 3600) : m_period(periodSec) {}

    void Forecast(const SeeingForecast& f) { if (f.Any()) m_forecast = f; }
    // Leoben not fresh this hour: an old forecast beside new measurements
    // would be a false pairing, so the line says none.
    void ClearForecast() { m_forecast = SeeingForecast{}; }
    void Measured(double normalised, const std::string& target) {
        if (normalised <= 0.0) return;
        if (!target.empty() && target != m_target) {
            m_values.clear();   // a new target is a new scale
            m_target = target;
        }
        m_values.push_back(normalised);
    }

    // True once a period has passed since the first call; the first call
    // only starts the clock.
    bool Due(int64_t nowUnix) {
        if (m_next == 0) { m_next = nowUnix + m_period; return false; }
        return nowUnix >= m_next;
    }

    // The line for the period just ended, and the start of the next.
    std::string TakeLine(int64_t nowUnix) {
        m_next = nowUnix + m_period;
        std::string s = "forecast ";
        char b[96];
        if (!m_forecast.Any()) s += "none from Leoben";
        else {
            bool first = true;
            auto sep = [&]() { if (!first) s += ", "; first = false; };
            if (m_forecast.seeing7Timer >= 0) {
                sep(); snprintf(b, sizeof(b), "7Timer seeing %.0f of 8", m_forecast.seeing7Timer); s += b;
            }
            if (m_forecast.jet200Kmh >= 0) {
                sep(); snprintf(b, sizeof(b), "jet 200 hPa %.0f km/h", m_forecast.jet200Kmh); s += b;
            }
            if (m_forecast.jet250Kmh >= 0) {
                sep(); snprintf(b, sizeof(b), "jet 250 hPa %.0f km/h", m_forecast.jet250Kmh); s += b;
            }
            const double jet = (std::max)(m_forecast.jet200Kmh, m_forecast.jet250Kmh);
            if (jet >= kJetBadKmh) s += " (jet above the bad-seeing line)";
        }
        s += " | measured ";
        if (m_values.empty()) s += "none";
        else {
            std::vector<double> v = m_values;
            std::sort(v.begin(), v.end());
            const size_t n = v.size();
            const double med = n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
            snprintf(b, sizeof(b), "median %.1f, best %.1f over %zu samples", med, v.back(), n);
            s += b;
            if (!m_target.empty()) s += ", target " + m_target;
        }
        m_values.clear();
        return s;
    }

private:
    int64_t m_period;
    int64_t m_next = 0;
    SeeingForecast m_forecast;
    std::vector<double> m_values;
    std::string m_target;
};

} // namespace raduk::core
