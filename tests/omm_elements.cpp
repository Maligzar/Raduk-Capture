// ============================================================
//  Phase 136: satellite elements as OMM
//
//    1. The ISS as OMM JSON (the same elements as satellite_passes'
//       TLE) reads to the same Tle and propagates to the same place, to
//       within a metre a day later.
//    2. A six-digit catalog number, which TLE cannot carry, reads and
//       propagates.
//    3. Several replies concatenated in one cache, with the ISS in two
//       groups: one entry each, the newer epoch kept.
//    4. A truncated reply keeps the whole records, and a record missing
//       an element is dropped and counted.
// ============================================================

#include "sky/Omm.h"
#include "sky/SatPass.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace raduk::sky;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static const char* kIss1 = "1 25544U 98067A   26305.53114906  .00006056  00000+0  11739-3 0  9995";
static const char* kIss2 = "2 25544  51.6332 340.4448 0007086 335.8887  24.1768 15.49140542593046";

// CelesTrak's layout, the same elements as the TLE above.
static const char* kIssOmm = R"J([{"OBJECT_NAME":"ISS (ZARYA)","OBJECT_ID":"1998-067A",
"EPOCH":"2026-11-01T12:44:51.278784","MEAN_MOTION":15.49140542,"ECCENTRICITY":0.0007086,
"INCLINATION":51.6332,"RA_OF_ASC_NODE":340.4448,"ARG_OF_PERICENTER":335.8887,"MEAN_ANOMALY":24.1768,
"EPHEMERIS_TYPE":0,"CLASSIFICATION_TYPE":"U","NORAD_CAT_ID":25544,"ELEMENT_SET_NO":999,
"REV_AT_EPOCH":59304,"BSTAR":0.00011739,"MEAN_MOTION_DOT":6.056e-5,"MEAN_MOTION_DDOT":0}])J";

int main() {
    printf("OMM satellite elements (phase 136)\n");

    printf("\n1. The ISS, OMM against TLE\n");
    const Tle tle = ParseTle("ISS (ZARYA)", kIss1, kIss2);
    int dropped = -1;
    const auto omm = ParseOmmJson(kIssOmm, &dropped);
    Check(omm.size() == 1 && dropped == 0, "one record read", std::to_string(omm.size()));
    if (omm.size() == 1) {
        const Tle& o = omm[0];
        Check(o.catalog == 25544 && o.name == "ISS (ZARYA)", "catalog number and name", o.name);
        Check(std::fabs(o.epochJd - tle.epochJd) * 86400.0 < 0.001, "epoch to the millisecond",
              std::to_string((o.epochJd - tle.epochJd) * 86400.0) + " s");
        Check(std::fabs(o.bstar - tle.bstar) < 1e-12 && std::fabs(o.ecc - tle.ecc) < 1e-12 &&
              std::fabs(o.revPerDay - tle.revPerDay) < 1e-8, "B*, eccentricity, mean motion");
        Sgp4 a, b;
        Check(a.Init(tle) && b.Init(o), "SGP4 takes both");
        const double jd = tle.epochJd + 1.0;
        const auto sa = a.AtJd(jd), sb = b.AtJd(jd);
        const double d = std::sqrt(std::pow(sa.r[0] - sb.r[0], 2) + std::pow(sa.r[1] - sb.r[1], 2) +
                                   std::pow(sa.r[2] - sb.r[2], 2)) * 1000.0;
        Check(sa.ok && sb.ok && d < 1.0, "a day later they agree to under a metre", std::to_string(d) + " m");
    }

    printf("\n2. A six-digit catalog number\n");
    {
        std::string six = kIssOmm;
        six.replace(six.find("\"NORAD_CAT_ID\":25544"), 20, "\"NORAD_CAT_ID\":270431");
        six.replace(six.find("ISS (ZARYA)"), 11, "CZ-5B R/B");
        const auto v = ParseOmmJson(six);
        Check(v.size() == 1 && v[0].catalog == 270431, "catalog 270431 reads as itself",
              v.empty() ? "" : std::to_string(v[0].catalog));
        Sgp4 s;
        Check(!v.empty() && s.Init(v[0]) && s.AtJd(v[0].epochJd + 0.1).ok, "and propagates");
    }

    printf("\n3. Groups concatenated\n");
    {
        std::string newer = kIssOmm;
        newer.replace(newer.find("2026-11-01T12:44"), 16, "2026-11-02T00:00");
        const std::string other = R"J([{"OBJECT_NAME":"HST","EPOCH":"2026-11-01T00:00:00","MEAN_MOTION":15.1,
"ECCENTRICITY":0.0002,"INCLINATION":28.47,"RA_OF_ASC_NODE":10,"ARG_OF_PERICENTER":20,"MEAN_ANOMALY":30,
"NORAD_CAT_ID":20580,"BSTAR":0.00002}])J";
        const auto v = ParseOmmJson(std::string(kIssOmm) + "\n" + other + "\n" + newer);
        Check(v.size() == 2, "the ISS in two groups is one entry", std::to_string(v.size()));
        for (const auto& t : v)
            if (t.catalog == 25544)
                Check(std::fabs(t.epochJd - JdFromOmmEpoch("2026-11-02T00:00:51.278784")) < 1e-6,
                      "the newer epoch is kept");
    }

    printf("\n4. Damage\n");
    {
        std::string two = std::string(kIssOmm);
        two.pop_back();   // drop the closing ']'
        two += R"J(,{"OBJECT_NAME":"HALF","EPOCH":"2026-11-01T00:00:00","MEAN_MO)J";
        int dr = -1;
        const auto v = ParseOmmJson(two, &dr);
        Check(v.size() == 1 && v[0].catalog == 25544, "a truncated reply keeps the whole record");
        const auto w = ParseOmmJson(R"J([{"OBJECT_NAME":"X","NORAD_CAT_ID":5,"EPOCH":"2026-11-01T00:00:00"}])J", &dr);
        Check(w.empty() && dr == 1, "a record missing elements is dropped and counted");
        Check(ParseOmmJson("<html>error</html>").empty(), "an error page reads as nothing");
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
