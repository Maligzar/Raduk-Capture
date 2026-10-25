# Code Map

Every source file, what it owns, and how big it is. ~34,500 lines across 86
files; **only 15 translation units compile** — the rest are header-only,
included into `main.cpp`.

Line counts are from the revision this wiki was generated from.

---

## Top level

| File | Lines | Owns |
|------|-------|------|
| [`src/main.cpp`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/main.cpp) | **4,305** | `WinMain`, `RunApp()`, startup dialog, hardware check, crash handler, all callback wiring, the render loop, the profile editor modal, shutdown summary |
| `src/Version.h` | 16 | `RADUK_VERSION_W` / `_A`. **The single source of truth for the version** — CMake derives the project version and the `.rc` resource from it. Shown in the title bar with `main.cpp`'s compile timestamp |
| `src/RadukCapture.rc.in` | 54 | Win32 resource **template** — icon and version info (Phase 44). Expanded by `configure_file` into `${CMAKE_BINARY_DIR}/RadukCapture.rc`, which is what compiles |

`RunApp()` is the composition root: every device, thread, and panel is a
local, wired with lambdas. Notable landmarks:

| Line | Section |
|------|---------|
| 110 | `EmergencyStopOnExit` — `atexit` mount stop |
| 117 | `CrashHandler` — `UnhandledExceptionFilter` mount stop |
| 146 | `CheckHardware()` |
| 353 | `ShowStartupDialog()` |
| 476 | `ApplyProfile()` |
| 515 | **`RunApp()` begins** |
| 1614 | Profile editor modal |
| 1963 | **The one capture-format path** (Phase 84) |
| 2220 | AutoStakkert handoff |
| 3130 | WebSocket command dispatch via `PostMessage` |
| **3320** | **The render loop** |
| 4194 | Cleanup and session summary |

---

## `src/core` — capture pipeline

| File | Lines | Owns |
|------|-------|------|
| [`FrameSlot.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/FrameSlot.h) | 47 | `FrameState` enum and `FrameSlot`. The `READING` state lives here |
| [`RingBuffer.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/RingBuffer.h) | 199 | Lock-free SPSC preview ring, CAS slot claims, skip-to-latest |
| [`WriteBuffer.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/WriteBuffer.h) / `.cpp` | 118 / 95 | Sequential SPSC write ring; never overwrites |
| [`CaptureThread.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/CaptureThread.h) / `.cpp` | 76 / 211 | RT-priority capture loop, `CaptureStats`, pipeline-wedge restart |
| [`CaptureManager.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/CaptureManager.h) / `.cpp` | 158 / 219 | Recording orchestration, writer thread, limits, pause, `SetOnComplete` |
| [`SERWriter.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/SERWriter.h) / `.cpp` | 113 / 199 | SER v3 header, frames, timestamp trailer, `_quality.txt` |
| [`Timestamp.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/Timestamp.h) | 51 | QPC microsecond timing |
| [`SessionLog.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/SessionLog.h) | 201 | Dated session log, mutex-serialised |

See [Capture Pipeline](Capture-Pipeline), [Recording and SER Format](Recording-and-SER-Format).

## `src/core` — devices

| File | Lines | Owns |
|------|-------|------|
| [`DeviceFactory.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/DeviceFactory.h) | 36 | **The compile-time hardware/emulator switch.** Include this, not individual device headers |
| [`IMount.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/IMount.h) | 122 | Abstract mount interface, `GuideDirection`, RA/Dec formatters |
| [`ASICamera.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/ASICamera.h) / `.cpp` | 83 / 160 | ZWO ASI SDK wrapper |
| `ASICamera2Stub.h` | 139 | SDK type stubs for emulator builds |
| [`CameraEmulator.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/CameraEmulator.h) | **984** | Synthetic frames: per-planet render, rotation, moons, defocus, noise, seeing, 1280×960 cap |
| [`EAFFocuser.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/EAFFocuser.h) / `.cpp` | 52 / 131 | ZWO EAF SDK wrapper |
| `EAF_focuser_stub.h` | 49 | SDK stubs |
| [`EAFEmulator.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/EAFEmulator.h) | 161 | Simulated motor at realistic step rate |
| [`NexStarMount.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/NexStarMount.h) / `.cpp` | 212 / **823** | NexStar serial, poll thread, watchdog, software-timed `PulseGuide`, `EmergencyStop` |
| [`ASCOMMount.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/ASCOMMount.h) | 792 | ASCOM `ITelescopeV3`. **Superseded**; all known CPWI drivers blacklisted |
| [`MountEmulator.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/MountEmulator.h) | 414 | Closed-loop tracking model, `GetPointingErrorArcsec()`, real NexStar rate curve |
| [`SerialPortScan.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/SerialPortScan.h) | 344 | `SERIALCOMM` enumeration plus NexStar echo probe; `busy` means CPWI |
| [`USBTrafficManager.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/USBTrafficManager.h) / `.cpp` | 60 / 56 | Poll throttling during capture |

See [Device Layer](Device-Layer), [Mount Control](Mount-Control).

## `src/core` — analysis

| File | Lines | Owns |
|------|-------|------|
| [`FrameAnalyzer.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/FrameAnalyzer.h) / `.cpp` | 143 / 333 | Analyzer thread, `AnalysisResult`, per-channel histograms, seeing score, centroid |
| [`Sharpness.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/Sharpness.h) | 200 | **The one** Bayer Laplacian variance definition, `SHARP_CEIL`, the recorded noise-floor finding |
| [`QuickLookStacker.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/QuickLookStacker.h) | 327 | Percentile gate, shift solving, scale-invariant gradient metric. Holds no pixel buffers |
| [`CalibrationFrames.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/CalibrationFrames.h) | 328 | Master dark and flat, per-Bayer-position flat normalisation |

See [Image Analysis](Image-Analysis).

## `src/core` — control loops

| File | Lines | Owns |
|------|-------|------|
| [`CentroidTracker.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/CentroidTracker.h) / `.cpp` | 355 / 581 | Five-stage acquisition, `GuideCalibration`, 2×2 pixel solve, cos(alt) rescale |
| [`AutofocusRoutine.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/AutofocusRoutine.h) | 789 | V-curve scan, `FitPeakWindow`, backlash approach, the regression revert |
| [`CordWrap.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/CordWrap.h) | 142 | Cumulative azimuth travel, GoTo-jump rejection |
| [`AlignGuide.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/AlignGuide.h) | 102 | Turns "point north" into the observer's own landmarks from one bearing |

See [Tracking and Guiding](Tracking-and-Guiding), [Autofocus](Autofocus).

## `src/core` — advisors and AI

| File | Lines | Owns |
|------|-------|------|
| [`ADCAssistant.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/ADCAssistant.h) | 180 | Edlen 1966 dispersion over the fitted filter's band, elevation-derived pressure |
| [`AutoExposureAssistant.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/AutoExposureAssistant.h) | 277 | I-controller, exposure-first, HCG floor at gain 182 |
| [`Readiness.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/Readiness.h) | 251 | `ReadyState`, verified vs manual items, silence-when-ready |
| [`AdvisoryLoop.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/AdvisoryLoop.h) | 237 | Unprompted advice. **No device references at all** |
| [`AIAssistant.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/AIAssistant.h) | 269 | Ollama WinHTTP NDJSON streaming client |
| [`AIContextBuilder.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/AIContextBuilder.h) | 337 | Six context blocks, `AISessionState`, help-file cache |
| [`ObjectKnowledgeDB.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/ObjectKnowledgeDB.h) | 277 | SQLite via runtime `LoadLibrary`; returns `""` when absent |
| [`AutoStakkertRunner.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/AutoStakkertRunner.h) | 382 | AS!4 headless CLI handoff. CLI and in-app launch both verified 2026-10-18 |
| [`GuideSolve.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/GuideSolve.h) | 151 | Pixel offset → guide pulses. The one solve, shared by the guide loop and click-to-recentre |
| [`MountProxy.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/MountProxy.h) | 208 | Phase 46. One mount pointer that never goes stale: holders keep the proxy for the session while the backend swaps underneath, so a reconnect can pick a different transport or port |
| [`HardwareLink.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/HardwareLink.h) | 134 | Phase 46. What the [HW] button shows, what a press does, and what it says afterwards — the decisions, kept out of the button |
| [`TextConv.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/TextConv.h) | 71 | UTF-8 ↔ UTF-16, sized from the data rather than a fixed buffer |
| [`ADCAlignment.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/ADCAlignment.h) | 144 | Per-channel centroid displacement — measures what a dispersion corrector has left behind |
| [`SkyConditions.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/SkyConditions.h) | 211 | Airmass, resolution cost and the imaging window, from altitude |

See [Advisors](Advisors), [AI Assistant](AI-Assistant).

## `src/core` — configuration

| File | Lines | Owns |
|------|-------|------|
| [`RigConfig.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/RigConfig.h) | 471 | **Single source of truth** for site, optics, filter, geometry, switches, last session state. Singleton |
| [`SiteLocator.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/SiteLocator.h) | 183 | Windows Location via `std::async`; `Denied` is a clean outcome |
| [`SkyMath.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/SkyMath.h) | 98 | **One** copy of alt/az conversion, `NormDeg360`, `ShortestTurnDeg` |

See [Configuration Reference](Configuration-Reference).

---

## `src/gui`

| File | Lines | Owns |
|------|-------|------|
| [`Window.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/Window.h) / `.cpp` | 243 / 754 | Win32 window, D2D render target on the **preview child**, message pump, `Run(fn)` |
| [`PreviewRenderer.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/PreviewRenderer.h) / `.cpp` | 581 / **1,666** | Frame display, debayer, zoom, every overlay, FX, stretch. **Owns the pixel buffers** |
| [`ControlPanel.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/ControlPanel.h) / `.cpp` | 851 / **2,839** | `WS_CHILD` scrollable panel, nine collapsible sections, Win32 + GDI hybrid |
| [`ToolbarStrip.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/ToolbarStrip.h) | 566 | Three button groups, `UpdateStates` anti-flash cache |
| [`StatusBar.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/StatusBar.h) | 272 | D2D bottom strip: FPS, drops, temps, resolution, EAF, mount, profile |
| [`Theme.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/Theme.h) | 169 | Palette tokens, mode-aware GDI cache, night-vision transform |
| [`GoToPanel.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/GoToPanel.h) | **1,890** | Catalog list, GoTo, manual coordinates, plate solve → sync, tonight's planner |
| [`SkyMapWindow.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/SkyMapWindow.h) | **1,147** | Zenith-centred polar projection, progressive reveal, click-to-confirm, horizon floor |
| [`MountController.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/MountController.h) | 910 | Floating always-on-top jog panel, rate selector, STOP, ★ ALIGN |
| [`MotionMonitor.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/MotionMonitor.h) | 255 | Ten-second motion trail; CAMERA or MOUNT source, labelled |
| [`AIChatPanel.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/AIChatPanel.h) | 448 | Streaming chat UI |
| [`ObjectInfoPanel.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/ObjectInfoPanel.h) | 340 | Knowledge-base content for the selected target |
| [`HelpWindow.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/HelpWindow.h) | 399 | `?` floating help with live search filter |
| [`NotificationBar.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/NotificationBar.h) | 297 | Transient info/warn/error strip, 8 s auto-dismiss, `→ Open folder` |
| [`ReadinessDialog.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/ReadinessDialog.h) | 222 | Readiness list, 2 s live refresh, "Explain this", "Continue" |
| [`RigSetupDialog.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/RigSetupDialog.h) | 366 | Observer bearing, keep-out wedge, wrap limit, filter picker, drawn as a plan |
| [`SiteDialog.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/SiteDialog.h) | 185 | Manual site entry |
| [`AlignHelperDialog.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/AlignHelperDialog.h) | 178 | The "north is behind you" helper |
| [`ObjectPanel.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/ObjectPanel.h) | 808 | Phase 92. The education panel — figures, derived sentences, the scale diagram, and the prose. `Shift+I` |
| [`WorkspaceLayout.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/WorkspaceLayout.h) | 840 | Phase 42. Window placement per monitor count, matched on device interface path; `Ctrl+Shift+End` saves, `Ctrl+Shift+Home` rescues |
| [`ShareView.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/ShareView.h) | 585 | Phase 89. The image alone on a second screen, for people who are not you. `Shift+V` |
| [`StatusWindow.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/StatusWindow.h) | 569 | Capture health and free space on every drive, as its own window. `Shift+D` |
| [`ADCAlignWindow.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/ADCAlignWindow.h) | 431 | Phase 35. Dispersion residual as a dial. Dark unless *ADC fitted* is ticked on the READY screen, because there is no corrector in the train. `Shift+A` |
| [`HistogramWindow.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/HistogramWindow.h) | 311 | The histogram as its own window. `Shift+H` |
| [`SharpnessWindow.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/SharpnessWindow.h) | 283 | The sharpness trace as its own window. `Shift+Q` |
| [`TemperatureWindow.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/TemperatureWindow.h) | 268 | Camera and focuser temperature over time |
| [`HistogramDraw.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/HistogramDraw.h) | 325 | The shared painter behind both the panel section and the window — one histogram, drawn in two places |
| [`SharpnessDraw.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/SharpnessDraw.h) | 127 | Shared sharpness painter, same reason |
| [`TemperatureDraw.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/TemperatureDraw.h) | 123 | Shared temperature painter, same reason |
| [`ConfirmToast.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/ConfirmToast.h) | 182 | A large, self-dismissing confirmation for actions whose whole point is to change a file — read from across a room, at night |

See [Rendering and UI](Rendering-and-UI).

---

## `src/gpu`, `src/net`, `src/sky`, `src/profiles`

| File | Lines | Owns |
|------|-------|------|
| [`gpu/GPUDebayer.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gpu/GPUDebayer.h) | 382 | D3D11 `cs_5_0` RGGB compute shader, DXGI→D2D interop, nullptr fallback |
| [`net/WebSocketServer.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/net/WebSocketServer.h) / `.cpp` | 146 / 389 | Winsock2 sockets, self-contained SHA-1, loopback default, token required (P5.6) |
| [`net/WsProtocol.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/net/WsProtocol.h) | 370 | RFC 6455 frames, the upgrade handshake and the token check, with no sockets, so they can be tested and fuzzed |
| [`net/APEXTelemetry.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/net/APEXTelemetry.h) / `.cpp` | 137 / 166 | Flat telemetry struct, `ToJSON()` via `ostringstream`, command parse |
| [`sky/SkyObject.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/sky/SkyObject.h) | 642 | VSOP87 planets, 110 Messier, NGC subset, bright stars, Julian date helpers |
| [`sky/ASTAPSolver.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/sky/ASTAPSolver.h) | 334 | BMP export, ASTAP invocation with FOV hint, `.ini` parse, threaded |
| [`sky/PointingModel.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/sky/PointingModel.h) | 284 | CSV history, inverse-distance-squared `GetCorrection()`. **Reports, does not apply** |
| [`profiles/TargetProfile.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/profiles/TargetProfile.h) | 376 | Profile struct, defaults, hardware rails, `GAIN_HCG_THRESHOLD` |
| [`profiles/ProfileManager.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/profiles/ProfileManager.h) | 220 | INI load/save via Win32, no dependencies |

---

## What compiles

From `CMakeLists.txt`:

```cmake
src/main.cpp
${RADUK_RC_GENERATED}      # configure_file'd from src/RadukCapture.rc.in
${DEVICE_SOURCES}          # empty in emulator mode
src/core/CaptureThread.cpp  src/core/CaptureManager.cpp
src/core/SERWriter.cpp      src/core/WriteBuffer.cpp
src/core/FrameAnalyzer.cpp  src/core/CentroidTracker.cpp
src/core/USBTrafficManager.cpp
src/gui/Window.cpp          src/gui/PreviewRenderer.cpp
src/gui/ControlPanel.cpp
src/net/WebSocketServer.cpp src/net/APEXTelemetry.cpp
```

`DEVICE_SOURCES` in hardware mode adds `ASICamera.cpp`, `EAFFocuser.cpp`,
`NexStarMount.cpp`.

---

## Non-source assets

| Path | Contents |
|------|----------|
| [`RadukCapture-Architecture.md`](https://github.com/Maligzar/Raduk-Capture/blob/master/RadukCapture-Architecture.md) | 92 KB architecture and build plan, phase tables |
| [`RadukCapture-Help.md`](https://github.com/Maligzar/Raduk-Capture/blob/master/RadukCapture-Help.md) | 90 KB user reference. **Also the AI assistant's app knowledge** |
| [`RadukCaptureDashboard.html`](https://github.com/Maligzar/Raduk-Capture/blob/master/RadukCaptureDashboard.html) | 286 KB single-file browser dashboard |
| `assets/RadukCapture.ico`, `.rc` | Application icon and resources |
| `data/object_knowledge.db` | 2.4 MB SQLite knowledge base |
| `data/pointing_model.csv` | Pointing history |
| `profiles/*.ini` | Eight target profiles |
| `tools/build_knowledge_db.py` | 37 KB — builds the knowledge DB |
| `external/ASI_SDK`, `external/EAF_SDK` | Vendored ZWO SDKs — ZWO's property under their own terms |
| `docs/` | Code reviews, security review, tracking findings, phase plans, clear-night checklist |
| `wiki/` | **This wiki's source** |

---

## Where to look for a behaviour

| "Where does it…" | File |
|------------------|------|
| …decide to drop a frame | `RingBuffer::AcquireWriteSlot` |
| …decide to drop a *recorded* frame | `WriteBuffer::Push` returning false → `CaptureStatus::framesDropped` |
| …gate on seeing | The render loop, ~`main.cpp:3374` — not in `CaptureManager` |
| …set the sharpness scale | `SHARP_CEIL` in `Sharpness.h` |
| …decide a pulse direction | The sign of `tE`/`tN` in `CentroidTracker::Run` |
| …refuse to acquire, and why | `CentroidTracker::StartAutoAcquire`, `CentroidBlockReason` |
| …revert a bad focus | `AutofocusRoutine::ScanThread`, the `REGRESSION_FRAC` block |
| …choose the ADC band | `ADCAssistant::Compute`, reading `RigConfig::filterLow/HighNm` |
| …stop the mount on a crash | `CrashHandler` / `EmergencyStopOnExit` in `main.cpp` |
| …change capture format | The one shared path at `main.cpp:1963` |
| …decide the COM port | `SerialPortScan` |
| …restore last session | `RigConfig::HasLastCamera()` / `HasLastFormat()` |

---

**Next:** [Architecture Overview](Architecture-Overview) · [Roadmap and Phase History](Roadmap-and-Phase-History) · [Glossary](Glossary)
