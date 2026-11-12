# Phase 139 plan: focus that respects backlash, and says when it can work

Planned 2026-11-11, from the owner's report at the rig. **139.0 to 139.2
written 2026-11-12** (see PHASE-STATUS row 139); 139.3 not built.

One change from this plan as written: 139.1's brightness normalisation
was not built. Autofocus has been fed 1000 / the planet's half-flux
diameter since 2026-11-07, a figure a fading target does not lower, so
there was nothing to divide out; dew blurring the image is a real change
in focus quality that no normalisation could remove, and 133.2's fade
warning covers it.

## What the owner sees

Focusing with the EAF, the image comes sharp for a moment and blurs as the
EAF keeps moving: that is the EAF passing through focus, which is expected.
What is not expected: **returning to the exact step number where it was
sharp gives a blurred image, and going away and coming back in sometimes
restores it.** The same step number does not mean the same focus.

## Why (inferred, to be measured in 139.0)

The EAF counts motor steps, not mirror position. On the 8SE it turns the
stock focus knob, which slides the primary mirror (recalled), so between
the step count and the focal plane sit the knob's gear slack and the
mirror's own shift. Reversing direction takes up that slack before the
mirror moves. So a position reached moving outward and the same number
reached moving inward are two different focus positions, offset by the
backlash. "Sometimes" fits a mirror that does not always settle the same
way.

The code makes two assumptions this breaks:

- **The out-and-back sweep averages the two visits to each position**
  (`AutofocusRoutine.h`, added after the 2026-11-08 Saturn fade, F9).
  Outward and inward visits to the same number sit a backlash apart, so
  averaging them averages two V-curves shifted against each other. That
  flattens the curve and can move its fitted peak. If the backlash is
  near the width of the focus zone, the peak can disappear.
- **Backlash is a guess.** The final approach overshoots by a fixed
  200 steps. Nothing measures the real figure. The app never calls
  `EAFSetBacklash` or `EAFGetBacklash`, so whatever compensation the EAF's
  own firmware holds is unknown and may be stacking with ours.

Manual moves (the panel, `move_eaf_rel` from the dashboard) do nothing
about direction at all, which is exactly where the owner met it.

**This reverses one line of Phase 133.** That plan says "No autofocus
redesign. F9's fix is plausible and tested; it needs a sky sweep, not more
code." The owner's observation is new evidence against the out-and-back
averaging that F9 introduced, and **the owner approved the redesign on
2026-11-11 without waiting for a night's measurement.** 139.0 to 139.2 are
built together; until the sky gives the real backlash, 139.1 and 139.2 use
the EAF firmware's figure, or 200 steps if that is zero, and say which.

## Stages

| Stage | What | Where | Size |
|---|---|---|---|
| **139.0** | **Measure, change nothing.** At connect, log the EAF firmware's backlash (`EAFGetBacklash`). After every sweep, fit the outward and the inward curves separately as well as together, and log both peaks and their difference. The difference is the backlash, mirror shift included, measured on the sky. Gate: `tests/af_direction_fit.cpp`, two synthetic V-curves offset by a known amount give that offset back, and their average gives the flattened peak this plan predicts | Bench, before the 133.3 night | S |
| **139.1** | **Fit one direction.** The position comes from the outward curve, the same direction as the final approach. The inward pass stops being averaged in; it keeps measuring backlash. The fade the averaging was added for is handled by normalising each point for brightness, as Phase 114 does, plus the 133.2 fade warning. The final approach overshoots by the measured backlash, stored as `FocuserBacklashSteps` in `rig.ini`, with the firmware's own compensation set to zero so it is applied once | Bench, after the night's number | S-M |
| **139.2** | **Manual moves approach from one side.** Every EAF move that ends inward goes past by the measured backlash and comes back outward, so a step number means one focus position however it was reached. This is the fix for the owner's report. On by default, with a setting to turn it off | Bench | S |
| **139.3** | **Autofocus readiness, from what is in the frame.** A pure `AutofocusReadiness(inputs)` returns READY or NOT NOW with one reason, shown on the AUTO FOCUS button and its status line, and checked again when it is pressed. Inputs, each already measured somewhere in the app: focuser connected and idle; not recording; exactly one target found by the area-flux finder; its disc between a lower size limit (too few pixels to measure HFD) and the frame (a full-frame Moon or Sun needs the surface metric, so it says so); peak signal neither clipped nor under a floor; the target far enough from the frame edge to survive the mirror's shift across a sweep; the lock holding or the mount tracking; no fade trend in progress (`FadeWatch`); seeing steady enough that a curve could be fitted (spread of `SeeingMonitor`'s recent medians). Pressing it while NOT NOW asks for a second press, as moving controls already do, rather than refusing outright. Gate: `tests/af_readiness.cpp`, one case per reason, and a scenario in the emulator | Bench | M |

Order (changed 2026-11-11 at the owner's word): 139.0 to 139.2 together,
after Phase 140's auto-exposure fix, since a sweep run on a clipped planet
measures nothing. 139.3 with 140.3. The first clear night after them
measures the backlash and replaces the provisional figure.

## What it owes the sky

- 139.0: one sweep on a real planet, read for the outward and inward peaks
  (clear-night item 47).
- 139.2: the owner's test, reversed: find focus, move away 1000 steps
  inward, return to the number, and it should still be sharp.
- 139.3: does READY agree with the owner's own judgement on a real night,
  including a night of poor seeing?

## Versioning

Every pull request for this plan bumps PATCH in `src/Version.h` (3.1.0 to 3.1.1)
and adds its row, with that version, to the Phase History in
`RadukCapture-Help.md`. The pull request that completes the phase bumps MINOR
instead and resets PATCH (3.1.4 to 3.2.0). MAJOR and a git tag are the
owner's call at a release. CI fails any pull request that does not raise the
version above the base branch's. When two pull requests in flight both bump,
the second to merge resolves `Version.h` to the higher of the two plus one
PATCH. The rule is the owner's, 2026-11-12, and is written out in
`src/Version.h`.

The stages merged so far shipped in 3.1.0, the catch-up. Every stage from here follows this rule.
