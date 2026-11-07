# Phase 128 — near-Earth objects on the sky map

Asked for by the owner: the map should show the NEOs. Both sky maps now draw the
near-Earth objects worth a look this week. Clicking one follows it through JPL
Horizons, the way a typed target does (phase 121).

## Which objects

JPL's **Close-Approach Data** (`ssd-api.jpl.nasa.gov/cad.api`): objects passing
within **0.05 au** of the Earth, about 19 lunar distances, from 7 days ago to 30
ahead. The 20 nearest in time get a position table. Of the roughly 35,000 known
NEOs, this is the list that changes, and the one a night is likely to want. On
the first run it returned 74 approaches.

## Where and how bright

- **Position:** one Horizons vector table per object, geocentric, 10-minute
  rows over the next day. Phase 121's interpolation puts it at your site
  locally, so **the site never leaves the PC**. That matters most here: at a
  tenth of a lunar distance, the parallax is about a degree.
- **Brightness:** the IAU H-G system with G = 0.15, using the object's H from
  the CAD row, its distances from the Sun and the Earth, and the phase angle.
  The Sun's position comes from one more Horizons table, so every distance is
  in the same frame.
- **Motion:** measured against the stars, not the horizon. The sky's own turn
  is the same for every object on the map and says nothing about any one of
  them.

`src/sky/NeoList.h` holds this and is pure. `src/core/NeoDesk.h` fetches off the
UI thread, at most every six hours, and logs what it got. On the first run:
"74 close approaches within 0.05 au from 7 days ago to 30 ahead; tables for 20
of the nearest 20".

## On the map

- **The marker:** an orange diamond, with the object's name and predicted
  magnitude. A line runs to where it will be in an hour. Below the horizon it
  is faded.
- **Clicking it:** the popup shows magnitude, rate (degrees an hour), distance
  and altitude, and a **FOLLOW** button in place of SLEW. FOLLOW is offered even
  when the object is low, because following waits for its window.
- **The cut:** objects fainter than `NeoMagLimit` (rig.ini, default 16) are not
  drawn. 16 is a guess at what the 8SE and the 678MC show in a few seconds of
  video. On the first run, two of the 20 cleared it: 2001 KF54 at 15.6 and
  2026 VG3 at 15.7.

## Found on the way, and fixed

**A sequence refused at its first check ended in silence.** Pressing RUNS on a
target below the 20° floor started the sequence, which failed its first check
and ended before it had ever been shown. The end message only fired for a
sequence already on screen, so RUNS did nothing and said nothing. Now it ends
with the reason, for example "Sequence: 0 of 10 runs - the target went below
the altitude floor". `runs-refused.scn` checks it.

The full suite caught this when Jupiter rose to just under the floor during
testing. `autorun.scn` and `seeing-hold.scn` now use Kochab, which never sets at
this site, with the floor lowered, and a new `press LOAD PROFILE <name>` loads
the star profile, which a GoTo does not do for a named star.

Also fixed: map labels were drawn in an 80 px box, so a longer label wrapped
onto a second line and ran into the next. The box is now 220 px; short labels
look exactly as before.

## What gates it

- **`tests/neo_list.cpp`:**
  - the CAD list is parsed from JPL's own JSON shape, by column name, with a
    missing H read as 99, not 0;
  - the Horizons command is right for a numbered object and for a provisional
    designation;
  - magnitudes are checked against known geometry;
  - an object opposite the Sun lands at the right RA, Dec and distance, and an
    object moving across the line of sight moves at the right rate.
- **`tests/scenarios/neo-map.scn`:** against the real services, it fetches,
  draws, screenshots the map, and follows the first NEO the way the map's
  FOLLOW does. The first run found 2026 VZ3 in the tracking band right away.

## Owed

A real NEO through the 8SE. The magnitude cut, and whether a fast close one can
be held, are for the sky: the image lock (117) and image-only tracking (122)
are what would hold it.
