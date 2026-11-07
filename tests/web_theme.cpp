// ============================================================
//  Phase 132.2: the app's look, as CSS
//
//  The pages the app writes had their own hand-typed palettes, and
//  they drifted. What matters here is that they cannot again:
//
//    1. Every CSS role is its Theme.h token, byte for byte.
//    2. Every night luminance is Theme's ladder, and night is red only,
//       as the window chrome has been since 132.1.
//    3. A page is self-contained: embedded fonts, no network.
//    4. The contrast floor a candidate palette is held to is computed
//       right, or the specimen page would pass a palette it should not.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "gui/WebPalette.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace raduk;
using namespace raduk::core::web;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-64s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static int Count(const std::string& s, const std::string& sub) {
    int n = 0;
    for (size_t p = s.find(sub); p != std::string::npos; p = s.find(sub, p + 1)) ++n;
    return n;
}

int main() {
    printf("\n=== Phase 132.2: web theme ===\n");

    printf("\n1. Colour arithmetic\n");
    {
        Check(Hex(0xFF00E5FF) == "#00e5ff", "a token becomes its CSS hex", Hex(0xFF00E5FF));
        Check(Hex(0x00123456) == "#123456", "alpha is not part of it");
        Check(NightArgb(200) == 0xFFC80000u, "night is (lum, 0, 0)");
        Check(NightArgb(-4) == 0xFF000000u && NightArgb(999) == 0xFFFF0000u, "and clamps");
        Check(std::fabs(Contrast(0xFFFFFFFF, 0xFF000000) - 21.0) < 1e-9, "white on black is 21:1");
        Check(std::fabs(Contrast(0xFF777777, 0xFF777777) - 1.0) < 1e-9, "a colour on itself is 1:1");
        Check(Contrast(0xFF000000, 0xFFFFFFFF) == Contrast(0xFFFFFFFF, 0xFF000000),
              "order does not matter");
        // A known pair: #767676 on white is the classic 4.54:1 AA edge.
        const double edge = Contrast(0xFF767676, 0xFFFFFFFF);
        Check(edge > 4.5 && edge < 4.6, "#767676 on white sits just over 4.5", std::to_string(edge));
    }

    printf("\n2. The roles are Theme.h's tokens\n");
    {
        const Palette p = gui::CurrentWebPalette();
        Check(p.day[Text] == gui::Theme::TEXT && p.day[Deep] == gui::Theme::DEEP &&
                  p.day[Accent] == gui::Theme::ACCENT && p.day[Fault] == gui::Theme::RED,
              "text, deep, accent and fault are the tokens");
        bool rgb = true;
        const COLORREF refs[kRoles] = {
            gui::Theme::Rgb::BG_VOID, gui::Theme::Rgb::DEEP, gui::Theme::Rgb::PANEL,
            gui::Theme::Rgb::BORDER, gui::Theme::Rgb::TEXT, gui::Theme::Rgb::TEXT_DIM,
            gui::Theme::Rgb::ACCENT, gui::Theme::Rgb::AMBER, gui::Theme::Rgb::RED,
            gui::Theme::Rgb::GREEN };
        for (int r = 0; r < kRoles; ++r) {
            const uint32_t a = p.day[r];
            if (GetRValue(refs[r]) != ((a >> 16) & 0xFF) || GetGValue(refs[r]) != ((a >> 8) & 0xFF) ||
                GetBValue(refs[r]) != (a & 0xFF))
                rgb = false;
        }
        Check(rgb, "and agree with the GDI spelling the chrome paints with, all ten");

        bool ladder = true;
        std::string bad;
        for (int r = 0; r < kRoles; ++r) {
            const uint32_t a = p.day[r];
            const int lum = gui::Theme::NightLuminance((a >> 16) & 0xFF, (a >> 8) & 0xFF, a & 0xFF);
            if (lum != kNightLum[r]) { ladder = false; bad += std::string(RoleVar(r)) + " "; }
        }
        Check(ladder, "every role's night luminance is Theme's ladder", bad);

        gui::Theme::SetNightVision(true);
        bool same = true;
        for (int r = 0; r < kRoles; ++r) {
            const uint32_t a = p.day[r];
            const COLORREF n = gui::Theme::NV(RGB((a >> 16) & 0xFF, (a >> 8) & 0xFF, a & 0xFF));
            const uint32_t w = NightArgb(kNightLum[r]);
            if (GetRValue(n) != ((w >> 16) & 0xFF) || GetGValue(n) != ((w >> 8) & 0xFF) ||
                GetBValue(n) != (w & 0xFF))
                same = false;
        }
        gui::Theme::SetNightVision(false);
        Check(same, "a page's night colours are exactly what Theme::NV paints");
    }

    printf("\n3. The CSS\n");
    {
        const Palette p = gui::CurrentWebPalette();
        const std::string css = PaletteCss(p);
        bool every = true;
        for (int r = 0; r < kRoles; ++r)
            if (Count(css, std::string(RoleVar(r)) + ":") != 2) every = false;
        Check(every, "every role is declared once for day and once for night");
        Check(css.find(":root{--void:#04080f;") == 0, "day is on :root, first");
        Check(css.find(":root[data-mode=\"night\"]{") != std::string::npos,
              "night follows one attribute on the root element");
        const std::string night = css.substr(css.find("[data-mode"));
        Check(night.find("--text:#c80000") != std::string::npos &&
                  night.find("--fault:#f00000") != std::string::npos,
              "night text is 200 red, fault 240 red");
        bool redOnly = true;
        for (size_t at = night.find('#'); at != std::string::npos; at = night.find('#', at + 1))
            if (night.substr(at + 3, 4) != "0000") redOnly = false;
        Check(redOnly, "every night colour is red only");

        const std::string base = BaseCss();
        Check(base.find("tabular-nums") != std::string::npos, "readouts use tabular figures");
        Check(base.find("prefers-reduced-motion") != std::string::npos, "reduced motion is honoured");
        Check(base.find(".sig.fault::before") != std::string::npos &&
                  base.find(".sig.warn::before") != std::string::npos,
              "fault and warning differ in SHAPE, not only colour");
        Check((css + base).find("http") == std::string::npos &&
                  (css + base).find("@import") == std::string::npos,
              "no network: no URL and no import");
    }

    printf("\n4. Fonts inline\n");
    {
        Check(Base64((const uint8_t*)"Man", 3) == "TWFu", "base64 of three bytes");
        Check(Base64((const uint8_t*)"Ma", 2) == "TWE=", "two bytes pad once");
        Check(Base64((const uint8_t*)"M", 1) == "TQ==", "one byte pads twice");
        Check(Base64(nullptr, 0).empty(), "nothing is nothing");
        Check(FontFaceCss("Space Mono", 400, {}).empty(),
              "a font that did not load declares no face");
        const std::string f = FontFaceCss("Space Mono", 700, {'a', 'b', 'c'});
        Check(f.find("font-family:'Space Mono'") != std::string::npos &&
                  f.find("font-weight:700") != std::string::npos &&
                  f.find("base64,YWJj") != std::string::npos,
              "a face carries its family, weight and bytes");
    }

    printf("\n5. Contrast floors\n");
    {
        const Palette today = gui::CurrentWebPalette();
        Check(PassesContrast(today), "today's palette clears them");
        Palette dim = today;
        dim.day[Dim] = dim.day[Deep] + 0x00101010;   // barely off the background
        bool caught = false;
        for (const ContrastCheck& c : CheckContrast(dim))
            if (c.role == Dim && !c.ok) caught = true;
        Check(caught && !PassesContrast(dim), "a label nearly the colour of its panel fails");
        Check(CheckContrast(today).size() == 6, "six foreground roles are checked");
    }

    printf("\n%s\n", g_fail ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return g_fail ? 1 : 0;
}
