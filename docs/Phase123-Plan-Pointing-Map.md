# Phase 123 — the pointing map, measured before it corrects

Phase 6.6 shipped the recording half of GoTo map learning. Every lock is plate
solved, the error between where the mount said it was and where it really was
goes into `pointing_model.csv`, and the mount is synced. The correcting half was
left off on purpose, and GoToPanel's own comment said why: two questions only the
sky could answer. This phase builds what answers them and fixes the code the
correction would have used.

## The two questions

1. **The sign.** The map holds truth minus reported. A GoTo drives until the
   reported position equals the target, which leaves the telescope at target
   plus error. The command has to be target minus error.
   `ApplyPointingCorrection` added it, which would have doubled every error it
   touched. It was never called. It is gone.
2. **Does a sync spend the data?** Every lock syncs, so the error recorded
   tonight may not exist tomorrow. If so, an all-nights map puts back errors
   the mount no longer has. Should the correction come from every night,
   tonight only, or nowhere? This is a measurement, not a design decision.

## How every solve answers it

Before a new solve is recorded, `PointingScore` asks each map what it would have
predicted at that spot. A GoTo there would have had exactly the error just
measured, after every sync so far, so the prediction is graded against it: had
the map been applied, this is what would have been left. Running RMS is kept for
no correction, the all-nights map and tonight's map. If syncs spend the data, the
all-nights map is graded worse than nothing and the score says so.

On first use the whole CSV is replayed through the same grading, so the verdict
covers every solve ever recorded, not just tonight's.

Each lock logs two lines, for example:

```
Pointing: off by 412"; all-nights map would have left 95"; tonight's map would have left 60"
Pointing map: tonight's cuts the error by 78% over 15 solves (380" to 84"). Set PointingMapApply=2 to use it.
```

A map is recommended only after **8 graded solves** and a **30% cut** in RMS.

## Applying it

`PointingMapApply` in the rig file: `0` off (the default), `1` the all-nights map,
`2` tonight's. When it is on, a GoTo commands target minus the prediction
(`CorrectedCommand`), and the log says what was applied. Predictions over a degree
are not applied: an error that size wants a realignment, not a map. With the
setting off, the prediction is still reported on every GoTo.

The app does not turn this on by itself. The owner does, after the verdict.

## Units fixed

The old prediction averaged raw RA hours across declinations. An RA difference is
four times as much sky at the equator as at 75 degrees. It also measured distance
with azimuth unwrapped, so 359 and 1 degrees were 358 apart. Errors are now
arcseconds east and north on the sky, and distance is the angle on the sphere.

## What gates it

`tests/pointing_score.cpp` grades simulated mounts whose error fields are known.

| Field | Verdict |
|---|---|
| Smooth and stable across 3 nights | All-nights map, 79% cut |
| Noise only, no pattern | Nothing recommended; both maps make it worse |
| Whole field moves up to half a degree between nights | Tonight's map, 97% cut; all-nights only 5% |
| No solves | Says so, recommends nothing |

Also: the corrected command lands on the target with the right sign, RA errors
shrink by cos(dec), and the map wraps through north and 0h.

## Owed

Checklist 44. `pointing_model.csv` has no rows yet, so nothing has been graded on
the real 8SE. The SKY SURVEY button fills it fastest. Only `AutoSolveOnLock`
(locks and the survey) is graded; POINT SOLVE → SYNC syncs without recording.
