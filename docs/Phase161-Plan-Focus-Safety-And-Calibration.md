# Phase 161 plan: focus that is safe, calibrated and stated with an error bar

From the first-principles review of 2026-11-14 (architecture, concurrency, and the autofocus arithmetic). Status: `PLANNED`. 161.0 to 161.2 belong to release 3.4 (reliability); 161.3 to 161.6 to release 3.5 (planetary), whose acceptance is "autofocus repeatable across sessions" ([`ROADMAP-Next.md`](ROADMAP-Next.md#next-in-order)).

## Why

The review found three kinds of fault. Each claim carries its evidence.

- **The focuser can be driven where nobody asked.** Read from the code:
  - `EAFFocuser::GetPosition()` returns 0 when the SDK read fails, and that 0 is the base of `MoveRelative`, of autofocus's start position, and of the position a profile saves.
  - A move that fails or times out is never stopped, and no move is checked for arrival.
  - `Abort()` can be overtaken by the scan thread's next move.
  - A second key press steps from the in-flight position, overshoot leg included.
- **Threads cross where they should not.** Read from the code:
  - Autofocus's completion hooks call `SetWindowTextW` and assign a `std::wstring` from the scan thread.
  - `RingBuffer::GetLatestFrame` can return a frame older than the one before it.
  - A coarse sample can come from a frame taken before the settle ended.
- **The routine is tuned in the wrong units.** Its note says about 2,000 steps per mm and a focus zone of ±500 to 1,000 steps. The figures that can be assembled say otherwise:

  | Quantity | Value | Evidence |
  |---|---|---|
  | EAF steps per knob turn | 5,760 | Sourced (ZWO's specification, forum confirmations of 5,720 to 5,775) |
  | Mirror travel per turn | 0.75 to 1.06 mm | Sourced, and the sources disagree |
  | Focal plane travel per unit of mirror travel | m² + 1 = 26, with m = 5 | Textbook for a Cassegrain with a moving primary; m = 5 is the 8SE's f/2 primary to f/10 |
  | Focal plane travel per step | 3.4 to 4.8 µm | Inferred from the three rows above |
  | Blur diameter per step at f/10 | 0.17 to 0.24 px | Inferred |
  | Depth of focus, 8-inch f/10 | about 290 µm in all (4.88 λ N² at 590 nm) | Sourced (Starizona gives 287 µm) |
  | Critical focus zone in steps | about 60 to 85 in all | Inferred |
  | Tube expansion, 0.36 m of aluminium | 8 µm per °C of mirror spacing, about 0.2 mm per °C at the focal plane, about 40 to 60 steps | Inferred. If right, one degree moves focus by most of the zone |

  The fine pass samples every 67 to 200 steps, so at most one or two of its seven points fall inside the zone, and the half-backlash shift, at the unmeasured default of 200 against forum reports of several hundred to two thousand, can be wrong by more than the whole zone. Nothing here is proven on this rig. The point is that the two scales differ by seven to ten times, and one sweep on the sky says which is right.

Tested on the bench for this plan (a hard-edged disc convolved with a 34% obstructed annulus, no noise unless stated):
- At the routine's assumed scale, Jupiter's half-flux diameter does not move across the whole ±3,000 sweep (155.6 to 155.8 px).
- The three-point fit on 1000/HFD is biased by up to about 130 steps when focus sits between two coarse points. A least-squares parabola on HFD² removes the bias (−31 to +2 steps in one case) and does not reduce the scatter.
- For the fine pass, a log-parabola over all seven points cut the scatter from about 25 steps to 5 when the span covered the curve, and was worse than three points when it did not. The lever is the span against the curve's width.
- The edge metric's region holds 23% to 46% disc for any planet, so its median noise estimate reads the sky, not the disc's shot noise.

## Stages

Bench stages first: they need no sky, and the sky stages are not safe to run before 161.0.

### 161.0 The focuser cannot be sent somewhere by accident (bench, M)

**161.0a written 2026-11-14 (3.3.12)**: everything below except "Stop wins", which is 161.0b. As built, where it differs from the bullets:
- The rule is one pure header, `src/core/FocuserGuard.h`, used by the EAF wrapper and the emulator alike.
- A profile load and the remote `move_eaf` never override the guard: they are refused and logged. Only the panel's GO TO asks twice. A remote command cannot be asked twice, and a profile's position is a number from another night.
- Autofocus is held to the guard measured from where the sweep started, not per hop, so a move from one side of its span to the other is not refused half way (`MoveToDirectFar`).
- A focuser that says "not moving" short of its leg gets 2 s before the move is called short. Whether the real EAF ever does this is not known: clear-night item 59.
- The second leg is still commanded from `IsMoving()`; moving it to `Service()` is 161.0b's.

- **A read that fails says so.** `IFocuser::TryGetPosition(int&)` returns false on an SDK error. `GetPosition()` returns the last good value and never 0 for a failure. `IsMoving` reports its error the same way; an unknown state counts as moving.
- **No move from a failed read.** `MoveRelative`, autofocus's start, the profile save and the dashboard all refuse, with a visible line, when the position is not known.
- **Relative steps add to the commanded target.** Two quick presses of "in 10" end 20 below where the first began, however far the first leg had got.
- **A travel guard.** One command may move at most `FocuserMaxTravelSteps` (rig.ini; default 12,000, about two knob turns, UNVERIFIED) unless the caller passes an explicit override, which only the GoTo-position box and a profile load after a confirmation do. Autofocus's bounds come from the EAF's `MaxStep` and a rig.ini floor, not from 0 and 600,000.
- **Every move is checked.** `MoveToDirect`'s return is read; arrival means stopped and within 2 steps of the target; anything else stops the motor, ends the sweep, and names the reason. An abort reads "Aborted", never "EAF move timed out".
- **Stop wins.** One command mutex inside the focuser covers move, stop and the second leg, with an abort generation: a move issued with a stale generation is dropped. The second leg leaves `IsMoving()` (a query with a side effect) for an explicit `Service()` called from the 500 ms tick and from `WaitForMove`.
- **The emulator can fail.** `EAFEmulator` gains: a read that fails, a move that is refused, a stall, and a lag before `IsMoving` turns true. This is phase 157's focuser fault injection, built here because the tests need it.
- Gate: `tests/focuser_safety.cpp`, one case per bullet, and `autofocus_saturn` rerun with each fault. Acceptance: no test can make the emulated focuser travel more than the guard from a single fault.

### 161.1 Threads (bench, S)

- **Completion goes to the GUI thread.** The scan thread posts its result; the hooks run from the message loop. `AutofocusController` gains a `Deliver()` the loop calls. No Win32 call and no shared string is touched from the scan thread.
- **Running from the first instant.** `Start()` sets the state before the thread exists, so `Busy()` has no gap and a second `Start()` cannot join a live sweep.
- **The ring only goes forward.** `GetLatestFrame` returns nothing unless the frame is newer than the last one it returned. Gate: a ring test where the producer outruns the consumer and then pauses.
- **A sample belongs to its frame.** `AnalysisResult` and the fast score carry the frame's timestamp. The routine arms a window with "not before" (move done plus settle) and drops anything older. The three sample vectors are cleared under their mutex.
- Gate: `tests/af_controller.cpp`, `tests/app_feed.cpp`, a new `tests/ring_monotonic.cpp`, and ThreadSanitizer on `app_headless focus` where the toolchain allows.

### 161.2 Backlash is two numbers (bench, S)

- **`FocuserOvershootSteps`** is how far a move goes past before coming back. It needs only to exceed the slack, and costs only time. Default: the larger of 3 times the backlash figure and 1,500, UNVERIFIED until 161.3.
- **`FocuserBacklashSteps`** stays the measured slack, used only for the half-backlash shift of a coarse-only result.
- With the fine stage on, the result no longer depends on the backlash figure at all, because every fine point is approached the final way past a full overshoot. The log says which case applied.
- Gate: `af_direction_fit` and `autofocus_saturn` with the emulated slack at 2, 5 and 10 times the configured backlash; all land within the fine pass's tolerance.

### 161.3 Calibrate the focuser on the sky (clear night, M)

One command, CALIBRATE FOCUS, on a bright star or a planet's limb at 45° or higher. About ten minutes. It writes `focus_calibration.ini` beside rig.ini with the date, the EAF's temperature and the altitude, and never overwrites an older block.

| Measured | How | Gives |
|---|---|---|
| Focal plane travel per step | The slope of HFD against steps on both wings, well outside focus. For an annulus with obstruction ε, HFD = blur diameter × sqrt((1 + ε²)/2), and blur diameter = defocus / N | µm per step; the critical zone in steps; the emulator's blur constant |
| Slack | From a loaded position, reverse in 10-step moves until the edge score changes by more than 3 of its scatters. Three times each way | `FocuserBacklashSteps`, with its spread |
| Repeatability | Approach one step number five times the final way and score each | The floor under any focus result, in steps |
| Settle | The edge score against time after the motor stops, on the against-gravity and with-gravity approaches | `settleMs`, measured |
| Image shift | The centroid's move across a reversal | The edge margin readiness needs (`kAfEdgeFrac`) |

- Repeat at a second altitude on another night: an SCT's mirror sits differently on its baffle at 30° and at 70°.
- Every figure is tagged UNVERIFIED until this has run, under a new clear-night item (59).
- The routine's note of 2,000 steps per mm, the emulator's constant and `wiki/Autofocus.md` are corrected from the result, whichever way it falls.

### 161.4 The sweep in optical units (bench, then one clear night, M)

- **Coarse, by growth.** Step outward each way until HFD has grown by 30% over its minimum, with the step size taken from the calibration (a blur of about a quarter of the disc per step). A fixed ±3,000 becomes the fallback when no calibration exists. This brackets Mars and Jupiter alike, where a fixed span is too wide for one and too narrow for the other.
- **Coarse fit.** A weighted least-squares parabola on HFD² over every point, checked against the three-point vertex; a disagreement beyond one coarse step is logged and the wider bracket rerun.
- **Fine, by the zone.** Nine points spaced half a critical zone apart, so the span is about four zones and the curve's top is covered. Each point: approached the final way past the full overshoot, a measured settle, then at least 2 s of frames, because seeing has power at 1 to 5 s that 0.7 s of frames does not average.
- **Fine order shuffled.** The points are visited centre-out in alternating order, not one way across. A fade or a slow change in seeing then becomes scatter about the peak, not a tilt that moves it. This retires `FadeCorrect`; the centre re-read stays as a repeatability figure and a give-up test.
- **Fine fit.** A log-parabola over all points within a set fraction of the peak, falling back to three points when fewer than five qualify.
- **An error bar.** The result is resampled over its frames (bootstrap) and reported as `focus 168214 ± 9 steps (0.2 of the zone)`. Accept when the spread is under a quarter of the zone; otherwise run the fine pass once more and say so. This is the release 3.5 acceptance in a number.
- Gate: `autofocus_saturn` with the emulator's blur set from 161.3's figure and from the old one; `focus_metric_compare` extended with slow seeing.

### 161.5 The edge metric on real light (replay clips, then sky, M)

- **The gate follows the signal.** Noise is estimated on the disc's own superpixels (those above half the region's mean), or from a variance-against-signal line fitted across the region, so shot noise at gain 400 and above is what the gate clears.
- **The region comes from focus.** Its size is set from the coarse result's HFD at best focus, and the pass refuses to start without a valid one, so it can never fall to 48 px inside Jupiter's disc.
- **Green as an option.** `AutofocusChannel=green` scores G1 + G2 only. On a colour sensor low in the sky, dispersion smears the red and blue limbs apart, and an SCT's corrector adds a little chromatic focus spread; a single-band score has a narrower peak. Luminance stays the default until a night compares them (inferred, not tested).
- Gate: the replay camera's real clips (155.5) through `MeasureEdgeFocus` at known focuser positions, once a sweep has been recorded with RECORD on.

### 161.6 What the operator sees (bench, S, after 161.4)

- The sharpness HUD shows distance from the last focus result in zones, not steps, and how long ago and how many degrees ago it was taken.
- Phase 158's history line gains µm per step, the error bar and the altitude, and 158.2's temperature fit starts from the expected 40 to 60 steps per degree as a sanity bound, not as a value.
- AE is returned to the state it was in before autofocus took it.

## Which model builds each stage

The owner asked on 2026-11-14 for each stage to be marked: needs Fable, or Opus can handle it. The rule used, which is a judgement and not a measurement: **Fable** where a mistake is silent and no test here would catch it (lock ordering and lock-free state, a measurement whose physics has to be right, numerics that decide where the mirror stops); **Opus** where the change is specified by this plan to the level of a test case and a wrong result fails a gate. Two stages are split so the routine part does not wait on the hard part.

| Stage | Model | Why |
|---|---|---|
| 161.0a Reads that fail, no move from a failed read, relative steps from the target, the travel guard, arrival checks, the emulator's four faults | Opus | Each bullet is one test case in `focuser_safety`; a miss fails the gate |
| 161.0b Stop wins: the command lock, the abort generation, the second leg out of `IsMoving()` | Fable | Three threads touch the focuser; a wrong lock order or a missed generation check passes every test and fails on the rig |
| 161.1 Completion on the GUI thread, running from the first instant, the forward-only ring, samples tied to frames | Fable | Lock-free state shared with the time-critical capture thread, and a Win32 delivery path CI cannot run |
| 161.2 Overshoot apart from backlash | Opus | Two settings and three test cases, in code 139 already structured for it |
| 161.3 CALIBRATE FOCUS | Fable | A new measurement on real hardware: the wing-slope optics, the reversal test's threshold, and a state machine that must be safe when the sky does not cooperate |
| 161.4 The sweep in optical units | Fable | The fits, the shuffled order and the bootstrap decide the final position; a subtle bias is invisible without knowing where to look |
| 161.5a The edge metric's noise gate | Fable | A noise model that must hold from gain 100 to 450 on light the emulator does not reproduce |
| 161.5b The region from focus, and `AutofocusChannel=green` | Opus | Sizing and a channel switch, gated by `focus_metric` |
| 161.6 The operator's view, AE put back | Opus | Display and state restore |
| Smaller: the write buffer's pre-touch off the GUI thread; the stamp latency constant | Opus | Contained, each with an obvious check |
| Smaller: a missing coarse point left out of the fit | Fable | Lands inside 161.4's fit |

Review follows the same split in reverse: a stage Opus builds is reviewed by Fable before it merges when it touches the focuser (161.0a, 161.2), since those are the ones that move glass.

## Smaller items, folded into the nearest stage

- A missing coarse point is left out of the fit, not scored as 0 (161.4).
- `PlanWriteBufferSlots`' pre-touch of a whole-run buffer takes seconds at full sensor, not "a fraction of a second": move it off the GUI thread or show it (161.1).
- Frame stamps subtract half the exposure and not the readout and transfer latency. Measure the latency once with a flashed LED and record it in the SER and FITS headers as a constant; it matters to mutual events and occultations, not to WinJUPOS (its own small PR).

## Not in this plan

- A Bahtinov aid (row 32) and multi-star focus (row 31): different instruments, still planned.
- Moving the focuser during a recording: phase 158 decides that.
- Any change to tracking or recording beyond the ring fix.

## Risks

- **The travel guard refuses a move the owner wants.** It asks for a second press, as the other moving controls do; it never fails silently.
- **Longer sweeps.** 2 s a point over nine points with full overshoots is about 60 to 90 s for the fine pass. That is the price of averaging seeing; the dwell is a setting.
- **The inferred scale is wrong.** Then 161.3 says so, and 161.4 runs from the measured figure. No stage hard-codes mine.

## Decisions for the owner

1. The travel guard's default (12,000 steps proposed).
2. The overshoot's default before 161.3 has run (1,500 proposed; time is the only cost of a larger one).
3. Whether 161.0 and 161.1 go ahead of 153.2, as this plan argues: they are safety, and 153.2 moves recording wiring that neither touches.

## Versioning

Every PR bumps PATCH. Suggested PRs: 161.0a; 161.0b; 161.1; 161.2; 161.3 (the routine and the checklist item, run when a night allows); 161.4; 161.5a; 161.5b with 161.6.
