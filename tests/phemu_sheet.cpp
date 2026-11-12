// ============================================================
//  Phase 141.1: IMCCE's PHEMU observation sheet, pre-filled
//
//    1. The phenomenon ID follows IMCCE's rule, checked against the
//       form's own example (E200301062325_2e1_2_OHP_T80).
//    2. Occultations are type 0, eclipses type 2 with a note to check.
//    3. The sheet carries the event, the site in degrees, minutes and
//       seconds with its hemisphere, the rig, the filter (X for none),
//       and both clock checks.
//    4. A clock not confirmed to 0.05 s before and after is flagged; an
//       unset site asks to be filled in rather than writing 0,0.
//
//  The coordinates below are invented for the test.
// ============================================================

#include "core/PhemuSheet.h"

#include <cstdio>
#include <string>

using namespace raduk::core::phemu_sheet;
using raduk::sky::phemu::Event;
using raduk::sky::phemu::JdFromIsoUtc;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}
static bool Has(const std::string& h, const std::string& n) { return h.find(n) != std::string::npos; }

int main() {
    printf("PHEMU observation sheet (phase 141.1)\n");

    printf("\n1. The phenomenon ID\n");
    Event ex;
    ex.kind = "Eclipse"; ex.by = 502; ex.of = 501;
    ex.beginJd = JdFromIsoUtc("2003-01-06T23:25:10.000");
    ex.greatestJd = ex.beginJd + 2.0 / 1440; ex.endJd = ex.beginJd + 4.0 / 1440;
    const std::string id = PhenomenonId(ex, "OHP T80");
    Check(id == "E200301062325_2e1_2_OHP_T80", "IMCCE's own example", id);
    Event occ = ex; occ.kind = "Occultation"; occ.by = 501; occ.of = 503;
    Check(PhenomenonId(occ, "Home 8SE!") == "E200301062325_1o3_0_Home_8SE", "an occultation, site made safe",
          PhenomenonId(occ, "Home 8SE!"));

    printf("\n2. Photometry type\n");
    Check(PhotometryType(occ) == 0 && PhotometryType(ex) == 2, "occultation 0, eclipse 2");

    printf("\n3. The sheet\n");
    Rig rig;
    rig.siteName = "Test Site 8SE";
    rig.siteKnown = true;
    rig.latDeg = 12.5; rig.lonDeg = -45.25; rig.elevM = 300;
    rig.filter = "UV/IR cut";
    Run run;
    run.serFile = "PHEMU_Europa_eclipses_Io_20030106.ser";
    run.startJd = ex.beginJd - 10.0 / 1440; run.endJd = ex.endJd + 10.0 / 1440;
    run.before.ok = true; run.before.offsetSec = 0.231; run.before.delaySec = 0.024; run.before.stratum = 2;
    run.after.ok = true;  run.after.offsetSec = 0.229;  run.after.delaySec = 0.030;  run.after.stratum = 2;
    run.exposureMs = 25.0; run.gain = 300;
    const std::string s = Sheet(ex, rig, run);
    Check(Has(s, "Observed phenomenon ID: E200301062325_2e1_2_Test_Site_8SE"), "the ID");
    Check(Has(s, "eclipsing satellite (1,2,3,4,...): 2") && Has(s, "eclipsed satellite (1,2,3,4,...): 1"),
          "the two moons");
    Check(Has(s, "[CHECK] 2 assumes both moons"), "an eclipse asks the observer to confirm type 2");
    Check(Has(s, "Longitude = (E,W): W\n                 deg: 45\n                 min: 15\n                 sec: 0.0"),
          "longitude west, 45 15 00");
    Check(Has(s, "Latitude = (N,S): N\n                 deg: 12\n                 min: 30"), "latitude north, 12 30");
    Check(Has(s, "Diameter in cm: 20.3") && Has(s, "Focal length in cm: 203.2"), "the telescope");
    Check(Has(s, "Filter (put X if no filter): UV/IR cut"), "the filter");
    Check(Has(s, "Clock before: PC clock 0.231 s behind UTC (+/- 0.012 s") &&
          Has(s, "Clock after: PC clock 0.229 s behind UTC (+/- 0.015 s"), "both clock checks");
    Check(Has(s, "Not measured: the delay between the camera's exposure"), "the unmeasured latency is stated");
    Check(!Has(s, "[CHECK] The clock was not confirmed"), "two confirmed checks raise no flag");
    Check(Has(s, "date UTC (0,1,2,3,4,5): 0"), "dates in JD, as the .dat will be");

    printf("\n4. What it will not pretend\n");
    Run bad = run;
    bad.after = raduk::net::sntp::Sample{};
    bad.after.why = "pool.ntp.org did not answer (UDP 123 blocked?)";
    const std::string s2 = Sheet(ex, rig, bad);
    Check(Has(s2, "Clock after: clock not checked: pool.ntp.org did not answer"), "a failed check says why");
    Check(Has(s2, "[CHECK] The clock was not confirmed"), "and the sheet is flagged");
    Rig nosite = rig; nosite.siteKnown = false;
    const std::string s3 = Sheet(ex, nosite, run);
    Check(Has(s3, "[FILL IN: no site set in rig.ini]") && !Has(s3, "deg: 0\n"), "no site: asked for, not 0,0");
    Rig nofilter = rig; nofilter.filter.clear();
    Check(Has(Sheet(ex, nofilter, run), "Filter (put X if no filter): X"), "no filter is X");

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
