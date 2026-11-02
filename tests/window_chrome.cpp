// ============================================================
//  The title bar's geometry, and the bundled fonts (phase 119)
//
//  What this cannot see is whether it looks right, or whether Windows
//  treats the bar as it should. Snap Layouts and dragging are someone
//  at a screen. A resize edge nobody can catch is not, and a font the
//  app thinks it has is not.
// ============================================================

#include "gui/BundledFonts.h"
#include "gui/ButtonIcons.h"
#include "gui/ChromeLayout.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace raduk::gui;
using namespace raduk::gui::chrome;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) ++g_fail;
}

static std::vector<char> Slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(f)), {});
}

int main(int, char** argv) {
    printf("Window chrome: geometry and fonts\n");

    printf("\n1. Scaling\n");
    {
        const Metrics a = ForDpi(96), b = ForDpi(192), c = ForDpi(144);
        Check(a.captionH == 32 && a.buttonW == 42, "the 96 dpi figures");
        Check(b.captionH == 64 && b.buttonW == 84, "200% is exactly twice");
        Check(c.captionH == 48, "150% rounds cleanly");
        Check(ForDpi(0).captionH == 32, "an unknown dpi falls back");
    }

    const Metrics m = ForDpi(96);
    const Flags all;

    printf("\n2. The buttons\n");
    {
        const Buttons b = LayoutButtons(800, m, all);
        Check(b.close.right == 800, "close reaches the corner");
        Check(b.max.right == b.close.left && b.min.right == b.max.left, "and they touch");
        Check(b.close.bottom == m.captionH, "full height, so a thrown mouse lands");
        Flags noMax = all; noMax.maximize = false;
        const Buttons c = LayoutButtons(800, m, noMax);
        Check(c.min.right == c.close.left, "with no maximise the gap closes");
        Check(HitTest(c.max.left + 1, 15, 800, m, noMax, false) != Zone::Maximize,
              "and nothing answers where it would be");
        Check(TitleRight(800, m, all) <= b.min.left, "a title stops before the buttons");
    }

    printf("\n3. Every pixel of the bar is something\n");
    {
        for (int dpi : {96, 120, 144, 192, 288}) {
            const Metrics d = ForDpi(dpi);
            const int w = 1000 * dpi / 96;
            bool gap = false;
            for (int y = 0; y < d.captionH; ++y)
                for (int x = 0; x < w; ++x)
                    if (HitTest(x, y, w, d, all, false) == Zone::Client) gap = true;
            char s[80];
            snprintf(s, sizeof(s), "%d dpi has no dead pixel", dpi);
            Check(!gap, s);
        }
    }

    printf("\n4. Edges\n");
    {
        Check(HitTest(400, 2, 800, m, all, false) == Zone::Top, "the top edge resizes");
        Check(HitTest(2, 2, 800, m, all, false) == Zone::TopLeft, "the corners do too");
        Check(HitTest(798, 2, 800, m, all, false) == Zone::TopRight, "both of them");
        Check(HitTest(400, 15, 800, m, all, false) == Zone::Caption, "below that it drags");
        Check(HitTest(400, m.captionH, 800, m, all, false) == Zone::Client, "the content starts under it");
        Check(HitTest(400, -1, 800, m, all, false) == Zone::Client, "and above it is not ours");
        Flags fixed = all; fixed.resizable = false;
        Check(HitTest(400, 2, 800, m, fixed, false) == Zone::Caption, "a fixed window has no edge");
    }

    printf("\n5. Maximised\n");
    {
        Check(HitTest(400, 0, 800, m, all, true) == Zone::Caption, "no resize edge");
        Check(HitTest(799, 0, 800, m, all, true) == Zone::Close,
              "the top right corner of a maximised window is close");
    }

    printf("\n6. Button icons\n");
    {
        Check(IconForLabel(L"ZOOM: FIT") == IconForLabel(L"ZOOM: 2X"), "a label that changes keeps its icon");
        Check(IconForLabel(L"SAVE") != 0, "a mapped label has one");
        Check(IconForLabel(L"DARK") == 0, "one with no honest icon has none");
        Check(IconForLabel(L"") == 0 && IconForLabel(nullptr) == 0, "and nothing is safe");
        Check(IconForLabel(L"GOTO") == IconForLabel(L"GO TO"), "both spellings of go to match");
        Check(IconForLabel(L"EDIT") != IconForLabel(L"SAVE"), "different jobs, different icons");
    }

    printf("\n7. The fonts\n");
    {
        std::string root = argv[0];
        const size_t cut = root.rfind("tests");
        root = root.substr(0, cut) + "assets\\fonts\\";
        std::vector<std::vector<char>> files;
        for (const char* n : {"BarlowCondensed-Regular.ttf", "BarlowCondensed-Bold.ttf",
                              "SpaceMono-Regular.ttf", "SpaceMono-Bold.ttf"})
            files.push_back(Slurp(root + n));
        std::vector<Fonts::Blob> blobs;
        for (const auto& f : files) blobs.push_back({f.data(), (DWORD)f.size()});
        Check(Fonts::Register(blobs) == 4, "all four load for GDI");

        IDWriteFontCollection* c = Fonts::CollectionFor(L"Barlow Condensed");
        Check(c != nullptr, "DirectWrite has a collection");
        Check(Fonts::CollectionFor(L"Segoe UI") == nullptr, "Segoe UI still goes to the system");
        Check(Fonts::CollectionFor(nullptr) == nullptr, "and no face is safe");
        Check(wcscmp(Fonts::FamilyFor(L"Segoe UI"), L"Segoe UI") == 0, "and keeps its name");

        for (const wchar_t* face : {L"Barlow Condensed", L"Space Mono"}) {
            UINT32 at = 0; BOOL found = FALSE;
            if (c) c->FindFamilyName(Fonts::FamilyFor(face), &at, &found);
            Check(found != FALSE, "DirectWrite finds the family");

            // Asked for by name, GDI must really answer with it.
            HDC dc = CreateCompatibleDC(nullptr);
            HFONT f = CreateFontW(-20, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, face);
            SelectObject(dc, f);
            wchar_t got[64] = {};
            GetTextFaceW(dc, 64, got);
            Check(wcscmp(got, face) == 0, "and GDI gets the real face");
            DeleteObject(f);
            DeleteDC(dc);
        }
    }

    printf("\n%s\n", g_fail ? "FAILED" : "All passed");
    return g_fail ? 1 : 0;
}
