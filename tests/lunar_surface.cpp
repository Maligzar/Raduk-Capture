// ============================================================
//  Where the terminator is (phase 94.3)
//
//  The emulator rendered the Moon as a flat tinted disc. At 900
//  arcsec radius that overflows every sensor the rig owns, which
//  is right, but a uniform bright rectangle has no detail at any
//  focus position -- so as a focus target it was worse than a
//  planet, which at least has a limb.
//
//  Lunar focus is judged on the TERMINATOR. So the terminator is
//  the thing to get right, and it is the thing a bench can check:
//  it falls exactly where the illumination cosine crosses zero,
//  it sweeps across the disc as the phase advances, and it is a
//  great circle through the poles at the quarters.
//
//  Three ways to get this plausibly wrong, all pinned here:
//
//    Treating phase as an angle. Phase runs 0 to 1 over a synodic
//    month while the Sun-Moon-Earth angle runs 180 to 0 and back,
//    so a linear map puts the terminator in the wrong place for
//    half the month and exactly right at the quarters -- which is
//    where anyone would check it.
//
//    Forgetting that it is a sphere. Illumination depends on the
//    surface normal, which has a z component; using only the
//    in-plane offset makes the terminator a straight line across
//    the disc at every phase instead of an ellipse.
//
//    Shadows that do not care how low the Sun is. The same crater
//    is invisible at lunar noon and a hard black crescent near
//    sunrise, and that is the entire reason the terminator is
//    where focus gets judged.
//
//  What is NOT checked, because it is not modelled: the position
//  angle of the bright limb. See the header.
// ============================================================

#include "core/LunarSurface.h"

#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace raduk::core;

static int g_fail = 0;

static void Near(double got, double want, double tol, const char* what) {
    if (std::fabs(got - want) <= tol) printf("  ok    %s (%.6f)\n", what, got);
    else { printf("  FAIL  %s: got %.6f, wanted %.6f +/- %.6f\n",
                  what, got, want, tol); ++g_fail; }
}

static void Check(bool ok, const char* what) {
    if (ok) printf("  ok    %s\n", what);
    else  { printf("  FAIL  %s\n", what); ++g_fail; }
}

int main() {
    printf("Lunar surface: where the terminator is\n");

    // ---- the Sun's direction from the phase -------------------
    printf("\n1. Phase is not an angle\n");
    {
        // Full: the Sun is behind the observer, so straight down +z.
        const SunDir full = MoonSunDirection(0.5);
        Near(full.z, 1.0, 1e-9, "at full the Sun is behind the observer");
        Near(full.x, 0.0, 1e-9, "and has no sideways component");

        // New: behind the Moon.
        const SunDir nw = MoonSunDirection(0.0);
        Near(nw.z, -1.0, 1e-9, "at new it is behind the Moon");

        // Quarters: side on, and on OPPOSITE sides of each other.
        const SunDir q1 = MoonSunDirection(0.25);
        const SunDir q3 = MoonSunDirection(0.75);
        Near(q1.z, 0.0, 1e-9, "at first quarter it is side on");
        Near(q3.z, 0.0, 1e-9, "and at last quarter too");
        Near(std::fabs(q1.x), 1.0, 1e-9, "fully sideways at first quarter");
        Check(q1.x * q3.x < 0.0,
              "and the two quarters light opposite limbs, as waxing and waning do");

        // A linear phase-to-angle map would pass the quarters and
        // fail here, which is why this case exists.
        const SunDir g = MoonSunDirection(0.375);     // waxing gibbous
        Check(g.z > 0.0 && g.x > 0.0,
              "a waxing gibbous Moon is mostly lit and lit on the waxing side");
        Near(g.z, std::cos(45.0 * 3.14159265358979 / 180.0), 1e-9,
              "and its Sun-Moon-Earth angle is 45 deg, not 135");

        // Out of range must not produce a Sun pointing nowhere.
        const SunDir wrapped = MoonSunDirection(1.5);
        const SunDir same    = MoonSunDirection(0.5);
        Near(wrapped.z, same.z, 1e-12, "a phase of 1.5 wraps to 0.5");
    }

    // ---- illumination, and the terminator it implies ----------
    printf("\n2. The terminator is where the cosine crosses zero\n");
    {
        const SunDir fq = MoonSunDirection(0.25);   // first quarter

        // At a quarter the terminator runs through the disc CENTRE,
        // because the Sun is exactly side on.
        Near(LunarIllumination(0.0, 0.0, fq), 0.0, 1e-9,
             "at first quarter the disc centre sits on the terminator");

        // And the lit side is the Sun's side.
        const double lit   = LunarIllumination(+0.5, 0.0, fq);
        const double dark  = LunarIllumination(-0.5, 0.0, fq);
        Check((fq.x > 0.0) ? (lit > 0.0 && dark == 0.0)
                           : (dark > 0.0 && lit == 0.0),
              "one side is lit and the other is exactly zero, not merely dim");

        // Full Moon: the whole disc is lit, brightest at the centre,
        // falling to zero at the limb because the normal turns away.
        const SunDir fu = MoonSunDirection(0.5);
        Near(LunarIllumination(0.0, 0.0, fu), 1.0, 1e-9,
             "at full the disc centre is fully lit");
        Near(LunarIllumination(0.99, 0.0, fu), std::sqrt(1.0 - 0.99*0.99), 1e-9,
             "and the limb falls off as the sphere turns away, not as a flat disc");
        Check(LunarIllumination(0.0, 0.0, fu) > LunarIllumination(0.8, 0.0, fu),
              "so a full Moon is still centre-bright");

        // New Moon: nothing lit at all.
        const SunDir nw = MoonSunDirection(0.0);
        Near(LunarIllumination(0.0, 0.0, nw), 0.0, 1e-12, "a new Moon is dark");
        Near(LunarIllumination(0.5, 0.3, nw), 0.0, 1e-12, "everywhere");

        // Off the limb is refused, not reported as night.
        Check(LunarIllumination(1.2, 0.0, fu) < 0.0,
              "a point beyond the limb is off the disc, not a dark part of it");
    }

    // ---- the terminator moves, and the right way --------------
    printf("\n3. It sweeps across the disc as the month runs\n");
    {
        // Find where illumination crosses zero along the equator,
        // for a series of waxing phases. The crossing must march
        // monotonically from one limb to the other.
        // Direction is NOT asserted. The lit region grows from a
        // sliver at one limb to the whole disc, so the crossing
        // marches steadily one way -- but WHICH way depends on the
        // handedness convention in MoonSunDirection, which is the one
        // thing here a bench cannot settle. So this checks that it
        // moves consistently, and the lit fraction below checks that
        // it moves the right way in the sense that matters.
        double prev = 2.0;
        bool monotone = true;
        printf("  --    phase -> terminator u along the equator\n");
        for (double p : { 0.10, 0.20, 0.30, 0.40, 0.48 }) {
            const SunDir s = MoonSunDirection(p);
            // Scan for the sign change.
            double cross = -2.0;
            for (int i = -999; i < 999; ++i) {
                const double u0 = i / 1000.0, u1 = (i + 1) / 1000.0;
                const double a = LunarIllumination(u0, 0.0, s);
                const double b = LunarIllumination(u1, 0.0, s);
                if (a <= 0.0 && b > 0.0) { cross = u0; break; }
            }
            printf("  --    %.2f -> %+.3f\n", p, cross);
            if (cross >= prev) monotone = false;
            prev = cross;
        }
        Check(monotone,
              "the terminator marches steadily one way as the Moon waxes");
        Check(prev < -0.9,
              "and by nearly full it has reached the far limb");

        // A thin crescent lights only a sliver; a gibbous Moon most
        // of the disc. Measured as the lit fraction along the equator.
        auto litFraction = [](double p) {
            const SunDir s = MoonSunDirection(p);
            int lit = 0, tot = 0;
            for (int i = -999; i <= 999; ++i) {
                const double u = i / 1000.0;
                const double c = LunarIllumination(u, 0.0, s);
                if (c < 0.0) continue;
                ++tot; if (c > 0.0) ++lit;
            }
            return tot ? (double)lit / tot : 0.0;
        };
        const double cres = litFraction(0.10);
        const double quar = litFraction(0.25);
        const double gibb = litFraction(0.40);
        printf("  --    lit fraction: crescent %.3f, quarter %.3f, gibbous %.3f\n",
               cres, quar, gibb);
        Check(cres < quar && quar < gibb, "and more of it is lit as it waxes");
        Near(quar, 0.5, 0.01, "with the quarter lighting exactly half");
    }

    // ---- lat/lon, for placing features -----------------------
    printf("\n4. The patch knows where it is on the sphere\n");
    {
        double lat = 0, lon = 0;
        LunarLatLon(0.0, 0.0, lat, lon);
        Near(lat, 0.0, 1e-9, "disc centre is the sub-observer point, lat 0");
        Near(lon, 0.0, 1e-9, "and lon 0");

        LunarLatLon(0.0, -0.5, lat, lon);
        Check(lat > 0.0, "upward on the sensor is northern latitude");
        LunarLatLon(0.0, +0.5, lat, lon);
        Check(lat < 0.0, "and downward is southern");

        LunarLatLon(0.7, 0.0, lat, lon);
        Check(lon > 0.0, "rightward is increasing longitude");
        Near(lat, 0.0, 1e-9, "with no latitude change along the equator");

        // Longitude compresses toward the limb, which is the whole
        // reason features bunch up there.
        double lonA = 0, lonB = 0, d = 0;
        LunarLatLon(0.30, 0.0, d, lonA);
        LunarLatLon(0.60, 0.0, d, lonB);
        Check((lonB - lonA) > lonA,
              "longitude opens faster near the limb, so features foreshorten");
    }

    // ---- maria ------------------------------------------------
    printf("\n5. The maria are darker, with soft edges\n");
    {
        // Centre of Imbrium against highland well away from any mare.
        const double mare = LunarAlbedo(23.0, -31.0);
        const double high = LunarAlbedo(-60.0, 120.0);
        printf("  --    mare albedo %.3f, highland %.3f\n", mare, high);
        Check(mare < high, "a mare is darker than highland");
        Near(high, 1.0, 1e-9, "highland is the reference value");
        Check(mare > 0.15, "and a mare is dark, not black");

        // Soft edge: stepping out of a mare must change gradually.
        const double a = LunarAlbedo(23.0, -31.0);
        const double b = LunarAlbedo(23.0, -31.0 + 8.0);
        const double c = LunarAlbedo(23.0, -31.0 + 16.0);
        Check(a < b && b < c,
              "albedo rises smoothly out of the mare rather than stepping");
    }

    // ---- crater shadows, which is the point -------------------
    printf("\n6. Shadows lengthen as the Sun gets low\n");
    {
        const SunDir fq = MoonSunDirection(0.25);
        const double cell = 6.0;

        // Hunt for a cell that actually carries a crater, then
        // compare its shading at high Sun and near the terminator.
        bool found = false;
        double noonSpread = 0.0, grazeSpread = 0.0;
        for (int i = 0; i < 400 && !found; ++i) {
            const double lat = -40.0 + (i % 20) * 4.0;
            const double lon =  10.0 + (i / 20) * 4.0;
            double lo = 2.0, hi = 0.0, lo2 = 2.0, hi2 = 0.0;
            for (double dl = -3.0; dl <= 3.0; dl += 0.25)
                for (double dn = -3.0; dn <= 3.0; dn += 0.25) {
                    const double sNoon  = LunarCraterShade(lat+dl, lon+dn, fq, 0.95, cell);
                    const double sGraze = LunarCraterShade(lat+dl, lon+dn, fq, 0.05, cell);
                    if (sNoon  < lo ) lo  = sNoon;   if (sNoon  > hi ) hi  = sNoon;
                    if (sGraze < lo2) lo2 = sGraze;  if (sGraze > hi2) hi2 = sGraze;
                }
            if (hi2 - lo2 > 0.2) {
                found = true;
                noonSpread  = hi  - lo;
                grazeSpread = hi2 - lo2;
            }
        }
        Check(found, "a crater exists somewhere on the surface");
        if (found) {
            printf("  --    contrast across the crater: high Sun %.3f, grazing %.3f\n",
                   noonSpread, grazeSpread);
            Check(grazeSpread > noonSpread * 2.0,
                  "and near the terminator it has over twice the contrast");
        }

        // Craters are stable: the same place is the same crater.
        Near(LunarCraterShade(12.0, 34.0, fq, 0.5, cell),
             LunarCraterShade(12.0, 34.0, fq, 0.5, cell), 0.0,
             "the surface does not move between calls");

        // Not every cell has one, or the Moon would be gravel.
        int plain = 0;
        for (int i = 0; i < 200; ++i)
            if (LunarCraterShade(i * 1.7 - 80.0, i * 2.3 - 100.0,
                                 fq, 0.5, cell) == 1.0) ++plain;
        Check(plain > 40, "and much of the surface is plain ground");

        // A Sun directly behind the observer casts no shadow at all.
        const SunDir fu = MoonSunDirection(0.5);
        Near(LunarCraterShade(12.0, 34.0, fu, 0.5, cell), 1.0, 1e-12,
             "with the Sun overhead there is no shadow anywhere");
    }

    if (g_fail == 0) printf("\nPASS\n"); else printf("\nFAIL: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
