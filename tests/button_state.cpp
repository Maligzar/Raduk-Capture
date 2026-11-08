// ============================================================
//  Phase 132.5: hover and press on the shared button face
//
//  ButtonState.h decides how a face answers the pointer. What must
//  hold: rest is the plain panel; hover lifts by half a surface step
//  and never as far as the frame; a press goes down a step and drops
//  the content; a disabled button ignores both. The hover ramp eases
//  in over its duration, reverses smoothly from wherever it is, and
//  with motion off is instant.
// ============================================================

#include "gui/ButtonState.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace raduk::gui::button;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}
static std::string F(double v) { char b[32]; snprintf(b, sizeof b, "%.3f", v); return b; }

int main() {
    printf("\n=== Phase 132.5: button state ===\n");

    printf("\n1. The look\n");
    {
        const Look rest = LookFor(false, false, 0.0f);
        Check(rest.lift == 0.0f && rest.offset == 0 && rest.litEdge && !rest.dimText,
              "at rest: the panel, lit edge, no offset");
        const Look hot = LookFor(false, false, 1.0f);
        Check(hot.lift > 0.0f && hot.lift < 1.0f,
              "hovered: lifted, but short of the frame's step", F(hot.lift));
        Check(hot.litEdge && hot.offset == 0, "and still raised, not pressed");
        Check(LookFor(false, false, 7.0f).lift == hot.lift &&
                  LookFor(false, false, -3.0f).lift == 0.0f,
              "a hover level outside 0..1 is clamped");
        const Look down = LookFor(false, true, 1.0f);
        Check(down.lift == -1.0f && down.offset == 1 && !down.litEdge,
              "pressed: a step down, content 1 px lower, no lit edge");
        const Look off = LookFor(true, true, 1.0f);
        Check(off.lift == 0.0f && off.offset == 0 && off.dimText,
              "disabled: ignores hover and press, text dim");
    }

    printf("\n2. The hover ramp\n");
    {
        HoverRamp r;
        Check(r.Level(0) == 0.0f && r.Settled(0) && !r.Hot(), "starts at rest and settled");
        r.Set(true, 1000, 150, true);
        Check(r.Hot() && !r.Settled(1000), "the pointer arrives: hot, moving");
        Check(r.Level(1000) == 0.0f, "level starts from where it was", F(r.Level(1000)));
        const float mid = r.Level(1075);
        Check(mid > 0.5f && mid < 1.0f, "halfway in time is past halfway: ease-out", F(mid));
        float prev = -1.0f;
        bool mono = true;
        for (int ms = 0; ms <= 150; ms += 5) {
            const float v = r.Level(1000 + ms);
            if (v < prev) mono = false;
            prev = v;
        }
        Check(mono, "rises without going back");
        Check(r.Level(1150) == 1.0f && r.Settled(1150), "arrives exactly, then is settled");
        Check(r.Level(5000) == 1.0f, "and does not overshoot");

        r.Set(true, 1200, 150, true);
        Check(r.Settled(1200), "a repeat of the same target restarts nothing");

        r.Set(false, 2000, 150, true);
        const float leaving = r.Level(2040);
        r.Set(true, 2040, 150, true);
        Check(std::fabs(r.Level(2040) - leaving) < 1e-6f,
              "reversing mid-ramp starts from the current level, no jump", F(leaving));
        Check(r.Level(2190) == 1.0f, "and finishes at the new target");
    }
    {
        HoverRamp r;
        r.Set(true, 0, 150, false);
        Check(r.Level(0) == 1.0f && r.Settled(0), "motion off: hover is instant");
        r.Set(false, 10, 150, false);
        Check(r.Level(10) == 0.0f && r.Settled(10), "and so is leaving");
    }

    printf("\n%s\n", g_fail ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return g_fail ? 1 : 0;
}
