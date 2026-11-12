// ============================================================
//  Phase 137: Jupiter's central meridians, the Red Spot, moons and shadows
//
//  Against JPL Horizons replies saved 2026-11-12 (jupiter_fixtures.h):
//
//    1. System III central meridian within 0.1 deg of Horizons on seven
//       dates from November 2026 to February 2027, and Systems I and II
//       moving at their own rates.
//    2. The Red Spot transits every System II rotation (9 h 55.5 m), at
//       the meridian the model says, and a degree of longitude moves the
//       transit by about 1.65 minutes.
//    3. Io's visibility codes become events: a transit, an occultation
//       and an eclipse that overlaps it.
//    4. Io's transit worked from vectors matches Horizons' own codes to
//       within two minutes (the check on the geometry), and its shadow
//       transit comes before it, as it does before opposition.
// ============================================================

#include "sky/JupiterEvents.h"
#include "jupiter_fixtures.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace raduk::sky;
using namespace raduk::sky::jupiter;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}
static std::string F(double v, int d = 2) { char b[48]; snprintf(b, sizeof(b), "%.*f", d, v); return b; }
static double Jd0(int y, int m, int d) {
    static const char* mon[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    char b[32]; snprintf(b, sizeof(b), "%04d-%s-%02d 00:00", y, mon[m - 1], d);
    return JdFromHorizonsDate(b);
}
static double Wrap(double a) { while (a > 180) a -= 360; while (a < -180) a += 360; return a; }

int main() {
    printf("Jupiter events (phase 137)\n");

    printf("\n1. Central meridians\n");
    {
        double worst = 0;
        for (const auto& r : kJupiterCmIII) {
            const Meridians m = CentralMeridians(Jd0(r.y, r.m, r.d));
            worst = (std::max)(worst, std::fabs(Wrap(m.sysIII - r.sysIII)));
        }
        Check(worst < 0.1, "System III within 0.1 deg of Horizons, Nov 2026 - Feb 2027", F(worst, 3) + " deg");
        const double j = Jd0(2026, 12, 1);
        const Meridians a = CentralMeridians(j), b = CentralMeridians(j + 1.0);
        const double rI = Norm360(b.sysI - a.sysI), rII = Norm360(b.sysII - a.sysII);
        Check(std::fabs(rI - Norm360(877.90 - 0.083)) < 0.5, "System I advances 877.9 deg a day (mod 360)", F(rI));
        Check(std::fabs(rII - Norm360(870.27 - 0.083)) < 0.5, "System II advances 870.3 deg a day (mod 360)", F(rII));
        Check(std::fabs(CentralMeridians(Jd0(2026, 11, 13)).distanceAu - 5.31448) < 0.002,
              "Earth-Jupiter distance matches Horizons", F(CentralMeridians(Jd0(2026, 11, 13)).distanceAu, 4) + " AU");
    }

    printf("\n2. The Great Red Spot\n");
    {
        GrsModel g;   // 91 deg on 2026-06-01, +1.75 deg a month
        const double from = Jd0(2026, 11, 13);
        const auto t = GrsTransits(g, from, from + 2.0);
        Check(t.size() >= 4 && t.size() <= 5, "four or five transits in two days", std::to_string(t.size()));
        bool onMeridian = true, period = true;
        for (size_t i = 0; i < t.size(); ++i) {
            if (std::fabs(Wrap(CentralMeridians(t[i]).sysII - g.LongitudeAt(t[i]))) > 0.05) onMeridian = false;
            if (i && std::fabs((t[i] - t[i - 1]) * 1440.0 - 595.6) > 1.0) period = false;
        }
        Check(onMeridian, "each at the spot's System II longitude");
        Check(period, "9 h 55.6 m apart");
        GrsModel g2 = g; g2.lonSysII += 1.0;
        const auto t2 = GrsTransits(g2, from, from + 1.0);
        const double shift = (t2[0] - t[0]) * 1440.0;
        Check(std::fabs(shift - 1.65) < 0.1, "one degree of longitude is about 1.65 minutes", F(shift) + " min");
        Check(g.AgeDays(from) > GrsModel::kStaleDays, "the default epoch is months old, and says so",
              F(g.AgeDays(from), 0) + " days");
        Check(JdFromIsoDate("2026-06-01") == g.epochJd, "rig.ini's date reads to the same epoch",
              F(JdFromIsoDate("2026-06-01"), 1));
        Check(JdFromIsoDate("June 1") == 0, "and a date that does not read is refused");
    }

    printf("\n3. Io from Horizons' visibility codes\n");
    {
        const auto rows = ParseVisibility(kIoVis2Day);
        Check(rows.size() == 289, "the two-day table reads", std::to_string(rows.size()) + " rows");
        const auto ev = EventsFromVisibility(1, rows);
        int nT = 0, nO = 0, nE = 0;
        const Event* occ = nullptr; const Event* ecl = nullptr; const Event* tr = nullptr;
        for (const auto& e : ev) {
            if (e.kind == Kind::Transit) { nT++; tr = &e; }
            if (e.kind == Kind::Occultation) { nO++; occ = &e; }
            if (e.kind == Kind::Eclipse) { nE++; ecl = &e; }
        }
        Check(nT == 1 && nO == 1 && nE == 1, "one transit, one occultation, one eclipse",
              std::to_string(nT) + "/" + std::to_string(nO) + "/" + std::to_string(nE));
        if (ecl && occ) {
            Check(HhMm(ecl->startJd) == "16:35", "eclipse begins between 16:30 and 16:40", HhMm(ecl->startJd));
            Check(occ->startJd > ecl->startJd && occ->startJd < ecl->endJd,
                  "Io goes behind the disc while still eclipsed (the U rows)", HhMm(occ->startJd));
            Check(HhMm(occ->endJd) == "20:15", "and reappears between 20:10 and 20:20", HhMm(occ->endJd));
        }
        if (tr) Check(HhMm(tr->startJd) == "15:05" && HhMm(tr->endJd) == "17:25",
                      "transit 15:05 to 17:25 at 10-minute rows", EventLine(*tr));
    }

    printf("\n4. Shadows, from vectors\n");
    {
        const Ephemeris io = ParseHorizonsVectors(kIoVec), sun = ParseHorizonsVectors(kSunVec),
                        earth = ParseHorizonsVectors(kEarthVec);
        Check(io.pts.size() == 55 && sun.pts.size() == 55 && earth.pts.size() == 55, "the vector tables read",
              std::to_string(io.pts.size()));
        const auto vis = EventsFromVisibility(1, ParseVisibility(kIoVisTransit));
        const Event* hzT = nullptr;
        for (const auto& e : vis) if (e.kind == Kind::Transit) hzT = &e;
        const auto vt = EventsFromVectors(1, io, sun, earth, false, true);
        Check(hzT && vt.size() == 1, "Horizons' transit, and one worked from the vectors");
        if (hzT && vt.size() == 1) {
            const double ds = (vt[0].startJd - hzT->startJd) * 1440.0, de = (vt[0].endJd - hzT->endJd) * 1440.0;
            Check(std::fabs(ds) < 2.0 && std::fabs(de) < 2.0, "they agree within two minutes at both ends",
                  F(ds, 1) + " / " + F(de, 1) + " min");
        }
        const auto sh = EventsFromVectors(1, io, sun, earth, true, false);
        Check(sh.size() == 1, "one shadow transit", std::to_string(sh.size()));
        if (sh.size() == 1 && hzT) {
            Check(sh[0].startJd < hzT->startJd && sh[0].endJd < hzT->endJd,
                  "the shadow leads the moon, as before opposition", EventLine(sh[0]));
            const double durMin = (sh[0].endJd - sh[0].startJd) * 1440.0;
            Check(durMin > 120 && durMin < 150, "lasting about as long as the transit", F(durMin, 0) + " min");
        }
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
