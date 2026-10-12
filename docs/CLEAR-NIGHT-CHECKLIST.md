# What needs a clear night

Written 2026-10-11, at Phase 88. This is the debt that cannot be paid on
the bench: numbers calibrated against a simulator that has no atmosphere
and no real sensor, and code paths the real camera has never executed.

Everything here already works in the emulator. That is the problem — the
emulator is where these numbers came from, so it agrees with them by
construction.

**Order matters.** Items 1–3 should be done before a serious capture,
because each one can silently bias everything recorded afterwards.
Items 4–8 can be checked whenever there is a spare minute.

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

**What to do:** get a planet roughly focused and watch the `[SHARP]` log
line through a focus sweep.

| What you see | Meaning | Action |
|---|---|---|
| `pct` reaches 90–100 and sits there | ceiling too low, curve is clipped | raise `SHARP_CEIL` |
| `pct` never exceeds about 10 | ceiling too high, all resolution lost | lower `SHARP_CEIL` |
| `pct` sweeps smoothly across roughly 5–80 with a clear peak | correct | leave it |

Only the **shape** of the curve matters to autofocus. The ceiling just
has to not flatten it.

## 2. Guide rate in `rig.ini` — assumed, never measured here

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

**What to do:** plate-solve one frame with ASTAP (`[GOTO]` → solve). The
solver reports the true scale. If it disagrees with 0.203 by more than a
few percent, set `PlateScaleArcsecPx` to what the solver says.

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

## 7. The motion plot's camera source

**File:** `src/gui/MotionMonitor.h`

The mount source is proven. The **camera** source — deriving motion from
the tracked centroid rather than from mount reports — has only ever seen
a simulated planet on a simulated mount.

**What to do:** lock onto a real planet, switch the plot to CAMERA, and
give the mount a single FINE nudge. The trail should move by roughly the
commanded amount. This is the feature that exists to answer "did that
small command do anything", so it needs to be right.

## 8. Known broken, and not ours

**AutoStakkert will not start on this machine.** It parses every option
correctly and then never begins processing. Proven not to be
RadukCapture: the identical command fails from a bare command prompt,
and so does the exact command that worked earlier the same day.
`AutoStackOnStop` ships off because of it.

Next steps are reboot, then reinstall. Not a night-sky item, but it
blocks the stacking handoff whenever it is next wanted.

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
