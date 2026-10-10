#pragma once
// ============================================================
// CordWrap.h — Phase 67: how far has the mount actually turned?
//
// An alt-az fork rotates in azimuth with the USB3 camera cable, the
// EAF cable, power and serial all hanging off the moving part.
// Enough rotation in one direction strains connectors and can pull
// the camera out mid-capture. Nothing tracked this before.
//
// The quantity that matters is CUMULATIVE TRAVEL, not azimuth. 350
// and 10 degrees are adjacent on a compass but 340 degrees apart in
// travel if the mount went the long way round. A cable does not care
// where the tube points; it cares how many turns have accumulated
// since it was last dressed.
//
// A useful consequence: this needs no alignment. Absolute azimuth is
// meaningless until the mount is aligned, but the DIFFERENCE between
// consecutive readings is valid from the moment it connects — which
// is exactly when people slew around hunting for a target.
// ============================================================

#include <cmath>
#include <cstdio>
#include "SkyMath.h"   // ShortestTurnDeg

namespace raduk::core {

class CordWrapTracker {
public:
    // Degrees of travel in one direction before the caller should warn.
    // Overridden from RigConfig at construction by the owner.
    static constexpr double DEFAULT_LIMIT_DEG = 360.0;

    // A jump larger than this between samples is treated as a GoTo
    // slew or a lost connection rather than continuous motion, and is
    // not accumulated. Polls arrive every couple of seconds; even a
    // fast 4 deg/s slew covers under 10 deg in that time, so anything
    // approaching a half turn means the samples are not continuous.
    static constexpr double MAX_STEP_DEG = 90.0;

    void SetLimit(double deg) { if (deg > 10.0) m_limitDeg = deg; }
    double Limit() const      { return m_limitDeg; }

    // Feed every azimuth reading. Out-of-range values are ignored.
    void Sample(double azDeg) {
        if (azDeg < 0.0 || azDeg > 360.0) return;
        if (!m_have) { m_lastAz = azDeg; m_have = true; return; }

        // Shortest signed path between two compass bearings: the mount
        // went the short way unless it was slewing, and a slew is
        // excluded below.
        double d = azDeg - m_lastAz;
        while (d >  180.0) d -= 360.0;
        while (d < -180.0) d += 360.0;

        if (std::fabs(d) > MAX_STEP_DEG) {
            // Discontinuity: a GoTo, a reconnect, or a stalled poll.
            // Re-seed rather than charge the whole jump to the cable,
            // which would make the total meaningless within minutes.
            m_lastAz = azDeg;
            m_skipped++;
            return;
        }
        m_cumulativeDeg += d;
        m_lastAz = azDeg;
    }

    // Signed: positive is one direction, negative the other. The sign
    // matters — 180 one way then 180 back leaves the cable slack, and
    // a model that summed magnitudes would wrongly report a full turn.
    double CumulativeDeg() const { return m_cumulativeDeg; }
    double AbsCumulativeDeg() const {
        return m_cumulativeDeg < 0 ? -m_cumulativeDeg : m_cumulativeDeg;
    }

    bool OverLimit() const { return AbsCumulativeDeg() >= m_limitDeg; }

    // Fraction of the limit used, clamped to [0,1] for a gauge.
    double Fraction() const {
        double f = AbsCumulativeDeg() / (m_limitDeg > 0 ? m_limitDeg : 1.0);
        return f < 0 ? 0 : (f > 1 ? 1 : f);
    }

    // Below this the readout is noise and should stay hidden rather
    // than adding a number to the panel that never means anything.
    bool WorthShowing() const { return AbsCumulativeDeg() >= 30.0; }

    // Call when the cables have been physically freed. This is the
    // only thing that legitimately zeroes the count — it cannot be
    // inferred, because the app has no way to see a cable.
    void CablesDressed() {
        printf("[CordWrap] Reset - was %+.0f deg (%d discontinuities ignored)\n",
               m_cumulativeDeg, m_skipped);
        m_cumulativeDeg = 0.0;
        m_skipped       = 0;
        m_have          = false;
    }

    // Last azimuth seen, or -1 before the first sample.
    double LastAz() const { return m_have ? m_lastAz : -1.0; }

    // -- Phase 67 step 5: what a slew would cost --------------
    struct SlewForecast {
        bool   usable        = false;  // false if the current az is unknown
        double targetAzDeg   = 0.0;
        double turnDeg       = 0.0;    // shortest signed path to get there
        double predictedDeg  = 0.0;    // cumulative travel afterwards
        bool   wouldExceed   = false;
        bool   alreadyOver   = false;  // over the limit before slewing
        double unwindDeg     = 0.0;    // travel to shed to get back inside
    };

    // The mount picks its own path for a GoTo -- the protocol sends
    // coordinates, not a direction -- so this assumes the shortest
    // turn. That is the optimistic case: if even the short way breaks
    // the limit, the long way certainly does, which makes a warning
    // based on it trustworthy in the direction that matters.
    SlewForecast ForecastSlewTo(double targetAzDeg) const {
        SlewForecast f;
        if (!m_have) return f;              // nothing to measure from
        f.usable       = true;
        f.targetAzDeg  = targetAzDeg;
        f.turnDeg      = ShortestTurnDeg(m_lastAz, targetAzDeg);
        f.predictedDeg = m_cumulativeDeg + f.turnDeg;
        double after   = f.predictedDeg < 0 ? -f.predictedDeg : f.predictedDeg;
        f.alreadyOver  = OverLimit();
        f.wouldExceed  = after >= m_limitDeg;
        f.unwindDeg    = after > m_limitDeg ? (after - m_limitDeg) : 0.0;
        return f;
    }

    int SkippedJumps() const { return m_skipped; }

private:
    double m_cumulativeDeg = 0.0;
    double m_lastAz        = 0.0;
    double m_limitDeg      = DEFAULT_LIMIT_DEG;
    bool   m_have          = false;
    int    m_skipped       = 0;
};

} // namespace raduk::core
