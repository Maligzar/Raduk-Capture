// ============================================================
//  The exposure rails, enforced (phase 100)
//
//  Phase 99's sweep found all four practical exposure limits read by
//  NOTHING, so a planetary profile could carry 500 ms and the
//  application would not say a word. The owner's call was to enforce
//  them.
//
//  Enforcing them turned up how many copies of the planet ceiling
//  there were. FOUR values for one concept:
//
//      EXP_PLANET_MAX_MS                       50   TargetProfile.h
//      AutoExposureAssistant::Config default   50
//      main.cpp's aeCfg.expMaxMs               40   <-- the live one
//      a comment in ControlPanel.cpp            50
//
//  So the documented figure was contradicted by the only one that
//  ran, and the slider's own range comment cited the wrong number.
//
//  And it put targetType to work -- a field every stock profile sets
//  and which nothing read, the fifth dead thing the sweep found.
//
//  THE SPLIT THIS FILE PINS: advisory for a human, hard for the
//  controller. These are practical limits with physical reasons, not
//  hardware bounds; a 500 ms planetary exposure is almost certainly a
//  mistake and just possibly deliberate on a very dim target, so the
//  honest response to a person is to say so rather than overrule
//  them. The auto-exposure assistant has no judgement to respect, so
//  for it the same rail is a clamp.
// ============================================================

#include "profiles/TargetProfile.h"

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

using namespace raduk::profiles;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (ok) printf("  ok    %s\n", what);
    else  { printf("  FAIL  %s\n", what); ++g_fail; }
}

int main() {
    printf("Exposure rails: one source, advisory for a human\n");

    // ───────────────────────────────────────────────────────────
    printf("\n1. Every target type gets the right rail\n");
    {
        struct C { const char* type; double lo, hi; bool advisory; };
        const C cases[] = {
            { "planet", CameraRails::EXP_PLANET_MIN_MS,
                        CameraRails::EXP_PLANET_MAX_MS, true },
            { "moon",   CameraRails::EXP_PLANET_MIN_MS,
                        CameraRails::EXP_PLANET_MAX_MS, true },
            { "star",   CameraRails::EXP_PLANET_MIN_MS,
                        CameraRails::EXP_PLANET_MAX_MS, true },
            { "dso",    CameraRails::EXP_DSO_MIN_MS,
                        CameraRails::EXP_DSO_MAX_MS,    true },
        };
        bool right = true;
        for (const C& c : cases) {
            const auto r = CameraRails::RailsFor(c.type);
            printf("  --    %-7s %8.3g - %-8.3g ms  %s\n",
                   c.type, r.minMs, r.maxMs, r.why);
            if (r.minMs != c.lo || r.maxMs != c.hi ||
                r.advisory != c.advisory) right = false;
        }
        Check(right, "planet, moon and star share the lucky-imaging rail;"
                     " dso gets its own");
        Check(std::string(CameraRails::RailsFor("planet").why).size() > 20,
              "and each carries the REASON, so the warning can say why"
              " rather than just no");

        // An unrecognised type is not licence to invent a limit for it.
        const auto unknown = CameraRails::RailsFor("comet");
        printf("  --    unknown type -> %.3g - %.3g ms, advisory=%d\n",
               unknown.minMs, unknown.maxMs, (int)unknown.advisory);
        Check(!unknown.advisory,
              "an unrecognised target type gets no opinion at all");
        Check(unknown.minMs == CameraRails::EXP_MIN_MS &&
              unknown.maxMs == CameraRails::EXP_MAX_MS,
              "only the hardware bounds, which are separately enforced");
        Check(!CameraRails::RailsFor("").advisory,
              "and so does an empty one");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n2. THE REGRESSION GUARD: the shipped profiles all pass\n");
    {
        // If the application's own stock profiles violated the rails,
        // enforcement would be wrong rather than the profiles. They
        // do not -- measured at 8 of 8 before any of this was built --
        // and this is the assertion that keeps it that way, because a
        // rail that the shipped defaults trip is a rail nobody will
        // believe.
        const std::vector<TargetProfile> stock = {
            DefaultJupiter(), DefaultSaturn(), DefaultMars(),
            DefaultVenus(),   DefaultMoon(),   DefaultDSO(),
            DefaultStar(),    DefaultPlanetFocus(),
        };
        int checked = 0, warned = 0;
        for (const auto& p : stock) {
            const std::string w = p.ExposureWarning();
            printf("  --    %-14s %-7s %10.3g ms   %s\n",
                   p.name.c_str(), p.targetType.c_str(), p.exposureMs,
                   w.empty() ? "inside its rail" : "OUTSIDE");
            ++checked;
            if (!w.empty()) ++warned;
        }
        printf("  --    %d stock profiles checked, %d warned\n",
               checked, warned);
        Check(checked >= 8, "the stock profiles were actually loaded");
        Check(warned == 0,
              "and NONE of them trips its own rail, so enforcement"
              " accuses nothing the project ships");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n3. A profile outside its rail is told so, and applied\n");
    {
        TargetProfile p;
        p.name = "Jupiter"; p.targetType = "planet"; p.exposureMs = 500.0;
        const std::string w = p.ExposureWarning();
        printf("  --    %s\n", w.c_str());
        Check(!w.empty(), "500 ms on a planet warns");
        Check(w.find("500") != std::string::npos,
              "and names the offending figure");
        Check(w.find("50") != std::string::npos,
              "and the rail it is outside");
        Check(w.find("lucky imaging") != std::string::npos,
              "and the reason, which is the part that teaches");
        Check(w.find("Applied as asked") != std::string::npos,
              "and says plainly that it is NOT being overruled -- these"
              " are practical limits, not hardware bounds");

        // Both ends, and the boundaries themselves.
        p.exposureMs = 0.5;
        Check(!p.ExposureWarning().empty(), "below the floor warns too");
        p.exposureMs = CameraRails::EXP_PLANET_MIN_MS;
        Check(p.ExposureWarning().empty(), "exactly on the floor does not");
        p.exposureMs = CameraRails::EXP_PLANET_MAX_MS;
        Check(p.ExposureWarning().empty(), "nor exactly on the ceiling");
        p.exposureMs = 12.0;
        Check(p.ExposureWarning().empty(), "nor anything comfortably inside");

        // A DSO at a planetary exposure is the error the other way,
        // and the one that matters most: a 10 ms deep-sky sub is not
        // an exposure, it is an empty file.
        TargetProfile d;
        d.name = "M42"; d.targetType = "dso"; d.exposureMs = 10.0;
        const std::string dw = d.ExposureWarning();
        printf("  --    %s\n", dw.c_str());
        Check(!dw.empty() && dw.find("deep sky") != std::string::npos,
              "a 10 ms deep-sky sub warns, with the deep-sky reason");

        // And an unrecognised type stays silent however odd the value.
        TargetProfile c;
        c.name = "Comet"; c.targetType = "comet"; c.exposureMs = 500.0;
        Check(c.ExposureWarning().empty(),
              "while an unrecognised type is never accused, because no"
              " rail was ever decided for it");
    }

    // ───────────────────────────────────────────────────────────
    printf("\n4. The rails are the single source the four copies were not\n");
    {
        // The whole point. If anyone restates a bound instead of asking
        // for it, this is where it shows.
        const auto planet = CameraRails::RailsFor("planet");
        Check(planet.maxMs == CameraRails::EXP_PLANET_MAX_MS &&
              planet.minMs == CameraRails::EXP_PLANET_MIN_MS,
              "RailsFor returns the constants rather than its own copy");

        // The planet rail must sit inside the hardware bounds, or the
        // advisory limit would be looser than what the camera can do.
        Check(planet.minMs >= CameraRails::EXP_MIN_MS &&
              planet.maxMs <= CameraRails::EXP_MAX_MS,
              "and inside the hardware bounds, not outside them");
        const auto dso = CameraRails::RailsFor("dso");
        Check(dso.minMs >= CameraRails::EXP_MIN_MS &&
              dso.maxMs <= CameraRails::EXP_MAX_MS,
              "as does the deep-sky rail");

        // The two advisory rails must not overlap, or "planet" and
        // "dso" would accept the same exposure and the distinction
        // would mean nothing.
        printf("  --    planet %.3g-%.3g ms | dso %.3g-%.3g ms\n",
               planet.minMs, planet.maxMs, dso.minMs, dso.maxMs);
        Check(planet.maxMs < dso.minMs,
              "and they do not overlap, so the target type genuinely"
              " decides something");
    }

    printf("\n%s\n", g_fail ? "FAILURES" : "all good");
    return g_fail ? 1 : 0;
}
