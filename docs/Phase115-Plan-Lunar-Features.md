# Phase 115 — lunar feature targeting

The frame is 13' x 7' and the Moon is 31' across, so on this rig the useful
target is a crater, not the Moon. This lets the GoTo panel slew to a named
feature and says whether that feature is lit.

## What the tree decided

- **Every lunar slew until now was wrong by up to a degree, and that was the
  real finding.** `MoonRADec` answered for the centre of the Earth, and nothing
  in `src/` applied parallax. JPL Horizons puts the geocentric figure 26' to
  51' from where the Moon is seen at 33N, altitude 18 to 63 degrees. The Moon's
  radius is 15.5', so a "Moon" GoTo could finish off the disc. A mosaic centred
  on that figure had the same error. Both now use the site.
- **The series was too short for this job.** Ten longitude terms, labelled good
  to 0.1 degrees, which is half a frame height. Replaced by all of Meeus Ch.47,
  on dynamical time.
- **`PrecessDateToJ2000` stays for the planets.** The Moon's path has its own
  full precession rotation.
- **No plate solve can correct this.** A frame on the Moon holds no stars, so
  a crater GoTo rests on the mount's alignment. That is the main thing owed.

## Design

- **`src/sky/LunarGeometry.h`, pure.** The Moon's body axes come from Cassini's
  laws as one rotation. The observer, the Sun and the feature are vectors in
  that frame, so topocentric libration, the sub-solar point and the pole's
  angle are not separate formulas that can disagree. Physical libration is left
  out: at most 0.5" here.
- **`src/sky/LunarFeatures.h`.** 99 features, generated from the IAU Gazetteer
  table rather than typed.
- **In the catalogue as `Moon - Copernicus`, type `Lunar`.** The name leading
  with `Moon` gives the lunar tracking rate and the Moon profile with no new
  code, and search finds the crater. Magnitude 99 keeps them out of the sky
  map, Tonight and the unfiltered list.
- **`GoToPanel::RefreshSky`** gives the catalogue the site each time, because
  the site can arrive after startup.
- **The slew is reported, not refused.** One log line gives the size, the Sun's
  height and whether the relief is good, or the time to sunrise, or that
  libration has hidden it. The terminator may be an hour away, and waiting is
  the operator's call.
- **`Ctrl+Shift+M` on a feature mosaics the whole Moon,** which is what the
  comment in `main.cpp` already promised and the code did not do.

## What gates it

`tests/lunar_features.cpp`:

| Check | Against | Result |
|---|---|---|
| Series sums | Meeus 47.a | Within 0.5 of 1e-6 degree units |
| Optical libration, pole, sub-solar point | Meeus 53.a | 0.05 degrees |
| Geocentric position, ten dates to 2031 | Horizons | Worst 20" |
| Topocentric position, seven altitudes, Palomar | Horizons | Worst 23" |
| Libration | Horizons | Worst 0.04 degrees |
| Sub-solar point | Horizons | Worst 0.05 degrees |
| Pole angle | Horizons | Worst 0.17 degrees |

The 23" is periodic and mostly in RA: what truncation leaves in the published
series, not an offset. It is 113 px at the full sensor and a sixth of the width
of a 640x480 ROI. Surface points are held to the geometry that must be true:
the sub-Earth point at the disc centre, Crisium toward the sky's west, Grimaldi
toward its east, a predicted sunrise checked by moving to that time.

## Owed

Whether a crater lands in the frame on the real mount, whether Moon GoTo still
looks right now that it is corrected, and whether the 20 and 50 degree relief
bands suit real frames. The emulator's Moon is procedural and its craters are
not the real ones, so nothing indoors can show a named crater. Checklist 38.
Lunar feature mosaics, an atlas view and sync on a recognised crater are not
built. A sky map click goes through the same corrected position, but the map's
own drawn Moon stays geocentric, which is invisible at its scale.
