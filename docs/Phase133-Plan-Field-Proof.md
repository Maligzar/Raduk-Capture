# Phase 133 — field proof for the 2026-11-07 night

Asked for by the owner on 2026-11-08: *"have all of the bugs from last night
been fixed? if not present roadmaps to implement next phase"*, then *"ok go
with this plan"*. It runs ahead of 132.3e.

## What the night found, and where each fix stands

All of these were fixed in #105 (`cea8cdb`, merged 2026-11-08). They differ in
how far each fix has been proven. Levels: **sky** means seen working on the
8SE; **rig** means measured on the mount with no sky needed; **bench** means
tests or the emulator only.

| # | Found (2026-11-07/08) | Fix in #105 | Proven |
|---|---|---|---|
| F1 | Centring and guiding ran away. A NexStar pulse replaces the axis's tracking, and rate 1 is slower than tracking | App-driven rate guiding (`CentroidTracker`) | **Sky**: Saturn locked at 01:11, held 1-2″ |
| F2 | The centroid sat on a hot pixel, about 600 px off at gain 353 | `TargetFinder.h`, which measures area flux against a local background | **Sky**, through the F1 lock. Also bench (`target_finder`, `hot-pixel-search.scn`) |
| F3 | Fast jogging locked the hand control | One motion sender that waits for each `#` | **Rig** |
| F4 | Any rate-0 stop turned that axis's tracking off; disconnect sent an emergency stop | Tracking re-asserted after each stop; no emergency stop on close | **Rig**: hand-back worked |
| F5 | The lock ended when the box changed size (01:21) | `NotifyFrameChanged`; the guider holds its rates and re-references | Bench (`rate_guide`) |
| F6 | The lock ended after a hand jog (01:37) | Re-reference when the jog ends | Bench (`rate_guide`) |
| F7 | Calibration started 348 px from a frame edge (01:42) | Refused within 20% of an edge | Code only |
| F8 | Calibration moves were 200 px in a 640×480 box | Moves capped at 15% of the box | Bench (`rate_guide`) |
| F9 | **Autofocus**: the capped score plateaued, then Laplacian noise, then a fading target | `FocusMetric.h` (1000/HFD), a re-sweep when the curve is not bracketed, out-and-back passes, and a re-measure before reverting | Bench (`autofocus_saturn`, `focus_metric`) and one real recording. **No sweep on the sky** |
| F10 | Crash after dragging a box; crash on every close | Frame-size guard; atexit no longer calls a freed mount | Not stated as re-checked |
| F11 | The wrong target name; Uranus captured on Saturn's settings | `CurrentTargetName()`; Mercury, Uranus and Neptune profiles | Bench |
| F12 | **Mount pointing about 8° off** | **None.** The code stops trusting the mount's pointing for naming | **Open** |
| F13 | Saturn faded 255 → 154 in 90 s (dew or haze) | Autofocus tolerates a fade | **Cause open** |

## Stages

| Stage | What | Needs | Size |
|---|---|---|---|
| **133.0** | Record the above in `PHASE-STATUS.md`, the clear-night checklist (items 47-53) and the roadmap | Bench | S |
| **133.1** | Make the emulated mount behave like the measured NexStar: a pulse replaces tracking, rate 1 is slower than tracking, one axis is mirrored, and the hand control's tracking overwrites a variable-rate command. Then add scenarios for F5, F6 and F7 during a lock | Bench | M |
| **133.2** | Fade warning: when the target's flux drops steadily during autofocus or a recording, say so, and name dew when the weather feed puts dew risk up | Bench | S |
| **133.3** | One clear night, about 60-90 minutes, worked through checklist items 47-53 in order | Sky | — |
| **133.4** | Pointing: re-align, measure the error, then decide whether phase 123's pointing map should be applied (`PointingMapApply`) | Sky, and the owner's decision | S |

Order: 133.0 first; 133.1 and 133.2 are bench work and can go in either order.
Both should land before the next clear night, so that 133.3 is a check list
and not a debugging session.

## Not doing

- No change to the guiding algorithm itself before it has been seen failing
  on the sky. F5-F8 are bench-only, but the bench reproduces what the rig
  measured, which is what 133.1 makes stronger.
- No autofocus redesign. F9's fix is plausible and tested; it needs a sky
  sweep, not more code.

## As built

### 133.0, 2026-11-08

- `PHASE-STATUS.md` has a row for phase 133 giving each F-item's proof level.
- `CLEAR-NIGHT-CHECKLIST.md` items 47-53 are the 133.3 night, in running order.
- `ROADMAP-Next.md` row 33 ranks this ahead of 132.3e, at the owner's word.
