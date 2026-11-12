// ============================================================
//  Phase 140.3: the display stretch chosen from the target
//
//    1. A dim, correctly exposed planet (bright side at 80 of 255) is
//       shown with its bright side near 90% of the screen, the sky dark.
//    2. A planet already bright is not darkened: the white point stays
//       at full range.
//    3. A faint target is amplified at most 8 times.
//    4. No target: nothing is changed.
// ============================================================

#include "core/DisplayStretch.h"

#include <cstdio>
#include <string>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}
static int Shown(const StretchPoints& s, int v) {
    int o = (v - s.black) * 255 / (s.white - s.black);
    return o < 0 ? 0 : (o > 255 ? 255 : o);
}

int main() {
    printf("Display stretch (phase 140.3)\n");

    printf("\n1. A dim planet\n");
    {
        const StretchPoints s = AutoStretchFor(true, 8.0f, 80.0f);
        Check(s.apply, "applied");
        const int hi = Shown(s, 80), sky = Shown(s, 8);
        Check(hi >= 220 && hi <= 240, "the bright side shows at about 90%", std::to_string(hi));
        Check(sky == 0, "the sky shows black", std::to_string(sky));
    }

    printf("\n2. A bright planet\n");
    {
        const StretchPoints s = AutoStretchFor(true, 8.0f, 240.0f);
        Check(s.apply && s.white == 255, "the white point stays at 255: never darkened", std::to_string(s.white));
        Check(Shown(s, 240) >= 238, "the planet shows as bright as it is", std::to_string(Shown(s, 240)));
    }

    printf("\n3. A faint target\n");
    {
        const StretchPoints s = AutoStretchFor(true, 10.0f, 14.0f);
        Check(s.apply && (s.white - s.black) >= 255 / 8, "amplified at most 8 times",
              std::to_string(s.black) + "-" + std::to_string(s.white));
    }

    printf("\n4. No target\n");
    Check(!AutoStretchFor(false, 8.0f, 80.0f).apply, "no target: the stretch is left alone");
    Check(!AutoStretchFor(true, 50.0f, 40.0f).apply, "a highlight under the sky: left alone");

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
