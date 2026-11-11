// ============================================================
//  The [DASH] button: finding the dashboard and building its link
//
//  Opening a browser cannot be tested on the bench; what it is handed
//  can. Three things would break the button quietly:
//
//    1. Searching the wrong folders -- the exe lives two below the
//       repository root that holds the page, in build\Release.
//    2. A path that is not a valid file:// URL. A space or a '#' in a
//       folder name would cut the path short or start the fragment in
//       the wrong place, and the browser would open nothing.
//    3. The token landing anywhere but the fragment, or a non-hex
//       value being passed through at all.
// ============================================================

#include "core/DashboardLaunch.h"

#include <cstdio>
#include <string>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-60s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static std::string Narrow(const std::wstring& w) { return std::string(w.begin(), w.end()); }

int main() {
    printf("\n=== [DASH]: the dashboard link ===\n");

    printf("\n1. Where it looks\n");
    {
        const auto d = DashboardSearchDirs(L"D:\\RadukCapture\\build_emu\\Release");
        Check(d.size() >= 3 && d[0] == L"D:\\RadukCapture\\build_emu\\Release",
              "beside the exe first");
        Check(d.size() >= 3 && d[2] == L"D:\\RadukCapture",
              "the repository root two levels up", d.size() >= 3 ? Narrow(d[2]) : "");
        Check(!d.empty() && d.back() == L"D:", "and stops at the drive", Narrow(d.back()));
        Check(DashboardSearchDirs(L"D:\\RadukCapture\\build\\Release\\")[0] ==
                  L"D:\\RadukCapture\\build\\Release",
              "a trailing backslash does not make an empty first entry");
        Check(DashboardSearchDirs(L"C:\\a\\b\\c\\d\\e\\f\\g").size() == 5,
              "at most five folders, so a deep install does not search the disk");
        Check(DashboardSearchDirs(L"").empty(), "an empty exe folder searches nothing");
    }

    printf("\n2. The path as a file:// URL\n");
    {
        Check(FileUrlFromPath(L"D:\\RadukCapture\\RadukCaptureDashboard.html") ==
                  "file:///D:/RadukCapture/RadukCaptureDashboard.html",
              "the plain case");
        Check(FileUrlFromPath(L"C:\\My Scope\\Dash.html") == "file:///C:/My%20Scope/Dash.html",
              "a space is encoded");
        Check(FileUrlFromPath(L"C:\\a#b\\x.html") == "file:///C:/a%23b/x.html",
              "a '#' in a folder cannot start the fragment");
        Check(FileUrlFromPath(L"C:\\a%b\\x.html") == "file:///C:/a%25b/x.html",
              "a '%' is encoded, not taken as an escape");
        Check(FileUrlFromPath(L"C:\\Sternwarte\\M\u00FCnchen\\x.html") ==
                  "file:///C:/Sternwarte/M%C3%BCnchen/x.html",
              "non-ASCII goes as UTF-8");
        Check(FileUrlFromPath(L"C:\\\U0001F52D\\x.html") == "file:///C:/%F0%9F%94%AD/x.html",
              "a character outside the BMP, from its surrogate pair");
    }

    printf("\n3. The token\n");
    {
        const std::wstring page = L"D:\\RadukCapture\\RadukCaptureDashboard.html";
        const std::string tok(64, 'a');
        Check(DashboardUrl(page, tok) ==
                  "file:///D:/RadukCapture/RadukCaptureDashboard.html#token=" + tok,
              "it rides in the fragment, never the query");
        Check(DashboardUrl(page, "").find('#') == std::string::npos,
              "no token, no fragment");
        Check(DashboardUrl(page, "abc def").find('#') == std::string::npos,
              "a non-hex value is left out, not escaped");
        Check(DashboardUrl(page, "<script>").find('#') == std::string::npos,
              "  including anything that could be markup");
    }

    printf("\n4. The page the app writes (132.10a)\n");
    {
        const std::string page =
            "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"UTF-8\">\n"
            "<style>:root{--text:#b8d4e8}</style>\n</head>\n<body><header>x</header></body></html>";
        const std::string css = "@font-face{font-family:'Space Mono'}";

        const std::string day = PrepareDashboardHtml(page, css, false);
        const size_t ours = day.find("<style id=\"raduk-app\">");
        Check(ours != std::string::npos, "the fonts go in as a style of our own");
        Check(ours == day.find("<head>") + 6, "  first thing inside <head>");
        Check(ours < day.find("<style>:root"),
              "  before the page's own rules, so the page still wins a tie");
        Check(day.find("<header>x</header>") != std::string::npos &&
                  day.find("<style id", day.find("<body>")) == std::string::npos,
              "  and not inside <header>, whose name starts the same way");
        Check(day.find("data-mode") == std::string::npos, "day: no night flag");
        Check(day.size() == page.size() + css.size() + std::string("<style id=\"raduk-app\"></style>").size(),
              "nothing else in the page changes");

        const std::string night = PrepareDashboardHtml(page, css, true);
        Check(night.find("<html lang=\"en\" data-mode=\"night\">") != std::string::npos,
              "night: the flag goes on <html>");

        const std::string was =
            "<html data-mode=\"night\" lang=\"en\"><head></head></html>";
        Check(PrepareDashboardHtml(was, "", false) == "<html lang=\"en\"><head></head></html>",
              "a flag already there is removed for day");
        Check(PrepareDashboardHtml(was, "", true) ==
                  "<html lang=\"en\" data-mode=\"night\"><head></head></html>",
              "  and not doubled for night");
        Check(PrepareDashboardHtml("<HTML><HEAD></HEAD></HTML>", "a", true) ==
                  "<HTML data-mode=\"night\"><HEAD><style id=\"raduk-app\">a</style></HEAD></HTML>",
              "tags in capitals are found");
        Check(PrepareDashboardHtml("<p>bare</p>", "a", true) ==
                  "<style id=\"raduk-app\">a</style><p>bare</p>",
              "no <head>: the style goes first, and no flag has anywhere to go");
        Check(PrepareDashboardHtml(page, "", false) == page,
              "no CSS and day: the page is returned unchanged");
    }

    printf("\n5. Edge's app window\n");
    {
        const std::string url = DashboardUrl(L"C:\\R C\\data\\dashboard.html", std::string(64, 'f'));
        const std::wstring a = EdgeAppArguments(url);
        Check(a == L"--app=\"" + std::wstring(url.begin(), url.end()) + L"\"",
              "--app= with the whole URL, quoted");
        Check(a.find(L"#token=") != std::wstring::npos, "  the token still in the fragment");
        Check(a.find(L' ', 7) == std::wstring::npos, "  a space in the path cannot split it",
              Narrow(a));
        const auto c = EdgeCandidates(L"C:\\Program Files (x86)", L"C:\\Program Files",
                                      L"C:\\Users\\u\\AppData\\Local");
        Check(c.size() == 3 &&
                  c[0] == L"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
              "Edge's usual home first", c.empty() ? "" : Narrow(c[0]));
        Check(EdgeCandidates(L"", L"C:\\PF", L"").size() == 1, "an unknown folder is skipped");
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
