# Phase 126 — locking on a Moon that fills the frame

Phase 125's first scenarios found that a Moon filling the frame could never be
locked. This phase fixes it, and fixes the emulator's Moon so the fix can be
tested indoors.

## Why it never locked

Acquisition finds the target, calibrates the guide axes by pulsing the mount and
measuring the response (phase 51), centres it, then guides. Every step measures
the target's position as the centre of the bright pixels. When the Moon fills the
crop, every pixel is bright, so that centre stays near the middle whatever the
mount does. Calibration logged "North response too small" on every attempt.

Surface following (phase 109) only starts once acquisition has locked, so on the
Moon it never started either.

## The fix

On a full frame, the analyzer reports where a patch of the surface has moved
instead of the centre of brightness. Phase 109's `SurfaceLock` already finds a
remembered patch again by correlation, so the analyzer now keeps its own copy.
The answer means what a centroid means: where the target has moved, starting
from the frame centre. So calibration, centring and the lock work unchanged;
on the Moon they now hold the surface where it was when the search began.

- **When it applies:** `FrameIsFilled`, phase 109's test. A quarter of the
  frame's edge must be brighter than half the peak.
- **Lost or featureless:** no position is reported, rather than the
  meaningless middle of the frame. The tracker's existing two-second loss rule
  then applies.
- **Back to a disc:** it resets, and the ordinary centroid returns.
- **The log** says once, per acquisition: "The target fills the frame:
  guiding on the surface itself".

## The emulator's Moon had nothing to hold

The first runs still failed. Screenshots of the preview, new in this phase,
showed why: three things made the emulated Moon a smooth grey gradient.

1. **Focus.** The Moon profile's focuser position is the real rig's, 11,700
   steps from the emulator's true focus. That blurs the image by hundreds of
   pixels. The scenario puts it in focus.
2. **Exposure.** At the profile's 2 ms the emulated Moon peaks at 8% of full
   scale, under the full-frame test's floor. The scenario uses
   `RADUK_EMU_PEAK_DN`, which exposes to the patch as an imager would.
3. **No fine detail.** The emulator had maria and 5-degree crater cells. A
   640x480 crop near the middle of the disc spans about 16 degrees, so there
   was almost nothing in it. `LunarFineTexture` adds value noise at 1, 0.3 and
   0.1 degrees, about ±15% in all, fixed to latitude and longitude so it
   moves with the surface.

Also new: `RADUK_EMU_LUNAR_PHASE`, so a scenario sees the same Moon on any date.
Near new Moon, the middle of the disc is dark.

## Screenshots now include the picture

A scenario's `shot` also saves `NAME_preview.png` from the renderer. The preview
is a swap chain that `PrintWindow` cannot read, which is why it came out black
in phase 125.

## What gates it

- **`tests/full_frame_lock.cpp`** feeds the real analyzer a synthetic surface:
  - a 12 px move reads as 12 px, each axis on its own;
  - a disc gets its ordinary centroid;
  - a new full frame starts fresh at the centre;
  - a featureless frame gives no position.
- **`tests/scenarios/surface-follow.scn`**, the phase 125 scenario that
  recorded the failure, now passes:
  - calibration succeeds on the first attempt, with the axes at right angles
    and a measured guide rate of 7.34"/s against the configured 7.50;
  - acquisition centres and locks, surface following engages, and the surface
    holds through 15 s of 5 px/s drift.

## Owed

Checklist 33 on the real Moon. The real surface has far more detail than the
emulator's, so the risk is the other way: a patch lost to seeing.
`SurfaceLock`'s `kMinScore` and `kMinDetail` are guesses until then. The Sun
needs the same, through a solar filter.
