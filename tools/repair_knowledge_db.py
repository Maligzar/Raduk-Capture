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
    ap.add_argument("--simbad", action="store_true",
                    help="also fetch Messier distances from SIMBAD "
                         "(one request, needs network)")
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

    if args.simbad:
        repair_simbad(conn, args.dry_run)

    if args.dry_run:
        print(f"\ndry run: {restored} object(s) would be repaired")
        return 0

    if args.simbad:
        repair_simbad(conn, args.dry_run)

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


def repair_simbad(conn, dry_run):
    """Fill in Messier distances, which have never once landed.

    build_knowledge_db.py asks SIMBAD for them and then writes
    UPDATE objects ... WHERE name='M42' -- against a table that stores
    'M042', because OpenNGC zero-pads to three digits. Every update
    matched nothing, and the loop counted rows RETURNED rather than
    rows CHANGED, so the log reported success each time.

    One request, and it is the whole reason the panel's "the light
    arriving now left ..." line works for nine objects instead of a
    hundred and sixteen.
    """
    import re
    try:
        import requests
    except ImportError:
        print("  --simbad needs: pip install requests")
        return

    bk = load_builder_tables()

    # The builder's query asks for basic.dist_value and basic.main_type.
    # Neither column exists any more -- SIMBAD answers 400 with
    # "unresolved identifiers", and the builder swallows it in a
    # try/except, so this phase has been failing silently as well as
    # writing to the wrong row names. Distances now live in
    # mesDistance, with a unit per measurement.
    query = """
    SELECT ident.id, mesDistance.dist, mesDistance.unit, basic.sp_type
    FROM basic
    JOIN ident ON basic.oid = ident.oidref
    JOIN mesDistance ON basic.oid = mesDistance.oidref
    WHERE ident.id LIKE 'M %'
    """
    print("  asking SIMBAD for Messier distances...")
    try:
        r = requests.post(bk.SIMBAD_TAP,
            data={"REQUEST": "doQuery", "LANG": "ADQL",
                  "FORMAT": "json", "QUERY": query},
            timeout=90,
            headers={"User-Agent": "RadukCapture-StarProfessor/1.0"})
        r.raise_for_status()
        data = r.json()
    except Exception as e:
        print(f"  SIMBAD request failed: {e}")
        return

    rows = data.get("data", [])
    cols = [c["name"] for c in data.get("metadata", [])]
    print(f"  SIMBAD returned {len(rows)} measurement(s)")

    TO_PC = {"pc": 1.0, "kpc": 1000.0, "Mpc": 1.0e6}

    # Several published distances per object, so take the MEDIAN rather
    # than whichever happened to come back first. A panel that tells
    # people how far away something is should not be quoting an
    # outlier it picked by accident.
    per_object = {}
    spectral = {}
    for row in rows:
        o = dict(zip(cols, row))
        name = re.sub(r"\s+", "", (o.get("id") or "").strip())
        m = re.fullmatch(r"M(\d{1,3})", name)
        if m:
            name = "M" + m.group(1).zfill(3)
        d = o.get("dist")
        unit = (o.get("unit") or "").strip()
        if d is None or unit not in TO_PC:
            continue
        per_object.setdefault(name, []).append(float(d) * TO_PC[unit])
        if o.get("sp_type"):
            spectral[name] = o["sp_type"]

    changed = missed = 0
    for name, vals in sorted(per_object.items()):
        vals.sort()
        dist_pc = vals[len(vals) // 2]
        dist_ly = dist_pc * 3.26156
        if dry_run:
            hit = conn.execute("SELECT 1 FROM objects WHERE name=?",
                               (name,)).fetchone()
            if hit: changed += 1
            else:   missed += 1
            continue
        cur = conn.execute(
            "UPDATE objects SET distance_pc=?, distance_ly=?, "
            "spectral_type=COALESCE(NULLIF(?,''), spectral_type) "
            "WHERE name=?",
            (dist_pc, dist_ly, spectral.get(name, ""), name))
        if cur.rowcount > 0: changed += 1
        else:                missed += 1

    print(f"  distances {'would be ' if dry_run else ''}set on {changed} "
          f"object(s); {missed} SIMBAD row(s) matched nothing here")


if __name__ == "__main__":
    sys.exit(main())
