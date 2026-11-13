# Phase 158 plan: focus through the night

From the strategic roadmap review the owner brought on 2026-11-13: focus stability monitoring, automatic refocus thresholds and a focus position history. Status: `PLANNED`. Part of release 3.5 ([`ROADMAP-Next.md`](ROADMAP-Next.md#next-in-order)).

## What exists
- Autofocus in two stages (145): a coarse HFD sweep, then a fine pass on the edge metric, with backlash handled (139).
- **What it lacks:** Autorun refocuses only by count: every `AutorunRefocusEvery` runs, default 3. It is put off around a Jupiter event (144). Nothing watches focus drift between sweeps, and no sweep is kept for later.

## Stages
- **158.0 A focus history.** Every sweep appends one line to `focus_history.csv` in the data root: time (UTC), target, start and final positions, the EAF's temperature, the coarse HFD and the fine pass's edge score at the result, the measured backlash, the outcome, and the seeing figure at the time. It is never rewritten, only appended. `[STATS]` gains a focus panel: position against temperature over the night.
- **158.1 A refocus when it is needed.** Between sweeps, the target's edge score, at a fixed exposure and gain, is tracked against the score just after the last sweep. A refocus becomes due when either holds for a few minutes:
  - the score has fallen by more than a set fraction, with the seeing figure steady, so seeing alone does not trigger it;
  - the EAF's temperature has moved by more than a set amount since the last sweep.

  Autorun runs it at the next gap, still deferring around a Jupiter event (144). Count-based refocus stays available. Both thresholds are rig.ini settings, tagged UNVERIFIED with a clear-night item until a night sets them.
- **158.2 Temperature compensation, later.** With enough history (some nights, a fair range of temperatures), fit steps per degree and offer to move the focuser between sweeps as the temperature changes. It is off by default, and offered only when the fit is good, showing its spread.

## Rules
Phase 149's rules: nothing assumes a camera or the EAF's step size. The temperature comes from `IFocuser`; a focuser without a sensor simply has no temperature column. Rule 7: nothing needs the internet.

## Versioning
Every PR bumps PATCH. MINOR marks a milestone release (3.5), not a phase. Suggested PRs: 158.0 with 158.1, then 158.2 after enough nights.
