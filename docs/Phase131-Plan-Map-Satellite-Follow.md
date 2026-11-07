# Phase 131 — FOLLOW a satellite from the sky map

Chosen by the owner from the roadmap ("A: Satellite follow"). Phase 130
put the satellites on the map but left clicking one as information only.

## What it does

- A satellite's popup on the map has **FOLLOW**. It hands the name to the
  GoTo panel's pass list, which flies that satellite's next pass that
  reaches the tracking band, exactly as TRACK SELECTED PASS would. The
  row is selected in the list, and the mount waits at the window's start
  if the pass is still to come.
- If the passes have not been worked out yet, they are, and the FOLLOW
  runs when they arrive.
- If there is nothing to fly it says why, in the pass panel and the log:
  "no pass of X in the next 24 h reaches the tracking band", or "X has no
  pass worth tracking in the next 24 h".
- Fixed in passing: an NEO's popup below 15° drew CANCEL on top of FOLLOW.

## Defect found and fixed

**A pass started late was failed as a reversed axis.** When the slew
lands after the window has opened, the satellite is already well ahead,
often more than 15°. The guard for an axis wired backwards fired on the
first tick: "the mount is moving away from the satellite: check the axis
directions". That is a telescope fault, so a night plan would also have
stopped there. The scenario found it on a 159° slew that took 80 s for a
pass opening in 45.

The guard is now measured from where the chase began: it fires past
`hardErrDeg`, or past the starting error plus a third of it, whichever
is larger. A reversed axis still shows, as an error that grows at twice
the sky rate.

**And the slew aimed at a point the satellite had left.** Every pass was
met at the start of its window. Map FOLLOW is usually pressed on a
satellite that is up now, its window already open, so the mount landed
20° or more behind it. A fast pass could then not be caught at all; the
first full run lost one whose azimuth moved at 2.9°/s, the mount's top
rate being 3. The slew now aims at where the satellite will be when the
mount lands, worked out from `SatSlewDegPerSec` and `SatSettleSec` (the
same figures the night plan uses), and the tracker holds there until it
arrives: "slewing ahead, to meet it 24 s into its window". A pass that
can be met at its start is flown exactly as before.

## Tests

- `satellite_passes`: a slew landing 30 s late completes; the same with
  a reversed axis is still caught.
- Scenario `map-sat-follow`: an unknown name is refused with a reason;
  the armed satellite's pass is flown and recorded under its name.
- Scenario `late-pass`: the window opens 5 s after arming, too soon for
  any slew. The mount is sent ahead to meet it, holds, and records.
