// ============================================================
//  Phase 132.3b: no Direct2D brush skips night vision
//
//  132.3b found the sky map painting its Sun, Moon, planets and every
//  object label with brushes that never went through Theme::NVf: a
//  yellow Sun and a blue Neptune on a night-vision screen. Nothing
//  caught it, because night vision is a transform applied at each call
//  site, and a call site that forgets it is silent.
//
//  So this reads the source. In each Direct2D window, every
//  CreateSolidColorBrush and every Clear must take its colour through
//  Theme::NVf or Theme::Color -- or from a variable, which is then that
//  variable's own business -- and never from a bare D2D1::ColorF
//  literal. Pure black is allowed: it is black in both modes.
//
//  A source scan is a blunt tool. It cannot see a literal hidden in a
//  helper, and it does not prove the colour is right. What it does is
//  make the specific mistake that shipped impossible to ship again.
// ============================================================

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-56s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static std::string Root() {
    std::string f = __FILE__;
    for (char& c : f) if (c == '\\') c = '/';
    const size_t at = f.rfind("/tests/");
    return at == std::string::npos ? std::string(".") : f.substr(0, at);
}

static std::string Read(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static int LineOf(const std::string& s, size_t pos) {
    int n = 1;
    for (size_t i = 0; i < pos && i < s.size(); ++i) if (s[i] == '\n') ++n;
    return n;
}

// The argument list of a call starting at `open` (the '('), to its
// matching ')'.
static std::string Args(const std::string& s, size_t open) {
    int depth = 0;
    for (size_t i = open; i < s.size(); ++i) {
        if (s[i] == '(') ++depth;
        else if (s[i] == ')' && --depth == 0) return s.substr(open + 1, i - open - 1);
    }
    return s.substr(open + 1);
}

static bool IsBlack(const std::string& args, size_t colorAt) {
    // "D2D1::ColorF(0, 0, 0" or "(0.0f, 0.0f, 0.0f": every channel zero.
    const size_t p = args.find('(', colorAt);
    if (p == std::string::npos) return false;
    int zeros = 0;
    std::string tok;
    for (size_t i = p + 1; i < args.size() && zeros < 3; ++i) {
        const char c = args[i];
        if (c == ',' || c == ')') {
            std::string t;
            for (char d : tok) if (d != ' ' && d != 'f' && d != '\t' && d != '\n') t += d;
            if (t != "0" && t != "0.0" && t != "0." && t != ".0") return false;
            ++zeros;
            tok.clear();
            if (c == ')') break;
        } else {
            tok += c;
        }
    }
    return zeros == 3;
}

// Every bare ColorF literal inside a brush or Clear call, by line.
static std::vector<int> Untinted(const std::string& s) {
    std::vector<int> bad;
    for (const char* call : {"CreateSolidColorBrush(", "->Clear("}) {
        for (size_t at = s.find(call); at != std::string::npos; at = s.find(call, at + 1)) {
            const size_t open = at + std::string(call).size() - 1;
            const std::string a = Args(s, open);
            for (size_t c = a.find("D2D1::ColorF("); c != std::string::npos;
                 c = a.find("D2D1::ColorF(", c + 1)) {
                const bool wrapped = c >= 11 && a.compare(c - 11, 11, "Theme::NVf(") == 0;
                if (!wrapped && !IsBlack(a, c)) bad.push_back(LineOf(s, at));
            }
        }
    }
    return bad;
}

int main() {
    printf("\n=== Phase 132.3b: Direct2D night coverage ===\n");
    const std::string root = Root();

    printf("\n1. The scan finds what it is for\n");
    {
        Check(Untinted("m_rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.9f, 0.2f), &b);").size() == 1,
              "a bare literal brush is caught");
        Check(Untinted("m_rt->CreateSolidColorBrush(Theme::NVf(D2D1::ColorF(1, 0, 0)), &b);").empty(),
              "one through NVf is not");
        Check(Untinted("m_rt->CreateSolidColorBrush(Theme::Color(Theme::TEXT, 0.5f), &b);").empty(),
              "nor one from a token");
        Check(Untinted("m_rt->CreateSolidColorBrush(x ? Theme::NVf(D2D1::ColorF(1,0,0))\n"
                       " : D2D1::ColorF(0, 0.5f, 1), &b);").size() == 1,
              "a bare literal in either arm of a ternary is caught");
        Check(Untinted("m_rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1.0f), &b);").empty() &&
                  Untinted("m_rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f));").empty(),
              "black is allowed: it is black in both modes");
        Check(Untinted("m_rt->Clear(D2D1::ColorF(0.016f, 0.031f, 0.063f));").size() == 1,
              "a coloured Clear is caught");
        Check(Untinted("m_rt->CreateSolidColorBrush(c, &b);").empty(),
              "a variable is its own business");
    }

    printf("\n2. The Direct2D windows\n");
    for (const char* f : {"src/gui/SkyMapWindow.h", "src/gui/StatusBar.h",
                          "src/gui/HistogramWindow.h", "src/gui/SharpnessWindow.h",
                          "src/gui/TemperatureWindow.h", "src/gui/StatusWindow.h",
                          "src/gui/ADCAlignWindow.h", "src/gui/ObjectPanel.h",
                          "src/gui/ShareView.h"}) {
        const std::string s = Read(root + "/" + f);
        if (s.empty()) { Check(false, f, "could not read"); continue; }
        const std::vector<int> bad = Untinted(s);
        std::string lines;
        for (int l : bad) lines += std::to_string(l) + " ";
        Check(bad.empty(), std::string(f) + " paints nothing untinted",
              bad.empty() ? "" : "lines " + lines);
    }

    printf("\n%s\n", g_fail ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return g_fail ? 1 : 0;
}
