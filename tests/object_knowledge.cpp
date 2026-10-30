// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// ============================================================
//  The object knowledge database — reachable, and resolving
//
//  Phase 92a. Two things are checked here and the first matters more
//  than it looks: that the database can be OPENED at all.
//
//  ObjectKnowledgeDB loads SQLite with LoadLibrary and returns
//  gracefully when the DLL is absent. That is correct behaviour and it
//  is also why nobody noticed that sqlite3.dll was in neither build
//  output: the 2.4 MB database built in March had never once been read
//  by the running app. The object info panel fell back to "basic
//  ephemeris only" every time it opened, and the assistant's knowledge
//  lookups returned empty strings. Silently, in both cases.
//
//  A graceful failure that nothing ever notices is indistinguishable
//  from a feature that does not exist. This test is the noticing.
// ============================================================

#include "core/ObjectKnowledgeDB.h"
#include "core/Paths.h"

#include <cstdio>
#include <string>

using namespace raduk::core;

static int g_fail = 0;

static void Check(bool ok, const std::string& what, const std::string& detail) {
    printf("  [%s] %-56s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

int main() {
    // Unbuffered: this is the first code in the project ever to call
    // into SQLite for real, and a crash would otherwise take the
    // output that says where it got to with it.
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("\n=== Object knowledge database ===\n");

    ObjectKnowledgeDB db;
    // Ask where the data actually lives rather than assuming. P5.4
    // made the root configurable and moved fifteen call sites onto
    // Paths; this test kept its own copy of the old literal, so it
    // would have gone looking in the wrong place the first time
    // anyone set DataRoot. Found by running the suite.
    const std::string dbPath = raduk::core::Paths::Data() + "\\object_knowledge.db";
    const bool opened = db.Open(dbPath);

    printf("\n1. Is it reachable at all\n");
    Check(opened && db.IsAvailable(),
          "the database opens, which means sqlite3.dll is deployed",
          opened ? "" : ("not at " + dbPath + " - sqlite3.dll missing, or DataRoot points elsewhere").c_str());

    if (!db.IsAvailable()) {
        printf("\n  Nothing further can be checked without it.\n\n");
        printf("=== 1 CHECK FAILED ===\n\n");
        return 1;
    }

    // ── 2. The planets ───────────────────────────────────────
    //
    // This is a planetary rig. Whatever else the catalogue does, the
    // five things it will be pointed at on a given evening have to
    // work.
    printf("\n2. The targets this rig actually uses\n");
    for (const char* n : { "Jupiter", "Saturn", "Mars", "Venus", "Moon" }) {
        const auto r = db.Lookup(n);
        Check(r.found, std::string(n) + " is found", r.name);
        if (r.found)
            Check(r.HasNarrative(),
                  std::string("  and has something to say about ") + n,
                  std::to_string(r.summary.size()) + " chars");
    }

    // ── 3. The name mismatch ─────────────────────────────────
    //
    // The sky map calls it "M13 Hercules Cluster". The database calls
    // it "M013". Matching on name exactly -- which is what the old
    // QueryObject did -- misses it.
    printf("\n3. Names that do not line up\n");
    {
        const auto r = db.Lookup("M13");
        Check(r.found, "M13 resolves, though it is stored as M013", r.name);
        Check(r.name == "M013",
              "  and resolves to the padded form the database uses", r.name);
        Check(r.commonName.find("Hercules") != std::string::npos,
              "  with the common name attached", r.commonName);
    }
    {
        // The app's own display string, tail and all.
        const auto r = db.Lookup("M13 Hercules Cluster");
        Check(r.found && r.name == "M013",
              "the app's full display string resolves too",
              r.found ? r.name : "not found");
    }
    {
        const auto r = db.Lookup("NGC6205");
        Check(r.found && r.name == "M013",
              "and so does the NGC number, via catalog_ids",
              r.found ? r.name : "not found");
    }

    // ── 4. Absent is not zero ────────────────────────────────
    //
    // An empty cell and a real zero have to stay distinguishable, or
    // the panel prints "magnitude 0" for every object nobody ever
    // measured. Vega turns out to be the example of ABSENCE here, not
    // of zero: this catalogue has no magnitude for it at all.
    printf("\n4. Missing facts stay missing\n");
    {
        const auto r = db.Lookup("Vega");
        Check(r.found, "Vega is found", r.name);
        Check(!r.hasMagnitude,
              "its magnitude is absent, and reads as absent",
              "the cell is NULL in this catalogue");
        Check(r.magnitude == 0.0 && !r.hasMagnitude,
              "  so a 0.0 value and a missing value are told apart",
              "value 0.0, present=false");
    }
    {
        // A real magnitude, to prove the other half of the same
        // distinction.
        const auto r = db.Lookup("IC0002");
        if (r.found) {
            Check(r.hasMagnitude, "a recorded magnitude reads as present",
                  r.magnitudeStr);
            Check(r.magnitude > 10.0, "  and parses to its real value",
                  std::to_string(r.magnitude));
        }
    }
    {
        const auto r = db.Lookup("Qqzzx Not A Real Object");
        Check(!r.found, "an object that is not there reports not-found", "");
        Check(r.summary.empty() && !r.hasMagnitude,
              "  and carries no half-filled fields", "");
    }

    // ── 5. What each kind of object actually has ─────────────
    //
    // The panel is layered because coverage is uneven, and it is
    // uneven in OPPOSITE directions depending on what you point at.
    // This was measured rather than assumed, and it corrected the
    // plan: deep-sky rows are rich in numbers and poor in prose,
    // named stars are the other way round, and planets have both.
    //
    // So neither layer is "the fallback". They cover for each other.
    printf("\n5. Coverage, which runs opposite ways\n");
    {
        const auto deep = db.Lookup("IC0002");
        if (deep.found) {
            Check(deep.hasMagnitude && !deep.type.empty(),
                  "a deep-sky row carries numbers", deep.type);
            Check(!deep.HasNarrative(),
                  "  and usually no prose - the derived layer carries it", "");
        }
        const auto star = db.Lookup("Vega");
        Check(star.HasNarrative(),
              "a named star carries prose", std::to_string(star.summary.size()) + " chars");
        Check(!star.hasDistanceLy,
              "  and almost never a distance - prose carries it instead",
              "1 of 1001 star rows has one");
        const auto planet = db.Lookup("Jupiter");
        Check(planet.HasNarrative() && !planet.type.empty(),
              "a planet carries both, which is this rig's main case",
              planet.type);
    }

    printf("\n");
    if (g_fail == 0) printf("=== the knowledge base is reachable and resolving ===\n\n");
    else             printf("=== %d CHECK(S) FAILED ===\n\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
