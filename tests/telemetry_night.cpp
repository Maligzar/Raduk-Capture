// ============================================================
//  Phase 132.10d: the dashboard follows the app's night vision
//
//  The telemetry message carries night_vision, additively, so the
//  dashboard can switch with the app's toggle. The page applies it only
//  when the field is there (checked in Chromium for the PR); here, that
//  the field is in the message and says what it should.
// ============================================================

#include "net/APEXTelemetry.h"

#include <cstdio>
#include <string>

using namespace raduk::net;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-58s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

int main() {
    printf("Telemetry night vision (phase 132.10d)\n\n");
    APEXTelemetry t;
    const std::string off = t.ToJSON();
    Check(off.find("\"night_vision\":false") != std::string::npos, "off by default, and said so");
    t.night_vision = true;
    const std::string on = t.ToJSON();
    Check(on.find("\"night_vision\":true") != std::string::npos, "on when the app is in night vision");
    Check(on.find("\"planet_lock\":") != std::string::npos, "the existing fields are still there");
    Check(!on.empty() && on.front() == '{' && on.back() == '}', "still one JSON object");
    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
