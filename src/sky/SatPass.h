// ============================================================
//  SatPass — when a satellite crosses this sky, and whether this
//  mount can follow it
//
//  Phase 116. Sgp4.h says where the thing is; this says what that
//  means from a tripod.
//
//  ── What a pass asks of an alt-az mount ─────────────────────
//
//  The sky rate of the ISS peaks near 1.2 deg/s overhead, well inside
//  what the 8SE can slew. The AZIMUTH rate does not stop there: it goes
//  as sky rate / cos(altitude), so a pass through 85 deg wants 10 deg/s
//  and one through the zenith wants infinity. That is the keyhole, and
//  it is why a pass is reported with its peak azimuth rate and not just
//  its height. The tube also cannot go everywhere, so the window to
//  track is cut at a floor and a ceiling.
//
//  ── What it asks of a sensor ────────────────────────────────
//
//  At 1 deg/s and 0.203"/px the ISS crosses 17,700 px/s. Unguided, even
//  a 0.1 ms exposure smears 1.8 px. So what matters is not the sky rate
//  but how well the mount holds it, and the arithmetic below says so.
//
//  ── Elements age fast ───────────────────────────────────────
//
//  Low orbits are dragged. A day-old ISS element set is a kilometre or
//  two out along track, which at 500 km is 4' to 8': the height of
//  this frame. Most of that error is TIMING, so a stale set still
//  gives the right path, late or early.
//
//  Times are UT Julian dates. Az from north through east.
// ============================================================

#pragma once

#include "LunarGeometry.h"
#include "Sgp4.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace raduk::sky {

// ── A clock good enough to point with ────────────────────────
//
// CurrentJD reads time() and so has one-second steps. That is nothing
// for a planet, and up to a degree of ISS. Milliseconds, from the
// system clock. Its accuracy is the PC's: one second wrong is seven
// kilometres of orbit.
inline double PreciseJdNow() {
    using namespace std::chrono;
    const double ms = (double)duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
    return 2440587.5 + ms / 86400000.0;
}

// ── One look ─────────────────────────────────────────────────
struct SatLook {
    bool   ok = false;
    double altDeg = 0.0, azDeg = 0.0;
    double rangeKm = 0.0;
    double raHours = 0.0, decDeg = 0.0;   // J2000, topocentric
    bool   sunlit = false;
    double sunAltDeg = 0.0;               // at the observer
};

inline LVec SunDirectionOfDate(double jdUt) {
    const double td = JdTdFromUt(jdUt);
    const double l = DegToRad(SunEclipticLonDeg(td));
    return RotX(LVec{std::cos(l), std::sin(l), 0.0}, MeanObliquityRad(td));
}

inline void LocalHorizon(const LVec& v, double jdUt, const LunarObserver& o,
                         double& altDeg, double& azDeg) {
    const double phi = DegToRad(o.latDeg);
    const double th  = DegToRad(GreenwichMeanSiderealDeg(jdUt) + o.lonDeg);
    const double sp = std::sin(phi), cp = std::cos(phi);
    const double st = std::sin(th),  ct = std::cos(th);
    const double south = sp * ct * v.x + sp * st * v.y - cp * v.z;
    const double east  = -st * v.x + ct * v.y;
    const double up    = cp * ct * v.x + cp * st * v.y + sp * v.z;
    const double r = std::sqrt(south * south + east * east + up * up);
    altDeg = SafeAsinDeg(up / r);
    azDeg  = NormDeg360(std::atan2(east, -south) / DegToRad(1.0));
}

inline SatLook SatLookAt(const Sgp4& s, double jdUt, const LunarObserver& o) {
    SatLook l;
    const Sgp4State st = s.AtJd(jdUt);
    if (!st.ok) return l;
    const LVec r{st.r[0], st.r[1], st.r[2]};
    const LVec rel = r - ObserverVectorKm(o, jdUt);
    LocalHorizon(rel, jdUt, o, l.altDeg, l.azDeg);
    l.rangeKm = rel.Len();
    VecToRaDec(DateToJ2000(rel, JdTdFromUt(jdUt)), l.raHours, l.decDeg);

    // A cylinder of shadow. The penumbra lasts seconds on a low orbit
    // and this answers "will it be lit", not "how bright".
    const LVec sun = SunDirectionOfDate(jdUt);
    const double along = r.Dot(sun);
    const double off = (r - sun * along).Len();
    l.sunlit = along > 0.0 || off > 6378.137;

    double az = 0.0;
    LocalHorizon(sun, jdUt, o, l.sunAltDeg, az);
    l.ok = true;
    return l;
}

// ── What the mount has to do at an instant ───────────────────
struct SatRates {
    bool   ok = false;
    double altDeg = 0.0, azDeg = 0.0;
    double altRate = 0.0, azRate = 0.0;      // deg/s, signed
};

inline double WrapDeg180(double d) {
    d = std::fmod(d, 360.0);
    if (d > 180.0)  d -= 360.0;
    if (d < -180.0) d += 360.0;
    return d;
}

inline SatRates SatRatesAt(const Sgp4& s, double jdUt, const LunarObserver& o) {
    SatRates r;
    const double h = 0.25 / 86400.0;
    const SatLook a = SatLookAt(s, jdUt - h, o);
    const SatLook b = SatLookAt(s, jdUt + h, o);
    const SatLook c = SatLookAt(s, jdUt, o);
    if (!a.ok || !b.ok || !c.ok) return r;
    r.altDeg  = c.altDeg;
    r.azDeg   = c.azDeg;
    r.altRate = (b.altDeg - a.altDeg) / 0.5;
    r.azRate  = WrapDeg180(b.azDeg - a.azDeg) / 0.5;
    r.ok = true;
    return r;
}

// ── A pass ───────────────────────────────────────────────────
struct SatPassLimits {
    double minAltDeg  = 15.0;    // lower than this is mostly tripod and air
    double maxAltDeg  = 80.0;    // the tube and the base
    double maxRateDeg = 3.0;     // deg/s on either axis
};

struct SatPassInfo {
    double riseJd = 0.0, peakJd = 0.0, setJd = 0.0;
    double riseAz = 0.0, peakAz = 0.0, setAz = 0.0;
    double peakAlt = 0.0;
    double peakRangeKm = 0.0;
    bool   visible = false;          // lit, with the observer in the dark
    double visibleSec = 0.0;

    // The part this rig should try.
    bool   trackable = false;
    double trackStartJd = 0.0, trackEndJd = 0.0;
    double startAlt = 0.0, startAz = 0.0;    // where the window opens
    double endAlt = 0.0, endAz = 0.0;        // and where it leaves the tube
    double peakAzRate = 0.0;         // deg/s, within the window
    double peakAltRate = 0.0;
    double peakSkyRate = 0.0;
    double azTravelDeg = 0.0;        // signed, for the cord wrap
    double saturatedSec = 0.0;       // asks more than the mount has
    bool   cutByCeiling = false;
};

inline constexpr double SAT_DARK_SUN_ALT_DEG = -6.0;

// Fill in everything that needs walking the pass a second at a time.
inline void MeasurePass(const Sgp4& s, const LunarObserver& o,
                        const SatPassLimits& lim, SatPassInfo& p) {
    const double dt = 1.0 / 86400.0;
    bool inWindow = false, done = false;
    double lastAz = 0.0;
    for (double t = p.riseJd; t <= p.setJd; t += dt) {
        const SatRates r = SatRatesAt(s, t, o);
        const SatLook l = SatLookAt(s, t, o);
        if (!r.ok || !l.ok) continue;
        if (l.sunlit && l.sunAltDeg < SAT_DARK_SUN_ALT_DEG) p.visibleSec += 1.0;
        if (done) continue;

        const bool band = r.altDeg >= lim.minAltDeg && r.altDeg <= lim.maxAltDeg;
        if (!inWindow) {
            if (!band) continue;
            inWindow = true;
            p.trackStartJd = t;
            p.startAlt = r.altDeg;
            p.startAz  = r.azDeg;
            lastAz = r.azDeg;
        } else if (!band) {
            // Only the first stretch: going back to the far side of a
            // ceiling means a flip nobody has tested.
            p.cutByCeiling = r.altDeg > lim.maxAltDeg;
            p.trackEndJd = t;
            done = true;
            continue;
        }
        p.trackEndJd = t;
        p.endAlt = r.altDeg;
        p.endAz  = r.azDeg;
        const double az = std::fabs(r.azRate), alt = std::fabs(r.altRate);
        const double sky = std::hypot(az * std::cos(DegToRad(r.altDeg)), alt);
        if (az  > p.peakAzRate)  p.peakAzRate  = az;
        if (alt > p.peakAltRate) p.peakAltRate = alt;
        if (sky > p.peakSkyRate) p.peakSkyRate = sky;
        if (az > lim.maxRateDeg || alt > lim.maxRateDeg) p.saturatedSec += 1.0;
        p.azTravelDeg += WrapDeg180(r.azDeg - lastAz);
        lastAz = r.azDeg;
    }
    p.visible = p.visibleSec > 0.0;
    p.trackable = inWindow && p.trackEndJd > p.trackStartJd + 5.0 / 86400.0;
}

// Every pass in the span with a peak above minPeakDeg. Coarse steps,
// then bisection on the edges: 20 s cannot step over a rise-to-set of
// the kind worth tracking.
inline std::vector<SatPassInfo> FindPasses(const Sgp4& s, const LunarObserver& o,
                                           double startJd, double hours,
                                           const SatPassLimits& lim,
                                           double minPeakDeg = 20.0) {
    std::vector<SatPassInfo> out;
    const double step = 20.0 / 86400.0;
    const double endJd = startJd + hours / 24.0;
    auto alt = [&](double t) {
        const SatLook l = SatLookAt(s, t, o);
        return l.ok ? l.altDeg : -90.0;
    };
    auto edge = [&](double lo, double hi) {     // alt crosses 0 between them
        const bool loUp = alt(lo) > 0.0;
        for (int i = 0; i < 16; ++i) {
            const double m = 0.5 * (lo + hi);
            if ((alt(m) > 0.0) == loUp) lo = m; else hi = m;
        }
        return 0.5 * (lo + hi);
    };

    double t = startJd;
    bool up = alt(t) > 0.0;
    double riseJd = up ? startJd : 0.0;
    double peakJd = 0.0, peakAlt = -90.0;
    while (t < endJd + 0.05) {
        const double n = t + step;
        const double an = alt(n);
        const bool nowUp = an > 0.0;
        if (!up && nowUp) { riseJd = edge(t, n); peakAlt = -90.0; }
        if (nowUp && an > peakAlt) { peakAlt = an; peakJd = n; }
        if (up && !nowUp) {
            if (riseJd > 0.0 && riseJd <= endJd) {
                // Golden enough: the coarse peak, refined by a scan of
                // its neighbourhood.
                double best = peakJd, bestA = peakAlt;
                for (double u = peakJd - step; u <= peakJd + step; u += 1.0 / 86400.0) {
                    const double a = alt(u);
                    if (a > bestA) { bestA = a; best = u; }
                }
                if (bestA >= minPeakDeg) {
                    SatPassInfo p;
                    p.riseJd = riseJd;
                    p.peakJd = best;
                    p.setJd  = edge(t, n);
                    p.peakAlt = bestA;
                    p.riseAz = SatLookAt(s, p.riseJd, o).azDeg;
                    const SatLook pk = SatLookAt(s, best, o);
                    p.peakAz = pk.azDeg;
                    p.peakRangeKm = pk.rangeKm;
                    p.setAz  = SatLookAt(s, p.setJd, o).azDeg;
                    MeasurePass(s, o, lim, p);
                    out.push_back(p);
                }
            }
            peakAlt = -90.0;
        }
        up = nowUp;
        t = n;
    }
    return out;
}

// ── Saying it ────────────────────────────────────────────────

// How long an exposure may be for this much residual motion to stay
// inside a pixel. Residual, because the mount is taking the rest.
inline double ExposureForOnePixelMs(double rateDegPerSec, double plateScale) {
    if (rateDegPerSec <= 0.0 || plateScale <= 0.0) return 0.0;
    return plateScale / (rateDegPerSec * 3600.0) * 1000.0;
}

inline double ElementsAgeDays(const Tle& t, double jdNow) {
    return jdNow - t.epochJd;
}

// Beyond this the frame will probably be missed. Roughly the age at
// which along-track error passes the frame's 7' at mid-pass range.
inline constexpr double SAT_STALE_ELEMENTS_DAYS = 2.0;

inline std::string DescribePass(const char* name, const SatPassInfo& p,
                                double ageDays, const SatPassLimits& lim) {
    char b[320];
    int n = snprintf(b, sizeof(b), "%s: peak %.0f deg at %.0f km, sky rate "
                     "%.2f deg/s, azimuth up to %.2f deg/s.",
                     name, p.peakAlt, p.peakRangeKm, p.peakSkyRate, p.peakAzRate);
    std::string s(b, n > 0 ? (size_t)n : 0);
    if (!p.trackable) s += " No part of it is inside the tracking band.";
    if (p.cutByCeiling) {
        snprintf(b, sizeof(b), " Tracking stops at the %.0f deg ceiling.",
                 lim.maxAltDeg);
        s += b;
    }
    if (p.saturatedSec > 0.0) {
        snprintf(b, sizeof(b), " For %.0f s it outruns the mount's %.1f deg/s, "
                 "and the tracker will fall behind.", p.saturatedSec,
                 lim.maxRateDeg);
        s += b;
    }
    if (!p.visible) s += " It will not be sunlit against a dark sky.";
    if (ageDays > SAT_STALE_ELEMENTS_DAYS) {
        snprintf(b, sizeof(b), " The elements are %.1f days old: update them.",
                 ageDays);
        s += b;
    }
    return s;
}

// ── A file of elements ───────────────────────────────────────
//
// Three-line form, name then the two lines, which is what CelesTrak's
// group files are. Lines that do not parse are dropped, and counted,
// because one damaged set must not cost the rest of the file.
inline std::vector<Tle> ParseTleText(const std::string& text, int* dropped = nullptr) {
    std::vector<std::string> lines;
    size_t at = 0;
    while (at < text.size()) {
        size_t e = text.find('\n', at);
        if (e == std::string::npos) e = text.size();
        std::string l = text.substr(at, e - at);
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
        if (!l.empty()) lines.push_back(l);
        at = e + 1;
    }
    std::vector<Tle> out;
    int bad = 0;
    for (size_t i = 0; i + 1 < lines.size(); ++i) {
        const bool pair = lines[i].size() >= 68 && lines[i][0] == '1' &&
                          lines[i + 1][0] == '2';
        if (!pair) continue;
        const std::string name = (i > 0 && lines[i - 1].size() < 40)
                               ? lines[i - 1] : ("#" + lines[i].substr(2, 5));
        const Tle t = ParseTle(name, lines[i], lines[i + 1]);
        if (t.valid) out.push_back(t); else ++bad;
        ++i;
    }
    if (dropped) *dropped = bad;
    return out;
}

// Checked against SGP4's own verification set and against JPL, never
// against a satellite in this telescope.
inline constexpr bool SAT_PREDICTION_PROVEN_ON_SKY = false;

} // namespace raduk::sky
