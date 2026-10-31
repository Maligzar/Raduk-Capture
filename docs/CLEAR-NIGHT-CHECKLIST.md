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

## 9. AutoStakkert — was "known broken", and is not

**Withdrawn 2026-10-18. It works.** This section used to say
AutoStakkert would not start on this machine, that it parsed every
option and then never began processing, and that the next steps were a
reboot and a reinstall.

It was re-measured instead. Three runs on the same 158 MB recording,
with no reinstall and no reboot in between: standalone 51.4 s, through
the app's own launch path 50.4 s, and through the app's path again with
AutoStakkert already open 57.0 s — all three logging `Processing
started` about half a second after `started...`, which is the signature
of a working run.

What caused the original failure is unexplained and no longer
reproducible. One trap is worth carrying forward, because it looks
identical to the real thing: launched from Git Bash or another MSYS
shell, the `/switches` are rewritten into paths before AutoStakkert
sees them, and it then starts and sits. The log tells them apart — a
mangled launch shows **no `command line ...` lines at all**, where a
genuine failure-to-begin logs every switch and then stops. Reproduce
from PowerShell or `cmd`.

`AutoStackOnStop` still ships off, but for its own reason now: stacking
is minutes of CPU on the capture machine and should be opted into.

Not a night-sky item. Kept here only so the old claim is not found
again and believed.

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

## 13. The capture pipeline at real frame rates — **EXERCISED ON THE BENCH 2026-10-17**

> **This no longer needs a clear night to de-risk.** The emulator's
> 20 fps cap came off (P4.3), and with it the premise of everything
> below — the 62 ms "render cost" was `sleep_for` rounding to the
> Windows timer tick, not rendering.
>
> Measured end to end, with a real recording written to the capture
> drive:
>
> | | format | rate | throughput | dropped |
> |---|---|---|---|---|
> | Run 1 | 1280×960 | 57 fps | 67 MB/s | **0** |
> | Run 2 | 3840×2160 | 37 fps | **293 MB/s** | **0** |
>
> Run 2 is 4398 MiB in 15 seconds — 556 frames, every one of them in
> the file — and it is sitting on the disk's own measured ceiling of
> 292 MB/s. The run-sized write buffer and the SER writer both held.
>
> **What still wants a real night:** nothing about throughput. What is
> left is whether a real ASI678MC over real USB behaves the same, and
> that is a different question from whether our side keeps up. It does.
>
> **One thing worth knowing before you try the full sensor in the
> field:** the write buffer sized itself to 1000 slots = **7910 MB of
> RAM** at 3840×2160. That is the `RAM_SHARE = 0.5` rule working as
> designed, but it is a lot of memory to commit, and it is pre-touched
> at allocation.
>
> **And the frame rate is exposure-bound, not pipeline-bound.** Both
> runs above were capped by the profile's 17.5 ms exposure, which is
> 57 fps and no more — on the real camera too. Reaching 120 fps at
> 1280×960 needs an exposure at or under 8.3 ms. That is a real
> constraint of the rig, not an artefact of the bench.

## 14. Click to recentre, on the sky

Added 2026-10-18, and cited by `RadukCapture-Architecture.md` before it
existed here — which is why it is written down now.

`Ctrl`+click a target and the mount puts it in the middle. The
arithmetic is gated on the bench: `tests/recentre.cpp` solves the
pulses and pushes them back through the measured matrix for a square
camera, one rotated 90 degrees, a mirrored one and a skewed one, and
every click lands within a pixel of centre.

None of that is the question a night answers. **The matrix itself comes
from a real guide calibration on a real star**, and no bench test can
produce one. What a clear night settles:

- that a click lands the planet in the middle **first time**, not after
  three tries;
- that the ten-second pulse ceiling is the right place to stop and say
  "slew instead" — too low and ordinary recentres get refused, too high
  and a mistaken click walks the target out of frame;
- that the alt-az cos(altitude) rescale holds near the zenith, where
  the azimuth response collapses.

Record what the notification bar said and what actually happened. A
click that works is one line; a click that overshoots is the useful
measurement.

---

### The original entry

### 13 as it was first written — superseded, kept for the record

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


## 15. The pixel readout's row order, which is a coin flip until a solve happens

Added 2026-10-30 with phase 47b. This is the shortest item on the list
and the only one whose answer is a single boolean.

Hover the cursor over the image and the tooltip now names the RA and
Dec under it. Every part of that arithmetic is gated by
`tests/pixel_wcs.cpp` except one, and the one is not gateable here:
**FITS counts image rows from the bottom and a frame buffer counts them
from the top**, and whether ASTAP applies that flip to the top-down BMP
it is handed is not something a machine without ASTAP can find out.

It is a named flag, `PlateSolution::fitsBottomUp` in
`src/sky/PixelWCS.h`, defaulted to the FITS convention, and both
settings are pinned by tests. If it is wrong, the readout is reflected
about the frame's centre line: Dec runs the wrong way and the error
grows with distance from the middle, which is exactly the shape of a
pointing error and will not announce itself.

**The measurement, which takes about two minutes:**

1. Plate solve any field.
2. Hover near the TOP of the image and read the Dec. Hover near the
   BOTTOM and read it again.
3. Toward the top of the frame, Dec should be **higher** on an
   unmirrored, unrotated setup.

If it runs the other way, flip the default of `fitsBottomUp` and note
it here. One field, one line.

**Free with the same run, and worth more:** the rose in the top-left
draws two pairs of arrows. The amber N/E pair comes from the solve. The
green `jE`/`jN` pair is the measured guide calibration, so it owes
nothing to any convention. They should point opposite ways, because
driving the scope east slides the field west. If they disagree by more
than 25 degrees the overlay prints `ROSE nn° OFF` and the solve's
parity or the row order is wrong. **That warning appearing is the
finding**, so write down the number.

(The rose needs a guide calibration to show its green half, so this
pairs naturally with item 14.)

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

## 16. The visual system, in a dark room, which is the only venue that counts

Added 2026-10-31 with phase 96.3. Nothing on this item is a defect to
hunt. Phase 96 made three rounds of changes that a bench can prove
correct and cannot prove *right*, and this is the look taken outside.

**What a test already settled**, so there is no point re-checking it by
eye: the night palette's ordering, the type ramp's separation, the
elevation ordering, the letterbox geometry, and that a shadow's alpha is
identical in both modes. All gated.

**What only your eyes can settle.** Press `N` for night vision and look
at each of these:

1. **Is danger now the loudest thing on screen?** 96.1 moved `ACCENT4`
   to the top of the ladder at 240, up from a derived 103. In the old
   build the capture and danger colour was *sixty per cent dimmer* than
   the labels you are meant to skim. If it now shouts too much, the
   ladder's top is a number in `Theme::detail::kNightLadder` and it is
   one edit.

2. **Open the share view (`Shift+V`) on a bright target.** The caption
   now sits on a gradient scrim rather than straight on the image. On a
   full-frame Moon the old white-on-white was the case that justified
   it; check it has not become a black bar on a dark planetary frame
   instead. `audience::SCRIM_PEAK_ALPHA` is the dial.

3. **Letterbox something.** Select a small ROI so the image does not
   fill the screen, and check the corner marks read as a frame edge
   rather than as decoration. They exist because a dark frame centred
   in a black field has no visible boundary from three metres back; if
   that is not a problem you actually have, say so and they come out.

4. **Start recording and look at the REC capsule.** This is the one with
   a measured defect behind it: as a literal colour it derived to 86,
   where the ladder puts danger at 240, so in night vision the recording
   light was *dimmer than the caption beneath it*. It should now be
   unmissable.

5. **Open the object panel (`Shift+O`) on a planet, then on a deep-sky
   target.** The figures are raised cards now. Check the labels did not
   grow too far — they moved from 0.52 em to the ramp's smallest step,
   which is a thirty per cent jump, and `APPARENT SIZE` in a four-card
   row is the string most likely to crowd.

6. **The object's name.** `Type::DISPLAY` exists so this did not shrink.
   Confirm it is the same size it was, because if it is not, the ratio
   that was supposed to preserve it is wrong.

The honest position: the arithmetic is right and the taste is untested.
Anything on this list you dislike is a token in `Theme.h` or a constant
in `AudienceLayout.h`, which is the whole reason they are tokens.

---

## 17. The warning tiers, which were inverted in the dark until phase 96.4

Added 2026-10-31. Unlike item 16 this one is **not** a matter of taste.
It is a measured defect that was live in every build until now, and the
first thing to do with night vision on is confirm it is gone.

**What was wrong.** `ACCENT4`, the capture and danger token, is
`RGB(255, 59, 92)`. Eleven indicators across four files spelled
`RGB(255, 60, 90)` instead — one green unit and two blue units away.
`NightLuminance` keys on the RGB bytes, so a near-miss falls silently
through to the derived value, and those eleven painted at **103** where
the token is designed at **240**.

Worse, the status window's three warning tiers were *all* literals in
the same ternary. In night vision they read:

| Tier | was | now |
|---|---|---|
| critical | **103** | 240 |
| caution | 175 | 110 |
| good | 150 | 170 |
| secondary labels | 134 | 80 |

**Critical was the dimmest of the four.** The order was close to
reversed.

**What to check, with `N` pressed:**

1. **Start a recording.** The REC indicator on the status window should
   now be the brightest thing on the screen. It was previously dimmer
   than the labels beside it.
2. **Fill a disk or a write buffer** (or just read the thresholds in
   `StatusWindow.h`): below 20 GB free and above 80% buffer should both
   shout. Between 20 and 100 GB, and 50 to 80% buffer, is the caution
   tier.
3. **The mount's state lamp.** Its rim used to be a 35% blend toward
   white, which put **89 units of blue** into a night-vision display on
   the one surface read from the eyepiece. There should now be none at
   all. If the lamp looks flat rather than lit, `Theme::HighlightOver`'s
   half weight is the dial.
4. **The AUTO FOCUS and RECORD buttons.** Both are raised inside their
   own rects now, RECORD a level higher than the rest because its state
   has to be readable without looking for it. If either reads as a
   1990s bevel rather than as depth, the highlight is too strong
   relative to the shade.

### The decision this leaves you

`ACCENT3` (mount, live) is designed at 170 and `ACCENT2` (EAF,
secondary) at 110, so **"good" is now brighter than "caution"**. For a
warning tier that is backwards, and it is backwards because the ladder
was designed around device roles and the warning system borrowed it.

Fixing it means a caution rung between danger and primary text, and the
ladder is full: 240 down to 80 at the 24-unit minimum holds seven rungs
and six are taken, so inserting one re-spaces every signal and changes
the whole operator look in the dark. That is your call, not a side
effect of a palette pass, so 96.4 did not make it.

**What is still outstanding (96.4b).** Six near-identical font sizes
(11 through 16) still want collapsing onto the three ramp steps, and
the spacing literals onto `Space::`. Not done here because
`ControlPanel.cpp` has six measured-text calls in 2424 lines — it lays
out by hard pixel offsets, so growing a label inside a static sized to
fit clips it, and nobody in a container can watch that happen. Every
off-ramp site is marked in the source with `96.4b` and the step it
wants, so it is mechanical once you have the screen in front of you.

---

## 18. The first lunar mosaic, and whether the drift budget survives an 8SE

Added 2026-10-31 with phase 97. **Numbered 18, not 17:** item 17 was in
flight on a separate branch when this was written, and two items sharing
a number would have been worse than numbering around it. Both are here
now.

Phase 97 computed the whole framing problem and has not seen the Moon.
Everything below is arithmetic checked against almanac figures, which is
not the same as having done it.

### The numbers to confirm

| | |
|---|---|
| Full sensor at 0.203″/px | 12.99′ × 7.31′ |
| Moon tonight | 29.4′ to 33.5′, so 2.4 frames wide and 4.2 tall |
| Panels at 20% overlap | 3 × 6 = **18** |
| Drift under sidereal | 0.55″/s = **2.71 px/s** |
| Deadline before the overlap is gone | **80 seconds** |

1. **Confirm the apparent diameter against an almanac.** The app now
   computes it; check tonight's figure against Stellarium or any
   ephemeris. The series should be within about 0.5″. If it is out by a
   factor of two, the diameter and the semidiameter have been confused
   somewhere, which is the error the test exists to catch and would
   mean the test is checking something other than what ships.

2. **Watch the drift with sidereal tracking on.** Point at the Moon,
   leave the mount at sidereal, and time how long the limb takes to
   cross a known number of pixels. It should be 2.7 px/s. This is the
   measurement the whole phase rests on and it takes a minute.

3. **Then select the Moon as a profile and watch the log.** It should
   say `[PROFILE] lunar rate: set`. If it says `REFUSED by backend,
   still sidereal`, your ASCOM driver does not implement the
   `TrackingRate` property — which `ITelescopeV3` permits — and every
   mosaic plan below is void. Say so and the fallback becomes a
   different problem.

4. **Confirm the rate survives a GoTo.** This is the specific bug phase
   97 fixed: `GoToRADec` wrote sidereal on *every* slew, so setting
   lunar rate and then slewing to the Moon undid it. Set the rate, slew,
   and re-check the drift. It should be near zero now.

5. **Then select Jupiter.** The rate must go *back* to sidereal. Without
   that, the fix would drift Jupiter out at 0.55″/s — a bug introduced
   by the repair.

### Running one (97.5)

`Ctrl+Shift+M` starts a mosaic of the current target; the same chord
stops it. It will refuse, and say why, if there is no mount, no frame
yet, or the target has no measurable disc.

**Do the first one on a short target.** Pick something that needs three
or four panels rather than eighteen, so a problem costs a minute
instead of eight.

1. **Watch panel 1 finish, then panel 2 begin.** The log says
   `[MOSAIC] panel N of M recording`. Each panel is its own file.
2. **The thing to look for: do the panels actually MOVE?** The one
   failure that does not announce itself is a runner that believes a
   slew finished before it started and captures the same patch every
   time, producing a complete-looking mosaic of one panel repeated.
   There is a guard with a 1.5 s minimum and a test for it, but the
   only proof is eighteen files that differ.
3. **Press `Ctrl+Shift+M` mid-capture.** It should stop, and the file
   in progress should still be closed properly rather than left half
   written.
4. **Check the last panel against the first.** Over eight minutes the
   Moon moves 4.6 arcmin, and the runner recomputes every panel
   against where it is *now* — so the last panel should be correctly
   placed on the Moon, not 4.6 arcmin off it. If it is off, the live
   position is not reaching `Tick`.

### The real unknown: settle time

The 8.3-minute figure assumes **20 s of capture and 8 s of settle per
panel.** The capture time is yours to choose; the settle time is the
mount's, and nobody has measured an 8SE's settle after a 10′ slew at
this magnification.

If it is 20 s rather than 8, eighteen panels take 12 minutes and the
drift budget needs the lunar rate working rather than merely set. If it
is 3 s, there is room for more overlap.

**Measure it once:** slew one panel step and time how long until the
image stops moving. That single number decides whether lunar mosaics are
practical on this mount, and it is the one input to the plan that is
pure guesswork today.

### What not to bother with

A lunar mosaic at a **small ROI**. A 640×480 crop needs 432 panels,
which is 2.9 hours even after skipping the 56 that hold no Moon. The
answer is the full sensor.

### Solar

The arithmetic covers the Sun and the optics do not. An 8″ SCT pointed
at the Sun without a **full-aperture front filter** destroys the
corrector and the camera behind it; a rear-cell filter is not
sufficient, because the heat is concentrated before it reaches one.

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
