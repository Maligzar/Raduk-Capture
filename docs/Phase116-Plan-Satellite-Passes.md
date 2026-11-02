# Phase 116 — satellite passes and high-rate mount control

The first target that moves faster than the mount's tracking can follow. The
GoTo panel gets a Passes tab: the next 24 hours of passes from this site, and
a button that flies one.

## What the tree decided

- **The mount can do it.** `SlewVariable` is on the wire to about 4.5 deg/s and
  the top slew is documented at 3. The ISS peaks near 1 deg/s on the sky.
- **The azimuth axis cannot always.** It runs at sky rate / cos(altitude). A
  pass through 89 degrees asks for 75 deg/s. So the window is cut at a ceiling,
  and any time beyond the mount's limit is counted and reported.
- **`GoToPanel::Tick` runs every two seconds,** and a serial round trip must
  never block the render loop, so the loop has its own 5 Hz thread.
- **`CurrentJD` has one-second steps.** That is up to a degree of ISS, so
  there is a millisecond clock, `PreciseJdNow`.
- **`GoToRADec` from the thread skips `m_preSlew`,** so the cord and the
  keep-out sector are checked once, against the whole pass, before it starts.
- **Near-Earth SGP4 only.** High orbits need SDP4, and GoTo can follow them.

## Design

- **`src/sky/Sgp4.h`, pure.** A port of the Vallado 2006 revision of Spacetrack
  Report #3, because TLEs are fitted through this model and mean nothing to any
  other.
- **`src/sky/SatPass.h`, pure.** Alt-az, sunlight by a cylinder of shadow,
  passes found by 20 s steps and bisection, then walked a second at a time for
  peak rates, azimuth travel and time beyond the mount.
- **`src/core/SatTracker.h`, pure.** Feed-forward rate plus 0.8/s of the error,
  clamped to 3 deg/s and 2 deg/s^2. Slew to the window, hold, close in, and
  record once within 0.25 degrees for a second.
- **`src/core/SatDesk.h`.** Elements file, fetch, prediction on a worker, and the
  flying thread. Its last act is always to stop both axes and restore tracking.
- **Elements** come from CelesTrak's `visual` group, on the button, no more than
  once in two hours as they ask, and only ever replace a good file with a good
  file. The request says nothing about the site.

**What stops a pass:** the window's end; the mount's own altitude leaving the
band by 3 degrees; 15 degrees of error, which is the guard for an axis running
backwards; 0.75 degrees of error for 3 s at a rate the mount has; 2 s with no
position; Escape, a jog, the stop button, a disconnect, closing the app.

Settings: `SatMinAltDeg` 15, `SatMaxAltDeg` 80, `SatMaxRateDeg` 3. All three are
starting figures. `IMount::DriveAltAzAxes` tells the emulator to treat its axes as
azimuth and altitude while a pass is flown, as the real fork always does.

## What gates it

`tests/satellite_passes.cpp`:

| Check | Against | Result |
|---|---|---|
| SGP4, 9 near-Earth cases, 54 rows | Vallado's `tcppver.out` | Below 1 mm |
| A whole ISS pass over Palomar | JPL Horizons | Within 0.11 degrees, 0.6 km |
| Flying that pass | Simulated mount with rate and acceleration limits | 4" mean, 0.03 degrees worst |
| Reversed axis, slipping axis, silent mount, low mount, abort, stuck slew | Simulated faults | All stop, with no rate after the stop |

Building it found two bugs: the TLE epoch was a month out, caught by the JPL
comparison, and a half-slipping axis ran a whole pass inside the first lost
limit of 2 degrees. `tests/manual-sat-track.cpp` flies real passes in real
time through the thread and the emulated mount.

## Owed

Everything about the real mount. Checklist 39, and read it before the first
run. In order of worry:

1. **Axis direction at speed.** Believed from the protocol, and untested here.
   The 15 degree guard is what stands behind that.
2. **`z` round-trip time,** which sets the loop's real latency.
3. **Backlash and gear noise** at these rates on an 8SE.
4. **The ceiling,** which is a guess at where the camera meets the base.
5. **Hitting the frame at all.** The frame is 13' x 7'. A day-old element set
   is 4' to 8' out and a second of PC clock is 28' at 500 km, so open-loop
   pointing may well miss.

Closing the loop on the image is phase 117. Rockets need their own prediction,
and SDP4 is not built.
