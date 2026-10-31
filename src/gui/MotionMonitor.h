#pragma once
// ============================================================
// MotionMonitor.h — Phase 75
//
// Shows that the mount is actually moving, and by how much.
//
// The problem it solves: at FINE rate a short press moves the telescope a
// few arcseconds. Nothing on screen changed enough to notice, so there was
// no way to tell a command that did nothing from a command that did
// something small — which is exactly when you need to know.
//
// Two sources, because neither alone is enough:
//
//   CAMERA  — the planet's centroid, every frame. Sub-arcsecond and
//             effectively instant, but only exists while a planet is
//             detected. Preferred whenever available.
//   MOUNT   — alt/az read back over the serial link. Always available,
//             but polled every 500 ms (2 s while capturing), so a short
//             nudge shows up as one jump rather than a smooth line.
//
// The active source is drawn on the plot, because the two have very
// different time resolution and a reading that does not say where it came
// from invites the wrong conclusion.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "Theme.h"
#include <cmath>
#include <cstdio>
#include <deque>

namespace raduk::gui {

enum class MotionSource { None, Camera, Mount };

class MotionMonitor {
public:
    // Rolling window. Long enough to see a jog and the drift after it,
    // short enough that the plot reflects now rather than the session.
    static constexpr double WINDOW_S   = 10.0;
    // Speed is measured over a SHORTER window than the trail. It has to
    // be long enough to contain the mount feed, whose samples arrive
    // >= 2s apart, but short enough that a jog is not averaged away
    // across the whole 10s trail.
    static constexpr double SPEED_WINDOW_S = 2.5;
    // NOTE on scaling: the plot frames the TRAIL'S OWN BOUNDING BOX,
    // never distance from the origin. The origin is fixed at the first
    // sample, so after a GoTo of a few degrees every later point sits
    // ~100000 arcsec away from it and an origin-based scale would be
    // wrong by orders of magnitude forever after, leaving the few-arcsec
    // moves this exists to show as a sub-pixel dot. Framing the trail
    // instead is self-correcting and needs nothing reset.
    // Never auto-scale tighter than this, or a stationary mount fills the
    // plot with seeing jitter and looks like it is running away.
    static constexpr float  MIN_SPAN_AS = 6.0f;
    // A camera sample this much newer than the last mount sample wins.
    static constexpr double CAMERA_HOLD_S = 1.5;

    struct Sample { float x, y; double t; };

    void Reset() {
        m_pts.clear();
        m_haveOrigin = false;
        m_src = MotionSource::None;
        m_lastCamera = -1e9;
    }

    // Planet centroid, in frame pixels, with the rig's plate scale.
    void PushCamera(float px, float py, float arcsecPerPx, double tSec) {
        if (arcsecPerPx <= 0.0f) return;
        if (m_src != MotionSource::Camera) { m_pts.clear(); m_haveOrigin = false; }
        m_src = MotionSource::Camera;
        m_lastCamera = tSec;
        if (!m_haveOrigin) { m_ox = px; m_oy = py; m_haveOrigin = true; }
        Add((px - (float)m_ox) * arcsecPerPx,
            (py - (float)m_oy) * arcsecPerPx, tSec);
    }

    // Mount alt/az in degrees. Ignored while camera samples are fresh.
    void PushMount(double altDeg, double azDeg, double tSec) {
        if (m_src == MotionSource::Camera &&
            (tSec - m_lastCamera) < CAMERA_HOLD_S) return;
        if (m_src != MotionSource::Mount) { m_pts.clear(); m_haveOrigin = false; }
        m_src = MotionSource::Mount;
        if (!m_haveOrigin) { m_ox = azDeg; m_oy = altDeg; m_haveOrigin = true; }
        // Azimuth degrees shrink as you approach the zenith: 1 degree of az
        // at alt 80 is only ~10 arcmin of actual sky. Without the cosine the
        // plot would exaggerate every east-west move made up high.
        const double cosAlt = cos(altDeg * 3.14159265358979 / 180.0);
        double dAz = azDeg - m_ox;
        while (dAz >  180.0) dAz -= 360.0;      // wrap at due north
        while (dAz < -180.0) dAz += 360.0;
        Add((float)(dAz * cosAlt * 3600.0),
            (float)((altDeg - m_oy) * 3600.0), tSec);
    }

    MotionSource Source() const { return m_src; }
    int SampleCount() const { return (int)m_pts.size(); }

    // Path length over the last second, per second. This is the number that
    // answers "did that command do anything": it goes non-zero the moment
    // the mount moves, however small the move.
    float SpeedArcsecPerSec() const {
        if (m_pts.size() < 2) return 0.0f;
        const double tEnd = m_pts.back().t;

        // ALWAYS include the newest segment, however long it spans.
        //
        // This used to skip any segment older than a fixed 1.0s, which made
        // the figure permanently 0.0 on the MOUNT source: that feed is
        // gated by `++mountTick % 4` on top of a 500ms poll, so its samples
        // are >= 2s apart (8s while recording) and the very first loop test
        // broke before accumulating anything. The widget printed
        // MOUNT 0.0"/s, in the grey not-moving colour, during a full 3 deg/s
        // slew - the exact false conclusion it exists to prevent.
        // Walk back by INDEX, not by tracking a timestamp separately - the
        // two get out of step at the loop boundary.
        size_t first = m_pts.size() - 2;          // newest segment, always in
        while (first > 0 && (tEnd - m_pts[first - 1].t) <= SPEED_WINDOW_S)
            first--;
        const double dt = tEnd - m_pts[first].t;
        if (dt <= 1e-3) return 0.0f;

        // NET DISPLACEMENT over the window, not path length along it.
        //
        // Path length counts seeing jitter, which is oscillatory: the planet
        // wanders several arcsec and returns, so a stationary mount still
        // accumulated metres of path and read ~5"/s - above both the green
        // "moving" colour and main.cpp's logging gate. Net displacement
        // cancels the wander and leaves the commanded motion, which is the
        // only thing this number is asked about.
        const double dx = m_pts.back().x - m_pts[first].x;
        const double dy = m_pts.back().y - m_pts[first].y;
        return (float)(std::sqrt(dx * dx + dy * dy) / dt);
    }

    void Draw(HDC hdc, const RECT& rc) const {
        const int w = rc.right - rc.left, h = rc.bottom - rc.top;
        if (w < 40 || h < 30) return;

        HBRUSH bg = CreateSolidBrush(Theme::NV(Theme::Rgb::DEEP));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);
        HPEN border = CreatePen(PS_SOLID, 1, Theme::NV(RGB(40, 54, 74)));
        HGDIOBJ oldPen = SelectObject(hdc, border);
        HGDIOBJ oldBr  = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(hdc, oldBr);
        SelectObject(hdc, oldPen);
        DeleteObject(border);

        SetBkMode(hdc, TRANSPARENT);

        if (m_src == MotionSource::None || m_pts.empty()) {
            SetTextColor(hdc, Theme::NV(RGB(110, 125, 145)));
            RECT t = rc; t.top += h / 2 - 8;
            DrawTextW(hdc, L"no motion source", -1, &t,
                      DT_CENTER | DT_SINGLELINE);
            return;
        }

        // Auto-scale to the window's own extent, floored so a still mount
        // does not get magnified into apparent chaos.
        float loX = m_pts[0].x, hiX = loX, loY = m_pts[0].y, hiY = loY;
        for (const auto& p : m_pts) {
            if (p.x < loX) loX = p.x;  if (p.x > hiX) hiX = p.x;
            if (p.y < loY) loY = p.y;  if (p.y > hiY) hiY = p.y;
        }
        const float midX = (loX + hiX) * 0.5f, midY = (loY + hiY) * 0.5f;
        float span = (hiX - loX) > (hiY - loY) ? (hiX - loX) : (hiY - loY);
        span *= 1.2f;                       // a little margin
        if (span < MIN_SPAN_AS) span = MIN_SPAN_AS;
        const int   plotH = h - 14;                  // leave a text strip
        const float cx = rc.left + w * 0.5f;
        const float cy = rc.top  + plotH * 0.5f;
        const float sc = (plotH < w ? plotH : w) * 0.45f / (span * 0.5f);

        // Crosshair at the MIDDLE OF THE TRAIL, not at the origin. It
        // used to be the origin, and the comment here still said so
        // after the scaling changed to frame the trail's bounding box —
        // so the line a reader would trust described a plot that had not
        // existed for two phases. What it marks now is the centre of
        // whatever range is on screen.
        HPEN axis = CreatePen(PS_SOLID, 1, Theme::NV(Theme::Rgb::BORDER));
        oldPen = SelectObject(hdc, axis);
        MoveToEx(hdc, rc.left + 2, (int)cy, nullptr);
        LineTo(hdc, rc.right - 2, (int)cy);
        MoveToEx(hdc, (int)cx, rc.top + 2, nullptr);
        LineTo(hdc, (int)cx, rc.top + plotH);
        SelectObject(hdc, oldPen);
        DeleteObject(axis);

        // Trail, oldest dim to newest bright, so direction of travel reads
        // without needing an arrowhead.
        const size_t n = m_pts.size();
        for (size_t i = 1; i < n; i++) {
            const float f = (float)i / (float)n;      // 0 old .. 1 new
            const int   g = 60 + (int)(150.0f * f);
            HPEN seg = CreatePen(PS_SOLID, 1, RGB(20 + g / 3, g, 60 + g / 3));
            oldPen = SelectObject(hdc, seg);
            MoveToEx(hdc, (int)(cx + (m_pts[i-1].x - midX) * sc),
                          (int)(cy - (m_pts[i-1].y - midY) * sc), nullptr);
            LineTo(hdc,   (int)(cx + (m_pts[i].x   - midX) * sc),
                          (int)(cy - (m_pts[i].y   - midY) * sc));
            SelectObject(hdc, oldPen);
            DeleteObject(seg);
        }

        // Current position
        const int px = (int)(cx + (m_pts.back().x - midX) * sc);
        const int py = (int)(cy - (m_pts.back().y - midY) * sc);
        HBRUSH dot = CreateSolidBrush(Theme::NV(RGB(120, 230, 170)));
        RECT d = { px - 2, py - 2, px + 3, py + 3 };
        FillRect(hdc, &d, dot);
        DeleteObject(dot);

        // Readout: source, live speed, and the plot's full width in arcsec,
        // so the trail has a scale rather than being merely suggestive.
        wchar_t line[96];
        const float sp = SpeedArcsecPerSec();
        swprintf(line, 96, L"%s  %.1f\"/s   ±%.0f\"",
                 m_src == MotionSource::Camera ? L"CAMERA" : L"MOUNT",
                 sp, span * 0.5f);
        SetTextColor(hdc, sp > 0.3f ? Theme::NV(RGB(120, 230, 170)) : Theme::NV(Theme::Rgb::TEXT_DIM));
        RECT tr = { rc.left + 3, rc.top + plotH, rc.right - 3, rc.bottom };
        DrawTextW(hdc, line, -1, &tr, DT_LEFT | DT_SINGLELINE);
    }

private:
    void Add(float x, float y, double t) {
        m_pts.push_back({x, y, t});
        while (!m_pts.empty() && (t - m_pts.front().t) > WINDOW_S)
            m_pts.pop_front();
        // Hard cap, in case timestamps ever go backwards.
        while (m_pts.size() > 600) m_pts.pop_front();
    }
    static double Dist(const Sample& a, const Sample& b) {
        const double dx = a.x - b.x, dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    std::deque<Sample> m_pts;
    MotionSource m_src        = MotionSource::None;
    bool         m_haveOrigin = false;
    double       m_ox = 0.0, m_oy = 0.0;
    double       m_lastCamera = -1e9;
};

} // namespace raduk::gui
