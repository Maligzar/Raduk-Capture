#!/usr/bin/env python3
"""
Repair the shipped object_knowledge.db in place, from local data only.

Phase 92. Two defects were found when the database was finally read by
the running app for the first time:

  1. mythology and discovery_notes are EMPTY for every one of the 679
     narratives, although build_knowledge_db.py authors real text for
     both. upsert_narrative used COALESCE(excluded.x, x), which only
     skips NULL -- and the parameters default to the empty string. So
     Phase 3's Wikipedia pass, which supplies a summary and nothing
     else, overwrote Phase 1's mythology and discovery with ''.

  2. The Moon's distance_ly read 1.07e-08, which is 0.34 light-seconds
     or about 101,000 km. It is 384,400 km: 1.282 light-seconds, or
     4.063e-08 light-years.

Both are fixed in build_knowledge_db.py, but rebuilding from scratch
needs Wikipedia and SIMBAD and takes a long time. This repairs what is
already on disk using nothing but the builder's own hardcoded tables,
so it runs offline in under a second and is safe to re-run.

    python tools/repair_knowledge_db.py [--dry-run]
"""

import argparse
import importlib.util
import os
import sqlite3
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DB   = os.path.join(ROOT, "data", "object_knowledge.db")
BUILDER = os.path.join(HERE, "build_knowledge_db.py")


def load_builder_tables():
    """Import the builder for its data tables without running it."""
    spec = importlib.util.spec_from_file_location("bk", BUILDER)
    mod = importlib.util.module_from_spec(spec)
    # The builder guards its work behind main(), so importing is safe.
    spec.loader.exec_module(mod)
    return mod


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true",
                    help="report what would change and write nothing")
    args = ap.parse_args()

    if not os.path.exists(DB):
        print(f"not found: {DB}")
        return 1

    bk = load_builder_tables()
    solar = getattr(bk, "SOLAR_SYSTEM", None)
    if not solar:
        print("could not read SOLAR_SYSTEM from the builder")
        return 1

    conn = sqlite3.connect(DB)
    before_myth = conn.execute(
        "SELECT count(*) FROM narratives "
        "WHERE length(coalesce(mythology,'')) > 20").fetchone()[0]
    before_disc = conn.execute(
        "SELECT count(*) FROM narratives "
        "WHERE length(coalesce(discovery_notes,'')) > 10").fetchone()[0]
    print(f"before:  mythology {before_myth}   discovery {before_disc}")

    restored = 0
    for obj in solar:
        name = obj["name"]
        myth = obj.get("mythology", "") or ""
        disc = obj.get("discovery", "") or ""
        if not myth and not disc:
            continue
        row = conn.execute(
            "SELECT name FROM narratives WHERE name=?", (name,)).fetchone()
        if args.dry_run:
            print(f"  would restore {name}: "
                  f"mythology {len(myth)} chars, discovery {len(disc)} chars"
                  f"{'' if row else '  (no narrative row yet)'}")
            restored += 1
            continue
        if row:
            conn.execute(
                "UPDATE narratives SET "
                "mythology       = CASE WHEN ?<>'' THEN ? ELSE mythology END, "
                "discovery_notes = CASE WHEN ?<>'' THEN ? ELSE discovery_notes END "
                "WHERE name=?",
                (myth, myth, disc, disc, name))
        else:
            conn.execute(
                "INSERT INTO narratives (name,mythology,discovery_notes) "
                "VALUES (?,?,?)", (name, myth, disc))
        restored += 1

    # The Moon's distance.
    moon = next((o for o in solar if o["name"] == "Moon"), None)
    if moon and moon.get("distance_ly"):
        cur = conn.execute(
            "SELECT distance_ly FROM objects WHERE name='Moon'").fetchone()
        want = moon["distance_ly"]
        if cur and cur[0] is not None and abs(cur[0] - want) > 1e-12:
            print(f"  Moon distance {cur[0]:.4g} -> {want:.4g} light-years")
            if not args.dry_run:
                conn.execute("UPDATE objects SET distance_ly=? "
                             "WHERE name='Moon'", (want,))

    if args.dry_run:
        print(f"\ndry run: {restored} object(s) would be repaired")
        return 0

    conn.commit()
    after_myth = conn.execute(
        "SELECT count(*) FROM narratives "
        "WHERE length(coalesce(mythology,'')) > 20").fetchone()[0]
    after_disc = conn.execute(
        "SELECT count(*) FROM narratives "
        "WHERE length(coalesce(discovery_notes,'')) > 10").fetchone()[0]
    print(f"after:   mythology {after_myth}   discovery {after_disc}")
    print(f"repaired {restored} object(s)")
    conn.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
