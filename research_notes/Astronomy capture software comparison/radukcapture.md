# RadukCapture — technical profile from its own source tree

All figures below were measured against the working tree at
`/home/user/Raduk-Capture`, branch `claude/astronomy-apps-comparison-gf5dkd`,
on 2026-10-18. Where the project's own prose disagrees with the tree, the tree
is reported and the discrepancy is flagged. Sources are repository-relative
paths, with `:line` where useful.

## Code size and shape

### Takeaway
44,754 lines of C++20 across 100 source files — materially larger than the
project's own published figure. It is overwhelmingly header-only (83 `.h`
carrying 30,194 lines vs 17 `.cpp` carrying 14,560), and GUI code slightly
exceeds core code.

### Cited Findings
- Exact totals, counted over `src/`: **83 `.h` files / 30,194 lines**, **17 `.cpp` files / 14,560 lines**, **100 source files / 44,754 total lines** — `src/` (measured by `find`/`wc -l`)
- Lines by directory (`.h` + `.cpp`): `src/` root 5,325 (2 files) · `src/core` 15,479 (55 files) · `src/gui` 20,809 (33 files) · `src/net` 772 (4) · `src/sky` 1,344 (3) · `src/profiles` 643 (2) · `src/gpu` 382 (1)
- **GUI:core line ratio ≈ 1.34:1** (20,809 GUI vs 15,479 core); including `src/main.cpp` (5,309 lines) on the "app/UI" side pushes it to roughly 1.7:1
- Largest files: `src/main.cpp` 5,309 · `src/gui/ControlPanel.cpp` 2,489 · `src/gui/GoToPanel.h` 2,168 · `src/gui/PreviewRenderer.cpp` 1,833 · `src/gui/SkyMapWindow.h` 1,435 · `src/core/CameraEmulator.h` 1,316 · `src/gui/MountController.h` 1,088 · `src/core/NexStarMount.cpp` 855 · `src/gui/Window.cpp` 929 · `src/core/ASCOMMount.h` 792 · `src/core/AutofocusRoutine.h` 789
- C++ standard: `set(CMAKE_CXX_STANDARD 20)` + `CMAKE_CXX_STANDARD_REQUIRED ON` — `CMakeLists.txt:97-98`; the test runner also compiles with `/std:c++20` — `tests/run-tests.ps1:85`
- All 17 `.cpp` files are in the build target; nothing in `src/` is orphaned — `CMakeLists.txt:124-148`
- Additional non-`src` code: `RadukCaptureDashboard.html` is a single 5,453-line browser dashboard; `tools/build_knowledge_db.py` (40 KB) and `tools/repair_knowledge_db.py` (8.8 KB) are Python build-time utilities; `tests/` holds 3,084 lines of test and manual-harness code
- Documentation volume is large relative to code: `RadukCapture-Architecture.md` 103 KB, `RadukCapture-Help.md` 123 KB, `docs/ROADMAP-Next.md` 62 KB, `wiki/` 6,806 lines across 26 pages

### Inferences
- The header-only style (83 of 100 files) means almost the entire codebase is
  recompiled into one translation unit chain; this is a single-binary,
  single-maintainer architecture rather than a modular library.
- Code review documents, phase plans and wiki together likely exceed the
  source in word count, which is unusual and is one of the project's
  distinguishing characteristics.

### Gaps
- No tooling in the repo measures comment-vs-code ratio; the files are very
  heavily commented (design rationale in headers), so effective "logic" line
  count is materially lower than 44,754, but I did not compute a stripped count.

## Documentation drift versus the actual tree

### Takeaway
The headline size claim in the wiki understates the tree by roughly 30%, and
the wiki also asserts there is no test suite when 13 gated tests exist.

### Cited Findings
- Claim: "Pure Win32 + Direct2D, no frameworks, **~34,500 lines of C++20 across 86 source files**" — `wiki/Home.md:3-5`. Measured: **44,754 lines across 100 source files**. Discrepancy: **+10,254 lines (+29.7%)** and **+14 files**
- Claim: "**There is no automated test suite.** Worth stating plainly: this project has no unit tests, no integration tests, and no CI." — `wiki/Emulator-and-Testing.md` (section "There is no automated test suite"). Contradicted by `tests/run-tests.ps1:22-36`, which builds and runs **13 gated tests**, and `tests/README.md`, which documents them as a merge gate. (The "no CI" half is still true — there is no `.github/` directory.)
- Claim: `wiki/Emulator-and-Testing.md` says `src/core/CameraEmulator.h` is "984 lines". Measured: **1,316 lines**
- Claim: write buffer default "512 slots" — `wiki/Capture-Pipeline.md`; the code now sizes it dynamically with `FLOOR = 512`, `CEILING = 4096`, `RAM_SHARE = 0.5` — `src/core/WriteBuffer.h:83-94`, and the commit `8a8214e` is titled "Size the write buffer to the run and the machine, not to 512". `CaptureConfig::writeBufferSlots = 512` remains as the struct default — `src/core/CaptureManager.h:42`
- Phase table marks **Phase 22 "Snapshot PNG/FITS" as ✅ v1.0.7** — `RadukCapture-Architecture.md:1501`, but `RadukCapture-Help.md:2258-2259` states "No AVI, and no FITS — the plate solver reads FITS but nothing here writes it", and a grep of `src/` finds no FITS writer (only `src/sky/ASTAPSolver.h` reading solver output). Snapshots are PNG via WIC only
- Phase table marks **Phase 36 "Sensor Analysis / Smart Histogram" as 🔲 (not built)** — `RadukCapture-Architecture.md:1535` — while commit `665bb78` is "Phase 36: the numbers the averaged histogram could not show" and `tests/sensor_analysis.cpp` (11 KB) gates it
- `docs/ROADMAP-Next.md:840-860` opens with an admission that the roadmap itself went stale: "Items 1, 2 and 3 were all already shipped and the list had not been told"; item 3 was implemented twice on two branches because of it
- `src/core/AIContextBuilder.h:13` still describes `[OBJECT KNOWLEDGE]` as a "SQLite stub (empty until 50a)" although the knowledge DB was wired up in Phase 92a (`b97902d`)

### Inferences
- The documentation is unusually rich but not machine-generated, so it drifts.
  A reviewer should treat every prose number in this project as approximate and
  verify against the tree — the project itself makes this point in
  `docs/ROADMAP-Next.md:853-860`.

## Build system and toolchain, and the "no frameworks" claim

### Takeaway
"No frameworks" is literally true in the sense that no GUI/application
framework (Qt, wxWidgets, .NET, Electron) is used, but the binary links 16
Windows system libraries and depends on two vendored ZWO SDKs, a runtime-loaded
SQLite, and three external programs.

### Cited Findings
- `cmake_minimum_required(VERSION 4.2)` — `CMakeLists.txt:1`. This is an unusually
  high floor and will refuse to configure on most distributions' current CMake
- MSVC-specific: `/utf-8` forced, `CMAKE_RC_FLAGS` set, Win32 `.rc` resource generated — `CMakeLists.txt:155-162`; `add_executable(RadukCapture WIN32 ...)` — `CMakeLists.txt:124`. README states "Requires Visual Studio (MSVC, C++20) and CMake" — `README.md`
- Version management: `src/Version.h` is the single source of truth (`RADUK_VERSION_W L"1.2.3"` / `RADUK_VERSION_A "1.2.3"` — `src/Version.h:15-16`). CMake parses both macros with regex, **fails the configure if they disagree**, derives `PROJECT_VERSION` and generates the Win32 version resource from `src/RadukCapture.rc.in`; `CMAKE_CONFIGURE_DEPENDS` re-runs configure when the header changes — `CMakeLists.txt:21-95`. The comment records that the version previously drifted across four hand-maintained places (1.1.9 / 1.0.5 / 1.0.8.0 / documented 1.2.3)
- Emulator build option: `option(RADUK_EMULATOR "Build in emulator mode — no hardware or SDK DLLs required" OFF)` — `CMakeLists.txt:101`. In emulator mode `DEVICE_SOURCES` is empty (ASICamera.cpp, EAFFocuser.cpp, NexStarMount.cpp are **not compiled**), the ZWO import libraries are not linked, and `RADUK_EMULATOR` is defined — `CMakeLists.txt:115-121, 164-166`
- Vendored under `external/`: `external/ASI_SDK/include/ASICamera2.h` + `lib/x64/ASICamera2.{dll,lib}`; `external/EAF_SDK/include/EAF_focuser.h` + `lib/x64/EAF_focuser.{dll,lib}`; `external/sqlite3/lib/x64/sqlite3.dll` — `CMakeLists.txt:105-122, 216-224`
- **Every external dependency, complete list:**
  - Windows system libraries (hardware build, 16): `d2d1 dwrite d3d11 dxgi d3dcompiler windowscodecs shell32 ws2_32 winhttp windowsapp ole32 oleaut32 advapi32 uxtheme Shcore dwmapi` — `CMakeLists.txt:176-181`. Emulator build drops `ole32 oleaut32 advapi32 uxtheme` (12 libraries) — `CMakeLists.txt:170-173`
  - ZWO ASI Camera SDK (imported shared library, hardware build only)
  - ZWO EAF Focuser SDK (imported shared library, hardware build only)
  - SQLite3 — **loaded at runtime via `LoadLibraryA("sqlite3.dll")`, no compile-time dependency**, and fails silently to an empty knowledge base if absent — `src/core/ObjectKnowledgeDB.h:15-17, 62-69`. The DLL is copied by a CMake target in **both** builds — `CMakeLists.txt:200-224`
  - External processes: **ASTAP** (`astap.exe`, plate solving — `src/sky/ASTAPSolver.h:3-19`), **AutoStakkert** (`src/core/AutoStakkertRunner.h`, 382 lines), **Ollama** (local HTTP service at `localhost:11434` — `src/core/AIAssistant.h:3-12`)
  - The browser dashboard calls public internet APIs: `api.open-meteo.com`, `www.7timer.info`, `clearoutside.com`, `www.cleardarksky.com`, `nominatim.openstreetmap.org` — grepped from `RadukCaptureDashboard.html`. No JS framework or CDN script tag is loaded; the dashboard is one self-contained 5,453-line HTML file
- Build scripts: `build_all.bat` (both configurations), `build_emulator.bat`, `launch_emulator.bat`, `kill_radukcapture.bat`
- Post-build steps hardcode output directories to `D:/RadukCapture/{captures,logs,profiles}` — `CMakeLists.txt:227-233`

### Inferences
- The "no frameworks" claim is a statement about *application* frameworks and
  dependency-manager packages, and on that reading it holds: there is no
  vcpkg/Conan manifest, no Qt, no Boost, no JSON library (JSON is hand-emitted
  in `src/net/APEXTelemetry.h`), and no test framework.
- CMake 4.2 as a floor plus MSVC-only flags plus `WIN32` executable plus
  Direct2D/D3D11 means the project is not portable without a rewrite of the
  entire presentation layer.

## Architecture: layers, threads, buffers, frame lifecycle

### Takeaway
Six long-lived threads plus a transient one, two lock-free SPSC ring buffers in
series with deliberately opposite overflow policies, and a frame path in which
the capture thread never blocks and never knows recording exists.

### Cited Findings
- Layer map: `src/core` (55 files, devices + capture + analysis + advisors),
  `src/gui` (33 files, Win32 + Direct2D), `src/net` (4 files, WebSocket +
  telemetry), `src/sky` (3 files, catalog, ASTAP, pointing model),
  `src/profiles` (2 files), `src/gpu` (1 file, D3D11 compute debayer),
  `src/main.cpp` (5,309 lines, the wiring)
- **Threads (7 total), from `wiki/Threading-Model.md` and verified against code:**

| Thread | Owner | Priority (verified) | Cadence | Owns |
|---|---|---|---|---|
| Main / GUI | `Window::Run()` | Normal | ~30–40 fps | Message pump, D2D render, panels, all Win32 |
| Capture | `CaptureThread` | `THREAD_PRIORITY_TIME_CRITICAL`, optional CPU pin via `SetThreadAffinityMask` — `src/core/CaptureThread.cpp:85,89` | sensor rate | `ASIGetVideoData()` → QPC timestamp → ring slot |
| Writer | `CaptureManager::WriterLoop()` | `THREAD_PRIORITY_ABOVE_NORMAL` — `src/core/CaptureManager.cpp:266` (wiki says "Normal" — drift) | disk rate | Drain WriteBuffer → SERWriter, per-frame sharpness |
| Analyzer | `FrameAnalyzer::Run()` | `THREAD_PRIORITY_BELOW_NORMAL` — `src/core/FrameAnalyzer.cpp:50` | ~2 Hz effective, lossy by design | Histogram, sharpness, seeing, centroid |
| Correction | `CentroidTracker::Run()` | `THREAD_PRIORITY_ABOVE_NORMAL` — `src/core/CentroidTracker.cpp:443` (wiki says "Normal" — drift) | 250 ms locked / 500 ms centering wake-up | Pixel error → `PulseGuide` |
| Autofocus | `AutofocusRoutine::ScanThread()` | Normal, created per scan, **joinable not detached** | on demand | EAF sweep, averaging, parabola fit |
| WebSocket | `WebSocketServer` | Normal | 500 ms broadcast | Accept, broadcast, receive commands |
| (transient) `SiteLocator` | short-lived `std::async` | — | once at startup | Windows Location query, so a hung consent prompt cannot block startup |

- **Preview ring buffer** — `src/core/RingBuffer.h`: `DEFAULT_RING_SIZE = 64` (`:22`), each slot its own `VirtualAlloc(MEM_COMMIT|MEM_RESERVE)` block (`:49-54`), lock-free SPSC. **Every slot claim is a compare-and-exchange**: prefer `FREE`, else CAS an unread `READY` (overwriting it and counting an overrun), never touch `READING`/`WRITING`. On full, it **overwrites the oldest unread frame**. `m_acquiredOffset` makes `CommitWrite` advance `m_writePos` by `offset+1` so newest-first scanning stays correct after skips
- **Frame slot state machine** — `src/core/FrameSlot.h`: `FREE → WRITING → READY → READING → FREE`. The wiki records that `READING` was added in Milestone 4 to fix a use-after-free that crashed the app
- **Write buffer** — `src/core/WriteBuffer.h`: lock-free, strictly sequential, `ready` flag per slot + monotonic read/write positions. On full it **refuses and counts an overrun** — it never overwrites. `Push()` memcpys so the preview slot is freed immediately. Sizing is now dynamic: `FLOOR = 512`, `CEILING = 4096` (`:83-94`), `RAM_SHARE = 0.5` of available RAM, with a comment that 4096 slots is ~5 GB at 1280×960 RAW8 ≈ 34 seconds of buffer. `CaptureManager` checks `GlobalMemoryStatusEx` and aborts the start rather than over-allocating — `src/core/CaptureManager.cpp:27,44`
- The two policies are deliberately opposite: "A preview that shows a frame from two seconds ago is useless… A recording that silently discards frames is worse than useless" — `wiki/Capture-Pipeline.md`
- **Frame lifecycle:** SDK callback → capture thread acquires ring slot by CAS, stamps QPC microseconds + monotonic frame number, commits → render loop `GetLatestFrame()` (newest-first scan, CAS to `READING`) → renders, submits a copy to the analyzer (lossy), and if recording pushes a memcpy into the WriteBuffer with `cropCX/cropCY` from the last centroid → `ReleaseRead()` as the last statement of the tick → writer thread `Peek()/Pop()`, scores sharpness itself, writes to SER → on `Close()`, patches the frame count and appends the timestamp trailer
- **The capture thread does not know recording exists** — it only feeds the preview ring; `CaptureManager` taps the ring from the render loop — `wiki/Capture-Pipeline.md`
- Invariants documented in `wiki/Threading-Model.md`: every `GetLatestFrame()` matched by `ReleaseRead()`; `SubmitCentroid()` once per *new* analysis; the capture thread never waits (drops instead); nothing on a worker thread calls Win32 UI; `mount->Heartbeat()` every render frame feeds the NexStar runaway watchdog so a stalled render loop stops the mount
- GPU path: `src/gpu/GPUDebayer.h` (382 lines) is a D3D11 compute-shader RGGB debayer writing a DXGI-shared texture D2D draws directly, falling back to CPU debayer when D3D11 is unavailable

### Inferences
- The threading documentation is more precise than most commercial products
  publish, and the two-buffer design with opposite policies is a genuinely
  well-reasoned answer to the preview-vs-recording conflict.
- The two priority mismatches between the wiki table and the code
  (writer and correction threads are ABOVE_NORMAL, not Normal) are minor but
  are further evidence that prose numbers need verification.

## Device layer

### Takeaway
Three devices driven natively with no ASCOM in the normal path (ZWO ASI SDK,
ZWO EAF SDK, raw NexStar serial); only the mount is polymorphic. ASCOM support
exists and is a real 792-line implementation, not a stub, but is explicitly
superseded and its known drivers are blacklisted.

### Cited Findings
- Compile-time device selection in `src/core/DeviceFactory.h`: under
  `RADUK_EMULATOR` it includes `CameraEmulator.h`/`EAFEmulator.h`/`MountEmulator.h`,
  otherwise `ASICamera.h`/`EAFFocuser.h`/`NexStarMount.h`/`ASCOMMount.h`/`IMount.h`.
  The emulator camera and focuser classes **carry the same class names**, so call
  sites are identical and there is no virtual dispatch and no mixed build
- Camera: `src/core/ASICamera.{h,cpp}` wraps the ZWO ASI SDK directly —
  `Enumerate/Open/SetROI/SetStartPos/GetROI/SetControl/StartVideoCapture/GetVideoFrame/RestartCapture`.
  Every caller reads the **granted** size back via `GetROI()` rather than assuming
  the request was honoured — `wiki/Device-Layer.md`
- Hardware rails enforced on every profile value — gain 0–570, exposure
  0.032 ms–2,000,000 ms, gamma 1–100, WB red/blue 0–100, USB bandwidth 40–100;
  HCG mode auto-activates at gain ≥ 182 (read noise ~3.5 e⁻ → 0.8 e⁻), full well
  11,270 e⁻ — `src/profiles/TargetProfile.h` via `wiki/Device-Layer.md`
- Focuser: `src/core/EAFFocuser.{h,cpp}` wraps the ZWO EAF SDK; absolute
  positioning across 0–600,000 steps; the real rig focuses near 168,187
- Mount: `src/core/IMount.h` is the only polymorphic abstraction — `Connect`,
  `GetRADec`, `GetAltAz`, `GoToRADec`, `IsSlewing`, `CancelGoTo`,
  `SetTrackingMode`/`GetTrackingMode`/`TrackingModeKnown`, `SlewFixed`,
  `StopSlew`, `SyncRADec`, `IsAligned`, `PulseGuide`, `IsPulseGuiding`,
  `Heartbeat`, `GetVersion`, `ConnectionId`. Implemented by `NexStarMount`
  (primary, 855-line `.cpp`), `ASCOMMount`, `MountEmulator`
- Two virtuals exist specifically to avoid downcasts that previously corrupted
  memory: `TrackingModeKnown()` (default `true`; only serial can fail to get an
  answer, and reporting that as `OFF` is a different claim) and `Heartbeat()`
  (default no-op) — `src/core/IMount.h:55,75,102`. Both came from code-review
  finding T2
- **`src/core/ASCOMMount.h` is a real implementation, not a stub**: 792 lines
  of ASCOM `ITelescopeV3` late binding through COM `IDispatch` with
  `CLSIDFromProgID` + `CoCreateInstance(CLSCTX_LOCAL_SERVER)`, ~20 `Invoke`
  call sites covering `AxisRates` enumeration, slewing, sync and PulseGuide, and
  it uses `COINIT_MULTITHREADED` because ASCOM EXE local servers require MTA.
  It is instantiated in `src/main.cpp:720-722` behind
  `ASCOMMount::FindCPWIDriver()`
- But the wiki labels it **"Superseded. Built and tested, kept for potential
  future use. All known CPWI/DeviceHub/ASIMount/JustAHub/simulator drivers are
  blacklisted in `FindCPWIDriver()`"** — `wiki/Device-Layer.md`. The roadmap
  lists "20 · Distribution / ASCOM support" as unstarted and **Very Hard** —
  `docs/ROADMAP-Next.md` unscheduled table; `RadukCapture-Architecture.md:1539`
- Serial port discovery: `src/core/SerialPortScan.h` enumerates
  `HKLM\HARDWARE\DEVICEMAP\SERIALCOMM` (present ports only), then probes each
  with the NexStar echo command `Kx` expecting `x#`, so a GPS dongle is never
  mistaken for a mount. A port that opens with `ERROR_ACCESS_DENIED`/
  `ERROR_SHARING_VIOLATION` is reported as **busy**, the signature of CPWI
  holding the mount. `rig.ini`'s `MountPort` pins a port; a stale pin falls back
  to auto-detect
- USB traffic arbitration — `src/core/USBTrafficManager.{h,cpp}`: while
  recording, focuser polling drops from 500 ms to **10,000 ms** and mount
  polling from 500 ms to **2,000 ms**; the camera is always allowed. Consequence
  during a recording: the focuser readout is up to 10 s stale and mount position
  up to 2 s stale, deliberately traded against dropped frames — `wiki/Device-Layer.md`

### Inferences
- The device breadth is **one camera model, one focuser model, one mount
  family**. Every competitor in this comparison supports hardware through ASCOM
  (and/or INDI, and/or native SDKs for dozens of camera brands). This is the
  single largest structural gap.
- Because camera and focuser are swapped by *header name* rather than by
  interface, adding a second camera brand would require introducing a
  polymorphic camera abstraction that does not currently exist.

## The emulator subsystem

### Takeaway
A genuinely coupled physical simulation — not a stub layer — in which the
camera and mount share a pointing model so the real tracking loop closes in
software, with fault injection and 13 environment switches. This is the
project's clearest technical differentiator.

### Cited Findings
- `src/core/CameraEmulator.h` is **1,316 lines** (the wiki's "984 lines" is
  stale). It renders synthetic RAW8/RAW16 frames with a per-planet disc,
  surface rotation, moons, turbulence, defocus blur, per-pixel sensor noise and
  per-frame atmospheric blur — `wiki/Emulator-and-Testing.md`
- It reports the real 3840×2160 sensor but **clamps streaming to 1280×960**,
  logging the clamp "the way a real camera reports a mode it cannot deliver" —
  so `FULL` and `1920` both land on 1280×960 while `1280` and `640` are exact
- `src/core/MountEmulator.h` (435 lines) implements `IMount` and models a fixed
  sky target; the pointing error is exposed through `GetPointingErrorArcsec()`,
  which `CameraEmulator` uses to **render the planet displaced from centre**.
  Guide pulses shrink the error and the planet re-centres, so **the full
  `CentroidTracker` correction loop runs closed in emulation** (Phase 52).
  Imperfect tracking is simulated by drifting the mount even while "tracking"
- Phase 78 made emulated jog rates follow the **real NexStar slew-rate curve**
  (rate-9 ≈ 3°/s)
- `src/core/EAFEmulator.h` models motor movement at a realistic step rate with
  absolute positioning and temperature, coupled to the camera's blur model
- **Fault injection and environment switches** — `wiki/Emulator-and-Testing.md`:
  `RADUK_EMU_JITTER_AS` (0.5, seeing wander), `RADUK_EMU_DRIFT_AS` (0.3 arcsec/s
  tracking error), `RADUK_EMU_ROTATION_DEG` (camera angle — proves the tracker
  *measures* its axis transform rather than assuming it), `RADUK_EMU_UNALIGNED`
  (mount reports unaligned, exercising the tracking-blocked refusal path),
  `RADUK_EMU_TRUE_FOCUS` (168000, the position of perfect sharpness),
  `RADUK_EMU_BLUR_PX_STEP` (0.0083 px/step), `RADUK_EMU_FREEZE_SURFACE`,
  `RADUK_EMU_NOISE`, `RADUK_EMU_SEEING_AS`, `RADUK_EMU_PEAK_DN`,
  `RADUK_EMU_TARGET`, **`RADUK_EMU_REMOVE_AFTER_S`** (simulates the camera being
  unplugged after N seconds, which is how the Phase 58 self-healing path is
  tested without pulling a cable mid-capture), and `RADUK_EMU_FPS` (lifts the
  20 fps cap — `docs/ROADMAP-Next.md` P4.3)
- The emulator starts the focuser at 166,500, 1,500 steps off the true-focus
  default, so the app opens slightly soft and an autofocus run is not a no-op
- Measured emulator throughput: **1280×960 at 120.3 fps, 147.5 MB/s, zero
  drops**, and a real recording at **3840×2160 sustaining 293 MB/s with zero
  dropped frames, 556 frames all present** — `docs/ROADMAP-Next.md:997+, 1204+`
- **Documented limits of the emulator**: serial timing (emulator `PulseGuide`
  is instantaneous, NexStar blocks for the pulse duration, so the real
  correction cadence cannot be measured); USB contention has nothing to contend
  with; real throughput (software render cannot produce 1.5 GB/s) so ring and
  write-buffer overrun behaviour under genuine load is untested; whether a
  sky-derived noise floor is an emulator artefact; the `SHARP_CEIL` value;
  whether the pointing model records the right quantity — `wiki/Emulator-and-Testing.md`
- Historical note establishing value: the 2026-07 review found (E1, CRITICAL)
  that before Phase 52 the emulated planet was pinned to frame centre and
  `MountEmulator::PulseGuide` fed nothing back, so "tracking 'passes' trivially
  no matter what the correction logic does" — `docs/CODE_REVIEW_2026-07-06.md`

### Inferences
- Of the six applications being compared, a coupled camera+mount+focuser
  simulator with fault injection is not a standard feature. SharpCap and
  N.I.N.A. have simulator camera drivers (and ASCOM/Alpaca simulators are
  available to any of them), but a *coupled* physical model in which the guide
  loop closes against rendered pixel displacement is unusual.
- The emulator is what makes this project developable without clear nights,
  and the project openly says so.

## Capture and recording

### Takeaway
SER v3 only, with capture-time (not write-time) timestamps correlated once at
file open, per-frame sharpness scores in a sidecar, RAM-first buffering, and
four resolution presets. No AVI and no FITS output.

### Cited Findings
- `src/core/SERWriter.{h,cpp}`: 178-byte packed header with
  `static_assert(sizeof(SERHeader) == 178)`, `FileID "LUCAM-RECORDER"`,
  `ColorID` default `BAYER_RGGB = 8`, frames uncompressed, then an
  **N × int64 timestamp trailer** in 100 ns ticks since 0001-01-01.
  `FrameCount` is written as 0 at `Open()` and patched by `fseek` at `Close()`
- Timestamp correctness (code-review finding C2, fixed Phase 59): the QPC clock
  is correlated with wall time **once at `Open()`** (`m_qpcBaseUs`, `m_ftBase`),
  and each trailer entry is `m_ftBase + (frameQPC − m_qpcBaseUs) × 10`. Before
  this, timestamps recorded disk-write time and could lag ~10 seconds behind the
  photons with a full backlog — useless for WinJUPOS derotation. Epoch constant
  `EPOCH_OFFSET = 504911232000000000LL`
- WinJUPOS compatibility is a *consequence* of correct per-frame capture-time
  stamps in SER v3; the files "open directly in AutoStakkert!, Registax, Siril,
  and PIPP" — `wiki/Recording-and-SER-Format.md`
- File naming: `<outputDir>\<prefix>_<frameType>_<W>x<H>_RAW<bits>_<YYYYMMDD>_<HHMMSS>.ser`,
  with `frameType` ∈ Light/Dark/Flat/Bias (Phase 44)
- Per-frame quality: a `*_quality.txt` sidecar with one sharpness score per
  line plus mean and peak. **Since Phase 76 the writer thread scores each frame
  itself** using `cropCX`/`cropCY` from the analyzer's last centroid, rather
  than repeating the ~2 Hz analyzer value across runs of frames. The wiki
  records that the long-standing claim that AutoStakkert reads this file **is
  false** and was tested and disproved — AS!4 runs its own `BufferAndAnalyze`,
  accepts only avi/ser/as3/dat and image formats, and grades quality per
  alignment point
- RAM-first buffering: WriteBuffer sized from available RAM (floor 512 slots,
  ceiling 4096) — at 1280×960 RAW8 the ceiling is ~5 GB ≈ 34 seconds of
  complete disk stall absorbed — `src/core/WriteBuffer.h:83-94`. `Pending()`
  drives the `BUF` bar in the status panel
- Resolution presets on the toolbar: **FULL 3840×2160, 1920×1080, 1280×960,
  640×480** — `src/gui/ToolbarStrip.h:68-71`. Bit depths 8 and 16
- Documented frame-rate ceilings: full-frame ~47.5 fps at RAW8 (~750 MB/s) and
  ~24 fps at RAW16 (~1.5 GB/s) — `wiki/Recording-and-SER-Format.md`
- ROI handling: one shared capture-format path since Phase 84 at
  `src/main.cpp:1963`. Changing resolution or bit depth closes the current
  recording (a SER cannot change format mid-file), reconfigures the camera,
  **reads back the granted size**, reallocates the ring and if necessary the
  write buffer, places the ROI **by centre not by origin**, and reopens a new
  file. A `FULL` button exists because a crop used to be a one-way door until
  v1.2.3
- `Pause()` does not close the file — `SubmitFrame()` skips incoming frames
  while the writer drains what is buffered, so the SER stays open and valid
- Quality gate applied in the render loop, open when disabled **and** open when
  there is no valid analysis yet, so it cannot block the first recording
- **Not written: no AVI (not built), no FITS (the plate solver reads FITS;
  nothing here writes it), and the recording stays raw — dark/flat correction
  is preview-only** — `wiki/Recording-and-SER-Format.md`; `RadukCapture-Help.md:2253-2259`
- Other outputs: `*_snap_*.png` single-frame PNG snapshots via the WIC encoder
  (`S` key), `session_log.txt`, `D:\RadukCapture\logs\session_<timestamp>.log`,
  and crash-safe `raduk_log.txt`

### Inferences
- SER-only output is adequate for planetary work (it is the planetary standard)
  but rules the product out of deep-sky workflows, which are FITS-centric across
  every competitor.

## Image analysis

### Takeaway
Bayer-aware Laplacian sharpness shared by the live gauge and the recorder, a
rolling 1–10 seeing score, a percentile quality gate, a registering quick-look
stacker, and calibration frames that are explicitly preview-only.

### Cited Findings
- `src/core/Sharpness.h` is the single definition of sharpness, shared by
  `FrameAnalyzer` and the SER writer, created because the two had drifted.
  **Laplacian variance over a crop**, with two Bayer rules documented as having
  been got wrong before: taps must be ±2 px (±1 on RGGB measures a red-minus-green
  colour difference, not detail) and the sampling lattice must be anchored to
  **even** coordinates (an odd start flipped which colour channel was measured
  whenever the planet moved one pixel, making the score jump between two
  unrelated values). RAW16 is read `>> 8` because ASI RAW16 is left-aligned
- `src/core/FrameAnalyzer.{h,cpp}` (430-line `.cpp`): histogram, sharpness,
  `seeingScore` rolling 1–10 over a `SEEING_WINDOW = 20` sample window, with a
  60-sample `seeingHistory` for the graph — `src/core/FrameAnalyzer.h:127-136, 286`
- Histogram correctness has its own findings document, `docs/HISTOGRAM-ACCURACY.md`,
  and its own gated test `tests/histogram_accuracy.cpp` covering Bayer channel
  assignment, what the G channel contains, clip percentages in 8- and 16-bit,
  left-aligned RAW16 handling and sampling coverage
- Quality gate: `seenOk = !gateEnabled || !lastAnalysis.valid || lastAnalysis.seeingScore >= gateThresh`,
  with an amber "GATED (waiting)" indicator — `wiki/Capture-Pipeline.md`
- `src/core/QuickLookStacker.h` (Phase 74): admits only frames in the **top
  ~15%** of the recent distribution and **shifts each admitted frame so the
  planet registers**, with shifts always even (to preserve the Bayer lattice).
  It holds no pixel buffers — `PreviewRenderer` owns those, because the previous
  design caused a heap overflow (review G1). The header records why the old
  `DisplayFX::Stack3/5/10` was wrong: it averaged at fixed pixel indices, and at
  2–5″ seeing and 0.203″/px that is 10–25 px of smear, so the result was
  *softer* than a single frame while the HUD said "STACK 10f". **Display only —
  nothing here touches the SER**
- `src/core/CalibrationFrames.h`: `corrected = (raw − dark) × channelMean(flat) / flat`.
  Masters are **built in the app from the camera** and stored as raw bytes with
  metadata, explicitly *not* loaded from PNG (a PNG is already debayered, so its
  mosaic is gone and it cannot be subtracted from a raw frame). The flat is
  normalised **per Bayer position**, not by a global mean, because the four RGGB
  positions have genuinely different sensitivities and one global mean would
  bake in a colour cast. **"Correction is applied to the PREVIEW only. The
  recorded SER keeps the raw sensor data"** — `src/core/CalibrationFrames.h:29-32`.
  The sharpness metric also reads raw
- `src/core/ADCAlignment.cpp` (376 lines) measures channel dispersion, gated by
  `tests/adc_alignment.cpp`, including the case that matters — a banded planet
  with no dispersion must read zero rather than invent a residual out of its
  cloud belts
- A disproved theory is recorded in the source: Phase 80 used `RADUK_EMU_PEAK_DN`
  to test and reject the idea that the sharpness metric's problem was a noise
  floor (at 0 DN noise the derived "floor" of 35.2 exceeded the entire variance
  of 32.5); the real problem was the ceiling — `wiki/Emulator-and-Testing.md`

### Inferences
- The analysis layer is narrow but unusually carefully validated for a hobby
  project, and the honesty about preview-only calibration is a point in its
  favour for a reviewer, not against it.

## Tracking and guiding

### Takeaway
A three-stage closed-loop planet tracker whose pixel→pulse transform is
*measured* at calibration rather than assumed, which is what lets it tolerate
arbitrary camera rotation and mirror flips.

### Cited Findings
- Four acquisition stages in `src/core/CentroidTracker.h:72-84`: `Searching`
  (detection on, no corrections) → `Calibrating` (measuring pulse→pixel
  response, Phase 51) → `Centering` (slow corrections to centre) → `Locked`
  (fine guiding, ROI optionally cropped)
- Stage constants — `src/core/CentroidTracker.h:111-123`:
  Centering `S2_AGGRESSIVENESS 0.20`, `S2_MAX_PULSE_MS 200`,
  `S2_DEADBAND_ARCSEC 2.0`, `S2_RATE_MS 500`; Locked `S3_AGGRESSIVENESS 0.50`,
  `S3_MAX_PULSE_MS 500`, `S3_DEADBAND_ARCSEC 2.5`, `S3_RATE_MS 250`
- Real cadence caveat documented in the source: `S3_RATE_MS` is the loop
  wake-up rate, but NexStar `PulseGuide` **blocks for the pulse duration**, so a
  Locked cycle firing both axes takes up to `S3_RATE_MS + 2×(maxPulse+50)` ≈ 1.35 s
  worst case — effective correction rate is ~1–4 Hz, not a fixed 4 Hz —
  `src/core/CentroidTracker.h:130-132`
- **Measured guide calibration** — `src/core/GuideSolve.h:1-45`: "Phase 51
  measures how the image responds to a guide pulse: pulse East, see where the
  centroid went, pulse North, same again. That gives a 2×2 matrix from pulse
  milliseconds to pixels, and it is MEASURED, so it already contains the
  camera's rotation in the focuser, the mirror flip of the optical train and the
  sign of each axis. Nothing else in the app knows which way is north in the
  frame, and nothing else needs to." A near-zero determinant is detected as
  parallel responses (axes indistinguishable) and the solve is refused
- This was code-review finding **T3 (CRITICAL) — "No guide calibration:
  pixel→direction mapping is assumed, not measured"** — `docs/CODE_REVIEW_2026-06-10.md:71`,
  deferred as "a new routine, not a patch" and delivered as Phase 51
- The same solver is now shared by the guide loop and click-to-recentre so
  there cannot be two sign conventions — `src/core/GuideSolve.h:12-18`,
  `docs/ROADMAP-Next.md` item 4 / `tests/recentre.cpp`
- `RADUK_EMU_ROTATION_DEG` exists specifically to prove the tracker measures
  rather than assumes: "a tracker that assumed camera orientation corrects in
  the wrong direction and fails visibly, while one that measures its 2×2
  transform does not care" — `wiki/Emulator-and-Testing.md`
- Stage transition ownership is split and documented: Off transitions on the
  main thread, `Calibrating→Centering` and `Calibrating→Searching` on the
  correction thread, with `m_stage` atomic and `m_calPending`/`m_calSuccess`
  flags bridging to the main thread's user-facing callback — `wiki/Threading-Model.md`
- Safety: a 2 s centroid-loss cutoff; the render loop's heartbeat feeds the
  NexStar runaway watchdog; jog input suspends the guide loop via a generation
  counter; `WM_CAPTURECHANGED` stops slewing on mouse-capture loss
- Known honest limitation, carried: "**6.6 GoTo map learning** — auto solve+sync
  on lock ships; the accumulating pointing map **reports but does not correct**.
  Half a feature, and the half that is missing is the half that helps" —
  `docs/ROADMAP-Next.md:986-990`

### Inferences
- Measured-transform calibration is the same approach PHD2 uses and is strictly
  better than orientation-assuming designs; the README's "PHD2-style measured
  guide calibration" is accurate as a description of method, though the product
  does not integrate with PHD2 itself.

## Autofocus

### Takeaway
An 11-position V-curve scan with 8-frame averaging per position, a local
three-point parabola (deliberately not a global least-squares fit), and a
revert that reports honestly when the scan made focus worse.

### Cited Findings
- Parameters — `src/core/AutofocusRoutine.h:61-70`: `scanRange = 6000` (±3000
  from start), `nSteps = 11` (600-step spacing), `framesPerStep = 8`,
  `settleMs = 600`, `backlash = 200` steps of compensation on the final move,
  `minSharpness = 2.0` abort floor, step bounds 0–600,000
- Tuning rationale in the header: the 8SE has ~2000 steps/mm at f/10 and the
  focus zone is ±500–1000 steps at typical planetary seeing — `src/core/AutofocusRoutine.h:54-59`
- State machine `AFState`: Idle / Scanning / Moving / Complete / Failed
- **Fit is a three-point parabola through the peak sample and its two
  neighbours, not the global least-squares fit across all 11 samples**: "a
  defocus curve is NOT a parabola: it has a sharp peak and long shallow tails,
  and a global least-squares fit is dragged by the tails. Measured against a
  known true focus of 51500, the global fit returned 52224 — 724 steps out,
  against a sample spacing of 600" — `src/core/AutofocusRoutine.h:497-510`
- Rejection criteria: degenerate parabola rejected at `fabs(a) < 1e-10`
  (`:605`); vertex outside the scan range rejected; upward-opening fits were
  code-review finding **S3 (HIGH)** — `docs/CODE_REVIEW_2026-06-10.md:464`
- **Revert behaviour** — `src/core/AutofocusRoutine.h:444-479`: `REGRESSION_FRAC = 0.50`.
  If `finalSharp < startSharpness × 0.50` the routine drives back to the start
  position and reports `Finish(false, ...)` with "Focus got worse (x% → y%) —
  reverted to start; try a wider sweep or check the target". The source records
  the bench observation that drove this: "start 11.3%, final 0.0%, reported as
  success". The 0.50 margin is deliberately loose because start and final are
  measured 45–60 s apart and seeing varies on that timescale
- A "focus not bracketed" warning is emitted when the sweep ended at the peak
- Final move approaches from below through the backlash offset — `:405-415`
- Move safety: `MOVE_STALL_MS = 5000`, `MOVE_CAP_MS = 180000` — `:639-642`
- Sample pacing is set by the ~2 Hz analyzer, not frame rate: five samples take
  ~2.5 s, which is why the verification window is 8,000 ms — `wiki/Threading-Model.md`
- The autofocus thread is joinable, not detached: review finding **G4** found a
  detached scan thread could outlive `RunApp()`'s locals and invoke a destroyed
  `std::function` at shutdown — `docs/CODE_REVIEW_2026-06-10.md:392`

### Inferences
- The autofocus is tuned specifically for a planetary disc; the roadmap itself
  notes star-focus needs FWHM/Bahtinov, which are unbuilt (phases 31/32, effort
  "H").

## Sky, GoTo and plate solving

### Takeaway
A compact built-in catalog (110 Messier + 7 planets + 97 bright stars = 214
objects), a hand-drawn Direct2D polar sky map, ASTAP shelled out for plate
solving, and a pointing model that records errors but does not yet correct them.

### Cited Findings
- Catalog contents, counted from the arrays in `src/sky/SkyObject.h`:
  **`kMessier[]` 110 entries** (`:365`), **`kPlanets[]` 7 entries** (`:108`),
  **bright-star table 97 entries** (header comment at `:479` says "Bright Star
  Catalog — 255 stars mag ≤ 4.5", which **does not match the 97 rows actually
  present** — a further documentation drift). Total catalog ≈ **214 objects**
- Object types supported: `"Planet"`, `"Messier"`, `"NGC"`, `"Star"` —
  `src/sky/SkyObject.h:29`; the header claims an "NGC bright object subset" at
  `:9` but no separate NGC table was found in the file
- `src/gui/SkyMapWindow.h` (1,435 lines) is a Direct2D polar sky map; a second
  map on the control screen shipped as Phase 90 (`68a1fb6`), and a share view
  for a screen pointed at other people as Phase 89
- Plate solving: `src/sky/ASTAPSolver.h` saves the current frame as **BMP** to a
  temp path, runs `ASTAP.exe`, and parses the `.ini` output for RA/Dec. ASTAP
  must be installed separately with an H18 or D80 star catalog. Review finding
  **S2 (HIGH)** was that it parsed keys ASTAP never writes and timed-out solves
  left zombie processes; the security review records this as "Hardened Phase 54 —
  temp files uniquely named, process killed on timeout"
- Pointing model: `src/sky/PointingModel.h` writes a CSV at
  `D:\RadukCapture\data\pointing_model.csv` with timestamp, az, alt, expected
  and actual RA/Dec, error in arcsec, temperature, seeing score and object name;
  plain file I/O, mutex-serialised. The repo ships a 128-byte
  `data/pointing_model.csv`
- **The model reports and does not correct**: "auto solve+sync on lock ships;
  the accumulating pointing map reports but does not correct. Half a feature,
  and the half that is missing is the half that helps" — `docs/ROADMAP-Next.md:986-990`.
  Whether the recorded quantity is even the right one is listed as an open
  question needing a real mount — `wiki/Emulator-and-Testing.md`
- A knowledge database (`data/object_knowledge.db`, 2.4 MB SQLite) backs an
  object info panel, reachable only since Phase 92a — before that "the 2.4 MB
  object_knowledge.db built in March has never once been readable by the running
  app… silently" — `CMakeLists.txt:202-210`. 670 non-solar objects have no
  mythology/cultural narrative and no source is wired up to supply one —
  `docs/ROADMAP-Next.md` ("Mythology for the deep sky")

### Inferences
- A 214-object catalog is adequate for planetary and naked-eye targets and is
  far smaller than the catalogs N.I.N.A./SGP/SharpCap ship or reach through
  Simbad/online services; the SIMBAD integration attempt is recorded as having
  "never worked" (`190b001`: "The SIMBAD phase has never worked: 9 distances
  become 107").

## Advisors and AI

### Takeaway
A physically-grounded atmospheric dispersion advisor using Edlén (1966) over
the *actual fitted filter band*, an integral-controller auto-exposure, a
silence-when-ready readiness check, and a strictly local Ollama assistant with
no cloud path.

### Cited Findings
- `src/core/ADCAssistant.h`: computes **R−B atmospheric dispersion in
  arcseconds** plus a dispersion vector, always along the altitude axis.
  Implements the **Edlén (1966) refractivity of dry air at STP** (`:106-119`)
  and evaluates it **between the edges of the band of the filter actually
  fitted** (`rigf.filterLowNm`/`filterHighNm`, `:135-136`) rather than a fixed
  400/700 nm, converting via `Δ = (n_blue − n_red) × tan(z) × 206265` (`:138-139`).
  The header records the correction of a hardcoded pressure constant
  "tied to one town" (918 hPa ≈ 830 m) to the configured site elevation,
  worth ~0.2″ on a 5.3″ dispersion (`:88-100`). `rig.ini` warns that a wrong
  `FilterName`/band "moves the ADC figure by up to 4×" — `README.md`
- Band edges are validated: both are reset to the UV/IR-cut default if either
  falls outside 300–1200 nm or the band is narrower than 10 nm, "because a
  nonsense band would push the Edlen model outside its domain and return an
  authoritative-looking meaningless figure" — `RadukCapture-Help.md:2317`
- The ADC **advisor** exists; the ADC **alignment tool** (dialling a corrector
  in) is Phase 35, marked 🔲 not built — `RadukCapture-Architecture.md:1522`,
  `RadukCapture-Help.md:2283-2285`. (`src/gui/ADCAlignWindow.h`, 431 lines, and
  `src/core/ADCAlignment.cpp`, 376 lines, exist and measure dispersion in the
  frame; commit `9360842` is titled "ADC alignment tool, dark until there is an
  ADC to align" — so the measurement half is built and the correction-assist
  half is not.)
- `src/core/AutoExposureAssistant.h`: keeps the histogram peak in **50–70%
  (127–178 DN)** using an **integral-only controller** (P-only oscillates with a
  slow camera loop); adjusts **exposure first, then gain**; emergency cut on
  `clipPct`; enforces an **HCG floor of gain ≥ 182** in AUTO mode. Two modes,
  ADVISORY (text) and AUTO (applies). Review finding **S4 (HIGH)** was that the
  HCG floor raised gain on already-bright low-gain profiles
- `src/core/Readiness.h` (Phase 68): "the app verifies what it can. Asking
  someone to self-certify a fact the machine already knows is theatre" — states
  are Pass / Warn / Fail / Unknown / Manual, with **only three items requiring a
  human**. Second design rule: "silence when ready". Suppressible via
  `ShowReadinessCheck=0`, with the log line retained
- `src/core/AdvisoryLoop.h` provides occasional unprompted advice, off by
  default (`AdvisoryEnabled`, interval 30–3600 s, default 180) — `RadukCapture-Help.md:2313-2314`
- AI assistant — `src/core/AIAssistant.h:3-12`: WinHTTP streaming client to
  **`http://localhost:11434/api/chat`**, model **`llama3.2:3b`**, `stream:true`,
  NDJSON chunk parsing, with an explicit reachability probe and the error text
  "Cannot connect to Ollama on localhost:11434… ollama serve"
- Context given to it — `src/core/AIContextBuilder.h:5-14` (822 lines):
  **`[IDENTITY]`** persona + site; **`[APP REFERENCE]`** the relevant sections
  of `RadukCapture-Help.md` (CMake copies the help file next to the exe for
  exactly this reason — `CMakeLists.txt:182-198`); **`[SESSION STATE]`** live
  snapshot (local time, LST, target, profile, mount RA/Dec/alt/az, tracking,
  gain, exposure, fps, seeing score 0–10, capture format and whether cropped);
  **`[TONIGHT'S SKY]`** planner data; **`[CAPTURES TONIGHT]`** SER listing from
  the output dir; **`[OBJECT KNOWLEDGE]`** from the SQLite database
- Retrieval over the project's own corpus shipped 2026-10-17 (`85b9d51`): "16
  documents and the six most recent session logs, matched by line and bounded at
  8.3 KB" — `docs/ROADMAP-Next.md:846`
- **Privacy posture**: the security review's secrets scan is CLEAN and records
  "The AI assistant talks only to a local Ollama instance (`localhost:11434`) —
  no cloud keys involved"; the attack-surface table lists the assistant as
  "Outbound to localhost:11434 (Ollama) only — OK" — `docs/SECURITY_REVIEW_2026-07-11.md` §3, §5
- What it cannot do: it is a chat/advice surface given read-only context; no
  tool-calling or app-control path appears in `AIAssistant.h`. Its quality is
  capped by the 3B model — the roadmap holds "a larger model… an 8B at 4-bit
  fits the 2060's 6 GB" behind a driver constraint (546.01 caps at CUDA 12.3),
  and notes "AI runs on CPU (D4) — Not actionable in code" — `docs/ROADMAP-Next.md:870-878`
- Note the dashboard, unlike the app, **does** call public internet APIs
  (open-meteo, 7timer, clearoutside, cleardarksky, nominatim) — so "fully local"
  is true of the application and the AI, not of the browser dashboard

### Inferences
- The dispersion advisor working over the *fitted filter band* rather than a
  fixed visible band is more physically careful than a generic ADC calculator,
  and I found no equivalent claimed feature in a mainstream capture application.
- The local-only AI is a real privacy differentiator against any cloud-assistant
  approach, but the 3B model and read-only context make it an explainer rather
  than an operator.

## Telemetry

### Takeaway
A hand-rolled RFC 6455 WebSocket server on Winsock2, loopback-bound by default,
broadcasting a flat JSON telemetry object every 500 ms to a self-contained
browser dashboard — with **no authentication** on the remote-command path.

### Cited Findings
- `src/net/WebSocketServer.{h,cpp}`: "Minimal RFC 6455 WebSocket server built on
  Winsock2" — `src/net/WebSocketServer.h:4`. Hand-rolled **SHA-1**
  (`WebSocketServer.cpp:10-50`) and **base64** (`:52+`), the RFC 6455 §4.2.2
  handshake with GUID `258EAFA5-E914-47DA-95CA-C5AB0DC85B11` (`:257-261`), and a
  masked client→server frame parser (`WebSocketServer.h:102`)
- Default port **8765**; `Start(uint16_t port = 8765, bool bindAny = false)` and
  `addr.sin_addr.s_addr = bindAny ? INADDR_ANY : htonl(INADDR_LOOPBACK)` —
  `src/net/WebSocketServer.h:85`, `src/net/WebSocketServer.cpp:88-123`. README
  confirms "loopback-only by default; `RADUK_WS_LAN=1` to expose on a trusted LAN"
- APEX JSON schema — `src/net/APEXTelemetry.h:7-23`: `rc_running, fps,
  exposure_ms, gain, gamma, wb_red, wb_blue, usb_bw, sensor_temp, width, height,
  bit_depth, eaf_position, eaf_temp, eaf_moving, mount_ra, mount_dec,
  mount_tracking, recording, frames_recorded, frames_dropped, limit_frames,
  elapsed_sec, bytes_written, write_rate_mbs, ram_used_slots, ram_total_slots,
  disk_free_gb, output_dir, file_prefix, capture_file, seeing_score, seeing_rms,
  histogram[256], session_events[]`. Broadcast every 500 ms from the render loop
- JSON is hand-serialised (`std::ostringstream`), no JSON library
- Browser dashboard: `RadukCaptureDashboard.html`, one self-contained 5,453-line
  file (292 KB), with an "RC tab" consuming the above
- Remote commands are accepted (jog, load_profile among them — review finding
  **G3** found `load_profile` fused into the `jog` branch). Security review:
  "WebSocket server :8765 (mount/focuser commands, **no auth**) — Loopback-only
  by default; LAN requires `RADUK_WS_LAN=1`. Hardened Phase 57 (frame-length
  clamp, recv cap) — acceptable for trusted LAN; **do not port-forward**" —
  `docs/SECURITY_REVIEW_2026-07-11.md` §5. The original finding **S1 was
  CRITICAL — "WebSocket server: remote overflow + wide-open hardware control"**
  — `docs/CODE_REVIEW_2026-06-10.md:443`

### Inferences
- Loopback-by-default plus an explicit opt-in env var is the right default, but
  an unauthenticated mount-control channel is a real limitation for anyone who
  wants genuine remote operation — competitors in this space either have no such
  surface or put it behind an account/tunnel.

## Testing

### Takeaway
13 bench tests gate a merge by exit code, covering roughly the numeric/logic
core; there is no CI, no coverage measurement, and the GUI, device and threading
code — the bulk of the tree — is not under test.

### Cited Findings
- `tests/run-tests.ps1:22-36` lists **13 gated tests**: `histogram_accuracy`,
  `adc_alignment`, `sensor_analysis`, `write_buffer_sizing`, `sky_conditions`,
  `capture_thread_submit`, `ai_context_facts`, `object_knowledge`,
  `object_panel_text`, `slew_variable`, `planet_apparent_size`, `profile_notes`,
  `recentre`
- Plus 4 manual harnesses not in the gate: `manual-autostakkert-launch.cpp`,
  `manual-emulator-blur-equivalence.cpp`, `manual-emulator-defocus-cost.cpp`,
  `manual-emulator-delivery-rate.cpp`, and 4 manual PowerShell checks
  (`manual-goto-layout-bounds.ps1`, `manual-night-vision-coverage.ps1`,
  `manual-record-throughput.ps1`, `manual-window-coverage.ps1`)
- Mechanism: no test framework at all — each test is a `.cpp` with a `main()`
  returning 0/non-zero, compiled by `cl /std:c++20 /EHsc /O2`; the runner's exit
  code is the whole contract — `tests/README.md`, `tests/run-tests.ps1:85`
- Only **4 of 17 production `.cpp` files** are linked into any test:
  `FrameAnalyzer.cpp`, `ADCAlignment.cpp`, `CaptureManager.cpp`,
  `WriteBuffer.cpp`, `SERWriter.cpp` (5 of 17, via 3 tests) —
  `tests/run-tests.ps1:23-35`. Nothing links `CaptureThread.cpp`,
  `CentroidTracker.cpp`, `USBTrafficManager.cpp`, `Window.cpp`,
  `PreviewRenderer.cpp`, `ControlPanel.cpp`, `WebSocketServer.cpp`,
  `APEXTelemetry.cpp`, or any device `.cpp`
- Test code volume: 3,084 lines total across `tests/` vs 44,754 lines of source
  (~6.9% by line count, and much of that is harness)
- **No CI**: no `.github/` directory exists; the gate is a local batch file
- The runner had its own documented defect: `tests/_build` is gitignored and the
  runner never copied `sqlite3.dll`, so `object_knowledge` "passed only on this
  machine, in this working directory… On a clean checkout the gate failed" —
  fixed by copying the DLL as part of the run — `docs/ROADMAP-Next.md:912-926`,
  `tests/run-tests.ps1:49-70`
- `wiki/Emulator-and-Testing.md` still states "There is no automated test suite.
  …no unit tests, no integration tests, and no CI" — stale for the first two
- The manual test plan for anything the emulator cannot settle is
  `docs/CLEAR-NIGHT-CHECKLIST.md` (23 KB, items 1–14); the project explicitly
  excludes hardware/sky tests from the gate because "they cannot gate anything"
  — `tests/README.md`

### Inferences
- The test philosophy is sound and well argued ("A test that always reports
  success is worse than no test, because it is believed"), but coverage is
  concentrated on arithmetic and text-formatting correctness. The lock-free
  buffers, the correction loop, the device layer and the entire GUI are verified
  by the emulator and by reading, not by tests.

## Licensing

### Takeaway
PolyForm Noncommercial 1.0.0 — source-available and explicitly **not**
open source; commercial use is prohibited, which rules the project out of any
comparison framed as "open-source alternative".

### Cited Findings
- `LICENSE` is PolyForm Noncommercial License 1.0.0 verbatim, linking
  `https://polyformproject.org/licenses/noncommercial/1.0.0`
- README: "PolyForm Noncommercial 1.0.0 — see LICENSE. The ZWO SDKs under
  `external/` remain the property of Suzhou ZWO Co., Ltd. under their own terms"
- PolyForm Noncommercial is not an OSI-approved licence: it grants use "for any
  permitted purpose" where permitted purpose excludes commercial use, and
  conditions distribution and derivative works on the same terms — `LICENSE`
- The security review flags the vendored SDKs as a licensing grey area:
  "Redistribution of vendor SDKs in a public repo is a licensing gray area —
  ZWO's developer terms generally allow shipping the runtime DLLs with
  applications, but review ZWO's current SDK license or replace with a
  'download from ZWO' setup step if in doubt" — `docs/SECURITY_REVIEW_2026-07-11.md` §4

### Inferences
- Against N.I.N.A. (MPL-2.0) and PHD2 (BSD), RadukCapture is *less* free;
  against SharpCap, FireCapture, SGP and ASIStudio (all proprietary) it is more
  inspectable. It occupies a middle category: source-available, noncommercial,
  single-author.

## Project maturity signals

### Takeaway
Version 1.2.3, roughly 92 numbered phases of work, extensive review
documentation — but a git history reset to 52 commits over 5 days, no releases,
no installer, no CI, and no first hardware light for several of its headline
subsystems.

### Cited Findings
- Version: **1.2.3** — `src/Version.h:15-16`
- Git: **52 commits total**, first 2026-10-13 20:42, last 2026-10-18 11:12 —
  spread 5 days (5 / 9 / 34 / 4 commits on 13th / 14th / 17th / 18th). The first
  commit (`ffe917d`) is an ordinary feature commit, not an initial import
- The short history is explained by the security review: §2 "⚠ Git history still
  contains everything — ACTION REQUIRED… reset history to a single fresh initial
  commit… **Do not skip this step** — the address exposure is only mitigated,
  not eliminated, until history is gone" — `docs/SECURITY_REVIEW_2026-07-11.md` §2.
  **So the git log is not a maturity signal for this project**; the real history
  is in the phase numbers (≥92) and the version number
- Code reviews: `docs/CODE_REVIEW_2026-06-10.md` (28 KB) found **8 CRITICAL**
  (T1 malformed PulseGuide so guide pulses could not work; T2 downcast corrupting
  `ASCOMMount`; T3 no measured guide calibration; T4 planet ephemeris sign error
  in two copies; C1 RingBuffer producer stomping slots the consumer was
  acquiring, producing torn preview frames and torn SER writes; G1 heap overflow
  on frame-size change with Stack FX; G2 rubber-band ROI freeing the frame the
  render loop was using; S1 WebSocket remote overflow + wide-open hardware
  control), plus ~12 HIGH and a long MEDIUM/LOW tail across tracking, capture,
  GUI and sky/net
- Follow-up `docs/CODE_REVIEW_2026-07-06.md` **verified every T1–T16 fix
  individually against the code with file:line citations** and concluded "The
  T1–T16 batch is real and correctly implemented. The tracking chain is
  substantially safer than at the June review." It then raised a new CRITICAL,
  **E1 — the tracking loop had never been closed even in emulation** (the
  emulated planet was pinned to frame centre), which drove the Phase 52
  emulator coupling
- `docs/SECURITY_REVIEW_2026-07-11.md` found and removed a full street address,
  house-level coordinates, session history, hostnames and a desktop screenshot
  from the repository; secrets scan CLEAN; flagged git history (acted on) and
  the vendored SDKs (still present)
- Later reviews: `docs/Tracking-Findings-2026-10-08.md`,
  `docs/Tracking-Review-2026-10-09.md`, `docs/Control-Review-2026-10-09.md`,
  `docs/HISTOGRAM-ACCURACY.md`
- Phase roadmap table — `RadukCapture-Architecture.md:1490-1539`: phases 51–59
  all "✅ validated (emu) 2026-07-11"; a long run of ✅ v1.0.x–v1.2.x features;
  and 15 phases still 🔲
- Review findings are traceable from the source: "review C1", "review G4",
  "review S1" references appear in code comments pointing back at these documents
- No releases, tags, installer, or packaging exist in the repo; build output
  paths are hardcoded to `D:/RadukCapture/` — `CMakeLists.txt:227-233`
- Single maintainer: all 52 commits are from one GitHub account (`Maligzar`),
  the documents repeatedly address "the owner" as the only decision-maker
  (e.g. "Both questions answered by the owner, 2026-10-17" —
  `docs/ROADMAP-Next.md:927`), and the rig is one person's specific telescope

### Inferences
- Reviewing one's own code this rigorously, publishing the findings, and then
  verifying each fix with citations is a maturity signal that most hobby
  projects and some commercial ones lack.
- But the project has an unusual profile: deeply reviewed, lightly tested,
  broadly documented, narrowly deployed. Several headline subsystems
  (plate scale, SHARP_CEIL, click-to-recentre, the real camera over real USB)
  are explicitly unverified on sky — `docs/ROADMAP-Next.md:1204-1234`.

## WEAKNESSES AND GAPS

### Takeaway
RadukCapture is a single-rig, single-platform, planetary-only capture tool. It
does not do deep sky, does not sequence, does not support any hardware beyond
one camera, one focuser and one mount family, has no installer, no plugin
system, no scripting, and no cross-platform story. Every one of these is a
standard capability in at least one competitor.

### Cited Findings — missing capability, with the evidence it is missing
- **Deep-sky / long-exposure imaging**: not a target. The product is "High-speed
  planetary capture suite" — `README.md`; autofocus is "tuned for a planetary
  disc" and star focus (FWHM/Bahtinov) is unbuilt — `docs/ROADMAP-Next.md`
  unscheduled table, phases 31/32, effort "H". A `dso.ini` profile exists in
  `profiles/` but the capture path, output format and analysis are all planetary
- **Filter wheel**: no support. No EFW/filter-wheel code anywhere in `src/`;
  `FilterName`/`FilterLowNm`/`FilterHighNm` in `rig.ini` describe a *physically
  fitted, non-detectable* filter — `README.md`, `RadukCapture-Help.md:2311,2317`
- **Rotator**: no support. Nothing in `src/` or the roadmap
- **Dome / roll-off roof / observatory control**: no support, not on the roadmap
- **Sequencing / automation / scripting**: no sequencer, no script engine, no
  job list. `src/gui/GoToPanel.h` has "planner rows" (Phase 14 ✅ —
  `RadukCapture-Architecture.md:1516`) which show tonight's imaging windows; that
  is a planning *display*, not an executable sequence
- **Multi-night planning**: "19 Multi-Session Stats Dashboard" is 🔲 —
  `RadukCapture-Architecture.md:1534`
- **Meridian flip**: not applicable and not implemented — the mount is alt-az
- **PHD2 integration**: none. Guiding is self-contained; "PHD2-style" in the
  README describes the *calibration method*, not interoperability
- **Polar alignment**: "**Polar alignment assistant.** Nothing guides a polar
  align" — `RadukCapture-Help.md:2264-2266`; Phase 16 is 🔲
  (`RadukCapture-Architecture.md:1528`) and is **backlogged, not deferred**:
  "The 8SE is alt-az and there is no wedge, so the procedure does not apply. It
  comes back only with a wedge and most likely a new mount, which makes it a
  different project" — `docs/ROADMAP-Next.md:927-934`
- **FWHM / Bahtinov focus aids**: "No multi-star FWHM score and no Bahtinov mask
  assistant" — `RadukCapture-Help.md:2261-2262`; phases 31/32 🔲
- **AVI output**: "No AVI. Not built" — `wiki/Recording-and-SER-Format.md`;
  Phase 18 🔲
- **FITS output**: "the plate solver reads FITS but nothing here writes it" —
  `RadukCapture-Help.md:2259`; grep of `src/` confirms no FITS writer. (Note the
  phase table wrongly marks Phase 22 "Snapshot PNG/FITS" ✅ —
  `RadukCapture-Architecture.md:1501`)
- **Calibration applied to output**: dark/flat exist but are **preview only**;
  the recorded SER stays raw — `src/core/CalibrationFrames.h:29-32`,
  `RadukCapture-Help.md:2253-2256`
- **Stacking**: "Deliberately not built… AutoStakkert does the real work, and
  nothing here would beat it" — `RadukCapture-Help.md:2287-2289`. The quick-look
  stacker is display-only
- **Plugin architecture**: none. No plugin loader, no extension points, no
  public API; the codebase is one statically linked executable
- **Scripting / command-line / headless**: none found. Remote control exists
  only as unauthenticated WebSocket commands
- **Cross-platform**: Windows-only by construction — `add_executable(... WIN32)`,
  Direct2D/DirectWrite/D3D11/WIC/Winsock2/WinHTTP/COM, MSVC-only flags, Win32
  message loop throughout `src/gui` — `CMakeLists.txt:124,155-181`. Porting would
  mean rewriting all 20,809 lines of `src/gui` plus the GPU and net layers
- **Hardware breadth**: exactly **one camera (ZWO ASI678MC), one focuser (ZWO
  EAF), one mount family (Celestron NexStar, tested on an 8SE)** —
  `wiki/Home.md:3-8`. Camera and focuser are not polymorphic at all; they are
  selected by swapping a header with an identically named class
  (`src/core/DeviceFactory.h`), so a second camera brand requires introducing an
  abstraction that does not exist
- **ASCOM as a supported path**: the implementation exists and works, but all
  known drivers are blacklisted and it is marked "Superseded"
  (`wiki/Device-Layer.md`), while "20 Distribution / ASCOM" is 🔲 **Very Hard**
  (`RadukCapture-Architecture.md:1539`). Effectively: **no ASCOM support in
  practice**, and no INDI/Alpaca at all
- **Multi-camera**: none. The design assumes one camera; no guide-camera
  support, no dual-stream
- **Installer / distribution**: none. No tags, no releases, no MSI/NSIS, no
  signed binary; the user must install Visual Studio and CMake 4.2 and build
  from source (`README.md`), and output directories are hardcoded to `D:/`
  (`CMakeLists.txt:227-233`). "20 Distribution" is 🔲 Very Hard
- **Multi-monitor workspace**: "No multi-monitor workspace — every window opens
  on whichever screen the main window is on, and positions are not remembered
  between runs" — `RadukCapture-Help.md:2273-2275`; Phase 42 🔲
- **Framing helpers**: no push-to assistant (Phase 30 🔲), no "go to previous
  image" framing recall (Phase 48 🔲). Click-to-recentre shipped 2026-10-18 but
  is "unverified on sky" — `RadukCapture-Architecture.md:1523`
- **Connect/disconnect hardware without restarting**: Phase 46 🔲 — hardware
  connection is "currently all-or-nothing at startup", named in the roadmap as
  "the one that pays back every session" — `docs/ROADMAP-Next.md` next-in-order
  table
- **Analysis extras**: no stellar photometry (Phase 33 🔲 Very Hard), no
  solar/lunar framing (Phase 34 🔲), no deep-sky annotation (Phase 43 🔲), no
  image annotation overlay — `RadukCapture-Help.md:2279-2281`
- **Pointing model corrects nothing** — reports only, "half a feature, and the
  half that is missing is the half that helps" — `docs/ROADMAP-Next.md:986-990`
- **SIMBAD/online catalog integration**: attempted and abandoned — "The SIMBAD
  phase has never worked: 9 distances become 107" (commit `190b001`)
- **Knowledge base gaps**: 670 non-solar objects have no cultural/mythological
  content and no source is wired up; the builder's cited source was never
  implemented — `docs/ROADMAP-Next.md`
- **No authentication on remote control** — `docs/SECURITY_REVIEW_2026-07-11.md` §5
- **No CI**; 13 tests touching 5 of 17 `.cpp` files; the GUI, device, threading
  and network layers are untested

### Cited Findings — risk assessment
- **Single-maintainer risk is total.** All 52 commits are from one account; the
  documents treat "the owner" as the sole decision-maker and the sole source of
  hardware answers (`docs/ROADMAP-Next.md:927-934` — two roadmap items sat
  unanswered because only the owner could decide them). There is no contributor
  guide, no issue templates, no `.github/`, no CI, and a noncommercial licence
  that discourages a company from adopting it. The bus factor is 1
- **Hardware-specificity risk is structural, not incidental.** The tuning
  constants are derived from one optical train: autofocus assumes ~2000
  steps/mm at f/10 and a ±500–1000 step focus zone
  (`src/core/AutofocusRoutine.h:54-59`); the emulator's true focus is 168,000
  because "the real rig focuses near 168,187"; the HCG floor of 182 is an
  IMX678 property; `PlateScaleArcsecPx=0.203` is one scope-camera pair; the
  SER header hardcodes `Instrument "ZWO ASI678MC"` and `Telescope "Celestron 8SE"`
  defaults. A different rig does not just need new config — several numbers are
  compiled in
- **Unverified-on-sky risk.** The project states that plate scale "has never
  once been checked against the sky, and every arcsecond figure in the app
  derives from it, the ADC tool's included", and that `SHARP_CEIL` "biases the
  capture gate and autofocus" and is unchecked — `docs/ROADMAP-Next.md:1204-1234`.
  Clear-night checklist items 1–12 and 14 are open. The real camera over real USB
  has never been measured; the 293 MB/s figure is an emulator result
- **The emulator's own disclaimed limits** mean the correction loop's real
  cadence, USB contention and genuine overrun behaviour are untested —
  `wiki/Emulator-and-Testing.md`
- **Build-environment fragility**: `cmake_minimum_required(VERSION 4.2)` plus
  MSVC-only flags plus hardcoded `D:/` output paths plus a manual
  vswhere/vcvars64 test runner means the project builds on essentially one
  configuration

### Inferences
- Positioned against the comparison set: RadukCapture competes only in the
  planetary-capture lane (FireCapture, SharpCap, ASIStudio), and only for one
  specific rig. It does not compete with N.I.N.A. or Sequence Generator Pro at
  all, since it has no sequencer, no filter wheel, no rotator, no meridian flip,
  no FITS and no deep-sky workflow.
- Its defensible advantages are narrow but real: the coupled fault-injecting
  emulator; measured (not assumed) guide calibration shared by guiding and
  recentring; the filter-band-aware Edlén dispersion advisor; per-frame
  capture-time SER timestamps with a per-frame quality sidecar; a fully local AI
  assistant with a live rig snapshot; and an unusually candid self-documentation
  practice that states what does not work.
- Its disadvantages are categorical: hardware breadth, platform, distribution,
  automation, output formats, and project bus factor.

### Gaps
- I could not measure what fraction of the 44,754 lines is comment versus code,
  so "lines of C++" overstates logic volume by an unknown amount.
- No benchmark of the real ASI678MC over real USB exists in the repo; all
  throughput numbers are emulator-derived (the project says so).
- The repo contains no issue tracker, release history or user feedback, so
  adoption and real-world reliability cannot be assessed from it.
- `wiki/Home.md`'s "86 source files" could not be reconciled with any subset of
  the tree I counted (83 headers alone, 100 total); I report the discrepancy
  rather than guessing at the counting rule the author used.
