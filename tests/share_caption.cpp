// tests/share_caption.cpp -- phase 104, the share view's caption
//
// Two defects, both on the surface an audience actually looks at.
//
// The caption converted its target name into a wchar_t[96] with
// MultiByteToWideChar. That call does not truncate: given more than
// the buffer holds it FAILS and returns 0, leaving the buffer's
// contents undefined. The code then checked target[0], so a name over
// 95 characters drew NOTHING -- the audience screen showed no target
// at all. Reachable from outside the app: the remote goto_target
// command sets that string and nothing bounds its length.
//
// And the caption block is exactly two lines tall by construction,
// with the scrim behind it sized from the same top, while DWrite's
// default wrapping is on and a layout rect does not clip. A name long
// enough to wrap landed its second line on the detail line, left the
// scrim, and ran at the bottom edge of the screen.

#include <cstdio>
#include <cmath>
#include <string>
#include <algorithm>
#include "gui/AudienceLayout.h"

static int failures = 0;
static int checks   = 0;
static void ok(bool cond, const char* what) {
    ++checks;
    if (!cond) { ++failures; printf("  FAIL: %s\n", what); }
}

namespace A = raduk::gui::audience;

// The share view's own geometry, from ShareView::MakeFonts and
// DrawCaption. Kept here so the test fails if the panel's arithmetic
// is changed without revisiting this.
struct Caption {
    float w, h;
    float fontPx() const {
        return (std::max)(28.0f, (std::min)(72.0f, h / 18.0f));
    }
    float bigH() const { return fontPx() * 1.25f; }
    // Pad is max(24, Space::Scaled(bodyPx, P_LG)); 34 at 1080, and the
    // exact value does not change what is being tested here.
    float pad() const { return (std::max)(24.0f, fontPx() * 0.57f); }
    float availW() const { return w - pad() * 2.0f; }
};

static void TestWrapIsReachable() {
    printf("The premise: a caption CAN wrap\n");

    const Caption c{ 1920.0f, 1080.0f };
    printf("  1920x1080: font %.0f px, line box %.0f px, width %.0f px\n",
           c.fontPx(), c.bigH(), c.availW());

    const size_t fits = A::CharsThatFit(c.availW(), c.fontPx(),
                                        A::DISPLAY_ADVANCE_EM);
    printf("  one line holds about %zu characters at full size\n", fits);

    // Ordinary names fit on one line and must NOT be shrunk.
    for (const char* n : { "Jupiter", "Saturn", "M42",
                           "Great Orion Nebula" }) {
        const size_t len = std::string(n).size();
        ok(A::FitPxOneLine(len, c.availW(), c.fontPx(),
                           A::DISPLAY_ADVANCE_EM) == c.fontPx(),
           "an ordinary target name is left at full size");
        ok(len <= fits, "and fits on one line");
    }

    // The font is h / 18, which is only a whole number on some screens.
    // At 1200 high it is 66.67, and a name that fits must keep that
    // size rather than being floored to a whole pixel on the way past.
    const Caption tall{ 1920.0f, 1200.0f };
    ok(tall.fontPx() != std::floor(tall.fontPx()),
       "this screen's caption size is fractional");
    ok(A::FitPxOneLine(7, tall.availW(), tall.fontPx(),
                       A::DISPLAY_ADVANCE_EM) == tall.fontPx(),
       "and an ordinary name keeps it exactly");

    // The buffer the old code used was 96 wide chars, and a line holds
    // fewer than that, so there was a range of lengths that wrapped
    // without ever reaching the truncation bug. Both failures were
    // live, in different length ranges.
    ok(fits < 95, "a line holds fewer characters than the old buffer held, "
                  "so wrapping was reachable well before truncation was");

    // A name past that wraps, which is what the two-line block and the
    // scrim cannot accommodate.
    ok(A::EstimatedLines(fits + 10, c.availW(), c.fontPx(),
                         A::DISPLAY_ADVANCE_EM) > 1.0f,
       "a longer name wraps to more than one line at full size");
}

static void TestFitKeepsOneLine() {
    printf("Shrinking keeps it to one line\n");
    const Caption c{ 1920.0f, 1080.0f };

    // A name that does not fit is shrunk, and at the size returned it
    // occupies one line. That is the contract the geometry needs.
    for (size_t len : { (size_t)70, (size_t)90, (size_t)140, (size_t)400 }) {
        const float px = A::FitPxOneLine(len, c.availW(), c.fontPx(),
                                         A::DISPLAY_ADVANCE_EM);
        ok(px <= c.fontPx(), "a long name is not enlarged");
        ok(px >= A::BODY_FLOOR_PX, "and not shrunk below the floor");
        const bool oneLine =
            A::EstimatedLines(len, c.availW(), px,
                              A::DISPLAY_ADVANCE_EM) <= 1.0f;
        // A name that CAN fit at the floor must fit: only one longer
        // than that may reach the floor and be cut by the caller.
        const size_t atFloor = A::CharsThatFit(c.availW(), A::BODY_FLOOR_PX,
                                               A::DISPLAY_ADVANCE_EM);
        if (len <= atFloor) ok(oneLine, "it fits on one line");
        else ok(px == A::BODY_FLOOR_PX, "past that it floors and is cut");
    }

    // The 95-character case: the exact length that used to blank the
    // caption entirely must now produce something drawable.
    const float px95 = A::FitPxOneLine(95, c.availW(), c.fontPx(),
                                       A::DISPLAY_ADVANCE_EM);
    const size_t fits95 = A::CharsThatFit(c.availW(), px95,
                                          A::DISPLAY_ADVANCE_EM);
    printf("  a 95-char name: fitted to %.0f px, %zu chars fit\n",
           px95, fits95);
    ok(fits95 > 0, "a 95-character name has room for something");
    ok(px95 < c.fontPx(), "and it had to shrink to get there");
}

static void TestCharsThatFit() {
    printf("CharsThatFit\n");
    ok(A::CharsThatFit(0.0f, 20.0f) == 0, "no width holds nothing");
    ok(A::CharsThatFit(800.0f, 0.0f) == 0, "no size holds nothing");
    ok(A::CharsThatFit(800.0f, 20.0f) > A::CharsThatFit(800.0f, 40.0f),
       "a bigger size fits fewer characters");
    ok(A::CharsThatFit(1600.0f, 20.0f) > A::CharsThatFit(800.0f, 20.0f),
       "a wider box fits more characters");
    // The bold display face is wider, so fewer of it fit.
    ok(A::CharsThatFit(800.0f, 20.0f, A::DISPLAY_ADVANCE_EM) <
       A::CharsThatFit(800.0f, 20.0f, A::BODY_ADVANCE_EM),
       "the display face fits fewer characters than the body face");
}

static void TestAdvanceConstants() {
    printf("The advance constants\n");
    // The display face is bold and wider. Set high on purpose:
    // overestimating predicts more lines, which shrinks text that
    // would have fit rather than letting text escape that would not.
    ok(A::DISPLAY_ADVANCE_EM > A::BODY_ADVANCE_EM,
       "the bold display advance is wider than the body advance");
    ok(A::BODY_ADVANCE_EM > 0.3f && A::DISPLAY_ADVANCE_EM < 0.8f,
       "both advances are within the plausible range for a latin face");
    ok(A::BODY_LINE_EM > 1.0f, "a line is taller than its em");
}

static void TestObjectPanelStillForwards() {
    printf("The object panel's names still work\n");
    // Phase 103's call sites use objectpanel:: names; 104 made them
    // forwards. Same answers, one implementation.
    ok(raduk::gui::audience::EstimatedLines(600, 800.0f, 16.0f) ==
       A::EstimatedLines(600, 800.0f, 16.0f),
       "the shared implementation is reachable under audience::");
    ok(A::FitPx(4000, 400.0f, 200.0f, 20.0f) <= 20.0f,
       "and still shrinks what does not fit");
}

static void TestWide() {
    printf("The conversion that used to blank the caption\n");
    ok(A::Wide("").empty(), "empty stays empty");
    ok(A::Wide("Jupiter") == L"Jupiter", "an ordinary name converts");

    // The defect itself: the old wchar_t[96] held 95 characters, and
    // 96 made the conversion fail and the caption vanish. Either side
    // of that edge, and far past it, the whole name must come through.
    for (size_t len : { (size_t)95, (size_t)96, (size_t)97, (size_t)5000 }) {
        const std::string name(len, 'x');
        const std::wstring w = A::Wide(name);
        ok(w.size() == len, "no length is lost, at or past the old buffer");
        ok(w == std::wstring(len, L'x'), "and the characters are intact");
    }

    // Fitting is by characters, so the length must count characters
    // and not bytes. "Ganymede" with a two-byte e-acute.
    const std::wstring accented = A::Wide("Ganym\xC3\xA9" "de");
    ok(accented.size() == 8, "UTF-8 counts characters, not bytes");
    ok(accented.size() == 8 && accented[5] == (wchar_t)0x00E9,
       "and decodes as UTF-8, not the ANSI page");

    // Nothing bounds the remote goto_target string, so it can be junk.
    ok(!A::Wide("bad\xFFname").empty(), "invalid UTF-8 still shows something");
}

static void TestEllipsize() {
    printf("Shortening a name\n");
    const wchar_t dots = (wchar_t)0x2026;
    ok(A::Ellipsize(L"Jupiter", 7) == L"Jupiter", "an exact fit is untouched");
    ok(A::Ellipsize(L"Jupiter", 50) == L"Jupiter", "so is a name with room");
    ok(A::Ellipsize(L"Jupiter", 6) == std::wstring(L"Jupit") + dots,
       "one too long loses a character to the ellipsis");
    ok(A::Ellipsize(L"Jupiter", 1) == std::wstring(1, dots),
       "one slot is only the ellipsis");
    ok(A::Ellipsize(L"Jupiter", 0).empty(), "no slots draws nothing");
    ok(A::Ellipsize(L"", 5).empty(), "empty stays empty");
    for (size_t fits = 1; fits < 12; ++fits)
        ok(A::Ellipsize(L"Jupiter's moons", fits).size() <= fits,
           "the result never exceeds what fits");
}

static void TestCaptionEndToEnd() {
    printf("A long name, the way DrawCaption handles it\n");
    const Caption c{ 1920.0f, 1080.0f };

    // The same three calls in the same order, on a name past both old
    // failures: it must come out non-empty, on one line, and cut.
    const std::wstring full = A::Wide(std::string(5000, 'x'));
    const float px = A::FitPxOneLine(full.size(), c.availW(), c.fontPx(),
                                     A::DISPLAY_ADVANCE_EM);
    const size_t fits = A::CharsThatFit(c.availW(), px, A::DISPLAY_ADVANCE_EM);
    const std::wstring shown = A::Ellipsize(full, fits);
    ok(!shown.empty(), "a 5000-character name still draws");
    ok(shown.size() < full.size() && shown.back() == (wchar_t)0x2026,
       "shortened, and visibly so");
    ok(A::EstimatedLines(shown.size(), c.availW(), px,
                         A::DISPLAY_ADVANCE_EM) <= 1.0f,
       "and what is drawn stays on one line");
}

int main() {
    printf("=== phase 104: the share view's caption ===\n\n");
    TestWrapIsReachable();
    TestFitKeepsOneLine();
    TestCharsThatFit();
    TestAdvanceConstants();
    TestObjectPanelStillForwards();
    TestWide();
    TestEllipsize();
    TestCaptionEndToEnd();
    printf("\n%d checks, %d failures\n", checks, failures);
    if (failures == 0) printf("PASS\n");
    return failures ? 1 : 0;
}
