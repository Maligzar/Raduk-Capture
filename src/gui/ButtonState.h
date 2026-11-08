#pragma once
// ============================================================
// ButtonState.h -- phase 132.5: how a button face answers the hand
//
// 119.4 gave the owner-drawn buttons a face but no response: the
// pointer could rest on one and nothing said so, and a press only
// swapped the fill. This is the pure part of the fix, no Windows, so
// tests/button_state.cpp can pin it.
//
// ── The look ────────────────────────────────────────────────
//
// A face is described by `lift`, in surface steps relative to PANEL:
//
//    +0.5  hovered: halfway up to BORDER. Not the whole step, because
//          the frame IS border and a full step would erase it.
//     0    at rest
//    -1    pressed: down to DEEP, the panel background, so the face
//          reads as pushed into the window. Content drops 1 px and the
//          lit top edge goes, which is what pressing means.
//
// Lift is luminance on the surface ladder, never hue, so night vision
// carries it unchanged: the steps are 24 / 29 / 14 at night.
//
// A disabled button does not respond to hover, and is never drawn
// pressed; its text goes dim, as before.
//
// ── The ramp ────────────────────────────────────────────────
//
// Hover eases in and out over Motion::FAST with the 132.4 ease-out, and
// reverses from wherever it is if the pointer leaves halfway. With
// motion off (rig.ini UiMotion=0) it is instant. Press is always
// instant: a press that lags the finger feels broken.
// ============================================================

namespace raduk::gui::button {

inline constexpr float kHoverLift   = 0.5f;
inline constexpr float kPressedLift = -1.0f;

struct Look {
    float lift    = 0.0f;   // surface steps from PANEL; see above
    int   offset  = 0;      // content drop, px
    bool  litEdge = true;   // the raised face's top highlight
    bool  dimText = false;
};

inline constexpr float Clamp01(float t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }

// `hover` is the ramp's level, 0..1.
inline constexpr Look LookFor(bool disabled, bool pressed, float hover) {
    Look l;
    if (disabled) { l.dimText = true; return l; }
    if (pressed)  { l.lift = kPressedLift; l.offset = 1; l.litEdge = false; return l; }
    l.lift = kHoverLift * Clamp01(hover);
    return l;
}

inline constexpr float EaseOut(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

// The hover level over time. Times are milliseconds on any steady clock.
class HoverRamp {
public:
    // The pointer arrived (hot) or left. A repeat of the current target
    // changes nothing, so WM_MOUSEMOVE can call this on every move.
    void Set(bool hot, double nowMs, int durationMs, bool motion) {
        const float target = hot ? 1.0f : 0.0f;
        if (target == m_to) return;
        m_from = motion ? Level(nowMs) : target;
        m_to = target;
        m_at = nowMs;
        m_dur = motion && durationMs > 0 ? durationMs : 0;
    }

    float Level(double nowMs) const {
        if (m_dur <= 0) return m_to;
        const float p = Clamp01((float)((nowMs - m_at) / m_dur));
        return m_from + (m_to - m_from) * EaseOut(p);
    }

    bool Settled(double nowMs) const { return m_dur <= 0 || nowMs - m_at >= m_dur; }
    bool Hot() const { return m_to > 0.5f; }

private:
    float  m_from = 0.0f, m_to = 0.0f;
    double m_at   = 0.0;
    int    m_dur  = 0;
};

} // namespace raduk::gui::button
