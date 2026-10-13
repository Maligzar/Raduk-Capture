# Operations Guide

How to run a night. This page is the operator's view; the engineering pages
explain why each step works the way it does.

---

## Safety

This comes before everything else, and it is not boilerplate.

**Never point the telescope at the Sun.** Not for a moment, not "just to
check", not with sunglasses on. An 8-inch telescope gathers roughly **800
times** more light than an eye. Looking through it at the Sun destroys
sight **instantly, permanently, and without any pain to warn you.** It will
also cook the camera and can set the inside of the tube smouldering. There
is no safe way to do this with this equipment and no accessory here that
makes it safe. **If the Sun is above the horizon, the telescope stays capped
and parked.**

The [AI assistant](AI-Assistant) is instructed to refuse to help point the
telescope anywhere if the Sun is up or could be, and to say why.

**Never push, pull or twist the telescope by hand while it is powered on.**
It moves itself with motors, and forcing it strips the gears — an expensive,
permanent repair. Use the arrow keys or the hand controller.

**The stop key is `Space`. `Esc` does the same thing.** Either halts all
movement immediately, from any window, at any time.

> If anything at all looks wrong — the telescope moving when it should not,
> a cable pulling tight, a noise you do not recognise, or you are simply not
> sure — **press `Space` first and work out why afterwards. Nothing is
> damaged by stopping. Plenty can be damaged by hesitating.**

**Mind the cables.** Several run from the telescope to the computer, and as
the telescope turns they can wrap around the mount or pull a plug out
mid-session. The app watches for this
([cord wrap](Mount-Control#cord-wrap)) but look at the actual cables now
and then. **Never let one go taut.**

**It is heavy and top-heavy.** The tube alone is ~6 kg on a tripod that can
tip if knocked. Keep the legs where nobody will trip over them in the dark.

**Use red light.** A white torch or a phone screen at full brightness ruins
everyone's night vision for about twenty minutes. Press `N`.

---

## First night, step by step

Written for someone who has never used a telescope. You do not need to
understand any of the words to get a picture of a planet.

**What you are doing:** pointing a telescope at a planet, getting it sharp,
and recording a short video. Later, software stacks the good frames into one
clean photograph. **You are not taking a single photo — you are collecting
thousands of slightly wobbly ones.**

**Why it wobbles:** you are looking through 100 km of moving air. The planet
boils and shimmers. This is *seeing*, and it is the reason for all the
machinery here. **Nothing is broken when the image wobbles.**

### 1. Put the telescope outside early

An hour before you want to look, carry it outside and take the cap off.
Glass and metal warmer than the night air make heat ripples inside the tube
that blur everything. **Nothing you do later fixes this.** Forty-five
minutes minimum; ninety is better.

### 2. Switch on and start the app

Power the mount, plug in camera and focuser, start RadukCapture. It finds
the equipment itself.

If something is missing it says so and names the cable to check. **A list
appearing is normal and not a failure** — read it, fix what it names, press
**Re-check now**. The list updates live, so you can stand at the telescope
and watch a line turn green as you plug something in. See
[readiness check](Advisors#readiness-check).

### 3. Teach the mount where it is pointing

The telescope does not know which way it faces when it wakes up. Until it
does, it cannot follow anything and **it will refuse to track**.

On the hand controller: **Align → Quick Align**, accept the prompts. Needs
no stars, takes under a minute.

> **If you skip this, everything later appears to work and then quietly does
> not. This is the single most common reason a night produces nothing.**

### 4. Choose something to look at

Press `G`. The panel lists what is up tonight, brightest first, greying out
anything below the horizon.

**Good first targets:** the Moon, Jupiter, Saturn. **Not for a first
night:** anything faint or fuzzy — galaxies and nebulae need a different
technique entirely.

Pick one and press **GoTo**. Let the slew finish.

### 5. Find it on the screen

The camera sees a patch of sky about the width of a pencil held at arm's
length, so seeing nothing at first is normal.

If the screen looks empty:

- **Press `B`** a couple of times for `BOOST ×2` and `×4`. Brightens the
  display so a dim smudge becomes visible; **does not change what is
  recorded**.
- **Nudge with the arrow keys** and watch for something crossing the frame.
- **Open `MOUNT CTRL` and watch the [motion plot](Mount-Control#motion-monitor)**
  while you press an arrow key. If the trail does not move, the telescope is
  not moving — a *different* problem from being pointed at the wrong patch
  of sky, and worth knowing before you hunt any further.
- **Check the lens cap is off.** This catches everybody eventually.

A planet looks like a small bright blob at this stage, probably fuzzy.

### 6. Get it sharp

Press `A` for [autofocus](Autofocus). **It takes about a minute. Let it
finish.**

When it is done it reports what it found:

- "Focus got worse, reverted to start" → **the app being honest rather than
  failing.** Read the suggestion and try again.
- "Focus not bracketed" → true focus is outside the ±3000-step range it
  searched. Focus roughly by hand first, then re-run.

You can also focus by hand while watching the picture.

### 7. Watch before you record

Press `B` until it says `STACK 10 BEST`. The app keeps the sharpest recent
frames, lines them up, and averages them. Detail lost in the shimmer appears
— Saturn's rings separating from the disc, cloud bands on Jupiter.

Two numbers top-right: `LIVE` for the single frame, `STACK` for the combined
one. **Higher means sharper.**

> Watch `STACK` for half a minute. **If it is drifting down, the air is
> getting worse, and waiting is better than recording.**

### 8. Record

Set a length — a minute or two is plenty — and press **RECORD**. Display
effects switch off automatically so raw frames are saved untouched.

Stop, and you are done. The video is in the captures folder, ready for
stacking.

---

## Recording workflow

1. Set the target name in the CAPTURE section's file prefix box
2. Select the frame type — Light for normal captures, Dark/Flat/Bias for
   calibration
3. Set the limit — frames (e.g. 5000) or time (e.g. 120 s)
4. Optionally enable **GATE** and set MIN SEEING, so only sharp frames are
   recorded
5. Click **▶ RECORD** — the SER file opens immediately, the button turns red
6. Press `P` to pause if seeing deteriorates; press again to resume. **The
   SER file stays open**
7. Click **■ STOP RECORDING**, or let the limit be reached

Set the capture format **before** you start. Changing resolution or bit
depth closes the current recording, because a SER file cannot change format
part-way through. See
[Capture Pipeline](Capture-Pipeline#format-changes).

---

## Keyboard

Keys work **whatever window or panel has focus**.

### Safety

| Key | Action |
|-----|--------|
| `Space` | Emergency stop all mount movement |
| `Esc` | Emergency stop — works in fullscreen too |
| `F11` | Toggle fullscreen (`Esc` no longer does this) |

### Mount and focus

| Key | Action |
|-----|--------|
| `↑ ↓ ← →` | Jog the mount — hold to slew, release to stop |
| `A` | Autofocus — start / abort |
| `+` / `-` | Fine focus step (profile's `step_fine`) |
| `[` / `]` | Coarse focus step (profile's `step_coarse`) |
| `F` / `Shift+F` | Focus step: fine / coarse |
| `G` | GoTo panel |
| `M` | Sky map |

### Capture

| Key | Action |
|-----|--------|
| `P` | Pause / resume recording |
| `S` | Save snapshot (PNG) |
| `Shift+S` | Framing shot — boost gain and exposure 5 s, then restore |
| `K` | Stac**K** the last finished recording in AutoStakkert |

### Display

| Key | Action |
|-----|--------|
| `B` | Cycle Display FX: Normal → Boost ×2 → ×4 → Stack 3/5/10 |
| `C` | Clipping warning overlay |
| `H` | Cycle channel: All → Red → Green → Blue |
| `L` | **L**evels — display stretch on/off |
| `N` | Night vision |
| `Q` | Quality bar graph |
| `R` | Reticle: off → crosshair → circle → rings → box → off |
| `V` | RGB align |
| `X` | Pixel value readout |
| `Z` | Cycle zoom: FIT → 50% → 100% → 200% |
| Mouse wheel | Zoom preview |
| `Ctrl`+drag / `Ctrl`+wheel / `Shift`+wheel | Move / size / rotate the reticle |

### Help and assistant

| Key | Action |
|-----|--------|
| `?` | Help window (live search filter) |
| `W` | **W**atch — occasional advice from the local AI. Off by default |

---

## Dark and flat masters

**Both are applied to the preview only. The recorded SER stays raw.**

### Making them

**Dark:** cap the telescope, press **DARK** in the IMAGE section. 32 frames
are averaged. The notification says "Master dark built — uncover the
telescope" when done.

**Flat:** point at an evenly illuminated surface (twilight sky, a flat
panel), press **FLAT**.

**CLEAR** discards both.

### When a master stops being valid

A master applies only at the resolution and bit depth it was taken at. The
status line says so when it does not apply. A dark is also temperature- and
exposure-dependent in practice.

If your saved file looks noisier than the screen, **that is why, and it is
correct.** For the final image, take darks and flats in your stacker. See
[Image Analysis](Image-Analysis#dark-and-flat-masters).

---

## Output files

Default `D:\RadukCapture\captures`:

| File | Contents |
|------|----------|
| `*.ser` | Raw Bayer frames, SER v3 |
| `*_quality.txt` | Per-frame sharpness, plus mean and peak |
| `*_snap_*.png` | PNG snapshots |
| `session_log.txt` | Session events, temperatures, status |

Naming:
`<prefix>_<frameType>_<W>x<H>_RAW<bits>_<YYYYMMDD>_<HHMMSS>.ser`

Also `D:\RadukCapture\logs\session_<timestamp>.log` and `raduk_log.txt`.

---

## Reading the HUD

```
LIVE 3840×2160 RAW8 38.3 FPS FRAMES: 2151 DROPS: 0
```

Click the resolution readout for a reset-to-full-sensor popup.

| Reading | Watch for |
|---------|-----------|
| `FPS` | Higher is better — more frames to choose from |
| `DROPS` / `LOST` | **Should be 0.** Anything else is a USB or disk problem, and those frames are gone |
| `TRK` | Must say **ON**. `OFF` means the planet drifts out of frame within a minute or two. `UNKNOWN` means the mount could not be read |
| `BUF` | Rising means the disk is not keeping up |
| `LIVE` / `STACK` | Relative sharpness. Higher is sharper; no universal good value |
| `Seeing n/10` | Relative to the best recent frame. Watch how far it **dips** |

Full explanation: [Image Analysis](Image-Analysis#reading-the-numbers).

---

## Before a clear night

The repository carries a dedicated checklist:
[`docs/CLEAR-NIGHT-CHECKLIST.md`](https://github.com/Maligzar/Raduk-Capture/blob/master/docs/CLEAR-NIGHT-CHECKLIST.md).
It is the manual test plan for anything the
[emulator cannot settle](Emulator-and-Testing#what-the-emulator-cannot-tell-you)
— worth reading before a night you intend to use for verification rather
than imaging.

Quick config sanity check:

- `rig.ini` site set, and `SiteSource=manual` if you entered it by hand
- `FilterName` matches the filter **physically** on the camera
- `SiteElevationM` set — it drives the [ADC](Advisors) pressure model
- `CABLES OK` pressed after dressing the cables

---

**Next:** [Troubleshooting](Troubleshooting) · [Image Analysis](Image-Analysis) · [Configuration Reference](Configuration-Reference)
