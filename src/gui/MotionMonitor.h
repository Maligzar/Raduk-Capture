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
        double path = 0.0, t0 = tEnd;
        for (size_t i = m_pts.size() - 1; i > 0; i--) {
            if (tEnd - m_pts[i - 1].t > 1.0) break;
            path += Dist(m_pts[i], m_pts[i - 1]);
            t0 = m_pts[i - 1].t;
        }
        const double dt = tEnd - t0;
        return dt > 1e-3 ? (float)(path / dt) : 0.0f;
    }

    void Draw(HDC hdc, const RECT& rc) const {
        const int w = rc.right - rc.left, h = rc.bottom - rc.top;
        if (w < 40 || h < 30) return;

        HBRUSH bg = CreateSolidBrush(RGB(10, 16, 26));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);
        HPEN border = CreatePen(PS_SOLID, 1, RGB(40, 54, 74));
        HGDIOBJ oldPen = SelectObject(hdc, border);
        HGDIOBJ oldBr  = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(hdc, oldBr);
        SelectObject(hdc, oldPen);
        DeleteObject(border);

        SetBkMode(hdc, TRANSPARENT);

        if (m_src == MotionSource::None || m_pts.empty()) {
            SetTextColor(hdc, RGB(110, 125, 145));
            RECT t = rc; t.top += h / 2 - 8;
            DrawTextW(hdc, L"no motion source", -1, &t,
                      DT_CENTER | DT_SINGLELINE);
            return;
        }

        // Auto-scale to the window's own extent, floored so a still mount
        // does not get magnified into apparent chaos.
        float span = MIN_SPAN_AS;
        for (const auto& p : m_pts) {
            span = (std::fabs(p.x) * 2.2f > span) ? std::fabs(p.x) * 2.2f : span;
            span = (std::fabs(p.y) * 2.2f > span) ? std::fabs(p.y) * 2.2f : span;
        }
        const int   plotH = h - 14;                  // leave a text strip
        const float cx = rc.left + w * 0.5f;
        const float cy = rc.top  + plotH * 0.5f;
        const float sc = (plotH < w ? plotH : w) * 0.45f / (span * 0.5f);

        // Crosshair at the origin — where things were when this view started
        HPEN axis = CreatePen(PS_SOLID, 1, RGB(34, 46, 64));
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
            MoveToEx(hdc, (int)(cx + m_pts[i-1].x * sc),
                          (int)(cy - m_pts[i-1].y * sc), nullptr);
            LineTo(hdc,   (int)(cx + m_pts[i].x   * sc),
                          (int)(cy - m_pts[i].y   * sc));
            SelectObject(hdc, oldPen);
            DeleteObject(seg);
        }

        // Current position
        const int px = (int)(cx + m_pts.back().x * sc);
        const int py = (int)(cy - m_pts.back().y * sc);
        HBRUSH dot = CreateSolidBrush(RGB(120, 230, 170));
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
        SetTextColor(hdc, sp > 0.3f ? RGB(120, 230, 170) : RGB(120, 136, 156));
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
