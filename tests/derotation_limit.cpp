// ============================================================
//  How long a run lasts before rotation blurs it
//
//  Phase 108. The window against the figure planetary imagers use,
//  which bodies count, and the WinJUPOS name, including the rename
//  that keeps a capture and its scores together. Whether WinJUPOS
//  reads the name needs WinJUPOS: checklist 32.
// ============================================================

#include "core/DerotationLimit.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace raduk::core;
namespace fs = std::filesystem;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_fail;
}

static void Touch(const std::string& p) {
    FILE* f = nullptr;
    if (fopen_s(&f, p.c_str(), "w") == 0 && f) { fputs("x", f); fclose(f); }
}

int main() {
    printf("\n=== Derotation limit ===\n\n");

    const double rayleigh = RayleighArcsec(203.0);
    Check(std::fabs(rayleigh - 0.682) < 0.002, "an 8-inch resolves 0.68 arcsec");
    Check(RayleighArcsec(0.0) == 0.0, "no aperture, no limit");

    const auto* jup = FindRotatingBody("Jupiter");
    Check(jup != nullptr, "Jupiter is found");
    const double jupWin = DerotationWindowSec(44.0, jup->periodSec, rayleigh);
    Check(jupWin > 170.0 && jupWin < 185.0,
          "Jupiter at 44 arcsec gives about three minutes");
    Check(std::fabs(jupWin * 3.14159265358979 * 44.0 / jup->periodSec - rayleigh)
              < 1e-9,
          "the disc centre moves exactly the tolerance in that time");
    Check(DerotationWindowSec(30.0, jup->periodSec, rayleigh) > jupWin,
          "a smaller Jupiter gives a longer window");
    Check(DerotationWindowSec(44.0, jup->periodSec, rayleigh * 2.0) > jupWin,
          "a smaller telescope gives a longer window");

    const auto* mars = FindRotatingBody("Mars");
    const auto* sat  = FindRotatingBody("Saturn");
    Check(DerotationWindowSec(25.0, mars->periodSec, rayleigh) > jupWin,
          "Mars at 25 arcsec allows longer than Jupiter");
    Check(DerotationWindowSec(19.0, sat->periodSec, rayleigh) > jupWin,
          "Saturn's globe allows longer than Jupiter");

    Check(DerotationWindowSec(0.0, 100.0, 1.0) == 0.0, "no disc, no window");
    Check(DerotationWindowSec(10.0, 0.0, 1.0) == 0.0, "no period, no window");
    Check(DerotationWindowSec(10.0, 100.0, 0.0) == 0.0, "no tolerance, no window");

    Check(FindRotatingBody("Jupiter - GRS transit") == jup, "a profile name counts");
    Check(FindRotatingBody("jupiter") == jup, "case does not matter");
    Check(FindRotatingBody("Moon") == nullptr, "the Moon keeps one face to us");
    Check(FindRotatingBody("Sunflower Galaxy") == nullptr, "not the Sun");
    Check(FindRotatingBody("Sun") != nullptr, "the Sun is");
    Check(FindRotatingBody("Vega") == nullptr, "a star has no window");
    Check(FindRotatingBody("") == nullptr, "nor does nothing");
    Check(FindRotatingBody("Jup") == nullptr, "a fragment is not a match");

    // 2026-11-01 21:30:24 UT
    const int64_t t = 1793568624;
    Check(WinJuposStamp(t) == "2026-11-01-2130_4", "the stamp, in UT");
    Check(WinJuposStamp(t - 24) == "2026-11-01-2130_0", "the top of the minute");
    Check(WinJuposStamp(t + 35) == "2026-11-01-2130_9", "the last tenth of it");
    Check(WinJuposStamp(t + 36) == "2026-11-01-2131_0", "the next minute");
    Check(WinJuposStamp(1798761599) == "2026-12-31-2359_9", "the last tenth of a year");
    Check(WinJuposPath("D:\\c\\Jup_0001.ser", t) ==
              "D:\\c\\2026-11-01-2130_4-Jup_0001.ser",
          "the stamp leads the name, not the path");

    Check(QualitySidecarFor("D:\\c\\a.fits") == "D:\\c\\a_quality.txt", "FITS sidecar");
    Check(QualitySidecarFor("D:\\c.d\\a") == "D:\\c.d\\a_quality.txt",
          "a dot in the folder is not an extension");

    const char* tmp = getenv("TEMP");
    const fs::path dir = fs::path(tmp ? tmp : ".") / "raduk_derotation_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::string cap = (dir / "Jup.ser").string();

    Touch(cap);
    Touch(QualitySidecarFor(cap));
    std::string got = RenameForWinJupos(cap, t);
    Check(got == WinJuposPath(cap, t), "the rename returns the new path");
    Check(fs::exists(got), "the capture moved");
    Check(fs::exists(QualitySidecarFor(got)), "its sidecar moved with it");
    Check(!fs::exists(cap) && !fs::exists(QualitySidecarFor(cap)),
          "nothing is left under the old name");

    Touch(cap);
    got = RenameForWinJupos(cap, t);
    Check(got == cap && fs::exists(cap), "it will not overwrite another capture");

    const std::string gone = (dir / "gone.ser").string();
    Check(RenameForWinJupos(gone, t) == gone, "a missing file comes back unchanged");
    fs::remove_all(dir, ec);

    printf("\n%s\n", g_fail ? "FAILED" : "PASS");
    return g_fail ? 1 : 0;
}
