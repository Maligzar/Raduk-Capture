# Phase 130 — satellites and rocket bodies on the sky map

Asked for after phase 128: "when running the emulator I am not seeing any of
the neos (satellites, rockets etc) on the sky map".

## What was found

- **Satellites had never been on the map.** Phase 128 added near-Earth
  asteroids only. The bright satellites were in the GoTo panel's pass list
  and nowhere else.
- **The NEOs were working, but filtered.** The user's run fetched 76 close
  approaches and kept tables for the nearest 20. Only one was brighter than
  `NeoMagLimit` (16 by default); most close passers are magnitude 18 to 22.
  The map gave no hint that a limit was hiding the rest.

## What changed

- Every satellite and rocket body in `satellites.tle` (CelesTrak's visual
  group, about 156, 91 of them rocket bodies) is positioned once a second
  while a map is open. Those above the horizon are drawn:
  - a dot for a satellite, a small square for a rocket body (`R/B`);
  - a line to where it will be in 30 s;
  - bright when it can be seen (sunlit, with the Sun at least 6° below the
    observer's horizon), dim otherwise.
- Clicking one shows what it is, how far away it is and its altitude, with
  no SLEW: it is gone from that spot in seconds. Tracking stays with the
  pass list, which knows the windows. Satellites in shadow never take a
  click, and a lit one gives way to anything fixed nearby.
- A count line above the status bar: satellites up and how many can be
  seen, and NEOs shown, the magnitude limit, and how many came back.
- The elements are read again whenever the GoTo panel rewrites the file.

## Names and a clock (asked for during review)

"expose the names of the NEOs all the time, just like we do with the
planets, this way a passerby looking at the panels can see the names".

- Every NEO is named with its magnitude: bright above the horizon,
  faded below it, like its diamond.
- Every satellite and rocket body is named: light blue when it can be
  seen, grey when it cannot.
- The OBSERVING SITE panel starts with the day, date and the time to the
  second, local and UT. The map redraws every second, so the clock ticks
  while the satellites visibly move along their 30 s lines.

## Scenario

`satellite-map`: the elements load (156), positions are computed, the count
is logged, and the map is captured.

## Not done

- FOLLOW from the map for a satellite. The pass list's tracker needs the
  pass window, which the map does not compute.
- `NeoMagLimit` stays 16. Raise it in rig.ini to see fainter NEOs.
