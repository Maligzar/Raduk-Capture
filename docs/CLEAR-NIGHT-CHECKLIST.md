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
  handles it — but no real RAW16 frame has been through it. Record a
  short 16-bit SER and confirm the file opens in your stacker with the
  expected bit depth.

  **This was outright broken until 2026-10-13 and is worth re-reading.**
  The RECORD button passed a hardcoded 8 as the bit depth, so in 16-bit
  mode the write buffer was sized at half what the camera sends. Every
  frame was rejected as too large, the writer consumed nothing, and the
  capture produced a **0-byte SER** while the UI showed it recording
  normally — right frame count on screen, nothing on disk. In 8-bit the
  hardcoded value happened to be correct, which is why it survived this
  long. Fixed, and verified in the emulator at 194 frames / 455 MB
  where it previously wrote 0 bytes. Still unproven against the real
  camera, which is why this item stays.
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

## 11. Histogram against a real sensor

**File:** `src/core/FrameAnalyzer.cpp`. Bench half done - see
`docs/HISTOGRAM-ACCURACY.md` and `tests/histogram_accuracy.cpp`.

The arithmetic is settled on the bench: channel assignment is correct,
clip percentages count what they claim in both 8- and 16-bit, and the
left-aligned RAW16 handling is right. Every one of those checks ran
against frames with **no noise at all**, which is the part sky fixes.

What to look at on a real target:

1. **Gr/Gb imbalance.** The analyser averages the two greens, so the
   histogram cannot show a split between them. If the colour looks off
   in a way the histogram does not explain, this is a candidate, and it
   needs a real sensor to appear at all.
2. **Does the G row look narrower than R and B?** It will, slightly, by
   construction - averaging two samples denoises only that channel. Worth
   seeing how large the effect is on real data before deciding whether
   to change it.
3. **Clip onset.** Saturation near full well is not sharp on a real
   sensor the way it is on a synthetic frame. Check that the clip
   percentage starts moving before the highlights visibly block up.
4. **The 99.9th-percentile highlight** that auto-exposure now drives.
   It assumes the planet is at least 0.1% of the frame - fine for
   Jupiter and Saturn here, marginal for anything smaller.

---

---

## 12. ADC alignment — only once there is a corrector to align

Dormant until one is bought. `ADC fitted` on the READY screen is off, so
the window, the preview arrow and the FOCUSER readout are all dark.

The arithmetic is settled on the bench — `tests/adc_alignment.cpp`
drives it with synthetic mosaics whose answer is known by construction,
and on a dispersion-free target it reads 0.01″ against a 0.05″ floor.
What the bench cannot answer:

1. **Does the residual actually fall as the prisms turn?** Everything
   above establishes that the number is the displacement in the frame.
   Nothing establishes that turning an ADC moves it the way the guidance
   text claims, because there has been no ADC to turn. The first night
   with one: point at something bright at 25-30° altitude, open
   `Shift+A`, and confirm the residual falls toward zero rather than
   wandering.

2. **Is the sense of the guidance right?** "Spread the prisms further
   apart" versus "back the prisms off" is derived from which side of the
   zenith axis the residual sits on. The geometry says blue toward the
   zenith is undercorrected. One real adjustment confirms or inverts it,
   and getting it backwards would have you turning the wrong way with
   the number telling you so only after the fact.

3. **Is 0.15″ the right threshold for ALIGNED?** It was chosen as
   roughly the seeing floor, so below it there is nothing left to chase.
   On a poor night the real floor is higher and the window will keep
   saying there is work to do.

4. **Does a real sensor's noise hold the measurement steady?** Every
   bench number came from frames with no read noise at all. The rolling
   median over 45 frames exists for exactly this and has never been
   tested against real noise.

5. **Does it work on a star as well as a planet?** Edge matching was
   chosen because the limb is the colour-neutral feature on a disc. A
   star has no limb, only a seeing-blurred profile, which should work
   but has not been tried.

## 13. The capture pipeline at real frame rates — never once exercised

**The largest untested surface in the app, and the one with the most to
gain.**

Everything about recording has only ever run at 15–20 fps, because that
is all the emulator can render — measured at 62 ms per synthetic frame,
against a hardcoded 50 ms cap it already overruns. The real camera is
specified far above that at 1280×960. So the entire write path has been
exercised at roughly a seventh of its working rate.

Two changes landed on 2026-10-14 off the back of that, and neither has
seen a real frame:

- **Recording moved off the render loop onto the capture thread.** It
  used to take one frame per render pass via `GetLatestFrame()`, and
  `Window::Run()` ends in a `Sleep(1)` measured at 15.98 ms — so the
  loop capped near 62 passes a second before any rendering work, 40–55
  with it. Everything in between was overwritten in the ring and lost.
- **The write buffer is sized to the run** rather than a flat 512 slots,
  so a bounded capture can be held entirely in memory and the disk comes
  off the critical path.

### The one measurement that settles it

**Does the frame count in the SER match the camera's delivered rate?**

Record a fixed duration with the frame limit set high enough not to
intervene, and compare:

```
frames in the SER   vs   (FPS shown during capture) x (seconds recorded)
```

Before the change those two diverged and nothing said so — the missing
frames were overwritten in the ring, which is not a drop and was never
counted. They should now agree to within a frame or two. If they do not,
the decoupling did not take.

### What else to read off the same run

1. **What rate does the camera actually give?** At 1280×960 RAW8, and
   again at RAW16. Nobody here has ever measured it. Every throughput
   figure below is quoted against guesses until this exists.

2. **Does `BUF` stay near zero?** The status window shows buffer
   occupancy. Near zero means the drive is keeping up. A steady climb
   means a sustained deficit, and the rate of climb is the size of it —
   that is the number worth writing down, not the fact that it climbed.

3. **Does `DROPS` stay at zero?** Non-zero means the buffer filled,
   which with a run-sized buffer means the drive fell behind for longer
   than the whole run. Note the drive, the format and the rate.

4. **Does the frame limit land exactly?** Ask for 3000 and count 3000.
   The limit used to be checked against frames *written* rather than
   frames *taken*, which overshot to 40 on a limit of 5 once submission
   outran the writer. Fixed and unit-tested, but the test drives it with
   synthetic frames and a 320×240 slot.

5. **Does the preview survive it?** The render loop no longer feeds the
   recorder, so at 120 fps it is displaying roughly one frame in three.
   That is the right trade and it should look no worse — but check it,
   because a preview that stutters at exactly the moment you are
   focusing is worse than a slower recording.

### What the bench already established, so the night need not

- The capture drive sustains **292 MB/s** writing frame-sized chunks the
  way `SERWriter` does, ranging **141–423 MB/s** second to second. At
  1280×960 that covers RAW8 to past 180 fps, and RAW16 to about 110.
  **Measured with the app idle**, though — not with five screens, the
  preview, the analyser and the assistant all running. Worth one
  re-check under real load.
- Per-frame sharpness scoring on the writer thread costs **0.079 ms**,
  about 1% of a core at 120 fps. Not a bottleneck.
- Submission from the capture thread costs **3.7 µs/frame**.

### Why this is worth a chunk of a clear night

Lucky imaging pays in frame count. Jupiter gives roughly three minutes
before derotation matters, and within that fixed window the only
variable that raises the stack quality is how many frames landed. At 120
fps that window holds ~21,000 frames; at the old ceiling, ~9,000.
Stacking the best 10% is 2,100 against 900.


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
