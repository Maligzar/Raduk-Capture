# Phase 107 — ROI following

The first rung of the ladder FireCapture calls AutoAlign: keep the target
on a small window by moving the window, and let the mount take the slow
part. It comes first because surface detail wants a tight ROI at a high
frame rate, which is when a target walks out of frame, and because every
fast target the owner wants later, satellites, rockets and near-Earth
objects, needs a correction with no gears in it.

## What the tree decided

- Nothing moves the ROI during a capture. `CameraEmulator::SetStartPos`
  was a no-op, so the emulator had no sensor window to move.
- `FrameAnalyzer` runs at a few hertz and drops most frames. That is too
  slow to follow anything fast, so following measures for itself on the
  capture thread, which is the only place that sees every frame.
- The tracker corrects from where the target sits in the FRAME. If the
  window follows, the target always looks centred and the mount would
  stop correcting until the window hit the sensor's edge.

## Design

- **`src/core/RoiFollower.h`, pure.** A subsampled centroid and a state
  machine, testable with no camera.
- **The mount offloads.** The tracker is fed the centroid plus the
  window's offset from home, so it still sees the true sensor error and
  walks the window back to where it started. Nothing in the tracker
  changes.
- **It only runs while `LOCKED`, on a crop.** Calibration measures how the
  image moves, so a moving window would corrupt it. A full-sensor frame
  has nowhere to go.
- **Steps of 8 across and 2 down,** as the ROI placement already aligns,
  which keeps the Bayer pattern.
- **A move has to be seen to land.** The camera gives no per-frame origin,
  so frames after a move may still be from the old window. Following waits
  until the target jumps by the amount it moved. That also measures the
  real latency, and logs it.
- **It gives up rather than fights.** Three moves in a row that never land
  mean the camera is not applying them, and following turns itself off
  and says so.
- **One odd frame does not move the window.** A jump of half a frame needs
  three frames in agreement. A lost target holds position.

## What gates it

`tests/roi_follower.cpp`: the centroid at both bit depths, a simulated
drifting target with a window that lags as a camera does, the mount's
view, sensor edges, alignment, outliers, lost targets, and giving up.

## Owed

- The real camera: does `ASISetStartPos` apply during capture, how many
  frames does it take, and does the USB call delay a frame? The log line
  `[FOLLOW]` answers the first two.
- Satellites, rockets and NEOs are separate phases: pass prediction and
  high-rate mount control, for which this is the prerequisite.
