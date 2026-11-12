# External data sources, reviewed against the roadmap

Written 2026-11-11. The question: of the public astronomy data ecosystem,
what should RadukCapture pull in, pull more of, or leave alone? The
ranking axis is this project's, not science's: does it raise surface
detail on bright targets or save time at the eyepiece, and does it work
offline? The roadmap carries the decisions; this file carries the
evidence.

Every claim is labelled. **Tested**: run against the live service from a
cloud session on 2026-11-11. **Sourced**: from the provider's own pages
or a named source, linked at the end. **Inferred**: reasoning, not fact.
**Recalled**: memory, may be stale. Unlabelled means read from this
repository.

## What the app already pulls

| Source | Used for | Where |
|---|---|---|
| CelesTrak `GROUP=visual`, TLE | Satellite passes, sky map | `src/core/SatDesk.h:356`, `src/sky/Sgp4.h` |
| JPL Horizons API | NEO, comet and spacecraft positions, the Sun for NEO brightness | `src/sky/Ephemeris.h`, `src/core/NeoDesk.h` |
| JPL close-approach API | Which NEOs to show | `src/sky/NeoList.h` |
| SIMBAD TAP, OpenNGC, Wikipedia | The object knowledge database (14,520 objects) | `tools/build_knowledge_db.py`, `data/object_knowledge.db` |
| ASTAP star databases | Plate solving | `src/sky/ASTAPSolver.h` |
| Leoben (Open-Meteo, 7Timer, GFS, NWS) | Weather, dew risk, imaging score | `src/net/LeobenWeather.h` |
| Built-in tables | 255 bright stars, Messier, 99 lunar features, Meeus ephemeris | `src/sky/SkyObject.h`, `src/sky/LunarFeatures.h` |

## Findings that change the roadmap

### 1. Satellite elements: TLE no longer carries new objects

CelesTrak's five-digit catalog numbers ran out on 2026-07-11, and the
TLE format cannot hold six (sourced). Both CelesTrak and Space-Track
recommend OMM for new software (sourced). Space-Track emits Alpha-5 in
TLEs (an `A0000`-style first character); whether CelesTrak does is
disputed by a third-party corpus and was not checked here.

The app fetches `FORMAT=tle` and parses the catalog field as five digits
(`Sgp4.h:132`, `TleField(line1, 2, 5)`), so an Alpha-5 line would read
as a wrong number and an object CelesTrak leaves out of TLE output never
arrives at all. The objects affected are the newest, which are exactly
the rocket bodies the owner's direction names as primary targets
(inferred). Nothing reports their absence.

CelesTrak's supplemental elements (SupGP) for the ISS come from NASA's
own ephemeris in 6-hour segments, fitted forward from the current epoch
rather than to past observations (sourced). No ISS-specific accuracy
figure was found.

### 2. Plate solving: this field is below every ASTAP catalog's floor

ASTAP's database listing gives solving ranges of 6° > FOV > 0.15° for
D80, 0.2° for D50, 0.3° for D20 and 0.6° for D05 (sourced). H18, which
the README still offers, is the older database D80 replaced (sourced).
`ASTAPSolver.h` passes ASTAP the image height, about 0.12° at the full
sensor and 0.203"/px. So the field is under D80's rated floor, and the
plate-scale check, the top live-sky item, could fail for want of stars
rather than for a wrong scale (inferred). The solver also downsamples
2x (`-z 2`), which removes the faintest stars in a field that is short
of them (inferred). Phase 94 reached the same conclusion from the other
side: a solvable field at this scale needs a catalog to about mag 14.

### 3. Jupiter: the season is now, and the app has no event data

Jupiter's 2027 opposition is about 2027-02-10/11 (sourced; several
sources, two outliers giving January 10, which was the 2026
opposition). Nothing outside the emulator knows where the Galilean moons
or the Great Red Spot are.

- **Moon events from Horizons (tested).** Observer quantity 12 for Io
  over 2026-11-12 to 14 returned `/*` clear, `/t` transiting, `/O`
  occulted, `/u` eclipsed and `/U` eclipsed and occulted, changing
  cleanly between rows. The app's Horizons client already reaches this
  service.
- **Shadow transits are not in those codes (inferred).** The code
  describes the moon, not its shadow on the disk. Shadow transits need
  the Sun-moon line intersected with Jupiter, which the Horizons vectors
  the app already fetches can supply.
- **IMCCE** answers (tested), but its `pub/ephem/satel/galilean/` folder
  holds theory files (`L1/`, `L2/`), not event tables. Its value is as a
  cross-check for tests, once a table is found.
- **GRS transit** is the System II central meridian minus the spot's
  longitude. The meridian is arithmetic (Meeus ch. 43, recalled). The
  longitude is measured and drifts: Sky & Telescope assumes 91° on
  2026-06-01 drifting 1.75°/month, Project Pluto (December 2025) about
  16°/year (sourced). The sources disagree. One degree is about
  1.65 min of transit time (inferred from System II's 9h 55m 40.6s).
  The BAA reports in System III since 2015, quoting drift in System II
  (sourced).

### 4. Seeing forecast: two free sources work, one paid one is unpriced

- **7Timer ASTRO** returns integer `seeing` and `transparency` on a 3 h
  step, no key (tested). `api.pl` now answers with a 302 to `astro.php`;
  a client that does not follow redirects gets an empty body (tested).
  The integer scale's meaning in arcseconds was not confirmed.
- **Open-Meteo** accepts `wind_speed_250hPa` and `wind_speed_200hPa`,
  in **km/h** by default (tested). meteoblue notes jet-stream speeds
  above 35 m/s (126 km/h), and also below 5 m/s, usually mean bad seeing
  (sourced).
- **meteoblue seeing** is purpose-built (arcseconds from layered
  turbulence), free on the website for 3 days and 7 with point+ or the
  API; a free API tier with 10M credits exists; the seeing package's
  credit cost was not found (sourced).
- A forecast cannot see the ground layer or tube currents, which on an
  8SE are often what limits (inferred). `SeeingMonitor` already measures
  the real figure, so the useful product is a per-night log of forecast
  against measured, which shows which source has skill at this site.

### 5. Time scales: no work needed

TT minus UT is a constant 69.2 s in `src/sky/LunarGeometry.h`. Bulletin
C 72 (2026-07-06) announced no leap second at the end of 2026, and
TAI minus UTC stays 37 s (sourced). UT1 minus UTC is held under 0.9 s by
definition, at most 13.5" of sidereal error against an 8° pointing error
and an image lock. Jupiter turns about 0.01° a second, so a second is
invisible to WinJUPOS. The real timing risk is the PC clock, and nothing
in `src/` checks it.

## Decisions

| Source | Decision | Why |
|---|---|---|
| CelesTrak OMM (JSON), plus `stations`, recent launches and ISS SupGP | **Pull, replacing TLE** | Finding 1 |
| ASTAP D80 (about 1 GB) | **Pull, before the next clear night** | Finding 2. Also meets Phase 94.5's return condition ("a catalog already on the rig"), so 94.5 would never need Gaia |
| Horizons moon vectors, cached at dusk | **Pull, per Jupiter night** | Finding 3 |
| 7Timer seeing, Open-Meteo 200/250 hPa wind | **Pull, in Leoben** | Finding 4 |
| meteoblue seeing | Hold | Only if a season of the log shows the free sources have no skill here |
| IERS Bulletin A / UT1 | No | Finding 5 |
| JPL DE440 kernels | No | Horizons online, Meeus offline, image lock either way |
| MPC orbits | No | Horizons already serves comets (Phase 121) |
| NOAA SWPC | No | Returns only if the Sun is imaged in H-alpha |
| Gaia DR3 or DR4 in bulk | No | DR4 is due 2026-12-02 (sourced); it reaches this app only through ASTAP's catalogs |
| MAST, Pan-STARRS, VizieR, more SIMBAD | No | Deep sky ranks below every bright target by the owner's direction; row 43 would use ASTAP and OpenNGC, already here |
| ESO, HEASARC, NRAO/ALMA, LAMBDA, NED, Exoplanet Archive, SDSS, NOIRLab, CADC, simulation archives, ADS | Never | Excellent archives answering questions nobody asks while recording Jupiter |

## Sources

- CelesTrak, GP data formats: https://celestrak.org/NORAD/documentation/gp-data-formats.php
- CelesTrak, SupGP: https://celestrak.org/NORAD/elements/supplemental/
- Space-Track documentation: https://www.space-track.org/documentation
- ASTAP star databases: https://sourceforge.net/projects/astap-program/files/star_databases/
- ASTAP: https://www.hnsky.org/astap.htm
- JPL Horizons manual: https://ssd.jpl.nasa.gov/horizons/manual.html
- Sky & Telescope GRS transit tool: https://skyandtelescope.org/observing/celestial-objects-to-watch/transit-times-of-jupiters-great-red-spot/
- Project Pluto GRS longitude: https://mail.projectpluto.com/grs_lon.txt
- BAA Jupiter Guide 2026: https://britastro.org/wp-content/uploads/2026/06/Jupiter_Guide-for-website_2026_v4Figs.pdf
- EarthSky, Jupiter at opposition: https://earthsky.org/astronomy-essentials/jupiter-at-opposition/
- meteoblue seeing: https://content.meteoblue.com/en/private-customers/website-help/outdoor-and-sports/astronomy-seeing
- meteoblue API pricing: https://business.meteoblue.com/pricing
- Open-Meteo docs: https://open-meteo.com/en/docs
- IERS Bulletin C 72: https://datacenter.iers.org/data/16/bulletinc-072.txt
- ESA Gaia news 2026: https://www.cosmos.esa.int/web/gaia/news-2026
