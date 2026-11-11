// ============================================================
//  Phase 141.0: Galilean mutual events, for IMCCE's PHEMU campaign
//
//  Everything here is synthetic. IMCCE's predictions are fetched by the
//  app for the owner's use and never committed (its terms do not allow
//  reproduction), so these replies are built in the shape Opale returns,
//  with invented values:
//
//    1. Opale's reply is read: only mutual events between two Galilean
//       moons are kept, Jupiter's own phenomena and the inner moons'
//       are dropped, and the fields land where they belong.
//    2. A reply that is not an event list is refused with a reason.
//    3. UTC strings become Julian dates and back, to the tenth of a
//       second the campaign times to.
//    4. A cache line round-trips, including a missing depth.
//    5. The recording window: at least 10 minutes either side, and a
//       long event's own length.
//    6. Tonight: overlap with the night, visibility at the site from an
//       injected sky, and IMCCE's cautions and preferences.
// ============================================================

#include "sky/MutualEvents.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace raduk::sky::phemu;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}
static std::string F(double v, int d = 3) { char b[48]; snprintf(b, sizeof(b), "%.*f", d, v); return b; }

// One Opale event, synthetic.
static std::string Ev(int by, int of, bool mutual, const char* type, const char* sub,
                      const char* begin, const char* greatest, const char* end,
                      const char* drop, double dur, double impact, double limb, double sep) {
    char b[1400];
    snprintf(b, sizeof(b),
        "{\"countLocalGreatest\":1,\"deltaCombinedMagnitude\":null,\"dropNormalizedFlux\":%s,"
        "\"duration\":%.2f,\"eclipsedBody\":%d,\"eclipsingBody\":%d,"
        "\"events\":{"
        "\"begin\":{\"S\":%.2f,\"combinedMagnitude\":5.1,\"date\":\"%s\","
        "\"distanceLimb\":{\"angle\":40.0,\"radiusPlanet\":%.2f},\"positionAngle\":80.0,\"visibility\":true},"
        "\"end\":{\"S\":%.2f,\"combinedMagnitude\":5.1,\"date\":\"%s\","
        "\"distanceLimb\":{\"angle\":40.0,\"radiusPlanet\":%.2f},\"positionAngle\":80.0,\"visibility\":true},"
        "\"greatest\":{\"S\":0.5,\"combinedMagnitude\":5.4,\"date\":\"%s\","
        "\"distanceLimb\":{\"angle\":40.0,\"radiusPlanet\":%.2f},\"positionAngle\":80.0,\"visibility\":true}},"
        "\"impactFactor\":%.2f,\"mutualPhenomena\":%s,\"subType\":\"%s\",\"type\":\"%s\"}",
        drop, dur, of, by, sep, begin, limb, sep, end, limb, greatest, limb, impact,
        mutual ? "true" : "false", sub, type);
    return b;
}

static std::string Reply(const std::string& events) {
    return "{\n  \"link\" : {\"self\" : \"x\"},\n  \"request\" : {\"body\" : 599},\n"
           "  \"response\" : {\"calendar\" : \"gregorian\", \"data\" : [" + events +
           "], \"timescale\" : \"UTC\"}\n}";
}

int main() {
    printf("Galilean mutual events (phase 141.0)\n");

    // ── 1. Parsing ─────────────────────────────────────────
    printf("\n1. Opale's reply\n");
    const std::string a = Ev(501, 502, true, "Occultation", "Partial",
        "2026-12-14T03:10:30.000", "2026-12-14T03:12:40.100", "2026-12-14T03:14:50.000",
        "38.0", 4.33, 0.42, 4.1, 21.5);
    const std::string jupShadow = Ev(505, 599, false, "Shadow", "Ingress",
        "2026-12-14T00:06:15.575", "2026-12-14T00:06:20.685", "2026-12-14T00:06:25.796",
        "null", 0.17, 0.0, 0.4, 25.0);
    const std::string inner = Ev(514, 501, true, "Eclipse", "Partial",
        "2026-12-14T02:27:00.000", "2026-12-14T02:29:02.428", "2026-12-14T02:31:00.000",
        "1.0", 2.86, 0.5, 1.0, 3.0);
    // Listed first but later in time, to check the sort.
    const std::string b = Ev(503, 504, true, "Eclipse", "Penumbral",
        "2026-12-15T01:00:00.000", "2026-12-15T01:20:00.000", "2026-12-15T01:40:00.000",
        "null", 40.0, 0.95, 2.0, 60.0);
    std::vector<Event> evs;
    std::string why;
    const bool ok = ParseOpale(Reply(b + "," + a + "," + jupShadow + "," + inner), evs, &why);
    Check(ok, "a well-formed reply parses", why);
    Check(evs.size() == 2, "only the two Galilean mutual events are kept", std::to_string(evs.size()));
    if (evs.size() == 2) {
        const Event& e = evs[0];
        Check(e.by == 501 && e.of == 502, "sorted by time: Io and Europa first");
        Check(e.kind == "Occultation" && e.sub == "Partial", "type and subtype");
        Check(std::fabs(e.durationMin - 4.33) < 1e-9 && std::fabs(e.dropPct - 38.0) < 1e-9,
              "duration and drop", F(e.durationMin) + " min, " + F(e.dropPct) + "%");
        Check(std::fabs(e.limbRadii - 4.1) < 1e-9 && std::fabs(e.impact - 0.42) < 1e-9 &&
              std::fabs(e.sepArcsec - 21.5) < 1e-9, "limb distance, impact factor and separation");
        Check(e.Describe() == "Io occults Europa (partial)", "the sentence", e.Describe());
        Check(std::isnan(evs[1].dropPct), "a null drop stays unknown, not zero");
        Check(evs[1].Describe() == "Ganymede eclipses Callisto (penumbral)", "the eclipse's sentence",
              evs[1].Describe());
    }

    // ── 2. Refusals ────────────────────────────────────────
    printf("\n2. Not an event list\n");
    {
        std::vector<Event> none;
        std::string w;
        const bool bad = ParseOpale("{\"title\":\"Query parameter in the URI has an invalid value\",\"status\":400}", none, &w);
        Check(!bad && none.empty(), "an error reply is refused");
        Check(w.find("invalid value") != std::string::npos, "and says what IMCCE said", w);
        std::string w2;
        Check(!ParseOpale("<html>gateway timeout</html>", none, &w2), "an HTML page is refused", w2);
        std::string w3;
        Check(!ParseOpale(Reply(a).substr(0, 300), none, &w3), "a truncated reply is refused", w3);
        std::vector<Event> empty;
        Check(ParseOpale(Reply(""), empty) && empty.empty(), "an empty list is a valid, empty answer");
    }

    // ── 3. Time ────────────────────────────────────────────
    printf("\n3. UTC and Julian dates\n");
    {
        const double j2000 = JdFromIsoUtc("2000-01-01T12:00:00.000");
        Check(std::fabs(j2000 - 2451545.0) < 1e-9, "J2000.0 is JD 2451545.0", F(j2000, 6));
        const double jd = JdFromIsoUtc("2026-12-14T03:12:40.100");
        const std::string back = IsoFromJd(jd, 1);
        Check(back == "2026-12-14 03:12:40.1", "a tenth of a second survives the round trip", back);
        Check(JdFromIsoUtc("not a date") == 0.0, "a bad string gives 0");
        Check(IsoFromJd(JdFromIsoUtc("2027-01-01T00:00:00.000"), 0) == "2027-01-01 00:00:00",
              "midnight at the turn of the year", IsoFromJd(JdFromIsoUtc("2027-01-01T00:00:00.000"), 0));
    }

    // ── 4. Cache ───────────────────────────────────────────
    printf("\n4. The cache line\n");
    if (evs.size() == 2) {
        for (const Event& e : evs) {
            Event back;
            const bool ok2 = ParseCacheLine(CacheLine(e), back);
            const bool same = ok2 && back.by == e.by && back.of == e.of && back.kind == e.kind &&
                back.sub == e.sub && std::fabs(back.greatestJd - e.greatestJd) < 1e-7 &&
                std::fabs(back.durationMin - e.durationMin) < 1e-3 &&
                (std::isnan(e.dropPct) ? std::isnan(back.dropPct) : std::fabs(back.dropPct - e.dropPct) < 1e-3);
            Check(same, "round trip: " + e.Describe(), CacheLine(e).substr(0, 40));
        }
        Event x;
        Check(!ParseCacheLine("# fetched from opale.imcce.fr", x), "a comment is not an event");
        Check(!ParseCacheLine("1\t2\t3", x), "a short line is refused");
    }

    // ── 5. Recording window ───────────────────────────────
    printf("\n5. Recording window\n");
    if (evs.size() == 2) {
        const Event& s = evs[0];   // 4.3 min
        const Event& l = evs[1];   // 40 min
        Check(std::fabs(MarginMinutes(s) - 10.0) < 1e-9, "a short event gets 10 minutes either side");
        Check(std::fabs(MarginMinutes(l) - 40.0) < 1e-9, "a long one gets its own length");
        Check(std::fabs((s.beginJd - RecordStartJd(s)) * 1440.0 - 10.0) < 1e-6 &&
              std::fabs((RecordEndJd(s) - s.endJd) * 1440.0 - 10.0) < 1e-6,
              "start before the predicted begin, stop after the predicted end");
    }

    // ── 6. Tonight ─────────────────────────────────────────
    printf("\n6. Tonight\n");
    if (evs.size() == 2) {
        const double nightFrom = JdFromIsoUtc("2026-12-14T00:00:00.000");
        const double nightTo   = JdFromIsoUtc("2026-12-14T12:00:00.000");
        // Jupiter up and the Sun down from 01:00 to 06:00 UTC on the 14th.
        const double upFrom = JdFromIsoUtc("2026-12-14T01:00:00.000");
        const double upTo   = JdFromIsoUtc("2026-12-14T06:00:00.000");
        SkyFn sky = [&](double jd) {
            SkyAt s; s.ok = true;
            const bool up = jd >= upFrom && jd <= upTo;
            s.jupiterAltDeg = up ? 50.0 : 5.0;
            s.sunAltDeg = up ? -30.0 : -30.0;
            return s;
        };
        auto picks = Tonight(evs, nightFrom, nightTo, sky);
        Check(picks.size() == 1, "only the event inside the night is listed", std::to_string(picks.size()));
        if (!picks.empty()) {
            const Pick& p = picks[0];
            Check(p.ev.by == 501 && p.wholeVisible, "Io and Europa, wholly visible");
            Check(p.cautions.empty(), "a deep, central occultation far from the limb has no caution",
                  PickLine(p));
            const std::string line = PickLine(p);
            Check(line.find("2026-12-14 03:12:40.1 UTC") == 0 &&
                  line.find("record 03:00-03:24") != std::string::npos,
                  "the line names the middle and the recording window", line);
        }

        // The next night: the long penumbral eclipse, near the limb,
        // grazing, with no depth predicted.
        const double n2From = JdFromIsoUtc("2026-12-15T00:00:00.000");
        const double n2To   = JdFromIsoUtc("2026-12-15T12:00:00.000");
        SkyFn sky2 = [&](double) { SkyAt s; s.ok = true; s.jupiterAltDeg = 40; s.sunAltDeg = -20; return s; };
        auto p2 = Tonight(evs, n2From, n2To, sky2);
        Check(p2.size() == 1, "the second night lists the eclipse");
        if (!p2.empty()) {
            const auto& c = p2[0].cautions;
            auto has = [&](const char* w) {
                for (const auto& s : c) if (s.find(w) != std::string::npos) return true;
                return false;
            };
            Check(has("depth not predicted") && has("3 radii") && has("grazing") && has("penumbral"),
                  "IMCCE's cautions: depth, limb, grazing, penumbral", PickLine(p2[0]));
        }

        // Daylight: the Sun is up, so nothing is listed unless asked.
        SkyFn day = [&](double) { SkyAt s; s.ok = true; s.jupiterAltDeg = 40; s.sunAltDeg = 10; return s; };
        Check(Tonight(evs, nightFrom, nightTo, day).empty(), "with the Sun up nothing is listed");
        auto missed = Tonight(evs, nightFrom, nightTo, day, Limits{}, true);
        Check(missed.size() == 1 && !missed[0].greatestVisible, "but asked, it says what was missed");

        // Half visible: Jupiter sets during the recording window.
        const double setAt = JdFromIsoUtc("2026-12-14T03:20:00.000");
        SkyFn setting = [&](double jd) {
            SkyAt s; s.ok = true; s.sunAltDeg = -30; s.jupiterAltDeg = jd < setAt ? 30 : 5; return s;
        };
        auto half = Tonight(evs, nightFrom, nightTo, setting);
        Check(half.size() == 1 && half[0].greatestVisible && !half[0].wholeVisible,
              "Jupiter setting mid-window: the middle is seen, the whole is not");

        // Ordering preference: an occultation beats an eclipse of equal standing.
        Event occ = evs[0], ecl = evs[0];
        ecl.kind = "Eclipse";
        SkyFn always = [&](double) { SkyAt s; s.ok = true; s.jupiterAltDeg = 40; s.sunAltDeg = -20; return s; };
        auto both = Tonight({occ, ecl}, nightFrom, nightTo, always);
        Check(both.size() == 2 && both[0].rank > both[1].rank, "IMCCE prefers the occultation",
              both.size() == 2 ? std::to_string(both[0].rank) + " vs " + std::to_string(both[1].rank) : "");
    }

    // ── The request path ───────────────────────────────────
    printf("\n7. The request\n");
    const std::string path = OpalePath(2026, 12, 1, 31);
    Check(path == "/api/v1/phenomena/satellites/599?date=2026-12-01T00:00:00.000&timescale=UTC&nbd=31",
          "the Opale path for a month from 2026-12-01", path);

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
