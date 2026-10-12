# What needs a clear night

Written 2026-10-11, at Phase 88. This is the debt that cannot be paid on
the bench: numbers calibrated against a simulator that has no atmosphere
and no real sensor, and code paths the real camera has never executed.

Everything here already works in the emulator. That is the problem — the
emulator is where these numbers came from, so it agrees with them by
construction.

**Order matters.** Items 1–3 should be settled before a serious capture,
because each one can silently bias everything recorded afterwards.
Items 4–9 can be checked whenever there is a spare minute.

**Items 1–3 now report themselves.** They used to require watching a log
for a condition you had to know to look for, which is a poor way to find
out that every frame of a session was mis-scaled. The app now measures all
three and says so when they are wrong — so for those, "doing the item" means
reading one line rather than running a procedure.

---

## 1. `SHARP_CEIL = 1000` — the one that biases everything

**File:** `src/core/Sharpness.h:193`

Every sharpness percentage, the seeing score, the capture quality gate
and the autofocus curve are all scaled by this one constant. It has been
wrong twice already, and both times the symptom was not obvious:

- `8000` was tuned against a sharpness kernel that turned out to be
  measuring colour, not detail.
- `100` was then calibrated on a **noiseless** emulator frame where
  at-focus variance is about 45. Switch on 10 DN of sensor noise, which
  any real camera has, and at-focus variance is about 610. The gauge
  pinned at 100% across three consecutive autofocus samples, the peak
  became a plateau with no curvature to fit, and autofocus went from 19
  steps off true focus to 500 steps off.

`1000` was chosen to leave headroom for a noisy real frame. It has never
seen one.

**The app now watches this itself.** After about three minutes of imaging it
writes one of these to the log if the ceiling is wrong, and says nothing if
it is right:

```
[SHARP] CEILING TOO LOW: 231 of 400 readings at or above 95%. The curve is
        clipped and autofocus cannot find a peak - raise SHARP_CEIL
[SHARP] CEILING TOO HIGH: 377 of 400 readings at or below 10%. Almost all
        resolution is being thrown away - lower SHARP_CEIL
```

It waits 400 samples on purpose, so the low readings during a focus sweep
cannot trigger it, and it says each thing once.

**To judge it by eye instead,** get a planet roughly focused and watch the
`[SHARP]` log line through a focus sweep.

| What you see | Meaning | Action |
|---|---|---|
| `pct` reaches 90–100 and sits there | ceiling too low, curve is clipped | raise `SHARP_CEIL` |
| `pct` never exceeds about 10 | ceiling too high, all resolution lost | lower `SHARP_CEIL` |
| `pct` sweeps smoothly across roughly 5–80 with a clear peak | correct | leave it |

Only the **shape** of the curve matters to autofocus. The ceiling just
has to not flatten it.

## 2. Guide rate in `rig.ini` — now measured and reported

**File:** `src/core/RigConfig.h:162` (`GuideRateArcsecPerSec = 7.5`)

Pulse sizing and the drift readout both depend on this. The tracker
measures the real rate during calibration and compares, but **until
today that comparison was a `printf`, which reaches nothing in a GUI
build** — so the one diagnostic that tells you the value is wrong has
been invisible for its whole life. It is now part of the calibration
message, which goes to the log and the session log.

**What to do:** run `[CENTROID]` through to calibration once and read the
line. It now ends with:

```
guide rate measured 7.41"/s vs rig.ini 7.50"/s
```

and appends `<- WRONG, set GuideRateArcsecPerSec` if they differ by more
than 2×. Put the measured value in `rig.ini` either way.

## 3. Plate scale — a calculated number, never checked against the sky

**File:** `src/core/RigConfig.h:136` (`PlateScaleArcsecPx = 0.203`)

Derived from 2032 mm focal length and 2.0 µm pixels. The real figure
depends on where the camera actually sits in the light path, and feeds
the ADC figure, the drift readout and the overlay scales.

**The app now reports this itself.** A solve knows the true field width, so
it knows the true scale. Every successful solve logs the comparison:

```
[GOTO] Plate scale: solved 0.2061"/px vs rig.ini 0.2030"/px (2% out)
[GOTO] Plate scale: solved 0.2480"/px vs rig.ini 0.2030"/px (22% out)
       <- set PlateScaleArcsecPx to the solved value
```

**What to do:** plate-solve one frame (`[GOTO]` → solve) and read that line.
Anything over about 5% is flagged, and worth correcting.

---

## 4. Code paths the real camera has never run

The emulator cannot reach any of these, so they are verified only by
their own clamp logic.

- **`FULL` at the true 3840×2160.** The emulator streams at most
  1280×960, so only the clamping path has ever executed. On the real
  camera `FULL` should give the whole sensor at origin (0,0), and the
  log should read `3840x2160`, not a clamp.
- **16-bit capture end to end.** The format bar offers it, the pipeline
  handles it, the emulator exercises it — but no real RAW16 frame has
  been through it. Record a short 16-bit SER and confirm the file opens
  in your stacker with the expected bit depth.
- **The 530 MB ring at full sensor.** 64 slots × 8.3 MB. Allocation
  succeeded in the emulator at smaller sizes; at full sensor on the real
  camera, watch `BUF` and `LOST` in the status bar for the first minute.

## 5. Dark and flat masters — the emulator has nothing to simulate them with

**File:** `src/core/CalibrationFrames.h`

The arithmetic is unit-tested against cases with known answers, and the
load/validate/apply path is verified in the app. But the emulator has no
covered-scope mode and no vignetting, so the masters have never been
built from a real sensor.

**What to do:**

1. Cover the telescope, press **DARK**, wait for 32 frames.
2. Uncover, and look at the preview. Hot pixels should be gone and the
   background slightly darker. If the image goes black, the dark was
   taken with light still reaching the sensor.
3. The status line should read `Cal: dark ON`. If it says
   `off(wrong resolution)` you changed format since building it.
4. For the flat: an evenly lit surface, press **FLAT**. Corners should
   brighten relative to centre, and **colour should not shift** — the
   flat is normalised per Bayer position precisely to avoid that, and a
   visible colour cast after enabling it means that is not working on
   real data.

## 6. Thresholds tuned on synthetic frames

Each of these was chosen against emulator output and is plausible rather
than measured.

| Constant | File | What to watch |
|---|---|---|
| `PERCENTILE = 0.85` | `QuickLookStacker.h:51` | The stacker keeps the best 15%. On real seeing, does the stack visibly beat a single frame? If it admits almost nothing, the gate is too tight. |
| `COLD_START = 32` | `QuickLookStacker.h:50` | Frames before it starts judging. Too few and the threshold is set by a bad sample. |
| `MAX_SHIFT = 64` | `QuickLookStacker.h:54` | Alignment range in superpixels. Real seeing jitter is 10–25 px; if frames are rejected as "planet jumped", this is too small. |
| `CENTROID_MIN_PEAK = 20` | `FrameAnalyzer.h:65` | Below this a frame is treated as noise. A faint target at high altitude may sit under it and report "too faint" when it is usable. |
| `SPEED_WINDOW_S = 2.5` | `MotionMonitor.h:50` | Speed averaging window for the motion plot. |

## 7. Night vision, read at the eyepiece

Verified on the bench by measuring the rendered windows -- the control panel
and status bar go to 0% blue, the toolbar to 3.5% (label anti-aliasing). What
the bench cannot answer is whether it is **dark enough in practice**: whether
the red is deep enough to keep dark adaptation through a session, and whether
the accent colours converging to one red actually costs anything when reading
the panel at night.

**What to do:** work a full session with it on. If the screen still feels
bright, the tint is a one-line change in `Theme::NV` (`src/gui/Theme.h`) --
the green component is currently a tenth of the luminance and can go to zero,
and the luminance itself can be scaled down.

(The mid-session overlay gap noted when this was written is fixed — the
preview overlays now follow a toggle without a restart.)

## 8. The motion plot's camera source

**File:** `src/gui/MotionMonitor.h`

The mount source is proven. The **camera** source — deriving motion from
the tracked centroid rather than from mount reports — has only ever seen
a simulated planet on a simulated mount.

**What to do:** lock onto a real planet, switch the plot to CAMERA, and
give the mount a single FINE nudge. The trail should move by roughly the
commanded amount. This is the feature that exists to answer "did that
small command do anything", so it needs to be right.

## 9. Known broken, and not ours

**AutoStakkert will not start on this machine.** It parses every option
correctly and then never begins processing. Proven not to be
RadukCapture: the identical command fails from a bare command prompt,
and so does the exact command that worked earlier the same day.
`AutoStackOnStop` ships off because of it.

Next steps are reboot, then reinstall. Not a night-sky item, but it
blocks the stacking handoff whenever it is next wanted.

## 10. Auto-exposure — restructured on the bench, unproven anywhere

**Files:** `src/core/AutoExposureAssistant.h`, `src/core/FrameAnalyzer.cpp`

Reported from a live session on 2026-10-12: AE cycles through settings
and never settles. The session log showed why, and it was arithmetic
rather than tuning:

```
AE: ↑ Exp 8 → 11 → 15 → 21 → 29 → 40.00ms      exposure to its ceiling
AE: ↑ Gain 100 → 182 → 248 → 338 → 461 → 570   then gain to maximum
AE: ↓ Exp 40 → 36 → 32 → 29 → 26.00ms          then unwinding again
```

AE drove the **frame mean** to 127-178 DN. A planet is a small bright
disc on black sky, so the frame mean sits in the low tens however the
planet is exposed — the target was unreachable, so the controller went
to both ceilings, clipped, got cut back by the clip emergency, and went
round again.

It now drives `highlightDN`, the 99.9th percentile of luminance — the
brightness of the brightest real thing in the frame — toward 185-215 DN.

**None of these numbers has met a real sensor, and the restructured loop
has not yet been watched through to a settle even on the bench.** What
needs checking, in order:

1. **Does it settle at all?** Turn AE to AUTO on a real planet and watch
   `[AE]` in the log. It should converge within a few ticks and then sit
   on `AE: OK hi NNN DN`. If it walks to a ceiling again, the target is
   still unreachable for that target's size.
2. **Is 185-215 DN right?** Too high and seeing will clip the planet on
   good moments; too low and you are throwing away dynamic range. Judge
   it by the histogram panel: R, G and B should all stop short of the
   right edge with the clip percentages reading 0.0%.
3. **Is the 99.9th percentile the right one?** It assumes the planet is
   at least 0.1% of the frame — about 1200 superpixels, so a disc 40px
   across. True for Jupiter and Saturn at this focal length, marginal
   for Mars away from opposition, wrong for Uranus. If AE behaves oddly
   on a small target, this is the first thing to suspect.
   `AnalysisResult::HIGHLIGHT_PCTILE` is the one constant to change.

AE's commentary now reaches `raduk_log.txt`. Every one of its
diagnostics used to be a `printf`, which a GUI-subsystem build discards
— so the running explanation of what AE was doing had been invisible for
its whole life, including while it was oscillating in the log above.

---

## Data, not code: five saved profiles hold the wrong resolution

Not a sky test, but it will bite on the night and takes a minute to fix.

`SAVE` records the live resolution, and the emulator used to force
1280×960 whatever was asked. So five of the eight profiles on disk hold
1280×960 instead of what their defaults intend:

| Profile | On disk | Intended |
|---|---|---|
| Jupiter, Saturn, Mars, Venus | 1280×960 | 640×480 |
| Moon | 1280×960 | 1920×1080 |

This was inert until Phase 85, when loading a profile started applying
its format. Now selecting Jupiter gives 1280×960 rather than the 640×480
that exists for the frame rate.

**Fix per profile:** load it, press the right resolution button, press
`SAVE`. Or leave them if 1280×960 is what you actually want — they may
be deliberate, since `[ROI]` crops to exactly that.

---

## What is already verified against hardware

For contrast, so this list is not read as "nothing works":

- Serial port auto-detect, and the mount accepting its site and clock.
- Autofocus end to end, reported by the owner: slew to Mars, manual
  focus, then `AUTO FOCUS` produced a sharp result.
- The ADC dispersion model, checked two independent ways — an Edlen
  implementation written from scratch and a test program compiled
  against the shipped code, agreeing exactly across four filter bands
  and three altitudes.
- The calibration arithmetic, the readiness logic, the capture-format
  round trip and the session restore, all by unit test or by driving the
  real controls from outside the process.
