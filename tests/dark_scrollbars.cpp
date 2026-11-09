// ============================================================
//  Phase 132.6b: dark scrollbars, and what they must not touch
//
//  1. The undocumented entry point is chosen by Windows build: none
//     before 1809, AllowDarkModeForApp on 1809, SetPreferredAppMode
//     from 1903.
//  2. Edit boxes and list boxes are never themed: a themed one stops
//     honouring the app's WM_CTLCOLOR* brushes and turns white.
//  3. The dark theme goes on AFTER Theme::StyleStockControls de-themes
//     the list views, or the de-theme would take it straight off.
//  4. Every list row is painted by the app (CDRF_SKIPDEFAULT on each),
//     so the theme cannot restyle rows, and the grid lines are gone.
// ============================================================

#include "gui/DarkScrollbars.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace raduk::gui::darkbars;

static int g_fail = 0;
static void Check(bool ok, const std::string& what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) g_fail++;
}
static std::string Root() {
    std::string f = __FILE__;
    for (char& c : f) if (c == '\\') c = '/';
    const size_t at = f.rfind("/tests/");
    return at == std::string::npos ? std::string(".") : f.substr(0, at);
}
static std::string Read(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main() {
    printf("\n=== Phase 132.6b: dark scrollbars ===\n");

    printf("\n1. By build\n");
    Check(AppModeFor(0) == AppMode::None && AppModeFor(17134) == AppMode::None,
          "nothing before Windows 10 1809");
    Check(AppModeFor(17763) == AppMode::AllowDarkModeForApp, "1809: AllowDarkModeForApp");
    Check(AppModeFor(18362) == AppMode::SetPreferredAppMode &&
              AppModeFor(26100) == AppMode::SetPreferredAppMode,
          "1903 and later, Windows 11 included: SetPreferredAppMode");

    printf("\n2. Never themed\n");
    for (const wchar_t* c : {L"Edit", L"edit", L"ListBox", L"ComboBox", L"Button", L"Static",
                             L"SysTabControl32", L"msctls_trackbar32"})
        Check(IsSkippedClass(c), "skipped: " + std::string(c, c + wcslen(c)));
    Check(!IsSkippedClass(L"RadukControlPanelHost"), "an app class is not skipped");

    printf("\n3. Order\n");
    {
        const std::string m = Read(Root() + "/src/main.cpp");
        const size_t chrome = m.find("auto styleChrome");
        const size_t stock = m.find("Theme::StyleStockControls(h)", chrome);
        const size_t dark = m.find("darkbars::Style(h)", chrome);
        Check(chrome != std::string::npos && stock != std::string::npos && dark != std::string::npos &&
                  stock < dark,
              "styleChrome themes the bars after the stock controls are de-themed");
        Check(m.find("darkbars::Enable(") != std::string::npos &&
                  m.find("darkbars::Enable(") < chrome,
              "and enables dark mode before the first styling");
    }

    printf("\n4. Rows\n");
    {
        const std::string s = Read(Root() + "/src/gui/StripDraw.h");
        const size_t proc = s.find("ListParentProc(");
        const size_t item = s.find("case CDDS_ITEMPREPAINT", proc);
        const size_t end = s.find("default:", item);
        const std::string body = s.substr(item, end - item);
        Check(body.find("PaintRow(") != std::string::npos &&
                  body.find("CDRF_SKIPDEFAULT") != std::string::npos &&
                  body.find("CDRF_DODEFAULT") == std::string::npos,
              "every row is the app's: painted, and the control's own skipped");
        Check(s.find("ListView_SetExtendedListViewStyleEx(c, LVS_EX_GRIDLINES, 0)") != std::string::npos,
              "the system grid lines are switched off");
    }

    printf("\n%s\n", g_fail ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return g_fail ? 1 : 0;
}
