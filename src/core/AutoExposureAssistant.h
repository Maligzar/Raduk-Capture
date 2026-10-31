#pragma once
// ============================================================
// AutoExposureAssistant.h — Phase 12: Auto-Exposure Assistant
//
// Keeps the histogram peak in the 50–70% range (127–178 DN)
// by suggesting (or automatically applying) gain and exposure
// adjustments. Tuned for the ASI678MC at f/10 planetary work.
//
// Design:
//   - Two modes: ADVISORY (text only) and AUTO (applies changes)
//   - Uses a simple I-controller (integral only) — avoids the
//     oscillation that P-only gives with a slow camera loop
//   - Adjusts EXPOSURE first (preserves gain for noise floor
//     reasons), then GAIN if exposure hits its limits
//   - Clipping detected via clipPct — emergency gain/exp cut
//   - HCG floor enforced: gain never drops below 182 in AUTO
//     mode unless operator explicitly overrides
//
// Usage (main render loop, ~2Hz):
//   ae.Feed(lastAnalysis.meanBrightness, lastAnalysis.clipPct,
//           cam.GetControlValue(ControlId::Gain),
//           cam.GetControlValue(ControlId::ExposureUs) / 1000.0);  // ms
//   ae.Tick();
//   if (ae.IsAuto()) {
//       if (ae.SuggestGain() != ae.CurrentGain())
//           cam.SetGain(ae.SuggestGain());
//       if (ae.SuggestExposure() != ae.CurrentExposureMs())
//           cam.SetExposure(ae.SuggestExposure());
//   }
//   controls.SetAutoExposureStatus(ae.StatusText());
// ============================================================

#include <cmath>
#include <cstdio>
#include <string>
#include <functional>
#include <algorithm>
#include "profiles/TargetProfile.h"   // for GAIN_HCG_THRESHOLD

namespace raduk::core {

// Suggestion returned by the assistant each tick
struct AESuggestion {
    int    gain        = -1;    // -1 = no change
    double exposureMs  = -1.0;  // -1 = no change
    bool   clipping    = false; // emergency: sensor clipping
    std::string text;           // human-readable advisory
};

class AutoExposureAssistant {
public:
    // ── Configuration ─────────────────────────────────────────
    struct Config {
        // Target HIGHLIGHT range (0-255 DN) -- the brightness of the
        // brightest real thing in the frame, i.e. the planet.
        //
        // These numbers used to be applied to the frame MEAN, which the
        // comment above them already called "peak". On a planetary
        // frame the two are nothing alike: the frame is mostly black
        // sky, so its mean sits in the low tens of DN however the
        // planet is exposed. Driving that mean to 152 is impossible --
        // the controller pushed exposure and gain to their ceilings,
        // the planet clipped, the clip emergency cut 30% off, the mean
        // fell again, and it went round. That is the "cycles but never
        // settles" behaviour, and it was arithmetic, not tuning.
        //
        // 185-215 DN leaves the planet bright but off the ceiling, with
        // headroom for seeing to brighten a moment of good luck without
        // clipping it. UNVERIFIED ON SKY -- see the clear-night list.
        float targetLo  = 185.0f;  // 73% of 255
        float targetHi  = 215.0f;  // 84% of 255

        // Clip threshold — emergency cut if any channel clips this %
        float clipEmergency = 1.5f;  // > 1.5% clipping = cut exposure

        // Exposure limits (milliseconds)
        double expMinMs = 0.5;
        double expMaxMs = 50.0;

        // Gain limits
        int gainMin = raduk::profiles::CameraRails::GAIN_HCG_THRESHOLD; // 182 - HCG floor
        int gainMax = 570;

        // Proportional gain -- the fraction of the normalised error
        // applied per tick.
        //
        // Phase 98: this was 0.10f with a comment saying "10%
        // correction per 2s tick", while the expression below
        // multiplied it by a bare 5. The real figure has always been
        // 50%. The constant now says what it does and the 5 is gone;
        // the arithmetic is unchanged.
        float iGain = 0.50f;

        // ── Minimum change worth applying ───────────────────
        //
        // RELATIVE, not absolute, and this was a real defect.
        //
        // It was a flat 0.1 ms, which is a sensible floor at 50 ms and
        // an impassable wall at 2. The correction is multiplicative --
        // exposure times a factor -- so at short exposures the step it
        // produces is small in absolute terms however large the error.
        // Measured against the old constants, the smallest error that
        // could move the exposure was:
        //
        //     3.5 ms ->  14.6 DN   (works: the deadband is +/-15)
        //     2.0 ms ->  25.5 DN   (stalls)
        //     1.0 ms ->  51.0 DN   (stalls)
        //     0.5 ms -> 102.0 DN   (stalls, and barely reachable)
        //
        // So below 3.4 ms auto-exposure could not make its own
        // smallest legal correction. Worse, the gain branch is gated
        // on the exposure being AT a limit, which it was not -- so
        // nothing happened and it fell through to a message reading
        // "AE: limit". It did not fail; it reported a limit it was
        // nowhere near. The Moon lives at 1 to 5 ms.
        //
        // One per cent of the current exposure, floored at the
        // camera's own 1 us resolution. At 2 ms that is 0.02 ms, which
        // a 25 DN error clears easily; at 50 ms it is 0.5 ms, which is
        // MORE jitter rejection than the old flat figure gave.
        double expMinStepFrac = 0.01;   // 1% of current exposure
        double expMinStepMs   = 0.001;  // ASI resolution: 1 us
        int    gainMinStep    = 3;      // DN

        // How many ticks to wait after a change before sampling again
        int    settleFrames = 5;
    };

    AutoExposureAssistant() = default;

    // Every diagnostic in here was a printf, and this is a GUI-subsystem
    // build where printf reaches nothing at all. So the one running
    // commentary that explains why auto-exposure is doing what it is
    // doing has been invisible for its whole life -- including while it
    // was oscillating. Route it at the log instead.
    void SetLogCallback(std::function<void(const std::string&)> cb) {
        m_log = std::move(cb);
    }

    void SetConfig(const Config& c) { m_cfg = c; }

    // ── Mode control ──────────────────────────────────────────
    void SetAuto(bool enabled) {
        m_autoMode = enabled;
        m_settling = 0;
        if (enabled) Say("[AE] Auto mode ON");
        else         Say("[AE] Advisory mode");
    }
    bool IsAuto()     const { return m_autoMode; }
    bool IsEnabled()  const { return m_enabled; }
    void SetEnabled(bool e) { m_enabled = e; if (!e) m_autoMode = false; }

    // ── Data feed (call every analysis update, ~2Hz) ──────────
    void Feed(float highlightDN, float meanBrightness, float clipPct,
              long currentGain, double currentExpMs) {
        m_highlightDN     = highlightDN;
        m_meanBrightness  = meanBrightness;
        m_clipPct         = clipPct;
        m_currentGain     = (int)currentGain;
        m_currentExpMs    = currentExpMs;
        m_hasData         = true;
    }

    // ── Compute suggestion (call after Feed) ──────────────────
    AESuggestion Tick() {
        AESuggestion s;
        s.gain       = m_currentGain;
        s.exposureMs = m_currentExpMs;

        if (!m_enabled || !m_hasData) {
            s.text = "AE: disabled";
            m_lastSuggestion = s;
            return s;
        }

        // Settling — wait after last change
        if (m_settling > 0) {
            --m_settling;
            char buf[64];
            snprintf(buf, sizeof(buf), "AE: settling (%d)", m_settling);
            s.text = buf;
            m_lastSuggestion = s;
            return s;
        }

        // ── Emergency clip cut ─────────────────────────────────
        if (m_clipPct > m_cfg.clipEmergency) {
            s.clipping = true;
            // Cut exposure by 30%
            double newExp = m_currentExpMs * 0.70;
            newExp = std::max(newExp, m_cfg.expMinMs);
            s.exposureMs = snapExp(newExp);
            // Phase 59 (review S4): if exposure is already at its floor and
            // we're still clipping, exposure alone can never recover — cut
            // gain too (below the HCG floor if that's what it takes; a
            // clipped image is worse than low conversion gain).
            // Phase 98: "at the floor" is a tolerance on the floor
            // itself, not on the current exposure, so it uses the
            // absolute resolution rather than the relative step. The
            // relative one would scale with whatever the exposure
            // happens to be and so would mean something different on
            // each tick of a run that is actively cutting it.
            const double floorTol = m_cfg.expMinStepMs;
            bool expStuck = (s.exposureMs   <= m_cfg.expMinMs + floorTol) &&
                            (m_currentExpMs <= m_cfg.expMinMs + floorTol);
            if (expStuck && m_currentGain > 0) {
                int newGain = (int)(m_currentGain * 0.85f);
                s.gain = std::max(0, newGain);
            }
            char buf[96];
            snprintf(buf, sizeof(buf),
                     "AE: CLIPPING %.1f%% -> exp %.2fms%s",
                     m_clipPct, s.exposureMs,
                     (s.gain != m_currentGain) ? " + gain cut" : "");
            s.text = buf;
            m_settling = m_cfg.settleFrames;
            m_lastSuggestion = s;
            Say(s.text);
            return s;
        }

        // ── I-controller on mean brightness ───────────────────
        // Error: how far are we from the center of target range?
        float target = (m_cfg.targetLo + m_cfg.targetHi) / 2.0f;  // ~200
        float error  = target - m_highlightDN;  // positive = too dark
        float deadband = (m_cfg.targetHi - m_cfg.targetLo) / 2.0f; // ±25.5

        if (fabsf(error) <= deadband) {
            // In target range — no action
            char buf[64];
            snprintf(buf, sizeof(buf),
                     "AE: OK  hi %.0f DN  [%.0f-%.0f]",
                     m_highlightDN, m_cfg.targetLo, m_cfg.targetHi);
            s.text = buf;
            m_lastSuggestion = s;
            return s;
        }

        // Correction factor: proportional to error. error/255 gives
        // the fraction; iGain is applied directly now that it says
        // what it means (see the Config comment).
        float correctionFactor = 1.0f + (error / 255.0f) * m_cfg.iGain;

        // ── Try exposure first ─────────────────────────────────
        double newExp = m_currentExpMs * correctionFactor;
        newExp = std::clamp(newExp, m_cfg.expMinMs, m_cfg.expMaxMs);
        newExp = snapExp(newExp);

        // The threshold scales with the exposure, so a short one is
        // not held still by a figure chosen for a long one.
        const double expMinStep =
            (std::max)(m_cfg.expMinStepMs,
                       m_currentExpMs * m_cfg.expMinStepFrac);
        bool expChanged = fabs(newExp - m_currentExpMs) >= expMinStep;

        if (expChanged) {
            s.exposureMs = newExp;
            const char* dir = (newExp > m_currentExpMs) ? "+" : "";
            char buf[80];
            snprintf(buf, sizeof(buf),
                     "AE: %s%.2fms  (hi %.0f DN)",
                     dir, newExp - m_currentExpMs, m_highlightDN);
            // Prepend arrow for clarity
            std::string arrow = (newExp > m_currentExpMs) ? "AE: Exp +" : "AE: Exp ";
            char full[80];
            snprintf(full, sizeof(full),
                     "%s%.2fms -> %.2fms",
                     (newExp > m_currentExpMs) ? "AE: \xe2\x86\x91 Exp " : "AE: \xe2\x86\x93 Exp ",
                     m_currentExpMs, newExp);
            s.text = full;
            m_settling = m_cfg.settleFrames;
            m_lastSuggestion = s;
            Say(s.text);
            return s;
        }

        // Exposure already at limit — try gain
        bool expAtMax = (m_currentExpMs >= m_cfg.expMaxMs - expMinStep);
        bool expAtMin = (m_currentExpMs <= m_cfg.expMinMs + expMinStep);
        bool needGain = (error > 0 && expAtMax) || (error < 0 && expAtMin);

        if (needGain) {
            int newGain = (int)(m_currentGain * correctionFactor);
            // Phase 59 (review S4): the HCG floor is advisory, not a hard
            // clamp. Clamping a DOWNWARD correction to 182 snapped gain UP
            // on already-bright low-gain profiles (Venus/Moon at min
            // exposure), brightening a clipped image. Only enforce the
            // floor when the correction itself is raising gain.
            int lowBound = (error < 0) ? 0 : m_cfg.gainMin;
            newGain = std::clamp(newGain, lowBound, m_cfg.gainMax);
            int gainDelta = newGain - m_currentGain;

            if (abs(gainDelta) >= m_cfg.gainMinStep) {
                s.gain = newGain;
                char buf[80];
                snprintf(buf, sizeof(buf),
                         "%s%d -> %d  (exp limit)",
                         (gainDelta > 0) ? "AE: \xe2\x86\x91 Gain " : "AE: \xe2\x86\x93 Gain ",
                         m_currentGain, newGain);
                s.text = buf;
                m_settling = m_cfg.settleFrames;
                m_lastSuggestion = s;
                Say(s.text);
                return s;
            }
        }

        // ── Nothing applied, and WHY matters ────────────────
        //
        // This used to read "AE: limit" unconditionally and print
        // m_meanBrightness -- a quantity the controller does not use
        // for any decision; every one of them is made on
        // m_highlightDN. So on the one occasion the operator most
        // needs to know what is going on, the panel named a limit it
        // might be nowhere near and showed the wrong number.
        //
        // Now it says which of the three it actually is.
        char buf[96];
        const bool atCeiling = expAtMax && m_currentGain >= m_cfg.gainMax;
        const bool atFloor   = expAtMin && m_currentGain <= 0;
        if (atCeiling || atFloor) {
            snprintf(buf, sizeof(buf),
                     "AE: at %s  hi %.0f DN  exp=%.2fms g=%d",
                     atCeiling ? "maximum" : "minimum",
                     m_highlightDN, m_currentExpMs, m_currentGain);
        } else if (needGain) {
            // Gain was the right lever and the step was too small to
            // be worth a write.
            snprintf(buf, sizeof(buf),
                     "AE: holding  hi %.0f DN  gain step < %d",
                     m_highlightDN, m_cfg.gainMinStep);
        } else {
            snprintf(buf, sizeof(buf),
                     "AE: holding  hi %.0f DN  exp=%.2fms g=%d",
                     m_highlightDN, m_currentExpMs, m_currentGain);
        }
        s.text = buf;
        m_lastSuggestion = s;
        return s;
    }

    // ── Accessors ─────────────────────────────────────────────
    const AESuggestion& LastSuggestion() const { return m_lastSuggestion; }
    std::string StatusText() const { return m_lastSuggestion.text; }

    int    CurrentGain()       const { return m_currentGain; }
    double CurrentExposureMs() const { return m_currentExpMs; }
    float  MeanBrightness()    const { return m_meanBrightness; }

private:
    // Round exposure to sensible steps to avoid micro-jitter
    static double snapExp(double ms) {
        if      (ms < 1.0)  return round(ms * 10.0) / 10.0;  // 0.1ms steps
        else if (ms < 5.0)  return round(ms * 4.0)  / 4.0;   // 0.25ms steps
        else if (ms < 20.0) return round(ms * 2.0)  / 2.0;   // 0.5ms steps
        else                return round(ms);                  // 1ms steps
    }

    Config  m_cfg;
    bool    m_enabled    = false;
    bool    m_autoMode   = false;
    bool    m_hasData    = false;
    int     m_settling   = 0;

    std::function<void(const std::string&)> m_log;
    void Say(const std::string& s) const { if (m_log) m_log(s); }

    float   m_meanBrightness = 0.0f;
    float   m_highlightDN    = 0.0f;
    float   m_clipPct        = 0.0f;
    int     m_currentGain    = 200;
    double  m_currentExpMs   = 8.0;

    AESuggestion m_lastSuggestion;
};

} // namespace raduk::core
