# Phase 122 — tracking from the image alone

Phase 121 left rockets: a rocket in flight has no prediction for JPL or SGP4 to
supply. This flies whatever is in the frame from the image alone. It also covers
anything whose prediction has gone wrong. The operator brings it into frame and
presses TRACK WHAT IS IN FRAME on the Passes tab.

## What the tree decided

- **The tracker only needs a sky position and rates,** as phase 121 found. So the
  image becomes the source, and every stop the tracker has still applies.
- **Phase 51's calibration** already turns pixels into axis moves, rotation and
  mirror included. Mount plus offset is where the object is.
- **The frame tap knew where but not when.** It now keeps the time each frame
  arrived, because the answer depends on it (below).

## Design

- **`src/core/MotionLock.h`, pure.** `PixelOffsetToAxisDeg` gives the object's
  position, and an alpha-beta-gamma filter on each axis gives its motion. A rocket's
  apparent rate grows, and a filter with no acceleration term lags it off the
  frame. It coasts for 2 s when the image goes, then stops.
- **The mount as it pointed when the frame was taken,** not now. The first
  version added each centroid to the mount's current reading, which put the object
  behind itself by the sky rate times the delay: 0.1 degrees at two ticks, off the
  sensor. Readings are now kept, and each fix is paired with its own moment.
- **It primes before moving anything.** If three fixes do not arrive within 1.5 s,
  nothing has moved and the log says so.
- `SatTracker::Config::skipSlew` starts it holding, since there is nowhere to slew.
- It needs tonight's guide calibration and refuses without one. Runs stop after
  15 minutes.

## A defect in phase 116, found here

`SatDesk` aimed the tracker 0.1 s ahead to cover command latency. That one point
was used for both the position and the rate, so the error was measured against
the future and the correction held the mount 0.1 s in front of the target. That is
0.08 degrees on the ISS, over half the frame height. Phase 116's simulation
measured its error against the same shifted point, so it never saw it. Position
now comes from now and the rate from 0.1 s ahead, in the app and both simulations.

## What gates it

`tests/motion_lock.cpp` uses a simulated rocket that climbs and speeds up to about
0.8 degrees a second, a rotated and mirrored camera, and 3 px of seeing noise on
every centroid.

| Case | Result |
|---|---|
| Settled, one to three ticks late | Within 0.002 degrees |
| Opening, three ticks late | Stays on the sensor |
| A second behind cloud | Coasted through |
| Gone for good | Stops the mount |

The filter gains were the only set tried that kept the three-tick opening on the
sensor. `satellite_passes` now holds 1 arcsecond against where the satellite
really is.

## Owed

Checklist 43, and items 39 and 40 before it. The frame tap times a frame by when
it reached the app, not when it was exposed. The difference is a fixed latency the
filter absorbs as lag. FastCentroid follows the brightest thing in frame.
