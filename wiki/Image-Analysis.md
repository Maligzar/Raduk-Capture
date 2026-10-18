# Image Analysis

Everything that looks at pixels and produces a number: histograms, the
sharpness metric, the seeing score, the centroid, the quality gate, the
quick-look stack, and the dark/flat masters.

This page carries more hard-won reasoning than any other in the wiki. Three
of the metrics here were measurably wrong at some point, and the source
records both the wrong version and why it was wrong. That record is worth
more than the current formulas.

---

## FrameAnalyzer

[`src/core/FrameAnalyzer.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/FrameAnalyzer.h) ·
[`src/core/FrameAnalyzer.cpp`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/FrameAnalyzer.cpp)

One below-normal-priority thread. `Submit()` is non-blocking and skips the
frame if the thread is busy — you want the latest result, never a queue.
Effective rate ~2 Hz.

### `AnalysisResult`

| Field | What it is |
|-------|-----------|
| `histogram[256]` | Luminance, brightness-weighted from the Bayer mosaic |
| `histR/histG/histB[256]` | Per-channel, demosaiced by position |
| `clipR/clipG/clipB`, `clipPct` | Fraction of pixels at 255 |
| `meanR/meanG/meanB`, `meanBrightness` | 0–255 |
| `sharpnessPct` | Normalised Laplacian variance, 0–100 |
| `sharpRaw`, `sharpNoise` | **Diagnostics.** Variance before, and the darkest-decile term — see below |
| `seeingScore` | Rolling 1–10, **relative** |
| `seeingHistory[60]` | For the graph |
| `centroidValid`, `centroidX/Y` | Planet lock input |
| `centroidTooFaint`, `centroidPeak` | Why there is no centroid, not merely that there is none |

### Per-channel histograms

RGGB demosaic by position:

| Channel | Bayer position |
|---------|----------------|
| R | even row, even column |
| G | mean of Gr (even row, odd col) and Gb (odd row, even col) |
| B | odd row, odd column |

Phase 37 added these. Before that the "R/G/B" histogram rows in the panel
were three tinted copies of the luminance histogram — they looked like
per-channel data and were not.

Bin 0–1 is skipped when drawing bars: dark-current noise piles up there and
a spike at bin 0 compresses everything else to invisibility.

---

## Sharpness

[`src/core/Sharpness.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/Sharpness.h)

One definition, shared by the live analyzer and the SER writer's sidecar.

> **Why there is exactly one copy.** Those two had drifted into measuring
> different things. The analyzer's kernel was fixed in v1.2.0 — it had been
> sampling the *immediate* neighbours of each pixel on a Bayer mosaic,
> which belong to **different colour channels**, so it computed a
> red-minus-green colour difference and barely responded to focus at all.
> Nothing stops that happening again to a second copy, so there is now only
> one.

### The two rules the Bayer layout imposes

Both were got wrong before:

**1. Taps must be ±2 pixels, not ±1.** On an RGGB mosaic the immediate
neighbours of a red pixel are all green. A ±1 kernel measures the colour
difference and the mosaic pattern rather than image detail.

**2. The sampling lattice must be anchored to even coordinates.** The crop
origin follows the planet, so an odd start flipped which colour channel was
being measured every time the planet moved one pixel, and the score jumped
between two unrelated values.

```cpp
const uint32_t sx = (x0 + 2) & ~1u;   // even anchor
const uint32_t sy = (y0 + 2) & ~1u;

const float lap = 4.0f * c
                - px8(x - 2, y) - px8(x + 2, y)
                - px8(x, y - 2) - px8(x, y + 2);
```

RAW16 is right-shifted by **8**, not 4: ASI RAW16 is left-aligned in the
word.

The crop is 256×256 (`halfCrop = 128`) centred on the last known centroid,
falling back to frame centre after `CENTROID_HOLD_FRAMES = 30` misses
(~0.7 s at 43 fps) so it does not chase a stale position.

### The noise floor that does not work, and why it is still computed

This is the most instructive comment in the codebase. A Laplacian is a
high-pass filter, so broadband noise dominates it — with this kernel,
independent per-pixel noise of variance σ² contributes 20σ² by itself. The
obvious fix is to estimate the noise from the sky and subtract it.

It does not work here. Measured, frozen scene, properly exposed to 200 DN
peak:

| Sensor noise | Raw variance | Derived "floor" | Net |
|--------------|--------------|-----------------|-----|
| 0 DN | 32.5 | 35.2 | **−2.7** |
| 6 DN | 216.2 | 266.0 | **−49.7** |
| 14 DN | 1201.8 | 1723.3 | **−521.4** |

> **The zero-noise row is the proof:** a frame with *no noise whatsoever*
> still yields a "noise floor" larger than its entire variance.
>
> The cause is the target, not the estimator. A planetary disc routinely
> **fills** the 256×256 crop — the emulated disc is 422 px across — so
> there is no sky inside the crop at all. What gets measured as
> "background" is then the darkest decile of the *disc*, whose gradients
> are real surface structure. Subtracting it removes signal, not noise.
>
> Clipping was the earlier suspicion and it was wrong: exposing the disc
> properly changed nothing.
>
> The right method for a target that fills the frame is **temporal** —
> noise is what differs between consecutive frames at the same pixel,
> while structure does not. That needs frame history, which this function
> does not have, so it belongs in `FrameAnalyzer` if it is ever wanted.

So the figure is computed and reported as `sharpNoise`, a **diagnostic
only**, and never subtracted. The `[SHARP]` log line calls it the
darkest-decile gradient level rather than "noise", which was only ever what
it had been built to estimate.

### The ceiling, which was the real problem

```cpp
inline constexpr double SHARP_CEIL = 1000.0;
inline float SharpnessPercent(double variance) {
    const double pct = variance / SHARP_CEIL * 100.0;
    return (float)(pct < 100.0 ? pct : 100.0);
}
```

The history: 8000 was tuned against the broken colour-difference kernel.
100 was then calibrated on a **noiseless emulator frame**, where at-focus
variance is ~45. Switch on 10 DN of sensor noise — which any real camera
has — and at-focus variance is ~610.

> The gauge pinned at 100% across three consecutive autofocus samples. The
> peak became a plateau with no curvature to fit, and **the autofocus
> result went from 19 steps off true focus to 500.**

1000 leaves headroom for a noisy real frame while keeping a clean one on
scale. Only the *shape* of the curve matters to autofocus; the ceiling just
has to not clip it. `main.cpp` now self-checks the ceiling and logs
whether readings are crowding the top or the bottom, with the recommended
direction to change it.

---

## Seeing score

Rolling window of 20 sharpness samples (~10 s at 2 Hz), mapped to 1–10.
**Relative, not absolute:**

```cpp
double best = 0.0;
for (float v : m_sharpHistory) if (v > best) best = v;
if (best > 1e-3) {
    double rel = r.sharpnessPct / best;
    if (rel > 1.0) rel = 1.0;
    r.seeingScore = 1.0f + (float)(rel * 9.0);
}
```

> **Why relative.** It used to be `1 + pct/100*9`, which tied it to
> `SHARP_CEIL`. When that ceiling was recalibrated the score silently
> collapsed into a 2.5–2.9 band — it can only reach 10 if sharpness reaches
> 100%, which a real in-focus frame does not. **And the capture gate
> compares against this number, so a gate set to 6 would have rejected
> every frame.**
>
> Relative is also the more honest definition. What the gate wants to know
> is "is this frame good compared with what this target is currently giving
> me", which is scale-free: immune to the next recalibration, to the
> target's brightness, and to how noisy the camera is.
>
> The cost: a uniformly poor night still reaches 10 at its best moments.
> Correct for gating — you cannot record frames better than the sky is
> offering — but it means the score is **not** a measure of how good the
> night is in absolute terms. Read `sharpnessPct` for that.

**How to read it at the eyepiece:** watch how far it *dips*. Steady air
stays near 9–10; poor air swings down to 5 or below.

---

## Centroid detection

Brightness-weighted centre of mass over the **full frame**, not a central
window.

> **Why the full frame.** Stage 1 (Searching) must find the planet wherever
> the GoTo pointing error left it. A central 50% window misses anything in
> the outer region, which is exactly where a cold GoTo puts it.

A threshold isolates the bright disc from background and stars. Below
`CENTROID_MIN_PEAK = 20` (8% of 255) no centroid is attempted:

> Below this a frame is noise, and a centroid computed from it would be a
> random walk.

Phase 69 split the failure into two reportable cases, because they need
opposite responses from the observer:

| Case | Flag | What the observer should do |
|------|------|------------------------------|
| Brightest pixel under the gate | `centroidTooFaint = true`, `centroidPeak` carries the level | Raise exposure or gain |
| Nothing resembling a target | `centroidTooFaint = false` | Go and find the planet |

Before this, acquisition stalled identically and silently for both.

---

## The quality gate

Applied in the render loop. Compares the rolling `seeingScore` against the
`MIN SEEING` threshold from the capture panel, and only submits passing
frames to the recording. The indicator goes amber ("GATED", holding) or
green ("STOP", passing).

Because the seeing score is relative, a threshold of 6 means "frames in
roughly the better two-thirds of what the last ten seconds offered", not
"frames of absolute quality 6". That is the behaviour you want, but it is
not what the number looks like it means.

---

## Quick-look stacker

[`src/core/QuickLookStacker.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/QuickLookStacker.h)

A **display-only** aligned, quality-gated preview stack, so you know that
night whether a capture was soft. Nothing here touches what is written to
the SER.

> **Why it exists.** `DisplayFX::Stack3/5/10` averaged every recent frame
> at fixed pixel indices. Seeing jitter is 2–5″, and at 0.203″/px that is
> 10–25 px of smear — so the result was **softer than a single frame**
> while the HUD said "STACK 10f". Lucky imaging exists to reject the mean;
> that code computed it.

Two things make it honest:

1. **Only frames in the top ~15%** of the recent distribution are admitted,
   so the stack is the good frames, not all of them.
2. **Each admitted frame is shifted** so the planet registers, so detail
   adds instead of smearing.

It holds **no pixel buffers** — `PreviewRenderer` owns those, so the
dimension-change teardown in `SetFrameSize` stays the single place that has
to get reallocation right. (That was a heap overflow once already, Phase 56
review G1.)

### Constants, with reasoning

| Constant | Value | Why |
|----------|-------|-----|
| `HISTORY` | 128 | ~1.5–4 s at the 30–100 fps the preview sees |
| `COLD_START` | 32 | Admit everything until the distribution means something. Those frames are already *aligned*, so even the cold start beats the old code |
| `PERCENTILE` | 0.85 | Keep the top ~15% |
| `MAX_SHIFT` | 64 | Further than seeing can account for — a jog, a slew, a bumped tripod, a failed centroid. 64 buffer px ≈ 13″ at the real plate scale |
| `JUMP_CONFIRM` | 3 | Consecutive agreeing outliers before the reference moves. Three at ~40 fps is under a tenth of a second, so a genuine slew is picked up immediately in human terms, **while a lone bad centroid cannot move the reference at all** |
| `JUMP_AGREE` | 8 | How close those outliers must be, in superpixels, to count as agreeing |
| `MIN_PEAK` | 20 | Matches `AnalysisResult::CENTROID_MIN_PEAK` |
| `REF_ALPHA` | 0.02 | Reference tracks mount drift (~0.3″/s) but **not** seeing jitter, which decorrelates in 0.5–1 s. Time constant ~50 admitted frames (~6 s), five times the stack's own window, so the reference cannot chase the frames being aligned to it |

### The stacker's own metric is different from `sharpnessPct`

```
S = mean|grad G| / mean G   over a window centred on the planet
```

> **Why not Laplacian variance here.** This metric is dimensionless and
> exactly invariant to `G → k·G`: numerator and denominator both scale by
> k. A Laplacian *variance* scales as k², so it reads a brightness change
> as a sharpness change — one capture peaking at 29k ADU against 54k for
> the others would look 3.4× softer when nothing about focus moved.

Differences are taken at ±1 **superpixel** (2 buffer px), because
`DebayerRGGB` writes each 2×2 block as four identical values — a 1-px
difference is identically zero and would measure the block lattice rather
than the image. Sampled on **green**: it is the Bayer-averaged channel and
has the best SNR of the three.

---

## Dark and flat masters

[`src/core/CalibrationFrames.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/CalibrationFrames.h)

```
corrected = (raw - dark) * channelMean(flat) / flat
```

A dark records what the sensor reports with no light reaching it: thermal
signal and hot pixels. A flat records how unevenly the optics deliver
light: vignetting and dust shadows.

Masters are built **in the application from the camera** — 32-frame average
— and stored as raw bytes with the metadata needed to tell whether they
still apply.

> **Why not load a master from a PNG, as the original phase spec said?** A
> PNG is debayered 8-bit RGB. It has already been through the very pass
> this correction has to happen *before*, and its Bayer mosaic is gone. A
> dark that is not in the same raw layout and the same bit depth as the live
> frame cannot be subtracted from it at all.

> **Why the flat is normalised per Bayer position.** The four positions of
> an RGGB mosaic sit under different filters and have genuinely different
> sensitivities, so a flat's *global* mean is not the level any single
> position should be corrected towards. Dividing by one global mean would
> push every channel towards the same level and **bake a colour cast into
> every frame** — correcting vignetting at the cost of white balance. Each
> position is normalised by its own mean instead.

### What they do not touch

**The preview only.** The recorded SER keeps raw sensor data, because a
stacker does this better with the full frame set, and raw data stays useful
when a master turns out to have been wrong.

**The sharpness metric also reads raw.** Hot pixels *do* inflate a
Laplacian, so dark-subtracting before analysis would genuinely improve
focusing — but it would also move the capture gate and the `SHARP_CEIL`
scale under a system tuned against raw frames. That is a change to make
deliberately and measured, not as a side effect of this one.

Master building is fed from the live stream **before the quality gate**,
because a master is built from what the sensor reports and must not be
filtered by a gate meant for pictures of planets.

---

## AutoStakkert handoff

[`src/core/AutoStakkertRunner.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/AutoStakkertRunner.h)

Phase 77. `K`, or `AutoStackOnStop` in `rig.ini` (off by default — stacking
is minutes of CPU and should be opted into, not sprung on someone
mid-session).

AS!4 has a complete headless command line that is essentially
undocumented. The published switches are the AS!2-era `/inifile` and
`/expanding`, but the binary carries `/inifile`, `/outputdirectory`,
`/settingsfile`, `/apgrid`, `/process`, `/hide`, `/exit`,
`/surfaceanchor`, `/monitoring`, `/monitoring_no_raw`, and four
`/priority_*` switches, each with its own log message.

Verified against AS!4 4.0.13 on a real 156 MB, 127-frame recording:

```
AutoStakkert.exe "<file>.ser" /apgrid /process /hide /exit
                 /outputdirectory "<dir>"
```

Ran with no window in 49.9 s and wrote `AS_P20/P50/P70/P90`, each with a
`.as3` session file and a stacked image, using 272 auto-placed alignment
points. Which percentages and which image format appear is governed by AS's
own saved settings, so the result is **discovered by listing the output
directory** rather than assumed.

> **Status — it works. Re-measured 2026-10-18.** This was recorded for a
> while as "verified from a shell, does not work from inside the app". That
> is withdrawn. Three runs on the same 158 MB recording: standalone 51.4 s,
> via the app's own `StackAsync` 50.4 s (`ok=1`, 8 files), and via
> `StackAsync` with AutoStakkert already open 57.0 s (`ok=1`, 8 files). All
> three log the working signature — `Processing started` 0.45 s after
> `started...`.
>
> What caused the original failure is unexplained and no longer
> reproducible. One trap worth knowing: launched from Git Bash or another
> MSYS shell, the `/switches` are rewritten into paths before AS sees them,
> which produces the identical symptom — and the log then shows **no
> `command line ...` lines at all**, which is how to tell the two apart.
> Reproduce from PowerShell or `cmd`.
>
> `AutoStackOnStop` still defaults off, because stacking is minutes of CPU
> and should be opted into. See the source header and
> [Roadmap and Phase History](Roadmap-and-Phase-History).

---

## Reading the numbers

| Reading | What it is | What is good |
|---------|-----------|--------------|
| `LIVE` / `STACK` | Sharpness of one frame, and of the stacked frames | **Relative only.** No universal good value. Watch whether it rises or falls as you change focus. Higher is sharper |
| `Seeing n/10` | How this moment compares with the best frame of the last few seconds | **Relative.** Watch how far it dips: steady air stays 9–10, poor air swings below 5 |
| `FPS` | Frames per second captured | Higher is better — more frames to choose from |
| `DROPS` | Frames lost before reaching disk | Should be **0**. Anything else is a USB or disk problem, and those frames are gone |
| Focuser position | Raw step count | Meaningless alone. Only the number at best focus matters, and it moves with temperature |

**The most important thing about the sharpness numbers:** they are only
comparable to other readings taken the same night on the same target. They
are not a score out of anything, and they changed scale in v1.2.0 — numbers
remembered from earlier sessions no longer compare.

---

**Next:** [Autofocus](Autofocus) · [Tracking and Guiding](Tracking-and-Guiding) · [Rendering and UI](Rendering-and-UI)
