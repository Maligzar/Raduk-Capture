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

### 133.1, 2026-11-08: the emulated mount behaves like the measured 8SE

**`RADUK_EMU_NEXSTAR=1`** switches `MountEmulator` to the NexStar as measured on
2026-11-07, the same mount that `tests/rate_guide.cpp` models on the bench, now
under the whole app. It is opt-in, so the scenarios written for the older model
keep their behaviour. In this mode:

- **The encoders are the state.** The axes are azimuth and altitude, always.
  `GetAltAz` reports the encoders directly, as the real mount does.
- **A command replaces tracking** on its axis while it runs, instead of adding
  to it. With tracking off and nothing commanded, the axes stand still while the
  sky turns.
- **Azimuth is mirrored.** Direction 0 turns its encoder backwards.
- **The hand control's tracking is mis-set**, as measured: 16.0 against
  16.3″/s in azimuth and 5.0 against 5.2″/s in altitude.
- **The hand control overwrites a guide-scale variable rate** while it tracks.
  This was measured for an altitude command at guide rates. Jogs worked that
  night, so commands of 100″/s or more are left alone. **That bound is
  inferred, not measured.**

**Two defects found by building it.**

- **The sky turned in whole seconds.** `CurrentJulianDate` used `time(nullptr)`.
  With the axes held still, the emulated star leapt about 16″ once a second
  instead of drifting. The clock now has millisecond resolution. Its only user
  is `LocalSiderealHours`, so every caller (sky map, cord wrap, the emulator)
  gains precision and nothing else changes.
- **The RA/Dec ↔ alt/az round trip is not exact.** Driving the encoders through
  it every tick put its error into the rates, by up to 75% on the first build.
  Tracking is now measured against the pointed sky point's own reading at the
  previous tick, so the error cancels.

**`tests/emulator_nexstar.cpp`** runs the emulator in real time and reads its
encoders. It checks that:

- with tracking on, azimuth runs at 0.983 of the sky and altitude at 0.963,
  against 0.982 and 0.962 measured;
- with tracking off, the axes stand still;
- a 20″/s command comes out at 19.98″/s, mirrored in azimuth and not in
  altitude;
- a 20″/s altitude command is overwritten by tracking within a tick;
- a 240″/s jog keeps its rate.

It passed twice under mingw-w64 and wine, with the timezone shim from
`tests/README.md`. `mount_jog` and `push_to`, which also use the sky clock,
still pass.

**Scenarios.** `keydown KEY` and `keyup KEY` hold a key for as long as the
`wait` between them. The existing `key` step presses and releases in the same
instant, which is no use for a jog. `tests/scenario_runner.cpp` section 4 gates
this. All four scenarios set `RADUK_EMU_NEXSTAR=1` and point at Kochab, as
`motion.scn` does:

| Scenario | Checks | From the night |
|---|---|---|
| `guide-nexstar` | PLANET LOCK calibrates on app-driven axes and locks; no "Guiding abandoned" for 20 s | The baseline |
| `guide-format` | 1280 → 640 → FULL while locked: "the frame changed" twice, no abandon, tracking not handed back | F5, 01:21 |
| `guide-jog` | A 0.3 s FINE jog on the full sensor while locked: "hand jog ended", no abandon, LOCKED again | F6, 01:37 |
| `guide-edge` | A 0.6 s FINE jog toward an edge, then PLANET LOCK: "centre it roughly", and no calibration move | F7, 01:42 |

**Not run.** The scenarios need the Windows emulator build, so they have only
been parsed here. The jog durations are estimates from the FINE rate (240″/s)
and the 0.2″/px plate scale. The owner's first `run-scenarios.ps1 guide` run
will settle them, and `guide-edge`'s 0.6 s is the one most likely to need
tuning.
