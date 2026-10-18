// ============================================================
//  The facts the assistant is handed
//
//  P3.6 asks for "a set of questions with known answers drawn from the
//  rig's own numbers" before the model's scope is widened. This is the
//  half of that which can be checked without a model: whether the
//  PROMPT is right. A model cannot be right about a number it was
//  given wrongly, and the worst defect found so far was exactly that
//  -- a sensor pixel pitch that contradicted the plate scale two lines
//  below it, sitting in the prompt for months.
//
//  It also gates the site-precision rule. The observing site must
//  never reach the model at more than one decimal: everything in this
//  context can be quoted back, logged, or pasted into a bug report.
// ============================================================

#include "core/AIContextBuilder.h"
#include "core/RigConfig.h"

#include <cstdio>
#include <string>
#include <vector>
#include <cmath>

using namespace raduk::core;

static int g_fail = 0;

static void Check(bool ok, const std::string& what, const std::string& detail) {
    printf("  [%s] %-58s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

// The planner is a template parameter, so the test can supply the
// smallest thing that satisfies it.
struct FakeRow {
    std::string name = "Jupiter";
    std::string type = "Planet";
    float       mag = -2.5f;
    float       altNow = 47.0f;
    float       peakAlt = 52.0f;
    int         minsLeft = 90;
    std::string windowStr = "good";
};

// Does the text contain this, allowing for the number being formatted
// however the stream chose?
static bool Has(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

int main() {
    printf("\n=== The facts the assistant is handed ===\n");

    AISessionState s;
    s.sensorWidth  = 3840;
    s.sensorHeight = 2160;
    s.roiWidth     = 1280;
    s.roiHeight    = 960;
    s.gain         = 300;
    s.exposureMs   = 12.5f;
    s.bitDepth     = 8;
    s.seeingScore  = 7.0f;

    // ── A site to redact ─────────────────────────────────────
    //
    // This test ran for its whole life with NO SITE LOADED. rig.ini is
    // untracked and lives beside the app in build/Release; the tests run
    // from tests/_build, where there is none, so siteLatDeg was 0.0 and
    // the precision check below was asserting that "0.0, 0.0" carries no
    // more than one decimal. It cannot: there was nothing there to leak.
    // The one test guarding the one rule that matters passed because it
    // was never given anything to guard.
    //
    // So load a site here, in process. Deliberately NOT the real one --
    // these digits are invented, and the real site stays in the
    // untracked rig.ini where it belongs. What matters is the SHAPE:
    // coordinate-sized, eight decimals, so anything that forwards it at
    // full precision has somewhere to show.
    RigConfig::Get().siteLatDeg = 41.87654321;
    RigConfig::Get().siteLonDeg = -87.65432109;

    AIContextBuilder b;
    std::vector<FakeRow> planner{ FakeRow{} };
    const std::string p = b.Build(s, planner, "what is my field of view?");

    const float scale = RigConfig::Get().plateScaleArcsecPx;

    // ── 1. The sensor ────────────────────────────────────────
    printf("\n1. Equipment facts\n");
    {
        Check(Has(p, "2.0um"),
              "the pixel pitch is 2.0um", "");
        Check(!Has(p, "2.9um"),
              "and NOT the 2.9um that contradicted the plate scale",
              "flagged in CODE_REVIEW_2026-06-10 and shipped anyway");

        // 2.0um at 2032mm IS 0.203 arcsec/px. If the prompt ever states
        // a pitch and a scale that disagree, one of them is a lie and
        // the model cannot tell which.
        const double derived = 206.265 * 2.0 / 2032.0;
        char d[96];
        snprintf(d, sizeof(d), "206.265 * 2.0 / 2032 = %.4f, rig.ini says %.4f",
                 derived, (double)scale);
        Check(std::fabs(derived - scale) < 0.002,
              "pitch and plate scale agree with each other", d);
    }

    // ── 2. Numbers from the app, not from the prompt ─────────
    printf("\n2. Grounded in the rig, not recalled\n");
    {
        char want[64];
        snprintf(want, sizeof(want), "%.3f arcsec/pixel", (double)scale);
        Check(Has(p, want), "the plate scale quoted is rig.ini's", want);
        Check(Has(p, "from rig.ini"),
              "and it says where it came from", "");

        Check(Has(p, "3840x2160"), "the sensor size is the camera's", "");
        Check(Has(p, "1280x960"),  "the capture format is the live one", "");
    }

    // ── 3. Field of view, computed ───────────────────────────
    printf("\n3. Field of view\n");
    {
        // The question this exists to answer: a 1280x960 crop is about
        // 4.3' wide, which does not hold Jupiter's moons; the full
        // sensor is about 13' and does.
        char full[64], roi[64];
        snprintf(full, sizeof(full), "%.1f'", 3840 * scale / 60.0);
        snprintf(roi,  sizeof(roi),  "%.1f'", 1280 * scale / 60.0);
        Check(Has(p, full), "the full-sensor field is stated", full);
        Check(Has(p, roi),  "and the current format's field too", roi);

        const double fullArcmin = 3840 * scale / 60.0;
        char d[96];
        snprintf(d, sizeof(d), "%.2f arcmin", fullArcmin);
        Check(fullArcmin > 12.0 && fullArcmin < 14.0,
              "which comes out near the 13' this rig really has", d);
    }

    // ── 4. The site, at one decimal and no finer ─────────────
    printf("\n4. The observing site\n");
    {
        // The security rule, as a test rather than a comment. Anything
        // in this prompt can be quoted back by the model, written to a
        // log, or pasted into a bug report.
        //
        // Looked for by pattern: a latitude or longitude with two or
        // more decimals anywhere in the context.
        bool tooPrecise = false;
        std::string offender;
        for (size_t i = 0; i + 4 < p.size(); i++) {
            if (!isdigit((unsigned char)p[i])) continue;
            if (i > 0 && (isdigit((unsigned char)p[i-1]) || p[i-1] == '.')) continue;
            size_t j = i;
            while (j < p.size() && isdigit((unsigned char)p[j])) j++;
            if (j >= p.size() || p[j] != '.') continue;
            size_t k = j + 1, dec = 0;
            while (k < p.size() && isdigit((unsigned char)p[k])) { k++; dec++; }
            if (dec < 2) continue;
            // Only latitude/longitude magnitudes are of interest; the
            // plate scale is 0.203 and must stay.
            const double v = atof(p.substr(i, k - i).c_str());
            if (v >= 10.0 && v <= 180.0) {
                tooPrecise = true;
                offender = "a " + std::to_string(dec) +
                           "-decimal coordinate-sized number";
                break;
            }
        }
        Check(!tooPrecise,
              "no coordinate-sized number carries more than one decimal",
              offender);
        Check(Has(p, "approx, from rig.ini"),
              "and the site is labelled as approximate", "");

        // Positive side of the same rule. The scan above proves nothing
        // if the site never reached the prompt at all -- which is
        // exactly the state this test was in. These say the rounded
        // site IS present and the full-precision one is NOT.
        Check(Has(p, "41.9") && Has(p, "-87.7"),
              "the rounded site did reach the prompt",
              "41.9, -87.7 from 41.87654321, -87.65432109");
        Check(!Has(p, "41.87") && !Has(p, "87.654"),
              "and the full-precision site did not", "");
    }

    // ── 5. The live state ────────────────────────────────────
    printf("\n5. Session state\n");
    {
        Check(Has(p, "300"),  "the gain is the live one", "");
        Check(Has(p, "12.5"), "and so is the exposure", "");
    }

    // ── 6. Retrieval over the local corpus (P3.6 option B) ───
    //
    // The assistant could see the help file and nothing else. These
    // check that the wider corpus reaches the prompt, that it is
    // SELECTED rather than dumped, and that widening it did not
    // smuggle the observing site in.
    printf("\n6. Retrieval over the local corpus\n");
    {
        AIContextBuilder b2;
        b2.LoadDocuments(L"D:\\RadukCapture\\docs");
        b2.LoadSessionLogs(L"D:\\RadukCapture\\logs");

        // A question the help file cannot answer and the project's own
        // notes can.
        const std::string p2 =
            b2.Build(s, planner, "what did the clear night checklist say "
                                 "about plate scale?");
        Check(Has(p2, "[LOCAL DOCUMENTS]"),
              "a question about the project's notes reaches them", "");

        // A question about what happened, which only the logs know.
        const std::string p3 =
            b2.Build(s, planner, "what happened with autofocus last session?");
        Check(Has(p3, "[SESSION HISTORY]"),
              "a question about the past reaches the session logs", "");
        Check(Has(p3, "session_"),
              "  and says which session it is quoting", "");

        // Selected, not dumped. A session log here is tens of
        // thousands of lines; the whole point is that a handful
        // arrive, not all of them.
        Check(p3.size() < 60000,
              "the prompt stays bounded with the corpus in it",
              std::to_string(p3.size()) + " bytes");

        // A question with nothing to match must not drag history in.
        const std::string p4 = b2.Build(s, planner, "hello");
        Check(!Has(p4, "[SESSION HISTORY]"),
              "a question with nothing to match pulls no history in", "");

        // And the site rule still holds with all of it present. This
        // is the check that matters most: the corpus grew, and
        // everything in the prompt can be quoted back.
        bool tooPrecise = false;
        for (size_t i = 0; i + 4 < p3.size(); i++) {
            if (!isdigit((unsigned char)p3[i])) continue;
            if (i > 0 && (isdigit((unsigned char)p3[i-1]) || p3[i-1] == '.')) continue;
            size_t j = i;
            while (j < p3.size() && isdigit((unsigned char)p3[j])) j++;
            if (j >= p3.size() || p3[j] != '.') continue;
            size_t k = j + 1, dec = 0;
            while (k < p3.size() && isdigit((unsigned char)p3[k])) { k++; dec++; }
            if (dec < 2) continue;
            const double v = atof(p3.substr(i, k - i).c_str());
            if (v >= 10.0 && v <= 180.0) { tooPrecise = true; break; }
        }
        Check(!tooPrecise,
              "and the site rule still holds with documents and logs in",
              "");
    }

    printf("\n");
    if (g_fail == 0)
        printf("=== the assistant is told the truth about the rig ===\n\n");
    else
        printf("=== %d CHECK(S) FAILED ===\n\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
