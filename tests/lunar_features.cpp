// ============================================================
//  lunar_features -- phase 115
//
//  WHAT THIS FILE IS CHECKING AGAINST
//
//  Not itself. The series is held to Meeus's worked example 47.a and
//  the libration to 53.a. Everything else is JPL Horizons (DE441),
//  fetched 2026-11-01 and pasted below: the Moon from the geocentre and
//  from Palomar (code 675, 33.354136N, 243.1375E, 1704.89 m), with the
//  sub-observer point, the sub-solar point and the pole's position
//  angle. RA and Dec are astrometric J2000, which is what the
//  application's star positions are.
//
//  A place on the surface has no Horizons figure here, so the craters
//  are checked by the geometry that must hold and by which limb the
//  named ones lie on.
// ============================================================

#include "../src/sky/SkyObject.h"

#include <cstdio>
#include <cstring>

using namespace raduk::sky;

static int g_fail = 0;

static void Check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) ++g_fail;
}

static void Near(double got, double want, double tol, const char* what) {
    char b[200];
    snprintf(b, sizeof(b), "%s: %.4f, want %.4f +/- %g", what, got, want, tol);
    Check(std::fabs(got - want) <= tol, b);
}

static double AngDiff(double a, double b) {
    double d = std::fmod(a - b, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d < -180.0) d += 360.0;
    return std::fabs(d);
}

// Great circle separation, arcsec.
static double SepArcsec(double ra1h, double dec1, double ra2deg, double dec2) {
    const double dRa = DegToRad(AngDiff(ra1h * 15.0, ra2deg));
    const double d1 = DegToRad(dec1), d2 = DegToRad(dec2);
    const double x = std::cos(d2) * std::sin(dRa);
    const double y = std::cos(d1) * std::sin(d2)
                   - std::sin(d1) * std::cos(d2) * std::cos(dRa);
    const double z = std::sin(d1) * std::sin(d2)
                   + std::cos(d1) * std::cos(d2) * std::cos(dRa);
    return std::atan2(std::sqrt(x*x + y*y), z) * ARCSEC_PER_RAD;
}

struct GeoRow {
    int y, mo; double day;
    double ra, dec, subLon, subLat, sunLon, sunLat, np;
};

static const GeoRow kGeo[] = {
    {2023, 2, 25.0,   35.873402123,  14.515397859,   7.280591, -0.319895, 124.622521, -1.335024, 342.8127},
    {2025,11, 21.0,  243.544916331, -26.237163649, 358.967384,  6.512839, 171.168725, -1.509042,   9.8889},
    {2026, 2, 28.75, 128.390552711,  21.767024912,   3.604937, -3.728216,  37.962135,  0.040395,  13.1601},
    {2026, 6,  9.25, 357.485394061,   1.299544140, 352.181262, -2.805743, 252.244473,  1.482109, 337.9656},
    {2026, 9, 16.625,235.281026436, -24.982694300,   3.449568,  6.804410, 117.921176, -0.664988,  13.0447},
    {2026,12, 26.0,  124.012174893,  21.546260329,   3.492776, -2.282872, 335.977544, -1.141603,  11.5816},
    {2027, 4,  5.125,351.772164901,  -0.489303718, 354.932225, -3.677885, 197.531004,  1.284226, 338.0546},
    {2031, 3,  1.0,   66.258993507,  21.065090530,   2.851671,  0.746831,  94.546410,  1.539935, 349.4137},
    {2031, 3, 10.0,  177.021718890,  -3.535262675, 355.080039,  5.815683, 345.150623,  1.495924,  24.1707},
    {2031, 3, 19.0,  304.904712293, -14.888690367,   1.391670, -5.910740, 235.633743,  1.340320, 347.3506},
};

struct TopoRow {
    int y, mo; double day;
    double ra, dec, alt, subLon, subLat, np;
};

// All above the horizon, which is the only place it matters.
static const TopoRow kTopo[] = {
    {2026,11,  2.0+13.0/24, 141.544578569,  15.617848688, 63.309978744, 5.455975, -0.660814,  16.7310},
    {2026,11,  5.0+15.0/24, 179.088967271,  -3.027999049, 45.752490990, 6.148490,  4.047643,  22.2230},
    {2026,11,  8.0+17.0/24, 215.637261109, -19.805560196, 29.528772757, 5.014328,  6.842619,  18.4543},
    {2026,11, 14.0+21.0/24, 296.445554499, -24.641524309, 18.113800766, 357.976821, 4.199965, 350.9679},
    {2026,11, 17.0+23.0/24, 332.952929695, -11.427914470, 26.020167086, 354.148889, 0.126366, 340.7276},
    {2026,11, 21.0+ 1.0/24,   8.697887422,   6.933100634, 36.230243991, 353.361072,-4.011652, 337.9250},
    {2026,11, 24.0+ 3.0/24,  51.638725403,  23.409173684, 36.358013870, 357.242280,-6.014503, 345.7927},
};

static const LunarObserver kPalomar = {33.354136, 243.1375, 1704.89, true};

static const LunarFeature* Find(const char* name) {
    for (const auto& f : kLunarFeatures)
        if (std::strcmp(f.name, name) == 0) return &f;
    return nullptr;
}

int main() {
    printf("Lunar features: the Moon from here, and places on it\n");

    printf("\n1. The series, against Meeus example 47.a\n");
    {
        // 1992 April 12, 0h TD.
        const double jde = 2448724.5;
        const LunarEcliptic m = MoonEclipticAt(jde);
        Near(m.sigmaL, -1127527.0, 2.0, "sum l");
        Near(m.sigmaB, -3229126.0, 500.0, "sum b (table truncated at 107)");
        Near(m.sigmaR, -16590875.0, 2.0, "sum r");
        Near(m.lonDeg, 133.162655, 0.00001, "longitude");
        Near(m.latDeg, -3.229126, 0.0005, "latitude");
        Near(m.distKm, 368409.7, 0.1, "distance");
    }

    printf("\n2. Libration, against Meeus example 53.a\n");
    {
        // Same moment, from the geocentre. His optical libration, in
        // ecliptic-of-date terms; ours has no physical term to add.
        const double jdUt = 2448724.5 - 59.0 / 86400.0;
        const LunarView v = LunarViewAt(jdUt, LunarObserver{});
        Near(v.subEarthLonDeg, -1.206, 0.05, "optical libration in longitude");
        Near(v.subEarthLatDeg,  4.194, 0.05, "optical libration in latitude");
        Near(v.poleAngleDeg,   15.08,  0.1,  "position angle of the axis");
        Near(v.subSolarLonDeg, 67.89,  0.2,  "sub-solar longitude");
        Near(v.subSolarLatDeg,  1.46,  0.1,  "sub-solar latitude");
    }

    printf("\n3. The geocentre, against Horizons\n");
    {
        double worstPos = 0, worstLib = 0, worstSun = 0, worstNp = 0;
        for (const auto& r : kGeo) {
            const double jd = CalcJD(r.y, r.mo, r.day);
            const LunarView v = LunarViewAt(jd, LunarObserver{});
            const double s = SepArcsec(v.raHours, v.decDeg, r.ra, r.dec);
            if (s > worstPos) worstPos = s;
            const double dl = AngDiff(v.subEarthLonDeg, r.subLon);
            const double db = std::fabs(v.subEarthLatDeg - r.subLat);
            if (dl > worstLib) worstLib = dl;
            if (db > worstLib) worstLib = db;
            const double sl = AngDiff(v.subSolarLonDeg, r.sunLon);
            const double sb = std::fabs(v.subSolarLatDeg - r.sunLat);
            if (sl > worstSun) worstSun = sl;
            if (sb > worstSun) worstSun = sb;
            const double np = AngDiff(v.poleAngleDeg, r.np);
            if (np > worstNp) worstNp = np;
        }
        printf("  worst: position %.1f\", libration %.3f deg, sub-solar "
               "%.3f deg, pole %.3f deg\n",
               worstPos, worstLib, worstSun, worstNp);
        Check(worstPos < 25.0, "position within 25\", the series' own limit");
        Check(worstLib < 0.1,  "libration within 0.1 deg");
        Check(worstSun < 0.15, "sub-solar point within 0.15 deg");
        Check(worstNp  < 0.2,  "pole angle within 0.2 deg, no physical libration");
    }

    printf("\n4. From a real site, against Horizons\n");
    {
        double worstPos = 0, worstLib = 0, worstOld = 0, smallestOld = 1e9;
        for (const auto& r : kTopo) {
            const double jd = CalcJD(r.y, r.mo, r.day);
            const LunarView v = LunarViewAt(jd, kPalomar);
            const double s = SepArcsec(v.raHours, v.decDeg, r.ra, r.dec);
            if (s > worstPos) worstPos = s;
            const double dl = AngDiff(v.subEarthLonDeg, r.subLon);
            const double db = std::fabs(v.subEarthLatDeg - r.subLat);
            if (dl > worstLib) worstLib = dl;
            if (db > worstLib) worstLib = db;

            // The size of what was wrong before this phase.
            const LunarView g = LunarViewAt(jd, LunarObserver{});
            const double old = SepArcsec(g.raHours, g.decDeg, r.ra, r.dec);
            if (old > worstOld) worstOld = old;
            if (old < smallestOld) smallestOld = old;
        }
        printf("  worst: position %.1f\", libration %.3f deg\n",
               worstPos, worstLib);
        printf("  the geocentric figure was %.1f' to %.1f' away\n",
               smallestOld / 60.0, worstOld / 60.0);
        Check(worstPos < 25.0, "position within 25\"");
        Check(worstLib < 0.15, "topocentric libration within 0.15 deg");
        Check(worstOld > 15.5 * 60.0,
              "the old answer really was off the disc at some altitude");
    }

    printf("\n5. Places on the surface\n");
    {
        const double jd = CalcJD(2026, 11, 17.0 + 23.0 / 24);
        const LunarView v = LunarViewAt(jd, kPalomar);

        // The sub-observer point is the middle of the disc, by definition.
        const LunarSpot mid = LunarSpotAt(v, v.subEarthLatDeg, v.subEarthLonDeg);
        Check(std::fabs(mid.eastArcsec) < 0.5 && std::fabs(mid.northArcsec) < 0.5,
              "the sub-observer point sits at the disc centre");
        Near(mid.viewAltDeg, 90.0, 0.1, "and we are overhead there");

        // The sub-solar point has the Sun overhead.
        const LunarSpot sub = LunarSpotAt(v, v.subSolarLatDeg, v.subSolarLonDeg);
        Near(sub.sunAltDeg, 90.0, 0.1, "the Sun is overhead at the sub-solar point");

        // Which way round. Selenographic east is WEST on the sky.
        const auto* crisium = Find("Mare Crisium");
        const auto* grimaldi = Find("Grimaldi");
        const auto* plato = Find("Plato");
        const auto* clavius = Find("Clavius");
        Check(crisium && grimaldi && plato && clavius, "the named features exist");

        // Rotate by the pole to test against the Moon's own axes: the
        // disc can stand at any angle on the sky.
        auto moonFrame = [&](const LunarSpot& s, double& right, double& up) {
            const double p = DegToRad(v.poleAngleDeg);
            up    =  s.northArcsec * std::cos(p) + s.eastArcsec * std::sin(p);
            right = -s.northArcsec * std::sin(p) + s.eastArcsec * std::cos(p);
        };
        double ri, up;
        moonFrame(LunarSpotAt(v, crisium->latDeg, crisium->lonDeg), ri, up);
        Check(ri < -500.0, "Mare Crisium lies toward the sky's west");
        moonFrame(LunarSpotAt(v, grimaldi->latDeg, grimaldi->lonDeg), ri, up);
        Check(ri > 500.0, "Grimaldi lies toward the sky's east");
        moonFrame(LunarSpotAt(v, plato->latDeg, plato->lonDeg), ri, up);
        Check(up > 500.0, "Plato is toward the Moon's north");
        moonFrame(LunarSpotAt(v, clavius->latDeg, clavius->lonDeg), ri, up);
        Check(up < -500.0, "Clavius is toward the Moon's south");

        // The whole disc stays on the disc.
        const double radius = ApparentDiameterArcsec(MOON_RADIUS_KM, v.distKm) / 2.0;
        double far = 0.0;
        for (double lon = -80; lon <= 80; lon += 10)
            for (double lat = -80; lat <= 80; lat += 10) {
                const LunarSpot s = LunarSpotAt(v, lat, lon);
                if (!s.visible) continue;
                const double r = std::hypot(s.eastArcsec, s.northArcsec);
                if (r > far) far = r;
            }
        Check(far <= radius + 1.0, "no visible point lies outside the limb");

        // A sensible separation: Copernicus and Tycho by the great
        // circle is 54.4 deg of Moon, and near the centre of the disc
        // the sky distance is close to the chord.
        const auto* cop = Find("Copernicus");
        const auto* tyc = Find("Tycho");
        Check(cop && tyc, "Copernicus and Tycho exist");
        const LunarSpot a = LunarSpotAt(v, cop->latDeg, cop->lonDeg);
        const LunarSpot b = LunarSpotAt(v, tyc->latDeg, tyc->lonDeg);
        const double sep = std::hypot(a.eastArcsec - b.eastArcsec,
                                      a.northArcsec - b.northArcsec);
        const double arcsecPerKm = ARCSEC_PER_RAD / v.distKm;
        Check(sep > 1200.0 * arcsecPerKm && sep < 1700.0 * arcsecPerKm,
              "Copernicus to Tycho is a plausible distance on the sky");
    }

    printf("\n6. Lighting\n");
    {
        // Horizons has the Sun 84.3 deg of selenographic longitude from
        // the sub-Earth point here, 5.7 deg past first quarter.
        const double fq = CalcJD(2026, 11, 17.0 + 23.0 / 24);
        const LunarView v = LunarViewAt(fq, kPalomar);
        Near(LunarPhaseFraction(fq), 0.2658, 0.005, "just past first quarter");

        const auto* crisium = Find("Mare Crisium");
        const auto* grimaldi = Find("Grimaldi");
        const LunarSpot lit = LunarSpotAt(v, crisium->latDeg, crisium->lonDeg);
        const LunarSpot dark = LunarSpotAt(v, grimaldi->latDeg, grimaldi->lonDeg);
        Check(lit.sunAltDeg > 0.0, "a waxing Moon lights Crisium");
        Check(dark.sunAltDeg < 0.0, "and leaves Grimaldi in the dark");

        // Colongitude is the morning terminator's selenographic
        // longitude, measured west, so a feature at -colongitude is on
        // the line and its Sun is on the horizon.
        const LunarSpot term = LunarSpotAt(v, 0.0, -v.colongitudeDeg);
        Near(term.sunAltDeg, 0.0, 2.0, "the Sun is on the horizon at the terminator");

        // Sunrise, predicted and then checked by waiting for it.
        const double days = DaysToSunrise(v, grimaldi->lonDeg);
        const LunarView later = LunarViewAt(fq + days, kPalomar);
        const LunarSpot rise = LunarSpotAt(later, grimaldi->latDeg, grimaldi->lonDeg);
        Near(rise.sunAltDeg, 0.0, 1.5, "the predicted sunrise is a sunrise");

        const std::string night = DescribeLunarSpot("Grimaldi", dark, v,
                                                    grimaldi->lonDeg, 235.0);
        printf("  %s\n", night.c_str());
        Check(night.find("darkness") != std::string::npos, "it says so when it is dark");
        const std::string day = DescribeLunarSpot("Crisium", lit, v,
                                                  crisium->lonDeg, 556.0);
        printf("  %s\n", day.c_str());
        Check(day.find("Sun") != std::string::npos, "and gives the Sun when it is not");

        LunarSpot far = lit; far.visible = false; far.viewAltDeg = -3.0;
        const std::string past = DescribeLunarSpot("Mare Orientale", far, v, -94.67, 294.0);
        printf("  %s\n", past.c_str());
        Check(past.find("limb") != std::string::npos, "and when libration has hidden it");
    }

    printf("\n7. The catalogue\n");
    {
        const int n = (int)(sizeof(kLunarFeatures) / sizeof(kLunarFeatures[0]));
        Check(n >= 90, "the table has what was generated");
        bool sane = true;
        for (const auto& f : kLunarFeatures)
            if (std::fabs(f.latDeg) > 90.0 || std::fabs(f.lonDeg) > 180.0 ||
                f.sizeKm <= 0.0) sane = false;
        Check(sane, "every coordinate is in range");

        // Published gazetteer values, as a guard on the generator.
        const auto* c = Find("Copernicus");
        Check(c && c->latDeg == 9.62 && c->lonDeg == -20.08, "Copernicus 9.62N 20.08W");
        const auto* t = Find("Tycho");
        Check(t && t->latDeg == -43.30 && t->lonDeg == -11.22, "Tycho 43.30S 11.22W");

        SkyObjectCatalog cat;
        cat.SetObserver(kPalomar);
        const double jd = CalcJD(2026, 11, 17.0 + 23.0 / 24);
        cat.UpdatePlanets(jd);

        auto hits = cat.Search("Copernicus");
        Check(!hits.empty() && hits[0]->name == "Moon - Copernicus",
              "a search for the crater finds it");

        const LunarView v = LunarViewAt(jd, kPalomar);
        const SkyObject* moon = nullptr;
        const SkyObject* cop = nullptr;
        int inMap = 0;
        for (const auto& o : cat.Objects()) {
            if (o.type == "Moon") moon = &o;
            if (o.name == "Moon - Copernicus") cop = &o;
            if (o.isLunarFeature() && o.magnitude < 12.0f) ++inMap;
        }
        Check(moon && SepArcsec(moon->ra, moon->dec, v.raHours * 15.0, v.decDeg) < 0.01,
              "the catalogue's Moon is the topocentric one");
        printf("  Copernicus is %.0f\" from the centre\n",
               SepArcsec(cop->ra, cop->dec, moon->ra * 15.0, moon->dec));
        Check(cop && SepArcsec(cop->ra, cop->dec, moon->ra * 15.0, moon->dec) > 200.0,
              "and a crater is not simply the middle of it");
        Check(inMap == 0, "no feature reaches the star lists");

        // MoonRADec keeps its old meaning for whoever only wants a phase.
        double ra, dec, ph;
        MoonRADec(jd, ra, dec, ph);
        Near(ph, LunarPhaseFraction(jd), 1e-12, "MoonRADec's phase");
    }

    printf("\n%s\n", g_fail ? "FAILED" : "All passed");
    return g_fail ? 1 : 0;
}
