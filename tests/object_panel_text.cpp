// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// ============================================================
//  What the object panel tells people
//
//  Phase 92b. These are the sentences a stranger reads off a screen
//  at a star party and believes, which makes them exactly the kind of
//  claim this project gates with a test. The numbers are checked
//  against values worked out by hand, not against whatever the code
//  happens to produce.
//
//  The panel's rule is that every element is a real measurement, a
//  real fact or a label, and that an absent fact is not displayed.
//  Most of what follows is checking the second half of that.
// ============================================================

#include "gui/ObjectPanel.h"

#include <cstdio>
#include <string>
#include <cmath>

using namespace raduk::gui::objectpanel;

static int g_fail = 0;

static void Check(bool ok, const std::string& what, const std::string& detail) {
    printf("  [%s] %-56s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static bool Has(const std::string& h, const std::string& n) {
    return h.find(n) != std::string::npos;
}

int main() {
    printf("\n=== What the object panel tells people ===\n");

    // ── 1. Light travel ──────────────────────────────────────
    //
    // The catalogue stores solar-system distances in light-years, so
    // Jupiter is 6.86e-05 ly. That is 6.86e-05 x 525,960 minutes in a
    // light-year = 36.1 minutes, which is right for Jupiter near
    // opposition.
    printf("\n1. Light travel time\n");
    {
        const std::string s = LightTravel(6.86e-05);
        Check(Has(s, "36 minutes"), "Jupiter's distance reads as 36 minutes", s);
    }
    {
        // The Moon is 384,400 km = 1.282 light-seconds = 4.063e-08 ly.
        // The database said 1.07e-08, a third of that, and the panel
        // duly printed "0 seconds ago" -- which is how the bad value
        // was noticed. Both the builder and the shipped database are
        // corrected; this checks the formatting as well as the number.
        const std::string s = LightTravel(4.063e-08);
        Check(Has(s, "1.3 seconds"),
              "the Moon reads 1.3 seconds, not 0 and not 1", s);
    }
    {
        // Neptune: 4.73e-04 ly = 248.7 minutes = 4.1 hours.
        const std::string s = LightTravel(4.73e-04);
        Check(Has(s, "hours"), "Neptune crosses into hours", s);
    }
    {
        // M31 at 2.54 million light-years. "2541 thousand years ago"
        // is not a number anybody says out loud.
        const std::string s = LightTravel(2.54e6);
        Check(Has(s, "2.5 million years"),
              "M31 reads in millions, not thousands of thousands", s);
    }
    {
        Check(LightTravel(0.0).empty(),
              "no distance produces no sentence, not a zero one", "");
        Check(LightTravel(-1.0).empty(), "and nor does a negative one", "");
    }

    // ── 2. How much of the frame ─────────────────────────────
    //
    // The line that works for 83% of the catalogue. At 0.203"/px a
    // 1280x960 frame is 4.33' x 3.25', so the SHORTER side is 3.25'.
    printf("\n2. Frame fill\n");
    {
        // M13 is 16.5 arcmin. 16.5 / 3.25 = 5.1 times wider.
        const std::string s = FrameFill(16.5, 0.203, 1280, 960);
        Check(Has(s, "is 5.1 times wider"),
              "M13 overflows a 1280x960 frame, with a verb", s);
        Check(Has(s, "1280x960"), "  and the sentence names the format", s);
    }
    {
        // The full sensor is 13.0' x 7.3'; 16.5 / 7.31 = 2.3.
        const std::string s = FrameFill(16.5, 0.203, 3840, 2160);
        Check(Has(s, "is 2.3 times wider"),
              "and still overflows the full sensor, by less", s);
    }
    {
        // IC0002 is 0.98'. 0.98 / 3.25 = 30%.
        const std::string s = FrameFill(0.98, 0.203, 1280, 960);
        Check(Has(s, "fills 30%"), "a small galaxy fills 30% of the frame", s);
    }
    {
        // A Barlow doubles the plate scale and halves the field, so
        // the same object fills twice as much. This is why the line
        // reads the LIVE plate scale rather than a constant.
        const std::string a = FrameFill(0.98, 0.203, 1280, 960);
        const std::string b = FrameFill(0.98, 0.406, 1280, 960);
        Check(Has(b, "fills 15%"),
              "doubling the plate scale halves the fraction", b);
        Check(a != b, "  so the sentence tracks the rig, not a constant", "");
    }
    {
        // M31. The catalogue has it at 178 arcmin, which is 54.8
        // times the 3.25-arcmin short side of a 1280x960 frame -- and
        // it is the number that made the missing verb obvious when it
        // read "It 54.8 times wider than the frame".
        const std::string s = FrameFill(178.0, 0.203, 1280, 960);
        Check(Has(s, "is 54.8 times wider"),
              "and M31 is 55 times wider still", s);
    }
    {
        Check(FrameFill(0.0, 0.203, 1280, 960).empty(),
              "no size produces no sentence", "");
        Check(FrameFill(16.5, 0.203, 0, 0).empty(),
              "and nor does an unknown format", "");
    }

    // ── 3. Brightness against the eye ────────────────────────
    //
    // 2.512^(6 - mag). Jupiter at -2.7 is 2.512^8.7 = 3021x.
    printf("\n3. Brightness\n");
    {
        const std::string s = BrightnessLine(-2.7);
        Check(Has(s, "3021"), "Jupiter is 3021x the naked-eye limit", s);
        Check(Has(s, "brighter"), "  and the word is 'brighter'", "");
    }
    {
        // M13 at 5.8 is just inside the limit: 2.512^0.2 = 1.2x.
        const std::string s = BrightnessLine(5.8);
        Check(Has(s, "faint as the eye can manage"),
              "an object at the limit says so rather than '1 times'", s);
    }
    {
        // IC0002 at 15.46 is 2.512^-9.46 = 1/6000.
        const std::string s = BrightnessLine(15.46);
        Check(Has(s, "too faint for the naked eye"),
              "a 15th-magnitude galaxy is too faint to see", s);
    }

    // ── 4. The constellation column is not always one ────────
    //
    // 408 of the 1001 named-star rows carry a MAGNITUDE in this
    // column -- Achernar "0.45", Acrux "1.33". A column shift in the
    // database builder, and not something to put on a screen in front
    // of people.
    printf("\n4. Refusing to print a bad field\n");
    {
        Check(!LooksLikeConstellation("0.45"),
              "Achernar's 0.45 is rejected as a constellation", "");
        Check(!LooksLikeConstellation("1.33"), "and so is Acrux's 1.33", "");
        Check(LooksLikeConstellation("Her"), "Her is accepted", "");
        Check(LooksLikeConstellation("Se1"),
              "Se1 is accepted - a real abbreviation, digit and all", "");
        Check(!LooksLikeConstellation("Various"),
              "Various is rejected: 'in Various' under a planet is nonsense",
              "");
        Check(!LooksLikeConstellation("N/A"), "and so is N/A", "");
        Check(!LooksLikeConstellation(""), "and an empty cell", "");
    }

    // ── 5. Codes are not for reading aloud ───────────────────
    printf("\n5. Type names\n");
    {
        Check(TypeName("GCl", "GCl") == "Globular cluster",
              "GCl reads as Globular cluster", TypeName("GCl", "GCl"));
        Check(TypeName("G", "G") == "Galaxy", "G reads as Galaxy", "");
        Check(TypeName("Planet", "") == "Planet", "Planet is already English", "");
        Check(TypeName("Satellite", "") == "Moon",
              "Satellite reads as Moon, which is what people call it", "");
        Check(TypeName("", "").empty(),
              "an unknown type produces nothing, not the word 'unknown'", "");
        Check(TypeName("ZZZ", "") == "ZZZ",
              "an uncatalogued code passes through rather than being lost", "");
    }

    printf("\n");
    if (g_fail == 0) printf("=== the panel says true things ===\n\n");
    else             printf("=== %d CHECK(S) FAILED ===\n\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
