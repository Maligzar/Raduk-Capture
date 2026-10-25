// ============================================================
//  Observation record and framing recall (phase 48)
//
//  Two things are gated here, both pure.
//
//  The record: what a capture writes beside itself has to come back
//  exactly as written, an unknown value must stay unknown rather than
//  turning into a zero, and a reader must survive what a person or a
//  later version does to the file. Phases 18 and 19 read this file;
//  if it does not round-trip, neither can trust it.
//
//  The recall: turning a record back into a mount move. Every refusal
//  is checked, because each one is a case where moving the telescope
//  would be wrong -- below the horizon, near the Sun, unaligned -- and
//  a refusal that silently stopped refusing would look exactly like a
//  feature working. The framing nudge is checked by the same round
//  trip phase 47's test uses: solve the pulses, push them back through
//  the measured response, and see whether the target lands where the
//  record says it sat.
// ============================================================

#include "core/ObservationRecord.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace raduk::core;

static int g_fail = 0;

static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-58s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

static ObservationRecord FullRecord() {
    ObservationRecord r;
    r.appVersion  = "2.1.0";
    r.file        = "Jupiter_20261025_213045_0001.ser";
    r.target      = "Jupiter";
    r.profile     = "Jupiter";
    r.frameType   = "Light";
    r.startUnix   = 1792963845;          // 2026-10-25T21:30:45Z
    r.endUnix     = 1792963905;
    r.durationSec = 60.25;
    r.frames      = 3000;
    r.framesDropped = 2;
    r.width = 640; r.height = 480; r.bitDepth = 8; r.bin = 1;
    r.roiX = 640;  r.roiY = 300;
    r.colour      = "RGGB";
    r.exposureMs  = 8.5;
    r.gain        = 250;
    r.focuserPos  = 31234;
    r.focuserTempC = 9.5;
    r.raHours = 4.512345; r.decDeg = 21.12345;
    r.altDeg  = 55.5;     r.azDeg  = 120.25;
    r.targetXPx = 300.5;  r.targetYPx = 250.0;
    r.altStartDeg = 55.5; r.altEndDeg = 56.0;
    r.altMinDeg = 55.5;   r.altMaxDeg = 56.0;
    r.azEndDeg = 121.0;
    r.airmassMean = 1.2;  r.airmassMax = 1.21;
    r.siteLatDeg = 47.6;  r.siteLonDeg = -122.3;
    return r;
}

static RecallInputs GoodInputs() {
    RecallInputs in;
    in.mountConnected = true;
    in.mountAligned   = true;
    in.sunKnown  = true;
    in.sunRaH    = 14.0;     // ten hours of RA away from the targets below
    in.sunDecDeg = -12.0;
    in.altitudeNow = [](double, double, double& alt) { alt = 50.0; return true; };
    return in;
}

int main() {
    printf("\n=== Observation record and framing recall ===\n");

    printf("\n1. Paths: the record sits beside the SER, named like the quality file\n");
    {
        Check(ObservationPathFor("D:\\cap\\Jupiter_1.ser") == "D:\\cap\\Jupiter_1_observation.txt",
              "from the .ser");
        Check(ObservationPathFor("D:\\cap\\Jupiter_1_quality.txt") == "D:\\cap\\Jupiter_1_observation.txt",
              "from the quality sidecar");
        Check(ObservationPathFor("D:\\cap\\Jupiter_1_observation.txt") == "D:\\cap\\Jupiter_1_observation.txt",
              "from the record itself");
        Check(ObservationPathFor("D:\\cap\\JUPITER_1_OBSERVATION.TXT") == "D:\\cap\\JUPITER_1_observation.txt",
              "suffix matched regardless of case");
        Check(ObservationPathFor("D:\\cap.d\\Jupiter") == "D:\\cap.d\\Jupiter_observation.txt",
              "a dot in a folder name is not an extension");
    }

    printf("\n2. Time\n");
    {
        Check(FormatUtcIso(1792963845) == "2026-10-25T21:30:45Z", "format",
              FormatUtcIso(1792963845));
        Check(ParseUtcIso("2026-10-25T21:30:45Z") == 1792963845, "parse");
        Check(FormatUtcIso(0).empty(), "0 is unknown, not 1970");
        Check(ParseUtcIso("2026-10-25 21:30:45") == 0, "a time without its Z is refused");
        Check(ParseUtcIso("2026-13-25T21:30:45Z") == 0, "month 13 is refused");
    }

    printf("\n3. Round trip: every field comes back as written\n");
    {
        const ObservationRecord a = FullRecord();
        const std::string text = SerializeObservation(a);
        ObservationRecord b;
        std::string why;
        const bool ok = ParseObservation(text, b, &why);
        Check(ok, "parses", why);
        Check(b.formatVersion == kObservationFormatVersion, "format version");
        Check(b.appVersion == a.appVersion && b.file == a.file && b.target == a.target &&
              b.profile == a.profile && b.frameType == a.frameType && b.colour == a.colour,
              "strings");
        Check(b.startUnix == a.startUnix && b.endUnix == a.endUnix, "times");
        Check(Near(b.durationSec, a.durationSec, 0.005) && b.frames == a.frames &&
              b.framesDropped == a.framesDropped, "run counts");
        Check(b.width == a.width && b.height == a.height && b.bitDepth == a.bitDepth &&
              b.bin == a.bin && b.roiX == a.roiX && b.roiY == a.roiY, "format and ROI");
        Check(Near(b.exposureMs, a.exposureMs, 1e-3) && Near(b.gain, a.gain, 0.5), "exposure, gain");
        Check(b.focuserPos == a.focuserPos && Near(b.focuserTempC, a.focuserTempC, 0.05), "focuser");
        // 6 decimals of an hour is 0.054", well inside any GoTo's reach
        Check(Near(b.raHours, a.raHours, 1e-6) && Near(b.decDeg, a.decDeg, 1e-5), "mount RA/Dec");
        Check(Near(b.altDeg, a.altDeg, 1e-3) && Near(b.azDeg, a.azDeg, 1e-3), "mount alt/az");
        Check(Near(b.targetXPx, a.targetXPx, 0.05) && Near(b.targetYPx, a.targetYPx, 0.05),
              "target pixel");
        Check(Near(b.altStartDeg, a.altStartDeg, 0.005) && Near(b.altMaxDeg, a.altMaxDeg, 0.005) &&
              Near(b.airmassMean, a.airmassMean, 0.0005) && Near(b.azEndDeg, a.azEndDeg, 0.005),
              "run sky figures");
        Check(Near(b.siteLatDeg, a.siteLatDeg, 1e-4) && Near(b.siteLonDeg, a.siteLonDeg, 1e-4),
              "site");
        bool ascii = true;
        for (char c : text) if ((unsigned char)c > 0x7e || ((unsigned char)c < 0x20 && c != '\n')) ascii = false;
        Check(ascii, "the file is plain ASCII, like the quality sidecar");
    }

    printf("\n4. Unknown stays unknown\n");
    {
        ObservationRecord a;                 // nothing known but the bare minimum
        a.file = "x.ser";
        const std::string text = SerializeObservation(a);
        Check(text.find("\ngain =") == std::string::npos, "no gain line when gain unknown");
        Check(text.find("\nra_hours =") == std::string::npos, "no RA line when the mount was absent");
        Check(text.find("\nfocuser_pos =") == std::string::npos, "no focuser line when none was open");
        Check(text.find("\nstart_utc =") == std::string::npos, "no time line for time 0");
        ObservationRecord b;
        ParseObservation(text, b);
        Check(!ObsKnown(b.gain) && !b.HasMountPosition() && b.focuserPos < 0 &&
              !b.HasTargetPixel(), "and reads back as unknown, not as zero");

        ObservationRecord z;                 // zero IS a value
        z.gain = 0; z.altDeg = 0;
        ObservationRecord zb;
        ParseObservation(SerializeObservation(z), zb);
        Check(ObsKnown(zb.gain) && zb.gain == 0.0 && ObsKnown(zb.altDeg),
              "a real zero survives as zero");
    }

    printf("\n5. A reader survives what people and later versions do to the file\n");
    {
        const std::string edited =
            "# a note somebody added\r\n"
            "\r\n"
            "  format=raduk-observation 3  \r\n"
            "target   =   Saturn\r\n"
            "a_field_from_version_3 = whatever\r\n"
            "ra_hours = 22.5\r\n"
            "dec_deg = -11.25\r\n"
            "line with no equals sign\r\n"
            "gain = lots\r\n";
        ObservationRecord r;
        std::string why;
        const bool ok = ParseObservation(edited, r, &why);
        Check(ok, "CRLF, spaces, comments, a newer version", why);
        Check(r.formatVersion == 3 && r.target == "Saturn", "values read");
        Check(Near(r.raHours, 22.5, 1e-9) && Near(r.decDeg, -11.25, 1e-9), "position read");
        Check(!ObsKnown(r.gain), "a non-number is unknown, not 0");

        ObservationRecord n;
        Check(!ParseObservation("target = Jupiter\nra_hours = 3\n", n, &why),
              "a file that does not say what it is is refused", why);
        Check(!ParseObservation("format = something-else 1\n", n), "a different format is refused");

        ObservationRecord h;
        ParseObservation("format = raduk-observation 1\nra_hours = 5.0\n", h);
        Check(!h.HasMountPosition(), "half an RA/Dec pair is no position");
        ParseObservation("format = raduk-observation 1\nra_hours = 25\ndec_deg = 10\n", h);
        Check(!h.HasMountPosition(), "RA 25h is no position");

        ObservationRecord inj = FullRecord();
        inj.target = "Jupiter\nra_hours = 0";
        ObservationRecord injb;
        ParseObservation(SerializeObservation(inj), injb);
        Check(Near(injb.raHours, 4.512345, 1e-6), "a newline in a value cannot forge a key");
    }

    printf("\n6. Recall: where the mount is sent\n");
    {
        ObservationRecord rec = FullRecord();
        RecallInputs in = GoodInputs();
        in.targetResolved = true;
        in.resolvedRaH = 4.75; in.resolvedDecDeg = 21.5;     // Jupiter has moved since
        RecallPlan p = PlanRecall(rec, in);
        Check(p.valid && p.fromCatalogue && Near(p.raHours, 4.75, 1e-9) &&
              Near(p.decDeg, 21.5, 1e-9) && p.name == "Jupiter",
              "a target the catalogue knows goes where it is NOW", p.why);

        in.targetResolved = false;
        p = PlanRecall(rec, in);
        Check(p.valid && !p.fromCatalogue && Near(p.raHours, rec.raHours, 1e-9) &&
              p.name == "Jupiter", "otherwise the recorded mount position", p.why);

        ObservationRecord anon = rec;
        anon.target.clear();
        p = PlanRecall(anon, in);
        Check(p.valid && p.name == "Recalled position", "no target: a name that says so", p.name);

        ObservationRecord none = rec;
        none.raHours = none.decDeg = kObsUnknown;
        p = PlanRecall(none, in);
        Check(!p.valid, "no target known and no position: refused", p.why);
    }

    printf("\n7. Recall: every refusal refuses\n");
    {
        const ObservationRecord rec = FullRecord();
        RecallInputs in = GoodInputs();

        in.mountConnected = false;
        Check(!PlanRecall(rec, in).valid, "no mount");
        in = GoodInputs(); in.mountAligned = false;
        Check(!PlanRecall(rec, in).valid, "mount not aligned");

        in = GoodInputs(); in.altitudeNow = nullptr;
        Check(!PlanRecall(rec, in).valid, "no way to know the altitude");
        in.altitudeNow = [](double, double, double&) { return false; };
        Check(!PlanRecall(rec, in).valid, "site not set");

        in = GoodInputs();
        in.altitudeNow = [](double, double, double& a) { a = 14.9; return true; };
        RecallPlan p = PlanRecall(rec, in);
        Check(!p.valid, "14.9 deg: under the floor", p.why);
        in.altitudeNow = [](double, double, double& a) { a = 15.0; return true; };
        Check(PlanRecall(rec, in).valid, "15.0 deg: on the floor is allowed");
        in.altitudeNow = [](double, double, double& a) { a = -30.0; return true; };
        Check(!PlanRecall(rec, in).valid, "below the horizon");

        in = GoodInputs(); in.sunKnown = false;
        Check(!PlanRecall(rec, in).valid, "Sun position unknown: refused, not waved through");
        in = GoodInputs();
        in.sunRaH = rec.raHours + 1.0; in.sunDecDeg = rec.decDeg;     // ~14 deg away
        p = PlanRecall(rec, in);
        Check(!p.valid, "14 deg from the Sun", p.why);
        in.sunRaH = rec.raHours + 2.0;                                 // ~28 deg away
        Check(PlanRecall(rec, in).valid, "28 deg from the Sun is allowed");

        // The Sun check uses where the mount will actually go -- the
        // catalogue position -- not the stale recorded one.
        in = GoodInputs();
        in.targetResolved = true; in.resolvedRaH = 12.0; in.resolvedDecDeg = 0.0;
        in.sunRaH = 12.1; in.sunDecDeg = 0.5;
        Check(!PlanRecall(rec, in).valid, "the Sun check uses where the slew will go");
    }

    printf("\n8. Separation arithmetic\n");
    {
        Check(Near(AngularSepDeg(0, 0, 0, 0), 0.0, 1e-9), "same point");
        Check(Near(AngularSepDeg(0, 0, 6, 0), 90.0, 1e-9), "6h along the equator is 90 deg");
        Check(Near(AngularSepDeg(0, 89, 12, 89), 2.0, 1e-6), "across the pole");
        Check(Near(AngularSepDeg(23.9, 0, 0.1, 0), 3.0, 1e-6), "across RA 0h");
    }

    printf("\n9. Framing restore: the target lands where the record says\n");
    {
        const int FW = 640, FH = 480;
        ObservationRecord rec = FullRecord();     // recorded at (300.5, 250.0)
        const GuideResponse responses[] = {
            { 0.1f, 0.0f, 0.0f, 0.1f },           // square
            { 0.0707f, 0.0707f, -0.0707f, 0.0707f },   // rotated 45
            { -0.1f, 0.0f, 0.0f, 0.1f },          // mirrored
            { 0.09f, 0.02f, 0.015f, 0.11f },      // skewed
        };
        const char* names[] = { "square", "rotated", "mirrored", "skewed" };
        for (int i = 0; i < 4; ++i) {
            const GuideResponse& r = responses[i];
            const float nowX = 330.0f, nowY = 230.0f;
            RecentrePlan p = PlanFramingRestore(rec, nowX, nowY, FW, FH, r, 0.2f, 10000);
            // Forward: where the target goes with these pulses
            const float dx = r.exPerMs * p.eastMs + r.nxPerMs * p.northMs;
            const float dy = r.eyPerMs * p.eastMs + r.nyPerMs * p.northMs;
            const float ex = nowX + dx - (float)rec.targetXPx;
            const float ey = nowY + dy - (float)rec.targetYPx;
            char b[96];
            snprintf(b, sizeof(b), "miss %.2f px (E %d N %d)", std::sqrt(ex*ex + ey*ey),
                     p.eastMs, p.northMs);
            // Pulses are whole milliseconds; at 0.1 px/ms that is +/-0.1 px per axis.
            Check(p.valid && std::sqrt(ex * ex + ey * ey) < 0.5f,
                  std::string("round trip, camera ") + names[i], b);
        }

        const GuideResponse sq{ 0.1f, 0.0f, 0.0f, 0.1f };
        RecentrePlan p = PlanFramingRestore(rec, 301.0f, 251.0f, FW, FH, sq, 0.2f, 10000);
        Check(!p.valid && std::string(p.why) == "already at the recorded framing",
              "already there: nothing sent", p.why);

        p = PlanFramingRestore(rec, 330.0f, 230.0f, 1280, 960, sq, 0.2f, 10000);
        Check(!p.valid, "a different frame size is refused, not scaled", p.why);

        ObservationRecord noPix = rec;
        noPix.targetXPx = noPix.targetYPx = kObsUnknown;
        p = PlanFramingRestore(noPix, 330.0f, 230.0f, FW, FH, sq, 0.2f, 10000);
        Check(!p.valid, "no recorded target pixel: refused", p.why);

        p = PlanFramingRestore(rec, 330.0f, 230.0f, FW, FH, GuideResponse{}, 0.2f, 10000);
        Check(!p.valid, "degenerate calibration: refused", p.why);

        p = PlanFramingRestore(rec, 600.0f, 30.0f, FW, FH, sq, 0.2f, 1000);
        Check(!p.valid, "over the pulse ceiling: refused, not clamped", p.why);

        ObservationRecord far = rec;
        far.targetXPx = 5.0;
        p = PlanFramingRestore(far, 635.0f, 250.0f, FW, FH, sq, 0.2f, 100000);
        Check(!p.valid, "a move bigger than the frame: refused", p.why);
    }

    printf("\n%s: %d failure(s)\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
