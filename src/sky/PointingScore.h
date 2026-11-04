// ============================================================
//  PointingScore — does the pointing map help, measured
//
//  Phase 123, the correcting half of 6.6, and the half that was never
//  built because nobody could say which correction was right. The map
//  records, at every lock, how far the mount's reported position was
//  from the plate-solved truth, and then the app syncs. Two questions
//  were left open in GoToPanel's own comment: does a sync spend the
//  data, and should the correction come from every night or tonight?
//
//  Both are measurements, and every solve is one. Before a new error is
//  recorded, the map is asked to predict it from what came before. The
//  error a GoTo to that spot would have had is exactly what was just
//  measured, so the prediction is graded against the truth: if the map
//  had been applied, this is what would have been left. Kept as running
//  RMS for no correction, a map from all nights, and a map from
//  tonight, the numbers answer the question that code could not.
//
//  ── Applying one ─────────────────────────────────────────────
//
//  The error is truth minus reported. A GoTo drives until the REPORTED
//  position is the target, which leaves the telescope at target plus
//  error. So the command is target MINUS the predicted error. The
//  dormant ApplyPointingCorrection added it, which would have doubled
//  every error it touched.
//
//  Errors are kept as arcseconds east and north on the sky, not as raw
//  RA hours: an RA difference means four times as much sky at the
//  equator as at 75 degrees, and averaging raw hours across
//  declinations mixes units.
// ============================================================

#pragma once

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace raduk::sky {

struct PointingObs {
    double t = 0.0;            // seconds, any epoch; nights split by kNightGapSec
    double azDeg = 0.0, altDeg = 0.0;
    double eastArcsec = 0.0;   // truth minus reported, on the sky
    double northArcsec = 0.0;
};

// Truth minus reported, as sky arcseconds at the reported position.
inline void SkyErrorArcsec(double raRepH, double decRepDeg, double raTrueH, double decTrueDeg,
                           double& east, double& north) {
    double dRa = (raTrueH - raRepH) * 15.0;
    if (dRa > 180.0) dRa -= 360.0;
    if (dRa < -180.0) dRa += 360.0;
    east  = dRa * std::cos(decRepDeg * 0.017453292519943295) * 3600.0;
    north = (decTrueDeg - decRepDeg) * 3600.0;
}

// The GoTo command that puts the telescope ON the target, given the
// predicted error there: target minus error.
inline void CorrectedCommand(double raH, double decDeg, double eastArcsec, double northArcsec,
                             double& cmdRaH, double& cmdDecDeg) {
    double c = std::cos(decDeg * 0.017453292519943295);
    if (c < 0.01) c = 0.01;
    cmdRaH = raH - eastArcsec / 3600.0 / c / 15.0;
    while (cmdRaH < 0.0) cmdRaH += 24.0;
    while (cmdRaH >= 24.0) cmdRaH -= 24.0;
    cmdDecDeg = decDeg - northArcsec / 3600.0;
}

class PointingScore {
public:
    enum Source { All = 0, Tonight = 1 };
    // A night is a run of solves with no gap longer than this.
    static constexpr double kNightGapSec = 6.0 * 3600.0;
    static constexpr double kRadiusDeg   = 20.0;
    static constexpr int    kMinNearby   = 2;
    // What it takes to recommend applying a map: enough graded solves,
    // and a real cut in RMS rather than noise.
    static constexpr int    kMinScored   = 8;
    static constexpr double kMinCut      = 0.30;

    void Load(const std::vector<PointingObs>& history) { m_obs = history; }
    const std::vector<PointingObs>& History() const { return m_obs; }

    // Predict the error at az/alt from what is held. Inverse distance
    // squared over everything within kRadiusDeg, angular distance on
    // the sphere, so azimuth wraps and squeezes toward the zenith.
    bool Predict(double azDeg, double altDeg, double now, Source src,
                 double& east, double& north) const {
        const double nightStart = NightStart(now);
        double sw = 0, se = 0, sn = 0;
        int n = 0;
        for (const PointingObs& o : m_obs) {
            if (src == Tonight && o.t < nightStart) continue;
            const double d = AngleDeg(azDeg, altDeg, o.azDeg, o.altDeg);
            if (d > kRadiusDeg) continue;
            const double w = 1.0 / ((d < 0.5 ? 0.5 : d) * (d < 0.5 ? 0.5 : d));
            sw += w; se += w * o.eastArcsec; sn += w * o.northArcsec;
            ++n;
        }
        if (n < kMinNearby) return false;
        east = se / sw;
        north = sn / sw;
        return true;
    }

    // Grade the map against a new measurement, then keep it. Returns a
    // line for the log.
    std::string Observe(const PointingObs& o) {
        const double raw = o.eastArcsec * o.eastArcsec + o.northArcsec * o.northArcsec;
        char b[220];
        int k = snprintf(b, sizeof(b), "Pointing: off by %.0f\"", std::sqrt(raw));
        for (int s = 0; s < 2; ++s) {
            double e = 0, nn = 0;
            if (!Predict(o.azDeg, o.altDeg, o.t, (Source)s, e, nn)) continue;
            const double de = o.eastArcsec - e, dn = o.northArcsec - nn;
            const double left = de * de + dn * dn;
            m_raw[s] += raw;
            m_left[s] += left;
            ++m_n[s];
            k += snprintf(b + k, sizeof(b) - k, "; %s map would have left %.0f\"",
                          s == All ? "all-nights" : "tonight's", std::sqrt(left));
        }
        m_obs.push_back(o);
        return std::string(b, k > 0 ? (size_t)k : 0);
    }

    int    Scored(Source s) const { return m_n[s]; }
    double RmsRaw(Source s) const  { return m_n[s] ? std::sqrt(m_raw[s] / m_n[s]) : 0.0; }
    double RmsLeft(Source s) const { return m_n[s] ? std::sqrt(m_left[s] / m_n[s]) : 0.0; }
    // Fraction of the RMS error a map takes away. Negative makes it worse.
    double Cut(Source s) const {
        const double r = RmsRaw(s);
        return r > 0.0 ? 1.0 - RmsLeft(s) / r : 0.0;
    }

    // The source worth applying, or -1 for none yet.
    int Recommended() const {
        int best = -1;
        double bestCut = kMinCut;
        for (int s = 0; s < 2; ++s)
            if (m_n[s] >= kMinScored && Cut((Source)s) >= bestCut) {
                bestCut = Cut((Source)s);
                best = s;
            }
        return best;
    }

    std::string Verdict() const {
        char b[260];
        const int r = Recommended();
        if (r >= 0) {
            snprintf(b, sizeof(b), "Pointing map: %s cuts the error by %.0f%% over %d solves "
                     "(%.0f\" to %.0f\"). Set PointingMapApply=%d to use it.",
                     r == All ? "all nights" : "tonight's", Cut((Source)r) * 100.0,
                     m_n[r], RmsRaw((Source)r), RmsLeft((Source)r), r + 1);
        } else if (m_n[All] + m_n[Tonight] == 0) {
            snprintf(b, sizeof(b), "Pointing map: nothing graded yet; each plate solve adds one.");
        } else {
            snprintf(b, sizeof(b), "Pointing map: not shown to help yet (all nights %+.0f%% over %d, "
                     "tonight %+.0f%% over %d). Needs %d graded and a %.0f%% cut.",
                     -Cut(All) * 100.0, m_n[All], -Cut(Tonight) * 100.0, m_n[Tonight],
                     kMinScored, kMinCut * 100.0);
        }
        return b;
    }

private:
    double NightStart(double now) const {
        // Walk back through the held solves while the gaps are short.
        double start = now;
        for (int i = (int)m_obs.size() - 1; i >= 0; --i) {
            if (start - m_obs[i].t > kNightGapSec) break;
            start = m_obs[i].t;
        }
        return start;
    }

    static double AngleDeg(double az1, double alt1, double az2, double alt2) {
        const double D = 0.017453292519943295;
        const double c = std::sin(alt1 * D) * std::sin(alt2 * D) +
                         std::cos(alt1 * D) * std::cos(alt2 * D) * std::cos((az1 - az2) * D);
        return std::acos(c > 1.0 ? 1.0 : (c < -1.0 ? -1.0 : c)) / D;
    }

    std::vector<PointingObs> m_obs;
    double m_raw[2] = {0, 0}, m_left[2] = {0, 0};
    int    m_n[2] = {0, 0};
};

// Graded against simulated mounts. What a real 8SE's error field looks
// like after a sync is exactly what this is built to find out.
inline constexpr bool POINTING_MAP_PROVEN_ON_SKY = false;

} // namespace raduk::sky
