# RadukCapture Help & Reference
### Built for the stars

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
- **9× / FAST / MED / FINE** — Slew rate selector.
- **STOP** — Immediately halt all movement (also `Space` key).
- **★ ALIGN** — One-point align on a centred object: syncs the mount to the
  object's computed position **and** switches tracking on. Pick the target from
  the list; planets are offered first. The result is reported honestly —
  "Aligned ... tracking ALT-AZ", "Synced ... mount refused tracking", or
  "Mount refused the sync".
- **RA / Dec readout** — Live coordinates updated every 500ms.

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
| `STACK 3f` | Rolling average of last 3 frames (~75ms at 40fps) |
| `STACK 5f` | Rolling average of last 5 frames (~125ms) |
| `STACK 10f` | Rolling average of last 10 frames (~250ms) |

Active mode shown in amber HUD top-right (e.g. `BOOST ×2` or `STACK 5f`). **FX automatically resets to NORMAL when recording starts** — you always capture raw frames. Also accessible via `[FX]` button in the IMAGE panel section.

Use Boost to find a dim target or check focus in poor seeing. Use Stack to reduce noise when evaluating optics or alignment.

### HUD Status Bar (bottom)
`LIVE 3840×2160 RAW8 38.3 FPS FRAMES: 2151 DROPS: 0`
- Click on the resolution readout to get a reset popup (reset to full sensor resolution).

---

## Autofocus Routine

1. Press `A` or click `★ AUTO FOCUS`.
2. The routine steps outward from the current EAF position in coarse steps, measuring sharpness at each point.
3. After the initial scan, it fits a parabola to the sharpness curve.
4. The focuser moves to the computed best-focus position.
5. A fine confirmation pass verifies the result.
6. Press `A` again to abort mid-run.

**Tips:**
- Start near focus. The routine works best within ~5000 steps of focus.
- Use on a bright star or planetary disk — not on a faint DSO.
- If the routine fails, check that the EAF is connected (FOCUSER section shows position).

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
- Check the reported result. `sharp=0.0%` means the curve was fitted to noise
  and the position returned is meaningless.
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

---

*RadukCapture Help v1.1.9 — Updated after each release*
