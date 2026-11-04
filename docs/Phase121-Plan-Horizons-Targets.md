# Phase 121 — following anything JPL Horizons predicts

Next on the roadmap was "rockets and near-Earth objects". Rather than a
prediction for each kind of object, this follows anything JPL Horizons can
predict: near-Earth asteroids on close approach, comets and spacecraft. It uses
phase 116's tracker, 117's image lock and every stop they have. Rockets with no
published trajectory need tracking from the image alone, which is a later phase.

## What the tree decided

- **The tracker never needed SGP4,** only an alt-az, rates and a window. So
  `SatDesk`'s flying thread now takes a `TrackSource`, and satellites became one
  kind of source.
- **The site stays here.** Horizons will compute the view from a site, but that
  means sending the site. The app asks for positions seen from the centre of the
  Earth and applies parallax itself. For a close approach that is not a detail:
  at 38,000 km the site moves Apophis by 8 degrees.
- **Vectors carry velocities,** so a cubic between one-minute rows is exact for
  anything smooth over a minute.

## Design

- **`src/sky/Ephemeris.h`, pure.** It parses Horizons' vector table and
  interpolates it. It converts ICRF to the equator of date and applies the site,
  and finds a tracking window measured as a pass is. When JPL cannot find a name,
  its own message is passed on.
- **On the Passes tab,** type a target (`99942;`, `C/2023 A3`, a spacecraft ID)
  and press FOLLOW. The table covers 12 hours from now. The window, preflight,
  slew and flight are those of a satellite pass. FOLLOW stops a flight in
  progress, as TRACK does.

## What gates it

`tests/horizons_targets.cpp` checks against JPL's own view from fixed sites. The
app never asks JPL for that view, so one row tests the parse, the interpolation,
the frame, the parallax and the horizon together.

| Check | Against | Result |
|---|---|---|
| Apophis on its 2029 close approach, Mauna Kea | Horizons' view from the site | Within 0.004 degrees |
| ISS, Palomar | The same | 0.06 degrees: about 0.07 s of timing, as the vector table is UTC and the site table JPL's UT |
| ISS rows rebuilt from two-minute steps | The missing rows | 6 m |
| The request | Its own text | Asks for the geocentre, so the site is never sent |

The app's exact request was also sent to the live API on 2026-11-03, for Apophis
and for C/2023 A3, and both came back as tables the parser reads.

## Owed

Checklist 42. Nothing has been followed in the app or on the sky. Nutation and
annual aberration are left out, which is about 20 arcseconds, comfortably inside
a 13' x 7' frame, but it is there. A FOLLOW is not part of a night plan.
