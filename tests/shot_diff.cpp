// ============================================================
//  Phase 132.0: the visual baseline diff
//
//  Every later stage of 132 is judged by this: which windows changed,
//  and were they the ones the stage said it would change. So the ways
//  it could lie are the ways that matter:
//
//    1. Pairing the wrong windows -- the trailing index is z-order and
//       moves between runs, so it must not be part of the match.
//    2. Calling a live window changed because its clock ticked -- the
//       noise floor exists for that, and must not swallow a real change.
//    3. Missing a size change, or a window that vanished.
//    4. A report that hides the problems below the routine rows.
// ============================================================

#include "core/ShotDiff.h"

#include <cstdio>
#include <string>

using namespace raduk::core::shotdiff;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static Image Solid(int w, int h, uint8_t v) {
    Image im;
    im.w = w; im.h = h;
    im.px.assign((size_t)w * h * 4, v);
    return im;
}

static void Set(Image& im, int x, int y, uint8_t v) {
    uint8_t* p = &im.px[((size_t)y * im.w + x) * 4];
    p[0] = p[1] = p[2] = v;
}

int main() {
    printf("\n=== Phase 132.0: shot diff ===\n");

    printf("\n1. File names\n");
    {
        ShotName n;
        Check(ParseShotName("day_GoTo_Panel_3.png", n) && n.shot == "day" &&
                  n.title == "GoTo_Panel" && n.index == 3,
              "shot, title with underscores, index");
        Check(ParseShotName("C:\\runs\\shots\\night_Sky_Map_12.PNG", n) && n.shot == "night" &&
                  n.title == "Sky_Map" && n.index == 12,
              "a full path and an upper-case extension");
        Check(!ParseShotName("day_preview.png", n), "the camera frame is not a window shot");
        Check(!ParseShotName("day_Status.png", n), "no index is not a window shot");
        Check(!ParseShotName("report.txt", n), "not a PNG");
        Check(!ParseShotName("day_3.png", n), "no title");
    }

    printf("\n2. Keys ignore z-order\n");
    {
        const auto a = KeyFiles({"day_Histogram_0.png", "day_Sky_Map_1.png", "day_preview.png"});
        const auto b = KeyFiles({"day_Sky_Map_0.png", "day_Histogram_4.png"});
        Check(a.size() == 2 && b.size() == 2, "two windows each, preview dropped");
        Check(a.count("day/Sky_Map") && b.count("day/Sky_Map") &&
                  a.at("day/Sky_Map") == "day_Sky_Map_1.png" && b.at("day/Sky_Map") == "day_Sky_Map_0.png",
              "the sky map pairs with the sky map whatever its index");

        const auto d = KeyFiles({"day_untitled_7.png", "day_untitled_2.png", "night_untitled_5.png"});
        Check(d.count("day/untitled") && d.at("day/untitled") == "day_untitled_2.png",
              "two same-titled windows: the lower index is first");
        Check(d.count("day/untitled#2") && d.at("day/untitled#2") == "day_untitled_7.png",
              "and the second is #2");
        Check(d.count("night/untitled") == 1, "day and night are separate keys");

        // The main window's title carries the build stamp; two builds of
        // the same code must still pair it with itself.
        const auto e = KeyFiles({"day_RadukCapture_v3_0_0___build_Nov__7_2026_11_08_57____EMULATOR__0.png"});
        const auto f = KeyFiles({"day_RadukCapture_v3_0_0___build_Dec_12_2026_23_59_01____EMULATOR__4.png"});
        Check(e.size() == 1 && f.size() == 1 && e.begin()->first == f.begin()->first,
              "the main window keeps its key across builds", e.begin()->first);
        Check(e.begin()->first == "day/RadukCapture_v3_0_0___build_____EMULATOR_",
              "only the stamp is cut", e.begin()->first);
        const auto g = KeyFiles({"day_D__RadukCapture_build_emu_Release_RadukCapture_exe_2.png"});
        Check(g.count("day/D__RadukCapture_build_emu_Release_RadukCapture_exe") == 1,
              "a build_ with no time after it is left alone");
    }

    printf("\n3. Pixels\n");
    {
        Image a = Solid(100, 50, 20), b = Solid(100, 50, 20);
        Diff d = Compare(a, b);
        Check(!d.sizeChanged && d.changed == 0 && d.fraction == 0.0, "identical images do not differ");
        Check(d.x1 < d.x0, "and have no bounding box");

        Set(b, 10, 5, 20 + kChannelTolerance);
        Check(Compare(a, b).changed == 0, "a step at the tolerance is dithering, not change");
        Set(b, 10, 5, 20 + kChannelTolerance + 1);
        Set(b, 60, 40, 200);
        d = Compare(a, b);
        Check(d.changed == 2, "a step past it counts", std::to_string(d.changed));
        Check(d.x0 == 10 && d.y0 == 5 && d.x1 == 60 && d.y1 == 40, "the box spans both changes");

        Image a2 = a;
        a2.px[3] = 0;   // alpha only
        Check(Compare(a, a2).changed == 0, "alpha is ignored; WindowShot forces it opaque");

        d = Compare(Solid(100, 50, 0), Solid(100, 51, 0));
        Check(d.sizeChanged && d.fraction == 1.0, "a size change is total");
        Check(IsChanged(d, 1.0), "and always counts, whatever the noise");
        Check(Compare(Image{}, Solid(4, 4, 0)).sizeChanged, "a missing image counts as a size change");
    }

    printf("\n4. The noise floor\n");
    {
        Diff d;
        d.fraction = 0.0005;
        Check(!IsChanged(d, 0.0), "a blinking caret on a static window is not a change");
        d.fraction = 0.002;
        Check(IsChanged(d, 0.0), "0.2% of a static window is");
        d.fraction = 0.03;
        Check(!IsChanged(d, 0.02), "a live window as busy as it was against itself is not");
        d.fraction = 0.06;
        Check(IsChanged(d, 0.02), "three times its own noise is");

        const std::map<std::string, double> n = {{"day/Status", 0.0213}, {"night/Sky_Map", 0.15}};
        const auto back = ParseNoise(FormatNoise(n));
        Check(back.size() == 2 && back.at("day/Status") > 0.02129 && back.at("day/Status") < 0.02131,
              "the noise file round-trips");
        Check(ParseNoise("# c\r\nday/A\t0.5\r\nbad line\n\tnokey\n").size() == 1,
              "comments, CRLF and malformed lines are skipped");
    }

    printf("\n5. What was expected\n");
    {
        const auto e = SplitExpect(" GoTo , sky_map,,");
        Check(e.size() == 2 && e[0] == "GoTo" && e[1] == "sky_map", "the list trims and drops empties");
        Check(Expected("night/Sky_Map", e), "case-insensitive fragment match");
        Check(!Expected("day/Histogram", e), "an unnamed window is not expected");
        Check(SplitExpect("").empty() && !Expected("day/GoTo", {}), "an empty list expects nothing");

        Diff big;
        big.fraction = 0.5;
        Check(Judge("day/GoTo_Panel", big, 0.0, e) == Verdict::Expected, "a named window changing is expected");
        Check(Judge("day/Histogram", big, 0.0, e) == Verdict::Changed, "an unnamed one is a problem");
        Check(Judge("day/Histogram", Diff{}, 0.0, e) == Verdict::Same, "an unchanged one is the same");
    }

    printf("\n6. The heatmap\n");
    {
        Image a = Solid(4, 2, 100), b = Solid(4, 2, 100);
        Set(b, 1, 1, 250);
        const Diff d = Compare(a, b);
        const Image h = Heatmap(b, d);
        const uint8_t* hit  = &h.px[((size_t)1 * 4 + 1) * 4];
        const uint8_t* miss = &h.px[0];
        Check(h.Valid() && hit[2] == 0xFF && hit[0] == 0x3C, "a changed pixel is painted");
        Check(miss[0] == 25 && miss[3] == 255, "the rest is dimmed to a quarter, opaque");
        Check(!Heatmap(b, Compare(a, Solid(5, 2, 0))).Valid(), "no heatmap across a size change");
    }

    printf("\n7. The report\n");
    {
        std::vector<Row> rows(4);
        rows[0].key = "day/Alpha";   rows[0].verdict = Verdict::Same;
        rows[1].key = "day/Beta";    rows[1].verdict = Verdict::Expected; rows[1].fraction = 0.2;
        rows[2].key = "day/Gamma";   rows[2].verdict = Verdict::Changed;  rows[2].fraction = 0.1;
        rows[3].key = "night/Delta"; rows[3].verdict = Verdict::Missing;  rows[3].fraction = 1.0;
        SortRows(rows);
        Check(rows[0].key == "day/Gamma" && rows[1].key == "night/Delta",
              "problems sort first");
        Check(rows[2].key == "day/Beta" && rows[3].key == "day/Alpha", "then expected, then the same");

        rows[0].before = "file:///a/<x>.png";
        const std::string html = Report(rows, "UI diff", "Beta");
        Check(html.find("2 unexpected") != std::string::npos, "the summary counts the problems");
        Check(html.find("1 changed as expected") != std::string::npos, "and the expected changes");
        Check(html.find("&lt;x&gt;") != std::string::npos && html.find("<x>") == std::string::npos,
              "paths are escaped");
        Check(html.find("id=\"day/Alpha\"") == std::string::npos,
              "an unchanged window gets a table row, not a section");
        Check(html.find("http") == std::string::npos, "the page asks the network for nothing");

        const std::string clean = Report({}, "UI diff", "");
        Check(clean.find("nothing unexpected") != std::string::npos &&
                  clean.find("Expected: nothing") != std::string::npos,
              "an empty run says so");
    }

    printf("\n%s\n", g_fail ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return g_fail ? 1 : 0;
}
