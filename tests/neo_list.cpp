// ============================================================
//  neo_list -- phase 128
//
//  The close-approach list parsed from JPL's own JSON shape, the
//  Horizons command for each kind of designation, and the brightness
//  and position of an NEO in geometry whose answers are known.
// ============================================================

#include "../src/sky/NeoList.h"

#include <cmath>
#include <cstdio>

using namespace raduk::sky;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) ++g_fail;
}

// JPL's shape: a signature, then field names, then rows of strings.
static const char* kCad = R"CAD({"signature":{"version":"1.5","source":"NASA/JPL SBDB Close Approach Data API"},
"count":"3","fields":["des","orbit_id","jd","cd","dist","dist_min","dist_max","v_rel","v_inf","t_sigma_f","h","fullname"],
"data":[["2026 VA1","4","2461351.250000000","2026-Nov-06 18:00","0.00213","0.00212","0.00214","9.81","9.70","< 00:01","27.1","       (2026 VA1)"],
["99942","220","2462240.407000000","2029-Apr-13 21:46","0.000254","0.000254","0.000254","7.42","5.84","< 00:01","19.09","  99942 Apophis (2004 MN4)"],
["2026 UX","2","2461360.5","2026-Nov-15 00:00","0.031","0.030","0.032","4.2","4.1","00:12",null,"       (2026 UX)"]]})CAD";

static Ephemeris Still(double jd0, double jd1, LVec r) {
    Ephemeris e;
    e.ok = true;
    for (double jd = jd0; jd <= jd1 + 1e-9; jd += 0.25) {
        EphemPoint p;
        p.jd = jd; p.r = r; p.v = LVec{0, 0, 0};
        e.pts.push_back(p);
    }
    return e;
}

int main() {
    printf("NEO list: which, where, how bright\n");

    printf("\n1. The close-approach list\n");
    const auto c = ParseCad(kCad);
    Check(c.size() == 3, "three rows");
    Check(c.size() == 3 && c[0].des == "2026 VA1" && std::fabs(c[0].distAu - 0.00213) < 1e-9 &&
          std::fabs(c[0].h - 27.1) < 1e-9, "designation, distance and H, by column name");
    Check(c.size() == 3 && c[1].fullname == "99942 Apophis (2004 MN4)", "a full name tidied");
    Check(c.size() == 3 && c[0].fullname == "2026 VA1", "and a bare designation unwrapped");
    Check(c.size() == 3 && c[2].h == 99, "a missing H is 99, not 0");
    Check(ParseCad("{\"count\":\"0\"}").empty(), "an answer with no table is no list");
    const std::string p = CadPath(2461351.0, 7, 30, 0.05);
    Check(p.find("date-min=2026-10-30") != std::string::npos &&
          p.find("date-max=2026-12-06") != std::string::npos &&
          p.find("dist-max=0.050") != std::string::npos, "the window asked for");

    printf("\n2. What Horizons is asked for\n");
    Check(HorizonsCommandFor("99942") == "99942;", "a numbered asteroid by number");
    Check(HorizonsCommandFor("2026 VA1") == "DES=2026 VA1;", "a provisional one as a designation");

    printf("\n3. Brightness\n");
    Check(std::fabs(ApparentMag(18.0, 1.0, 1.0, 0.0) - 18.0) < 1e-9, "at 1 au from both, opposite the Sun, it is H");
    Check(std::fabs(ApparentMag(18.0, 2.0, 1.0, 0.0) - (18.0 + 5 * std::log10(2.0))) < 1e-9,
          "twice as far from the Sun is 1.5 mag fainter");
    Check(ApparentMag(18.0, 1.0, 1.0, 60.0) > ApparentMag(18.0, 1.0, 1.0, 20.0) + 0.5,
          "and a wider phase angle is fainter");

    printf("\n4. One seen from the centre of the Earth\n");
    const double jd = 2461351.0;
    // Sun 1 au along +x; the NEO 0.01 au along -x: opposite the Sun.
    const Ephemeris sun = Still(jd - 1, jd + 2, LVec{kAuKm, 0, 0});
    const Ephemeris neo = Still(jd - 1, jd + 2, LVec{-0.01 * kAuKm, 0, 0});
    LunarObserver geo;   // set = false: the geocentre
    const NeoView v = ViewNeo(neo, sun, 20.0, jd, geo);
    printf("    RA %.3f h, Dec %.3f, %.4f au, mag %.2f\n", v.raHours, v.decDeg, v.earthDistAu, v.mag);
    Check(v.ok && std::fabs(v.raHours - 12.0) < 0.01 && std::fabs(v.decDeg) < 0.01,
          "along -x it is at RA 12h, Dec 0");
    Check(std::fabs(v.earthDistAu - 0.01) < 1e-6, "0.01 au away");
    Check(std::fabs(v.mag - (20.0 + 5 * std::log10(1.01 * 0.01))) < 0.01,
          "at opposition its magnitude is H + 5 log(r delta)");
    Check(v.rateDegPerHour < 0.01, "an object holding still in space does not move against the stars");
    // Moving 0.001 au a day across the line of sight at 0.01 au: about
    // 5.7 degrees a day, 0.24 an hour.
    Ephemeris mv;
    mv.ok = true;
    for (double t = jd - 1; t <= jd + 2 + 1e-9; t += 0.25) {
        EphemPoint pt;
        pt.jd = t;
        pt.r = LVec{-0.01 * kAuKm, 0.001 * kAuKm * (t - jd), 0};
        pt.v = LVec{0, 0.001 * kAuKm / 86400.0, 0};
        mv.pts.push_back(pt);
    }
    const NeoView m = ViewNeo(mv, sun, 20.0, jd, geo);
    printf("    moving: %.3f deg/h\n", m.rateDegPerHour);
    Check(std::fabs(m.rateDegPerHour - 0.2387) < 0.01, "and one crossing the line of sight moves at the right rate");

    printf("\n%s\n", g_fail ? "FAILED" : "All passed");
    return g_fail ? 1 : 0;
}
