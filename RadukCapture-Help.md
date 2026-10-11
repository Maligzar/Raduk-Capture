# RadukCapture Help & Reference
### Built for the stars

---

## Read this first — safety

This telescope is shared with family, including children. These rules come
before everything else in this document.

**Never point the telescope at the Sun.** Not for a moment, not "just to
check", not with sunglasses on. An 8-inch telescope gathers roughly 800 times
more light than an eye does. Looking through it at the Sun destroys sight
instantly, permanently, and without any pain to warn you. It will also cook
the camera and can set the inside of the tube smouldering. There is no safe
way to do this with this equipment and no accessory here that makes it safe.
**If the Sun is above the horizon, the telescope stays capped and parked.**

**Wait until it is properly dark.** If the Sun is visible anywhere in the sky,
it is not time yet.

**Never push, pull or twist the telescope by hand while it is powered on.**
It moves itself with motors, and forcing it strips the gears — an expensive,
permanent repair. To move it, use the arrow keys or the hand controller.

**The stop key is `Space`.** `Esc` does the same thing. Either one halts all
telescope movement immediately, from any window, at any time. If anything at
all looks wrong — the telescope moving when it should not, a cable pulling
tight, a noise you do not recognise, or you are simply not sure — **press
`Space` first and work out why afterwards.** Nothing is damaged by stopping.
Plenty can be damaged by hesitating.

**Mind the cables.** Several cables run from the telescope to the computer. As
the telescope turns to follow the sky they can wrap around the mount or pull a
plug out mid-session. The app watches for this and warns you (see *Cord wrap*),
but look at the actual cables now and then. Never let one go taut.

**It is heavy and top-heavy.** The tube alone is about 6 kg on a tripod that
can tip if knocked. Do not lean on it, hang anything from it, or let anyone
swing on the tripod legs. Keep the legs where nobody will trip over them in
the dark.

**Use red light.** A white torch or a phone screen at full brightness ruins
everyone's night vision for about twenty minutes. Press `N` for the app's
night-vision mode.

---

## First night: a walkthrough for someone who has never done this

Written for someone who has never used a telescope. Follow it in order. You do
not need to understand any of the words to get a picture of a planet.

**What you are doing:** pointing a telescope at a planet, getting it sharp, and
recording a short video of it. Later, software stacks the good frames of that
video into one clean photograph. You are not taking a single photo — you are
collecting thousands of slightly-wobbly ones.

**Why it wobbles:** you are looking through 100 km of moving air. The planet
boils and shimmers. This is called *seeing*, and it is the reason for all the
machinery here. Nothing is broken when the image wobbles.

### 1. Put the telescope outside early

An hour before you want to look, carry the telescope outside and take the cap
off. Glass and metal that are warmer than the night air make heat ripples
inside the tube that blur everything. Nothing you do later fixes this, so give
it time. Forty-five minutes is a minimum; ninety is better.

### 2. Switch on and start the app

Power the mount, plug the camera and focuser into the computer, then start
RadukCapture. It finds the equipment by itself.

If something is missing it says so plainly and tells you which cable to check.
A list appearing is normal and not a failure — read it, fix what it names, and
press **Re-check now**. The list updates live, so you can stand at the
telescope and watch a line turn green as you plug something in.

### 3. Teach the mount where it is pointing

The telescope does not know which way it is facing when it wakes up. Until it
does, it cannot follow anything and **it will refuse to track**.

On the hand controller: **Align → Quick Align**, and accept the prompts. This
needs no stars and takes under a minute. If you skip this, everything later
appears to work and then quietly does not — this is the single most common
reason a night produces nothing.

### 4. Choose something to look at

Press `G` for the GoTo panel. It lists what is up tonight, brightest first,
and greys out anything below the horizon.

**Good first targets:** the Moon (enormous, bright, impossible to miss),
Jupiter, Saturn. **Not for a first night:** anything faint or fuzzy — galaxies
and nebulae need a different technique entirely.

Pick one and press **GoTo**. The telescope swings round. Let it finish.

### 5. Find it on the screen

Look at the preview. You may see the planet immediately, or you may see
nothing at all, which is normal — the camera sees a patch of sky about the
width of a pencil held at arm's length.

If the screen looks empty:

- Press `B` a couple of times for `BOOST ×2` and `×4`. This brightens the
  display so a dim smudge becomes visible. It does not change what is recorded.
- Nudge the telescope with the **arrow keys** and watch for something moving
  through the frame.
- Open `MOUNT CTRL` and watch the motion plot at the bottom while you
  press an arrow key. If the trail does not move, the telescope is not
  moving — which is a different problem from being pointed at the wrong
  patch of sky, and worth knowing before you hunt any further.
- Check that the lens cap is off. This catches everybody eventually.

A planet looks like a small bright blob at this stage, probably fuzzy. That is
expected — it is not focused yet.

### 6. Get it sharp

Press `A` for autofocus. The focuser steps through a range, measures sharpness
at each position, and drives to the best one. **It takes about a minute. Let it
finish.**

When it is done it reports what it found. If it says the focus got worse and it
has gone back to where it started, that is the app being honest rather than
failing — read what it suggests and try again. If it says focus was *not
bracketed*, the true focus lies outside the range it searched: focus roughly by
hand first, then run it again.

You can also focus by hand with `,` and `.` while watching the picture.

### 7. Watch before you record

Press `B` until the display says `STACK 10 BEST`. The app now keeps the
sharpest recent frames, lines them up, and averages them. The picture goes
quieter and detail that was lost in the shimmer appears — Saturn's rings
separating from the disc, cloud bands on Jupiter.

Two numbers appear top-right: `LIVE` for the single frame, `STACK` for the
combined one. **Higher means sharper.** Watch `STACK` for half a minute. If it
is drifting down, the air is getting worse, and waiting is better than
recording.

### 8. Record

Set a length — a minute or two is plenty — and press **RECORD**. Display
effects switch off automatically so the raw frames are saved untouched.

Then stop, and you are done. The video is in the captures folder, ready for
stacking software.

### If you get stuck

Everything in *Troubleshooting* below is written as a symptom you would
actually notice, not a technical fault name. Look for the one that sounds like
what you are seeing.

---

## What the numbers mean

A beginner will reasonably ask whether a number is good. Honest answers:

| Reading | What it is | What is good |
|---------|-----------|--------------|
| `LIVE` / `STACK` | Sharpness of one frame, and of the stacked frames. A gradient measure, corrected for noise and brightness. | **Relative only.** There is no universal good value. Watch whether it rises or falls as you change focus or as the night goes on. Higher is sharper. |
| `Seeing n/10` | How this moment compares with the best frame of the last few seconds | **Relative, not absolute (v1.2.2).** It touches 10 whenever a frame is the best seen recently, so it does NOT say whether tonight is a good night - it says whether right now is a good moment. Watch how far it DIPS: steady air stays near 9-10, poor air swings down to 5 or below. |
| `FPS` | Frames per second being captured | Higher is better — more frames to choose from. Shorter exposures raise it. |
| `DROPS` | Frames lost before reaching the disk | Should be **0**. Anything else means a USB or disk problem, and those frames are gone. |
| `TRK` | Whether the mount is following the sky | Must say **ON**. If it says OFF, the planet drifts out of frame within a minute or two. |
| Focuser position | Raw step count of the focus motor | Meaningless on its own. Only the number at best focus matters, and it changes with temperature. |
| Motion plot `"/s` | How fast the telescope is moving right now | Zero when parked with tracking off. Any non-zero value while you hold an arrow key means the command is getting through. |

**The most important thing to understand about sharpness numbers:** they are
only comparable to *other readings taken the same night on the same target*.
They are not a score out of anything, and they changed scale in v1.2.0, so
numbers remembered from earlier sessions no longer compare.

---

## Notes for the assistant

This file is loaded as reference context for the AI assistant. If you are that
assistant, helping someone who does not know this equipment:

**Safety is not negotiable.** If the Sun is up, or could be, refuse to help
point the telescope anywhere and say why. If the person describes a cable
pulling tight, the mount making a grinding noise, or movement they did not
ask for, tell them to press `Space` before anything else.

**Assume no vocabulary.** Say "the knob that moves the focus" rather than
"the EAF", "how steady the air is" rather than "seeing", unless they use the
term first. Explain a word the first time you need it, once.

**Check alignment early.** When someone reports the planet drifting out of
frame, that the mount will not track, or that `TRK` shows OFF, the cause is
almost always that Quick Align was never run. Ask before suggesting anything
more elaborate.

**Give one step at a time** and say what they should expect to see, so they can
tell whether it worked. "Press `A`. The focuser will hum and move for about a
minute — let it finish" is useful. A list of six steps is not.

**Do not invent numbers.** There is no good value for the sharpness readings.
If asked whether a reading is good, explain that it is relative and tell them
what to compare it against.

**Dim results are usually the sky, not the rig.** Poor seeing, a tube that has
not cooled, or a target low to the horizon explain most disappointing nights,
and no setting fixes them. Say so rather than suggesting changes that will not
help.

**Be straight about failure.** This app reports honestly when autofocus made
things worse or when a check did not pass. Relay that plainly instead of
softening it — a beginner who is told everything is fine when it is not learns
to distrust the whole thing.

---

## Quick Key Reference

Keys work **whatever window or panel has focus**. Before v1.1.5 they only
reached the mount while the main window was focused, so clicking a text field
or opening Help silently killed them until you clicked the preview again.

Both `Esc` and `Space` stop the mount, and both work unconditionally. Until
v1.1.8 `Space` was listed here but bound to nothing, and `Esc` exited
fullscreen instead of stopping - so there was no stop key at all while
fullscreen.

| Key | Action |
|-----|--------|
| `Space` | Emergency stop all mount movement |
| `Esc` | Emergency stop (works in fullscreen too; `F11` exits fullscreen) |
| `Arrow keys` | Jog the mount (hold to slew, release to stop) |
| `?` | Toggle this Help window |
| `A` | Toggle Autofocus (start / abort) |
| `B` | Cycle Display FX (Normal → Boost ×2 → Boost ×4 → Stack 3/5/10) |
| `C` | Toggle Clipping Warning Overlay |
| `F` | Toggle Fullscreen |
| `G` | Open GoTo panel |
| `H` | Cycle Channel Mode (All → Red → Green → Blue → All) |
| `M` | Open Map (Sky Map) |
| `N` | Toggle Night Vision Mode |
| `P` | Pause / Resume recording |
| `Q` | Toggle Quality Bar Graph |
| `R` | Toggle Reticle |
| `S` | Save Snapshot (PNG) |
| `V` | Toggle RGB Align |
| `X` | Toggle Pixel Value Readout |
| `Z` | Cycle Zoom (FIT → 50% → 100% → 200%) |
| `K` | Stac**K** the last finished recording in AutoStakkert |
| `↑↓←→` | Jog mount (hold to slew) |
| `+` / `-` | Fine focus step |
| `[` / `]` | Coarse focus step |
| `Esc` | Close popup panels |
| Mouse wheel | Zoom preview in/out |

---

## Panels Overview

### Toolbar (top strip)
Nine buttons across the top of the preview window:

| Button | Key | Function |
|--------|-----|----------|
| `[CENTROID]` | — | **Acquire the planet**: find, calibrate, centre, lock. Press again to cancel. |
| `[LOCK]` | — | Manual override — force the calibration step |
| `[ROI]` | — | Crop the sensor around a locked planet (not a freehand ROI — drag a box on the preview for that) |
| `[C] COLOR` | — | Toggle COLOR / RAW debayer mode |
| `[*] MAP` | `M` | Toggle Sky Map window |
| `[I] GOTO` | `G` | Open GoTo panel |
| `[+] RETICLE` | `R` | Toggle crosshair reticle overlay |
| `[CLIP]` | `C` | Toggle clipping warning (red flash on saturated pixels) |
| `[?]` | `?` | Toggle this Help window |

Buttons highlight (bold) when their feature is active.

---

## Control Panel Sections

All sections are collapsible — click the section header to expand/collapse.

### IMAGE
- **ZOOM: FIT / FULLSCR** — Switch between fit-to-window and fullscreen preview.
- **RGB ALIGN** — Toggle atmospheric dispersion correction on the display. Auto-computes pixel shift from mount altitude using the Edlen refractivity model. Blue channel shifts UP, red shifts DOWN by half. Display only — SER data is unaffected.
- **Manual slider** — Fine-tune the RGB align offset ±20px on top of the auto value. Center position = 0px offset.
- HUD shows `RGB ALIGN +N px` in amber when active.
- **[FX] NORMAL** — Cycle Display FX modes (see Display FX section below). Also `B` key.
- **[N] NIGHT** — Toggle Night Vision Mode (red-tinted preview for dark adaptation). Also `N` key. HUD shows `NIGHT VISION` in red when active.
- **[H] CH: ALL** — Cycle single-channel display mode: All → Red → Green → Blue. Shows selected channel as grayscale. Display only — SER data unaffected. Also `H` key. HUD shows channel label in cyan when active.

### PROFILE
- Load, save, and edit named target profiles (e.g. Jupiter, Saturn, Moon).
- Profiles store: gain, exposure, gamma, white balance, output directory, frame prefix, capture limits, and gate threshold.

### CAMERA
The **Exposure** slider covers **0.001 ms to 60 ms**, which spans planetary
work including the auto-exposure assistant's 50 ms ceiling. Longer exposures
are set through the profile editor, which accepts up to 60 s.

- **Gain** — ASI678MC gain (0–600). HCG threshold at 182 — above this gain, the camera switches to high-conversion-gain mode.
- **Exposure** — Exposure time in ms (0.1ms–10000ms).
- **Gamma** — Display gamma adjustment (1–100, default 50).
- **WB Red / WB Blue** — White balance offsets for the red and blue channels.
- **USB** — USB bandwidth limit (40–100%). Reduce if you see USB errors during recording.
- **AE OFF / AE ADV / AE AUTO** — Auto-Exposure modes:
  - `AE OFF` — Manual control only.
  - `AE ADV` — Advisory mode: suggests gain/exposure changes but does not apply them.
  - `AE AUTO` — Automatically adjusts gain and exposure to target 50–70% histogram peak.

### FOCUSER
- **Position readout** — Current EAF stepper position (0–500000).
- **`<< < STOP > >>`** — Step buttons: 1K / 10 / manual / 10 / 1K steps.
- **Input box** — Type a target position and click `GO TO` for a direct move.
- **TEMP** — Current EAF temperature in °C.
- **AUTO FOCUS** — Runs the built-in V-curve autofocus routine. Steps outward, measures sharpness at each position, fits a parabola, moves to the computed best focus. Abortable mid-run.
- **ADC: severity (N.N")** — Live atmospheric dispersion measurement. Computed from mount altitude using the Edlen 1966 model, with air pressure derived from your site elevation (T=288K). Shows NONE / MINOR / MODERATE / SIGNIFICANT / SEVERE. Use as a guide for when to apply an ADC or use RGB Align.

### TELESCOPE
- **Mount name / COM port** — Celestron 8SE over the NexStar serial protocol
  (not ASCOM). The port is auto-detected; the one in use is shown here.
- **Az / Alt / RA / Dec** — Live mount coordinates. Azimuth was read from the
  mount and discarded before v1.1.6, so these showed `--`.
- **Cord wrap** — Appears beside the azimuth once the mount has turned more
  than 30° since the cables were dressed, e.g. `184.2°   wrap +210°`. See
  *Cord wrap* below.
- **CABLES OK** — Press after dressing the cables. Zeroes the wrap count, which
  every later warning is measured from.
- **SETUP** — Opens the rig setup view: a plan of where you stand relative to
  the mount, the keep-out sector and the wrap limit, drawn so the stored
  numbers can be checked against the actual garden.
- **READY** — Reopens the readiness check at any time. It appears by itself
  at startup only when something is wrong; this is how you get back to it
  after fixing whatever it flagged.
- **Acquisition status** — While acquiring, this line shows the stage and what
  is holding it up, e.g. `SEARCHING - planet too faint to detect (frame peak
  3%, needs above 8%)`. It updates continuously, not only when you press
  something.
- **MOUNT CTRL** — Opens the floating mount controller (always-on-top).
- **TRK ON/OFF** — Tracking state toggle.
- **PLANET LOCK** — Engages planetary centroid lock mode.
- **TGT / GOTO** — Type a target name (e.g. Jupiter) or coordinates and slew.
- **POINT SOLVE → SYNC** — Run ASTAP plate solve on the current frame and sync mount coordinates.

### CAPTURE
- **File prefix** — Base name for output files (e.g. `Jupiter`).
- **Type dropdown** — Frame type: `Light` / `Dark` / `Flat` / `Bias`. Inserted in filename.
- **Output directory** — Where SER files are saved (default: `D:\RadukCapture\captures`).
- **Frames / Sec radio** — Capture limit mode: by frame count or by time.
- **MIN SEEING / GATE** — Quality gate: only record frames where seeing score ≥ threshold. Enable with the GATE checkbox.
- **▶ RECORD** — Start recording. Button turns red while recording. Press again to stop.
  - Press `P` while recording to **pause** (amber `⏸ PAUSED` state). Press `P` again to resume.

### STATUS
- **HDD** — Free disk space in GB.
- **BUF** — Write buffer fill level (slots used / total).
- **FPS** — Live frames per second.
- **LOST** — Frames dropped (write buffer overflow).

### HISTOGRAM
- Live 256-bin luminance histogram of the current frame.
- Peak channel and clip percentage shown below.

### NOTES  (saved with profile)
- Free-text notes for the current session. **Saved and restored with the active profile** — click SAVE on the profile to persist, click LOAD to restore. Notes are also editable in the profile EDIT dialog.

---

## Mount Controller (Floating Panel)

Open with `MOUNT CTRL` button or from the TELESCOPE section. Always stays on top.

- **N / S / E / W** — Hold to slew. Release to stop.
- **9× / FAST / MED / FINE** — Slew rate selector. **This also sets the
  speed of the arrow keys**, and it is currently the only place the rate
  can be changed — the main panel has no rate row. Before v1.2.1 the two
  panels each kept their own copy of the rate and never told each other,
  so picking FINE here highlighted the button but left the arrow keys
  slewing at 9×. If you tried to slow the arrow keys down and nothing
  happened, that was why.
- **STOP** — Immediately halt all movement (also `Space` key).
- **★ ALIGN** — One-point align on a centred object: syncs the mount to the
  object's computed position **and** switches tracking on. Pick the target from
  the list; planets are offered first. The result is reported honestly —
  "Aligned ... tracking ALT-AZ", "Synced ... mount refused tracking", or
  "Mount refused the sync".
- **RA / Dec readout** — Live coordinates updated every 500ms.
- **Motion plot** (bottom) — see below.

### Motion plot (v1.2.1)

A trail at the bottom of the panel showing where the telescope has
actually moved over the last ten seconds, with the current speed:

```
CAMERA  4.8"/s   ±8"
```

The number turns green while something is moving. ±8" is the width of
the plot in arcseconds — it rescales itself, so read the scale before
judging how far the trail has travelled.

**What it is for.** At FINE rate a short press moves the telescope a few
arcseconds. Nothing on screen changes enough to notice, so a command that
did nothing and a command that did something small look identical. This
tells them apart: press an arrow key and either the trail moves or it
does not.

**The two sources are not equivalent, which is why the plot says which
one it is using:**

| Source | Where it comes from | Resolution |
|--------|--------------------|------------|
| `CAMERA` | The planet's position in the image, every analysed frame | Fractions of an arcsecond, effectively instant. Preferred. |
| `MOUNT` | Alt/az read back from the mount over the cable | Polled every 500ms, or 2s while recording — a short nudge appears as one jump, not a line. |

`CAMERA` is used whenever a planet is being detected; the plot falls back
to `MOUNT` otherwise. A switch to `MOUNT` is itself useful: it usually
means the planet has left the frame, which is worth knowing at once
rather than a minute later.

A trail drifting steadily with no input from you is normal when tracking
is off, and is exactly what tracking exists to cancel.

---

## GoTo Panel (`G` key)

- **Object list** — 110 Messier objects + 7 planets, sorted by altitude (highest first).
- **GOTO** — Slew to selected object.
- **Manual RA/Dec** — Enter coordinates directly and slew.
- **Plate Solve → Sync** — Runs ASTAP on the current frame, computes actual pointing, syncs mount.
- Objects below the horizon are shown greyed out.

---

## Preview Overlays

### Reticle (`R` key)
Crosshair overlay centered on the image. Adjustable size. Use for centering targets before tracking.

### Clipping Warning (`C` key)
Any pixel where R, G, or B = 255 flashes red at ~2.5Hz. The `CLIP X.X%` label appears top-right in the HUD. Useful for dialing in exposure without clipping planetary detail.

### ADC Overlay
Shown automatically when mount altitude > 0°. Bottom-right corner box showing:
- Dispersion in arcseconds
- Severity level (color-coded)
- Blue upward arrow indicating the direction of dispersion

### Quality Bar Graph (`Q` key)
Rolling 200-frame sharpness history shown bottom-left, above the status bar.
- **Green bars** — Sharpness ≥ 60% (excellent seeing)
- **Amber bars** — Sharpness ≥ 30% (acceptable)
- **Red bars** — Sharpness < 30% (poor seeing / turbulence)
- **PK** — Peak sharpness in current window
- **AV** — Mean sharpness in current window

Watch for a burst of green bars during a good seeing cell — that's when to capture.

### RGB Align (`V` key)
Shifts blue channel UP and red channel DOWN during the display debayer pass to correct atmospheric dispersion. Auto value computed from mount altitude + Edlen model. Manual slider adds ±20px fine-tune.
- HUD shows `RGB ALIGN +N px` in amber when active
- Plate scale: 0.203 arcsec/px (8SE + ASI678MC at f/10)


### Pixel Value Readout (`X` key)
Toggle with `X`. When active, hover the cursor over any pixel in the preview to see a tooltip:
- **`R:NNN  G:NNN  B:NNN`** — raw 8-bit channel values from the debayered RGB buffer
- **`[fx, fy]`** — sensor pixel coordinates (accounts for zoom and pan)

**Click** to lock the readout to that pixel — tooltip turns green and `PIXEL LOCK` appears in the HUD top-right. Click again to unlock. Useful for:
- Checking exact exposure level on a planet's limb vs. disk center
- Confirming clipping (R/G/B = 255) on specific detail
- Verifying white balance by checking neutral gray stars

### Display FX (`B` key)
Cycles through six display modes — all display-only, SER data is always raw:

| Mode | Effect |
|------|--------|
| `NORMAL` | Unmodified debayered output |
| `BOOST ×2` | Each channel multiplied ×2, clamped at 255 |
| `BOOST ×4` | Each channel multiplied ×4, clamped at 255 |
| `STACK 3 BEST` | Aligned stack of the best 3 recent frames |
| `STACK 5 BEST` | Aligned stack of the best 5 recent frames |
| `STACK 10 BEST` | Aligned stack of the best 10 recent frames |

Active mode shown in amber HUD top-right (e.g. `BOOST ×2` or `STACK 5 BEST`). **FX automatically resets to NORMAL when recording starts** — you always capture raw frames. Also accessible via `[FX]` button in the IMAGE panel section.

Use Boost to find a dim target or check focus in poor seeing.

### The quick-look stacker (v1.2.0)

The three STACK modes are a **quick-look stacker**: they keep only the
sharpest recent frames, line them up on the planet, and average those.
Two numbers appear in the HUD:

```
LIVE 0.062  STACK 0.071  (10)
```

`LIVE` scores the current single frame, `STACK` the combined image, and
`(10)` is how many frames are in it. Both use the same measure over the
same area, so they are directly comparable. **Higher is sharper.**
While the first 32 frames are still being measured it shows
`learning...` instead of a threshold.

What this is for: deciding **at the telescope** whether what you are
capturing is sharp, instead of finding out the next morning. Watch
`STACK` while you nudge focus, and watch it over a few minutes to see
whether the air is improving or deteriorating.

Only about the top 15% of frames are admitted. Skipped frames are
normal and are the point — the whole value of lucky imaging is throwing
most frames away.

This is a **preview**, not a substitute for AutoStakkert. It aligns the
whole frame as one piece, where AutoStakkert corrects distortion
separately across dozens of small patches and will always produce a
better final image from the same video. Record the video regardless.

**Before v1.2.0 these modes averaged every recent frame at fixed pixel
positions.** Because the planet wanders 10-25 px with the seeing, that
smeared it — `STACK 10f` produced an image *softer* than a single frame
while the label implied otherwise. If you learned to avoid the stack
modes because they looked worse, that was why, and it is fixed.

### HUD Status Bar (bottom)
`LIVE 3840×2160 RAW8 38.3 FPS FRAMES: 2151 DROPS: 0`
- Click on the resolution readout to get a reset popup (reset to full sensor resolution).

---

## Autofocus Routine

1. Press `A` or click `★ AUTO FOCUS`.
2. The focuser sweeps 11 positions across ±3000 steps, averaging **8
   separate measurements** at each one.
3. A parabola is fitted to the peak sample and its two neighbours.
4. The focuser drives to the fitted position, approaching from one side
   to take up backlash.
5. Sharpness is measured again there and compared with where it started.
6. Press `A` again to abort mid-run.

**It takes about a minute.** That is expected. Each position waits for 8
genuinely new measurements at the analyser's own rate, which is what
makes the curve fittable rather than seeing noise.

**Tips:**
- Start near focus. The sweep covers ±3000 steps; if true focus is
  outside that it will say so rather than guess.
- Use a bright star or a planetary disk, never a faint fuzzy object.
- Check the EAF is connected (FOCUSER shows a position, not dashes).

### What it reports, and why you can trust it (v1.2.0)

Autofocus now checks its own work and says plainly when it did not help:

| Message | Meaning |
|---------|---------|
| `AF done: pos=… +24.0%` | Worked. Sharpness improved by that much. |
| `focus not bracketed — scan wider or recentre` | The sharpest point was at the edge of the sweep, so true focus is probably outside it. Focus roughly by hand, then re-run. |
| `Focus got worse (… → …) — reverted to start` | The fit landed somewhere worse, so it put the focuser back. Usually too little contrast to work with: centre the target, or wait for steadier air. |

Three faults were fixed in v1.2.0, and together they mean **results from
before that version were not meaningful**:

- Each of the 8 measurements per position was the *same* reading copied 8
  times, so every point on the curve carried full single-frame seeing
  noise instead of an average.
- The confirming measurement after the move collected nothing at all, so
  the reported sharpness was always `0.0%` and the improvement figure was
  meaningless.
- Success was reported unconditionally. A curve fitted to noise could
  drive the focuser somewhere clearly worse and still say `AF done`.

Separately, the sharpness measure itself was computing a red-minus-green
colour difference rather than detail, because it compared neighbouring
pixels that belong to different colours on the sensor. It barely
responded to focus at all. **Sharpness readings changed scale when this
was fixed, so numbers you remember from earlier sessions no longer
compare.**

---

## Recording Workflow

1. **Set target name** in the CAPTURE section file prefix box.
2. **Select frame type** (Light for normal captures, Dark/Flat/Bias for calibration frames).
3. **Set limit** — frames (e.g. 5000) or time (e.g. 120 sec).
4. **Optionally enable GATE** — set MIN SEEING threshold so only sharp frames are recorded.
5. **Click ▶ RECORD** — SER file opens immediately. Button turns red.
6. **Press `P`** to pause if seeing deteriorates. Press again to resume.
7. **Click ■ STOP RECORDING** or wait for the limit to be reached.

**Output filename format:**
`Jupiter_20260322_213045_Light_3840x2160_8bit_0001.ser`

`prefix _ timestamp _ frametype _ WxH _ bitdepth _ runnumber .ser`

A `_quality.txt` file is written alongside each SER with per-frame sharpness scores, mean, and peak.

---

## Snapshot (`S` key)

Saves the current debayered frame as a PNG to the captures directory. Uses the same prefix and output directory as the SER recorder.

**Output filename format:**
`Jupiter_20260322_213045_snap_0001.png`

---

## File Organization

All files go to the configured output directory (default: `D:\RadukCapture\captures`):

| File | Contents |
|------|----------|
| `*.ser` | Raw Bayer video frames, SER format |
| `*_quality.txt` | Per-frame sharpness scores + summary |
| `*_snap_*.png` | Single-frame PNG snapshots |
| `session_log.txt` | Session events, temperatures, status |

---

## Hardware Reference

| Device | Model | Connection |
|--------|-------|------------|
| Telescope | Celestron 8SE SCT | — |
| Camera | ZWO ASI678MC | USB3 |
| Focuser | ZWO EAF | USB |
| Mount | NexStar (8SE single-arm) | USB-serial, port auto-detected |
| Alignment | StarSense AutoAlign | — |

**Plate scale:** 0.203 arcsec/pixel (206.265 × 2.0µm ÷ 2032mm)
**Sensor:** Sony IMX678, 3840×2160, **2.0µm** pixel pitch
**Field of view:** 12.99' × 7.31' at full frame
**EAF safe test position:** 50000 (physical position currently 150000)
**HCG threshold:** gain 182 (above this = high-conversion-gain mode)
**Site:** auto-detected at startup (see *Observing site* below); stored in rig.ini
**ADC model:** Edlen 1966, pressure from site elevation, T=288K

---

## Cord wrap

An alt-az fork turns in azimuth with the camera, focuser, power and serial
cables all hanging off the moving part. Enough rotation one way strains the
connectors and can pull the camera out mid-capture.

What matters is **cumulative travel, not azimuth**. 350° and 10° are next to
each other on a compass but 340° apart in travel if the mount went the long
way round, and a cable only cares how many turns have piled up.

- The readout appears next to Az once travel passes 30°, so a fresh session
  is not cluttered with `+0`.
- It is **signed**: 180° out and 180° back reads 0, because the cable is
  slack again.
- GoTo slews are excluded. A jump of more than 90° between readings is not
  continuous rotation, and counting it would make the total meaningless.
- It works **before alignment**, since it only needs the change between
  readings, not where the mount thinks it is pointing.
- Past the limit (`WrapLimitDeg` in rig.ini, default 360°) it warns once.

**Press CABLES OK whenever you free or re-dress the cables.** Nothing else
resets the count — the app cannot see a cable, so it has to be told.

### Before a slew

A GoTo is checked before it runs. If the result would pass your limit you get
the three numbers that matter and which way to jog to unwind:

```
  Travelled since CABLES OK:  +340°
  This slew adds:              +80°
  Would leave you at:         +420°   (limit 360°)
```

The mount chooses its own path for a GoTo — the protocol carries coordinates,
not a direction — so the forecast assumes the **shortest** turn. If even the
short way breaks the limit, the long way certainly does, so the warning is
never a false alarm, and a slew that happens to *unwind* stays silent.

Unwinding is left to you rather than driven automatically: shedding a turn
means rotating up to 360°, a long motion that can snag something by itself.
Jog in the direction given and watch the wrap figure beside Az fall.

Only slews you start are checked. The sky survey fires dozens unattended and
would otherwise raise a dialog on each.

---

## Keep-out sector

An optional arc of azimuth marking something you cannot point at — a wall, a
tree, the house. A GoTo whose target lands inside it asks first.

Direction is part of the definition and cannot be guessed: **300° to 60°** is
the 120° sector crossing north, while **60° to 300°** is the 240° sector the
other way round. The convention is **clockwise from start to end**. Equal
values mean no sector, which is the default.

Set it in the **SETUP** view, where it is drawn as a wedge so you can see
which sector you have actually described, or directly as `KeepOutStartDeg`
and `KeepOutEndDeg` in rig.ini.

---

## Before you start (readiness check)

Once the camera and mount are up, RadukCapture checks whether the rig can
actually image and writes one line to the log either way:

```
[READY] 7 ok, 0 warn, 1 fail, 0 unknown, 3 to check by hand
[READY]   Alignment  FAIL  mount reports NOT aligned - hand control,
          Align > Quick Align (needs no stars)
```

**A dialog appears only if something failed or warned.** A list that appeared
every launch would be dismissed unread within a week, so a ready rig gets
silence.

What it verifies for itself: camera streaming, dropped frames, focuser
present, mount connected, **mount aligned**, **tracking actually driving**,
observing site known, disk space, profile loaded. The two in bold are the
ones that will stop a night dead.

Three things it cannot see and must ask you: dust cap off, tube cooled to
ambient, cables dressed. These are reminders and **never** cause the dialog to
appear on their own. Ticking all three also resets the cord-wrap count, since
that is what confirming the cables means.

While the dialog is open the rows **refresh every two seconds**. Walk to the
mount, run Quick Align, and watch the row turn green without restarting
anything. **Explain this** opens the Help section for the first failing row,
and **Continue** is always available — this is advice, not a gate.

**READY** in the TELESCOPE section reopens it whenever you want — unlike the
automatic appearance, that always shows the list whatever it says, because
you asked for it.

Set `ShowReadinessCheck=0` in rig.ini to suppress the automatic appearance.
The READY button still works.

---

## Stacking a finished capture

RadukCapture does not produce the final image. It records the video and
shows a quick-look stack so you can judge sharpness at the telescope;
**AutoStakkert** does the real work afterwards, and better than anything
here would.

`K` hands the last finished recording to AutoStakkert. Setting
`AutoStackOnStop=1` in `rig.ini` does it automatically whenever a capture
ends normally - you press stop, or it reaches its frame or time limit. An
emergency stop never triggers it.

**Known problem as of v1.2.2: this does not currently work on this
machine.** AutoStakkert starts, parses every option correctly, then never
begins processing. It is not RadukCapture - the identical command fails
from a plain command prompt too, and so does the exact command that worked
earlier the same day. `AutoStackOnStop` is off by default because of it.

The command is sound and works when AutoStakkert is healthy, so you can
always run it yourself:

```
AutoStakkert.exe "<file>.ser" /apgrid /process /hide /exit
                 /outputdirectory "<folder>"
```

To tell a healthy run from a stuck one, open AutoStakkert's own log at
`debug_data\log.txt`. A working run prints `Processing started` about
half a second after `started...`; a stuck one never does. If it is stuck:
reboot, then reinstall AutoStakkert if that does not help.

### The `_quality.txt` file

Every recording gets one, listing a sharpness score per frame.

**AutoStakkert does not read it.** It measures quality itself, and does so
separately around each alignment point, so a single score per frame could
not drive its choices anyway. The file is for you: it records how the
seeing ran across a capture, and it is what tells you the next morning
which runs were worth keeping.

Before v1.2.2 it held whichever score the analyser happened to have last,
repeated across long runs of frames, and could not rank anything. It is now
measured per frame.

---

## Emulator test switches

The emulator build simulates camera, focuser and mount so the app can be
developed and tested with the telescope packed away. These environment
variables adjust what it simulates. They have no effect on the real
hardware build and are listed here so a test result can be reproduced.

| Variable | Default | Effect |
|----------|---------|--------|
| `RADUK_EMU_JITTER_AS` | 0.5 | Seeing wander amplitude, arcsec |
| `RADUK_EMU_DRIFT_AS` | 0.3 | Mount tracking error, arcsec/sec |
| `RADUK_EMU_ROTATION_DEG` | 0 | Camera angle, to prove the tracker measures its axis transform rather than assuming it |
| `RADUK_EMU_UNALIGNED` | off | Mount reports itself unaligned, reproducing the whole tracking-blocked path |
| `RADUK_EMU_TRUE_FOCUS` | 51200 | Focuser position at which the image is perfectly sharp |
| `RADUK_EMU_BLUR_PX_STEP` | 0.0083 | Blur added per step away from focus |
| `RADUK_EMU_FREEZE_SURFACE` | off | Stops planet rotation, turbulence and moons, so only focus varies |
| `RADUK_EMU_NOISE` | 0 | Per-pixel sensor noise, DN |
| `RADUK_EMU_SEEING_AS` | 0 | Per-frame atmospheric blur, arcsec. Without it every simulated frame is equally sharp, so the seeing score, the capture gate and the stacker's frame selection have nothing to choose between |
| `RADUK_EMU_REMOVE_AFTER_S` | off | Simulates the camera being unplugged after N seconds |

The last four exist because the simulated planet was previously
rendered perfectly sharp at every focuser position and perfectly clean
in every frame. Nothing that depends on focus or on averaging frames
could be tested at all — an autofocus sweep was fitting a curve to
noise, and a stack of identical clean frames can only ever equal one
frame. Setting `RADUK_EMU_TRUE_FOCUS` gives a focus test a known
correct answer, which is what turns it into a real check.

---

## Troubleshooting

### No signal / black preview
- Check USB connection to ASI678MC
- Check that camera is not in use by another application (close FireCapture if open)
- Restart RadukCapture

### Mount not responding
The serial port is **auto-detected** at startup: RadukCapture probes every COM
port Windows reports as present and keeps the one that answers the NexStar echo
command. The port a USB-serial cable gets is assigned by Windows and changes
when the cable is plugged into a different USB socket, so it is never assumed.

The startup log line names what happened:
- `Mount:  OK (COM4 responded)` — hand control found on that port
- `Mount:  OK (COM4 owned by CPWI)` — port exists but CPWI holds it
- `Mount:  NOT FOUND (probed: COM3, COM4)` — nothing answered on any present port

If it is NOT FOUND:
- Check the hand control is powered on and the cable is seated at both ends
- Check the USB-serial adapter appears under Device Manager > Ports (COM & LPT)
  with no warning icon — a port listed there but not working needs its driver
- If CPWI is running, disconnect the mount in CPWI first, then RadukCapture
  will take the port
- Use `RELEASE` in the TELESCOPE section to hand the port back to CPWI

To pin a specific port and skip the scan, add it to `rig.ini` next to the
executable:

```ini
[Rig]
MountPort=COM4
```

A pinned port is tried first; if it does not answer, the scan continues anyway,
so a stale entry falls back to auto-detect instead of breaking the connection.

### Small moves seem to do nothing

- **Check the rate is what you think it is.** The rate buttons live only
  in the floating `MOUNT CTRL` panel, and they set the arrow keys too.
  Before v1.2.1 the two did not talk to each other, and the arrow keys
  stayed at 9× whatever the panel showed.
- **Watch the motion plot** at the bottom of `MOUNT CTRL` while pressing.
  A moving trail means the command is reaching the mount and the move is
  simply too small to see in the preview. A flat trail means it is not.
- At `FINE` a quarter-second press is a few arcseconds. That is intended
  — it is for centring a planet, not finding one. Use `9×` to travel.
- If the plot reads `MOUNT` rather than `CAMERA` it is only updating
  twice a second, so a brief nudge shows as one step rather than a smooth
  line. That is the readout being coarse, not the mount being stuck.

### Nothing is being recorded, or almost nothing

Check the **GATE** threshold first. The gate only records frames whose
seeing score beats it, and **the meaning of that number changed in v1.2.2**
- the score is now relative to the best recent frame rather than an
absolute scale. A threshold set before then will not behave as you remember.

Re-pick it against the live readout: watch the seeing number for a minute,
note how low it dips, and set the gate a little above that. Set too high it
records nothing at all, which looks exactly like a broken capture.

### Frames dropping (LOST > 0)
- Reduce USB bandwidth slider in CAMERA section
- Close other USB-intensive applications
- The write buffer is 512 slots — sustained drops mean disk write speed is insufficient

### Mount will not track (TRK stays OFF)
An alt-az mount computes its altitude and azimuth rates from **latitude and
local time**. Without them the calculation is underdetermined and the mount
declines to track, however often it is asked.

RadukCapture sends both on connect. Check the startup console:

```
[NexStarMount] Site  -> ... : accepted
[NexStarMount] Clock -> ... : accepted
```

If those are accepted and `TRK` still reads `OFF`, the mount is **not
aligned**. Tracking is gated behind the hand control's own alignment, and
nothing in this application can substitute for it:

> **Hand control -> Align -> Quick Align.** It needs **no stars** - level the
> tube, point north, confirm. GoTo accuracy will be mediocre; tracking will
> work, which is what matters if you find targets by hand.

RadukCapture notices an unaligned mount at startup and offers help with the
hard part of that instruction. "Point the tube north" is an abstraction in a
dark garden, so it asks one question - *standing where you normally sit, the
scope is to my ...?* - and answers in terms of what is actually around you:

> North is behind you. Point the tube back towards yourself - north is on your
> side of the mount, so the tube should face your desk.

The tube ending up aimed at you is correct and counter-intuitive, which is
why it says so plainly. The answer is saved to rig.ini
(`ObserverBearingDeg`), so you are asked once per rig, not once per session.
Dismiss it permanently with **Don't show again**, or set
`ShowAlignHelper=0`.

Afterwards the console should read `Mount reports ALIGNED - leaving its site
and clock untouched`: the app will not re-push a site to an aligned mount,
because changing time or location can invalidate a Celestron's model.

Note that `TRK` reflects a readback of the mount's actual state. A command the
mount merely acknowledges is not reported as success.

`TRK: UNKNOWN` is a third state, and means the mode could not be read at all
after several tries — a link problem, not a tracking problem. It used to be
reported as `OFF`, which sent at least one session chasing the wrong fault.

### Acquisition refuses to start
`[CENTROID]` runs the whole sequence: find the planet, calibrate, centre, lock.
If it cannot start it says why, in the notification bar and the log. The usual
cause is tracking:

- *"mount is not tracking"* - see above. With the mount undriven the planet
  drifts 15"/s, which is 74 px/s at this plate scale; the centring dwell window
  is 10 px, so it is crossed in 0.14 s and can never be satisfied.
- *"planet too faint to detect (frame peak 3%, needs above 8%)"* - it is in
  frame but under the detection threshold. Raise exposure or gain.
- *"no planet detected in frame"* - nothing bright enough is there at all.
  Go and find it first.

Those two used to read identically, which mattered because they need opposite
responses.

`[ROI]` is stage 3 of that sequence, not a manual crop tool. To set an ROI by
hand, **drag a box on the preview** instead.

### Observing site wrong or missing
The site is resolved at startup in this order, and the first that answers wins:

1. `SiteLatDeg` / `SiteLonDeg` in rig.ini, if present
2. **Windows Location** - typically accurate to ~100 m, which is far better
   than the 5.6 km needed to land a target in the frame
3. A USB GPS receiver on any serial port (NMEA, passive listen)
4. The site the mount itself already holds
5. A dialog asking for it

If none answers, **no site is sent to the mount**. An unknown site is left
unknown rather than defaulted to 0,0 - which is a real place in the Gulf of
Guinea and would break tracking, the sky map and the ADC model while looking
perfectly configured.

Windows Location needs *Settings > Privacy > Location*, including **"Let
desktop apps access your location"**. If it is refused the log says so and
names the setting.

To pin the site by hand, set both coordinates and `SiteSource=manual` in
rig.ini; an entry marked manual is never replaced by an automatic estimate.

### Autofocus fails / doesn't converge
- Ensure EAF is connected (position shows a number, not dashes)
- **Centre the target first.** Sharpness is measured on a 256×256 crop that
  follows the detected planet; with nothing detected it falls back to frame
  centre and reads background noise.
- **Read what it reports.** Since v1.2.0 it tells you which way it failed:
  `focus not bracketed` means true focus is outside the ±3000-step sweep,
  so focus roughly by hand and re-run; `Focus got worse … reverted to
  start` means the fit landed somewhere worse and the focuser has been put
  back, usually for want of contrast.
- A `sharp=0.0%` result was a bug in versions before v1.2.0, not a
  diagnosis — the confirming measurement collected nothing. If you see it
  now, the target really is too faint.
- Check EAF is not at travel limits (safe zone: 10000–490000)

A move that makes no progress for 5 s is reported as stalled. Long moves are
fine - the sweep's first hop is 3000 steps and is allowed to take as long as
it needs.

### GoTo misses target
- Run `POINT SOLVE → SYNC` to correct mount model at current position
- Ensure StarSense alignment was performed before session

### ADC reading seems wrong
- ADC uses cached mount altitude — verify mount is tracking and RA/Dec updating in TELESCOPE section
- Ensure mount is polar-aligned (single-arm mount errors compound at low altitude)

---

## rig.ini reference

Sits next to the executable, is never committed, and is the only place your
real site is stored. Most of it is written for you.

| Key | Meaning |
|-----|---------|
| `SiteLatDeg` / `SiteLonDeg` | Observing site. Auto-detected if absent. |
| `SiteElevationM` | Metres above sea level; sets the ADC pressure model. |
| `SiteSource` | `manual`, `windows`, `gps` or `mount`. A `manual` site is never overwritten by an automatic one. |
| `MountPort` | Pin a COM port. Empty means auto-detect. |
| `PlateScaleArcsecPx` | Nominal image scale; seeds overlays and solver hints. |
| `GuideRateArcsecPerSec` | Assumed guide rate. Calibration measures the real one and warns if they disagree. |
| `WrapLimitDeg` | Cumulative azimuth travel before the cord-wrap warning. |
| `KeepOutStartDeg` / `KeepOutEndDeg` | Obstruction sector, clockwise from start to end. Equal values = none. |
| `ObserverBearingDeg` | Mount to observer, degrees true. Used by the align helper. |
| `ShowAlignHelper` | `0` suppresses the align helper. |
| `ShowReadinessCheck` | `0` suppresses the readiness dialog (the log line remains). |

---

## Phase History

| Version | Phase | Feature |
|---------|-------|---------|
| v0.9 | 1–4 | Camera, EAF, NexStar — all devices communicating |
| v0.9 | 5 | Centroid tracking (3-stage: Search → Center → Lock) |
| v0.9 | 6 | GoTo panel (110 Messier + 7 planets, VSOP87, ASTAP) |
| v1.0 | UI | Full Win32 control panel + Direct2D preview |
| v1.0.1 | — | Floating mount controller |
| v1.0.2 | 15 | ADC Assistant (Edlen model, dispersion overlay) |
| v1.0.3 | 11 | Lucky imaging frame tagger (_quality.txt) |
| v1.0.4 | — | GetAltAz render-loop fix (moved to background thread) |
| v1.0.5 | 12 | Auto-Exposure Assistant (I-controller, HCG floor) |
| v1.0.6 | 21 | Clipping Warning Overlay (pixel flash + HUD) |
| v1.0.7 | 22 | Snapshot PNG (WIC encoder, S key) |
| v1.0.7 | 25a | Quality Bar Graph (200-frame rolling history) |
| v1.0.8 | 41 | Pause Capture (P key, amber button, SER stays open) |
| v1.0.9 | 44 | Frame Type Labels + App Icon (.rc embedded) |
| v1.0.10 | 23 | RGB Align Display Mode (V key, auto+manual) |
| v1.0.11 | 24 | Pixel Value Readout (X key, hover tooltip, click to lock) |
| v1.0.11 | — | Notes linked to profile (save/load with PROFILE section) |
| v1.0.12 | 25 | Image Boost/Frame Stack FX (B key, 6 modes, auto-reset on record) |
| v1.0.13 | 26 | Night Vision Mode (N key, red tint via Rec.709 luminance) |
| v1.0.14 | 38 | Single-Channel Display Mode (H key, R/G/B/All cycle, grayscale) |
| v1.0.15 | 49 | Help System (? key, floating window, live search filter) |
| v1.1.0 | — | Build version + compile timestamp in title bar |
| v1.1.1 | — | Hold last frame in preview (stop NO SIGNAL flashing) |
| v1.1.2 | — | Version in preview banner, dark title bar |
| v1.1.3 | — | Initial public release (MIT licence, README) |
| v1.1.4 | 60 | Hotkeys routed from the message pump — work whatever has focus |
| v1.1.4 | 60 | Arrow-key jog: auto-repeat suppressed, stop on app deactivation |
| v1.1.4 | 61 | Mouse reaches the preview — drag-box ROI and pixel readout |
| v1.1.4 | 61 | Sharpness crop follows the planet instead of the frame centre |
| v1.1.4 | 61 | Autofocus waits on progress, not a flat 10 s budget |
| v1.1.4 | 61 | ROI and LOCK state why they refuse instead of doing nothing |
| v1.1.4 | — | Mount serial port auto-detected (NexStar echo probe) |
| v1.1.4 | — | Profile SAVE captures live camera and focuser settings |
| v1.1.5 | 62 | Site and clock sent to the mount — makes alt/az tracking possible |
| v1.1.5 | 62 | ALIGN performs a one-point align and enables tracking |
| v1.1.5 | 63 | Sky map rebuilt as a true zenith-centred polar projection |
| v1.1.5 | 64 | Tracking verified by readback, not by command acknowledgement |
| v1.1.5 | 65 | One-button acquisition: find, calibrate, centre, lock |
| v1.1.5 | 65 | Calibration halved; guide rate measured instead of assumed |
| v1.1.5 | 66 | Observing site auto-detected (Windows Location, GPS, mount) |
| v1.1.5 | 66 | ADC pressure derived from site elevation |
| v1.1.6 | 69 | Acquisition says whether the planet is too faint or absent |
| v1.1.6 | 69 | Tracking reads UNKNOWN when unreadable, no longer OFF |
| v1.1.6 | 67 | Azimuth surfaced — it was read every poll and discarded |
| v1.1.6 | 67 | Cord-wrap tracking as cumulative travel, with CABLES OK reset |
| v1.1.6 | 67 | Quick Align helper — "point north" in terms of your landmarks |
| v1.1.7 | 67 | Pre-slew cord-wrap forecast and warning |
| v1.1.7 | 67 | Keep-out sector — warn before slewing into a wall or tree |
| v1.1.7 | 67 | Rig setup view — check the stored bearings against reality |
| v1.1.7 | 68 | Readiness check: verified at startup, shown only on a failure |
| v1.1.7 | 68 | Live-refreshing rows and "Explain this" into the Help file |
| v1.1.7 | 70 | Acquisition stage and its blocker shown continuously |
| v1.1.8 | 71 | Esc stops the mount unconditionally, including in fullscreen |
| v1.1.8 | 71 | Space (emergency stop) and A (autofocus) actually bound |
| v1.1.8 | 71 | Exposure slider: one unit system, ceiling 10 ms -> 60 ms |
| v1.1.8 | 71 | PLANET LOCK indicator restored when acquisition ends |
| v1.1.9 | 72 | READY button — reopen the readiness check without relaunching |
| v1.2.0 | 73 | Autofocus: real per-position averaging, honest reporting, reverts a bad fit |
| v1.2.0 | 73 | Sharpness metric fixed — was measuring colour, not focus |
| v1.2.0 | 73 | Emulator models defocus and sensor noise, so focus work can be tested |
| v1.2.0 | 74 | Quick-look stacker — aligned, quality-gated preview stack with a score |
| v1.2.1 | 75 | Jog rate now reaches the arrow keys; both rate selectors stay in step |
| v1.2.1 | 75 | Motion plot on the Mount Controller — camera or mount source, labelled |
| v1.2.2 | 76 | Per-frame quality scores; one shared sharpness definition; a ceiling that survives sensor noise |
| v1.2.2 | 77 | AutoStakkert handoff — `K` and AutoStackOnStop (launch currently blocked, see above) |
| v1.2.2 | 78 | Emulator jog rates follow the real NexStar curve |
| v1.2.2 | 79 | Capture gate fixed; emulator can vary frame quality |

---

*RadukCapture Help v1.2.2 — Updated after each release*
