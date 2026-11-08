// ============================================================
//  Phase 133.2: the fade warning
//
//  On 2026-11-08 Saturn's peak fell 255 -> 154 in 90 s during an
//  autofocus sweep. That is a fade of about 40%, steady, and it should
//  be reported. What must NOT be reported: seeing scatter on a steady
//  target, a slow fall too small to matter, an exposure or gain change,
//  and the same fade over and over.
// ============================================================

#include "core/FadeWatch.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <string>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

// Feeds `sec` seconds at 4 Hz of flux = f0 * fall^(t/sec) * (1 + noise),
// returning the first alert, if any.
static FadeAlert Run(FadeWatch& w, double sec, double f0, double endFrac, double noise,
                     unsigned seed = 1, long exp = 20000, int gain = 350) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> nd(0.0, noise);
    FadeAlert first;
    for (int i = 0; i <= (int)(sec * 4); ++i) {
        const double t = i / 4.0;
        const double f = f0 * std::pow(endFrac, t / sec) * (1.0 + nd(rng));
        const FadeAlert a = w.Add(t, f, exp, gain);
        if (a.fired && !first.fired) first = a;
    }
    return first;
}

int main() {
    printf("\n=== Phase 133.2: fade watch ===\n");

    printf("\n1. The night's fade is reported\n");
    {
        FadeWatch w;
        // 255 -> 154 is 60% of the start: down 40% in 90 s, with 5% scatter.
        const FadeAlert a = Run(w, 90.0, 1.0e6, 154.0 / 255.0, 0.05);
        Check(a.fired, "a 40% fade over 90 s fires");
        Check(a.dropFrac > 0.25 && a.dropFrac < 0.45, "and reports about the drop it saw",
              std::to_string(a.dropFrac));
        Check(a.windowSec >= FadeWatch::kMinSpanSec, "only once enough time has passed",
              std::to_string(a.windowSec) + " s");
    }

    printf("\n2. What must not be reported\n");
    {
        FadeWatch w;
        Check(!Run(w, 180.0, 1.0e6, 1.0, 0.15).fired,
              "a steady target with 15% seeing scatter is quiet");
    }
    {
        FadeWatch w;
        Check(!Run(w, 90.0, 1.0e6, 0.9, 0.03).fired, "a 10% fall is too small to matter");
    }
    {
        FadeWatch w;
        Check(!Run(w, 30.0, 1.0e6, 0.4, 0.02).fired,
              "nothing is said on less than the minimum span");
    }
    {
        // An exposure step halves the flux in one frame: not a fade.
        FadeWatch w;
        bool fired = false;
        for (int i = 0; i <= 360; ++i) {
            const double t = i / 4.0;
            const long exp = t < 45.0 ? 20000 : 10000;
            const double f = t < 45.0 ? 1.0e6 : 0.5e6;
            fired |= w.Add(t, f, exp, 350).fired;
        }
        Check(!fired, "an exposure change resets the window rather than reading as a fade");
    }
    {
        FadeWatch w;
        bool fired = false;
        for (int i = 0; i <= 360; ++i) {
            const double t = i / 4.0;
            fired |= w.Add(t, t < 45.0 ? 1.0e6 : 0.4e6, 20000, t < 45.0 ? 350 : 200).fired;
        }
        Check(!fired, "and so does a gain change");
    }
    {
        FadeWatch w;
        Check(!w.Add(0, 0.0, 1, 1).fired && !w.Add(1, -5.0, 1, 1).fired &&
                  !w.Add(2, std::nan(""), 1, 1).fired,
              "no flux, negative flux and NaN are ignored");
    }

    printf("\n3. Once, then quiet for the cooldown\n");
    {
        FadeWatch w;
        int alerts = 0;
        for (int i = 0; i <= 4 * 200; ++i) {
            const double t = i / 4.0;
            alerts += w.Add(t, 1.0e6 * std::pow(0.5, t / 90.0), 20000, 350).fired ? 1 : 0;
        }
        Check(alerts == 2, "a 200 s fade fires once, then once more after the 120 s cooldown",
              std::to_string(alerts) + " alerts");
    }

    printf("\n4. The words\n");
    {
        FadeAlert a;
        a.fired = true; a.dropFrac = 0.4; a.windowSec = 88.6;
        const std::wstring plain = FadeMessage(a, "");
        Check(plain.find(L"down 40% in 89 s") != std::wstring::npos &&
                  plain.find(L"dew on the corrector, or cloud?") != std::wstring::npos,
              "with no weather it names both causes");
        const std::wstring dew = FadeMessage(a, "elevated");
        Check(dew.find(L"dew risk is elevated") != std::wstring::npos &&
                  dew.find(L"check the corrector") != std::wstring::npos,
              "with the feed's dew risk up it says so");
        Check(FadeMessage(a, "safe") == plain, "a safe dew risk does not blame dew");
    }

    printf("\n%s\n", g_fail ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return g_fail ? 1 : 0;
}
