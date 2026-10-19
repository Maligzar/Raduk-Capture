# Emulator and Testing

Full device simulation — camera, mount, focuser — with the real SDK
semantics, so the entire application runs with no hardware attached. The
emulator is not a stub layer; it closes the tracking loop, models defocus,
and injects faults.

This is the most practically valuable part of the project for anyone
developing on it, because planetary imaging work is otherwise gated on
clear nights.

---

## What each emulator simulates

### Camera

[`src/core/CameraEmulator.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/CameraEmulator.h)
(984 lines — the largest emulator by far)

Synthetic RAW8/RAW16 frames with a rendered planetary disc: per-planet
appearance, surface rotation, moons, turbulence, defocus blur, per-pixel
sensor noise, and per-frame atmospheric blur.

**It reports the real 3840×2160 sensor but will only stream up to
1280×960.**

> Larger requests are clamped and logged, **the way a real camera reports a
> mode it cannot deliver** — and because every caller reads the granted size
> back, the application follows along correctly. So `FULL` and `1920` both
> land on 1280×960 in the emulator while `1280` and `640` are exact.
>
> Above that cap the software render plus a 64-slot ring stops delivering
> frames inside the capture timeout altogether, which is **a dead emulator
> rather than a slow one** — hence the clamp rather than a best effort.

Phase 84 also fixed `FrameSize()` ignoring its own arguments, which is the
kind of bug that only shows up once something actually depends on the
answer.

### Mount

[`src/core/MountEmulator.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/MountEmulator.h)

Implements `IMount`. Simulates RA/Dec drift, slewing, tracking modes, and
`PulseGuide`.

**Phase 52 — closed-loop tracking:**

> The emulator models a fixed "sky target" (the planet). The pointing error
> (target − mount) is exposed via `GetPointingErrorArcsec()`, which
> `CameraEmulator` uses to **render the planet displaced from frame
> centre**. Guide pulses move the mount → the error shrinks → the planet
> re-centres, so **the full `CentroidTracker` correction loop runs closed in
> emulation.**
>
> Imperfect tracking is simulated by drifting the mount away from the target
> at a configurable rate even while "tracking" is on.

That is the key design decision: the camera and mount emulators are coupled
through a shared physical model, so the loop under test is the real loop,
not a mock of it.

Phase 78 made the emulated jog rates follow the **real NexStar slew-rate
curve**, so a rate-9 jog moves at a realistic 3°/s rather than an arbitrary
value — which matters for anything that measures motion, like the
[motion monitor](Mount-Control#motion-monitor).

### Focuser

[`src/core/EAFEmulator.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/EAFEmulator.h)

Motor movement at a realistic step rate, with absolute positioning and
temperature. Coupled to the camera's blur model through
`RADUK_EMU_TRUE_FOCUS`.

---

## Environment switches

They have no effect on the hardware build, and are listed so a test result
can be reproduced.

### Tracking and geometry

| Variable | Default | Effect |
|----------|---------|--------|
| `RADUK_EMU_JITTER_AS` | 0.5 | Seeing wander amplitude, arcsec |
| `RADUK_EMU_DRIFT_AS` | 0.3 | Mount tracking error, arcsec/sec |
| `RADUK_EMU_ROTATION_DEG` | 0 | Camera angle — **to prove the tracker measures its axis transform rather than assuming it** |
| `RADUK_EMU_UNALIGNED` | off | Mount reports itself unaligned, reproducing the whole tracking-blocked path |

`RADUK_EMU_ROTATION_DEG` is the sharpest test in the set: a tracker that
assumed camera orientation corrects in the wrong direction and fails
visibly, while one that measures its 2×2 transform does not care. See
[Tracking and Guiding](Tracking-and-Guiding#guide-calibration).

`RADUK_EMU_UNALIGNED` exercises the path where
[`StartAutoAcquire`](Tracking-and-Guiding#one-button-acquisition) must
refuse with a reason, which is otherwise only reachable with a real
unaligned mount.

### Focus

| Variable | Default | Effect |
|----------|---------|--------|
| `RADUK_EMU_TRUE_FOCUS` | 168000 | Focuser position at which the image is perfectly sharp |
| `RADUK_EMU_BLUR_PX_STEP` | 0.0083 | Blur added per step away from focus |
| `RADUK_EMU_FREEZE_SURFACE` | off | Stops planet rotation, turbulence and moons, so **only focus varies** |

### Image quality

| Variable | Default | Effect |
|----------|---------|--------|
| `RADUK_EMU_NOISE` | 0 | Per-pixel sensor noise, DN |
| `RADUK_EMU_SEEING_AS` | 0 | Per-frame atmospheric blur, arcsec |
| `RADUK_EMU_PEAK_DN` | 0 | Exposes the simulated disc to this peak level. 0 keeps plain gain/exposure behaviour; **~200 gives a realistically exposed frame with headroom**, which is what anything measuring a noise floor needs |

### Scene and faults

| Variable | Default | Effect |
|----------|---------|--------|
| `RADUK_EMU_TARGET` | *active profile* | Which planet to render. Normally the emulator renders whatever profile is selected and follows every later switch; this **pins** one for bench work |
| `RADUK_EMU_REMOVE_AFTER_S` | off | Simulates the camera being unplugged after N seconds |

`RADUK_EMU_REMOVE_AFTER_S` is how the Phase 58 self-healing path
(`CaptureStats::restarts`, `cameraFault`) gets tested without pulling a real
cable mid-capture.

---

## Why the focus, noise and seeing switches exist

This is the most important paragraph on the page:

> The simulated planet was previously rendered **perfectly sharp at every
> focuser position and perfectly clean in every frame.** Nothing that
> depends on focus or on averaging frames could be tested at all — an
> autofocus sweep was fitting a curve to noise, and **a stack of identical
> clean frames can only ever equal one frame.**
>
> Setting `RADUK_EMU_TRUE_FOCUS` gives a focus test a **known correct
> answer**, which is what turns it into a real check.

Two subsystems are untestable without these:

| Subsystem | Needs | Without it |
|-----------|-------|------------|
| [Autofocus](Autofocus) | `TRUE_FOCUS`, `BLUR_PX_STEP`, `FREEZE_SURFACE` | Fits a parabola to noise; the "peak" is wherever noise happened to be highest |
| [Quick-look stacker](Image-Analysis#quick-look-stacker), seeing score, capture gate | `SEEING_AS`, `NOISE` | Every frame is equally sharp, so frame selection has nothing to choose between — a top-15% gate admits an arbitrary 15% |

### Why the focuser starts at 166,500

Both numbers matter, and neither is arbitrary:

- **The real rig focuses near 168,187**, so the emulator starts in the same
  part of the range. It used to start at **50,000**, which was nowhere near
  anything real.
- **Autofocus searches ±3000 steps** from wherever it starts. From 50,000 it
  swept 47,000–53,000 — about **115,000 steps away from focus** — so it
  could never bracket focus from a fresh launch, **failed every time, and
  looked broken when it was behaving correctly.**
- **The 1,500-step offset** means the emulator opens slightly *soft*.
  Starting exactly at focus would make an autofocus run a no-op that proves
  nothing.

---

## A disproved theory, recorded

Phase 80 used `RADUK_EMU_PEAK_DN` to test and **disprove** the theory that
the sharpness metric's problem was a noise floor:

| Sensor noise | Raw variance | Derived "floor" | Net |
|--------------|--------------|-----------------|-----|
| 0 DN | 32.5 | 35.2 | **−2.7** |
| 6 DN | 216.2 | 266.0 | **−49.7** |
| 14 DN | 1201.8 | 1723.3 | **−521.4** |

A frame with zero noise yielded a "noise floor" larger than its entire
variance. Clipping was the earlier suspicion and it was also wrong —
exposing the disc properly changed nothing. The real problem was the
*ceiling*. Full reasoning in
[Image Analysis](Image-Analysis#the-noise-floor-that-does-not-work-and-why-it-is-still-computed).

The point for testing practice: the emulator was built out far enough to
settle a measurement question, and the negative result was written into the
source so nobody tries it a fourth time.

---

## Build both configurations

```bat
build_all.bat          :: both — use this
build_emulator.bat     :: emulator only
launch_emulator.bat    :: build if needed, then launch
kill_radukcapture.bat  :: force-terminate a hung instance
```

> **Phase 80 found the hardware build had been silently broken for four
> phases** while all development happened in the emulator configuration.
> Both configurations now build from one script. If you only ever run one of
> them, you will eventually ship the other one broken.

---

## What the emulator cannot tell you

Honest limits, worth knowing before trusting an emulator result:

- **Serial timing.** `PulseGuide` on the emulator is an instantaneous
  position nudge; on [NexStar it blocks](Mount-Control#pulseguide) for the
  pulse duration. The correction loop's real cadence cannot be measured in
  emulation.
- **USB contention.** [`USBTrafficManager`](Device-Layer#usb-arbitration)
  throttling has nothing to contend with.
- **Real throughput.** The software render cannot produce 1.5 GB/s, so ring
  and write-buffer overrun behaviour under genuine load is untested here.
- **Whether a sky-derived noise floor is an emulator artefact.** The source
  says so explicitly: the synthetic disc sits near 255, and judging that
  question "needs a properly exposed frame, so this stays a diagnostic until
  there is on-sky data."
- **The `SHARP_CEIL` value.** Still worth one on-sky check: if readings sit
  near the top raise it, if they crowd the bottom lower it. `main.cpp`
  self-checks and logs the recommended direction.
- **Whether the pointing model's recorded quantity is the right one.** That
  is "a measurement on a real mount, not a decision to take here." See
  [Sky, GoTo and Plate Solving](Sky-GoTo-and-Plate-Solving#why-it-reports-and-does-not-correct).

---

## The test suite, and what it does not cover

This section used to say there was no automated test suite. There is one,
and it gates merges — corrected 2026-10-18.

[`tests\run-tests.ps1`](https://github.com/Maligzar/Raduk-Capture/blob/master/tests/run-tests.ps1)
builds and runs every suite in `tests\` and exits non-zero if any fails.
On `master` that is 13 suites: `histogram_accuracy`, `adc_alignment`,
`sensor_analysis`, `write_buffer_sizing`, `sky_conditions`,
`capture_thread_submit`, `ai_context_facts`, `object_knowledge`,
`object_panel_text`, `slew_variable`, `planet_apparent_size`,
`profile_notes` and `recentre`. They run on the bench with no camera,
mount or sky attached, which is what lets them gate anything.

Beside them sit the **manual harnesses**, `tests\manual-*`, deliberately
NOT in the gate because each needs hardware, third-party software or a
real recording — a gate that cannot run on a clean checkout is not a
gate.

**There is still no CI.** Nothing runs these on push; the gate is a
local one, run by hand before a commit. That is the honest limit of it.

The suite does not cover the GUI, the device SDKs, or anything that
needs sky. Those remain the emulator's job and the review documents in
[`docs/`](https://github.com/Maligzar/Raduk-Capture/tree/master/docs):

| Document | Covers |
|----------|--------|
| `CODE_REVIEW_2026-06-10.md` | Full sweep, 28 KB |
| `CODE_REVIEW_2026-07-06.md` | Follow-up |
| `SECURITY_REVIEW_2026-07-11.md` | Pre-public-release privacy and attack surface |
| `Tracking-Findings-2026-10-08.md` · `Tracking-Review-2026-10-09.md` | Tracking behaviour on the real rig |
| `Control-Review-2026-10-09.md` | Control path |
| `CLEAR-NIGHT-CHECKLIST.md` | What to verify on sky |
| `Phase66/67/68-Plan-*.md` | Phase design documents |
| `NEXT-SESSION.md` | Working notes |

The review documents are where the "review C1", "review G4", "review S1"
references scattered through the source point to, and they are the nearest
thing to a regression record. `CLEAR-NIGHT-CHECKLIST.md` is the manual test
plan for anything the emulator cannot settle.

---

**Next:** [Getting Started](Getting-Started) · [Autofocus](Autofocus) · [Troubleshooting](Troubleshooting)
