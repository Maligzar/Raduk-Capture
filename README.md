# RadukCapture: Planetary Capture Software for the ZWO ASI678MC

**RadukCapture is a native Windows planetary astrophotography capture
application, written in C++20 on pure Win32 and Direct2D, built for the ZWO
ASI678MC camera (Sony IMX678 sensor), the ZWO EAF electronic focuser, and the
Celestron NexStar 8SE telescope.** It records lucky-imaging video of Jupiter,
Saturn, Mars, the Moon and the Sun as SER or FITS, controls the focuser and
the mount, plate-solves, tracks the planet on the sensor, and follows
satellites, rocket bodies and near-Earth asteroids across the sky.

It is a custom, open-source alternative in the space occupied by FireCapture
and SharpCap, aimed at one goal: the most surface detail a planetary rig can
deliver. No frameworks, no .NET, no Qt. The whole application runs with no
hardware attached, through device emulators that follow the real ZWO SDK and
NexStar semantics.

Version 3.1, source release (the title bar shows the exact build; the number moves with every merge, see `src/Version.h`). Developed through phase 133, and first used
under the sky on 2026-11-07.

## Who this is for

- Planetary imagers using a **ZWO ASI678MC**, or another ZWO ASI USB 3 camera,
  who want capture software they can read and change.
- Owners of a **Celestron NexStar / StarSense** alt-az mount, driven directly
  over serial or through **ASCOM** (CPWI preferred).
- Anyone controlling a **ZWO EAF** focuser who wants a V-curve autofocus that
  reports honestly when it made focus worse.
- Developers looking for a working C++ example of the **ZWO ASI Camera SDK**
  (`ASICamera2`) and **EAF SDK**, SER v3 writing, D3D11 compute-shader
  debayering, SGP4 satellite prediction, or JPL Horizons integration.

## Features

### Capture and recording

- **SER v3 recording** with per-frame trailer timestamps at the exposure
  midpoint (WinJUPOS-ready), a RAM-first write buffer, and a lucky-imaging
  quality score for every frame
- **FITS output**: one cube per run with a `FRAMES` table of mid-exposure MJD
  and sharpness
- **GPU debayering** of RAW8 and RAW16 through a Direct3D 11 compute shader,
  drawn by Direct2D with no CPU readback
- **Capture format on the toolbar**: resolution presets from the full
  3840x2160 sensor down to 640x480, 8 or 16 bit, through one reconfigure path
  so an ROI crop is always recoverable
- **Target profiles** that apply gain, exposure, resolution, bit depth, focus
  position and capture limit when a target is chosen
- **Autorun sequences** that repeat check, focus, record and gap, hold for the
  seeing, stop at the derotation limit, and rank their runs
- **Derotation limit** computed from tonight's apparent diameter and the
  aperture (Jupiter at 44 arcseconds comes to 176 s)
- **SER player** ([PLAY]): step through any SER recording with a per-frame
  quality trace on the slider, jump to the best frame, zoom and pan; drop a
  `.ser` on the window to open it
- **FITS to SER**: [TO SER] converts one or many FITS cubes for PIPP and
  AutoStakkert, and the `fits2ser` command rescues a cube left unclosed by a
  crash by recovering every whole frame on disk
- **Fade warning** when the target dims steadily during autofocus or a
  recording, naming dew when the weather feed's dew risk is up

### Tracking and pointing

- **Closed-loop planet tracking**: three-stage acquisition with PHD2-style
  measured guide calibration (camera rotation, plate scale and guide rate are
  measured, never assumed). The target is found by light gathered over an
  area with a local background, so a hot pixel at high gain cannot capture
  the lock
- **ROI following and surface lock**: the camera's crop window follows the
  planet on the capture thread, and on a Moon or Sun that fills the frame the
  lock holds a patch of the surface instead of a centroid
- **GoTo, sky map and plate solving**: Messier and planet catalogue, Meeus
  ephemeris, a Direct2D sky map, ASTAP plate solving with mount sync, a plate
  scale check on every solve, push-to guidance, and a measured pointing map
- **Lunar feature targeting**: 99 named features, with libration and
  topocentric parallax
- **Lunar and solar mosaics** planned and executed, re-aiming every panel at
  the target's live position so the mosaic closes
- **Mount safety**: runaway watchdog, cord-wrap veto, keep-out sector and an
  unconditional stop

### Fast targets

- **Satellite passes** predicted from CelesTrak elements with SGP4 (verified
  against Vallado's test output) and flown at up to 3 deg/s, with an image
  lock that corrects the prediction once the satellite is on the sensor
- **Satellite night plan**: queue passes, pick the best set, fly them in turn
- **Satellites and rocket bodies on the sky map**, each clickable to FOLLOW
- **Near-Earth objects** from JPL's close-approach list, positioned through
  JPL Horizons and followed from the map
- **Any JPL Horizons target**, and **image-only tracking** that follows
  whatever is in the frame with no prediction at all

### Focus, exposure and quality

- **ZWO EAF V-curve autofocus** with per-position averaging, a parabola fit it
  will reject, and an honest report when focus got worse
- **Auto-exposure**, a light ladder, typed values on every camera control,
  and capture quality gating
- **Seeing score, sharpness graph, histograms and clipping**
- **Quick-look stacker**: an aligned, quality-gated preview stack while you
  record, plus a one-button handoff to AutoStakkert
- **Atmospheric dispersion (ADC) advisor** using the Edlen 1966 model over the
  band of the filter actually fitted, plus an ADC alignment window

### Operating at night

- **Instrument-style interface**: a single palette defined as tokens and
  shared with the browser pages, custom-drawn buttons, lists and tabs, and
  restrained motion that can be switched off
- **Night vision across the whole interface** that emits red and nothing
  else, with a designed luminance ladder so warnings stay brighter than
  labels once colour is gone
- **Command palette** (Ctrl+K): type part of any command; anything that
  moves the telescope or focuser asks for Enter twice
- **Audio cues** for record start, stop and fault (off by default)
- **Display aids**: stretch with black and white points, single-channel view,
  RGB alignment, movable and rotatable reticles (crosshair, circle,
  collimation rings, framing box), and a five-second framing boost
- **Five-monitor workspace** with a one-monitor fallback, an audience share
  view, and an object panel backed by a knowledge database
- **Readiness check** that stays silent when the rig is fine
- **Local AI assistant** (Ollama) given the help file and a live session
  snapshot: mount position, camera settings, capture format, display modes
  altering the image, seeing, focus and tonight's imaging windows
- **WebSocket telemetry and a browser dashboard** for remote monitoring,
  loopback-only by default, token-protected on a LAN
- **Session statistics** across every night's logs, capture records and
  pointing results

### Testing without a telescope

- **Full device emulators** for the camera, mount and focuser: closed tracking
  loop, fault injection, defocus and sensor noise, hot pixels, a real star
  field, per-planet rendering at tonight's apparent size, a textured Moon, and
  a mount mode that reproduces the NexStar 8SE as measured on the real rig
- **Scenario runner**: `--scenario` drives the emulator build end to end and
  reports pass or fail, so most indoor checks are automated
- **Bench tests** gate every merge (`tests\run-tests.bat`), and a screenshot
  baseline catches any window that changes when a stage did not mean it to
- **Crash diagnostics** that name the exception and the stack at the throw

## Supported hardware

| Item | Status |
|---|---|
| ZWO ASI678MC (Sony IMX678, 3840x2160, 2.0 um, RGGB, USB 3) | Primary camera. Other ZWO ASI cameras use the same SDK path but are untested |
| ZWO EAF | Supported through the ZWO EAF SDK |
| Celestron NexStar 8SE, StarSense hand control | Direct serial protocol, or ASCOM (CPWI preferred) |
| Other ASCOM telescopes | Through the ASCOM path; tested against the ASCOM telescope simulator |
| ASTAP | Plate solving and mount sync |
| Ollama | Optional local assistant |

The design calculates a plate scale of 0.203 arcseconds per pixel for this
rig (2032 mm at f/10 with 2.0 um pixels).

## Status

RadukCapture is in active development and in use on one rig. Capture,
tracking, GoTo, autofocus, the ADC advisor, the quick-look stacker, the
readiness check and the emulators are built and in use.

**First field night, 2026-11-07, on Saturn.** Proven under the sky: rate
guiding (PLANET LOCK held Saturn to 1 to 2 arcseconds) and the area-flux
target finder. Proven at the rig: the serialized NexStar command sender and
tracking kept across stops. The night also found real defects, fixed since:
a centroid that locked onto a hot pixel, jogging that could lock the hand
control, and a crash on closing a FITS recording. Still open: the
mount's pointing error of about 8 degrees, and why Saturn faded 40% in 90
seconds during a focus sweep (dew or haze).

Much of the rest (FITS, ROI following, surface lock, mosaics, Autorun, lunar
features, satellites, NEOs, Horizons targets and image-only tracking) is
built and gated by bench tests and emulator scenarios, but **has not yet
been proven under the sky.** Every phase's evidence, and what it still owes,
is in [docs/PHASE-STATUS.md](docs/PHASE-STATUS.md), the only file that
asserts status. The order of what remains is in
[docs/ROADMAP-Next.md](docs/ROADMAP-Next.md) and section 13 of
[RadukCapture-Architecture.md](RadukCapture-Architecture.md).

In progress: phase 132, the instrument UI, has stages 132.0 to 132.9 written
and 132.10 planned. Phase 133's remaining stages, the field proof of the
night's fixes, need a clear night. Not built: AVI output, multi-star FWHM, a
Bahtinov focus aid, deep-sky annotation and stellar photometry. Polar
alignment is backlogged because the mount is alt-az with no wedge.
Calibration frames are a preview aid only; recordings stay raw. Full
stacking is deliberately out of scope, since AutoStakkert does it better.
There is no installer or binary download yet.

## Getting started from a fresh clone

### Prerequisites

- Windows 10 or 11, x64
- Visual Studio 2022 or later with the **Desktop development with C++**
  workload (MSVC, C++20, and the Windows SDK)
- CMake 3.21 or later (the one bundled with Visual Studio works)

The ZWO camera and EAF SDKs and SQLite are already in `external/`. Nothing
else needs downloading to build.

### Try it with no hardware

The emulator build simulates the camera, focuser and mount, so the whole
application runs on any Windows PC.

```bat
git clone https://github.com/Maligzar/RadukCapture.git
cd RadukCapture
build_emulator.bat
launch_emulator.bat
```

Or by hand:

```bat
cmake -S . -B build_emu -DRADUK_EMULATOR=ON
cmake --build build_emu --config Release
build_emu\Release\RadukCapture.exe
```

On first launch, set your observing site (latitude, longitude, elevation)
so the ephemeris, sky map and satellite passes are right. Until then the
site is 0,0.

### Build for the real rig

```bat
cmake -S . -B build
cmake --build build --config Release
```

`build_all.bat` builds both configurations and fails loudly if either
breaks. Before connecting hardware, install ZWO's camera driver and the
USB-serial driver for your mount's hand control, and set `FilterName`,
`FilterLowNm` and `FilterHighNm` in `rig.ini` to the filter actually fitted.

### Where your files go

With `DataRoot` blank, captures, logs, profiles and the object knowledge
database live beside the executable (for example
`build_emu\Release\captures`). Set `DataRoot` in `rig.ini` to keep them on
another disk. Nothing in those folders is tracked by git.

### Optional companions

| Tool | Enables | Without it |
|---|---|---|
| [ASTAP](https://www.hnsky.org/astap.htm) with the D80 star database (the full-sensor field, about 0.12° high, is under even D80's rated 0.15°; smaller databases fare worse) | Plate solving and mount sync | Solves fail with a clear message |
| [Ollama](https://ollama.com) on `localhost:11434` | The local AI assistant | The chat panel reports no model |
| AutoStakkert! 4 | One-button handoff of a finished recording | The button reports it could not find it |

### Running the tests

```bat
tests\run-tests.bat
```

Bench tests need no hardware and exit non-zero on any failure. GitHub
Actions builds both configurations and runs them on every push and pull
request.

## Configuration

`rig.ini` sits next to the executable, is **not tracked by git**, and is the
only place your observing site is stored. It is created on first save and
defaults to latitude and longitude 0,0 until you set it (the app can also
auto-detect, and will refuse to overwrite a site you entered by hand).

```ini
[Rig]
; Where the app keeps captures, logs, profiles and data. Leave it
; blank and it resolves itself: an existing D:\RadukCapture is kept
; so the original rig needs no edit, otherwise it sits beside the
; executable. Set it to put the tree on the disk you want.
DataRoot=
; Pin an ASCOM telescope driver by ProgID and skip discovery.
; Empty discovers, preferring CPWI. Set it to
; ASCOM.Simulator.Telescope to exercise the ASCOM path with no
; mount attached, which discovery will not do on its own.
AscomProgId=
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

Full key reference, panel-by-panel documentation and a first-night
walkthrough are in [RadukCapture-Help.md](RadukCapture-Help.md), which the
app also loads as reference context for the AI assistant.

## Documentation

- **[Wiki](https://github.com/Maligzar/RadukCapture/wiki)**: architecture,
  threading, the capture pipeline, every subsystem, and why each is built the
  way it is. Source lives in [`wiki/`](wiki) and is published with
  `wiki\publish-wiki.bat` (or `wiki/publish-wiki.sh` under bash).
- [RadukCapture-Help.md](RadukCapture-Help.md): operator reference and
  first-night walkthrough.
- [RadukCapture-Architecture.md](RadukCapture-Architecture.md): how it is
  built, what has been done, and what is planned.
- [docs/](docs): phase plans, code and security reviews, tracking findings,
  and the clear-night checklist.

## Keywords

Planetary imaging software, lucky imaging capture, astrophotography capture
software for Windows, ZWO ASI678MC capture, Sony IMX678 planetary camera,
ZWO ASI SDK C++ example, ZWO EAF autofocus, Celestron NexStar 8SE control,
StarSense, ASCOM telescope control, SER video recorder, FITS cube, WinJUPOS,
AutoStakkert, ASTAP plate solving, satellite tracking telescope, ISS imaging,
SGP4, JPL Horizons, near-Earth asteroid tracking, lunar mosaic, atmospheric
dispersion corrector, Win32, Direct2D, Direct3D 11.

## License

Copyright 2026 Kenneth C Wood Jr. Licensed under PolyForm Noncommercial 1.0.0;
see [LICENSE](LICENSE).

The ZWO SDKs under `external/` remain the property of Suzhou ZWO Co., Ltd.
under their own terms. Barlow Condensed and Space Mono are bundled under the
SIL Open Font License 1.1; their notices are in [assets/fonts](assets/fonts).
