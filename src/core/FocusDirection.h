#pragma once
// ============================================================
//  FocusDirection.h -- phase 139.0 to 139.2: a step number that means
//  one focus position
//
//  The EAF counts motor steps, not mirror position. On the 8SE it turns
//  the stock knob, which slides the primary mirror, and reversing takes
//  up the knob's slack before the mirror moves. So a step number reached
//  moving outward (rising numbers) and the same number reached moving
//  inward are two focus positions, a backlash apart. The owner saw it:
//  returning to the step where focus was sharp gave a blur.
//
//  With a slack of B steps, an outward move leaves the mirror B/2 behind
//  the count and an inward one B/2 ahead. A sweep's outward curve peaks
//  at focus + B/2, its inward curve at focus - B/2, and the average of
//  the two (what autofocus fitted since F9) at about focus itself: the
//  wrong step for a final approach made outward, by B/2, and flatter.
//
//  Here, all pure:
//    PeakWindow3        the vertex through the best point and its two
//                       neighbours (what autofocus already fitted).
//    FitDirections      the outward, inward and averaged peaks, and
//                       their difference: the backlash, mirror included.
//    OneSidedApproach   every move ends travelling outward by at least
//                       the backlash, so the slack is always taken up the
//                       same way (139.2).
//    ApproachSequencer  the same, as the two legs a focuser commands.
//    ResolveBacklash    which figure to use, and says where it came from.
//
//  Gated by tests/af_direction_fit.cpp.
// ============================================================

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

namespace raduk::core {

struct CurvePeak {
    bool   ok        = false;   // a concave peak inside the sweep
    bool   bracketed = false;   // the best point is not at either end
    double pos       = 0.0;     // the vertex, in steps
    double height    = 0.0;     // the best measured score
};

// Parabola through the best point and its neighbours, by divided
// differences (positions may be unevenly spaced).
inline CurvePeak PeakWindow3(const std::vector<int>& pos, const std::vector<float>& score) {
    CurvePeak r;
    const int n = (int)(std::min)(pos.size(), score.size());
    if (n < 3) return r;
    int pk = 0;
    for (int i = 1; i < n; i++) if (score[i] > score[pk]) pk = i;
    r.height = score[pk];
    if (pk == 0 || pk == n - 1) return r;
    r.bracketed = true;
    const double x1 = pos[pk-1], y1 = score[pk-1];
    const double x2 = pos[pk],   y2 = score[pk];
    const double x3 = pos[pk+1], y3 = score[pk+1];
    if (x2 - x1 < 1.0 || x3 - x2 < 1.0) return r;
    const double d1 = (y2 - y1) / (x2 - x1);
    const double d2 = (y3 - y2) / (x3 - x2);
    const double a  = (d2 - d1) / (x3 - x1);
    if (a >= -1e-12) return r;
    const double b = d1 - a * (x1 + x2);
    const double v = -b / (2.0 * a);
    if (v < x1 || v > x3) return r;
    r.ok = true;
    r.pos = v;
    return r;
}

struct DirectionFit {
    CurvePeak out, back, mean;
    bool   haveBacklash = false;
    double backlash     = 0.0;   // out peak minus back peak, in steps
};

// outScore[i] and backScore[i] were measured at pos[i], moving outward
// and inward. A score <= 0 means nothing was measured there.
inline DirectionFit FitDirections(const std::vector<int>& pos,
                                  const std::vector<float>& outScore,
                                  const std::vector<float>& backScore) {
    DirectionFit f;
    f.out  = PeakWindow3(pos, outScore);
    f.back = PeakWindow3(pos, backScore);
    std::vector<float> m(pos.size(), 0.0f);
    for (size_t i = 0; i < pos.size(); i++) {
        const float a = i < outScore.size() ? outScore[i] : 0.0f;
        const float b = i < backScore.size() ? backScore[i] : 0.0f;
        const int k = (a > 0) + (b > 0);
        m[i] = k ? (std::max(a, 0.0f) + std::max(b, 0.0f)) / k : 0.0f;
    }
    f.mean = PeakWindow3(pos, m);
    if (f.out.ok && f.back.ok) { f.haveBacklash = true; f.backlash = f.out.pos - f.back.pos; }
    return f;
}

// 139.2. Outward is rising step numbers, the direction autofocus makes
// its final approach in. A move is made in one leg when it already ends
// outward with the slack taken up; otherwise it first goes to
// target - backlash and then comes up.
struct OneSidedApproach {
    int  backlash = 0;        // 0: off, every move in one leg
    bool loadedUp = false;    // the slack is taken up outward right now

    // The first leg's target, or `to` when one leg will do.
    int ViaFor(int from, int to) const {
        if (backlash <= 0 || to == from) return to;
        if (to > from && (loadedUp || to - from >= backlash)) return to;
        return (std::max)(0, to - backlash);
    }
    // Record a leg that was commanded and completed.
    void Moved(int from, int to) {
        if (to > from) { if (loadedUp || to - from >= backlash) loadedUp = true; }
        else if (to < from) loadedUp = false;
    }
};

// The legs a focuser commands. Thread-safe: autofocus's thread and the
// UI both poll the focuser.
class ApproachSequencer {
public:
    void SetBacklash(int b) { std::lock_guard<std::mutex> lk(m_mx); m_a.backlash = (std::max)(0, b); }
    int  Backlash() const   { std::lock_guard<std::mutex> lk(m_mx); return m_a.backlash; }

    // A one-sided move from `from` to `to`: returns the leg to command now.
    int Begin(int from, int to) {
        std::lock_guard<std::mutex> lk(m_mx);
        const int via = m_a.ViaFor(from, to);
        m_pending = (via != to) ? to : -1;
        m_a.Moved(from, via);
        return via;
    }
    // A plain move (autofocus's sweep): no second leg.
    void Direct(int from, int to) {
        std::lock_guard<std::mutex> lk(m_mx);
        m_pending = -1;
        m_a.Moved(from, to);
    }
    // The focuser has stopped at `at`: the next leg to command, or -1.
    int NextLeg(int at) {
        std::lock_guard<std::mutex> lk(m_mx);
        if (m_pending < 0) return -1;
        const int t = m_pending;
        m_pending = -1;
        m_a.Moved(at, t);
        return t;
    }
    // Stopped part way: nothing pending, and the slack's state unknown.
    void Cancel() {
        std::lock_guard<std::mutex> lk(m_mx);
        m_pending = -1;
        m_a.loadedUp = false;
    }
    bool Pending() const { std::lock_guard<std::mutex> lk(m_mx); return m_pending >= 0; }

private:
    mutable std::mutex m_mx;
    OneSidedApproach   m_a;
    int                m_pending = -1;
};

// Which backlash figure to use until the sky measures one. rigSteps < 0
// means rig.ini does not set it.
inline int ResolveBacklash(int rigSteps, int firmwareSteps, const char** source) {
    if (rigSteps >= 0)     { if (source) *source = "rig.ini FocuserBacklashSteps"; return rigSteps; }
    if (firmwareSteps > 0) { if (source) *source = "the EAF's own setting"; return firmwareSteps; }
    if (source) *source = "the default 200, not measured";
    return 200;
}

}  // namespace raduk::core
