# Phase 113 — Autorun

No roadmap row named this. It was chosen on 2026-11-01 from the owner's
direction: surface detail comes from many clean runs across a night, kept
sharp and kept inside the derotation window, and until now each of those
runs was a press of RECORD. FireCapture's Autorun is the established form
of the idea. Satellites and rockets stay behind it, because they need
pass prediction and high-rate mount control, which is a larger phase.

## What the tree decided

- Every stop lives in `SubmitFrame`: the frame limit, the time limit and
  the derotation stop are all checked when a frame ARRIVES. A dead camera
  never reaches them, so a run that lost its camera would never end.
  The runner needs its own timeout.
- The gate skips a frame after the limits are checked, so a time limit
  still fires under cloud and a frame limit does not.
- `doRecord` already works out the window, the format and the WinJUPOS
  rename for one run, so Autorun presses it and adds nothing beside it.
- `startAutofocus` already does AE, settling and the sweep (phase 112),
  and AE already holds for recordings, so the gap between runs is when AE
  corrects. Autorun only says when.
- `SetOnComplete` launches AutoStakkert on any normal stop. Between runs
  that is minutes of CPU in the seconds before the next recording.
- `startAutofocus` returns nothing and can refuse, and the focuser can go
  away mid-sequence. A refusal looks like a sweep that finished at once,
  which does no harm: the run just goes ahead on the focus it has.

## Design

- **`src/core/AutorunRunner.h`, pure,** as `MosaicRunner` is. `Tick()`
  returns an action and the caller performs it, so every path runs in a
  test with a fake clock.
- **The sequence:** check, focus if due, record, gap, repeat. Focus comes
  before the first run and then every `RefocusEvery` runs.
- **The check runs before every run,** and it is the point of the phase,
  because nobody is watching. Target in frame, target above the altitude
  floor, room on the disk. A lost target is waited on for two minutes and
  then ends the sequence. Falling below the floor is a clean finish. So is
  reaching the count.
- **Run length.** The derotation window when the target has one, whatever
  `[DEROT]` says, because a run past the window is the thing this exists
  to prevent. Otherwise the panel's limit. Frame limits become seconds
  from the frame rate, for the timeout and the disk check only.
- **A run that overstays ends the sequence.** The timeout is the expected
  length, half again, plus a minute. It closes the file first.
- **Two empty runs in a row end it.** That is cloud, or a closed gate, and
  a third would only make a third empty file.
- **Stopping.** `[RUNS]` again, or RECORD, closes the file and stops. A
  stop that was not normal, such as Escape or an ROI change, stops the
  sequence too. It never starts over a manual recording, or a mosaic.
- **Auto-stack is passed over during a sequence,** and the end message
  says how many runs there are to stack.
- **A focus that fails does not stop the sequence.** The sweep restores
  where it started, the frames are still worth having, and the count
  appears in the final summary.

Settings in `rig.ini`: `AutorunRuns` 10, `AutorunGapSec` 15,
`AutorunRefocusEvery` 3, `AutorunMinAltDeg` 20. These are starting
figures, not measured ones. Nothing moves the mount, so one toolbar press
starts it, where the mosaic wanted a chord.

## What gates it

`tests/autorun.cpp`: a clean sequence, the refocus cadence, each preflight
refusal, the hold and its timeout, a refused start, the overrun, empty
runs, every kind of stop, the arithmetic for run length and disk, and the
degenerate configs.

## Owed

The wiring in `main.cpp` has no test and has not been run. One sequence in
the emulator, then one on a planet (checklist 36).
