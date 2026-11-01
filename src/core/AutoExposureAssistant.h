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
//   - Moves EXPOSURE and GAIN along one path, LightLadder.h, so a
//     dim target gets gain before its exposure stops freezing seeing
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
#include "LightLadder.h"

namespace raduk::core {

// How long autofocus waits for auto-exposure before sweeping anyway.
// A correction takes 3 s with its settling, and a far-off start wants
// four or five of them.
inline constexpr double AF_AE_SETTLE_LIMIT_S = 20.0;

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

        // Where the ladder changes lever. See LightLadder.h.
        double expKneeMs = 20.0;
        int    gainKnee  = 350;

        // Proportional gain -- the fraction of the error in stops
        // applied per tick. See Tick() for phase 112's change of unit.
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
    // Phase 100: readable, so a caller can adjust one field without
    // restating the rest -- the profile hook changes only the two
    // exposure bounds and must not reset the gain rails doing it.
    const Config& Config_() const { return m_cfg; }

    // ── Mode control ──────────────────────────────────────────
    void SetAuto(bool enabled) {
        m_autoMode = enabled;
        m_settling = 0;
        m_settled  = false;
        if (enabled) Say("[AE] Auto mode ON");
        else         Say("[AE] Advisory mode");
    }
    // ── Phase 112: when the settings must not move ────────────
    //
    // Pass what is running, or nullptr when nothing is.
    //
    // A focus sweep compares sharpness between positions, and the
    // sharpness score is a Laplacian variance: it goes as the SQUARE of
    // the signal. So a 10% exposure change mid-sweep moves the score
    // about 21%, and it moves it where AE is most likely to act, since
    // defocus dims the highlight and AE answers by adding light. AE was
    // running straight through autofocus, bending the curve the fit was
    // reading.
    //
    // A recording is held for a plainer reason: the observation record
    // states one exposure and one gain for the run, and a stack of
    // frames at several of each matches no dark.
    //
    // A hold beats the clip emergency too. The operator can see clipping
    // and stop; a sweep or a run quietly changed underneath cannot be
    // repaired afterwards.
    //
    // On release the assistant settles first, so its first decision is
    // made on frames from after the sweep and not on the last one of it.
    void SetHold(const char* why) {
        const bool was = m_hold != nullptr;
        m_hold = why;
        if (was && !why) m_settling = m_cfg.settleFrames;
    }
    bool IsAuto()     const { return m_autoMode; }
    // True when the last tick looked and found nothing to change: on
    // target, or against a limit it cannot pass. Autofocus waits on it.
    bool Settled()    const { return m_settled; }
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
        m_settled    = false;

        if (!m_enabled || !m_hasData) {
            s.text = "AE: disabled";
            m_lastSuggestion = s;
            return s;
        }

        if (m_hold) {
            s.text = std::string("AE: held for ") + m_hold;
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
        // 30% off, down the ladder, so gain goes before exposure and a
        // rig at its exposure floor still has gain to give. A clipped
        // image is worse than low conversion gain, so this can pass the
        // HCG floor (phase 59).
        if (m_clipPct > m_cfg.clipEmergency) {
            s.clipping = true;
            const LightPoint p = WalkLadder(Ladder(), m_currentExpMs,
                                            m_currentGain, std::log2(0.70));
            s.exposureMs = p.expMs;
            s.gain       = p.gain;
            char buf[96];
            snprintf(buf, sizeof(buf),
                     "AE: CLIPPING %.1f%% -> exp %.2fms g=%d",
                     m_clipPct, s.exposureMs, s.gain);
            s.text = buf;
            // Nothing left to cut: autofocus should not wait on this.
            if (p.expMs == m_currentExpMs && p.gain == m_currentGain) {
                m_settled = true;
                m_lastSuggestion = s;
                return s;
            }
            m_settling = m_cfg.settleFrames;
            m_lastSuggestion = s;
            Say(s.text);
            return s;
        }

        // ── I-controller on the highlight ─────────────────────
        // Error: how far are we from the center of target range?
        float target = (m_cfg.targetLo + m_cfg.targetHi) / 2.0f;  // ~200
        float error  = target - m_highlightDN;  // positive = too dark
        float deadband = (m_cfg.targetHi - m_cfg.targetLo) / 2.0f;

        if (fabsf(error) <= deadband) {
            // In target range — no action
            char buf[64];
            snprintf(buf, sizeof(buf),
                     "AE: OK  hi %.0f DN  [%.0f-%.0f]",
                     m_highlightDN, m_cfg.targetLo, m_cfg.targetHi);
            s.text = buf;
            m_settled = true;
            m_lastSuggestion = s;
            return s;
        }

        // In stops, the unit the ladder walks in, and from the ratio
        // rather than the difference. The signal is linear in light, so
        // the ratio IS the correction; iGain takes a fraction of it to
        // leave room for seeing. A difference over 255 asked for only
        // 1.29x on a highlight of 60 DN that wanted 3x, which took
        // 50 s to converge, past what autofocus waits for.
        const double stops = m_cfg.iGain *
            std::log2((double)target / (std::max)((double)m_highlightDN, 1.0));
        const LightPoint p = WalkLadder(Ladder(), m_currentExpMs, m_currentGain,
                                        std::clamp(stops, -2.0, 2.0));

        // The threshold scales with the exposure, so a short one is
        // not held still by a figure chosen for a long one.
        const double expMinStep =
            (std::max)(m_cfg.expMinStepMs,
                       m_currentExpMs * m_cfg.expMinStepFrac);
        const bool expChanged  = fabs(p.expMs - m_currentExpMs) >= expMinStep;
        const bool gainChanged = abs(p.gain - m_currentGain) >= m_cfg.gainMinStep;

        if (expChanged || gainChanged) {
            if (expChanged)  s.exposureMs = p.expMs;
            if (gainChanged) s.gain       = p.gain;
            char ex[48] = "", ga[32] = "";
            if (expChanged)
                snprintf(ex, sizeof(ex), "Exp %.2f -> %.2fms  ",
                         m_currentExpMs, p.expMs);
            if (gainChanged)
                snprintf(ga, sizeof(ga), "Gain %d -> %d",
                         m_currentGain, p.gain);
            char full[112];
            snprintf(full, sizeof(full), "%s%s%s",
                     (error > 0) ? "AE: \xe2\x86\x91 " : "AE: \xe2\x86\x93 ",
                     ex, ga);
            s.text = full;
            m_settling = m_cfg.settleFrames;
            m_lastSuggestion = s;
            Say(s.text);
            return s;
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
        // Now it says which it actually is.
        char buf[96];
        const bool atCeiling = error > 0 &&
                               m_currentExpMs >= m_cfg.expMaxMs - expMinStep &&
                               m_currentGain >= m_cfg.gainMax;
        const bool atFloor   = error < 0 &&
                               m_currentExpMs <= m_cfg.expMinMs + expMinStep &&
                               m_currentGain <= 0;
        if (atCeiling || atFloor) {
            snprintf(buf, sizeof(buf),
                     "AE: at %s  hi %.0f DN  exp=%.2fms g=%d",
                     atCeiling ? "maximum" : "minimum",
                     m_highlightDN, m_currentExpMs, m_currentGain);
        } else {
            snprintf(buf, sizeof(buf),
                     "AE: holding  hi %.0f DN  exp=%.2fms g=%d",
                     m_highlightDN, m_currentExpMs, m_currentGain);
        }
        s.text = buf;
        m_settled = true;
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
    LightLadder Ladder() const {
        LightLadder l;
        l.expMinMs  = m_cfg.expMinMs;
        l.expKneeMs = m_cfg.expKneeMs;
        l.expMaxMs  = m_cfg.expMaxMs;
        l.gainFloor = m_cfg.gainMin;
        l.gainKnee  = m_cfg.gainKnee;
        l.gainMax   = m_cfg.gainMax;
        return l;
    }

    Config  m_cfg;
    bool    m_enabled    = false;
    bool    m_autoMode   = false;
    bool    m_hasData    = false;
    int     m_settling   = 0;
    const char* m_hold   = nullptr;   // a literal, see SetHold
    bool    m_settled    = false;

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
