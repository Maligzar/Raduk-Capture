// ============================================================
//  Phase 140.2: the per-planet exposure band
//
//  The owner's audit (140.0) found Saturn recorded at 0.1 to 2 ms with
//  gain doing the work: frames bright on screen and thin in signal.
//  Each planet profile now carries a band, and auto-exposure's ladder is
//  narrowed to it:
//
//    1. The rail: floor and ceiling from the band, the exposure knee at
//       the ceiling, the gain knee at the HCG floor.
//    2. More light: exposure climbs to the ceiling first, then gain.
//    3. Less light: gain falls to the HCG floor first, then exposure,
//       and never below the band's floor.
//    4. Every stock planet band sits inside the planetary rail; profiles
//       without a band (DSO, star, focus) keep their type's rail; a band
//       outside the rail is ignored rather than inverted.
// ============================================================

#include "profiles/TargetProfile.h"
#include "core/LightLadder.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace raduk::profiles;
using raduk::core::LightLadder;
using raduk::core::LightPoint;
using raduk::core::WalkLadder;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}
static std::string F(double v, int d = 2) { char b[48]; snprintf(b, sizeof(b), "%.*f", d, v); return b; }

static LightLadder LadderFrom(const CameraRails::ExposureRail& r) {
    LightLadder l;
    l.expMinMs = r.minMs; l.expKneeMs = r.expKneeMs; l.expMaxMs = r.maxMs;
    l.gainFloor = r.gainFloor; l.gainKnee = r.gainKnee; l.gainMax = r.gainMax;
    return l;
}

int main() {
    printf("Exposure band (phase 140.2)\n");

    printf("\n1. Saturn's rail\n");
    const TargetProfile sat = DefaultSaturn();
    const auto r = sat.AERail();
    Check(sat.HasBand(), "Saturn has a band", F(sat.bandFloorMs, 1) + "-" + F(sat.bandCeilMs, 1) + " ms");
    Check(r.minMs == sat.bandFloorMs && r.maxMs == sat.bandCeilMs, "floor and ceiling from the band");
    Check(r.expKneeMs == r.maxMs, "the exposure knee is the ceiling");
    Check(r.gainKnee == r.gainFloor && r.gainFloor == CameraRails::GAIN_HCG_THRESHOLD,
          "the gain knee is the HCG floor", std::to_string(r.gainKnee));
    const LightLadder L = LadderFrom(r);

    printf("\n2. More light: exposure first\n");
    {
        const LightPoint p = WalkLadder(L, r.minMs, r.gainFloor, 2.0);
        const double expStops = std::log2(r.maxMs / r.minMs);
        const int wantGain = (int)std::lround(r.gainFloor + (2.0 - expStops) * raduk::core::GAIN_UNITS_PER_STOP);
        Check(std::fabs(p.expMs - r.maxMs) < 1e-6, "exposure goes to the ceiling", F(p.expMs) + " ms");
        Check(std::abs(p.gain - wantGain) <= 1, "and only the rest goes to gain", std::to_string(p.gain));
        const LightPoint q = WalkLadder(L, r.minMs, r.gainFloor, 1.0);
        Check(q.gain == r.gainFloor && q.expMs > r.minMs && q.expMs < r.maxMs,
              "a smaller correction is exposure alone", F(q.expMs) + " ms, gain " + std::to_string(q.gain));
    }

    printf("\n3. Less light: gain first, never under the floor\n");
    {
        const LightPoint p = WalkLadder(L, r.maxMs, 300, -3.0);
        Check(p.gain == r.gainFloor, "gain falls to the HCG floor first", std::to_string(p.gain));
        Check(p.expMs < r.maxMs && p.expMs > r.minMs, "then exposure shortens", F(p.expMs) + " ms");
        const LightPoint d = WalkLadder(L, r.minMs, r.gainFloor, -5.0);
        Check(d.expMs >= r.minMs - 1e-9, "exposure never goes under the band's floor", F(d.expMs) + " ms");
        Check(d.gain < r.gainFloor, "past that, gain leaves HCG: too bright for the band",
              std::to_string(d.gain));
    }

    printf("\n4. Every profile\n");
    for (auto fn : {DefaultJupiter, DefaultSaturn, DefaultMars, DefaultVenus, DefaultMercury,
                    DefaultUranus, DefaultNeptune, DefaultMoon}) {
        const TargetProfile p = fn();
        const auto ar = p.AERail();
        const auto tr = CameraRails::RailsFor(p.targetType);
        Check(p.HasBand() && ar.minMs == p.bandFloorMs && ar.maxMs == p.bandCeilMs &&
              ar.minMs >= tr.minMs && ar.maxMs <= tr.maxMs,
              p.name + ": band inside its rail", F(p.bandFloorMs, 1) + "-" + F(p.bandCeilMs, 1) + " ms");
    }
    {
        const TargetProfile moon = DefaultMoon();
        Check(moon.AERail().gainFloor < CameraRails::GAIN_HCG_THRESHOLD,
              "the Moon keeps its low gain floor", std::to_string(moon.AERail().gainFloor));
    }
    for (auto fn : {DefaultDSO, DefaultStar, DefaultPlanetFocus}) {
        const TargetProfile p = fn();
        const auto ar = p.AERail(), tr = CameraRails::RailsFor(p.targetType);
        Check(!p.HasBand() && ar.minMs == tr.minMs && ar.maxMs == tr.maxMs && ar.expKneeMs == tr.expKneeMs,
              p.name + ": no band, the type's rail as before");
    }
    {
        TargetProfile odd = DefaultSaturn();
        odd.bandFloorMs = 200; odd.bandCeilMs = 400;   // outside the planetary rail
        const auto ar = odd.AERail(), tr = CameraRails::RailsFor(odd.targetType);
        Check(ar.minMs == tr.minMs && ar.maxMs == tr.maxMs, "a band outside the rail is ignored, not inverted");
        TargetProfile inv = DefaultSaturn();
        inv.bandFloorMs = 20; inv.bandCeilMs = 10;
        Check(!inv.HasBand(), "a floor above the ceiling is no band");
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
