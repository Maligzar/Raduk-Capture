# RadukCapture Help & Reference
### v1.0.10 — Built for the stars, from Anthem, Arizona

---

## Quick Key Reference

| Key | Action |
|-----|--------|
| `Space` | Emergency stop all mount movement |
| `A` | Toggle Autofocus (start / abort) |
| `C` | Toggle Clipping Warning Overlay |
| `F` | Toggle Fullscreen |
| `G` | Open GoTo panel |
| `M` | Open Map (Sky Map) |
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
Eight buttons across the top of the preview window:

| Button | Key | Function |
|--------|-----|----------|
| `[CENTROID]` | — | Start centroid tracking (Stage 1: Search) |
| `[LOCK]` | — | Advance to Stage 2: Center target |
| `[ROI]` | — | Advance to Stage 3: Lock ROI |
| `[C] COLOR` | — | Toggle COLOR / RAW debayer mode |
| `[*] MAP` | `M` | Toggle Sky Map window |
| `[I] GOTO` | `G` | Open GoTo panel |
| `[+] RETICLE` | `R` | Toggle crosshair reticle overlay |
| `[CLIP]` | `C` | Toggle clipping warning (red flash on saturated pixels) |

Buttons highlight (bold) when their feature is active.

---

## Control Panel Sections

All sections are collapsible — click the section header to expand/collapse.

### IMAGE
- **ZOOM: FIT / FULLSCR** — Switch between fit-to-window and fullscreen preview.
- **RGB ALIGN** — Toggle atmospheric dispersion correction on the display. Auto-computes pixel shift from mount altitude using the Edlen refractivity model. Blue channel shifts UP, red shifts DOWN by half. Display only — SER data is unaffected.
- **Manual slider** — Fine-tune the RGB align offset ±20px on top of the auto value. Center position = 0px offset.
- HUD shows `RGB ALIGN +N px` in amber when active.

### PROFILE
- Load, save, and edit named target profiles (e.g. Jupiter, Saturn, Moon).
- Profiles store: gain, exposure, gamma, white balance, output directory, frame prefix, capture limits, and gate threshold.

### CAMERA
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
- **ADC: severity (N.N")** — Live atmospheric dispersion measurement. Computed from mount altitude using the Edlen 1966 model for Anthem AZ (P=918hPa, T=288K). Shows NONE / MINOR / MODERATE / SIGNIFICANT / SEVERE. Use as a guide for when to apply an ADC or use RGB Align.

### TELESCOPE
- **Mount name / COM port** — Celestron 8SE on COM3 (NexStar serial protocol, not ASCOM).
- **Az / Alt / RA / Dec / HA** — Live mount coordinates, updated every 500ms.
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
- **Tracking** — Toggle sidereal tracking on/off.
- **Park** — Send mount to park position.
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
| Mount | NexStar (8SE single-arm) | COM3 (serial) |
| Alignment | StarSense AutoAlign | — |

**Plate scale:** 0.203 arcsec/pixel (f/10, 2032mm FL, 2.9µm pixels)
**Sensor:** Sony IMX678, 3840×2160, 2.9µm pixel pitch
**EAF safe test position:** 50000 (physical position currently 150000)
**HCG threshold:** gain 182 (above this = high-conversion-gain mode)
**Site:** Anthem, AZ — Bortle 4, elevation 1732ft, UTC-7 (no DST)
**ADC model:** Edlen 1966, P=918hPa, T=288K

---

## Troubleshooting

### No signal / black preview
- Check USB connection to ASI678MC
- Check that camera is not in use by another application (close FireCapture if open)
- Restart RadukCapture

### Mount not responding
- Check COM3 is available (not locked by CPWI)
- If CPWI is running, disconnect mount in CPWI first, then RadukCapture will grab COM3
- Use `RELEASE` in the TELESCOPE section to hand COM3 back to CPWI

### Frames dropping (LOST > 0)
- Reduce USB bandwidth slider in CAMERA section
- Close other USB-intensive applications
- The write buffer is 512 slots — sustained drops mean disk write speed is insufficient

### Autofocus fails / doesn't converge
- Ensure EAF is connected (position shows a number, not dashes)
- Ensure target is bright enough for sharpness measurement
- Check EAF is not at travel limits (safe zone: 10000–490000)

### GoTo misses target
- Run `POINT SOLVE → SYNC` to correct mount model at current position
- Ensure StarSense alignment was performed before session

### ADC reading seems wrong
- ADC uses cached mount altitude — verify mount is tracking and RA/Dec updating in TELESCOPE section
- Ensure mount is polar-aligned (single-arm mount errors compound at low altitude)

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

---

*RadukCapture Help v1.0.11 — Updated after each release*
