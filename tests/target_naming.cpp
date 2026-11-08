// ============================================================
//  2026-11-08: a recording is named for what the telescope is on
//
//  On 2026-11-07 Uranus was captured on the Saturn profile and filed
//  as Saturn, while the panel could already name the planet. The rule
//  in TargetNaming.h: the run is named for the target unless the
//  target IS the profile's object, or somebody chose the prefix; and a
//  target on another object's profile is called out.
// ============================================================

#include "core/TargetNaming.h"

#include <cstdio>
#include <string>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

int main() {
    printf("\n=== Target naming ===\n");

    printf("\n1. The night's case\n");
    {
        const RecordingName n = NameRecording("Saturn_", "Saturn", "Saturn_", "Uranus");
        Check(n.prefix == "Uranus_" && n.renamed, "Uranus on the Saturn profile is filed as Uranus",
              n.prefix);
        Check(n.settingsMismatch, "and is called out: the settings are Saturn's");
    }

    printf("\n2. When the name stands\n");
    {
        const RecordingName n = NameRecording("Saturn_", "Saturn", "Saturn_", "Saturn");
        Check(n.prefix == "Saturn_" && !n.renamed && !n.settingsMismatch,
              "the target is the profile's object: nothing changes");
        Check(!NameRecording("Saturn_", "Saturn", "Saturn_", "saturn").renamed,
              "regardless of case");
        Check(NameRecording("Saturn_", "Saturn", "Saturn_", "").prefix == "Saturn_",
              "no target known: the profile's prefix");
        const RecordingName typed = NameRecording("Rings_test", "Saturn", "Saturn_", "Uranus");
        Check(typed.prefix == "Rings_test" && !typed.renamed,
              "a prefix the operator typed is kept", typed.prefix);
        Check(typed.settingsMismatch, "but the wrong profile is still called out");
        Check(NameRecording("ISS", "Jupiter", "Jupiter_", "ISS (ZARYA)").prefix == "ISS",
              "a satellite pass's own prefix is kept");
    }

    printf("\n3. Generic profiles\n");
    {
        const RecordingName f = NameRecording("Focus_", "Planet Focus", "Focus_", "Mars");
        Check(f.prefix == "Mars_" && f.renamed, "Planet Focus on Mars is filed as Mars", f.prefix);
        Check(!f.settingsMismatch, "and is not a mismatch: it is for any target");
        Check(!NameRecording("DSO_", "DSO", "DSO_", "M 42").settingsMismatch &&
                  !NameRecording("Star_", "Star", "Star_", "Vega").settingsMismatch,
              "nor are DSO and Star");
    }

    printf("\n4. Safe for a file\n");
    {
        Check(NameRecording("DSO_", "DSO", "DSO_", "M 42 / Orion").prefix == "M_42___Orion_",
              "spaces and slashes become underscores",
              NameRecording("DSO_", "DSO", "DSO_", "M 42 / Orion").prefix);
        Check(SafeFilePart("C/2025 A1") == "C_2025_A1" && SafeFilePart("NGC-7000") == "NGC-7000",
              "letters, digits and '-' kept");
        Check(IsGenericProfileName("planet focus") && IsGenericProfileName("Planet_Focus") &&
                  !IsGenericProfileName("Uranus"),
              "generic names are recognised, case aside");
    }

    printf("\n%s\n", g_fail ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return g_fail ? 1 : 0;
}
