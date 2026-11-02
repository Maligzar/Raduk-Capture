# Phase 117 — steering a satellite pass from the image

Phase 116 flies on prediction, which cannot hold a 13' x 7' frame. This puts
the sensor in the loop, and adds a hand control to get the satellite onto the
sensor in the first place.

## What the tree decided

- **The guide calibration already knows the camera.** Phase 51 measures the
  image's response to an East and a North pulse, rotation and mirror included.
  On NexStar those are axis 0 and 1 in direction 0, the same as a positive
  azimuth and altitude rate here.
- **An alt-az camera does not rotate against its axes,** so the map holds through
  the pass, with only the cos(altitude) `GuideSolve.h` already rescales.
- **`FastCentroid` is already cheap enough** for the capture thread, at a stride
  and ten times a second.
- **The image cannot find what is not on the sensor.** Most of the prediction's
  error is along track, so one number, a time offset, puts it right.

## Design

- **`src/core/SatImageLock.h`.** `SatFrameTap` takes a centroid off the capture
  thread. `SatImageLock` turns it into a bias on the aim. `SatTracker` is
  unchanged, so all its stops still apply to what is left.
- **A correction, never a controller.** The bias is clamped to a degree, so a
  bad centroid can lose the satellite but cannot run the mount away.
- **A centroid must be agreed by the next one** before it is believed.
- **Gain 0.12.** The loop is an integrator with delay in it. The test saw 0.5
  diverge at three ticks late and 0.2 at seven. 0.12 takes about eight seconds
  to centre.
- **`Ctrl+[` and `Ctrl+]`** slide the prediction 0.1 s along its track while a
  pass flies.
- **No calibration, no lock:** the pass flies on prediction and says so. The
  calibration is read just before `tracker.Reset()` at the start of a pass.

## What gates it

`tests/satellite_image_lock.cpp` runs the loop against a simulated sensor at six
camera rotations, mirrored and not, with one to seven ticks of delay, and with
a calibration from another altitude. It also holds the refusals: no or
degenerate calibration, a one-frame jump, a stuck centroid, the deadband.

## Owed

Checklist 40. Whether `FastCentroid` finds a real satellite (it takes the
brightest thing, so the Moon or a star in frame would win), whether the
calibration holds at these rates, and the real delay. The gain's margin depends
on that delay. The keys have not been pressed in the app.
