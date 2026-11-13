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
#include <cstdint>
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

// 139.2. Every move ends travelling the FINAL direction with the slack
// taken up: outward (rising step numbers, dir +1) by default, inward
// (dir -1) when rig.ini FocuserFinalDirection=in (phase 145.4). Which of
// the two pushes the 8SE's primary toward the corrector, against gravity,
// depends on how the EAF is mounted and its reverse setting, so the owner
// sets it once. A move is one leg when it already ends the final way with
// the slack taken up; otherwise it first goes a backlash past the target
// on the other side, and then comes back.
struct OneSidedApproach {
    int  backlash = 0;        // 0: off, every move in one leg
    int  dir      = 1;        // +1 finish outward, -1 finish inward
    bool loadedUp = false;    // the slack is taken up the final way right now

    // The first leg's target, or `to` when one leg will do.
    int ViaFor(int from, int to) const {
        if (backlash <= 0 || to == from) return to;
        const int d = (to - from) * (dir < 0 ? -1 : 1);
        if (d > 0 && (loadedUp || d >= backlash)) return to;
        return dir < 0 ? to + backlash : (std::max)(0, to - backlash);
    }
    // Record a leg that was commanded and completed.
    void Moved(int from, int to) {
        const int d = (to - from) * (dir < 0 ? -1 : 1);
        if (d > 0) { if (loadedUp || d >= backlash) loadedUp = true; }
        else if (d < 0) loadedUp = false;
    }
};

// The legs a focuser commands. Thread-safe: autofocus's thread and the
// UI both poll the focuser.
class ApproachSequencer {
public:
    void SetBacklash(int b) { std::lock_guard<std::mutex> lk(m_mx); m_a.backlash = (std::max)(0, b); }
    int  Backlash() const   { std::lock_guard<std::mutex> lk(m_mx); return m_a.backlash; }
    // Phase 145.4: +1 finish outward, -1 inward. A change forgets the slack.
    void SetDirection(int d) {
        std::lock_guard<std::mutex> lk(m_mx);
        const int nd = d < 0 ? -1 : 1;
        if (nd != m_a.dir) { m_a.dir = nd; m_a.loadedUp = false; }
    }
    int  Direction() const  { std::lock_guard<std::mutex> lk(m_mx); return m_a.dir; }

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

// ── Phase 145.3: the fine stage's arithmetic ────────────────────
//
// The coarse sweep (HFD) finds roughly where focus is and how wide its
// curve is; the fine pass then sweeps a narrow span on the edge metric,
// every point approached the final way, many frames a point.

// The value at fraction p (0..1) of the sorted scores: the 75th
// percentile is the focus as it looks in the steadier moments, which is
// what lucky imaging keeps. 0 for no scores.
inline float ScorePercentile(std::vector<float> v, float p) {
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    const float k = (std::max)(0.0f, (std::min)(1.0f, p)) * (float)(v.size() - 1);
    const size_t i = (size_t)k;
    if (i + 1 >= v.size()) return v.back();
    return v[i] + (v[i + 1] - v[i]) * (k - (float)i);
}

// The fine span: the coarse curve's width where it is within 10% of its
// peak, at least 2 backlashes and 120 steps (or 7 points sit inside the
// slack and seeing), at most two coarse steps (it was bracketed by them).
inline int FineSpanFrom(const std::vector<int>& pos, const std::vector<float>& score, int backlash) {
    const int n = (int)(std::min)(pos.size(), score.size());
    if (n < 2) return 0;
    int lo = pos[0], hi = pos[0];
    for (int i = 1; i < n; ++i) { lo = (std::min)(lo, pos[i]); hi = (std::max)(hi, pos[i]); }
    const int coarseStep = (hi - lo) / (n - 1);
    float pk = score[0];
    for (int i = 1; i < n; ++i) pk = (std::max)(pk, score[i]);
    int wlo = INT32_MAX, whi = INT32_MIN;
    for (int i = 0; i < n; ++i)
        if (score[i] >= 0.9f * pk) { wlo = (std::min)(wlo, pos[i]); whi = (std::max)(whi, pos[i]); }
    int span = (whi > wlo) ? whi - wlo : 2 * coarseStep;
    const int floorSpan = (std::max)(2 * backlash, 120);
    const int ceilSpan  = (std::max)(floorSpan, 2 * coarseStep);
    return (std::max)(floorSpan, (std::min)(span, ceilSpan));
}

// The fine pass's positions in the order they are visited: `points`
// across `span` centred on `center`, travelling the final way (rising for
// dir +1), kept within [minStep, maxStep].
inline std::vector<int> FineSweepPositions(int center, int span, int points, int dir,
                                           int minStep, int maxStep) {
    std::vector<int> out;
    if (points < 3 || span <= 0) return out;
    int lo = center - span / 2, hi = lo + span;
    if (lo < minStep) { hi += minStep - lo; lo = minStep; }
    if (hi > maxStep) { lo -= hi - maxStep; hi = maxStep; }
    lo = (std::max)(lo, minStep);
    for (int i = 0; i < points; ++i)
        out.push_back(lo + (int)std::lround((double)(hi - lo) * i / (points - 1)));
    if (dir < 0) std::reverse(out.begin(), out.end());
    return out;
}

// ── Phase 145.5: a fade during the fine pass ────────────────────
//
// The fine pass visits its points one way only, so a target dimming as
// it goes (dew, haze: 2026-11-08) lowers the later points and pulls the
// fit toward the first. The coarse sweep cancels that by averaging out
// and back; the fine pass instead scores its centre point again at the
// end, and the ratio of the two readings says how much the light moved.

// Second centre reading over the first; 1 when either is missing.
inline float FadeRatio(float firstCentre, float againCentre) {
    if (!(firstCentre > 0.0f) || !(againCentre > 0.0f)) return 1.0f;
    return againCentre / firstCentre;
}

enum class FadeAction { None, Correct, GiveUp };

// Within 10%: seeing alone does that. Under 60%: too much changed to fit.
inline FadeAction FadeVerdict(float ratio) {
    // UNVERIFIED(item 58): the fine-pass fade-check ratio on a clear, steady night
    if (ratio >= 0.9f && ratio <= 1.0f / 0.9f) return FadeAction::None;
    // UNVERIFIED(item 58): a real fade (dew or cloud) during a fine pass, and whether correcting helped
    if (ratio >= 0.6f && ratio <= 1.0f / 0.6f) return FadeAction::Correct;
    return FadeAction::GiveUp;
}

// Undo a fade taken as linear in time. The centre was read at tFirst and
// again at tEnd, `ratio` apart, so the light at time t relative to tFirst
// is 1 + (ratio - 1) * (t - tFirst) / (tEnd - tFirst); every score is
// brought to the light at tFirst.
inline std::vector<float> FadeCorrect(const std::vector<float>& score, const std::vector<double>& t,
                                      float ratio, double tFirst, double tEnd) {
    std::vector<float> out(score);
    if (!(tEnd > tFirst)) return out;
    for (size_t i = 0; i < out.size() && i < t.size(); ++i) {
        const double f = 1.0 + ((double)ratio - 1.0) * (t[i] - tFirst) / (tEnd - tFirst);
        if (f > 0.05) out[i] = (float)(out[i] / f);
    }
    return out;
}

// Which backlash figure to use until the sky measures one. rigSteps < 0
// means rig.ini does not set it.
inline int ResolveBacklash(int rigSteps, int firmwareSteps, const char** source) {
    if (rigSteps >= 0)     { if (source) *source = "rig.ini FocuserBacklashSteps"; return rigSteps; }
    if (firmwareSteps > 0) { if (source) *source = "the EAF's own setting"; return firmwareSteps; }
    if (source) *source = "the default 200, not measured";
    return 200;
}

}  // namespace raduk::core
