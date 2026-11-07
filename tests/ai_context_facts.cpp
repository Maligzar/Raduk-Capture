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
#include <iterator>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace raduk::core;

static int g_fail = 0;

// The docs folder beside tests, found from this file's own path. run-tests.ps1
// compiles with an absolute source path, so __FILE__ is absolute.
static std::wstring RepoDocsDir() {
    std::filesystem::path here(__FILE__);
    return (here.parent_path().parent_path() / "docs").wstring();
}

// A session log shaped like SessionLog's output, in a fresh temp
// directory. Twelve thousand lines with a handful about autofocus for the
// retrieval to find. Kept under 1 MB: ReadWholeFile returns nothing for
// a file of 1 MB or more, so a larger log would load as no session.
static std::wstring WriteSyntheticSessionLog() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec) / "raduk_ai_context_facts";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    std::ofstream f(dir / "session_20261010_210000.log");
    for (int i = 0; i < 12000; i++) {
        f << "[21:" << (10 + i / 600 % 50) << ":" << (10 + i / 10 % 50)
          << "] [CAPTURE     ] frame " << i << " written, 1280x960 8-bit\n";
        if (i == 6000) {
            f << "[22:15:00] [AUTOFOCUS   ] V-curve sweep started at 168032\n"
              << "[22:16:40] [AUTOFOCUS   ] parabola fit accepted, best 168110\n"
              << "[22:16:41] [AUTOFOCUS   ] focus improved 6.2% over start\n";
        }
    }
    return dir.wstring();
}

// ── What a leaked site looks like ────────────────────────────
//
// Section 6 sets the documents block aside from the broad any-decimal
// scan, because the project's documents are prose full of such numbers
// ("132.10" as a plan section, "12.99' x 7.31'", "15.04"/s", "MSVC
// 14.50"), and holds it to the configured site instead. On a bench or
// CI machine the configured site is the invented one above, so on its
// own that leaves the documents unchecked for any OTHER position --
// a real site pasted into a plan, say.
//
// So for the documents the test also looks for a number written AS A
// POSITION,
// at more than the one decimal the site rule allows:
//   - a pair: two such numbers separated only by a comma, semicolon,
//     spaces, degree signs or hemisphere letters, the first a valid
//     latitude and the second a valid longitude ("41.88, -87.65",
//     "41.88N 87.65W"). Not a slash: "1.45/2.00 TB" is a ratio, and
//     nobody writes a position that way;
//   - a hemisphere suffix: "41.88°N", "87.65 W";
//   - a label right before it: lat, lon, lng, latitude, longitude,
//     site, coordinates, coords or location, as a whole word, with
//     only a few spaces, ':', '=', '(' or quotes between
//     ("lat=41.876", "Site: 41.88").
// What it deliberately no longer flags is a lone, unlabelled
// coordinate-sized number. Half a position with nothing saying it is
// one is not a location, and it is indistinguishable from the corpus's
// own figures. Dotted identifiers ("132.10.1") are not numbers at all.
//
// Returns a description of the first hit, or "" if there is none.
static std::string FindWrittenPosition(const std::string& t) {
    struct Num { size_t start, end; double v; int dec; };
    std::vector<Num> nums;
    for (size_t i = 0; i < t.size(); i++) {
        if (!isdigit((unsigned char)t[i])) continue;
        if (i > 0 && (isdigit((unsigned char)t[i-1]) || t[i-1] == '.')) continue;
        size_t j = i;
        while (j < t.size() && isdigit((unsigned char)t[j])) j++;
        int dec = 0;
        size_t k = j;
        if (j + 1 < t.size() && t[j] == '.' && isdigit((unsigned char)t[j+1])) {
            k = j + 1;
            while (k < t.size() && isdigit((unsigned char)t[k])) { k++; dec++; }
            // A dotted identifier, not a number.
            if (k + 1 < t.size() && t[k] == '.' && isdigit((unsigned char)t[k+1])) {
                i = k;
                continue;
            }
        }
        const bool neg = i > 0 && t[i-1] == '-';
        const size_t st = neg ? i - 1 : i;
        const double v = atof(t.substr(st, k - st).c_str());
        nums.push_back({ st, k, v, dec });
        i = k - 1;
    }

    auto context = [&](size_t a, size_t b) {
        const size_t lo = a >= 30 ? a - 30 : 0;
        const size_t hi = std::min(b + 30, t.size());
        std::string c = t.substr(a, b - a) + " in \"" + t.substr(lo, hi - lo) + "\"";
        for (char& ch : c) if (ch == '\n' || ch == '\r') ch = ' ';
        return c;
    };
    // UTF-8 degree sign is C2 B0.
    auto skipDegree = [&](size_t p) {
        if (p + 1 < t.size() && (unsigned char)t[p] == 0xC2 &&
            (unsigned char)t[p+1] == 0xB0) return p + 2;
        return p;
    };
    auto hemisphereAt = [&](size_t p, const char* set) {
        p = skipDegree(p);
        while (p < t.size() && t[p] == ' ') p++;
        if (p >= t.size() || !strchr(set, t[p])) return false;
        return p + 1 >= t.size() || !isalpha((unsigned char)t[p+1]);
    };
    static const char* kLabels[] = { "lat", "lon", "lng", "latitude",
                                     "longitude", "site", "coordinates",
                                     "coords", "location" };
    auto labelled = [&](size_t start) {
        // The label must be right before the number, with nothing
        // between but a few spaces or ':', '=', '(' or quotes:
        // "Site: 41.88", "lat=41.877". A label further back is prose
        // ("seen from the site | within 0.004 degrees").
        size_t p = start, gap = 0;
        while (p > 0 && gap < 4 && strchr(" \t:=(\"'", t[p-1])) { p--; gap++; }
        size_t q = p;
        while (q > 0 && isalpha((unsigned char)t[q-1])) q--;
        if (q == p) return false;
        std::string w = t.substr(q, p - q);
        for (auto& c : w) c = (char)tolower((unsigned char)c);
        for (const char* l : kLabels) if (w == l) return true;
        return false;
    };

    for (size_t n = 0; n < nums.size(); n++) {
        const Num& a = nums[n];
        if (a.dec < 2 || std::fabs(a.v) > 180.0) continue;
        if (hemisphereAt(a.end, "NSEW"))
            return "a hemisphere-marked " + context(a.start, a.end);
        if (labelled(a.start))
            return "a labelled " + context(a.start, a.end);
        if (n + 1 < nums.size() && std::fabs(a.v) <= 90.0) {
            const Num& b = nums[n + 1];
            if (b.dec >= 2 && std::fabs(b.v) <= 180.0 && b.start - a.end <= 8) {
                bool onlySeparators = true;
                for (size_t p = a.end; p < b.start; p++) {
                    const unsigned char c = (unsigned char)t[p];
                    if (!(c == ',' || c == ';' || c == ' ' ||
                          c == '\t' || c == 0xC2 || c == 0xB0 ||
                          c == 'N' || c == 'S' || c == 'E' || c == 'W'))
                        onlySeparators = false;
                }
                if (onlySeparators)
                    return "a coordinate pair " + context(a.start, b.end);
            }
        }
    }
    return "";
}

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
        // Hermetic on purpose. This read the docs and logs folders under
        // D:/RadukCapture, so the gate passed on one machine only and
        // failed on every fresh clone. The documents now come from the
        // clone this test was compiled in, and the session history is
        // a synthetic log written here, large enough that "selected,
        // not dumped" below still means something.
        const std::wstring docsDir = RepoDocsDir();
        const std::wstring logsDir = WriteSyntheticSessionLog();
        AIContextBuilder b2;
        b2.LoadDocuments(docsDir);
        b2.LoadSessionLogs(logsDir);

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
        //
        // The broad scan below flags ANY number from 10 to 180 with two
        // decimals, which is right for text this rig produced at runtime
        // (logs, state) where a site could appear in any form. It is
        // wrong for the project's own documents, which are reviewed,
        // committed text full of measurements and stage numbers: it
        // tripped on "132.10", a Phase 132 stage. So the documents block
        // is held to the exact rule -- the configured site does not
        // appear at more than one decimal anywhere in the prompt -- and
        // the broad scan runs over everything else.
        Check(!Has(p3, "41.87") && !Has(p3, "87.65"),
              "the configured site is nowhere in the prompt at two decimals", "");
        std::string scan = p3;
        const size_t docs = scan.find("[LOCAL DOCUMENTS]\n");
        if (docs != std::string::npos) {
            // Up to the next section the builder writes. Markdown can
            // itself contain "\n\n[", so a generic search would stop
            // inside the documents.
            size_t next = scan.find("[SESSION HISTORY]\n", docs);
            if (next == std::string::npos) next = scan.find("[SESSION STATE]\n", docs);
            scan.erase(docs, next == std::string::npos ? std::string::npos
                                                      : next - docs);
        }
        Check(scan.size() < p3.size() || !Has(p3, "[LOCAL DOCUMENTS]"),
              "  and the documents block was found to set aside", "");

        bool tooPrecise = false;
        std::string offender;
        for (size_t i = 0; i + 4 < scan.size(); i++) {
            if (!isdigit((unsigned char)scan[i])) continue;
            if (i > 0 && (isdigit((unsigned char)scan[i-1]) || scan[i-1] == '.')) continue;
            size_t j = i;
            while (j < scan.size() && isdigit((unsigned char)scan[j])) j++;
            if (j >= scan.size() || scan[j] != '.') continue;
            size_t k = j + 1, dec = 0;
            while (k < scan.size() && isdigit((unsigned char)scan[k])) { k++; dec++; }
            if (dec < 2) continue;
            const double v = atof(scan.substr(i, k - i).c_str());
            if (v >= 10.0 && v <= 180.0) {
                tooPrecise = true;
                const size_t a = i >= 40 ? i - 40 : 0;
                offender = scan.substr(i, k - i) + " in \"" +
                           scan.substr(a, std::min<size_t>(k + 40, scan.size()) - a) + "\"";
                for (char& c : offender) if (c == '\n' || c == '\r') c = ' ';
                break;
            }
        }
        Check(!tooPrecise,
              "and nothing from the logs or state is coordinate-precise",
              offender);

        // The position scanner (FindWrittenPosition, above) is checked
        // first against the shapes it must
        // catch and the corpus figures it must not, so a scanner that
        // stopped matching anything would fail here rather than pass
        // everything. The positions are the invented test site above.
        struct Probe { const char* text; bool leak; };
        const Probe probes[] = {
            { "Observing site: 41.88, -87.65",            true  },
            { "at 41.876543, -87.654321 tonight",         true  },
            { "41.88N 87.65W",                            true  },
            { "lat=41.877 lon=-87.654",                   true  },
            { "lat 41.88",                                true  },
            { "longitude: -87.65",                        true  },
            { "41.88\xC2\xB0N",                           true  },
            { "one additive field in\n  132.10.\n- **Gates", false },
            { "### 132.10 Dashboard written by the app",  false },
            { "(132.2, 132.9, 132.10)",                   false },
            { "the full sensor sees 12.99' x 7.31' at 0.203\"/px", false },
            { "sidereal 15.04\"/s = 74.1 px/s",           false },
            { "MSVC 14.50 and 22.04 ships 3.22",          false },
            { "plate scale 0.203 arcsec/pixel",           false },
            { "site: 41.9, -87.7 (approx, from rig.ini)", false },
            { "version 132.10.1",                         false },
            { "S: 1.45/2.00 TB, percentages 71/64/72",    false },
            { "view from the site | Within 0.004 degrees", false },
        };
        int probeMisses = 0;
        std::string firstMiss;
        for (const Probe& pr : probes) {
            const bool hit = !FindWrittenPosition(pr.text).empty();
            if (hit != pr.leak) {
                if (probeMisses++ == 0)
                    firstMiss = std::string(pr.leak ? "missed: " : "false hit: ") + pr.text;
            }
        }
        for (char& c : firstMiss) if (c == '\n') c = ' ';
        Check(probeMisses == 0,
              "the position scanner catches leaks and ignores figures",
              probeMisses ? firstMiss : std::to_string(sizeof(probes) / sizeof(probes[0])) + " probes");

        // The documents block the broad scan set aside: no position
        // written out in it, whoever's site it is.
        const std::string written = FindWrittenPosition(
            docs == std::string::npos ? std::string()
                                      : p3.substr(docs, p3.size() - scan.size()));
        Check(written.empty(),
              "  and the documents retrieved write out no position",
              written);

        // Retrieval only shows what one question happened to pull in.
        // The whole corpus is what a question COULD pull in, so scan
        // every document, not just the selected sections.
        std::string docOffender;
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            for (const auto& e : fs::directory_iterator(fs::path(docsDir), ec)) {
                if (e.path().extension() != ".md") continue;
                std::ifstream f(e.path(), std::ios::binary);
                std::string body((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
                const std::string hit = FindWrittenPosition(body);
                if (!hit.empty()) {
                    docOffender = e.path().filename().string() + ": " + hit;
                    break;
                }
            }
        }
        Check(docOffender.empty(),
              "and no document in the corpus writes out a position",
              docOffender);
    }

    printf("\n");
    if (g_fail == 0)
        printf("=== the assistant is told the truth about the rig ===\n\n");
    else
        printf("=== %d CHECK(S) FAILED ===\n\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
