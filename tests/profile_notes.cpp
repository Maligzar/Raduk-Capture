// ============================================================
//  What a SAVE is allowed to do to a profile's note
//
//  saturn.ini lost
//
//    "Longer exposure than Jupiter. Rings add contrast challenge."
//
//  to a SAVE pressed during a Jupiter session: the NOTES box had never
//  held Saturn's note, so it was empty, and SAVE wrote the empty
//  string over the note on disk. It was typed back in by hand.
//
//  That loss is quiet in exactly the way this project's tests exist
//  for -- nothing failed, nothing was logged, and the note was gone
//  only the next time anyone went looking for it. So the rule that
//  replaced it is pinned here rather than left to be re-reasoned from
//  the UI code.
// ============================================================

#include "profiles/TargetProfile.h"

#include <cstdio>
#include <string>

using namespace raduk::profiles;

static int g_fail = 0;

static void Check(bool ok, const std::string& what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) g_fail++;
}

static const std::string kSaturnNote =
    "Longer exposure than Jupiter. Rings add contrast challenge.";

int main() {
    printf("\n=== Profile notes: what a SAVE may overwrite ===\n\n");

    // ── The loss that happened ────────────────────────────────
    //
    // A night on Jupiter, then SAVE with Saturn picked in the combo.
    // The box holds Jupiter's note or nothing at all; either way it
    // has nothing to say about Saturn.
    Check(!NotesBoxMayReplaceStored("", "Jupiter", "Saturn", kSaturnNote),
          "empty box owned by Jupiter cannot erase Saturn's note");

    // Same SAVE in an app run where no profile was ever loaded, so the
    // box owns nothing. This is the launch state: NOTES starts
    // collapsed, so the box does not even exist yet.
    Check(!NotesBoxMayReplaceStored("", "", "Saturn", kSaturnNote),
          "empty box owning no profile cannot erase Saturn's note");

    // A rename through the EDIT dialog leaves the box naming a profile
    // that is no longer there. Fail safe: keep the note.
    Check(!NotesBoxMayReplaceStored("", "Saturn", "Saturn (rings)", kSaturnNote),
          "stale owner after a rename keeps the stored note");

    // ── What must still work ──────────────────────────────────
    //
    // A deliberate clear: Saturn loaded, its note selected and
    // deleted, SAVE. The box owns Saturn, so an empty box is an
    // instruction, not an accident.
    Check(NotesBoxMayReplaceStored("", "Saturn", "Saturn", kSaturnNote),
          "emptying the loaded profile's own note clears it");

    // A note typed into the box is recorded wherever SAVE points. The
    // user wrote it on purpose and nothing else holds it.
    Check(NotesBoxMayReplaceStored("Seeing 7/10, cirrus after 01:00",
                                   "Jupiter", "Saturn", kSaturnNote),
          "a typed note is saved even to another profile");
    Check(NotesBoxMayReplaceStored("First light with the new ADC", "", "Mars", ""),
          "a typed note is saved when the box owns no profile");

    // Nothing on disk to lose -- the write is a no-op either way, and
    // treating it as allowed keeps the first note a profile ever gets
    // from needing a load first.
    Check(NotesBoxMayReplaceStored("", "", "Mars", ""),
          "an empty box over an empty note is allowed");
    Check(NotesBoxMayReplaceStored("", "Jupiter", "Mars", ""),
          "a foreign empty box over an empty note is allowed");

    // ── The default profiles all ship with notes ──────────────
    //
    // Which is why the unguarded write had something to destroy on
    // every one of them.
    const TargetProfile defs[] = {
        DefaultJupiter(), DefaultSaturn(), DefaultMars(), DefaultVenus(),
        DefaultMoon(), DefaultDSO(), DefaultStar(), DefaultPlanetFocus(),
    };
    for (const TargetProfile& p : defs) {
        Check(!p.notes.empty() &&
              !NotesBoxMayReplaceStored("", "", p.name, p.notes),
              "shipped note on " + p.name + " survives a SAVE from an empty box");
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED"
                             : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
