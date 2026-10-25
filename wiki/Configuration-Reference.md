# Configuration Reference

Two configuration systems. `rig.ini` describes **the rig** — one per
installation, never committed. Target profiles describe **a target** — eight
built-ins, versioned in the repository.

Both use `GetPrivateProfileString` / `WritePrivateProfileString`, the Win32
INI API, so both are hand-editable.

---

## `rig.ini`

Sits next to the executable. **Not tracked by git**, and the only place your
real observing site is stored. Created on first save. A singleton loaded on
first access:

```cpp
static RigConfig& Get() {
    static RigConfig cfg = LoadFromDisk();
    return cfg;
}
```

### Minimal working file

```ini
[Rig]
SiteLatDeg=0.0
SiteLonDeg=0.0
SiteElevationM=0.0
PlateScaleArcsecPx=0.203
GuideRateArcsecPerSec=7.5
; The filter physically fitted to the camera. Not detectable --
; the dispersion model computes across this band, so a wrong
; value moves the ADC figure by up to 4x.
FilterName=UV/IR cut
FilterLowNm=400
FilterHighNm=700
```

### Full key reference

#### Site

| Key | Meaning |
|-----|---------|
| `SiteLatDeg` / `SiteLonDeg` | Observing site, +N and +E. Auto-detected if absent |
| `SiteElevationM` | Metres above sea level. **Sets the [ADC](Advisors#atmospheric-dispersion-advisor) pressure model** |
| `SiteSource` | `manual`, `windows`, `gps`, or `mount`. **A `manual` site is never overwritten by an automatic one** |

> **`siteValid` is tracked separately from the coordinates, and that is not
> redundancy.** 0,0 is both the default *and* a legal coordinate — a real
> spot in the Gulf of Guinea. So the two were indistinguishable, an absent
> `rig.ini` looked exactly like a deliberate site on the equator, and a
> wrong site was pushed to the mount in silence.

#### Optics and guiding

| Key | Default | Meaning |
|-----|---------|---------|
| `PlateScaleArcsecPx` | 0.203 | **Nominal** image scale. Seeds overlays and the ASTAP FOV hint. **The correction loop never uses it** — see [Tracking](Tracking-and-Guiding) |
| `GuideRateArcsecPerSec` | 7.5 | Assumed guide rate. Calibration **measures** the real one and warns if they disagree by more than 2× |
| `MountPort` | *(empty)* | Pin a COM port. Empty means auto-detect; a stale value falls back to auto-detect rather than failing |
| `WsToken` | *(generated)* | The [dashboard's](Telemetry-and-Dashboard) access token: 64 hex characters, made on first launch. Under 32 hex characters and the server admits nobody. Delete it to get a new one |

#### Filter — the one that silently changes a number

| Key | Meaning |
|-----|---------|
| `FilterName` | One of the presets, or your own label |
| `FilterLowNm` / `FilterHighNm` | Band edges in nm. **These are what the ADC model actually uses** |

Edit the edges directly for a filter that is not a preset — the SETUP picker
will then show blank rather than overwrite them.

> **Both edges are reset to the UV/IR cut default** if either falls outside
> 300–1200 nm, or the band is narrower than 10 nm, because a nonsense band
> would push the Edlen model outside its domain and return an
> authoritative-looking meaningless figure.

#### Mount geometry

| Key | Default | Meaning |
|-----|---------|---------|
| `WrapLimitDeg` | 360 | Cumulative azimuth travel before the [cord-wrap warning](Mount-Control#cord-wrap) |
| `KeepOutStartDeg` / `KeepOutEndDeg` | 0 / 0 | Obstruction sector, **clockwise from start to end**. Equal values = none |
| `ObserverBearingDeg` | −1 | Mount → observer, degrees true. Used by the [align helper](Mount-Control#align-helper). `−1` means unasked; **0 is a legal bearing** (due north) |

> Equal keep-out values mean no arc, which is the default: most sites do not
> need one, and **an accidental sector would be worse than none.**

#### Behaviour switches

| Key | Default | Meaning |
|-----|---------|---------|
| `ShowAlignHelper` | 1 | `0` suppresses the align helper (set by "Don't show again") |
| `ShowReadinessCheck` | 1 | `0` suppresses the [readiness dialog](Advisors#readiness-check); the log line and the READY button remain |
| `NightVision` | 0 | `1` restores [night vision](Rendering-and-UI#night-vision-n) at launch |
| `AdvisoryEnabled` | 0 | `1` turns on [occasional AI advice](Advisors#advisory-loop) (`W`) |
| `AdvisoryIntervalS` | 180 | Seconds between advice checks, 30–3600 |
| `AutoStackOnStop` | 0 | Hand each finished recording to [AutoStakkert](Image-Analysis#autostakkert-handoff) automatically |
| `AutoStakkertPath` | *(empty)* | Blank probes the usual places |

#### Last session state (Phase 88)

| Key | Meaning |
|-----|---------|
| `LastRoiWidth` / `LastRoiHeight` / `LastBitDepth` | Capture format to restore at launch |
| `LastGain` / `LastExposureMs` / `LastGamma` / `LastWbRed` / `LastWbBlue` / `LastUsb` | Camera state to restore |

> **Why these exist.** Startup used to write hardcoded values over whatever
> the rig was last doing — gain 100, 8 ms, full frame, 8-bit. A night's
> tuning survived only if it had been saved into a profile first, and **the
> capture format did not survive at all.**

`-1` / `0` means "never stored", and then the old defaults apply, so a first
run and an upgrade both behave as before. `HasLastCamera()` and
`HasLastFormat()` test this.

**Each value is validated and ignored individually** if out of range, rather
than discarding the whole stored session — so one bad number does not reset
everything.

**This is not the same as a profile.** The session is where you left off; a
profile is a named set of settings for a target that you choose to load.

### The three values that bias results and cannot be detected

Since Phase 77 these report themselves rather than sitting silent:

| Key | Getting it wrong costs |
|-----|------------------------|
| `SiteLatDeg` / `SiteLonDeg` | Alt/az maths, the sky map, and GoTo, entirely |
| `SiteElevationM` | ~10% high at sea level, ~15% low on a mountain, on every ADC figure |
| `FilterLowNm` / `FilterHighNm` | Up to **4×** on every ADC figure, in the direction that under-warns |

---

## Target profiles

`profiles/*.ini`, eight built-ins. Written with defaults on first launch if
absent.

`jupiter.ini`, `saturn.ini`, `mars.ini`, `venus.ini`, `moon.ini`,
`dso.ini`, `star.ini`, `planet_focus.ini`

### Format

```ini
[profile]
name=Jupiter
[camera]
gain=100
exposure_ms=8.0000
gamma=50
wb_red=52
wb_blue=47
usb_bandwidth=80
high_speed=true
[roi]
width=1280
height=960
binning=1
img_type=0
[focuser]
position=150000
step_coarse=100
step_fine=10
[mount]
tracking_mode=alt_az
slew_rate=9
goto_target=
[capture]
format=SER
limit_type=frames
limit_value=3000
file_prefix=Jupiter_
[notes]
text=
```

A profile is everything the rig should be for one target: camera settings,
resolution, bit depth, focus position, focus step sizes, tracking mode, file
prefix, and capture limit.

### Loading

**Automatically.** Picking an object on the sky map, in the GoTo panel, or in
the object info panel loads the matching profile **before the slew starts**,
so the camera is ready when the mount arrives.

Matching is by name: `Jupiter` finds the Jupiter profile; anything starting
`M` plus a digit finds `DSO`; `Moon` finds `Moon`. **No match leaves the
current settings alone and says so in the log** — it does not fall back to a
default, which would silently reconfigure the camera for the wrong target.

**By hand.** Pick it under Target in the CAPTURE section and press `LOAD`.

Either way it applies the camera settings, drives the focuser to the
profile's focus position, sets the tracking mode, sets resolution and bit
depth, fills in prefix and capture limit — **and the panel updates to
match**, so what you see is what the camera is doing.

### Resolution: who wins

Three things can set resolution, and the last to act wins:

| Action | Effect |
|--------|--------|
| Startup | Full sensor, 8-bit — **always**, whatever the active profile says |
| Loading a profile | The profile's resolution and bit depth |
| A toolbar button | What you pressed, overriding either |

So the app always opens at the widest field, a target selection narrows it to
what that target wants, and `FULL` is always one press away.

**Loading a profile does not move the ROI on the sensor.** A profile says how
wide to read, not where to point; the mount and the tracker decide that.

### Saving

`SAVE` records the **live** rig, not the stored profile: actual gain,
exposure, gamma, white balance, USB bandwidth, resolution and bit depth, the
focuser's actual position, and prefix and limit as they stand in the boxes.

> **A caveat about the shipped profiles.** Because `SAVE` records the live
> resolution, and because the emulator used to force 1280×960 regardless of
> what was asked, **five of the eight profiles on disk — Jupiter, Saturn,
> Mars, Venus and Moon — hold 1280×960** rather than the 640×480 (or
> 1920×1080 for Moon) their defaults intend. That did nothing before,
> because loading a profile ignored resolution. **It does something now.**
>
> If you want Jupiter back at 640×480 for the frame rate: load it, press
> `640`, press `SAVE`.

### The EDIT modal

Eleven fields: Name, Gain, Exposure ms, Gamma, WB Red, WB Blue, USB BW%,
Focus Position, Limit Frames, File Prefix, Notes. Values are clamped to the
hardware rails on save, then `Save()` + `LoadAll()` + combo refresh.

Two safety mechanics in that dialog
([`src/main.cpp:1614`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/main.cpp#L1614)):
`SetWindowSubclass` + `IsDialogMessageW` rather than a hand-rolled message
loop, which never received `WM_COMMAND` and froze; and **both the main
window and the panel** are `EnableWindow(FALSE)` for the modal duration,
because otherwise the panel forwards arrow keys and **jogs the mount while
the dialog is open**.

### Focus steps

`+` / `-` move by the profile's **fine** step, `[` / `]` by its **coarse**
step — 10 and 100 by default. Phase 85 was the first time these reached the
keys at all.

### Hardware rails

Every value is clamped to these on load and save:

| Control | Range |
|---------|-------|
| Gain | 0 – 570 |
| Exposure | 0.032 ms – 2,000,000 ms |
| Gamma | 1 – 100 |
| WB Red / WB Blue | 0 – 100 |
| USB bandwidth | 40 – 100 |
| EAF position | 0 – 600,000 |

`GAIN_HCG_THRESHOLD = 182` also lives here, so the
[auto-exposure HCG floor](Advisors#auto-exposure-assistant) and the profile
defaults cannot disagree.

---

## Environment variables

| Variable | Effect |
|----------|--------|
| `RADUK_WS_LAN=1` | Expose the [telemetry server](Telemetry-and-Dashboard) on the LAN instead of loopback. The command set can slew the mount; every connection still needs `WsToken`. Trusted networks only, never port-forwarded |
| `RADUK_EMU_*` | Emulator simulation switches — see [Emulator and Testing](Emulator-and-Testing) |

---

## Paths

| Path | Contents |
|------|----------|
| `rig.ini` (beside the exe) | Rig configuration. **Untracked** |
| `profiles/*.ini` | Target profiles |
| `D:\RadukCapture\captures` | SER files, `_quality.txt`, PNG snapshots, `session_log.txt` |
| `D:\RadukCapture\logs` | `session_<timestamp>.log` |
| `raduk_log.txt` | Crash-safe application log |
| `data/pointing_model.csv` | [Pointing model](Sky-GoTo-and-Plate-Solving#pointing-model) history |
| `data/object_knowledge.db` | SQLite [knowledge base](AI-Assistant#object-knowledge-base) |
| `RadukCapture-Help.md` (beside the exe) | AI assistant reference — copied on every build |
| `sqlite3.dll` (beside the exe) | Optional; knowledge base |

The three `D:\RadukCapture\` directories are created by a `POST_BUILD` step.
The capture output directory is settable in `CaptureConfig`.

---

**Next:** [Emulator and Testing](Emulator-and-Testing) · [Operations Guide](Operations-Guide) · [Advisors](Advisors)
