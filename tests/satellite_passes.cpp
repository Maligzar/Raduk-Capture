// ============================================================
//  satellite_passes -- phase 116
//
//  WHAT THIS FILE IS CHECKING AGAINST
//
//  The propagator: Vallado et al.'s own verification output for every
//  near-Earth case in SGP4-VER.TLE, in sgp4_vectors.h. Pass geometry:
//  JPL Horizons for the ISS over Palomar on 2026-11-01, which comes
//  from NASA's own trajectory and has never been near SGP4. Tracking:
//  a simulated mount with latency and rate and acceleration limits.
// ============================================================

#include "../src/sky/SatPass.h"
#include "../src/core/SatTracker.h"
#include "sgp4_vectors.h"

#include <cstdio>

using namespace raduk::sky;
using raduk::core::SatTracker;

static int g_fail = 0;

static void Check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) ++g_fail;
}

static const LunarObserver kPalomar = {33.354136, 243.1375, 1704.89, true};

static const char* kIss1 = "1 25544U 98067A   26305.53114906  .00006056  00000+0  11739-3 0  9995";
static const char* kIss2 = "2 25544  51.6332 340.4448 0007086 335.8887  24.1768 15.49140542593046";

static double Jd(int y, int mo, int d, int h, int mi, double s = 0.0) {
    if (mo <= 2) { y -= 1; mo += 12; }
    const int A = y / 100, B = 2 - A + A / 4;
    return std::floor(365.25 * (y + 4716)) + std::floor(30.6001 * (mo + 1))
         + d + B - 1524.5 + (h + mi / 60.0 + s / 3600.0) / 24.0;
}

// A mount that behaves like one: commands arrive late, the axes have a
// top rate and take time to reach it.
struct FakeMount {
    double alt = 0, az = 0, rateAlt = 0, rateAz = 0, cmdAlt = 0, cmdAz = 0;
    double maxRate = 3.0, accel = 3.0;
    double signAz = 1.0;      // -1: that axis is wired backwards
    double gainAz = 1.0;      // < 1: a slipping axis
    void Step(double dt) {
        auto go = [&](double& r, double c) {
            double d = c - r, a = accel * dt;
            r += d > a ? a : (d < -a ? -a : d);
            if (r > maxRate) r = maxRate;
            if (r < -maxRate) r = -maxRate;
        };
        go(rateAlt, cmdAlt);
        go(rateAz, cmdAz);
        alt += rateAlt * dt;
        az  += rateAz * signAz * gainAz * dt;
    }
};

struct RunResult {
    SatTracker::Phase end = SatTracker::Phase::Idle;
    std::string reason;
    bool stopped = false, began = false, ended = false, offSent = false;
    double peakErr = 0.0, residual = 0.0;
    double lastCmdAz = 0.0, lastCmdAlt = 0.0;
    int   ratesAfterStop = 0;
};

// Fly a pass end to end at 5 Hz, with `fault` free to break things.
template <class Fault>
static RunResult Fly(const Sgp4& s, const SatPassInfo& p, FakeMount m,
                     SatTracker::Config c, Fault fault) {
    RunResult r;
    const double t0 = p.trackStartJd;
    c.startSec = 0.0;
    c.endSec = (p.trackEndJd - t0) * 86400.0;
    SatTracker tr;
    tr.Configure(c);

    double now = -60.0;
    tr.Start(now);
    int goToAt = -1, tick = 0;
    double residualSum = 0; int residualN = 0;
    while (now < c.endSec + 10.0 && tick < 40000) {
        // As SatDesk does: position now, rate 0.1 s ahead.
        const double jd = t0 + now / 86400.0;
        const SatRates q  = SatRatesAt(s, jd, kPalomar);
        const SatRates qa = SatRatesAt(s, jd + 0.1 / 86400.0, kPalomar);
        SatTracker::Sky sky;
        sky.ok = q.ok && qa.ok; sky.altDeg = q.altDeg; sky.azDeg = q.azDeg;
        sky.altRate = qa.altRate; sky.azRate = qa.azRate;

        bool ok = true;
        double a = m.alt, z = m.az;
        fault(now, ok, a, m, tr);

        const bool slewing = goToAt >= 0 && tick - goToAt < 25;
        const auto o = tr.Tick(now, sky, ok, a, z, slewing);
        if (o.goTo) {
            goToAt = tick;
            const SatRates st = SatRatesAt(s, t0, kPalomar);
            m.alt = st.altDeg + 0.05;      // a GoTo is never quite there
            m.az  = st.azDeg - 0.05;
        }
        if (o.trackingOff) r.offSent = true;
        if (o.rates) {
            if (r.stopped) ++r.ratesAfterStop;
            m.cmdAz = o.azRate; m.cmdAlt = o.altRate;
            r.lastCmdAz = o.azRate; r.lastCmdAlt = o.altRate;
        }
        if (o.stop) { r.stopped = true; m.cmdAz = 0; m.cmdAlt = 0; }
        if (o.beginCapture) r.began = true;
        if (o.endCapture) r.ended = true;
        if (tr.State() == SatTracker::Phase::Tracking) {
            residualSum += tr.ErrorDeg(); ++residualN;
        }
        for (int i = 0; i < 20; ++i) m.Step(0.01);
        now += 0.2;
        ++tick;
    }
    r.end = tr.State();
    r.reason = tr.Reason();
    r.peakErr = tr.PeakErrorDeg();
    r.residual = residualN ? residualSum / residualN : 0.0;
    return r;
}

int main() {
    printf("Satellite passes: orbit, sky, and flying the mount\n");

    printf("\n1. SGP4 against Vallado's verification output\n");
    {
        double worstR = 0, worstV = 0;
        int rows = 0, cases = 0;
        for (const auto& c : kSgp4Cases) {
            const Tle t = ParseTle("", c.l1, c.l2, false);
            Sgp4 s;
            if (!s.Init(t)) {
                printf("  catalog %d refused: %s\n", t.catalog, s.Why().c_str());
                continue;
            }
            ++cases;
            for (int i = 0; i < c.n; ++i) {
                const auto& w = c.rows[i];
                const Sgp4State st = s.Propagate(w.t);
                if (st.decayed) continue;
                ++rows;
                const double dr = std::hypot(std::hypot(st.r[0] - w.x, st.r[1] - w.y), st.r[2] - w.z);
                const double dv = std::hypot(std::hypot(st.v[0] - w.vx, st.v[1] - w.vy), st.v[2] - w.vz);
                if (dr > worstR) worstR = dr;
                if (dv > worstV) worstV = dv;
            }
        }
        printf("  %d cases, %d rows: worst %.3f m, %.6f m/s\n", cases, rows,
               worstR * 1000.0, worstV * 1000.0);
        Check(cases >= 8, "the near-Earth cases all ran");
        Check(rows >= 40, "and enough rows to mean it");
        Check(worstR * 1000.0 < 5.0, "position within 5 m at every row");
        Check(worstV * 1000.0 < 0.05, "velocity within 5 cm/s");
    }

    printf("\n2. Elements\n");
    {
        const Tle t = ParseTle("ISS", kIss1, kIss2);
        Check(t.valid, "a real set parses");
        Check(t.catalog == 25544, "catalog number");
        Check(std::fabs(t.revPerDay - 15.49140542) < 1e-7, "mean motion");
        Check(std::fabs(t.bstar - 0.11739e-3) < 1e-10, "implied-point drag term");
        // 2026 day 305 is November 1.
        Check(std::fabs(t.epochJd - Jd(2026, 11, 1, 12, 44, 51.3)) < 2e-5,
              "epoch: day 305.53 of 2026 is 1 November, 12:44:51");

        std::string bad = kIss2; bad[20] = '9';
        Check(!ParseTle("ISS", kIss1, bad).valid, "a damaged line is refused");

        const std::string text = std::string("ISS (ZARYA)\r\n") + kIss1 + "\r\n" +
                                 kIss2 + "\r\nJUNK\r\n" + kIss1 + "\r\n" + bad + "\r\n";
        int dropped = 0;
        const auto all = ParseTleText(text, &dropped);
        Check(all.size() == 1 && all[0].name == "ISS (ZARYA)", "a file parses, with its name");
        Check(dropped == 1, "and the damaged set is counted, not fatal");

        // GEO: one rev a day.
        const Tle geo = ParseTle("", "1 28626U 05006A   06176.46747782 -.00000089  00000-0  10000-3 0  5019",
                                 "2 28626   0.0279 280.0534 0002108 121.8884 134.1094  1.00271375  5001", false);
        Sgp4 g;
        Check(geo.valid && !g.Init(geo) && !g.Why().empty(), "a high orbit is refused, with a reason");
    }

    Tle iss = ParseTle("ISS", kIss1, kIss2);
    Sgp4 s;
    s.Init(iss);

    printf("\n3. The ISS over Palomar, against JPL Horizons\n");
    {
        struct H { int mi; double az, alt, km; };
        const H rows[] = {
            {16, 325.39112722, 12.212185156, 1375.8},
            {17, 332.575088635, 21.157638534, 999.6},
            {18, 350.088125715, 36.133564083, 682.3},
            {19, 43.181756085, 50.007570972, 542.3},
            {20, 93.750730299, 35.037910293, 696.9},
            {21, 110.220907289, 20.452858991, 1019.6},
            {22, 117.085541703, 11.732048104, 1397.8},
        };
        double worstSky = 0, worstKm = 0, worstCross = 0;
        for (const auto& r : rows) {
            const double jd = Jd(2026, 11, 1, 13, r.mi);
            const SatLook l = SatLookAt(s, jd, kPalomar);
            const double dz = WrapDeg180(l.azDeg - r.az) * std::cos(DegToRad(r.alt));
            const double da = l.altDeg - r.alt;
            const double e = std::hypot(dz, da);
            if (e > worstSky) worstSky = e;
            const double km = std::fabs(l.rangeKm - r.km);
            if (km > worstKm) worstKm = km;
            printf("  13:%d  az %+.3f  alt %+.3f deg   range %+.1f km\n",
                   r.mi, WrapDeg180(l.azDeg - r.az), da, l.rangeKm - r.km);
            (void)worstCross;
        }
        Check(worstSky < 0.5, "within half a degree across the pass");
        Check(worstKm < 8.0, "range within 8 km");
    }

    SatPassLimits lim;
    const double from = Jd(2026, 11, 1, 13, 0);
    const auto passes = FindPasses(s, kPalomar, from, 24.0, lim, 20.0);

    printf("\n4. Finding passes\n");
    const SatPassInfo* good = nullptr;
    {
        Check(!passes.empty(), "a day holds at least one");
        for (const auto& p : passes) {
            printf("  rise +%.1f h  peak %.1f deg  az rate %.2f  sky rate %.2f deg/s  "
                   "travel %+.0f  window %.0f s  visible %.0f s\n",
                   (p.riseJd - from) * 24.0, p.peakAlt, p.peakAzRate, p.peakSkyRate,
                   p.azTravelDeg, (p.trackEndJd - p.trackStartJd) * 86400.0, p.visibleSec);
            if (std::fabs(p.peakJd - Jd(2026, 11, 1, 13, 19)) < 2.0 / 1440.0) good = &p;
        }
        Check(good != nullptr, "the 13:19 pass Horizons has is found");
        if (good) {
            Check(std::fabs(good->peakAlt - 50.0) < 1.5, "peaking at 50 deg as Horizons says");
            Check(good->riseJd < Jd(2026, 11, 1, 13, 16) &&
                  good->setJd > Jd(2026, 11, 1, 13, 22), "rise and set bracket it");
            Check(good->trackable, "and it is inside the tracking band");
            Check(good->peakSkyRate > 0.5 && good->peakSkyRate < 1.5, "sky rate near 1 deg/s");
        }
    }

    printf("\n5. Sunlight\n");
    {
        // A point on the night side of the planet, opposite the Sun.
        const double jd = Jd(2026, 11, 1, 13, 19);
        const LVec sun = SunDirectionOfDate(jd);
        const double declination = SafeAsinDeg(sun.z);
        Check(declination < -13.0 && declination > -16.0,
              "the Sun's declination on 1 November is about -14.5");
        // Scan an orbit: a low orbit spends about a third of it dark.
        int lit = 0, n = 0;
        for (double t = 0; t < 92.7; t += 0.1, ++n)
            if (SatLookAt(s, jd + t / 1440.0, kPalomar).sunlit) ++lit;
        const double f = (double)lit / n;
        char b[120];
        snprintf(b, sizeof(b), "sunlit for %.0f%% of an orbit, as a low orbit should be", f * 100.0);
        Check(f > 0.55 && f <= 1.0, b);
    }

    printf("\n6. The keyhole\n");
    {
        // Straight through the zenith from a site under the track.
        const double jd = good ? good->peakJd : from;
        const Sgp4State st = s.AtJd(jd);
        // Geodetic-ish site directly under the satellite.
        const double gmst = GreenwichMeanSiderealDeg(jd);
        const double lon = std::atan2(st.r[1], st.r[0]) / DegToRad(1.0) - gmst;
        const double lat = std::atan2(st.r[2], std::hypot(st.r[0], st.r[1])) / DegToRad(1.0);
        LunarObserver under{lat, WrapDeg180(lon + 0.15), 0.0, true};
        const auto over = FindPasses(s, under, jd - 0.01, 1.0, lim, 70.0);
        Check(!over.empty(), "a near-overhead pass exists from under the track");
        if (!over.empty()) {
            const auto& p = over[0];
            printf("  peak %.1f deg, az rate in the band %.2f deg/s, cut by ceiling: %d\n",
                   p.peakAlt, p.peakAzRate, (int)p.cutByCeiling);
            Check(p.cutByCeiling, "the ceiling cuts the window short");
            SatPassLimits tight = lim; tight.maxAltDeg = 89.5;
            SatPassInfo q = p;
            q.peakAzRate = 0; q.peakAltRate = 0; q.peakSkyRate = 0; q.azTravelDeg = 0;
            q.saturatedSec = 0; q.visibleSec = 0; q.cutByCeiling = false;
            MeasurePass(s, under, tight, q);
            printf("  with no ceiling: az rate %.1f deg/s, %.0f s beyond the mount\n",
                   q.peakAzRate, q.saturatedSec);
            Check(q.peakAzRate > 3.0, "without it the azimuth rate outruns the mount");
            Check(q.saturatedSec > 0.0, "and the time that lasts is counted");
            const std::string d = DescribePass("ISS", q, 0.1, tight);
            printf("  %s\n", d.c_str());
            Check(d.find("outruns") != std::string::npos, "and said");
        }
    }

    printf("\n7. Saying it\n");
    {
        Check(std::fabs(ExposureForOnePixelMs(1.0, 0.203) - 0.0564) < 0.001,
              "1 deg/s unguided is 0.056 ms a pixel");
        if (good) {
            const std::string d = DescribePass("ISS", *good, 3.5, lim);
            printf("  %s\n", d.c_str());
            Check(d.find("3.5 days old") != std::string::npos, "stale elements are called out");
        }
        // Two reads straddling a sleep-free spin must differ by less
        // than a second and more than nothing: time() could not.
        const double a = PreciseJdNow();
        double b = a;
        while (b == a) b = PreciseJdNow();
        Check(a > 2460000.0, "the fine clock reads a real date");
        Check((b - a) * 86400.0 < 0.1, "and steps in well under a second");
    }

    if (!good) { printf("\nFAILED: no pass to fly\n"); return 1; }

    printf("\n8. Flying the pass\n");
    SatTracker::Config cfg;
    auto none = [](double, bool&, double&, FakeMount&, SatTracker&) {};
    {
        FakeMount m;
        const RunResult r = Fly(s, *good, m, cfg, none);
        printf("  ended %d, mean error %.3f deg (%.0f\"), peak %.3f deg\n",
               (int)r.end, r.residual, r.residual * 3600.0, r.peakErr);
        Check(r.end == SatTracker::Phase::Complete, "a healthy mount completes");
        Check(r.offSent, "tracking is turned off before the first rate");
        Check(r.began && r.ended, "and the capture opens and closes");
        Check(r.stopped, "and it stops at the end");
        Check(r.ratesAfterStop == 0, "with no rate sent after the stop");
        Check(r.residual < 0.05, "holding within 3' of the prediction");
    }

    printf("\n9. Everything that should stop it\n");
    {
        FakeMount m; m.signAz = -1.0;
        const RunResult r = Fly(s, *good, m, cfg, none);
        printf("  reversed axis: %s\n", r.reason.c_str());
        Check(r.end == SatTracker::Phase::Failed && r.stopped, "an axis running backwards is caught");
        Check(!r.began, "before anything is recorded");
        Check(r.ratesAfterStop == 0, "and nothing moves after it");
    }
    {
        FakeMount m; m.gainAz = 0.5;
        const RunResult r = Fly(s, *good, m, cfg, none);
        printf("  slipping axis: %s\n", r.reason.c_str());
        Check(r.end == SatTracker::Phase::Failed && r.stopped, "a stalling axis is caught");
    }
    {
        FakeMount m;
        const RunResult r = Fly(s, *good, m, cfg,
            [](double now, bool& ok, double&, FakeMount&, SatTracker&) {
                if (now > 60.0) ok = false; });
        printf("  silent mount: %s\n", r.reason.c_str());
        Check(r.end == SatTracker::Phase::Failed && r.stopped, "a mount that stops answering stops the run");
        Check(r.began && r.ended, "and the file is closed");
    }
    {
        FakeMount m;
        const RunResult r = Fly(s, *good, m, cfg,
            [](double now, bool&, double& a, FakeMount&, SatTracker&) {
                if (now > 60.0) a = 5.0; });
        printf("  low mount: %s\n", r.reason.c_str());
        Check(r.end == SatTracker::Phase::Failed && r.stopped, "the altitude floor holds, from the mount's own reading");
    }
    {
        FakeMount m;
        const RunResult r = Fly(s, *good, m, cfg,
            [](double now, bool&, double&, FakeMount&, SatTracker& t) {
                if (now > 40.0) t.Abort("escape"); });
        Check(r.end == SatTracker::Phase::Aborted && r.stopped && r.ended,
              "abort stops the axes and closes the file");
        Check(r.ratesAfterStop == 0, "and no rate follows");
        Check(r.reason == "escape", "keeping its reason");
    }
    {
        // Slew that never ends.
        SatTracker::Config c = cfg;
        c.startSec = 500.0; c.endSec = 900.0;
        SatTracker t; t.Configure(c); t.Start(0.0);
        SatTracker::Sky sky; sky.ok = true;
        SatTracker::Output o;
        bool stopped = false;
        for (double n = 0; n < 300.0; n += 1.0) {
            o = t.Tick(n, sky, true, 30, 100, true);
            stopped = stopped || o.stop;
        }
        Check(t.State() == SatTracker::Phase::Failed && stopped, "a slew that never ends fails");
    }
    {
        // A slew that lands after the pass.
        SatTracker::Config c = cfg;
        c.startSec = 5.0; c.endSec = 20.0;
        SatTracker t; t.Configure(c); t.Start(0.0);
        SatTracker::Sky sky; sky.ok = true;
        for (double n = 0; n < 30.0; n += 1.0) t.Tick(n, sky, true, 30, 100, true);
        Check(t.State() == SatTracker::Phase::Failed, "a slew outlasting the pass fails rather than flying");
    }
    {
        SatTracker t; SatTracker::Sky sky;
        const auto o = t.Tick(0, sky, true, 30, 100, false);
        Check(!o.rates && !o.goTo && !o.stop, "an idle tracker does nothing");
    }

    printf("\n10. The limits it obeys\n");
    {
        SatTracker::Config c = cfg;
        c.startSec = 0.0; c.endSec = 100.0;
        SatTracker t; t.Configure(c); t.Start(-5.0);
        SatTracker::Sky sky; sky.ok = true; sky.altDeg = 40; sky.azDeg = 100;
        sky.azRate = 50.0; sky.altRate = -50.0;      // absurd
        t.Tick(-5.0, sky, true, 40, 100, false);
        t.Tick(-3.0, sky, true, 40, 100, false);
        double peak = 0, lastAz = 0, worstStep = 0;
        double n = 0.0;
        for (; n < 8.0; n += 0.2) {
            const auto o = t.Tick(n, sky, true, 40, 100, false);
            if (std::fabs(o.azRate) > peak) peak = std::fabs(o.azRate);
            const double st = std::fabs(o.azRate - lastAz);
            if (st > worstStep) worstStep = st;
            lastAz = o.azRate;
        }
        Check(peak <= c.maxRateDeg + 1e-9, "no command above the rate limit, whatever is asked");
        Check(worstStep <= c.maxAccelDeg * 0.2 + 1e-9, "and no step above the acceleration limit");
    }

    printf("\n%s\n", g_fail ? "FAILED" : "All passed");
    return g_fail ? 1 : 0;
}
