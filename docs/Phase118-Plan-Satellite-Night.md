# Phase 118 — a night of satellites

Raised by the owner: a planet is a slew and a stay, but a satellite is gone in
six minutes, so a session is an imaging plan. It starts from now, with targets
lined up for the scope to go to one after another through the night.

## What the tree decided

- **The gap matters more than overlap.** The tube ends a pass wherever that pass
  ended, and has to reach the next start and settle. Two passes that do not
  overlap can still be impossible back to back, so `SatPassInfo` now records
  where each window opens and closes.
- **Whether one pass can follow another depends only on those two,** so the
  best queue has an exact answer. A greedy pick can take a low pass and lose the
  fine one ninety seconds later.
- **The Passes tab's rows are already the raw material,** so a tick box on each
  is the plan.

## Design

- **`src/core/SatPlan.h`, pure.** `BuildSatPlan` picks the best queue. Score is 1
  for being in, plus height, plus 0.5 for being lit, less time beyond the
  mount. Changeover is the longer axis over `SatSlewDegPerSec` (2.4), plus
  `SatSettleSec` (15), plus `SatBufferSec` (20). The speed is a guess until
  measured, so every pass logs the slew it really made.
- **`SatPlanRunner`, pure.** Starts each pass about 45 s before it has to, so the
  tube waits rather than lands ringing. A pass gone by the time it is reached
  is skipped, and the next taken.
- **PLAN THE NIGHT** ticks the best set from now. **RUN THE PLAN** runs whatever
  is ticked, after checking an edited queue for pairs that cannot work.
- **Captures are filed under each satellite's name.**

**What a failure means.** A pass that could not be caught costs that pass. A
mount that ran backwards, fell behind, stopped answering or left the altitude
band is a fault in the telescope, so the plan stops: the next pass would meet
it unattended. `SatTracker::TelescopeFault` draws that line. Escape, a jog, the
stop button or disconnecting stop the plan, not just the pass.

## What gates it

`tests/satellite_plan.cpp`: hand-built schedules with known answers, a real day
of ISS passes, the full sequence, and every way a night can end.

## Owed

Checklist 41. No plan has run in the app, against the emulator or the sky. The
list shows the next 24 hours, and elements are not refreshed during a run. The
same exposure applies to every target, since the plan does not set the camera.
The cord check is per pass, so it can refuse a late pass and the plan goes on
without it.
