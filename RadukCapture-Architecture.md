# RadukCapture: Architecture and Roadmap

Planetary capture software for the ZWO ASI678MC, ZWO EAF and Celestron 8SE.
Version 3.0.0 (`src/Version.h`). Rewritten 2026-11-01, through phase 115.

This file describes how the application is built, what exists, and what is
left to do. It was rewritten because its old build plan had stopped at about
phase 50 and still described ASCOM as retired, RTSP streaming as planned, and
a panel, toolbar and recording path that no longer exist. The old text is in
git history.

---

## 1. OVERVIEW

RadukCapture is one C++20 Win32 process that owns the camera, focuser, mount,
plate solving, recording, quality measurement and an operator workspace across
five monitors. Pixels, pointing and quality scores live in one process, so a
decision such as "hold this run until the seeing recovers" is made from one
live picture of the system.

### Direction

Set by the owner on 2026-11-01. It outranks the competitive scoring wherever
the two disagree.

1. Make this the best planetary capture application ever made, surface detail
   above all.
2. Then make it the best astrophotography application for a CMOS camera, on a
   telescope or on its own.

Adoption is not a goal, and a release may be months or years away. The primary
targets will always be bright objects visible from this site: planets,
satellites, rockets and the like. Faint deep-sky work ranks below all of them.
PolyForm Noncommercial stays.

### The rig

| Item | Detail |
|---|---|
| Camera | ZWO ASI678MC: Sony IMX678, 3840x2160, 2.0 um pixels, RGGB, USB 3 |
| Focuser | ZWO EAF |
| Telescope and mount | Celestron NexStar 8SE, 2032 mm f/10, single-arm alt-az, StarSense hand control |
| Plate scale | 0.203"/px. **Calculated, never checked against the sky** (clear-night item 3). Every arcsecond figure in the app derives from it |
| Capture PC | StarMachine: RTX 2060 and Quadro P1000 on one unified NVIDIA driver, five monitors, with a one-monitor fallback |
| Other machine | Elminster: reaches the browser dashboard through Leoben over the home network, and runs Leoben, which supplies weather |

AutoStakkert runs on the capture PC. Raw recordings stay there.

---

## 2. WHERE EACH KIND OF TRUTH LIVES

| Question | Document | Rule |
|---|---|---|
| How is it built? What is left, in outline? | This file | Sections 12 and 13 sort work into built and not built. They do not grade evidence |
| What state is a phase in, and on what evidence? | [`docs/PHASE-STATUS.md`](docs/PHASE-STATUS.md) | **The only place a status is asserted.** If another document disagrees, the other document is the defect |
| What should be done next? | [`docs/ROADMAP-Next.md`](docs/ROADMAP-Next.md) | Ranks, and asserts no status |
| What needs a clear night? | [`docs/CLEAR-NIGHT-CHECKLIST.md`](docs/CLEAR-NIGHT-CHECKLIST.md) | Referred to by item number |
| What shipped in which release? | Phase History in [`RadukCapture-Help.md`](RadukCapture-Help.md) | Says what shipped, not what is proven |
| How does a subsystem work in depth? | [`wiki/`](wiki) | Deep reference |
| How was a phase designed? | `docs/Phase*-Plan-*.md` | Design record for that phase |

`docs/NEXT-SESSION.md` is stale and kept for its testing notes. Run
`tests\manual-doc-truth.ps1` after a phase lands.

---

## 3. ARCHITECTURE

### 3.1 Threads

The capture thread never waits on a mutex, a disk or a serial port.

```
                         CAMERA (USB 3)
                              |
                     +--------v---------+
                     |  CAPTURE THREAD  |  time-critical
                     |  ROI follower    |
                     +--+------------+--+
         every frame    |            |    preview, may skip
            +-----------v--+      +--v-----------+
            | WriteBuffer  |      | RingBuffer   |
            | never skips  |      | 64 slots     |
            +-----+--------+      +--+--------+--+
                  |                  |        |
            +-----v--------+  +------v---+ +--v-----------+
            | WRITER       |  | MAIN/GUI | | ANALYZER     |
            | above normal |  | render,  | | below normal |
            | SER or FITS, |  | runners, | | sharpness,   |
            | sidecars     |  | panels   | | centroid,    |
            +--------------+  +----+-----+ | histogram    |
                                   |       +------+-------+
                                   |              |
            +----------------------v--------------v------+
            |      TRACKER (CentroidTracker), above normal|
            |      solve pixels to pulses                 |
            +----------------------+----------------------+
                                   |
                         +---------v---------+
                         | MountProxy        |  stable pointer
                         | ASCOM, NexStar,   |  backend swaps
                         | or emulator       |  underneath
                         +-------------------+
```

| Thread | Owner | Job |
|---|---|---|
| Main / GUI | `Window::Run()` | Message pump, Direct2D render, panels, and ticking the mosaic and Autorun runners |
| Capture | `CaptureThread` | Pull frames, stamp them, run the frame sink, commit to the ring. The sink is the recording path, so recording does not depend on render rate |
| Writer | `CaptureManager::WriterLoop()` | Drain `WriteBuffer` through a `CaptureWriter` |
| Analyzer | `FrameAnalyzer` | Skips frames while busy, so the answer is always about the latest one |
| Tracker | `CentroidTracker` | Acquisition, calibration and correction |
| Mount poll and watchdog | `NexStarMount` | Position reads and the runaway watchdog |
| WebSocket | `WebSocketServer`, port 8765 | Telemetry and remote commands |
| Preview stream | `PreviewStreamer` | JPEG preview for subscribed dashboards |
| Leoben | `LeobenClient` | Weather fetch |
| On demand | `AutofocusRoutine`, `ASTAPSolver`, `AutoStakkertRunner`, `AIAssistant`, status drive scanner, GoTo survey | Long jobs off the GUI thread |
| Emulator only | `CameraEmulator`, `EAFEmulator`, `MountEmulator` | Frame generation and simulated motion |

### 3.2 Ring buffer

`src/core/RingBuffer.h` carries frames to the preview and analyzer. It is
lock-free, single producer and single consumer, with 64 slots.

Slot states run FREE, WRITING, READY, READING, FREE. READING exists so the GUI
can hold a slot during a render without the capture thread overwriting it. The
producer skips a READING slot, and takes a READY slot by compare-and-swap so
that it cannot tear a frame a reader has just claimed.

The ring may lose frames, because the preview is allowed to. Recording does
not go through it.

### 3.3 Memory and the write path

- Full sensor: 8.3 MB a frame in RAW8, 16.6 MB in RAW16. A 640x480 crop in
  RAW8 is 0.3 MB.
- Frame memory comes from `VirtualAlloc`, page-aligned.
- `WriteBuffer` is a second lock-free ring that reads in order and reports an
  overrun rather than dropping silently. It is **sized to the run**, not to a
  fixed slot count, and a failed allocation aborts the start with a visible
  error.
- `CaptureWriter` is the seam between `SERWriter` and `FITSWriter`. It owns
  what the formats share: pointing notes, wall-clock conversion and the
  quality sidecar.
- SER stamps are exposure midpoints, from each frame's capture time.
- Each Light writes `_quality.txt` and `_observation.txt` beside it. With
  `WinJuposNames` on, a finished Light and its sidecars are renamed
  `yyyy-mm-dd-hhmm_t-<old name>`.
- Measured figures, with venue and date, are in `docs/PHASE-STATUS.md`.

### 3.4 Device layer

| Interface | Real | Emulated | Selection |
|---|---|---|---|
| `ICamera` | `ASICamera` | `CameraEmulator` | Compile time, `RADUK_EMULATOR` |
| `IFocuser` | `EAFFocuser` | `EAFEmulator` | Compile time |
| `IMount` | `ASCOMMount`, `NexStarMount` | `MountEmulator` | Run time, behind `MountProxy` |

`ICamera` is brand-neutral, and the capture hot path names no vendor type.
`FrameSlot::imgType` and `TargetProfile::imgType` keep the ZWO enum, because
the second persists to saved profiles.

The mount path tries ASCOM first, when CPWI is running, then NexStar serial
direct. Simulators are refused unless `AscomProgId` in `rig.ini` names one.
ASCOM is live, not superseded.

`src/core/HardwareLink.h` decides what the `[HW]` button does, and
`src/core/MountProxy.h` lets a reconnect pick a different transport or port.

### 3.5 Pointing and tracking

- **Acquisition:** Off, Searching, Centering, Locked.
- **Calibration:** PHD2-style. Pulse east, measure, pulse north, measure. The
  resulting 2x2 matrix is the pixel-to-sky transform, including camera
  rotation and the mirror flip.
- **`GuideSolve.h`** is the one solve, shared by the guide loop and
  click-to-recentre.
- **`RoiFollower.h`** moves the sensor window on the capture thread, at low
  latency. The mount offloads what is left, and `CentroidTracker` receives the
  centroid plus the window offset, so it needs no special case.
- **`SurfaceLock.h`** replaces the centroid when the target fills the frame,
  by correlating a surface patch. Its answer means what a centroid means, so
  nothing downstream changed.
- **Mount rates:** `IMount::TrackingRate` is sidereal, lunar or solar, is
  remembered across GoTo, and is set when a profile is applied.
  `SlewVariable` sends a true variable-rate slew.
- **Safety:** horizon floor, keep-out sector, cumulative cord-wrap tracking,
  and `Esc` as an unconditional stop.

### 3.6 Sky

| File | Job |
|---|---|
| `SkyObject.h`, `LunarSolarSize.h` | Catalogue, ephemeris, apparent sizes |
| `ASTAPSolver.h`, `SolveResult.h` | Plate solving |
| `PlateScaleCheck.h` | Measured against configured scale, on all four solve paths |
| `PixelWCS.h` | Coordinates under the cursor and the direction rose. A solve expires, because an alt-az mount rotates the field |
| `PushTo.h` | Push-to guidance |
| `PointingModel.h` | Pointing record: reports, does not correct |
| `MosaicPlan.h`, `MosaicRunner.h` | Lunar and solar mosaics |

### 3.7 Measurement and automation

- **`FrameAnalyzer`, `Sharpness.h`:** histogram, one shared Bayer sharpness
  metric, centroid, and star metrics in star mode.
- **Two seeing figures, on purpose.** The gate's score is relative to its own
  recent best, so a poor night still reaches 10. `SeeingMonitor.h` divides
  sharpness by the square of the highlight, which takes exposure and gain back
  out, so it can be compared across a sequence. Its signal-squared rule is
  proven on emulator frames only.
- **`LightLadder.h`:** one path for auto-exposure along exposure and gain.
  `CameraRails` limits are advisory to a person and hard for the controller.
- **`AutofocusRoutine.h`:** V-curve sweep, with AE settled and held.
- **`DerotationLimit.h`:** run length from the Rayleigh limit and tonight's
  apparent size.
- **`AutorunRunner.h`, `MosaicRunner.h`:** pure state machines. They return an
  action for the caller to perform and touch no hardware, because their
  failures are timing failures that have to be tested with a fake clock.

### 3.8 Interface

- **Rendering:** Direct2D and DirectWrite, with a D3D11 compute shader for
  debayering and a CPU fallback. Direct2D is bound to the preview's child
  window, so the OS clips it. The preview only produces pixels when the frame
  has changed.
- **Main window:** two-row owner-drawn toolbar (`ToolbarLayout.h`), preview,
  and a scrollable control panel of collapsible sections, `PANEL_WIDTH` 560.
- **Workspace:** `WorkspaceLayout.h` places every window by role, keyed by
  monitor count and matched on device interface path, because Windows display
  names are not stable. Layouts save position and open or closed state.
- **Floating windows:** status, histogram, sharpness, temperature, mount
  controller, GoTo, two sky maps, help, AI chat, ADC alignment, object panel
  and share view.
- **Theme:** tokens in `Theme.h`. Each declares its own night luminance,
  because deriving it from the day colour inverted the hierarchy. Layout that
  can be tested is pure arithmetic in `AudienceLayout.h` and `ToolbarLayout.h`.

### 3.9 Network and assistant

- **`WebSocketServer`:** a hand-written RFC 6455 server on port 8765, loopback
  by default. `WsLan` opens it to the network. Every connection needs
  `WsToken`, and `WsLanOpen` exempts private addresses only. The parser,
  `WsProtocol.h`, is fuzzed.
- **Dashboard:** `RadukCaptureDashboard.html`, opened by `[DASH]`, with the
  token in the URL fragment so it never reaches a server.
- **Leoben:** weather and sky into the ADC, status window, observation record
  and assistant.
- **Assistant:** Ollama on `localhost:11434`, default model `llama3.2:3b`,
  using the help file and project notes as reference, with a live session
  snapshot. The site never reaches the model at more than one decimal.

### 3.10 Testing

- `tests\run-tests.ps1` is the merge gate. It builds pure headers against real
  code, with no hardware, and needs MSVC.
- `build_all.bat` builds both configurations. The hardware build has silently
  broken before, so both must compile.
- `tests\manual-*` are measurements and audits that do not gate.
- The emulators close the tracking loop and model defocus, noise, seeing, real
  apparent sizes, point sources and a lunar surface. They also inject faults.
  **They have no solvable star field, so no solve path can run indoors.**
- Status words and venues are defined in `docs/PHASE-STATUS.md`. A green gate
  says nothing about the sky.

---

## 4. KEY DESIGN DECISIONS

| Decision | Why |
|---|---|
| Lock-free buffers on the frame path | The capture thread must never block |
| Recording on the capture thread | From the render loop, recording topped out near 62 fps and silently lost the rest |
| RAM-first write buffer, sized to the run | Separates disk stalls from acquisition |
| Stable mount pointer through `MountProxy` | Holders keep it for the session while the backend swaps |
| Retired mount backends are kept, not freed | The tracker may be inside a 500 ms pulse on one. A mutex would block the render loop's heartbeat behind that pulse |
| A partial link connects what is missing | Two of three means a device was not ready, not that the working two should go |
| One pixel-to-pulse solve | Two copies would be two sign conventions, and a wrong sign steers the mount confidently the wrong way |
| Recentre from the guide calibration, not a solve | A planet field has no stars to solve |
| Recentre refuses rather than clamps | A shortened pulse lands somewhere nobody asked for |
| `Ctrl`+click, not a recentre mode | A mode that moves the telescope is one somebody forgets they are in |
| Three-key chord for long unattended sequences | Starting one by accident is a slew with nobody's hand on it |
| Runners return actions and touch no hardware | Timing failures can then be tested with a fake clock and a fake mount |
| Pure headers for arithmetic | Anything a test can reach gets gated; `main.cpp` wiring usually cannot be |
| Live position on every mosaic panel | Fixed offsets drift past the overlap. Taking the target's position each tick removes the cumulative error |
| AutoStakkert handoff, not an in-app stacker | It does the job better, and the quick-look stacker answers "is this sharp" at the telescope |
| `AutoStackOnStop` off by default | Stacking is minutes of CPU on the capture machine |
| SER as default, FITS as an option | AutoStakkert's headless run does not process a FITS cube |
| Calibration frames affect the preview only | The recording stays raw for the stacker |
| Night luminance declared per token | Night vision flattens hue, so hierarchy has to survive as luminance, shape and size |
| Advisory for people, hard for controllers | A person may have a reason. A controller has no judgement to respect |
| Profiles do not own every setting | Startup restores the last camera state, and loading a profile applies that profile |
| One status file | A phase once held four statuses across six files |

---

## 5. FILE STRUCTURE

The full annotated list is [`wiki/Code-Map.md`](wiki/Code-Map.md). This is the
shape.

```
src\
├── core\       Devices, capture, tracking, analysis, runners, config
│   ├── ICamera, IFocuser, IMount      The device seams
│   ├── ASICamera, EAFFocuser          ZWO SDK backends
│   ├── NexStarMount, ASCOMMount       Mount backends
│   ├── MountProxy, HardwareLink       Reconnect and the [HW] decision
│   ├── *Emulator, DeviceFactory       Emulators and selection
│   ├── RingBuffer, WriteBuffer        Frame path
│   ├── CaptureThread, CaptureManager  Acquisition and recording
│   ├── CaptureWriter, SERWriter       Output formats
│   ├── FITSWriter, FITSFormat
│   ├── ObservationRecord              The record beside every capture
│   ├── CentroidTracker, GuideSolve    Tracking
│   ├── RoiFollower, SurfaceLock       Following within the sensor
│   ├── FrameAnalyzer, Sharpness       Measurement
│   ├── StarMetrics, SeeingMonitor
│   ├── AutoExposureAssistant          Exposure control
│   ├── LightLadder, ExposureScale
│   ├── AutofocusRoutine, DerotationLimit
│   ├── AutorunRunner                  A night's sequence
│   ├── RigConfig, Paths, Readiness    Configuration and startup checks
│   ├── AIAssistant, AIContextBuilder, AdvisoryLoop
│   └── AutoStakkertRunner, QuickLookStacker
├── gui\        Win32 and Direct2D windows, layout, theme
├── gpu\        D3D11 debayer
├── sky\        Catalogue, ephemeris, solving, WCS, mosaics
├── net\        WebSocket, telemetry, preview stream, Leoben
├── profiles\   Target profiles
└── main.cpp    Startup and all callback wiring
```

`src/analysis`, `src/bridge` and `src/writers` are empty directories.

`main.cpp` is the largest risk in the tree: most `owes:` clauses in the status
file come down to wiring there, which no test reaches.

Data outside `src`: `data/object_knowledge.db`, `data/pointing_model.csv`,
`profiles/*.ini`, `rig.ini` beside the executable, `captures/`, `logs/`, and
the dashboard HTML at the repository root.

**`rig.ini` holds the real site and `WsToken`, is never committed, and must
never be packaged.**

---

## 6. RUNTIME BEHAVIOUR

### 6.1 Startup

Hardware detection, then the readiness check, which is silent when the rig is
fine. The readiness dialog is modal, so it blocks everything after it.
Workspace state is restored twice, the second time just before the render
loop, so that a later force-show cannot win.

### 6.2 Stop paths

`Esc` stops the mount unconditionally. Every recording stop goes through one
`endRecording`, so USB polling returns to normal on all of them. Stops in
`CaptureManager` are checked when a frame arrives, so a dead camera cannot end
a run. Autorun has its own limit for that reason.

### 6.3 Fault tolerance and auto-recovery

The capture thread distinguishes a timeout from a camera fault. Repeated
timeouts count as a pipeline wedge and call `RestartCapture()`: stop, a 100 ms
USB settle, start. A fault such as a USB drop returns from the SDK at once, so
the thread sets `cameraFault` and retries a restart once a second rather than
spinning. Restarts are counted in `m_stats.restarts`.

---

## 7. PROFILE FORMAT

INI files in `profiles/`: `jupiter`, `saturn`, `mars`, `venus`, `moon`, `dso`,
`star` and `planet_focus`. A missing file is rewritten from defaults at launch.
`src/profiles/TargetProfile.h` holds the fields and rails.

```ini
[profile]
name=Jupiter
[camera]
gain=100
exposure_ms=9.0000
gamma=50
wb_red=52
wb_blue=47
usb_bandwidth=80
high_speed=true
[roi]
width=1920
height=1080
binning=1
img_type=2
[focuser]
position=168032
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

`rig.ini` keys are in the help file's **rig.ini reference**.

---

## 8. DEPENDENCIES

| Dependency | Use | Where |
|---|---|---|
| ZWO ASI and EAF SDKs | Camera and focuser (hardware build only) | `external/` |
| SQLite | Object knowledge database; DLL copied beside the exe | `external/sqlite3` |
| Direct2D, DirectWrite, Direct3D 11, WIC | Rendering, debayer, PNG | Windows SDK |
| WinHTTP | Ollama and Leoben | Windows SDK |
| Winsock | WebSocket server | Windows SDK |
| ASCOM Platform | Optional mount path | Installed separately |
| ASTAP | Plate solving, external process | Installed separately |
| AutoStakkert | Stacking, external process | Installed separately |
| Ollama | Local assistant | Installed separately |

There are no vcpkg dependencies. JSON and WebSocket framing are written here.

---

## 9. BUILD

```bat
:: Emulator: no hardware or SDKs needed
cmake -S . -B build_emu -DRADUK_EMULATOR=ON
cmake --build build_emu --config Release

:: Hardware
cmake -S . -B build
cmake --build build --config Release

:: Both, and the gate
tests\run-tests.bat && build_all.bat
```

The CMake floor is 3.21. Below 4.2, Kitware's builds need `-G Ninja`.
`src/Version.h` is the single source of the version number. Launch the emulator
from PowerShell or `cmd`; Git Bash rewrites `/switches` into paths, which also
breaks the AutoStakkert command line.

---

## 10. WHAT TO INSTALL ON THE CAPTURE PC

1. Visual Studio 2026 with the C++ desktop workload, and CMake 3.21 or newer
2. ZWO camera driver; the SDKs are in `external/`
3. Celestron StarSense USB-serial driver
4. ASTAP
5. AutoStakkert
6. Ollama, for the assistant
7. ASCOM Platform and CPWI, only if the ASCOM path is wanted
8. Space Mono and Barlow Condensed fonts

**Keep Windows Update away from the GPU drivers.** The RTX 2060 and Quadro
P1000 need one unified package.

---

## 11. NAMING CONVENTIONS

| Element | Convention | Example |
|---|---|---|
| Classes, methods, files | PascalCase | `CaptureManager`, `SubmitFrame()` |
| Variables | camelCase | `exposureMs` |
| Members | `m_` prefix | `m_ringBuffer` |
| Constants | UPPER_SNAKE or `k` prefix | `DEFAULT_RING_SIZE`, `kMinScore` |
| Namespaces | lowercase | `raduk::core` |
| Profiles | lowercase.ini | `jupiter.ini` |

---

## 12. WHAT HAS BEEN BUILT

Everything here exists in the tree. **"Built" does not mean proven.** Almost
none of it has seen the sky, and much of it has not run in the app. Each
number's evidence, and what it still owes, is its row in
[`docs/PHASE-STATUS.md`](docs/PHASE-STATUS.md).

| Area | What exists | Numbers |
|---|---|---|
| Camera and preview | Live preview, ROI, GPU debayer, format presets, one reconfigure path | 1-4, 10, 84, 85 |
| Recording | SER, RAM write buffer, capture-thread recording, pause, frame types, midpoint stamps | 6, 41, 44, P5.1 |
| FITS | Cube per run with a `FRAMES` table | 106 |
| Records | Quality sidecar, observation record, framing recall (`Shift+G`) | 11, 48 |
| Mount | NexStar direct, ASCOM, site and clock, alignment, variable slew, tracking rates | 62, 64, 93, P5.3 |
| Mount safety | Runaway watchdog, cord wrap, keep-out sector, unconditional stop | 57, 67, 71 |
| Hardware link | `[HW]`, `MountProxy` | 46 |
| Tracking | Three-stage acquisition, measured calibration, correction dynamics, one-button acquisition | 5, 51-53, 65, 69, 70 |
| Fine following | ROI following, surface lock, click to recentre | 47, 107, 109 |
| GoTo and sky map | Catalogue, ephemeris, sky map, second map, session planner, survey | 6, 13, 14, 63, 90 |
| Plate solving | ASTAP, solve and sync, scale check on every solve, pixel readout, push-to | 30, 47b, 54, 102, 110 |
| Focus | V-curve autofocus, single-star HFD and FWHM, focus-step keys | 9, 31/32 part, 73, 85 |
| Exposure | Auto-exposure, log slider, typed values, rails, light ladder, AE hold | 12, 98, 100, 112 |
| Quality | Sharpness, seeing score, gate, clipping, quality graph, sensor analysis, histograms | 21, 25a, 36, 76, 79 |
| Automation | Autorun, seeing hold, run ranking, derotation limit, WinJUPOS naming | 108, 113, 114 |
| Lunar and solar | Apparent sizes, drift analysis, mosaic plan and execution | 34, as 97-97.5 |
| Atmosphere | ADC advisor, filter band model, ADC alignment window, Leoben weather | 15, 35, 82, 83 |
| Stacking | Quick-look stacker, AutoStakkert handoff, `[STACK]` | 74, 77 |
| Display aids | Stretch, single channel, RGB align, ghost, reticles, framing shot, pixel readout, snapshot, boost FX | 22-25, 29, 37-39, 45 |
| Calibration | Master dark and flat, preview only | 27, 28 |
| Night use | Night vision everywhere, night luminance ladder, type and elevation tokens, motion tokens | 26, 96.1-96.5, D2 |
| Workspace | Five monitors, role placement, saved open/closed state, share view, toolbar rows | 42, 89, 101, 101b, 111, D1, D3 |
| Audience screens | Object panel with knowledge database, share view caption fitting | 92, 103, 104 |
| Guidance | Help, readiness check, align helper, notifications | 40, 49, 66-68, 72 |
| Assistant | Ollama chat, live context, retrieval, occasional advice | 12b, 50, 86, 87, P3.6 |
| Remote | WebSocket, dashboard, preview picture, LAN access, token, fuzzed parser | B3, M12, P5.6 |
| Emulators | Closed loop, fault injection, defocus, full sensor, stars, live sizes, Moon | 52, 78, 80, 81, 91, 94.1-94.3 |
| Structure and review | Rig config, memory safety, fault recovery, device interfaces, path root, dead-code sweeps | 55, 56, 58, 59, 99, 100, 102, 105, P5.2, P5.4 paths |

Numbers 60-84 were mostly reactive: reviews, defect fixes and things the rig
turned out to need. Rows 13, 14, 29 and 40 are in the code and have no row in
the status file.

---

## 13. WHAT IS STILL PLANNED

Order and reasoning are in [`docs/ROADMAP-Next.md`](docs/ROADMAP-Next.md). This
is the whole of what is left, sorted by what it needs.

### 13.1 Needs a clear night

The largest debt in the project, and it costs little code. In order:

1. **Clear-night item 3, plate scale.** Plate-solve one frame and read the
   line. Nothing else returns as much for as little.
2. **Clear-night item 1, `SHARP_CEIL`,** which biases the gate and autofocus.
3. **Clear-night item 14, click to recentre.**
4. **Clear-night item 15,** which settles the pixel readout's row order.
5. **Clear-night item 18, the first lunar mosaic.**
6. **Real-camera unknowns from recent work,** checklist 31 to 38: whether
   `ASISetStartPos` applies during capture and how many frames it takes,
   surface lock on a real Moon, whether the camera lists gamma, the light
   ladder's knees, whether `centroidValid` holds on a full-frame Moon, whether
   signal squared holds on real frames, and whether 80% is the right seeing
   bar.
7. **The rest of the checklist,** including thresholds tuned on synthetic
   frames, and a recall to a real planet on the real mount.

### 13.2 Can be done indoors now

Running what already exists. Most of this is emulator time, and it is the
cheapest way to shrink the status file's debt.

| Item | Needs |
|---|---|
| Autorun and seeing hold wiring in `main.cpp` | Checklist 36, 37. Neither has been run |
| Camera controls, panel and autofocus path | Checklist 35 |
| ROI following, derotation limit, surface lock | Checklist 31, 32, 33 |
| `[HW]` button | One press in the running app |
| Panel state, buttons, text fit, timer | Checklist 20 to 28 |
| FITS in Siril, `[STACK]`, `Shift+K` | Checklist 29, 30 |
| Toolbar in a dark room | Checklist 34 |
| One remote stop ending a recording | P5.6; also `apex_bridge.py` if it is still in use |
| ASCOM simulator run | P5.3, in [`docs/ASCOM-SIMULATOR-RUN.md`](docs/ASCOM-SIMULATOR-RUN.md) |
| A real fetch from Leoben on Elminster | Needs `expose_lan`, `LEOBEN_API_TOKEN`, `LeobenUrl` and `LeobenToken` |
| `Ctrl+Shift+G` framing restore | Needs a guide calibration |
| Visual system in a dark room | Clear-night item 16 and item 17 |

### 13.3 New capability, next in direction

| Item | Note |
|---|---|
| **Satellite pass prediction and high-rate mount control** | Named next for fast targets. ROI following is the prerequisite. Rockets and near-Earth objects are separate pieces of work after it. None is designed yet, and none has a phase number or plan |
| GoTo map learning, correcting half (6.6) | Auto solve and sync ships. The pointing map reports and does not correct, and the correcting half is the half that helps |

### 13.4 Smaller bench work

| Item | Note |
|---|---|
| 96.4b type and spacing retrofit | Needs eyes on a screen. Off-ramp font sites are marked in the source |
| B1 object panel | A derived line long enough to wrap still loses its second row |
| Emulator blur, further | Only if full-sensor bench work becomes routine |
| Knowledge base | Wikipedia summaries for objects lacking one. A rebuild recovers no mythology, because no loader exists for it |
| Mythology for the deep sky | A content-sourcing decision, not a coding task |
| Larger assistant model | An 8B at 4-bit fits the 2060's 6 GB, but driver 546.01 caps at CUDA 12.3, so it runs on CPU now (D4) |

### 13.5 Planned features, unstarted

| # | Feature | Effort | Note |
|---|---|---|---|
| 19 | Multi-session statistics | Medium | All sessions, pointing accuracy, hours by target, from existing logs |
| 18 | AVI output | Medium | Only if someone asks. FITS shipped as 106 |
| 31 | Multi-star FWHM | Hard | Needs the mag-14 catalog that 94.5 was about |
| 32 | Bahtinov focus aid | Hard | Needs a mask on the OTA, or its diffraction pattern rendered |
| 43 | Deep sky image annotation | Hard | Catalogue names over the preview after a solve. Below the bright targets |
| 33 | Stellar photometry | Very hard | Needs a catalog subset and calibration. Below the bright targets |

### 13.6 Decisions for the owner

- `ACCENT3` now outranks `ACCENT2`, so "good" reads brighter than "caution".
  A caution rung would re-space all six signals, because the ladder is full.
- Should dark subtraction feed the sharpness metric? It would help focusing
  and would move the gate and `SHARP_CEIL`, so it needs before and after
  numbers from a real night.
- `CentroidTracker`'s unreachable entry points stand for now: `StartCentering`,
  `ClearCalibration`, `SetDriftCallback`, `AutoAdvancing`, `DwellCount` and
  `GUIDE_RATE_AS_PER_S`.

### 13.7 Parked, backlogged, dropped

| Item | State | Comes back if |
|---|---|---|
| Installer and binary downloads (P5.4 remainder, 20) | Parked by the owner. v3.0 is a source release | A release is wanted: [`docs/Plan-Installer-And-Release-PARKED.md`](docs/Plan-Installer-And-Release-PARKED.md) |
| Second camera brand | Unranked: hardware breadth no longer drives the order | A second camera is wanted. The `ICamera` seam exists and no second backend has tested it |
| 94.4, 94.5 solvable emulator field | Backlogged | Solve paths need indoor exercise enough to justify a catalog to about mag 14 |
| P4.1 Xbox controller | Backlogged | A controller and adapter exist. There is no Bluetooth on this machine |
| 16 Polar alignment | Backlogged | A wedge, and probably a new mount |
| 17 Live stacking preview | Dropped | Superseded by the quick-look stacker |
| 7 OBS and RTSP streaming | Dropped | |
| P3.5 Gamepad-layout mount panel | Dropped | |
| Docking | Deleted | |
| Full stacking | Non-goal | AutoStakkert does it better |

---

*RadukCapture 3.0.0*
