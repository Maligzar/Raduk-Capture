# Tracking review — 2026-10-09

Written after the 2026-10-08/09 Saturn session. Two parts: a second
pass over what was changed that night, then a review of the tracking
chain against the goal —

> a planet visible on screen should be targetable, centreable and
> trackable, entirely from RadukCapture.

---

## Part 1 — second pass on the night's work

### Verified against hardware

| Change | Evidence |
|---|---|
| Serial port auto-detect | `Mount: OK (COM4 responded)` at startup; probe found COM4 only, pinned and stale-pin cases both correct |
| Site/clock push | `Site -> 33°51'36" N, 112°05'24" W : accepted`, `Clock -> ... UTC-7 dst=0 : accepted` |
| Sky map projection | user reports "close" after the fix, versus unusable before |

### Compile-verified only — still unproven on sky

Hotkey routing, profile SAVE capturing live state, sharpness crop
following the centroid, preview mouse routing (drag ROI, pixel
readout), loud ROI/LOCK refusals, autofocus progress-based timeout,
verified tracking readback, conditional site/clock push.

None of these are hard to test; they simply were not reached before
the session ended.

### Defects found in that night's own work, now fixed

1. **`PreviewProc` ignored `WM_CAPTURECHANGED`.** Capture stolen
   mid-drag (alt-tab, a popup) means `WM_LBUTTONUP` never arrives, so
   the renderer stayed in `DragState::Dragging` with a rubber band
   painted over the image. The handler existed in a draft and was lost
   when the patch was reapplied — it is back.

2. **DST was double-counted in the mount clock push.** The NexStar `H`
   command carries the *standard* timezone offset plus a separate DST
   flag; the mount adds the hour itself. `PushSiteAndTime` also added
   it, which would set the mount an hour fast in summer. Invisible at
   this site, since Arizona keeps standard time year round — precisely
   why it would have survived unnoticed until a trip elsewhere.

3. **Autofocus polled the EAF twice per 50 ms during moves.** The
   progress-based `WaitForMove` added a `GetPosition()` call beside the
   existing `IsMoving()`, doubling USB round-trips to the focuser while
   the camera streams. The USB traffic manager throttles routine EAF
   polling to 10 s during capture for exactly this reason. Position is
   now polled every 250 ms, which still beats the 5 s stall timeout
   comfortably.

### A pattern worth naming

Three separate faults that night had the same shape: **trusting an
acknowledgement instead of checking a result.**

- `SyncRADec`'s return was discarded and the UI reported "Aligned"
  unconditionally.
- `SetTrackingMode` returned true on the mount's `#` ACK, which means
  "command received", not "motors running" — so the UI announced
  tracking on a mount that never moved.
- The poll loop defaults its tracking-mode local to `OFF`, so a failed
  read is displayed identically to genuine OFF.

The first two are fixed. The third remains: `PollLoop` still conflates
a read failure with OFF. It should carry a validity flag the way
`m_cacheValid` already does for position.

---

## Part 2 — why tracking is hard to reach

### The dependency chain

Every stage depends on the one before, and the first dependency is not
owned by this application at all:

```
mount aligned (hand control)       <- not in our control
  -> mount accepts tracking
    -> drift falls from 15"/s to ~0
      -> centroid stays in frame
        -> 5 stable centroids -> LOCK enabled
          -> calibration measures pulse response
            -> Centering holds 8 frames within 10 px
              -> Locked, ROI crop
```

With tracking off, the chain cannot even start: drift of 15.04"/s is
74 px/s at 0.203"/px, so the 8-frame dwell inside 10 px is
arithmetically impossible (drift alone covers 13.8 px in 8 frames at
43 FPS), and calibration measures roughly four times more drift than
pulse response.

That is not a bug in the tracker. It is a precondition the UI never
states.

### The guide rate is assumed, not measured

`PulseGuide` uses NexStar fixed rate 1 and a comment asserting it is
0.5x sidereal (7.5"/s). `RigConfig::guideRateArcsecPerSec` repeats the
same 7.5 figure. Neither is measured.

The correction solve itself is immune: it works in the pixel domain
using the measured px/ms matrix, so a wrong rate assumption cancels
out. But `cal.arcsecPerPx` is computed as

```
arcsecPerPx = (guideRateArcsecPerSec * CAL_PULSE_MS / 1000) / eMag
```

so every arcsecond figure the user sees — the drift readout, the error
display — scales with an unverified constant.

### The known and the unknown are inverted

The plate scale is *known* from the optics: 206.265 x 2.0 um / 2032 mm
= 0.203"/px, and `RigConfig` already stores it. The guide rate is the
genuinely unknown quantity.

Calibration currently assumes the rate and derives the scale. It
should assume the scale and derive the rate — the same measurement,
read the other way round, grounded in a number that cannot drift.

### Calibration is too heavy to be a prerequisite

Four pulses at 800 ms plus 700 ms settle plus sample time is roughly
10-20 seconds of the planet being deliberately pushed around, and it
is gated behind LOCK, which is gated behind five stable centroids. For
"centre this planet", none of that is necessary.

Centring needs only a direction and a step size. A full 2x2 transform
is needed for *fine guiding*, not for getting a planet into the middle
of the frame.

### The variable-rate command is unused

The NexStar protocol has a variable-rate slew (`P`, length 3, command
6 or 7, rate in arcsec/s x 4 across two bytes) alongside the fixed
rates 1-9 the code uses. Driving corrections at an explicit arcsec/s
would remove the fixed-rate assumption entirely and make pulse sizing
exact rather than inferred. Worth verifying on the HC 1.22 firmware
before relying on it.

---

## Part 3 — proposed Phase 65: point, centre, track

### The flow this should be

1. A planet is visible in the preview.
2. The user clicks it — or the app simply takes the brightest centroid.
3. One button: **CENTRE & TRACK**.
4. The app centres it and starts guiding, narrating what it is doing.

No separate CENTROID / LOCK / ROI sequence with unstated preconditions.

### Centring without a calibration stage

The first corrective move does not need a measured transform. It needs
the plate scale (known) and a guess at orientation. So:

1. Convert the centroid's pixel offset to an angular offset using the
   nominal plate scale.
2. Move along one mount axis only, sized to cover part of that offset.
3. Measure the resulting pixel displacement. That *is* one column of
   the calibration matrix — obtained while making useful progress
   toward the centre rather than in a separate blocking stage.
4. Repeat on the other axis.
5. From there the transform is known; iterate to convergence.

Calibration becomes a by-product of centring instead of a gate in
front of it. If a fresh calibration already exists, steps 2-4 collapse
into using it directly.

A further fallback needing no orientation knowledge at all: pulse,
observe whether the error grew or shrank, reverse if it grew. Slower,
but it converges with zero prior knowledge and is a reasonable last
resort.

### State the precondition, loudly

Tracking must be on before any of this. The app can now verify that
honestly (Phase 64 reads the mode back), so CENTRE & TRACK should
check first and say plainly:

> Mount is not tracking. Align it on the hand control (Align ->
> Quick Align needs no stars), then try again.

That one message would have saved most of a night.

### Smaller items worth doing alongside

- **Give `PollLoop` a tracking-validity flag** so a read failure stops
  being displayed as OFF.
- **Derive the guide rate from calibration** using the known plate
  scale, and log it. A measured rate far from 7.5"/s would immediately
  explain any pulse-sizing error.
- **Show the acquisition stage and its blocking condition** in the UI
  continuously, not only when a button is pressed.
- **Re-use the sharpness crop logic for the centroid search**: both now
  want "where is the planet", and they currently answer it separately.

### What this does not fix

The mount must still be aligned by its hand control. Nothing in this
application can substitute for that, and the honest thing is to detect
it, say so, and point at Quick Align — which needs no bright stars and
therefore suits this site.
