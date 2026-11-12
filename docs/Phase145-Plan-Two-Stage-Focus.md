# Phase 145: autofocus in two stages

The owner asked for this on 2026-11-12, after sharing advice on planetary focus measures and on EAF backlash on a C8. They chose the two-stage design, gated on the bench. It was built the same day, as 3.3.0.

## Why
- **HFD is good far from focus and weak near it on a big disc.** Since 2026-11-07, autofocus has scored 1000/HFD, after Laplacian variance measured sensor noise on Saturn at gain 411. HFD has a wide capture range, and noise and exposure cancel out of it. But on a disc much larger than the blur, the half-flux diameter barely moves near focus. On the bench, blur 0 and blur 3 px were only 0.1 to 0.2 frame-to-frame scatters apart on Saturn- and Jupiter-sized discs. On a frame-filling Moon there is no disc at all.
- **The edge metric is the reverse.** Edge contrast (Tenengrad) separated the same two positions by 2.2 to 2.3 scatters on the same synthetic discs. Far out (blur 48 against 72), HFD separated them by 44 and the edge metric by 12.
- **Too few frames per position.** About 4 single-frame analyses per visit, at the analyzer's 2 Hz rate.
- **The final direction was fixed.** Phase 139 already ended every move one-sided, with the EAF's firmware backlash at 0. But "one side" was always rising step numbers, with no tie to which way pushes the primary against gravity.

## What was built
- **145.0** `tests/focus_metric_compare.cpp` compares the two metrics on synthetic Saturn, Jupiter (with belts) and lunar scenes, with seeing and read noise. It asserts:
  - the edge metric wins near focus;
  - HFD separates positions far out;
  - the 75th percentile of 24 frames scatters well under one frame (0.022 against 0.056 on Jupiter).
- **145.1** `MeasureEdgeFocus` in `src/core/FocusMetric.h`, built from the owner's sketch with six corrections:
  1. Sobel runs only where the smoothed grid is defined. The sketch read an unwritten zero ring, which made a false edge round every region.
  2. The noise gate is in the gradient's own units. It is k = 3 times the composite kernel's L2 norm times the noise, which is measured on the grid itself with Immerkaer's kernel and a median (robust to edges, works on the Moon). The sketch gated at 1.5× the raw noise, which nearly all noise passes.
  3. The score is divided by the fixed number of points, not by the count above the gate. That count rose with blur on the bench, from 982 to 1863.
  4. Luminance superpixels at even anchors, so the score does not depend on the Bayer pattern (RGGB and GRBG agree to 0.3%). RAW16 is read at full precision on the 8-bit scale (RAW8 and RAW16 agree to 0.0%).
  5. The unused gain argument is gone; dividing by the mean squared cancels exposure (within 10% at half exposure).
  6. A caller-owned scratch buffer, so repeated calls allocate nothing.
  - Gated by `tests/focus_metric.cpp` section 3.
- **145.2** While the fine pass is collecting, main.cpp scores the newest raw frame up to 30 times a second. The region's size is fixed for the run, and centred on the target or the surface-lock position. The routine takes at least 20 scores a point (`SubmitFastScore`) and keeps their 75th percentile (`ScorePercentile`).
- **145.3** `AutofocusRoutine`:
  - After the coarse sweep, the fine pass runs 7 points across the coarse curve's width (`FineSpanFrom`). The width is where the curve is within 10% of its peak, clamped to [max(2 backlashes, 120), 2 coarse steps].
  - Every point is approached the final way, with the slack taken up first (`FineSweepPositions`), and the fit is `PeakWindow3`.
  - If the best point is at an end, it sweeps once more centred there. Anything else keeps the coarse result.
  - On a frame-filling target (phase 126's `surfaceTracked`), the fine pass runs alone. `AfReadiness` now says READY there when the fine pass is on, instead of NOT NOW.
  - rig.ini `AutofocusFineStage` turns it on (the default, 1) or off (0).
  - Gated by `tests/autofocus_saturn.cpp` cases 5 to 8, run three times here against the real routine (EAF emulator, a windows.h stand-in, ASan and UBSan):
    - flat-topped coarse curve: the coarse result alone ended 187 to 547 steps off, with the fine pass 2 to 34;
    - no edge scores: the coarse result stands;
    - fine pass only: 3 to 4 steps off;
    - finishing inward with 400 steps of slack: within 37 of focus.
- **145.4** rig.ini `FocuserFinalDirection=out|in`. `OneSidedApproach` and `ApproachSequencer` take the sign, and so do the coarse stage's half-backlash shift, its final approach and the fine pass. Gated by `tests/af_direction_fit.cpp` section 3b. The help says how to check which way pushes the mirror against gravity, and gives the hand check for backlash.

## Not adopted from the advice
- **A fixed sweep of 30 to 50 steps across 7 to 9 points.** The mechanical figures behind it were unverified (5,760 steps a turn, 0.75 mm pitch, 0.13 µm a step), and they disagree with the routine's own note of about 2,000 steps per mm. A span that narrow can fall inside the flat bottom that seeing creates, and it cannot bracket focus from 1,000 steps out. Instead, the fine span comes from the coarse curve's measured width.
- **The "typical" backlash of 80 to 180 steps.** Not verified. Clear-night item 47 measures it.
- **SML.** A thresholded second derivative is the family that failed on 2026-11-07 (inferred). The bench comparison used Tenengrad only.

## Not done
- A fade during the fine pass is not detected inside it. The final check against the start position still runs for planets, but not on the fine-only Moon path.
- The edge metric has never seen a real frame. The noise gate's k = 3 and the tests' 15% exposure tolerance were set on synthetic noise.
- Clear-night item 58.

## Versioning
Under the owner's rule ([`ROADMAP-Next.md`](ROADMAP-Next.md#next-in-order)), the PR that completes a phase bumps MINOR. This one PR completes Phase 145, so it is 3.3.0.
