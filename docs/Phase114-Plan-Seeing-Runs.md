# Phase 114 — seeing-aware runs

Autorun records on a timer, so a sequence spends the same disk and the same
derotation window on the worst minutes of the night as on the best. This
holds a run for the sky, and says afterwards which runs to stack first.

## What the tree decided

- **The gate's seeing score cannot do this, though the plan said to use it.**
  It is sharpness over the best of a short recent history, so it
  self-normalises: a poor night still reaches 10. `FrameAnalyzer.cpp` says so,
  and `main.cpp`'s session summary refuses to average it for that reason.
- **`sharpnessPct` is absolute, and it is not a measure of the sky.** It goes
  as the square of the signal, so AE moving exposure between runs would read
  as seeing improving or failing. Phase 112 measured that: 10% more exposure
  moves it 21%.
- The `_quality.txt` sidecars have the same fault across runs, and carry no
  highlight to correct it with. Ranking from them, as the plan said, would
  rank exposure. So the ranking uses the live figures instead.
- Focus sweeps change the score for reasons that are not the sky.

## Design

- **`src/core/SeeingMonitor.h`, pure.** Sharpness over `(highlight / 200)^2`,
  which takes the signal back out. A rolling 10 s median, so one frame in a
  moment of good seeing does not open anything. The figure is compared with
  the best median this sequence has seen, so the reference is what tonight
  can do, not a constant nobody could calibrate. Not fed during a sweep.
- **The hold goes before the focus sweep as well as before the run,** because
  a V-curve fitted in bad seeing is a worse focus.
- **It gives up rather than losing the night.** After `AutorunSeeingHoldSec`
  (180) the sequence goes on anyway and counts it, because a run in poor
  seeing is worth more than no run if the sky never recovers.
- **The first run is the reference.** Nothing can be judged on the first
  check, so the bar rises as the sequence goes on.
- **Ranking.** Each run's score is the median of the same figure across the
  run. A sequence ends by writing `autorun_<date>_<time>.txt` beside the
  captures: runs best first, each as a percentage of the best. Empty runs go
  last. Stopped sequences write it too.

Settings: `AutorunSeeingPct` 80, where 0 turns the hold off, and
`AutorunSeeingHoldSec` 180. 80 is a starting figure.

## What gates it

`tests/seeing_monitor.cpp`: the normalisation on real emulator frames, across
gain and exposure, and against real seeing blur. The window, the reference, the
ranking and its text. `tests/autorun.cpp`: the hold, its order against focus,
its limit, and that it never starts twice in one cycle.

## Owed

Whether signal squared holds on real frames, whether the median of a 2 Hz
figure follows real seeing, and whether 80% is the right bar. Checklist 37.
