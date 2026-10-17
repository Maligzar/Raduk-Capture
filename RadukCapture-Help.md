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

**What you can see.** Alongside this file you are given a live session
snapshot: the time, the target and profile in use, the mount's RA/Dec/Alt/Az
and whether it is tracking, gain, exposure, FPS, the seeing score, the
**capture format and whether it is a crop**, the capture limit, **which
display modes are altering the image**, focuser position and temperature,
recording state, tonight's visible objects with their remaining imaging
windows, and the captures already recorded. The filter fitted to the camera
is in your identity block. Use those numbers rather than asking for them, and
do not state a number that is not in them.

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

**Know what the app cannot see.** It has no way to detect which filter is on
the camera, whether the dust cap is off, or whether the tube has cooled. The
filter is the one that silently changes a number: every dispersion figure
assumes the configured band. If someone says they changed or removed a
filter, tell them to set it in SETUP before trusting any ADC reading. Do not
guess a band from the target or the season.

**Autofocus is not a find-focus-from-anywhere button.** It searches ±3000
steps around wherever the focuser already is. If someone is far out of focus,
they need to get roughly sharp by hand first, or it will report `focus not
bracketed` — which is it working correctly, not failing.

**The display stretch is not an exposure change.** If someone says the image
looks too dark or too flat, the stretch (`L`, and the Black/White sliders) is
the first thing to try, because it costs nothing and changes no data. Raising
gain or exposure to fix how something *looks* makes the recording worse.

**Dark and flat correction change the preview, not the recording.** If
someone asks why their saved file looks noisier than the screen, that is why
and it is correct. A master only applies at the resolution and bit depth it
was taken at; the status line says so when it does not.

**Check the display line before blaming the camera.** Several modes change
only what the image *looks* like and nothing about what is captured: RAW
debayer makes it greyscale, night vision makes it red, single-channel makes
it a grey version of one colour, display FX brightens it, and a framing shot
makes it briefly bright and noisy. The snapshot lists whichever of these are
on, and says `Display: normal` when none are. So when someone reports a
picture that looks wrong, read that line first. If it says normal, the
display is not the cause and the cause is exposure, focus, or the sky. If it
lists something, name it and say which key turns it off — do not send them
hunting through the camera sliders for a fault that is not there.

**Do not promise what is not built.** There is no dark-frame subtraction, no
flat-field correction, no FITS or AVI output, no polar-alignment assistant, no
Bahtinov or FWHM focus aid, and no click-to-recentre. The section "What is not
built yet" is the list. Say it plainly and say what to do instead — darks
belong in the stacker afterwards, not here.

**Selecting a target sets the camera up.** Picking an object on the map or
in GoTo loads that target's profile: gain, exposure, resolution, bit depth,
focus position. So if someone asks "what settings should I use for Jupiter",
the answer is usually "pick Jupiter and it is already done". If the picture
then looks wrong, it is the profile that wants editing, not the sliders —
sliders changed by hand are lost on the next load unless they press SAVE.

**A crop is recoverable; say so.** The session snapshot tells you the format
and marks it `CROPPED` when it is narrower than the sensor, so you do not
have to ask. If someone says the view has gone narrow, or that they cropped
and cannot get back, the answer is the `FULL` button on the top bar. Do not
suggest restarting the app. If they ask about data rate or
file size, the controls are the resolution buttons and `8b`/`16b`; the USB
slider in the CAMERA section is a different thing and does not change what a
frame contains.

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
| `F` / `Shift+F` | Focus step: fine / coarse (uses the profile's step sizes) |
| `Shift+S` | Framing shot — boost gain and exposure for 5s, then restore |
| `F11` | Toggle fullscreen |
| `G` | Open GoTo panel |
| `H` | Cycle Channel Mode (All → Red → Green → Blue → All) |
| `M` | Open Map (Sky Map) |
| `F11` *(in the Sky Map window)* | Fill the display the map is on, with no title bar. `Esc` or `F11` again to come back. See **Putting the Sky Map on its own screen**. |
| `Ctrl+Shift+Home` | **Bring every window back to the main screen.** Works from anywhere, even with the app in the background. See **The multi-monitor workspace**. |
| `Ctrl+Shift+End` | **Save the current arrangement** as the layout to open with. |
| `Shift+H` | Close or reopen the **histogram window**. Open at startup. |
| `Shift+Q` | Close or reopen the **sharpness window**. Open at startup. |
| `Shift+D` | Close or reopen the **status window** — capture health and free space on every drive. |
| `Shift+A` | **ADC alignment window** — for dialling in a dispersion corrector. Inert unless *ADC fitted* is ticked on the READY screen. |
| `Shift+V` | **Share view** — the image alone, filling a second screen, for people who are not you. See **The share view**. |
| `Ctrl+Shift+V` | Share view in and out of a **movable window**. Works from the control screen, which is the point. |
| `Shift+M` | **A second sky map**, on the control screen, so the first can stay where other people can see it. |
| `N` | Night vision — red preview and HUD. Remembered between sessions |
| `W` | **W**atch — occasional advice from the local AI. Off by default |
| `L` | **L**evels — display stretch on/off, for comparing the two |
| `P` | Pause / Resume recording |
| `Q` | Toggle Quality Bar Graph |
| `R` | Reticle: off → crosshair → circle → rings → box → off |
| `S` | Save Snapshot (PNG) |
| `U` | **Clear the view** — hide everything drawn over the image except the reticle. Remembered between sessions |
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
Across the top of the preview window, in three groups: acquisition, view, and
capture format.

| Button | Key | Function |
|--------|-----|----------|
| `[CENTROID]` | — | **Acquire the planet**: find, calibrate, centre, lock. Press again to cancel. |
| `[LOCK]` | — | Manual override — force the calibration step |
| `[ROI]` | — | Crop the sensor around a locked planet (not a freehand ROI — drag a box on the preview for that) |
| `[C] COLOR` | — | COLOR / RAW debayer. **RAW shows the raw greyscale sensor data** — useful for judging focus, but the image goes monochrome |
| `[*] MAP` | `M` | Toggle Sky Map window |
| `[I] GOTO` | `G` | Open GoTo panel |
| `[+] RETICLE` | `R` | Toggle crosshair reticle overlay |
| `[CLIP]` | `C` | Toggle clipping warning (red flash on saturated pixels) |
| `[GH]` | — | Ghost/frame-stack display FX |
| `[?]` | `?` | Toggle this Help window |
| `[AI]` | — | Toggle the AI assistant chat panel |
| `FULL` | — | **Whole sensor, 3840x2160** - the widest field, and the way back from any crop |
| `1920` | — | 1920x1080 |
| `1280` | — | 1280x960 - what `[ROI]` crops to |
| `640` | — | 640x480 - smallest frame, highest frame rate |
| `8b` | — | 8-bit capture (RAW8). Half the data; the normal choice for planets |
| `16b` | — | 16-bit capture (RAW16). Headroom for stacking, twice the data rate |
| `EXIT` | — | Graceful shutdown (confirms if recording) |

Buttons highlight (bold) when their feature is active. The live resolution and
bit depth each carry a dot, so you can see at a glance what you are recording.

### Getting the field back (capture format)

`FULL` exists because a crop used to be a one-way door. `[ROI]` and a dragged
box both narrow the sensor read-out, and until v1.2.3 nothing widened it
again - the only way back to a full field was to restart the app. Press
`FULL` and you are back.

Resolution changes keep whatever is in the **middle** of the frame in the
middle of the frame, so widening the field to find a planet that has drifted
out does not also move the field somewhere else.

Changing either setting **closes the current recording**, because a SER file
cannot change format part-way through and the rest of it would be unreadable.
Set the format before you start, not during. If you were recording, a new
file is opened at the new format.

**Which bit depth?** 8-bit for planets: the frames are half the size, so the
disk and USB keep up at high frame rates, and stacking hundreds of frames
recovers the precision anyway. 16-bit when a single frame has to carry the
detail - long exposures, faint targets, or anything you intend to stretch
hard afterwards. The data rate doubles, so watch `LOST` in the status bar.

**Not the same as USB bandwidth.** That is a separate slider in the CAMERA
section of the control panel (40-100%), and it controls how hard the camera
pushes the USB bus, not how much data each frame holds. Lower it if frames
are dropping.

---

## Control Panel Sections

All sections are collapsible — click the section header to expand/collapse.

### IMAGE
- **DARK / FLAT / CLEAR** — Build or clear calibration masters. See [Dark and flat frames](#dark-and-flat-frames).
- **Black / White sliders** — Display stretch. See [Display stretch](#display-stretch).
- **ZOOM: FIT / FULLSCR** — Switch between fit-to-window and fullscreen preview.
- **RGB ALIGN** — Toggle atmospheric dispersion correction on the display. Auto-computes pixel shift from mount altitude using the Edlen refractivity model. Blue channel shifts UP, red shifts DOWN by half. Display only — SER data is unaffected.
- **Manual slider** — Fine-tune the RGB align offset ±20px on top of the auto value. Center position = 0px offset.
- HUD shows `RGB ALIGN +N px` in amber when active.
- **[FX] NORMAL** — Cycle Display FX modes (see Display FX section below). Also `B` key.
- **[N] NIGHT** — Toggle Night Vision Mode (red-tinted preview for dark adaptation). Also `N` key. HUD shows `NIGHT VISION` in red when active.
- **[H] CH: ALL** — Cycle single-channel display mode: All → Red → Green → Blue. Shows selected channel as grayscale. Display only — SER data unaffected. Also `H` key. HUD shows channel label in cyan when active.

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
- **Filter: name (low–high nm)** — The filter the line above assumes is on the camera, and the wavelength band it computed the dispersion across. This is **configured, not detected** — the app has no way to see what is screwed onto the camera. If it is wrong, the ADC figure above is wrong with it, by up to 4×. Change it in **SETUP**. See [Filters](#filters).

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
Holds the target profile and the recording settings together. They used to
be two sections, PROFILE and CAPTURE, which described the same thing from
opposite ends — and because the profile's copies were never pushed into the
boxes, the boxes silently won every time. One section, one set of values.

- **Target** — The profile, with `LOAD` / `SAVE` / `EDIT`. Picking an object
  on the sky map or in GoTo loads the matching profile automatically; see
  [Profiles](#profiles).
- **Prefix** — Base name for output files (e.g. `Jupiter_`). Filled in by a
  profile load, and read back by `SAVE`.
- **Type dropdown** — Frame type: `Light` / `Dark` / `Flat` / `Bias`. Inserted in filename.
- **Output directory** — Where SER files are saved (default: `D:\RadukCapture\captures`).
- **Frames / Sec radio** — Capture limit mode: by frame count or by time. Also
  set by a profile load — a time-limited profile selects **Sec** for you,
  which it previously did not, so it loaded its seconds into the box and
  still stopped on the frame count.
- **MIN SEEING / GATE** — Quality gate: only record frames where seeing score ≥ threshold. Enable with the GATE checkbox. The setting survives collapsing the section — until v1.2.3 it did not, and collapsing CAPTURE silently switched the gate off.
- **▶ RECORD** — Start recording. Button turns red while recording. Press again to stop.
  - Press `P` while recording to **pause** (amber `⏸ PAUSED` state). Press `P` again to resume.

### STATUS
- **HDD** — Free disk space in GB.
- **BUF** — Write buffer fill level (slots used / total).
- **FPS** — Live frames per second.
- **LOST** — Frames dropped (write buffer overflow).

### HISTOGRAM and TEMPERATURE — moved to the preview (v1.2.4)

Both left the control panel. They are now permanent panels on the live
image itself, one either side of the target, and there is nothing to
expand — see **Side panels** under Preview Overlays.

They moved because they were the wrong shape for this panel: every
section here starts collapsed, so the two readouts you want to watch
continuously were the two you had to open first, and opening them
pushed everything else down. The dark sky either side of a planet was
going spare.

### NOTES  (saved with profile)
- Free-text notes for the current session. **Saved and restored with the active profile** — click SAVE on the profile to persist, click LOAD to restore. Notes are also editable in the profile EDIT dialog.

---

## The multi-monitor workspace (v1.2.4)

The app counts the displays at launch, loads the layout for that count,
and opens with each window already on the screen it belongs on. Nothing
to arrange by hand at the start of a session.

**Layouts are keyed by how many screens are attached**, not by one saved
arrangement. Five screens get the five-screen layout; plug in only the
laptop panel and it uses the one-screen layout instead. If there is no
layout for the exact number found, it steps *down* to the largest one
that fits, so a five-screen layout still does something sensible on
three and everything ultimately falls back to the one-screen layout.
This matters on a rig whose screens come from two graphics cards: the
set changes when a cable or a driver does, and a single remembered
arrangement would be wrong the moment it did.

The layout lives in **`workspace.ini`** next to the program, written on
first run so it can be edited rather than guessed at:

```ini
[monitors.5]
main   = \\.\DISPLAY5
skymap = \\.\DISPLAY4 maximized
goto   = \\.\DISPLAY2
mount  = \\.\DISPLAY1
help   = \\.\DISPLAY3
aichat = \\.\DISPLAY3
```

Each line is `role = display [maximized] [x,y,w,h]`. The sizes, if given,
are fractions of that screen, so a layout written on one monitor size
still means something on another. **A role with no size is moved, not
resized** — the mount and GoTo panels place their controls at fixed
positions, so stretching one would leave its buttons in a corner of an
empty window. Only the map and the preview actually scale.

**More than one window can name the same screen.** Five screens means
one for the main window and four to share between five panels, so two
of them double up — help and AI chat by default, both being panels you
read occasionally rather than watch. Change the file if you would
rather pair a different two.

### Arrange it by hand, then keep it

Drag the windows wherever you want them and press **`Ctrl+Shift+End`**.
That records exactly where everything is — to the pixel, including which
windows are maximised — and the app opens that way from then on.

This is the intended way to set the layout up. A generated default
spreads the windows one per screen because it has nothing better to go
on; it will never guess an arrangement you built deliberately. Saved
entries use the `@x,y,w,h` form, which is exact screen coordinates
rather than fractions, so what you get back is what you had.

The layout in use on this rig gives the histogram window DISPLAY2 and
the sharpness window DISPLAY1, with the map on DISPLAY4, the main window
and mount controller on DISPLAY5, and help, AI chat and GoTo sharing
DISPLAY3. A copy lives in `docs/workspace-5monitor.ini`.

### When a screen comes or goes (v1.2.6)

Unplug a monitor, let one sleep, change a resolution, or have a driver
reset the topology, and the app reapplies the whole saved layout by
itself. No restart, no keystroke.

It **re-places** rather than merely rescuing. A monitor waking from
sleep is by far the commonest case, and its windows should go back
where they belong rather than pile onto the main screen for you to sort
out by hand. Anything still unreachable afterwards is brought home and
the notification bar says so.

What it did goes to the session log, because this fires during exactly
the moments the rig is most fragile — driver installs, resolution
changes, a panel waking — and a 3am misbehaviour has to be diagnosable
the next morning.

A window dragged to a screen with a different scale factor is resized to
match it. Five panels of mixed vintage will not share a scale, and
without this a window keeps its old pixel size and either overflows the
smaller screen or shrinks on the larger one.

### Which GPU the preview is on

The rig drives five screens from two cards. The live preview presents
through whichever adapter drives the screen it is on, so moving it to a
screen hanging off the other card costs a cross-adapter copy on every
single frame — for nothing, since both show the same picture.

The app reads which card drives each screen at startup and names it in
the log:

```
[LAYOUT]   \.\DISPLAY5  1920x1080 at (0,0)  [primary]
[LAYOUT]       driven by NVIDIA GeForce RTX 2060
[LAYOUT]   \.\DISPLAY2  1920x1080 at (-562,1080)
[LAYOUT]       driven by NVIDIA Quadro P1000
```

If the preview ends up on a card other than the primary's, you get one
warning in the notification bar — at startup, and again if a display
change moves it. Once each, never per frame.

This is also why the generated default layout pins the main window to
the primary screen and never moves it. Every other window can go
anywhere; the preview is the one with a cost attached.

### Why a window can be stuck

A window can be on screen, fully drawn, and still impossible to move,
because the only part you can drag is its title bar — and that can end
up somewhere you cannot click. Two ways it happens on a multi-monitor
desktop:

- **Under a taskbar.** Monitors can each have one.
- **In the gap between screens.** Monitors arranged at different
  vertical offsets leave bands that belong to no display at all. On this
  rig there is a 19-pixel band between the bottom of one row and the top
  of the next, and the GoTo panel's title bar landed exactly in it.

So placement does not ask "is this window on the desktop" — the desktop
as Windows reports it is the *bounding box* of all the monitors, dead
space included. It asks whether the **title bar** overlaps some
monitor's usable area, taskbars excluded. Anything that fails is put on
the main screen instead, and the log says so.

If one gets stuck anyway, `Ctrl+Shift+Home`.

### If a window goes missing

Press **`Ctrl+Shift+Home`**. Every window comes back to the main screen
at a usable size, and anything that was maximised is maximised again.

This exists because a window placed on a monitor that is no longer there
is not minimised and not closed — it is invisible, focusable, and still
taking keystrokes, which looks exactly like a feature that stopped
working. The shortcut is registered with Windows rather than handled by
the app's own window, so it works whatever has focus, including when the
window you would otherwise click on is the one that went missing.

Two safeguards usually prevent you needing it. Every position is checked
against the screens that actually exist before it is used, and anything
that would land off every screen goes to the main one instead. Screens
are matched by device name rather than by position in a list, because
that order changes when the graphics drivers re-enumerate.

**The preview stays on the main screen** and is not moved by any layout.
The live image is drawn by whichever card drives its monitor, and moving
it onto a screen belonging to the other card costs a copy on every
frame. Until the app knows which screens belong to which card, leaving
it alone is the safe default rather than a considered choice.

---

## Putting the Sky Map on its own screen (v1.2.4)

The map has always been a window you could drag to any display. What it
had no way to do was **fill** one, and the reason was two window flags
rather than anything to do with monitors: it was created as a tool
window, which suppresses the minimise and maximise buttons outright,
and without `WS_MAXIMIZEBOX`, so the system menu's Maximize was greyed
and double-clicking the title bar did nothing. There was no "make this
fill the screen" control anywhere because Windows had been told not to
offer one.

Three ways now, all of which act on **whichever display the window is
currently on** — drag it where you want it first:

| | What it does |
|---|---|
| **Maximise button**, or double-click the title bar | Fills that display, keeping the title bar and respecting the taskbar. |
| **`F11`** | Fills that display completely, with no title bar and over the taskbar. `Esc` or `F11` returns it. |
| **Drag the edges** | Still works; the map redraws at any size. |

The map is also now in the taskbar and Alt-Tab, which it was not before
— useful once it lives on a screen of its own rather than on top of the
capture window.

Measured on this rig's five displays: `F11` with the map on DISPLAY2 at
(-504,1088) filled that screen, not the primary.

**The other floating panels do not get this**, deliberately. Mount
Controller, GoTo and Object Info lay their controls out at fixed
positions and have no sizing border, so maximising one would strand its
buttons in a corner of an empty window. The map and the preview are the
two surfaces that actually scale.

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

### Clearing the view — `U` (v1.2.6)

`U` hides everything drawn over the image **except the reticle**: both
side panels, the sharpness graph, the ADC arrow, the caption, the zoom
hint, the mode labels, the pixel readout and the LIVE status line. Press
it again to bring them back. The setting is saved, so the app opens the
way you left it.

The reticle stays because it is the one overlay you look *through* the
image with rather than read off it. Clearing the view to judge focus or
contrast is exactly when you still want to know where centre is.

Two reasons to reach for it:

- **Judging the image.** Numbers next to an image change how you read
  the image. With five screens the readouts already have screens of
  their own, so on the preview they are a second copy sitting on top of
  the thing they describe.
- **Showing someone.** Handing over a view of Saturn is better without
  a HUD across it.

It is `U` for **u**ncluttered, and it takes either case — this is a key
you reach for in the dark, and caps lock is not a mode anyone tracks at
2am. It replaced an earlier `Shift+O` that only covered the side panels
and the sharpness graph, which meant pressing it left the caption and
HUD line still over the image and the view not actually cleared.

### Side panels — histogram and temperature (v1.2.4)

Always on, one either side of the image, low down and sharing a
bottom edge — just above the sharpness graph on the left and the ADC
readout on the right. `U` hides them along with the rest.

They were vertically centred until v1.2.6, which put them level with
the target: a planet is centred too, so at any real zoom the two panels
were the things closest to the thing being judged, one either side of
it. Low and bottom-aligned leaves the whole middle band of the image
clear and reads as one row of instruments rather than two floating
boxes. At a very short window they stop short rather than ride up into
the caption.

**Left — histogram.** One row per Bayer channel, R, G and B, each with
its own bar chart, a white tick at the channel mean and the **clipped
percentage** on the right. The percentage is the number to watch: it
turns amber past 0.1% and red past 1%. Below the rows is the **seeing
score** out of 10.

Two details worth knowing:

- Bins 0 and 1 are not drawn. On a planetary frame that is sky, and it
  is most of the pixels.
- **Counts are on a log scale.** A planetary frame holds orders of
  magnitude more sky pixels than planet pixels, so scaled linearly
  everything except the sky spike came out under one pixel tall and the
  rows read as empty. The control panel version had the same problem
  and it mattered less there, collapsed behind a header. Log scaling
  shows the distribution; the price is that read noise is visible too.

Because the three channels share a scale you can read the **colour
balance** straight off how far each row extends to the right — on
Jupiter, R reaches furthest and B stops soonest.

**Right — temperature.** Camera and focuser temperature over the last
120 samples, about a minute: camera in green, focuser in amber. The
range in use is printed bottom-left of the panel and the current
readings bottom-right. The vertical range never shrinks below 2°C, so
a steady night reads as a flat line rather than being magnified into
apparent swings.

Both panels hide themselves if the window is too narrow to hold them
without covering the target.

### The share view (`Shift+V`, v1.2.7)

For a screen other people are looking at.

The main window is built for you: controls, histogram, temperature,
sharpness, reticle, a status bar and a HUD. That is the right window
to work in and the wrong one to point at a crowd, because almost none
of it is the planet.

`Shift+V` opens a second window that is the planet and nothing else.
It fills whichever screen it is on, borderless, with the image centred
and never stretched. Along the bottom: what you are looking at, then
one quiet line of scope, camera, exposure, gain, frame rate and how
high the target is. A red **REC** dot in the corner while you are
recording, so nobody has to ask.

Nothing on it is clickable. It will not take focus from the control
screen when you open it, which matters — the arrow keys jog the
mount, and a window that quietly captured them would be a hazard
rather than an inconvenience.

**Getting it back into a movable window:** `Ctrl+Shift+V`, from the
control screen. It fills its screen borderlessly, so there is no
caption to drag and no close button, and it does not take focus on its
own — which is why the way out is a key you press where you are
standing rather than one you press over there. Once it is windowed it
has a normal caption and edges, so drag and resize it as usual, and
`Ctrl+Shift+V` again puts it back. Whichever state you leave it in is
the state it comes back in next time you open it.

A reminder of all this appears across the top for a few seconds
whenever the view goes fullscreen, then fades so the screen is clean.

If you click on the view it takes focus, and then `F11` fills and
unfills the screen and `C` hides the caption for photographing it.
Those two keys are the only ones this window takes — everything else,
`Escape` above all, still goes where it always goes.

**Where it opens.** On whatever screen your saved layout puts it — it
is an ordinary workspace role called `share`. Drag it where you want
it and press `Ctrl+Shift+End`. Until you do, it takes a generated
default, which on a five-screen rig is the first spare screen after
the sky map's.

**What it costs.** Close to nothing. It is handed the finished image
the preview has already built rather than working one out for itself,
it shows at most 30 frames a second however fast the camera runs, and
when the window is closed the whole feature is one branch per frame.
A star party is still an imaging session and the frames still matter.

**What it does not do.** It ignores the `Z` zoom — that is a tool for
inspecting pixels, and the share view always fits the whole frame to
the screen. For a 640×480 crop on a 1920×1080 monitor that is already
a 2.25× enlargement.

**If you want the moons in it,** you need the field of view, which
means the full sensor. Press **FULL** on the toolbar. At 1280×960 the
field is 4.3 arcmin and even Io falls outside it; the whole sensor is
about 14 arcmin and holds Io, Europa and Ganymede, with Callisto
drifting in and out as it swings around.

> **An earlier version of this page warned that the full sensor is
> slow to preview. That was wrong** and the measurement behind it was
> misread — the figure quoted was the camera's frame rate, not the
> preview's, and the cause was a cost that exists only in the
> emulator. On the real camera the whole sensor previews at the rate
> the loop runs. Crop for frame rate when you want frame rate, not
> because the display cannot keep up.

### Reading the histogram (v1.2.7)

Each row is one colour channel. The bars are the distribution of pixel
values, on a **log** scale — a planetary frame is mostly sky, so on a
linear scale the sky spike is the only thing tall enough to see and
the planet works out under one pixel high.

**The number on the right is the PEAK LEVEL**, as a percentage of full
scale. It is the brightness of the brightest real thing in that
channel, which on a planet is the planet. That is the figure you set
exposure by: wind the gain or exposure up until it reaches about three
quarters, and stop before any channel reaches the end.

The bright vertical mark on each bar sits at that same level, so the
picture and the number explain each other. The faint grey tick is 75%
— where you are aiming.

Colours, which match the advice line underneath exactly:

| Reading | Meaning |
|---|---|
| **Red** | Clipping. Pixels are stuck at the top and that detail is gone for good |
| **Amber, 84% and up** | At the limit. A moment of good seeing will clip it |
| **Green, 50–84%** | Good |
| **Amber, 12–50%** | Under-exposed. Usable, but you are giving away range |
| **Grey, under 12%** | Far under — the planet is near the noise floor |

Comparing the three numbers is how you read colour balance. On Jupiter
through an 8SE you would expect red highest and blue lowest; if blue
is far behind, that is atmospheric extinction and altitude, not the
camera.

> **It used to show something else.** Before v1.2.7 this column was the
> *clipped fraction* — the proportion of pixels stuck at 255. That is
> a good alarm and a useless readout, because on any correctly exposed
> frame it reads 0.0%, which is what three rows of a working rig looked
> like. Clipping has not been dropped: a clipped channel's peak is at
> the top by definition, so it reads 100% and turns red.

### The histogram window (`Shift+H`, v1.2.4)

The same per-channel histogram as the preview panel, in a window you can
put on a screen of its own. `F11` fills that screen, `Esc` closes it.

It is the identical display, not a second implementation — one piece of
code draws both, so the log scale, the skipped bins and the clip
colouring cannot drift apart between them. What the window adds is room:
rows a third of a screen tall instead of 24 pixels, where the shape of
each channel reads from across the room and the gap between R, G and B
is obvious rather than inferred.

The small panel on the preview stays as it is, for working on one
screen or with the preview full screen.

`Shift+H` rather than a letter of its own, the same way `Shift+S` is the
framing shot — and `H` already cycles the channel display mode, which is
the same question asked a different way.

### What the histogram window adds below the bars (v1.2.6)

Three single lines under the three bar charts. The copy drawn on the
preview has none of them — it is 104 px tall and already full — so this
is the stand-alone window only (`Shift+H`).

**SEEING n/10.** Unchanged.

**Exposure headroom.** How much more exposure the channel that clips
*first* could take, in stops. That channel is the one that decides the
exposure; the others have room to spare, and raising exposure to suit
them would clip it.

| Reads | Means |
|---|---|
| `CLIPPING in R - 4.2% of pixels at 255` | Already lost. The percentage, not a stop count — a clipped channel has lost the information needed to say how far past 255 it went, so any stop figure would be invented |
| `at the limit - any more clips` | Under a quarter stop left |
| `good - 0.6 stops left in R` | Where you want to be |
| `under by 2.0 stops` | Room to raise exposure |
| `far under - the planet is near the noise floor` | Over three stops under |

Stops rather than DN because a stop is the unit the adjustment is made
in: "half a stop under" tells you to move the exposure by a factor of
1.4, where "highlight at 181 DN" does not. It is measured from the
99.9th-percentile highlight, not the mean, for the same reason
auto-exposure stopped using the mean — on a planetary frame the mean is
sky and barely moves however the planet is exposed.

**Gr/Gb split.** The difference between the two greens, as a percentage.

This is the one number here that **cannot be seen in the bars above at
all**, by construction. A Bayer quad has two greens and the G row is
their mean, so a frame with Gr=20 and Gb=180 draws a G histogram
occupying one bin at 100 — identical to a frame with Gr=Gb=100. A
160-count split, completely invisible.

| Split | Reads |
|---|---|
| under 3% | `normal` |
| 3–10% | `slight - usually just crosstalk` |
| 10–25% | `high - suspect the flat, or the Bayer phase` |
| over 25% | `very high - wrong Bayer phase, or a sensor fault` |

Every colour sensor has some, from crosstalk differing between the
green next to red and the green next to blue, which is why a few percent
says `normal` rather than warning. A large one is a real signal: it is
what a bad flat, or a debayer applied to the wrong Bayer phase, looks
like.

Worth knowing while reading the rows: because G is the mean of two
samples and R and B are single samples, **the G row is about √2 quieter
than the other two for a reason that is not the sensor's colour
response.** Comparing how far each row extends is the point of having
three rows, and one of them is being quietly denoised relative to the
others. The split figure is there so the averaging is at least visible
rather than silent.


### The sharpness window (`Shift+Q`, v1.2.4)

The rolling sharpness graph on a screen of its own, and **not** just a
larger copy of the corner overlay.

The overlay keeps 200 samples, about five seconds. That is the right
amount for *is it sharp right now* while your hand is on the focuser.
A screen of its own can answer a different question — has the seeing
settled, is focus drifting as the tube cools, was that a gust or the
mount — and that needs minutes. So this window keeps its own history of
4000 samples, fed from the same stream: roughly 90 seconds at 40fps,
longer at lower frame rates.

It also draws **gridlines at 30% and 60%**, the two thresholds the bars
are coloured by, so a value can be read off rather than guessed. Those
same thresholds are what autofocus and the capture quality gate act on.

`F11` fills the screen it is on, `Esc` closes it.

**Every window opens at startup**, on the screen its layout names —
sharpness, histogram, the sky map, GoTo, help and AI chat. A readout you
have to remember to open is one you will not be looking at when it
matters, and on a five-screen desk there is no reason to hide any of
them. The shortcuts close them when a screen is wanted for something
else.

They open *without taking focus*, so the main window keeps it and the
first keystroke of a session goes where you expect.

One change worth knowing: the GoTo panel used to start hidden
deliberately, to stop a mis-click starting a slew while imaging. That
was a fair worry when it opened over the imaging controls; it now sits
on a screen of its own, well away from anything you touch during a
capture. Press `G` if you would rather have it shut.

### The ADC alignment window (`Shift+A`, v1.2.6)

For dialling in an atmospheric dispersion corrector. **Hidden, and
inert, unless *ADC fitted to the imaging train* is ticked on the READY
screen** — see below.

The ADC *advisor* (the FOCUSER readout and the arrow on the preview)
predicts how much dispersion the atmosphere is producing at the current
altitude. It is a model and it never looks at the image, so it cannot
tell you whether the corrector you fitted is doing its job. This window
measures the dispersion **actually present in the live frame**, which is
what is left after the corrector has done whatever it is currently
doing. Turn the prisms, watch the residual fall, stop when it stops
falling.

| What it shows | Meaning |
|---|---|
| **Measured residual** | Arcsec and pixels of red-to-blue displacement still in the image. Green below 0.15″, amber to 0.40″, red beyond |
| **Predicted, uncorrected** | What the model says would be there with no corrector, and what percentage of it is left. A small residual at 70° altitude proves nothing; a percentage is comparable between nights |
| **Guidance** | What to do with your hands — spread the prisms, back them off, or rotate the ADC body |
| **Dial** | Which way the residual points, against the zenith axis. A correctly *rotated* corrector leaves a residual at 0° or 180°; off that axis means the ADC body itself is turned wrong, which no prism angle will fix |
| **Residual over time** | Whether the last nudge helped. Seeing moves the current number by more than a good nudge does, so the trend is the honest answer |

Three things about how it measures, because they decide whether to
believe it:

- **It reads the raw sensor mosaic, not the preview.** The preview's
  debayer takes red from a 2×2 block's top-left and blue from its
  bottom-right, which bakes a one-pixel diagonal offset into the two
  channels. Measured against the preview buffer on a target with no
  dispersion at all, that read **1.70 px of residual that no corrector
  could ever remove**. Reading the mosaic exposes the sampling grid, so
  the offset is a known constant to subtract.
- **It matches structure, not centroids.** Jupiter's and Saturn's belts
  are red-brown, so red's centre of brightness sits elsewhere on the
  disc than blue's for reasons that have nothing to do with the
  atmosphere. A centroid difference read **0.63″ on a banded planet with
  no dispersion**, drifting as the planet rotated — a number that would
  have you chasing cloud belts with a corrector.
- **It matches on edges.** Same reason an experienced imager watches the
  *limb* rather than the disc: the limb is in both channels at the same
  place with the same shape, while the belts are in both at wildly
  different contrast. On the same banded target, matching brightness
  invented 0.24″; matching edges reads 0.04″.

Together those take a dispersion-free target from 0.63″ of phantom
residual to **0.01″**. The floor is about 0.05″, set by red and blue
each being sampled on half the sensor grid.

It keeps a rolling window of 45 frames and shows the median, not the
latest frame. A single frame's reading at this plate scale is dominated
by seeing — consecutive frames disagree by more than the residual being
hunted, and dialling a corrector against a number that jumps is
guesswork.

**Use a bright target at the altitude you intend to image at.** There is
no dispersion worth correcting near the zenith, so there is nothing to
dial against up there.

`F11` fills the screen it is on, `Esc` closes it.

#### "ADC fitted to the imaging train" (READY screen)

A checkbox at the bottom of the READY list, **off by default**. It is
not a readiness condition — not owning a corrector never blocks
Continue — it is a statement about what is bolted to the train, and it
is remembered between sessions.

With it off, the ADC arrow on the preview, the FOCUSER readout (which
reads `ADC: not fitted`) and `Shift+A` all stay dark. The figures would
still be *true* with no corrector fitted, because the atmosphere
disperses either way — but a readout about a part you do not own is
noise, and an alignment window showing a residual is an invitation to go
and adjust something that is not there.

It saves the moment you tick it rather than when you press Continue,
because `Esc` and the close box also end that dialog, and a setting that
only persists down one of three exits is one that sometimes silently
does not.

### The status window (`Shift+D`, v1.2.6)

Capture health and disk space, one bar per row.

| Row | Bar | Right |
|---|---|---|
| `REC` | How much of the capture drive's free space this recording has used | Live FPS |
| `BUF` | Ring-buffer slots in use — amber past 50%, red past 80% | Frames lost |
| one per drive | Space **used** | Percent **free** |

**`REC` is bytes written by the current recording**, not disk usage. It
was labelled `HDD` until v1.2.6 and scaled against a flat 500 GB, which
on a 2 TB drive meant nothing and, sitting directly above a list of real
drive capacities, read as a disk with 0.188 GB on it. It is now scaled
against the free space on the drive being written to, so the bar answers
the question actually being asked mid-run: how much of tonight's
headroom is gone.

**One row per fixed drive**, by letter, with the capture drive marked
`*`. The bar grows as a drive fills — the same direction as every other
bar in the app — but the number spelled out is **free** space, because
that is what decides whether tonight's capture completes. Free turns
amber under 100 GB and red under 20 GB. Those thresholds are in
gigabytes rather than percent on purpose: 2% is ample on a 4 TB drive
and nothing on a 256 GB one, and a night of SER files is tens of GB
whatever the drive's size.

Fixed drives only. A network share can hang far longer than a sleeping
disk, a CD-ROM's free space means nothing, and a RAM disk is not where
anyone keeps a night's data.

The free-space figure is the space available **to you**, which under a
disk quota is smaller than the raw free total — and the smaller number
is the one that decides whether the capture fits.

Space is re-read every five seconds **on a background thread**. A
sleeping external USB drive can take seconds to answer while it spins
up, and doing that on the UI thread would stall the preview and the
capture loop with it. A slow drive shows numbers a few seconds stale
instead.

`F11` fills the screen it is on, `Esc` closes it.

### Reticle (`R` key, or the `[+]` toolbar button)

One control, four shapes. Each press steps to the next and the fifth
turns it off, so a single press still just switches the reticle on:

**off → crosshair → circle → rings → box → off**

| Shape | Use |
|-------|-----|
| **Crosshair** | Centring a target before tracking. Spans the frame with a gap at the middle, so the target itself is never covered. |
| **Circle** | A single ring, for judging whether a disc sits centred. |
| **Rings** | Three concentric rings for **collimation** — defocus a star until it shows as a donut and check the donut is centred in these. |
| **Box** | Corner brackets framing a region, for deciding on a crop before committing to one. Brackets rather than a full rectangle so the edge does not hide the detail just inside it. |

All four keep a centre dot — the only part that marks an exact pixel, and
what you nudge the planet onto. The toolbar button names the live shape.

### Moving, sizing and rotating it

| Gesture | Effect |
|---------|--------|
| `Ctrl` + drag | Move it anywhere in the frame |
| `Ctrl` + wheel | Make it bigger or smaller |
| `Shift` + wheel | Rotate it, 5° a notch |

Modifiers rather than plain gestures on purpose: a plain drag is the ROI
rubber band and a plain wheel is the preview zoom, both used constantly. The
reticle takes the modifiers so neither is disturbed.

**Rotation only applies to crosshair and box.** A circle and three
concentric rings look identical rotated, so the app says so rather than
accepting the input and appearing to do nothing.

The position is stored as a fraction of the frame, so it stays where you put
it when the window is resized or you change zoom. **Cycling the reticle off
resets position, size and rotation**, which is how you recover from a stray
`Ctrl`+drag without hunting for a reset button.

### Clipping Warning (`C` key)
Any pixel where R, G, or B = 255 flashes red at ~2.5Hz. The `CLIP X.X%` label appears top-right in the HUD. Useful for dialing in exposure without clipping planetary detail.

### ADC Overlay
Shown automatically when mount altitude > 0°. Bottom-right corner box showing:
- Dispersion in arcseconds
- Severity level (color-coded)
- Blue upward arrow indicating the direction of dispersion

The number depends on which filter is fitted — see [Filters](#filters). The
FOCUSER section states the band in use directly beneath the ADC readout.

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

## Night vision (`N`)

Turns the **preview and HUD** deep red: the image is reduced to luminance and
pushed into the red channel, which keeps detail visible while giving your eyes
far less to re-adapt from. **Remembered between sessions**, so if you finish a
night with it on, the next launch comes up red rather than blinding you.

**The whole interface goes red, not just the image.** The control panel, the
toolbar, the status bar, the floating windows and the dialogs all follow.
Measured by capturing the rendered windows: of the pixels sampled in the
control panel, 21% were blue- or green-dominant in day mode and **0%** with
night vision on; the status bar went from 77% to 0%.

The toolbar retains about 3% blue, and that is the anti-aliasing of the
button labels rather than any colour the app chooses — removing it would mean
changing how every font in the application is rendered.

**Accent colours converge.** Cyan, amber and green are distinguishable by
hue in day mode and all become the same red at night, because they have
almost the same brightness. That is unavoidable in a red-only palette: any
transform that kept them apart would have to put back the blue and green
that night vision exists to remove. Section headers still carry their names
in text.

One known gap: a few overlays drawn on top of the preview keep their day
colours if you switch night vision on **during** a session. Start with it on,
or press `N` twice, and they follow. The main surfaces are unaffected either
way.

Pair it with the `[FX]` brightness modes rather than raising gain if the
screen is still too bright.

---

## Framing shot (`Shift+S`, or the `[FRAME]` button)

A faint target is invisible at planetary settings, so checking whether it is
even in the frame used to mean winding gain and exposure up by hand and
remembering what they were.

One press saves your gain and exposure, goes to **gain 570 and a 2-second
exposure** for **five seconds**, then puts both back exactly as they were. The
preview is live throughout — nothing is captured to disk and the recording
settings are untouched.

**It refuses while recording**, and says so. Boosting gain mid-file would
bury unusable frames in the middle of a capture, and you would not find out
until you came to stack it.

The restore is automatic, so there is nothing to undo if you walk away. It is
`Shift+S` rather than `F` — a framing shot is a boosted snapshot, and `F` has
stepped the focuser since long before this existed.

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

## What is remembered between sessions

The app reopens where you left it. `rig.ini` keeps the live camera state and
capture format — gain, exposure, gamma, white balance, USB bandwidth,
resolution and bit depth — written whenever the format changes and on a
clean exit, and restored at launch.

Before v1.2.3 startup wrote fixed values over whatever the rig had been
doing (gain 100, 8 ms, full frame, 8-bit), so a night of tuning survived
only if it had been saved into a profile first, and the capture format did
not survive at all.

A first run still comes up at full frame, 8-bit. Values that cannot be used
are ignored individually rather than discarding the whole session, so one
bad number does not reset everything.

**This is not the same as a profile.** The session is simply where you left
off; a profile is a named set of settings for a target that you choose to
load. See [Profiles](#profiles).

Also remembered: night vision, and whether AI advice is on.

---

## Profiles

A profile is everything the rig should be for one target: gain, exposure,
gamma, white balance, USB bandwidth, resolution, bit depth, focus position,
focus step sizes, tracking mode, file prefix and capture limit.

### Loading one

**Automatically.** Picking an object on the sky map, in the GoTo panel or in
the object info panel loads the matching profile before the slew starts, so
the camera is ready when the mount arrives. Matching is by name: `Jupiter`
finds the Jupiter profile, anything starting `M` and a digit finds `DSO`,
`Moon` finds `Moon`. No match leaves the current settings alone and says so
in the log.

**By hand.** Pick it under **Target** in the CAPTURE section and press
`LOAD`.

Either way it applies the camera settings, drives the focuser to the
profile's focus position, sets the tracking mode, sets the resolution and
bit depth, and fills in the prefix and capture limit — and the panel updates
to match, so what you see is what the camera is doing.

### Resolution: who wins

Three things can set resolution, and the last one to act wins:

| Action | Effect |
|--------|--------|
| Startup | Full sensor, 8-bit — always, whatever the active profile says |
| Loading a profile | The profile's resolution and bit depth |
| A toolbar button | What you pressed, overriding either of the above |

So the app always opens at the widest field, a target selection narrows it
to what that target wants, and `FULL` is always one press away. Loading a
profile does **not** move the ROI on the sensor — a profile says how wide to
read, not where to point; the mount and the tracker decide that.

### Saving one

`SAVE` records the **live** rig, not the stored profile: the camera's actual
gain, exposure, gamma, white balance, USB bandwidth, resolution and bit
depth, the focuser's actual position, and the prefix and limit as they stand
in the boxes. So a night of tuning is kept by pressing `SAVE`, and anything
you change in CAPTURE is part of the profile from then on.

**Worth knowing about your saved profiles.** Because `SAVE` records the live
resolution, and because the emulator used to force 1280x960 regardless of
what was asked, five of the eight profiles on disk — Jupiter, Saturn, Mars,
Venus and Moon — hold 1280x960 rather than the 640x480 (or 1920x1080 for
Moon) their defaults intend. That did nothing before, because loading a
profile ignored resolution. It does something now. If you want Jupiter back
at 640x480 for the frame rate, load it, press `640`, press `SAVE`.

### Focus steps

`+` / `-` move the focuser by the profile's **fine** step and `[` / `]` by
its **coarse** step (10 and 100 by default). Change them in the profile
editor for a target that wants finer control.

---

## Advice from the AI (`W`)

Separate from the chat panel, which answers when you ask. This one watches
and occasionally remarks — at most once every three minutes, never while
recording, and only when something has actually changed. **Off by default**;
`W` turns it on and it is remembered.

**The software decides what is worth saying, not the model.** It checks for
specific conditions — target below 30 degrees, frames dropping, mount not
tracking, sensor cropped small — and the model is only asked to put one of
them into a sentence. That restriction exists because the first version let
the model judge, and it confidently advised *"lowering gain for improved
dispersion visibility"*, which is nonsense. Advice that sounds like
expertise and is wrong is worse than no advice, so the model is no longer
allowed to invent a reason it was not handed.

It never operates anything. It produces a sentence in the notification bar
and nothing else — it has no connection to the camera, mount or focuser at
all.

If nothing is wrong, it says nothing and costs nothing: with no condition
found, the model is not called.

Needs Ollama running locally. If it is not, turning advice on says so.

---

## Display stretch

Two sliders in the IMAGE section, **Black** and **White**, that decide which
part of the brightness range fills the screen:

    shown = (value - black) × 255 / (white - black)

Raise **Black** until the background goes properly black. Lower **White**
until the target's detail fills the range. Anything below black shows as
black, anything above white as white.

**This changes only what you see.** Nothing recorded is affected, which is
the point — it is how a faint target becomes visible without touching gain
or exposure, both of which would change the data. `L` switches it off and on
so you can compare, which is the only reliable way to tell whether a stretch
is helping or just making the noise bigger.

Use the **histogram** below it to choose the points. The three rows are the
real red, green and blue channels, so a single channel about to clip is
visible on its own — before v1.2.3 all three rows drew the same luminance
figure, which looked like a colour histogram but could not show that.

The status line reads `Stretch: 20-180` when it is doing something,
`Stretch: on, 0-255 (no effect)` when the window is open wide enough to
change nothing, and `Stretch: off (L)` when disabled. The sliders cannot be
crossed over; the window is held at least 4 levels wide.

---

## Dark and flat frames

Two corrections, both applied to **what you see** and not to what gets
recorded.

A **dark** records what the sensor reports with no light reaching it: thermal
signal, and hot pixels that sit at the same bright spot every frame. Subtract
it and the clutter goes, which matters most while focusing — a field of hot
pixels looks like detail.

A **flat** records how unevenly the optics deliver light: vignetting, and the
shadows of dust on the sensor window. Divide by it and the field evens out.

### Making them

Press **DARK** in the IMAGE section, **cover the telescope completely**, and
leave it. It averages 32 frames and saves the result. Press **FLAT** pointing
at something evenly lit — a white screen, or twilight sky through the scope
— again 32 frames. **CLEAR** deletes both.

It averages rather than taking one frame on purpose: a single dark carries
its own random noise into every frame you subtract it from, which can leave
you worse off than before.

Masters are saved next to your captures, so they are still there next launch.

### When a master stops being valid

A dark is only correct for the gain, exposure and temperature it was taken
at, because that is what determines the thermal signal. The status line says
which state it is in:

| Status | Meaning |
|--------|---------|
| `Cal: none` | Nothing built. |
| `Cal: dark ON` | Being applied, and nothing looks wrong. |
| `Cal: dark ON(taken at a different gain)` | **Still being applied**, but it will be imperfect. Rebuild it. |
| `Cal: dark off(wrong resolution)` | Loaded and **not** applied — you changed resolution since. Rebuild it. |

Resolution and bit depth are hard requirements: a master of the wrong shape
cannot be subtracted at all, so it is refused rather than misapplied. Gain
and exposure are warnings, because an imperfect dark usually still beats
none.

### What they do not touch

The recorded SER keeps the raw sensor data, and so does the sharpness
reading. That is deliberate: your stacker does this better with the whole
frame set, raw data stays useful if a master turns out to be wrong, and the
sharpness scale was calibrated against raw frames.

So for a real session, these are a **focusing and framing aid**. Take darks
and flats with your stacker for the final image.

---

## Filters

A filter is a piece of glass in front of the sensor that only lets certain
colours through. This rig normally has a **UV/IR cut** filter fitted — it
blocks the invisible light either side of what the eye can see, which the
camera is otherwise sensitive to and which arrives out of focus with
everything else.

The app **cannot detect** which filter is fitted. Nothing in the camera
reports it. So you tell it once, in **SETUP → Filter fitted to the camera**,
and it remembers in `rig.ini`. You only need to touch it when you physically
change filters, which on this rig means taking the camera off the scope.

### Why it matters

Atmospheric dispersion is the sky splitting a planet into a faint red and
blue fringe, worse the lower the target sits. How much depends entirely on
**which wavelengths reach the sensor** — so the same night, at the same
altitude, gives wildly different answers depending on the filter:

| Filter | 50° alt | 40° alt | 30° alt | Verdict at 40° |
|--------|---------|---------|---------|----------------|
| UV/IR cut (400–700nm) | 1.13″ | 1.61″ | 2.33″ | recommended |
| None (380–1000nm) | 1.59″ | 2.25″ | 3.28″ | **REQUIRED** |
| IR pass 685 (685–1000nm) | 0.29″ | 0.41″ | 0.60″ | not needed |
| Methane 889 (880–900nm) | 0.01″ | 0.02″ | 0.03″ | not needed |

A narrow band disperses almost nothing — that is part of why IR imaging
punches through poor seeing. A wide-open sensor with no filter disperses
most of all.

So a wrong setting is wrong in both directions that cost you something.
Told "UV/IR cut" when the camera is actually bare, the app under-warns at
40° — it says *recommended* where the truth crosses into *required*. Told
"UV/IR cut" when you are actually shooting IR-pass, it overstates by 4×,
and you would fit a corrector for a problem four times smaller than
reported.

### Where it shows up

- **FOCUSER section** — `Filter: name (low–high nm)`, directly under the ADC
  readout, so the assumption sits beside the figure it drives
- **SETUP** — the picker. An unrecognised band (because you hand-edited
  `rig.ini` with your own edges) leaves the box blank and is left alone
- **Readiness check** — a `Filter` row, listed as CHECK. It never makes the
  dialog appear on its own; it is there to be read on a night the list
  already opened for some other reason
- **AI assistant** — knows the fitted filter and that it is configured rather
  than detected

### Presets

| Name | Band | Use |
|------|------|-----|
| `UV/IR cut` | 400–700nm | Normal colour planetary. The default. |
| `None` | 380–1000nm | Bare sensor. Widest dispersion; an ADC matters most here. |
| `IR pass 685` | 685–1000nm | Cuts through poor seeing; mono-ish detail on Jupiter and Mars. |
| `Methane 889` | 880–900nm | Narrowband Jupiter/Saturn cloud structure. Very dim — long exposures. |

The upper edges are the practical limit of filter **and** sensor together.
The IMX678 still responds past 1000nm, but weakly enough that it moves the
dispersion extremes very little.

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

The check waits up to **two seconds** for the camera's first frame before it
runs. Without that it raced the capture thread it was checking, reported
`Camera FAIL — open but no frames have arrived` on a camera that was about to
work, and then blocked the launch with a modal dialog about it. A re-check you
ask for yourself does not wait, because by then no frames means no frames.

What it verifies for itself: camera streaming, dropped frames, focuser
present, mount connected, **mount aligned**, **tracking actually driving**,
observing site known, disk space, profile loaded. The two in bold are the
ones that will stop a night dead.

Three things it cannot see and must ask you: dust cap off, tube cooled to
ambient, cables dressed. These are reminders and **never** cause the dialog to
appear on their own. Ticking all three also resets the cord-wrap count, since
that is what confirming the cables means.

A fourth row, **Filter**, is stated rather than asked — it names the filter
the app believes is fitted and the band it uses for the ADC figure. It changes
only when the camera comes off the scope, so being asked about it nightly
would be noise; it is there to be read on a night the list already opened.
See [Filters](#filters).

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

**Known problem, still unresolved as of v1.2.3: this does not work on
this machine.** AutoStakkert starts, parses every option correctly, then never
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
| `RADUK_EMU_TRUE_FOCUS` | 168000 | Focuser position at which the image is perfectly sharp |
| `RADUK_EMU_BLUR_PX_STEP` | 0.0083 | Blur added per step away from focus |
| `RADUK_EMU_FREEZE_SURFACE` | off | Stops planet rotation, turbulence and moons, so only focus varies |
| `RADUK_EMU_NOISE` | 0 | Per-pixel sensor noise, DN |
| `RADUK_EMU_SEEING_AS` | 0 | Per-frame atmospheric blur, arcsec. Without it every simulated frame is equally sharp, so the seeing score, the capture gate and the stacker's frame selection have nothing to choose between |
| `RADUK_EMU_PEAK_DN` | 0 | Exposes the simulated disc to this peak level. 0 keeps the plain gain/exposure behaviour; ~200 gives a realistically exposed frame with headroom, which is what anything measuring a noise floor needs |
| `RADUK_EMU_TARGET` | *active profile* | Which planet to render. Normally the emulator renders whatever profile is selected and follows every later switch; this pins one for bench work |
| `RADUK_EMU_REMOVE_AFTER_S` | off | Simulates the camera being unplugged after N seconds |

The emulated camera reports the real 3840x2160 sensor but will only **stream
up to 1280x960**. Larger requests are clamped and logged, the way a real
camera reports a mode it cannot deliver — and because every caller reads the
granted size back, the app follows along correctly. So `FULL` and `1920` both
land on 1280x960 in the emulator while `1280` and `640` are exact. Above that
cap the software render plus a 64-slot ring stops delivering frames inside the
capture timeout altogether, which is a dead emulator rather than a slow one.

### Why the focuser starts at 166500

The simulated EAF opens at **166500**, 1500 steps short of the 168000 default
true focus. Both numbers matter, and they are not arbitrary:

- The real rig focuses near **168187**, so the emulator starts in the same
  part of the range. It used to start at 50000, which was nowhere near
  anything real.
- Autofocus searches **±3000 steps** from wherever it starts. From 50000 it
  swept 47000–53000 — about 115,000 steps away from focus — so it could
  never bracket focus from a fresh launch, failed every time, and looked
  broken when it was behaving correctly.
- The 1500-step offset means the emulator opens slightly **soft**. Starting
  exactly at focus would make an autofocus run a no-op that proves nothing.

The focus, noise and seeing switches exist because the simulated planet was
previously rendered perfectly sharp at every focuser position and perfectly
clean in every frame. Nothing that depends on focus or on averaging frames
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
- **Check the log for `[SHARP] CEILING`.** After a few minutes of imaging the
  app judges its own sharpness scale and says so if it is wrong — a ceiling
  set too low clips the focus curve flat, and autofocus then has no peak to
  find. This is the single most likely reason a sweep returns something far
  from focus. It names the constant to change.
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

### The picture looks wrong — grey, red, too bright, monochrome

Before changing any camera setting, check whether a **display mode** is on.
These change only what you see; the recording is unaffected, and nothing in
the camera is broken.

| What you see | Likely mode | Turn it off with |
|--------------|-------------|------------------|
| Greyscale / monochrome | RAW debayer | `[C] COLOR` button |
| Everything red | Night vision | `N` |
| Grey, but one colour's detail | Single channel (R/G/B) | `H` until it reads `CH: ALL` |
| Unnaturally bright or smeared | Display FX / stack | `B` until it reads `FX: NORMAL` |
| Briefly very bright and noisy | Framing shot — restores itself after 5s | wait |
| Circles or brackets drawn on it | Reticle | `R` until it turns off |

The AI assistant is given this same list of what is currently on, so asking
it "why does my image look like this" works.

If none of those are on, the cause is exposure, focus, or the sky — not the
display.

### The view has gone narrow / I cropped and cannot get back
Press **`FULL`** on the top bar. `[ROI]` and dragging a box on the preview
both narrow the sensor read-out, and before v1.2.3 nothing widened it again,
so restarting the app was the only way back. It no longer is.

If `FULL` gives you less than 3840x2160, the camera clamped the request and
the app is showing you what it actually got — in the emulator that is always
the case, by design.

### I cannot change the data rate / file size
Two controls, both on the top bar: the resolution buttons and `8b`/`16b`.
A SER file is uncompressed, so the rate is simply width x height x depth x
frame rate — there is no separate bitrate to set. The **USB Bandwidth**
slider in the CAMERA section is a different thing: it limits how hard the
camera pushes the bus, and is what to lower when `LOST` starts climbing.

Both the resolution and bit-depth buttons close the current recording, since
a SER file cannot change format part-way through. Set the format first.

### ADC reading seems wrong
- **Plate-solve once and read the log.** The ADC figure scales with the plate
  scale, and `PlateScaleArcsecPx` in `rig.ini` is calculated rather than
  measured. Every successful solve now logs
  `Plate scale: solved X"/px vs rig.ini Y"/px`, and flags anything more than
  5% out. Correct it there and the ADC figure corrects with it.
- **Check the filter first.** The `Filter:` line directly under the ADC
  readout says which band the figure was computed over. If that does not match
  the glass actually on the camera, the number is wrong by as much as 4× —
  this is the most likely cause. Fix it in SETUP; see [Filters](#filters).
- ADC uses cached mount altitude — verify mount is tracking and RA/Dec updating in TELESCOPE section
- Ensure mount is polar-aligned (single-arm mount errors compound at low altitude)

---

## What is not built yet

Listed so nobody goes looking for it, and so the assistant does not promise
it. These are planned, not missing by accident — see
`RadukCapture-Architecture.md` for the full roadmap.

**Calibration frames are a preview aid only.** Dark and flat correction
exist and are applied to what you see, but the recorded SER stays raw — see
[Dark and flat frames](#dark-and-flat-frames). For the final image, take
darks and flats in your stacker.

**Output formats.** SER only, plus PNG snapshots. No AVI, and no FITS — the
plate solver reads FITS but nothing here writes it.

**Focus aids beyond autofocus.** No multi-star FWHM score and no Bahtinov
mask assistant. Autofocus plus the sharpness readout is what there is.

**Polar alignment assistant.** Nothing guides a polar align. The align
helper covers mount orientation and the keep-out sector, which is a
different job.

**Alignment and framing helpers.** No push-to assistant, no click-on-the-
preview to recentre, and no "go to previous image" framing recall. The
reticle (four shapes, movable, sizeable, rotatable) and the framing shot do
exist — see the **Reticle** and **Framing shot** sections.

**Display extras.** No multi-monitor workspace — every window opens on
whichever screen the main window is on, and positions are not remembered
between runs. Night vision (now including the whole interface), the
single-channel R/G/B view and the display stretch all exist — see
[Display stretch](#display-stretch).

**Analysis extras.** No sensor-analysis or smart-histogram tooling, no
stellar photometry, no multi-session statistics dashboard, no solar/lunar
framing assistant, and no image annotation overlay on the preview.

**ADC alignment tool.** The ADC *advisor* tells you how much dispersion
there is and whether a corrector is worth fitting (see [Filters](#filters)).
It does not help you dial one in.

**Stacking.** Deliberately not built. The quick-look stacker is a preview so
you know that night whether a capture was soft; AutoStakkert does the real
work, and nothing here would beat it.

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
| `FilterName` | Filter fitted to the camera. One of the presets, or your own label. |
| `NightVision` | `1` restores night vision at launch. |
| `AdvisoryEnabled` | `1` turns on occasional AI advice (`W`). |
| `AdvisoryIntervalS` | Seconds between advice checks; 30-3600, default 180. |
| `LastRoiWidth` / `LastRoiHeight` / `LastBitDepth` | Capture format to restore at launch. See **What is remembered between sessions**. |
| `LastGain` / `LastExposureMs` / `LastGamma` / `LastWbRed` / `LastWbBlue` / `LastUsb` | Camera state to restore. Each is ignored individually if out of range. |
| `FilterLowNm` / `FilterHighNm` | Band edges in nanometres; these are what the ADC model actually uses. Edit them for a filter that is not a preset — the SETUP picker will then show blank rather than overwrite them. Both are reset to the UV/IR cut default if either falls outside 300–1200nm, or the band is narrower than 10nm, because a nonsense band would push the Edlen model outside its domain and return an authoritative-looking meaningless figure. |

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
| v1.2.3 | 80 | Both configurations build from one script — the hardware build had silently been broken for four phases |
| v1.2.3 | 80 | Emulator exposure control (`RADUK_EMU_PEAK_DN`); the noise-floor theory tested and disproved |
| v1.2.3 | 80 | Review pass: eight confirmed defects fixed |
| v1.2.3 | 80 | Control panel opens with every section collapsed |
| v1.2.3 | 80 | Focuser GoTo field can be typed into again — a periodic refresh was overwriting it mid-keystroke |
| v1.2.3 | 81 | Emulator renders the selected target, and the focuser starts where the real rig actually focuses |
| v1.2.3 | 81 | Autofocus pass 2: sample vectors cleared before arming, not after, closing a race |
| v1.2.3 | 82 | ADC dispersion computed across the fitted filter's band instead of a hardcoded 450–700nm |
| v1.2.3 | 83 | Filter stated in the focuser panel, the readiness list and the assistant's context; picker in SETUP |
| v1.2.3 | 84 | Resolution and bit-depth buttons on the toolbar — a crop is recoverable, and 16-bit is reachable |
| v1.2.3 | 84 | One shared capture-format path replaces three divergent copies; ROI placed by centre, not origin |
| v1.2.3 | 84 | Startup readiness check no longer races the first frame and false-fails the camera |
| v1.2.3 | 84 | Emulator honours requested ROI up to 1280x960; its FrameSize() no longer ignores its arguments |
| v1.2.3 | 85 | Loading a profile applies its resolution, bit depth and capture limit, and the panel updates to match |
| v1.2.3 | 85 | CAPTURE and PROFILE merged into one section — they held the same values and disagreed |
| v1.2.3 | 85 | Profile focus step sizes reach the `+`/`-` and `[`/`]` keys for the first time |
| v1.2.3 | 86 | The assistant is told the capture format, whether it is a crop, and the real target and profile names |
| v1.2.3 | 26 | Night vision remembered between sessions (the mode itself already shipped) |
| v1.2.3 | 45 | Multi-reticle: crosshair / circle / rings / box, cycled by one control |
| v1.2.3 | 39 | Framing shot — `Shift+S` boosts gain and exposure for 5s, then restores |
| v1.2.3 | 87 | The assistant is told which display modes are altering the image, so "why does it look wrong" starts in the right place |
| v1.2.3 | 88 | Startup restores the last camera state and capture format instead of overwriting them |
| v1.2.3 | 12b | Occasional AI advice (`W`) — conditions detected in code, the model only phrases them |
| v1.2.3 | 27 | Master dark: 32-frame average, subtracted from the preview |
| v1.2.3 | 28 | Master flat: normalised per Bayer position so vignetting is corrected without shifting colour |
| v1.2.3 | 37 | Display stretch (`L`), and the histogram shows the real R/G/B channels for the first time |
| v1.2.3 | 45 | Reticle moves, sizes and rotates (Ctrl+drag, Ctrl+wheel, Shift+wheel) |
| v1.2.3 | 26 | Night vision reddens the whole interface, not just the preview |

---

*RadukCapture Help v1.2.3 — Updated after each release*
