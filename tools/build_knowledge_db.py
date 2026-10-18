#!/usr/bin/env python3
"""
build_knowledge_db.py — RadukCapture Star Professor Knowledge Base Builder
============================================================================
Builds D:\RadukCapture\data\object_knowledge.db from multiple free sources:

  - OpenNGC CSV       : physical data for all NGC/Messier objects
  - IAU star names    : official names for 451 named stars
  - Wikipedia API     : narrative summaries (rate-limited, respectful)
  - SIMBAD TAP        : distance, spectral type, classification
  - Allen's Star Names: mythology (Project Gutenberg, parsed locally)
  - Hardcoded notes   : curated imaging notes per object

Run once on the capture PC. Takes ~20-30 min (rate-limited Wikipedia calls).
Subsequent runs update existing records without losing manual edits.

Usage:
    python build_knowledge_db.py
    python build_knowledge_db.py --skip-wikipedia   (fast rebuild, no API calls)
    python build_knowledge_db.py --object "M42"     (update single object)

Requirements: pip install requests
"""

import sqlite3
import requests
import time
import os
import sys
import csv
import io
import re
import json
import argparse
from datetime import datetime

# ── Configuration ───────────────────────────────────────────────────────────

DB_PATH       = r"D:\RadukCapture\data\object_knowledge.db"
LOG_PATH      = r"D:\RadukCapture\logs\knowledge_build.log"

WIKIPEDIA_API = "https://en.wikipedia.org/api/rest_v1/page/summary/{}"
SIMBAD_TAP    = "https://simbad.cds.unistra.fr/simbad/sim-tap/sync"
OPNNGC_URL    = "https://raw.githubusercontent.com/mattiaverga/OpenNGC/master/database_files/NGC.csv"
IAU_NAMES_URL = "https://www.pas.rochester.edu/~emamajek/WGSN/IAU-CSN.txt"

RATE_LIMIT_SEC = 0.5   # delay between Wikipedia API calls
MAX_RETRIES    = 3

# ── Imaging notes — hardcoded for best objects ──────────────────────────────
# Format: object_name -> {aperture, filters, notes, best_season}

IMAGING_NOTES = {
    # Messier Showpieces
    "M1":  {"aperture":"8\"+", "filters":"OIII, Ha", "notes":"Crab Nebula supernova remnant. Look for filamentary structure. Requires narrowband to stand out from background. Center pulsar is stellar.", "best":"Nov-Feb"},
    "M8":  {"aperture":"Any",  "filters":"Ha, OIII", "notes":"Lagoon Nebula. Bright enough for widefield. The dark lane (the lagoon) bisects bright emission. Paired with M20 for dramatic 2-frame mosaic.", "best":"Jun-Aug"},
    "M13": {"aperture":"6\"+", "filters":"None/LPro", "notes":"Hercules Cluster. Resolve to pinpoints at f/10. Look for 3-pronged star chains ('propeller') off core. Core overexposes quickly — HDR stack helps.", "best":"Apr-Jul"},
    "M20": {"aperture":"6\"+", "filters":"Ha, OIII, SII", "notes":"Trifid Nebula. Three-lobed emission (red) + reflection (blue) + dark lanes. Trifurcating dark lanes need good seeing and resolution.", "best":"Jun-Aug"},
    "M27": {"aperture":"4\"+", "filters":"OIII, Ha", "notes":"Dumbbell Nebula. Brightest planetary nebula. Apple-core shape. OIII shows lobes dramatically. Central star visible in 8\"+.", "best":"Jul-Sep"},
    "M31": {"aperture":"Any",  "filters":"LPro, Ha", "notes":"Andromeda Galaxy. Too large for most sensors — plan mosaic. Dust lanes visible in dark skies. Satellite galaxies M32/M110 in same FOV.", "best":"Sep-Dec"},
    "M42": {"aperture":"Any",  "filters":"Ha, OIII, LPro", "notes":"Great Orion Nebula. Best deep sky object for beginners. Trapezium core saturates — HDR essential. Wispy outer tendrils reward long exposures. Running Man nebula (NGC 1977) adjacent.", "best":"Nov-Feb"},
    "M45": {"aperture":"Any",  "filters":"None, LPro", "notes":"Pleiades. Blue reflection nebula needs dark sky. Merope nebula (NGC 1435) most prominent. Wide FOV preferred — cluster is 2° wide.", "best":"Oct-Feb"},
    "M51": {"aperture":"8\"+", "filters":"LPro, LRGB", "notes":"Whirlpool Galaxy. Companion NGC 5195 connected by tidal bridge. Spiral arms resolve well in 8\"+ under dark skies. Requires good seeing for HII region detail.", "best":"Mar-Jun"},
    "M57": {"aperture":"6\"+", "filters":"OIII, Ha", "notes":"Ring Nebula. Iconic smoke-ring planetary. Central white dwarf requires 12\"+ and good conditions. Outer halo extremely faint — needs stacking. OIII shows ring brightly.", "best":"Jun-Aug"},
    "M63": {"aperture":"8\"+", "filters":"LRGB, LPro", "notes":"Sunflower Galaxy. Striking spiral with mottled arms. Surface brightness gradient — expose generously. HII regions visible in long integrations.", "best":"Mar-May"},
    "M64": {"aperture":"8\"+", "filters":"LRGB", "notes":"Black Eye Galaxy. Dark dust lane ('black eye') offset from nucleus is defining feature. Relatively compact — suits longer focal lengths.", "best":"Mar-May"},
    "M78": {"aperture":"6\"+", "filters":"LRGB, Ha", "notes":"Reflection nebula in Orion. Blue-white haze around two 10th-mag stars. Embedded in extensive dust — Ha reveals surrounding HII.", "best":"Nov-Feb"},
    "M82": {"aperture":"6\"+", "filters":"Ha, LRGB", "notes":"Cigar Galaxy starburst. Ha captures the superwind filaments erupting perpendicular to disk — dramatic red jets. Best paired with M81 in same frame.", "best":"Feb-May"},
    "M97": {"aperture":"8\"+", "filters":"OIII", "notes":"Owl Nebula. Low surface brightness planetary. Two dark eye patches (the owl eyes) need excellent transparency. OIII essential.", "best":"Feb-Apr"},
    "M101":{"aperture":"8\"+", "filters":"LRGB, Ha", "notes":"Pinwheel Galaxy. Large face-on spiral with extensive HII regions. Ha reveals dozens of star-forming knots. Needs dark sky for outer arm detail.", "best":"Apr-Jun"},
    "M104":{"aperture":"6\"+", "filters":"LRGB", "notes":"Sombrero Galaxy. Dust lane bisects bright core. Edge-on orientation. Excellent at any focal length — dust lane detail improves at f/8+.", "best":"Mar-May"},
    # Planets
    "Jupiter":{"aperture":"6\"+", "filters":"None, Red, Blue", "notes":"Best planetary target. Use R, G, B filters for atmospheric belt detail. IR742 cuts through seeing turbulence — try IR-pass captures. Record 3000+ frames, stack top 20% in AutoStakkert.", "best":"At opposition"},
    "Saturn": {"aperture":"6\"+", "filters":"None, Red", "notes":"Cassini division visible at 100x+. Ring tilt varies year to year. Moons (Titan brightest) trackable. Same capture workflow as Jupiter — high frame rate, best % stacking.", "best":"At opposition"},
    "Mars":   {"aperture":"8\"+", "filters":"Red, Orange, Blue", "notes":"Only worth imaging near opposition (every ~26 months). Surface detail requires excellent seeing. Red filter enhances polar caps and dark regions.", "best":"At opposition"},
    "Venus":  {"aperture":"Any",  "filters":"UV, Violet", "notes":"Phases visible in any telescope. UV filter reveals atmospheric cloud structure. Featureless in visible light.", "best":"Elongation"},
    "Moon":   {"aperture":"Any",  "filters":"ND, Moon filter", "notes":"Neutral density filter essential to reduce glare. Terminator region shows maximum shadow relief. Mosaics can cover whole surface.", "best":"Any"},
    "Sun":    {"aperture":"Any",  "filters":"SOLAR FILTER REQUIRED", "notes":"⚠ NEVER image without a certified solar filter. White-light filter shows sunspots and faculae. Ha solar scope shows prominences and chromosphere.", "best":"Any"},
    # Constellations
    "Orion":  {"aperture":"Any",  "filters":"Ha, OIII", "notes":"Most photogenic constellation. Entire area embedded in Barnard's Loop (giant HII shell). M42, M78, Horsehead (B33), Flame (NGC 2024), Running Man all within 10° of Betelgeuse.", "best":"Nov-Feb"},
    "Cygnus": {"aperture":"Any",  "filters":"Ha, OIII, SII", "notes":"Richest narrowband region in northern sky. North America Nebula (NGC 7000), Pelican, Veil complex, Crescent (NGC 6888), Gamma Cygni all within 15°. Narrowband heaven.", "best":"Jun-Sep"},
    "Sagittarius":{"aperture":"Any","filters":"Ha, OIII, widefield","notes":"Points toward galactic center. Densest star fields. Lagoon, Trifid, Omega, Eagle, Swan nebulae all in 15° area. Galactic core imaging target.", "best":"Jun-Aug"},
}

# Solar system narrative additions
SOLAR_SYSTEM = [
    {"name":"Sun",     "type":"Star",      "const":"N/A",     "mag":-26.7, "distance_ly":0.000016, "size_ly":0,
     "wiki_title":"Sun",
     "mythology":"The Sun has been worshipped as a deity by virtually every human civilization. Ra (Egypt), Apollo (Greece/Rome), Helios (Greece), Sol Invictus (Rome), Surya (India), Amaterasu (Japan). The ancient Greeks debated whether it was a burning stone or a god. Copernicus placed it correctly at the center of the solar system in 1543.",
     "discovery":"Known since prehistory. Copernican heliocentrism 1543. Fraunhofer absorption lines discovered 1814. Helium discovered in Sun before Earth (1868)."},
    # 384,400 km is 1.282 light-seconds, which is 4.063e-08 light-years.
    # This read 1.07e-08 -- about 101,000 km -- so the panel's "the light
    # arriving now left ..." line came out at a third of the truth.
    {"name":"Moon",    "type":"Satellite", "const":"N/A",     "mag":-12.7, "distance_ly":0.00000004063,
     "wiki_title":"Moon",
     "mythology":"Luna (Rome), Selene/Artemis (Greece), Sin (Babylon), Tsukuyomi (Japan), Chang'e (China). The Moon's phases governed ancient calendars worldwide. Many cultures saw a face, rabbit, or toad in the lunar maria.",
     "discovery":"Known since prehistory. First telescopic observations by Galileo 1609. First human landing Apollo 11, July 20, 1969."},
    {"name":"Mercury", "type":"Planet",    "const":"Various", "mag":-2.5,  "distance_ly":0.00000593,
     "wiki_title":"Mercury_(planet)",
     "mythology":"Named for the Roman messenger god (Greek: Hermes). Swift planet closest to the Sun, matching Mercury's role as divine messenger. Known since Babylonian times as two separate objects (morning and evening star) before Greeks recognized it as one planet.",
     "discovery":"Known since antiquity. Mariner 10 flyby 1974. MESSENGER orbiter 2011-2015."},
    {"name":"Venus",   "type":"Planet",    "const":"Various", "mag":-4.4,  "distance_ly":0.0000121,
     "wiki_title":"Venus",
     "mythology":"Named for the Roman goddess of love and beauty (Greek: Aphrodite). The brightest object in the sky after Sun and Moon. Babylonians called it Ishtar. Mayans built architecture aligned to Venus cycles and used it for warfare timing.",
     "discovery":"Known since antiquity. Galileo observed phases 1610, proving orbital motion. Venera probes (USSR) 1970s-80s."},
    {"name":"Mars",    "type":"Planet",    "const":"Various", "mag":0.5,   "distance_ly":0.0000241,
     "wiki_title":"Mars",
     "mythology":"Named for the Roman god of war (Greek: Ares). Red color associated with blood and conflict across cultures. Babylonians called it Nergal (plague/death god). Egyptians called it 'Her Desher' (the red one).",
     "discovery":"Known since antiquity. Schiaparelli's 'canali' sparked canal/life speculation 1877. Viking landers 1976. Curiosity rover 2012-present."},
    {"name":"Jupiter", "type":"Planet",    "const":"Various", "mag":-2.7,  "distance_ly":0.0000686,
     "wiki_title":"Jupiter",
     "mythology":"King of the Roman gods (Greek: Zeus). Largest planet, befitting its royal status. Great Red Spot observed since 1830s (possibly since 1665). Galilean moons (Io, Europa, Ganymede, Callisto) discovered by Galileo 1610 — first proof that not everything orbits Earth.",
     "discovery":"Known since antiquity. Galilean moons 1610. Voyager 1/2 flybys 1979. Juno orbiter 2016-present."},
    {"name":"Saturn",  "type":"Planet",    "const":"Various", "mag":0.7,   "distance_ly":0.000127,
     "wiki_title":"Saturn",
     "mythology":"Roman god of agriculture and time (Greek: Kronos). Slowest visible planet, associated with time's passage. Rings first seen by Galileo (confused as triple planet) 1610, correctly identified by Huygens 1655. Saturday named after Saturn.",
     "discovery":"Known since antiquity. Rings identified by Huygens 1655. Voyager 1/2 1980-81. Cassini orbiter 2004-2017."},
    {"name":"Uranus",  "type":"Planet",    "const":"Various", "mag":5.7,   "distance_ly":0.000286,
     "wiki_title":"Uranus",
     "mythology":"Ancient Greek personification of the sky, father of the Titans. First planet discovered in modern times by William Herschel, March 13, 1781. Herschel initially named it 'Georgium Sidus' (George's Star) for King George III. Name Uranus proposed by Bode.",
     "discovery":"William Herschel, 1781. First planet discovered with telescope. Voyager 2 flyby 1986 — only spacecraft to visit."},
    {"name":"Neptune", "type":"Planet",    "const":"Various", "mag":8.0,   "distance_ly":0.000473,
     "wiki_title":"Neptune",
     "mythology":"Roman god of the sea (Greek: Poseidon). Deep blue color inspired the name. First planet discovered through mathematical prediction rather than observation — Le Verrier and Adams independently predicted its position from Uranus perturbations. Johann Galle confirmed in 1846.",
     "discovery":"Predicted mathematically by Le Verrier/Adams, observed by Galle 1846. Voyager 2 flyby 1989 — only spacecraft to visit."},
]

# ── Database schema ──────────────────────────────────────────────────────────

SCHEMA = """
CREATE TABLE IF NOT EXISTS objects (
    name            TEXT PRIMARY KEY,
    common_name     TEXT,
    catalog_ids     TEXT,   -- e.g. "M42, NGC 1976"
    type            TEXT,   -- Galaxy, Nebula, Cluster, Planet, Star, etc.
    subtype         TEXT,   -- Spiral, Planetary Nebula, Open Cluster, etc.
    constellation   TEXT,
    ra_h            REAL,
    dec_deg         REAL,
    magnitude       REAL,
    surface_brightness REAL,
    size_arcmin     REAL,   -- major axis
    distance_ly     REAL,
    distance_pc     REAL,
    size_ly         REAL,   -- physical diameter
    spectral_type   TEXT,
    updated_at      TEXT
);

CREATE TABLE IF NOT EXISTS narratives (
    name            TEXT PRIMARY KEY,
    summary         TEXT,   -- Wikipedia lead paragraph
    full_text       TEXT,   -- Extended Wikipedia extract
    discovery_notes TEXT,   -- Who discovered it, when, how
    mythology       TEXT,   -- Cultural/mythological context
    source_urls     TEXT,   -- JSON array of source URLs
    updated_at      TEXT
);

CREATE TABLE IF NOT EXISTS imaging_notes (
    name            TEXT PRIMARY KEY,
    aperture_min    TEXT,   -- e.g. "6\"+", "Any"
    filters         TEXT,   -- e.g. "Ha, OIII"
    imaging_notes   TEXT,   -- What to look for, tips, techniques
    best_season     TEXT,   -- e.g. "Nov-Feb"
    difficulty      TEXT,   -- Easy / Moderate / Challenging / Expert
    updated_at      TEXT
);

CREATE TABLE IF NOT EXISTS build_log (
    run_at          TEXT,
    objects_added   INTEGER,
    objects_updated INTEGER,
    errors          INTEGER,
    duration_sec    REAL
);
"""

# ── Logging ──────────────────────────────────────────────────────────────────

_log_file = None

def log(msg):
    ts = datetime.now().strftime("%H:%M:%S")
    line = f"[{ts}] {msg}"
    print(line)
    if _log_file:
        _log_file.write(line + "\n")
        _log_file.flush()

def init_log():
    global _log_file
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
    _log_file = open(LOG_PATH, "a", encoding="utf-8")
    log(f"=== Knowledge DB build started {datetime.now().isoformat()} ===")

# ── Database helpers ─────────────────────────────────────────────────────────

def open_db():
    os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)
    conn = sqlite3.connect(DB_PATH)
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA synchronous=NORMAL")
    conn.executescript(SCHEMA)
    conn.commit()
    return conn

def upsert_object(conn, row: dict):
    cols = list(row.keys())
    vals = list(row.values())
    placeholders = ",".join(["?"] * len(cols))
    col_str = ",".join(cols)
    update_str = ",".join([f"{c}=excluded.{c}" for c in cols if c != "name"])
    conn.execute(
        f"INSERT INTO objects ({col_str}) VALUES ({placeholders}) "
        f"ON CONFLICT(name) DO UPDATE SET {update_str}",
        vals
    )

def upsert_narrative(conn, name, summary="", full_text="",
                     discovery="", mythology="", sources=None):
    conn.execute("""
        INSERT INTO narratives (name,summary,full_text,discovery_notes,mythology,source_urls,updated_at)
        VALUES (?,?,?,?,?,?,?)
        ON CONFLICT(name) DO UPDATE SET
            -- NULLIF(...,'') before COALESCE, and it is not cosmetic.
            --
            -- COALESCE only skips NULL. These parameters default to the
            -- EMPTY STRING, so every later writer that did not happen to
            -- supply a field overwrote whatever an earlier one had put
            -- there. Phase 1 loads real mythology and discovery text for
            -- the solar system; Phase 3's Wikipedia pass then upserts the
            -- same rows with summary only, and wiped both -- which is why
            -- the shipped database has 679 narratives and ZERO rows of
            -- mythology or discovery notes, while this file is full of
            -- both.
            summary=COALESCE(NULLIF(excluded.summary,''), summary),
            full_text=COALESCE(NULLIF(excluded.full_text,''), full_text),
            discovery_notes=COALESCE(NULLIF(excluded.discovery_notes,''),
                                     discovery_notes),
            mythology=COALESCE(NULLIF(excluded.mythology,''), mythology),
            source_urls=COALESCE(NULLIF(excluded.source_urls,'[]'),
                                 source_urls),
            updated_at=excluded.updated_at
    """, [name, summary, full_text, discovery, mythology,
          json.dumps(sources or []), datetime.now().isoformat()])

def upsert_imaging(conn, name, aperture="", filters="",
                   notes="", season="", difficulty=""):
    conn.execute("""
        INSERT INTO imaging_notes (name,aperture_min,filters,imaging_notes,best_season,difficulty,updated_at)
        VALUES (?,?,?,?,?,?,?)
        ON CONFLICT(name) DO UPDATE SET
            aperture_min=COALESCE(excluded.aperture_min, aperture_min),
            filters=COALESCE(excluded.filters, filters),
            imaging_notes=COALESCE(excluded.imaging_notes, imaging_notes),
            best_season=COALESCE(excluded.best_season, best_season),
            difficulty=COALESCE(excluded.difficulty, difficulty),
            updated_at=excluded.updated_at
    """, [name, aperture, filters, notes, season, difficulty,
          datetime.now().isoformat()])

# ── Wikipedia fetcher ────────────────────────────────────────────────────────

def fetch_wikipedia(title: str, retries=MAX_RETRIES):
    url = WIKIPEDIA_API.format(requests.utils.quote(title))
    for attempt in range(retries):
        try:
            r = requests.get(url, timeout=15,
                headers={"User-Agent": "RadukCapture-StarProfessor/1.0 (educational astronomy app)"})
            if r.status_code == 200:
                data = r.json()
                return {
                    "summary": data.get("extract", "")[:2000],
                    "full":    data.get("extract", ""),
                    "url":     data.get("content_urls", {}).get("desktop", {}).get("page", ""),
                    "title":   data.get("title", title),
                }
            elif r.status_code == 404:
                return None
            else:
                time.sleep(2 ** attempt)
        except Exception as e:
            log(f"  Wikipedia error ({title}): {e}")
            time.sleep(2 ** attempt)
    return None

# ── OpenNGC loader ───────────────────────────────────────────────────────────

def load_opengc(conn):
    log("Downloading OpenNGC catalogue...")
    try:
        r = requests.get(OPNNGC_URL, timeout=30,
            headers={"User-Agent": "RadukCapture-StarProfessor/1.0"})
        r.raise_for_status()
    except Exception as e:
        log(f"  ERROR downloading OpenNGC: {e}")
        return 0

    count = 0
    reader = csv.DictReader(io.StringIO(r.text), delimiter=";")
    for row in reader:
        name = row.get("Name", "").strip()
        if not name:
            continue

        # Build display name
        m_id = row.get("M", "").strip()
        display = f"M{m_id}" if m_id else name

        obj_type_map = {
            "GX": "Galaxy",    "OC": "Open Cluster", "GC": "Globular Cluster",
            "BN": "Nebula",    "EN": "Emission Nebula", "RN": "Reflection Nebula",
            "PN": "Planetary Nebula", "SNR": "Supernova Remnant",
            "DN": "Dark Nebula", "NF": "Nebula", "CL+N": "Cluster+Nebula",
            "**": "Double Star", "*": "Star",
        }
        raw_type = row.get("Type", "").strip()
        obj_type = obj_type_map.get(raw_type, raw_type or "Unknown")

        def safe_float(v, default=None):
            try: return float(v) if v and v.strip() else default
            except: return default

        mag = safe_float(row.get("V-Mag") or row.get("B-Mag"))
        ra_str = row.get("RA", "").strip()
        dec_str = row.get("Dec", "").strip()

        ra_h = None
        if ra_str:
            try:
                parts = ra_str.split(":")
                ra_h = float(parts[0]) + float(parts[1])/60 + float(parts[2])/3600
            except: pass

        dec_deg = None
        if dec_str:
            try:
                sign = -1 if dec_str.startswith("-") else 1
                parts = dec_str.lstrip("+-").split(":")
                dec_deg = sign * (float(parts[0]) + float(parts[1])/60 + float(parts[2])/3600)
            except: pass

        catalog_ids = name
        if m_id:
            catalog_ids = f"M{m_id}, {name}"

        upsert_object(conn, {
            "name":         display,
            "common_name":  row.get("Common names", "").strip() or None,
            "catalog_ids":  catalog_ids,
            "type":         obj_type,
            "subtype":      raw_type,
            "constellation":row.get("Const", "").strip(),
            "ra_h":         ra_h,
            "dec_deg":      dec_deg,
            "magnitude":    mag,
            "surface_brightness": safe_float(row.get("SurfBr")),
            "size_arcmin":  safe_float(row.get("MajAx")),
            "updated_at":   datetime.now().isoformat(),
        })
        count += 1

    conn.commit()
    log(f"  Loaded {count} objects from OpenNGC")
    return count

# ── IAU named stars loader ───────────────────────────────────────────────────

def load_iau_stars(conn):
    log("Downloading IAU star names...")
    try:
        r = requests.get(IAU_NAMES_URL, timeout=30,
            headers={"User-Agent": "RadukCapture-StarProfessor/1.0"})
        r.raise_for_status()
    except Exception as e:
        log(f"  ERROR downloading IAU star names: {e}")
        return 0

    count = 0
    for line in r.text.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or line.startswith("IAU"):
            continue
        # Format: Name | Designation | HIP | HD | RA | Dec | Mag | Constellation | ...
        parts = re.split(r'\s{2,}|\t', line)
        if len(parts) < 5:
            parts = line.split()
        if len(parts) < 4:
            continue

        name = parts[0].strip()
        if not name or name.upper() == name:  # skip header-like all-caps lines
            continue

        try:
            mag = float(parts[6]) if len(parts) > 6 else None
        except: mag = None
        try:
            const = parts[7].strip() if len(parts) > 7 else ""
        except: const = ""

        upsert_object(conn, {
            "name":       name,
            "type":       "Star",
            "constellation": const,
            "magnitude":  mag,
            "updated_at": datetime.now().isoformat(),
        })
        count += 1

    conn.commit()
    log(f"  Loaded {count} named stars from IAU catalogue")
    return count

# ── Solar system loader ──────────────────────────────────────────────────────

def load_solar_system(conn):
    log("Loading solar system objects...")
    for obj in SOLAR_SYSTEM:
        upsert_object(conn, {
            "name":       obj["name"],
            "type":       obj["type"],
            "constellation": obj.get("const", "Various"),
            "magnitude":  obj.get("mag"),
            "distance_ly":obj.get("distance_ly"),
            "updated_at": datetime.now().isoformat(),
        })
        upsert_narrative(conn,
            name=obj["name"],
            discovery=obj.get("discovery", ""),
            mythology=obj.get("mythology", ""),
        )
    conn.commit()
    log(f"  Loaded {len(SOLAR_SYSTEM)} solar system objects")

# ── Constellation loader ─────────────────────────────────────────────────────

CONSTELLATIONS_88 = [
    ("Andromeda","And"),("Antlia","Ant"),("Apus","Aps"),("Aquarius","Aqr"),
    ("Aquila","Aql"),("Ara","Ara"),("Aries","Ari"),("Auriga","Aur"),
    ("Boötes","Boo"),("Caelum","Cae"),("Camelopardalis","Cam"),("Cancer","Cnc"),
    ("Canes Venatici","CVn"),("Canis Major","CMa"),("Canis Minor","CMi"),
    ("Capricornus","Cap"),("Carina","Car"),("Cassiopeia","Cas"),("Centaurus","Cen"),
    ("Cepheus","Cep"),("Cetus","Cet"),("Chamaeleon","Cha"),("Circinus","Cir"),
    ("Columba","Col"),("Coma Berenices","Com"),("Corona Australis","CrA"),
    ("Corona Borealis","CrB"),("Corvus","Crv"),("Crater","Crt"),("Crux","Cru"),
    ("Cygnus","Cyg"),("Delphinus","Del"),("Dorado","Dor"),("Draco","Dra"),
    ("Equuleus","Equ"),("Eridanus","Eri"),("Fornax","For"),("Gemini","Gem"),
    ("Grus","Gru"),("Hercules","Her"),("Horologium","Hor"),("Hydra","Hya"),
    ("Hydrus","Hyi"),("Indus","Ind"),("Lacerta","Lac"),("Leo","Leo"),
    ("Leo Minor","LMi"),("Lepus","Lep"),("Libra","Lib"),("Lupus","Lup"),
    ("Lynx","Lyn"),("Lyra","Lyr"),("Mensa","Men"),("Microscopium","Mic"),
    ("Monoceros","Mon"),("Musca","Mus"),("Norma","Nor"),("Octans","Oct"),
    ("Ophiuchus","Oph"),("Orion","Ori"),("Pavo","Pav"),("Pegasus","Peg"),
    ("Perseus","Per"),("Phoenix","Phe"),("Pictor","Pic"),("Pisces","Psc"),
    ("Piscis Austrinus","PsA"),("Puppis","Pup"),("Pyxis","Pyx"),("Reticulum","Ret"),
    ("Sagitta","Sge"),("Sagittarius","Sgr"),("Scorpius","Sco"),("Sculptor","Scl"),
    ("Scutum","Sct"),("Serpens","Ser"),("Sextans","Sex"),("Taurus","Tau"),
    ("Telescopium","Tel"),("Triangulum","Tri"),("Triangulum Australe","TrA"),
    ("Tucana","Tuc"),("Ursa Major","UMa"),("Ursa Minor","UMi"),("Vela","Vel"),
    ("Virgo","Vir"),("Volans","Vol"),("Vulpecula","Vul"),
]

def load_constellations(conn):
    log("Loading 88 constellations...")
    for name, abbr in CONSTELLATIONS_88:
        upsert_object(conn, {
            "name":       name,
            "common_name":abbr,
            "type":       "Constellation",
            "updated_at": datetime.now().isoformat(),
        })
    conn.commit()
    log(f"  Loaded {len(CONSTELLATIONS_88)} constellations")

# ── Imaging notes loader ─────────────────────────────────────────────────────

def load_imaging_notes(conn):
    log("Loading curated imaging notes...")
    count = 0
    for name, note in IMAGING_NOTES.items():
        # Guess difficulty from aperture requirement
        ap = note.get("aperture","Any")
        if "12" in ap or "16" in ap:
            diff = "Expert"
        elif "8" in ap:
            diff = "Challenging"
        elif "6" in ap:
            diff = "Moderate"
        else:
            diff = "Easy"
        upsert_imaging(conn, name,
            aperture=note.get("aperture",""),
            filters=note.get("filters",""),
            notes=note.get("notes",""),
            season=note.get("best",""),
            difficulty=diff)
        count += 1
    conn.commit()
    log(f"  Loaded {count} imaging note entries")

# ── Wikipedia fetch loop ─────────────────────────────────────────────────────

def build_wikipedia_narratives(conn, skip_existing=True):
    log("Fetching Wikipedia narratives (priority objects only)...")

    # Only fetch Wikipedia for objects that matter to an astrophotographer.
    # Obscure NGC objects have no article or useless stubs — skip them.
    # Priority list: all Messier, bright NGC, named stars, planets, constellations.
    PRIORITY_TYPES = {'Planet', 'Star', 'Constellation', 'Satellite', 'Galaxy',
                      'Emission Nebula', 'Planetary Nebula', 'Supernova Remnant',
                      'Globular Cluster', 'Open Cluster', 'Nebula', 'Cluster+Nebula'}

    # Only fetch NGC objects with magnitude brighter than 10
    # (fainter ones are too obscure for useful Wikipedia articles)
    MAX_NGC_MAG = 10.0

    cur = conn.execute("""
        SELECT name, common_name, type, magnitude
        FROM objects
        WHERE type IN ('Planet','Star','Constellation','Satellite')
           OR (name LIKE 'M%' AND CAST(REPLACE(name,'M','') AS INTEGER) BETWEEN 1 AND 110)
           OR (common_name IS NOT NULL AND common_name != '' AND magnitude < ?)
           OR type IN ('Emission Nebula','Planetary Nebula','Supernova Remnant',
                       'Globular Cluster','Galaxy')
              AND magnitude < ?
        ORDER BY magnitude ASC NULLS LAST, type, name
    """, (MAX_NGC_MAG, MAX_NGC_MAG))
    all_objects = cur.fetchall()

    if skip_existing:
        existing = set(row[0] for row in
            conn.execute("SELECT name FROM narratives WHERE summary != '' AND summary IS NOT NULL"))
    else:
        existing = set()

    log(f"  Priority objects to fetch: {len(all_objects)} (skipping {len(existing)} already done)")

    title_overrides = {
        "M1":  "Crab Nebula",     "M8":  "Lagoon Nebula",
        "M13": "Messier 13",      "M17": "Omega Nebula",
        "M20": "Trifid Nebula",   "M27": "Dumbbell Nebula",
        "M31": "Andromeda Galaxy","M32": "Messier 32",
        "M33": "Triangulum Galaxy","M42":"Orion Nebula",
        "M44": "Beehive Cluster", "M45": "Pleiades",
        "M51": "Whirlpool Galaxy","M57": "Ring Nebula",
        "M63": "Sunflower Galaxy","M64": "Black Eye Galaxy",
        "M81": "Bode's Galaxy",   "M82": "Cigar Galaxy",
        "M87": "Messier 87",      "M97": "Owl Nebula",
        "M101":"Pinwheel Galaxy", "M104":"Sombrero Galaxy",
        "M110":"Messier 110",
    }
    for name, _ in CONSTELLATIONS_88:
        if name not in title_overrides:
            title_overrides[name] = f"{name} (constellation)"

    fetched = skipped = errors = 0

    for name, common_name, obj_type, magnitude in all_objects:
        if name in existing:
            skipped += 1
            continue

        wiki_title = title_overrides.get(name) or common_name or name
        if name.startswith("NGC") and common_name:
            wiki_title = common_name

        log(f"  [{fetched+skipped+1}/{len(all_objects)}] {name} → {wiki_title}")
        result = fetch_wikipedia(wiki_title)

        if result and result.get("summary"):
            upsert_narrative(conn, name=name,
                summary=result["summary"][:1000],
                full_text=result["full"],
                sources=[result["url"]])
            fetched += 1
        else:
            errors += 1

        conn.commit()
        time.sleep(RATE_LIMIT_SEC)

    log(f"  Wikipedia: {fetched} fetched, {skipped} skipped, {errors} not found")
    return fetched

# ── SIMBAD physical data ─────────────────────────────────────────────────────

def fetch_simbad_distances(conn):
    """Batch fetch distances for Messier objects from SIMBAD TAP."""
    log("Fetching distances from SIMBAD...")

    # ── The query, corrected 2026-10-17 ──────────────────────
    #
    # This asked for basic.dist_value and basic.main_type. Neither
    # column exists in SIMBAD's schema any more: the service answers
    # 400 with "unresolved identifiers", the except below swallows it,
    # and the phase has been doing nothing at all. Distances live in
    # mesDistance now, with a unit per measurement.
    query = """
    SELECT ident.id, mesDistance.dist, mesDistance.unit, basic.sp_type
    FROM basic
    JOIN ident ON basic.oid = ident.oidref
    JOIN mesDistance ON basic.oid = mesDistance.oidref
    WHERE ident.id LIKE 'M %'
    """
    TO_PC = {"pc": 1.0, "kpc": 1000.0, "Mpc": 1.0e6}
    try:
        r = requests.post(SIMBAD_TAP,
            data={"REQUEST":"doQuery","LANG":"ADQL","FORMAT":"json","QUERY":query},
            timeout=90,
            headers={"User-Agent":"RadukCapture-StarProfessor/1.0"})
        r.raise_for_status()
        data = r.json()
        rows = data.get("data", [])
        cols = [c["name"] for c in data.get("metadata", [])]

        # Several published distances per object, so take the MEDIAN
        # rather than whichever came back first. A panel telling people
        # how far away something is should not be quoting an outlier it
        # picked by accident.
        per_object, spectral = {}, {}
        for row in rows:
            obj = dict(zip(cols, row))
            # "M  42" -> "M042", NOT "M42".
            #
            # load_opengc stores Messier objects zero-padded to three
            # digits, which is how OpenNGC names them. Stripping the
            # spaces and stopping meant every UPDATE below matched
            # WHERE name='M42' against a table holding 'M042' and
            # changed nothing.
            name = re.sub(r'\s+', '', (obj.get("id") or "").strip())
            m = re.fullmatch(r'M(\d{1,3})', name)
            if m:
                name = "M" + m.group(1).zfill(3)
            d = obj.get("dist")
            unit = (obj.get("unit") or "").strip()
            if d is None or unit not in TO_PC:
                continue
            per_object.setdefault(name, []).append(float(d) * TO_PC[unit])
            if obj.get("sp_type"):
                spectral[name] = obj["sp_type"]

        count = 0
        for name, vals in per_object.items():
            vals.sort()
            dist_pc = vals[len(vals) // 2]
            # Count what actually CHANGED, not what was attempted. The
            # old loop counted rows returned by SIMBAD, so the log
            # reported success every time it updated nothing.
            cur = conn.execute("""
                UPDATE objects SET distance_pc=?, distance_ly=?,
                    spectral_type=COALESCE(NULLIF(?,''), spectral_type)
                WHERE name=?
            """, [dist_pc, dist_pc * 3.26156, spectral.get(name, ""), name])
            if cur.rowcount > 0:
                count += 1

        conn.commit()
        log(f"  SIMBAD: updated {count} distance records")
        return count

    except Exception as e:
        log(f"  SIMBAD error: {e}")
        return 0

# ── Main build ───────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Build RadukCapture Star Professor knowledge DB")
    parser.add_argument("--skip-wikipedia", action="store_true",
                        help="Skip Wikipedia fetch (fast rebuild of physical/imaging data only)")
    parser.add_argument("--skip-existing",  action="store_true", default=True,
                        help="Skip objects already in narratives table (default: True)")
    parser.add_argument("--object", type=str, default=None,
                        help="Update a single named object only")
    args = parser.parse_args()

    init_log()
    start_time = time.time()

    log(f"DB path: {DB_PATH}")
    conn = open_db()

    added = 0
    updated = 0
    errors = 0

    if args.object:
        # Single object update mode
        name = args.object
        log(f"Single object update: {name}")
        result = fetch_wikipedia(name)
        if result:
            upsert_narrative(conn, name=name,
                summary=result["summary"][:1000],
                full_text=result["full"],
                sources=[result["url"]])
            conn.commit()
            log(f"Updated {name} from Wikipedia")
        else:
            log(f"Not found on Wikipedia: {name}")
        conn.close()
        return

    # ── Phase 1: Structural data ─────────────────────────────────────────
    log("\n── Phase 1: Loading structural data ──────────────────────────────")
    added += load_opengc(conn)
    added += load_iau_stars(conn)
    load_solar_system(conn)
    load_constellations(conn)
    load_imaging_notes(conn)

    # ── Phase 2: Physical measurements ──────────────────────────────────
    log("\n── Phase 2: SIMBAD physical data ─────────────────────────────────")
    fetch_simbad_distances(conn)

    # ── Phase 3: Wikipedia narratives ───────────────────────────────────
    if not args.skip_wikipedia:
        log("\n── Phase 3: Wikipedia narratives ─────────────────────────────────")
        log("  This takes ~20-30 min. Press Ctrl+C to pause (safe, restartable).")
        try:
            build_wikipedia_narratives(conn, skip_existing=args.skip_existing)
        except KeyboardInterrupt:
            log("\nInterrupted — progress saved. Re-run to continue.")
    else:
        log("\n── Phase 3: Wikipedia skipped (--skip-wikipedia) ─────────────────")

    # ── Summary ──────────────────────────────────────────────────────────
    elapsed = time.time() - start_time
    obj_count  = conn.execute("SELECT COUNT(*) FROM objects").fetchone()[0]
    narr_count = conn.execute("SELECT COUNT(*) FROM narratives WHERE summary != ''").fetchone()[0]
    note_count = conn.execute("SELECT COUNT(*) FROM imaging_notes").fetchone()[0]

    conn.execute("INSERT INTO build_log VALUES (?,?,?,?,?)",
        [datetime.now().isoformat(), added, updated, errors, elapsed])
    conn.commit()
    conn.close()

    log(f"\n{'='*60}")
    log(f"Build complete in {elapsed:.0f}s")
    log(f"  Objects:       {obj_count}")
    log(f"  Narratives:    {narr_count}")
    log(f"  Imaging notes: {note_count}")
    log(f"  DB:            {DB_PATH}")
    log(f"{'='*60}\n")
    log("Leoben Star Professor is ready.")
    log("Ask it: 'What is M42?' or 'Tell me about Jupiter' or 'How do I image the Veil Nebula?'")

if __name__ == "__main__":
    main()
