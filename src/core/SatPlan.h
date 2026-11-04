#pragma once
// ============================================================
// SatPlan.h — Phase 118: a night of satellites
//
// A planet is a slew and a stay. A satellite is gone in six minutes, so
// a session is a queue: from now on, this one, then that one, with the
// telescope going to meet each in turn. This chooses the queue and runs
// it.
//
// ── Choosing ────────────────────────────────────────────────
//
// The hard part is not overlap but the gap. The tube ends one pass
// pointing wherever that pass ended, and has to reach the start of the
// next, settle, and be ready before the window opens. Two passes that do
// not overlap can still be impossible back to back.
//
// Whether one pass can follow another depends only on those two, so
// this is a weighted scheduling problem with an exact answer: walk the
// passes in order, and for each ask what the best plan ending here is.
// A greedy "next good one" would take a low pass and thereby lose the
// fine one starting ninety seconds later.
//
// ── Running ─────────────────────────────────────────────────
//
// Pure, as MosaicRunner is. The rule that matters is what a failure
// means. A pass that could not be caught costs that pass, and the plan
// goes on. A mount that ran backwards, stopped answering or left the
// altitude band is a fault in the telescope, and the next pass would
// meet it again unattended, so the plan stops. So does anything a person
// did: Escape means stop, not skip.
// ============================================================

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "../sky/SatPass.h"

namespace raduk::core {

struct SatPassRow {
    std::string name;
    raduk::sky::Tle tle;
    raduk::sky::SatPassInfo pass;
};

struct SatPlanOptions {
    double slewDegPerSec = 2.5;   // under the top rate: GoTo slows to land
    double settleSec     = 15.0;
    double marginSec     = 20.0;  // spare, because a slew can run long
    bool   visibleOnly   = false; // lit, against a dark sky
};

// Seconds to get from the end of one window to the start of another.
inline double SatChangeoverSec(double fromAlt, double fromAz,
                               double toAlt, double toAz,
                               const SatPlanOptions& o) {
    const double dAz  = std::fabs(raduk::sky::WrapDeg180(toAz - fromAz));
    const double dAlt = std::fabs(toAlt - fromAlt);
    // The axes move together, so the longer one sets the time.
    return (std::max)(dAz, dAlt) / o.slewDegPerSec + o.settleSec + o.marginSec;
}

inline bool SatCanFollow(const SatPassRow& a, const SatPassRow& b,
                         const SatPlanOptions& o) {
    const double need = SatChangeoverSec(a.pass.endAlt, a.pass.endAz,
                                         b.pass.startAlt, b.pass.startAz, o);
    return (b.pass.trackStartJd - a.pass.trackEndJd) * 86400.0 >= need;
}

// What a pass is worth. Being in the plan counts most, so more targets
// beats fewer; then height, because that is range and air; then being
// lit, because an unlit satellite is a black dot against daylight or
// nothing at all. A second of mount saturation costs.
inline double SatPassScore(const raduk::sky::SatPassInfo& p) {
    return 1.0 + p.peakAlt / 90.0 + (p.visible ? 0.5 : 0.0)
         - (std::min)(p.saturatedSec, 30.0) / 60.0;
}

// The best queue from rows, as indices in time order. startAlt and
// startAz are where the tube is now.
inline std::vector<int> BuildSatPlan(const std::vector<SatPassRow>& rows,
                                     double nowJd, double startAlt,
                                     double startAz, const SatPlanOptions& o) {
    std::vector<int> c;
    for (int i = 0; i < (int)rows.size(); ++i) {
        const auto& p = rows[i].pass;
        if (!p.trackable) continue;
        if (o.visibleOnly && !p.visible) continue;
        const double lead = (p.trackStartJd - nowJd) * 86400.0;
        if (lead < SatChangeoverSec(startAlt, startAz, p.startAlt, p.startAz, o))
            continue;
        c.push_back(i);
    }
    std::sort(c.begin(), c.end(), [&](int a, int b) {
        return rows[a].pass.trackStartJd < rows[b].pass.trackStartJd;
    });

    std::vector<double> best(c.size(), 0.0);
    std::vector<int> prev(c.size(), -1);
    int top = -1;
    for (size_t i = 0; i < c.size(); ++i) {
        const auto& r = rows[c[i]];
        best[i] = SatPassScore(r.pass);
        for (size_t j = 0; j < i; ++j) {
            if (!SatCanFollow(rows[c[j]], r, o)) continue;
            const double v = best[j] + SatPassScore(r.pass);
            if (v > best[i]) { best[i] = v; prev[i] = (int)j; }
        }
        if (top < 0 || best[i] > best[top]) top = (int)i;
    }

    std::vector<int> out;
    for (int i = top; i >= 0; i = prev[i]) out.push_back(c[i]);
    std::reverse(out.begin(), out.end());
    return out;
}

// For a queue a person has edited: the first pair that cannot work,
// as the index of the later one, or -1.
inline int FirstSatConflict(const std::vector<SatPassRow>& rows,
                            const std::vector<int>& plan,
                            const SatPlanOptions& o) {
    for (size_t i = 1; i < plan.size(); ++i)
        if (!SatCanFollow(rows[plan[i - 1]], rows[plan[i]], o)) return (int)i;
    return -1;
}

// ── Running it ───────────────────────────────────────────────
enum class SatOutcome {
    None,
    Complete,
    Lost,      // this pass is gone; the telescope is fine
    Stopped,   // a person
    Fault,     // the telescope
};

class SatPlanRunner {
public:
    struct Step {
        enum Kind { Wait, Start, Finished } kind = Wait;
        int index = -1;           // into the rows
    };

    void Begin(const std::vector<SatPassRow>& rows, const std::vector<int>& plan,
               const SatPlanOptions& o) {
        m_items.clear();
        for (int i : plan) m_items.push_back({i, rows[i].pass.trackStartJd,
                                              rows[i].pass.trackEndJd,
                                              rows[i].pass.startAlt,
                                              rows[i].pass.startAz});
        m_opt = o;
        m_at = 0;
        m_done = m_missed = 0;
        m_flying = false;
        m_running = !m_items.empty();
        m_why.clear();
    }

    // A person, or anything else that ends the night.
    void Abort(const char* why) {
        if (!m_running) return;
        m_running = false;
        m_why = why ? why : "stopped";
    }

    // alt and az are where the tube is. flying says a pass is in hand;
    // outcome is how the last one ended, once.
    Step Tick(double nowJd, double alt, double az, bool flying,
              SatOutcome outcome) {
        Step s;
        if (!m_running) return s;

        if (m_flying && !flying) {
            m_flying = false;
            switch (outcome) {
            case SatOutcome::Complete: ++m_done; break;
            case SatOutcome::Stopped:
                m_running = false;
                m_why = "stopped";
                s.kind = Step::Finished;
                return s;
            case SatOutcome::Fault:
                m_running = false;
                m_why = "the mount had a fault";
                s.kind = Step::Finished;
                return s;
            default: ++m_missed; break;
            }
            ++m_at;
        }
        if (flying) return s;

        while (m_at < (int)m_items.size()) {
            const Item& it = m_items[m_at];
            const double lead = (it.startJd - nowJd) * 86400.0;
            const double need = SatChangeoverSec(alt, az, it.alt, it.az, m_opt);
            if (lead < need * 0.5 && lead < 30.0) {
                // Too late to do it properly and not worth a dash.
                ++m_missed;
                ++m_at;
                continue;
            }
            if (lead > need + kEarlySec) return s;
            s.kind = Step::Start;
            s.index = it.row;
            m_flying = true;
            return s;
        }
        m_running = false;
        s.kind = Step::Finished;
        return s;
    }

    // The start was refused, by the cord or anything else. It is one
    // pass, not the night: a later one may be fine.
    void Refused() {
        m_flying = false;
        ++m_missed;
        ++m_at;
    }

    bool Running() const { return m_running; }
    int  Total()   const { return (int)m_items.size(); }
    int  Captured() const { return m_done; }
    int  Missed()  const { return m_missed; }
    int  Position() const { return m_at; }
    const std::string& Why() const { return m_why; }

    // Time in hand: a tube arriving early waits, and that beats landing
    // with a minute of ringing still to come.
    static constexpr double kEarlySec = 45.0;

private:
    struct Item { int row; double startJd, endJd, alt, az; };
    std::vector<Item> m_items;
    SatPlanOptions m_opt;
    int  m_at = 0, m_done = 0, m_missed = 0;
    bool m_running = false, m_flying = false;
    std::string m_why;
};

inline constexpr bool SAT_PLAN_PROVEN_ON_SKY = false;

} // namespace raduk::core
