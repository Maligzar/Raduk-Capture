// tests/object_panel_fit.cpp -- phase 103, fitting narrative text
//
// Jupiter's narrative ran off the bottom of the object panel. The
// panel already had an EstimateHeight member for exactly this, with no
// callers, and a comment ending "the text is clipped to the rect
// either way" -- which was false and was the reason the text escaped.
//
// These are the arithmetic that replaces the guess.

#include <cstdio>
#include <cmath>
#include <string>
#include "gui/ObjectPanel.h"

static int failures = 0;
static int checks   = 0;
static void ok(bool cond, const char* what) {
    ++checks;
    if (!cond) { ++failures; printf("  FAIL: %s\n", what); }
}

namespace OP = raduk::gui::objectpanel;

static void TestLines() {
    printf("Line estimation\n");

    // One short line stays one line.
    ok(OP::EstimatedLines(20, 800.0f, 16.0f) == 1.0f,
       "a short string is one line");

    // Twice the characters in the same column is about twice the lines.
    const float a = OP::EstimatedLines(600, 800.0f, 16.0f);
    const float b = OP::EstimatedLines(1200, 800.0f, 16.0f);
    ok(b >= a * 1.8f && b <= a * 2.2f,
       "twice the text is about twice the lines");

    // A narrower column needs more lines for the same text.
    ok(OP::EstimatedLines(600, 400.0f, 16.0f) >
       OP::EstimatedLines(600, 800.0f, 16.0f),
       "a narrower column needs more lines");

    // A larger font needs more lines for the same text and column.
    ok(OP::EstimatedLines(600, 800.0f, 24.0f) >
       OP::EstimatedLines(600, 800.0f, 12.0f),
       "a bigger font needs more lines");

    // Degenerate inputs must not divide by zero or run away.
    ok(OP::EstimatedLines(600, 0.0f, 16.0f) == 1.0f,
       "a zero-width column falls back to one line");
    ok(OP::EstimatedLines(600, 800.0f, 0.0f) == 1.0f,
       "a zero font size falls back to one line");
    ok(OP::EstimatedLines(0, 800.0f, 16.0f) == 1.0f,
       "empty text is still one line, never zero");

    // The eight-character floor: a column too narrow to hold a word
    // must not predict thousands of lines.
    const float silly = OP::EstimatedLines(10000, 1.0f, 40.0f);
    ok(silly <= 10000.0f / 8.0f + 1.0f,
       "a pathologically narrow column is bounded by the 8-char floor");
}

static void TestFit() {
    printf("Fitting to a box\n");
    const float want = 20.0f;

    // Text that already fits keeps the size it asked for. This is the
    // common case and must not shrink anything.
    ok(OP::FitBodyPx(80, 800.0f, 600.0f, want) == want,
       "text that fits is left at the wanted size");

    // Text that does not fit comes back smaller.
    const float tight = OP::FitBodyPx(4000, 400.0f, 200.0f, want);
    ok(tight < want, "text that overflows is shrunk");
    ok(tight >= OP::BODY_FLOOR_PX, "and never below the readable floor");

    // The result actually fits, which is the whole contract. Checked
    // at the returned size, not assumed. These need a case that lands
    // ABOVE the floor: they used to run on the 4000-character case
    // above behind an "if above the floor", which that case never is,
    // so neither assertion had ever executed.
    const float roomy = OP::FitBodyPx(1000, 400.0f, 300.0f, want);
    ok(roomy < want && roomy > OP::BODY_FLOOR_PX,
       "this case shrinks but stops short of the floor");
    ok(OP::EstimatedHeight(1000, 400.0f, roomy) <= 300.0f,
       "the size it returns does fit the box");

    // And it is the LARGEST such size: one pixel more must not fit.
    ok(OP::EstimatedHeight(1000, 400.0f, roomy + 1.0f) > 300.0f,
       "it returns the largest size that fits, not merely one that does");

    // Impossible boxes floor rather than vanishing. Readable and
    // clipped beats complete and unreadable, and clipping is real now.
    ok(OP::FitBodyPx(200000, 300.0f, 40.0f, want) == OP::BODY_FLOOR_PX,
       "an impossible amount of text floors at the minimum");

    // Monotonic in the box: a taller box never yields a smaller font.
    float prev = 0.0f;
    for (float h = 40.0f; h <= 1200.0f; h += 20.0f) {
        const float px = OP::FitBodyPx(3000, 500.0f, h, want);
        ok(px >= prev - 0.001f, "a taller box never gives a smaller font");
        prev = px;
    }

    // Monotonic in the text: more text never yields a larger font.
    prev = 1e9f;
    for (size_t n = 100; n <= 8000; n += 100) {
        const float px = OP::FitBodyPx(n, 500.0f, 400.0f, want);
        ok(px <= prev + 0.001f, "more text never gives a larger font");
        prev = px;
    }

    // Degenerate and boundary inputs.
    ok(OP::FitBodyPx(0, 800.0f, 10.0f, want) == want,
       "empty text is never shrunk, however small the box");
    ok(OP::FitBodyPx(500, 0.0f, 100.0f, want) == want,
       "a zero-width column is not something to solve by shrinking");
    ok(OP::FitBodyPx(500, 800.0f, 0.0f, want) == want,
       "nor is a zero-height box");
    ok(OP::FitBodyPx(5000, 400.0f, 100.0f, OP::BODY_FLOOR_PX) ==
       OP::BODY_FLOOR_PX,
       "a wanted size already at the floor stays there");
    ok(OP::FitBodyPx(5000, 400.0f, 100.0f, 5.0f) == OP::BODY_FLOOR_PX,
       "a wanted size below the floor is raised to it");
    ok(OP::FitBodyPx(10, 800.0f, 600.0f, 5.0f) == OP::BODY_FLOOR_PX,
       "even when the text would have fitted at that size");
}

static void TestJupiter() {
    printf("The case that started this\n");

    // Jupiter's panel, as the owner saw it: a 1920x1080 window, the
    // narrative in the lower portion, two columns. The real text is in
    // the knowledge base; these are representative lengths for a
    // summary plus a mythology aside, which is the longest combination
    // the panel produces.
    const float panelH   = 1080.0f;
    const float bodyPx   = (std::max)(13.0f,
                              (std::min)(44.0f, panelH / 26.0f) * 0.80f);
    const float colW     = (1920.0f - 2.0f * 87.0f);   // pad = w/22
    const float col2     = (colW - bodyPx * 1.4f) * 0.5f;
    const float boxH     = 320.0f;                     // what is left below

    const size_t summaryChars = 900;
    const size_t mythChars    = 760;

    printf("  bodyPx %.1f, column %.0f px, box %.0f px\n", bodyPx, col2, boxH);
    const float needed = OP::EstimatedHeight(summaryChars, col2, bodyPx);
    printf("  at full size the summary wants %.0f px in a %.0f px box\n",
           needed, boxH);

    // The premise: at the full body size this genuinely does not fit,
    // which is why it escaped. If this ever stops being true the fix
    // is no longer being exercised by this case.
    ok(needed > boxH, "at full size Jupiter's summary overflows its box");

    const float fit = (std::min)(
        OP::FitBodyPx(summaryChars, col2, boxH, bodyPx),
        OP::FitBodyPx(mythChars,    col2, boxH, bodyPx));
    printf("  fitted body size: %.1f px\n", fit);

    ok(fit < bodyPx, "so it is shrunk");
    ok(fit >= OP::BODY_FLOOR_PX, "but stays readable");
    ok(OP::EstimatedHeight(summaryChars, col2, boxH > 0 ? fit : fit) <= boxH ||
       fit == OP::BODY_FLOOR_PX,
       "and the summary fits at the fitted size");
    ok(OP::EstimatedHeight(mythChars, col2, fit) <= boxH ||
       fit == OP::BODY_FLOOR_PX,
       "as does the mythology, since both columns share one size");

    // Both columns share a size, so the shared size must be the
    // smaller of the two. A reader seeing two body sizes side by side
    // reads it as a bug even when both fit.
    ok(fit <= OP::FitBodyPx(summaryChars, col2, boxH, bodyPx),
       "the shared size is no larger than the summary's own fit");
    ok(fit <= OP::FitBodyPx(mythChars, col2, boxH, bodyPx),
       "nor larger than the mythology's");
}

int main() {
    printf("=== phase 103: fitting the object panel's narrative ===\n\n");
    TestLines();
    TestFit();
    TestJupiter();
    printf("\n%d checks, %d failures\n", checks, failures);
    if (failures == 0) printf("PASS\n");
    return failures ? 1 : 0;
}
