// ============================================================
//  horizons_targets -- phase 121
//
//  WHAT THIS FILE IS CHECKING AGAINST
//
//  JPL's own answer for the view from a site, which this code never
//  asks for: the app fetches positions seen from the centre of the
//  Earth and applies the site itself. So a row here checks the parse,
//  the interpolation, the frame, the parallax and the horizon at once,
//  against horizons_fixtures.h, fetched 2026-11-03.
// ============================================================

#include "../src/sky/Ephemeris.h"
#include "horizons_fixtures.h"

#include <cstdio>

using namespace raduk::sky;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) ++g_fail;
}

static double Jd(int y, int mo, int d, int h, int mi) {
    if (mo <= 2) { y -= 1; mo += 12; }
    const int A = y / 100, B = 2 - A + A / 4;
    return std::floor(365.25 * (y + 4716)) + std::floor(30.6001 * (mo + 1))
         + d + B - 1524.5 + (h + mi / 60.0) / 24.0;
}

static const LunarObserver kPalomar  = {33.354136, 243.1375, 1704.89, true};
static const LunarObserver kMaunaKea = {19.8260847, 204.5278, 4210.24, true};

template <int N>
static void AgainstSite(const char* what, const Ephemeris& e, const ObsRow (&rows)[N],
                        const LunarObserver& site, double tolDeg, bool aboveOnly) {
    double worst = 0, worstRaDec = 0;
    int n = 0;
    for (const ObsRow& r : rows) {
        if (aboveOnly && r.el < 5.0) continue;
        const SatLook l = EphemLookAt(e, Jd(r.y, r.mo, r.d, r.h, r.mi), site);
        if (!l.ok) continue;
        ++n;
        const double e1 = std::hypot(WrapDeg180(l.azDeg - r.az) * std::cos(DegToRad(r.el)),
                                     l.altDeg - r.el);
        if (e1 > worst) worst = e1;
        const double e2 = std::hypot(WrapDeg180(l.raHours * 15.0 - r.ra) * std::cos(DegToRad(r.dec)),
                                     l.decDeg - r.dec);
        if (e2 > worstRaDec) worstRaDec = e2;
    }
    printf("  %s: %d rows, worst %.4f deg alt-az, %.4f deg RA/Dec\n",
           what, n, worst, worstRaDec);
    char b[160];
    snprintf(b, sizeof(b), "%s agrees with JPL's view from the site within %g deg", what, tolDeg);
    Check(n >= 5 && worst < tolDeg && worstRaDec < tolDeg, b);
}

int main() {
    printf("Horizons targets: anything JPL predicts, from this site\n");

    const Ephemeris iss = ParseHorizonsVectors(kIssVectors);
    const Ephemeris apo = ParseHorizonsVectors(kApophisVectors);

    printf("\n1. Reading the table\n");
    {
        Check(iss.ok && iss.pts.size() == 9, "nine ISS rows");
        Check(apo.ok && apo.pts.size() == 25, "twenty-five Apophis rows");
        Check(std::fabs(iss.pts[0].jd - Jd(2026, 11, 1, 13, 15)) < 1e-6, "times are read as UT");
        Check(std::fabs(iss.pts[0].r.x + 1597.917718395591) < 1e-6, "positions to the digit");
        Check(std::fabs(iss.pts[0].v.z + 2.700687056318758) < 1e-9, "and velocities");

        const Ephemeris bad = ParseHorizonsVectors(
            "API VERSION: 1.2\nNo matches found.\n  Use ID# to make unique selection.\n");
        Check(!bad.ok && bad.why.find("No matches") != std::string::npos,
              "a name JPL cannot find is said, not called a parse error");
        Check(!ParseHorizonsVectors("").ok, "an empty reply is refused");
        const Ephemeris named = ParseHorizonsVectors(
            "Target body name: 99942 Apophis (2004 MN4)        {source: JPL#220}\n$$SOE\n$$EOE\n");
        Check(named.name == "99942 Apophis (2004 MN4)", "the name comes off the header");
        Check(!named.ok, "and a table with no rows is not a table");
    }

    printf("\n2. Between the rows\n");
    {
        // Drop every other ISS row and put them back by interpolation:
        // the hardest case there is, at 7.7 km/s.
        Ephemeris half;
        for (size_t i = 0; i < iss.pts.size(); i += 2) half.pts.push_back(iss.pts[i]);
        half.ok = true;
        double worst = 0;
        for (size_t i = 1; i + 1 < iss.pts.size(); i += 2) {
            LVec r;
            EphemerisAt(half, iss.pts[i].jd, r);
            const double km = (r - iss.pts[i].r).Len();
            if (km > worst) worst = km;
        }
        printf("  ISS rebuilt from two-minute rows: worst %.3f km\n", worst);
        Check(worst < 0.5, "a missing ISS row is rebuilt within half a kilometre");
        LVec r;
        Check(!EphemerisAt(iss, iss.pts.front().jd - 0.001, r) &&
              !EphemerisAt(iss, iss.pts.back().jd + 0.001, r), "outside the table there is no answer");
    }

    printf("\n3. From the site, against JPL\n");
    // 0.06 deg is the ISS moving for about 0.07 s: the vector table is
    // tagged UTC and the site table in JPL's UT, and they part by UT1 -
    // UTC. Geometry is the Apophis row below, at 0.004 deg.
    AgainstSite("ISS over Palomar", iss, kIssFromPalomar, kPalomar, 0.08, false);
    AgainstSite("Apophis over Mauna Kea", apo, kApophisFromMaunaKea, kMaunaKea, 0.02, true);
    {
        // The reason the site is applied here and not ignored.
        const double jd = Jd(2029, 4, 13, 22, 30);
        const SatLook site = EphemLookAt(apo, jd, kMaunaKea);
        const SatLook geo  = EphemLookAt(apo, jd, LunarObserver{});
        const double sep = std::hypot(WrapDeg180(site.raHours - geo.raHours) * 15.0 * std::cos(DegToRad(site.decDeg)),
                                      site.decDeg - geo.decDeg);
        printf("  parallax at closest approach: %.2f deg\n", sep);
        Check(sep > 2.0, "at 38,000 km the site moves Apophis by degrees");
    }

    printf("\n4. A window to fly\n");
    {
        SatPassLimits lim;
        const SatPassInfo w = EphemWindow(apo, kMaunaKea, Jd(2029, 4, 13, 20, 0), lim);
        printf("  Apophis: in the band %.1f h after 20:00 UT, for %.0f min, peak %.1f deg, "
               "sky rate up to %.4f deg/s\n",
               (w.trackStartJd - Jd(2029, 4, 13, 20, 0)) * 24.0,
               (w.trackEndJd - w.trackStartJd) * 1440.0, w.peakAlt, w.peakSkyRate);
        Check(w.trackable, "the close approach is trackable from Mauna Kea");
        Check(w.startAlt >= 14.9 && w.startAlt < 16.0, "it starts at the floor");
        Check(w.trackStartJd > Jd(2029, 4, 13, 22, 10), "after it rises");
        double jplPeak = 0;
        for (const ObsRow& r : kApophisFromMaunaKea) if (r.el > jplPeak) jplPeak = r.el;
        Check(std::fabs(w.peakAlt - jplPeak) < 0.1, "and peaks where JPL says");
        Check(w.peakSkyRate > 0.001 && w.peakSkyRate < 0.1,
              "slow enough for the mount, far faster than a planet");
        Check(!EphemWindow(Ephemeris{}, kMaunaKea, 0.0, lim).trackable, "no table, no window");
    }

    printf("\n5. The request\n");
    {
        const std::string p = HorizonsVectorsPath("99942;", 2461346.0, 2461346.25);
        Check(p.find("CENTER=%27500%40399%27") != std::string::npos,
              "asks for the centre of the Earth, so the site is never sent");
        Check(p.find("COMMAND=%2799942%3B%27") != std::string::npos, "the name is encoded");
        Check(p.find("JD%202461346.000000") != std::string::npos, "times as Julian dates");
        Check(p.find(" ") == std::string::npos && p.find("'") == std::string::npos,
              "nothing unencoded");
    }

    printf("\n%s\n", g_fail ? "FAILED" : "All passed");
    return g_fail ? 1 : 0;
}
