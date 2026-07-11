# RADUKCAPTURE — Architecture & Build Plan
## Custom Planetary Capture Software for ASI678MC + EAF Focuser + Mount Control
### Version 1.0 · March 2026 — Production Release

---

## 1. PROJECT OVERVIEW

**RadukCapture** is a high-performance planetary imaging capture application built in C++ for Windows, it runs on a Windows PC, talks directly to the ZWO ASI678MC, ZWO EAF focuser, and the Celestron 8SE mount via the NexStar serial protocol (no CPWI, no ASCOM), and integrates with the APEX Astrophotography Command Center dashboard.

Mount control is entirely NexStar direct over COM3 — no third-party software required. The full GoTo + sky map pipeline is operational. Profile management includes a full modal EDIT dialog. The control panel is a `WS_CHILD` window, scrollable, with all sections collapsible via colored circle headers matching the SharpCap Pro aesthetic.

### Window Architecture (v1.0)

RadukCapture uses a three-tier child window layout. All elements are children of the main window — nothing bleeds or overlaps.

```
Main Window  (WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, dark bg brush RGB(7,13,22))
├── ToolbarStrip     WS_CHILD  x=0      y=0       w=clientW-560  h=30
├── Preview Child    WS_CHILD  x=0      y=30      w=clientW-580  h=clientH-30
│     └── D2D HwndRenderTarget  — renders live preview, reticle, HUD, status bar
├── ControlPanel     WS_CHILD  x=clientW-560  y=0  w=560  h=clientH  WS_VSCROLL
└── [Gap strip]      Filled by main window dark background brush
```

**Key architectural property:** D2D is bound to the preview child HWND, not the main window. The OS clips D2D output to the child's bounds automatically — no manual `PushAxisAlignedClip`, no offset transforms, no gap-fill hacks. The preview is a true bounded box.

### UI — Elite Raduk / SharpCap-Style (Complete)

- **Colored circle section headers** — IMAGE, PROFILE, CAMERA, FOCUSER, TELESCOPE, CAPTURE, NOTES, STATUS, HISTOGRAM, TEMPERATURE
- **All sections collapsible** — Win32 `SS_OWNERDRAW | SS_NOTIFY` headers for Win32 sections; GDI headers with `WM_LBUTTONDOWN` hit testing for STATUS/HISTOGRAM/TEMPERATURE
- **Profile cache** — `m_cachedProfileNames/Index` survives `RebuildControls` so combo is never empty after collapse/expand
- **Scrollbar layout** — `w = clientW - 10 - GetSystemMetrics(SM_CXVSCROLL) - 2` prevents controls drawing behind scrollbar
- **Toolbar anti-flash** — `UpdateStates` state cache prevents 38 FPS `SetWindowTextW` calls

### Panel Section Colour Assignments

| Section | Colour | Hex |
|---------|--------|-----|
| CAMERA | Cyan | `#00E5FF` |
| FOCUSER | Amber | `#FFB900` |
| TELESCOPE | Green | `#4DFF91` |
| CAPTURE | Red | `#FF3B5C` |
| STATUS + HISTOGRAM | Cyan | `#00E5FF` |
| NOTES | Dim | `#8CAFBE` |

### Why Build This

RadukCapture exists because the astronomy stack — capture, mount control, plate solving, weather-aware scoring, and eventually AI-assisted operation — should be one coherent system, not five separate concerns reconciled by hand every session.

Unified state across the session. Pixels, mount position, plate solves, and quality scores live in one process, so a capture decision — start recording, hold for better seeing, correct pointing — is made from a single live picture of the system, not reassembled from logs afterward.
Direct hardware integration. Native ASI SDK access and direct NexStar serial control give the pipeline the low-latency, zero-copy path the ASI678MC's ~1.5 GB/s raw stream and the centroid-tracking loop actually require.
Instrument-specific tuning. Exposure logic, quality gating, and gust-aware scoring are built around this telescope (8SE, 2032mm f/10, single-arm mount) and this site (Bortle 4, Anthem AZ, 0.203"/px plate scale) — not generalized for an average setup.
A foundation for capabilities that don't exist yet. Pointing-model learning from repeated plate solves, hybrid SGP4/JPL Horizons satellite tracking, and an AI assistant with growing autonomy over the capture loop all require the application's internals to be exposed and addressable from the start, not bolted on later.
Full ownership of the data. Session logs, quality metrics, and pointing corrections are captured in a schema designed for how the data will actually be used — archival, AI context building, dashboard scoring — rather than adapted from someone else's log format.
A long-term personal instrument. This is infrastructure meant to be extended indefinitely as equipment, targets, and ambitions change, the same way an amateur astronomer extends their own optics or mount over decades.

### Two-Machine Setup

RadukCapture is designed around a two-desktop configuration:

| Machine | Role | Location |
|---------|------|----------|
| Imaging PC — runs RadukCapture, connected to all hardware | Outside with telescope |
| Monitoring/streaming desktop — runs APEX dashboard, OBS Studio | Inside the house |

Both machines are on the same home network (LAN via CAT5 or WiFi). They communicate via:
- **WebSocket** (Phase 4): real-time telemetry — FPS, seeing score, mount position, session events. Tiny payloads, works on WiFi.
- **RTSP stream** (Phase 7): H.264-encoded preview stream from StarMachine's GPU to Elminster's OBS. ~5-10 Mbps, works on WiFi.
- **File transfer**: stacked images only (TIFF/PNG, 10-50MB) — never raw SER files. Raw capture stays on StarMachine; AutoStakkert! runs on StarMachine; finished stacks transfer to Elminster for post-processing and archiving.

### Workflow

```
StarMachine (outside)                    Elminster (inside)
─────────────────────────────────        ──────────────────────────────
Telescope + camera + EAF + mount         APEX Dashboard (weather/scoring)
RadukCapture:                            OBS Studio:
  - Live preview                           - RTSP feed from StarMachine
  - Hardware control                       - Telemetry overlay (web panel)
  - Capture to SER file                    - Seeing monitor graph
  - Quality scoring                        - Webcam (optional)
  - Seeing monitor                         - Stream to Twitch/YouTube
  - Session logging
  - RTSP stream server ─────────────────▶  OBS Media Source
  - WebSocket telemetry ────────────────▶  Companion view
AutoStakkert! (stacking)
Export stacked TIFF ─────────────────────▶ Post-processing (PixInsight etc.)
                                           Final image archive
```

### Target Hardware

| Component | Spec |
|-----------|------|
| Imaging Machine | Windows, Intel i9, discrete GPU |
| Monitoring Machine | Windows desktop, high-powered GPU, inside |
| Camera | ZWO ASI678MC |
| Sensor | Sony IMX678 — 3840x2160, 1/1.8", 2um pixels |
| Interface | USB 3.0 |
| Bit Depth | 8-bit / 12-bit (stored as 16-bit) |
| Bayer Pattern | RGGB |
| Max FPS | ~47 fps @ full res RAW8, ~24 fps @ RAW16 |
| Peak Throughput | ~1.5 GB/s (RAW16 full frame) |
| Unity Gain | ~100 (12-bit mode) |
| Focuser | ZWO EAF (Electronic Automatic Focuser) |
| EAF Interface | USB 2.0 |
| EAF Range | 0-600,000 steps (configurable max position) |
| EAF Safe Test Position | 50,000 steps (default for development/testing) |
| EAF Features | Absolute positioning, temperature sensor, backlash compensation, reverse direction |
| Mount | Celestron NexStar 8SE (alt-az) with StarSense AutoAlign |
| Mount Alignment | StarSense AutoAlign — automatic sky alignment (~3 min) |
| Mount Interface | USB to Serial (StarSense hand control, NexStar-compatible) |
| Mount Protocol | Celestron NexStar serial command set (RS-232 over USB) |
| Mount COM Port | COM3 (StarMachine) |
| Tracking Modes | Sidereal, Solar, Lunar, Off |
| GoTo | COMPLETE — catalog, GoTo, plate-solve alignment, ASTAP |
| Slew Rates | 1-9 (1x to 3 deg/s). Jog buttons use rate 9 for physical movement. |
| CPWI | Not used — NexStar direct eliminates CPWI dependency entirely |

---

## 2. PHASED BUILD PLAN

### Phase 1 — Camera Connection + Live Preview [COMPLETE]
- ASI SDK integration (detect, open, configure camera)
- EAF SDK integration (detect, open, control focuser)
- NexStar mount connection via serial (detect, connect, read position)
- Live video streaming with real-time preview window (Direct2D, GPU-accelerated)
- Basic camera controls: gain, exposure, gamma, WB, ROI, binning, USB bandwidth
- Focuser controls: position readout, GoTo (editable text field), coarse/fine step buttons, temperature display, STOP button
- Mount controls: live RA/DEC readout, tracking mode toggle, jog buttons (hold-to-slew, rate 9), STOP button
- Safety: ESC key = emergency stop for both mount and EAF; STOP buttons on panel
- Jog mode: hold-to-slew (WM_LBUTTONDOWN start, WM_LBUTTONUP stop, mouse capture)
- RingBuffer: lock-free SPSC, READY->READING->FREE state machine (prevents use-after-free)
- Layout: preview owns left region, control panel anchors to right edge (no overlap)
- Device detection: hardware check at launch; if any device missing, ask user to launch emulator mode
- Emulator: full software emulators for camera (synthetic frames with planet disk), EAF (motor simulation), mount (RA/DEC drift + slew simulation). Compile-time flag: -DRADUK_EMULATOR=ON
- Device policy: all real or all emulated — no mixing permitted in Phase 1 milestones
- **Deliverable:** Interactive live preview with full camera, EAF, and mount control

#### Milestone Status
- [x] Milestone 1: Camera + Focuser + Mount Talk
- [x] Milestone 2: Frames Flow
- [x] Milestone 3: Picture Appears
- [x] Milestone 4: Controls Work

### Milestone 5 — Battle Ready [COMPLETE]
- Status bar: FPS, dropped frames, sensor temp, resolution, EAF position + temp, mount RA/DEC + tracking mode
- Full-screen preview mode (F11 — hides all controls, minimal HUD overlay)
- Reticle overlay (R key toggle)
- Zoom levels: fit, 1:1, 2x, 4x (Z key cycles)
- Mount jog rate selector 1-9 in mount panel (live, affects all jog buttons)
- Profile load/save: Jupiter, Saturn, Mars, Venus, Moon, DSO (saves gain/exposure/gamma/WB/ROI/EAF position)
- Keyboard shortcuts: R=reticle, Z=zoom cycle, F=EAF fine step, Shift+F=coarse, T=tracking toggle, F11=fullscreen, ESC=emergency stop
- Session log: timestamped action history written to file alongside captures
- **Deliverable:** Feature-complete Phase 1 — field-ready for live planetary sessions

### Phase 2 — Capture + Infrastructure

#### Milestone 6: SER Capture [COMPLETE]

**SERWriter** — SER v3 format: 178-byte header (`LUCAM-RECORDER`, BAYER_RGGB, observer/instrument/telescope), raw frame data, per-frame UTC timestamp trailer (Windows FILETIME, epoch offset 504911232000000000 ticks to convert from 1601 to 0001 base), frame count patched into header at `Close()`. Header-only, no external dependencies. Compatible with AutoStakkert!, Registax, Siril, PIPP.

**WriteBuffer** — Second lock-free ring buffer dedicated to the writer thread. Unlike the preview RingBuffer (skip-to-latest), this reads sequentially and never overwrites unread frames — if full it reports an overrun rather than dropping silently. 512 slots at 1280x960 RAW8 ≈ 600 MB. Absorbs ~10 seconds of disk stall before overrun. Fully decouples the capture thread from disk I/O latency.

**CaptureManager** — Orchestrates recording. Reads config (prefix, output dir, frame/time limit), builds filename with timestamp and auto-incrementing run number, allocates write buffer, opens SER file, starts writer thread (above-normal priority, tight drain loop). `SubmitFrame()` called from render loop — copies frame into WriteBuffer before rendering begins so frame is never missed due to render jitter. `StopRecording()` drains remaining write buffer fully before closing SER file.

**Capture panel section** — Big `[REC]`/`[STOP]` button, status line (IDLE / REC MM:SS), frame count vs limit + drops, RAM buffer display (always visible: used/total slots + free), disk space display (always visible: GB free / GB total via `GetDiskFreeSpaceExW`), editable output directory with `[...]` folder browser (`SHBrowseForFolderW`), file prefix, frame/time radio buttons with limit edit boxes.

**Panel layout** — All six sections (VIEW / PROFILE / CAMERA / FOCUSER / MOUNT / CAPTURE) in a WS_CHILD panel fixed at PANEL_WIDTH=520, scrollable via system scrollbar. Panel is created with WS_CLIPCHILDREN | WS_CLIPSIBLINGS; WM_PAINT clips to below m_contentBottom to prevent D2D flashing over controls. Key decisions: camera sliders use full-width layout with label/value rows; two-column WB row; button heights 20px; focuser position display in large amber Space Mono.

**Render loop order (Phase 2):**
```
frame = ring.GetLatestFrame()
capturer.SubmitFrame(frame)      // copy to WriteBuffer if recording
preview.RenderFrame(...)          // BeginDraw, preview + HUD
statusBar.Draw(...)               // same D2D frame
preview.EndFrame()                // EndDraw, present
ring.ReleaseRead(frame)           // return slot to FREE
```

**File naming:** `{prefix}_{YYYYMMDD_HHMMSS}_{W}x{H}_{bits}bit_{NNNN}.ser`  
Example: `Jupiter_20260314_213045_1280x960_8bit_0001.ser`

#### Milestone 7: USB Traffic Manager [COMPLETE]

Header-only `USBTrafficManager`. During capture the ASI camera owns the USB 3.0 bus. EAF and mount polling are throttled to reduce bus contention and prevent USB packet collisions that cause dropped frames.

- **Normal mode:** EAF and mount polled every 500ms (same as readout timer)
- **Capture mode:** EAF polled every 10s, mount every 2s, camera unrestricted
- `SetCapturing(bool)` called from the `doRecord` lambda when recording starts/stops — also wired to emergency stop
- `ShouldPollFocuser()` / `ShouldPollMount()` gate the readout section of the render loop; `ShouldPollCamera()` always returns true
- State and timing tracked internally via `std::chrono::steady_clock`
- Session log records throttle mode changes: `"USB capture mode — EAF:10s  Mount:2s"`

### Mount Architecture — NexStar Direct [CURRENT]

CPWI is not used. RadukCapture talks to the mount directly via NexStar serial on COM3. The `IMount` abstract interface is retained for emulator compatibility; `ASCOMMount.h` exists but all known CPWI/simulator/DeviceHub ASCOM drivers are blacklisted. `NexStarMount` is the primary hardware path.

```
StarSense HC
    │  USB-serial (COM3)
    ▼
RadukCapture (NexStar direct) ── position, jog, GoTo, PulseGuide
```

CPWI coexistence mode (handoff): if CPWI is running and holds COM3, RadukCapture can request a handoff. CPWI disconnects, RC takes COM3 directly, and a RELEASE command gives it back. This path is not normally used — RC runs standalone.

#### Milestone 8: ASCOM Mount Layer [SUPERSEDED]

The ASCOM path was built and tested but superseded by NexStar direct. All known CPWI ASCOM drivers (CPWI Telescope, DeviceHub, ASIMount, JustAHub, all simulators) are blacklisted in `ASCOMMount::FindCPWIDriver()`. NexStar direct on COM3 is faster, has no COM/registry dependencies, and supports all required commands including PulseGuide (`Mg/Me/Ms/Mw`). `IMount` interface is retained; `ASCOMMount.h` is kept for potential future use.

#### Milestone 9: Frame Analyzer [COMPLETE]

Header-only `FrameAnalyzer` running on a dedicated below-normal-priority thread. Non-blocking submit — if the thread is busy with the previous frame, the new frame is skipped (always want latest result, never queue up work).

**Per-frame analysis:**
- **Histogram:** 256-bin luminance from RAW8 Bayer data (downsampled 2x in both axes for speed — ~320x240 samples at 1280x960). Normalised to peak bin for display.
- **Sharpness:** Laplacian variance on a 128x128 centre crop (256x256 native pixels). Kernel `[0,-1,0; -1,4,-1; 0,-1,0]` applied to every other pixel. Variance normalised against a 8000-unit ceiling → 0-100%.
- **Clipping:** Fraction of pixels at bin 255. Shown as warning when > 0.5%.
- **Mean brightness:** Used for future auto-exposure assistant.
- **Seeing score:** 20-sample rolling window (≈10 seconds at 2Hz update) of sharpness readings, averaged and mapped 0-100% → 1-10 scale.

**D2D overlay** (`DrawAnalysisOverlay`) drawn in the same BeginDraw/EndDraw frame between `RenderFrame` and `StatusBar.Draw`:
- 128×48px histogram strip, bottom-left of preview. Bars coloured: shadows = cyan, highlights = amber, saturated = red.
- `CLIP x.x%` warning in red above histogram when clipping detected.
- `SEEING x.x/10` below histogram in green/amber/red based on score.
- `SHARP xx%` in dim text.

**Thread safety:** Frame data copied under `m_frameMutex` on submit; result published under `m_resultMutex` and read via `HasResult()` / `GetResult()` from the render loop.

### Phase 3 — Analysis + GPU Pipeline [COMPLETE]

#### Milestone 10: GPU Debayer + Frame Bar + Quality Gate [COMPLETE]

- D3D11 compute shader (cs_5_0) RGGB→RGB debayer — replaces CPU debayer for preview. <1ms on StarMachine GPU vs ~15ms CPU. CPU fallback retained.
- Frame control bar: [CENTROID] [C] COLOR [+] MAP [I] GOTO [+] RETICLE buttons at bottom of preview
- ROI rubber-band selection: left-click drag on preview → crops capture ROI
- Quality gate: MIN SEEING threshold + GATE checkbox in capture panel. When enabled, `capturer.SubmitFrame()` is gated on `lastAnalysis.seeingScore >= threshold`. `SetGateActive(bool)` provides visual feedback (amber = holding, green = passing).
- Autofocus routine: `AutofocusRoutine` V-curve scan from current EAF position. AUTO FOCUS / ABORT AF owner-drawn button in focuser section (cyan = idle, amber = running). Completion callback updates AF status label and session log.

### Phase 4 — APEX + Session Intelligence [COMPLETE]

#### APEX Dashboard — RadukCapture Tab [COMPLETE]

Full monitoring tab on Elminster. Three-column layout: connection + telemetry | live preview canvas + histogram | capture control + seeing monitor. WebSocket bridge (`apex_bridge.py`) connects to RadukCapture on StarMachine at `ws://starmachine:8765`.

**Remaining Phase 4 work (deferred):**
- `apex_bridge.py` update to forward RC telemetry (emit `rc_running` instead of `fc_running`)
- Weather-aware capture suggestions from APEX scoring

#### Milestone 12: WebSocket Telemetry Server [COMPLETE]

**`net/WebSocketServer.h`** — minimal RFC 6455 WebSocket server on Winsock2. No external dependencies. Broadcasts JSON telemetry every 500ms. Accepts `start_capture`/`stop_capture`/`load_profile`/`move_eaf`/`goto_target` commands. Session events accumulate between broadcasts and are flushed as a JSON array each tick.

**`net/APEXTelemetry.h`** — flat struct matching APEX dashboard fields. `ToJSON()` uses `std::ostringstream` — no external JSON library.

### Phase 5 — Planetary Centroid Tracking [COMPLETE]

Three-stage acquisition state machine: Off → Searching → Centering → Locked.

- `CentroidTracker`: single mutex guards all shared state via `GetSnapshot()`. `SetMount()` mutex-protected. PulseGuide fires `Mg/Me/Ms/Mw` as fire-and-forget (`SendCommandNoWait`) — eliminates 500ms serial block. Sub-10ms pulses rejected (noise gate). Min pulse gap enforced to prevent HC input buffer overflow.
- Plate scale: 206.265 × 2µm / 2032mm = **~0.203 arcsec/pixel** (ASI678MC + C8)
- Stage buttons live in frame bar. ESC → `tracker.Reset()`. Jog suspend: corrections pause during mount jog + 500ms settle.
- ROI crop on Stage 3 lock: 1280×960 centred on last known centroid, aligned to ASI constraints (width multiple of 8, height multiple of 2).

### Phase 6 — GoTo + Sky Map [COMPLETE]

#### Phase 6: GoTo Panel [COMPLETE]
- `src/gui/GoToPanel.h` — 110 Messier + 7 planets (VSOP87 ephemeris), altitude-sorted list, GoTo by object or manual RA/DEC, plate solving via ASTAP, sync to mount
- `G` key or `[I] GOTO` frame bar button toggles panel
- Hidden by default — prevents accidental slews during imaging
- Public `RequestGoTo(ra, dec, name)` entry point used by SkyMapWindow

#### Phase 6.1: Sky Map [COMPLETE]
- `src/gui/SkyMapWindow.h` — D2D alt-az polar projection
- Observer: Anthem AZ (33.8653°N, 112.1235°W), LST from UTC + longitude
- 100+ bright stars (mag ≤ 4.5), planets (coloured glowing dots), Messier (cyan)
- Zoom: mouse wheel 1×–32×; right-click resets. Pan: click-drag when zoomed.
- Progressive reveal: zoom 1× = mag<4.5, 2–4× = mag<5.5, 4–8× = mag<6.5, 8×+ = mag<8.5
- Mount crosshair (green) + FOV circle (0.13° = ASI678MC + C8)
- **Click-to-confirm:** clicking an object shows an inline D2D confirmation popup ("Slew to Planet? Alt 42°") with ✓ SLEW and ✗ CANCEL buttons — no immediate slew on first click
- **Horizon safety:** objects below 15° show "BELOW HORIZON" in red; SLEW button is not offered
- Click outside popup box or right-click dismisses without slewing
- `M` key or `[★ MAP]` frame bar button toggles

#### Phase 6.2: Planet Lock Hardening [COMPLETE]
- CentroidTracker fully rewritten with single-mutex snapshot pattern
- PulseGuide changed to fire-and-forget (no 500ms serial block)
- Mount poll interval 2s → 4s during tracking to reduce serial contention

#### Phase 6.3: FC-Style UI Redesign [COMPLETE]
- Elite Raduk dark palette: panel `RGB(7,13,22)`, headers `RGB(10,21,32)`, cyan/amber/green/red accents
- Section headers: 3px accent stripe, Barlow Condensed bold, accent-coloured text
- Focuser: large amber Space Mono position number, five-button coarse/fine rows, TEMP row
- Histogram: three R/G/B tinted rows on black background (FC style), seeing + sharpness row
- Status: HDD bar + FPS, BUF bar + LOST — painted below controls in WM_PAINT strip

### Phase 9/10 — Profile Editor + Panel Architecture [COMPLETE]

#### Profile EDIT Modal [COMPLETE]
- EDIT button added to PROFILE row alongside LOAD and SAVE (three equal 48px buttons)
- `SetProfileEditCallback` in `main.cpp`: creates a `WS_POPUP` modal window centred over the main window
- 11 labeled edit fields: Name, Gain, Exposure ms, Gamma, WB Red, WB Blue, USB BW%, Focus Position, Limit Frames, File Prefix, Notes
- `SetWindowSubclass` installs a proper WndProc on the dialog (`WM_COMMAND` via `BN_CLICKED`, `WM_CLOSE`, `VK_ESCAPE` all handled correctly)
- `IsDialogMessageW` in pump handles Tab/Enter/Esc at dialog level
- **Safety:** both main window (`window.Handle()`) AND panel (`controls.PanelHwnd()`) are `EnableWindow(FALSE)` for the modal duration — prevents `PanelWndProc`'s arrow key forwarding from issuing mount jog commands while dialog is open
- On SAVE: values clamped to valid ranges, `profiles.Save()` + `profiles.LoadAll()`, combo box refreshed
- On CANCEL or outside click: no changes written

#### Panel Architecture [COMPLETE]
- `PANEL_WIDTH = 520` — fixed client width
- Panel created as `WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VSCROLL`
- `WM_PAINT` clips rendering to below `m_contentBottom` via `CreateRectRgn` — prevents D2D flashing over child controls
- `WM_SIZE` guarded by `if (m_contentBottom < 1) break` — prevents bogus scroll range on first WM_SIZE before `RebuildControls` runs
- `RebuildControls` scroll setup falls back to main window height when `GetClientRect` returns 0 (window not yet laid out)
- Scrollbar shown only when content height exceeds panel height; scroll position always reset to 0 on rebuild
- `StatusCache` includes `bufferTotal` field (int, default 512) — used by `PaintStatus` BUF bar
- `UpdateStatus()` takes 7 parameters: fps, lost, bufPct, bufFrames, bufTotal, diskGB, recording
- `GateEnabled()` / `GateThreshold()` / `SetGateActive()` inline accessors for quality gate

---

## 3. ARCHITECTURE

### 3.1 Thread Model

The core challenge is sustaining ~1.5 GB/s from USB3 while simultaneously displaying preview, computing quality metrics, and writing to disk — all without dropping frames. The architecture uses a multi-threaded pipeline with lock-free ring buffers between stages.

```
STARMACHINE THREAD MODEL
========================

+----------------------------------------------------------+
|                    MAIN / GUI THREAD                      |
|  Win32 window, Direct2D preview, Control panel, HUD       |
|  Receives latest frame for display (~30 fps)              |
+----------------+-----------------------+------------------+
                 | (events)              | (display frames)
                 v                       ^
+--------------------+    +---------------------------------+
|   CAPTURE THREAD   |    |      RING BUFFER (lock-free)    |
|  (RT priority)     |--->|      64-128 frame slots         |
|                    |    |      FREE->WRITING->READY->      |
|  ASIGetVideoData() |    |      READING->FREE              |
|  Timestamps frames |    +----------+----------+-----------+
|  Monitors drops    |               |          |
+--------------------+               v          v
                            +----------+  +---------------+
+--------------------+      | WRITER   |  | ANALYZER      |
|   MOUNT THREAD     |      | THREAD   |  | THREAD        |
|   (~4 Hz polling)  |      |          |  |               |
|                    |      | SER file |  | Histogram     |
|   NexStar serial   |      | (async   |  | Sharpness     |
|   RA/DEC readout   |      |  drain   |  | Centroid      |
|   GoTo dispatch    |      |  from    |  | Quality sort  |
|   Guide pulses     |      |  RAM     |  +-------+-------+
+--------------------+      |  buffer) |          |
                            +----------+          v
+--------------------+                   +---------------+
|   STREAM THREAD    |                   | WEBSOCKET     |
|   (Phase 8 —       |                   | THREAD        |
|    far future,     |                   |               |
|    WiFi TBD)       |                   | Telemetry to  |
|   H.264 encode     |                   | Elminster     |
|   RTSP serve       |                   | every 500ms   |
|   ~30fps stream    |                   +---------------+
+--------------------+
```

### 3.2 Ring Buffer Design

The ring buffer is the central data structure. It must sustain 1.5 GB/s writes without blocking the capture thread.

State machine: FREE -> WRITING -> READY -> READING -> FREE

READING state is critical — without it, the GUI thread can receive a slot and have the capture thread overwrite it before rendering completes (use-after-free crash fixed in Milestone 4).

The writer (CaptureThread) skips READING slots rather than stomping them.

```cpp
enum class FrameState : uint8_t {
    FREE = 0,
    WRITING,
    READY,
    READING   // Added Milestone 4 — slot held by consumer during render
};

struct FrameSlot {
    uint8_t*                data;            // Pre-allocated frame buffer (VirtualAlloc)
    size_t                  bufferCapacity;
    size_t                  dataSize;
    uint64_t                timestamp;       // QPC microseconds
    uint64_t                frameNumber;
    uint32_t                width, height;
    uint8_t                 bitDepth;        // 8 or 16
    ASI_IMG_TYPE            imgType;
    std::atomic<FrameState> state;
};
```

### 3.3 Memory Strategy

At full resolution RAW16: 3840 x 2160 x 2 = ~16.6 MB per frame.
At planetary ROI (1280x960 RAW8): ~1.2 MB per frame.

Ring buffer: 64 slots × 16.6 MB = ~1 GB (full res RAW16) or 64 × 1.2 MB = ~77 MB (planetary ROI).
Write buffer: 512 slots × 1.2 MB = ~600 MB (planetary ROI RAW8).
Frame memory allocated with `VirtualAlloc` (page-aligned, avoids heap fragmentation).

---

## 4. KEY DESIGN DECISIONS

| Decision | Rationale |
|----------|-----------|
| Lock-free ring buffer | Prevents capture thread from ever blocking on mutex contention |
| READING state | Prevents use-after-free when GUI holds a slot during render |
| RAM-first write buffer | Decouples disk I/O latency from capture thread |
| NexStar direct (no ASCOM/CPWI) | Faster, no COM/registry dependencies, all commands available |
| WS_CHILD panel | Panel always docked, moves with main window, no separate title bar |
| **D2D in preview child HWND** | **Zero bleed: OS clips D2D to child bounds; no manual clip rects needed** |
| PANEL_WIDTH = 560 | Wide enough for SharpCap-style sections with margins |
| WS_CLIPCHILDREN on main window | OS skips painting child areas — no flicker |
| WM_SIZE guard (m_contentBottom < 1) | Prevents bogus scroll range on first WM_SIZE before RebuildControls |
| Modal dialog via SetWindowSubclass | Correct WM_COMMAND routing vs hand-rolled GetMessage loop |
| Disable panel during modal | Prevents PanelWndProc arrow key forwarding → accidental mount jog |
| Sky map click-to-confirm | Prevents accidental slews from fat-finger clicks on map |
| 15° horizon floor | Anthem AZ obstruction margin — SLEW not offered below this altitude |
| Fire-and-forget PulseGuide | Eliminates 500ms serial block per correction cycle |
| Quality gate (seeing threshold) | Only records frames meeting minimum seeing score |
| Profile cache in ControlPanel | `m_cachedProfileNames` survives RebuildControls — combo never empty |
| SS_NOTIFY on section headers | Required for SS_OWNERDRAW statics to receive WM_LBUTTONDOWN |
| UpdateStates state cache | Prevents 38 FPS SetWindowTextW → toolbar button flash |
| scrollW in layout constants | Reserves SM_CXVSCROLL pixels so controls don't draw behind scrollbar |

---

## 5. FILE STRUCTURE

```
D:\RadukCapture\src\
├── core\
│   ├── ASICamera.h / .cpp        Camera abstraction (ZWO SDK)
│   ├── CameraEmulator.h          Software camera for emulator builds
│   ├── EAFFocuser.h / .cpp       EAF focuser abstraction (ZWO SDK)
│   ├── EAFEmulator.h             Software focuser for emulator builds
│   ├── IMount.h                  Abstract mount interface
│   ├── NexStarMount.h / .cpp     NexStar direct serial (primary hardware path)
│   ├── ASCOMMount.h              ASCOM ITelescopeV3 (kept, blacklisted drivers)
│   ├── MountEmulator.h           Software mount for emulator builds
│   ├── DeviceFactory.h           Compile-time hardware/emulator selection
│   ├── RingBuffer.h              Lock-free SPSC ring buffer (preview)
│   ├── WriteBuffer.h             Lock-free SPSC ring buffer (disk write)
│   ├── FrameSlot.h               Frame data structure
│   ├── CaptureThread.h / .cpp    ASI SDK capture thread (RT priority)
│   ├── CaptureManager.h          Recording orchestration (SER + WriteBuffer)
│   ├── SERWriter.h               SER v3 file format writer
│   ├── FrameAnalyzer.h           Histogram + sharpness + seeing score
│   ├── CentroidTracker.h         Three-stage planet lock + PulseGuide
│   ├── AutofocusRoutine.h        V-curve EAF autofocus scan
│   ├── USBTrafficManager.h       EAF/mount poll throttle during capture
│   ├── SessionLog.h              Timestamped session log writer
│   └── Timestamp.h               QPC-based microsecond timestamps
├── gui\
│   ├── Window.h / .cpp           Main Win32 + D2D window
│   ├── ControlPanel.h            Panel class definition (PANEL_WIDTH=520)
│   ├── ControlPanel.cpp          Panel implementation (WS_CHILD, scrollable)
│   ├── PreviewRenderer.h / .cpp  D2D frame rendering, ROI, HUD, frame bar
│   ├── GoToPanel.h               GoTo dialog (catalog, ASTAP, NexStar GoTo)
│   ├── SkyMapWindow.h            D2D sky map (click-to-confirm, horizon safety)
│   ├── StatusBar.h               D2D status bar overlay
│   ├── GPUDebayer.h              D3D11 cs_5_0 RGGB debayer compute shader
│   └── Theme.h                   Elite Raduk colour constants
├── sky\
│   ├── SkyObject.h               Object catalog (planets VSOP87, Messier, stars)
│   └── ASTAPSolver.h             ASTAP plate solve interface
├── net\
│   ├── WebSocketServer.h         RFC 6455 server (Winsock2, no deps)
│   └── APEXTelemetry.h           Telemetry struct + ToJSON() + command parse
├── profiles\
│   ├── TargetProfile.h           Profile struct + default factory functions
│   └── ProfileManager.h          INI load/save via GetPrivateProfileString
└── main.cpp                      WinMain, RunApp, all callback wiring
```

---

## 6. KNOWN ISSUES / BUGS RESOLVED

| Issue | Resolution |
|-------|-----------|
| `bg_void` / `VOID` macro conflict with Windows headers | Renamed to `BG_VOID` |
| `void` keyword corruption from find-replace | Manual correction pass |
| ASCOM CPWI driver broken / simulator blacklist needed | All known bad drivers blacklisted in `FindCPWIDriver()` |
| WM_SIZE fires before RebuildControls → bogus scroll range | Guard: `if (m_contentBottom < 1) break` |
| GetClientRect returns 0 during Create() → scroll page=0 | Fallback to main window height, then 800px |
| Modal dialog freeze (WM_COMMAND not received in raw GetMessage loop) | Replaced with SetWindowSubclass + IsDialogMessageW pump |
| Accidental mount jog during profile editor modal | Panel also disabled (EnableWindow FALSE) during modal |
| Sky map click fires immediate GoTo | Replaced with click-to-confirm popup; click outside cancels |
| Objects below horizon slewed to accidentally | SLEW button not offered below 15° altitude |
| `min`/`max` unresolved with NOMINMAX defined | Added `#include <algorithm>`, use `std::min`/`std::max` |
| `StatusCache` missing `bufferTotal` field | Added `int bufferTotal = 512` to struct |
| `UpdateStatus()` signature mismatch (6 vs 7 args) | Added `bufTotal` parameter |
| D2D bleeds over control panel | Moved D2D to preview child HWND — OS clips automatically |
| Reticle off-center / clipped by toolbar transform | No transform needed with child window; y=0 = below toolbar |
| Toolbar buttons flash at 38 FPS | `UpdateStates` state cache — only repaints on actual state change |
| SS_OWNERDRAW headers not receiving clicks | Added `SS_NOTIFY` flag — required for mouse message pass-through |
| Profile combo empty after collapse/expand | `m_cachedProfileNames` repopulated at end of every `RebuildControls` |
| Controls draw behind scrollbar | Layout `w = clientW - 10 - SM_CXVSCROLL - 2` reserves scrollbar width |
| Histogram bin 0 spike (dark current noise) | Skip bins 0-1 in bar drawing loop |
| GDI sections (STATUS/HISTOGRAM) duplicated on scroll | `PaintAll` now uses `m_contentBottom - m_scrollY` for screen coords |
| `DrawSectionHeader` not visible in `PaintAll` | Moved `PaintAll` definition to after anonymous namespace |

---

## 7. PROFILE FORMAT

Profiles stored as INI files in `D:\RadukCapture\profiles\`. Eight built-in profiles: `jupiter.ini`, `saturn.ini`, `mars.ini`, `venus.ini`, `moon.ini`, `dso.ini`, `star.ini`, `planet_focus.ini`. If a file doesn't exist, defaults are written on first launch.

All fields editable via the PROFILE → EDIT modal dialog (11 fields: Name, Gain, Exposure ms, Gamma, WB Red, WB Blue, USB BW%, Focus Position, Limit Frames, File Prefix, Notes).

```ini
[profile]
name = Jupiter

[camera]
gain = 100
exposure_ms = 8.000
gamma = 50
wb_red = 52
wb_blue = 47
usb_bandwidth = 80
high_speed = true

[roi]
width = 1280
height = 960
binning = 1
img_type = 0

[focuser]
position = 50000
step_coarse = 100
step_fine = 10

[mount]
tracking_mode = alt_az
slew_rate = 9
goto_target = jupiter

[capture]
format = SER
limit_type = frames
limit_value = 3000
file_prefix = Jupiter

[notes]
text =
```

---

## 8. DEPENDENCIES

| Library | Purpose | Phase | Source |
|---------|---------|-------|--------|
| ZWO ASI SDK | Camera control | 1 | zwoastro.com |
| ZWO EAF SDK | Focuser control | 1 | zwoastro.com |
| Direct2D / DirectWrite | Preview rendering | 1 | Windows SDK |
| Direct3D 11 | GPU debayer compute shader | 3 | Windows SDK |
| nlohmann/json | Profile files, WS protocol | 2 | vcpkg |
| IXWebSocket | WebSocket server | 4 | vcpkg |
| ASTAP | Plate solving (local, fast, free) | 6 | han.astro.uni-bonn.de/astap |
| VSOP87 | Planetary ephemeris for sky map | 6 | public domain |
| Yale BSC | Star catalog for sky map (~9000 stars) | 6 | public domain |
| Media Foundation / NVENC | H.264 hardware encode | 7 | Windows SDK / NVIDIA |
| librtsp or similar | RTSP server | 7 | vcpkg |

---

## 9. BUILD INSTRUCTIONS

### Hardware mode (default — requires all three devices)
```bash
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
build\Release\RadukCapture.exe
```

### Emulator mode (no hardware required)
```bash
cmake -B build -G "Visual Studio 18 2026" -A x64 -DRADUK_EMULATOR=ON
cmake --build build --config Release
build\Release\RadukCapture.exe
```

At launch (hardware build), if any device is missing a dialog asks whether to launch in emulator mode. No mixing of real and emulated devices is permitted.

---

## 10. WHAT TO INSTALL ON STARMACHINE

1. **Visual Studio 2026** (v18) — Community Edition, "Desktop development with C++" workload
2. **CMake 4.2+** — bundled with VS 2026 or from cmake.org
3. **ZWO ASI Camera SDK** — zwoastro.com -> Software -> Developers -> ASI Camera SDK
4. **ZWO EAF SDK** — zwoastro.com -> Software -> Developers -> EAF SDK
5. **ZWO Camera Driver** — installs with ASI SDK
6. **Celestron StarSense USB-to-Serial Driver** — auto-installs when HC plugged in. Note COM port in Device Manager.
7. **ASTAP** — plate solving engine for Phase 6 alignment (free, local, fast). han.astro.uni-bonn.de/astap
8. **FireCapture** — installed, kept for reference until RadukCapture reaches full parity
9. **ASIStudio** — installed but not used (SDK access is direct)
10. **Git** — version control
11. **vcpkg** — for Phase 2+ dependencies (nlohmann/json, ixwebsocket)
12. **Space Mono + Barlow Condensed fonts** — for Elite Raduk aesthetic (Verdana ships with Windows)
13. **AutoStakkert!** — runs on StarMachine post-session for stacking

**Not needed (removed from plan):**
- ~~ASCOM Platform~~ — NexStar direct eliminates ASCOM dependency
- ~~CPWI~~ — GoTo tab replaces it entirely

### What to install on Elminster

1. **OBS Studio** — for streaming (Phase 7). Configure sources to pull from StarMachine URLs.
2. **APEX Dashboard** — already running
3. **PixInsight / post-processing software** — for stacked image processing
4. No RadukCapture installation needed — Elminster only consumes telemetry and RTSP stream

---

## 11. NAMING CONVENTIONS

| Element | Convention | Example |
|---------|-----------|---------|
| Classes | PascalCase | ASICamera, EAFFocuser, NexStarMount |
| Methods | PascalCase | StartVideoCapture(), GetDroppedFrames() |
| Variables | camelCase | frameCounter, exposureMs |
| Members | m_ prefix | m_cameraId, m_ringBuffer |
| Constants | UPPER_SNAKE | RING_SIZE, DEFAULT_RING_SIZE |
| Files | PascalCase | ASICamera.h, CaptureThread.cpp |
| Namespaces | lowercase | raduk::core, raduk::gui |
| Profiles | lowercase.ini | jupiter.ini, saturn.ini |
| Emulators | *Emulator.h | CameraEmulator.h, EAFEmulator.h |

---

## 12. MILESTONE COMPLETION STATUS

### ✅ Complete

| Milestone | Notes |
|-----------|-------|
| M1: Camera + Focuser + Mount Talk | All three devices communicating |
| M2: Frames Flow | 112 FPS at 1280×960, lock-free ring buffer |
| M3: Picture Appears | Direct2D preview, debayer pipeline |
| M4: Controls Work | Full interactive panel, safety stops, hold-to-slew |
| M5: Battle Ready | Status bar, reticle, zoom, profiles, keyboard shortcuts, session log |
| M6: SER Capture | RAM-first write buffer (512 slots), SER v3 writer, CaptureManager, REC button |
| M7: USB Traffic Manager | EAF throttled to 10s, mount to 2s during capture |
| M8: ASCOM Mount Layer | ⛔ SUPERSEDED — NexStar direct is primary; ASCOM built but blacklisted |
| M9: Frame Analyzer | Histogram, Laplacian sharpness, 10s rolling seeing score |
| M10: GPU Debayer + Frame Bar + Quality Gate | D3D11 cs_5_0 debayer, CPU fallback, frame bar, ROI rubber-band, autofocus, quality gate |
| M12: WebSocket Telemetry | RFC 6455 server, JSON telemetry, APEX command handling |
| Phase 5: Centroid Tracking | Three-stage acquisition, NexStar PulseGuide, planet lock |
| Phase 6: GoTo Panel | Catalog (110 Messier + 7 planets), GoTo, plate-solve, ASTAP, NexStar sync. Now centers on screen when opened |
| Phase 6.1: Sky Map | D2D polar projection, click-to-confirm, horizon safety (15°) |
| Phase 6.2: Planet Lock Hardening | Single-mutex CentroidTracker, fire-and-forget PulseGuide |
| Phase 6.3: FC-Style UI Redesign | Elite Raduk palette, WS_CHILD panel, scrollable, collapsible sections |
| Phase 9/10: Profile Editor + Panel Architecture | EDIT modal, SetWindowSubclass, panel disable during modal |
| Phase 9 (AF): EAF V-Curve Autofocus | AutofocusRoutine.h — full scan, parabola fit, backlash compensation, wired to panel + APEX |
| Phase 10 (Gate): Capture Quality Gating | MIN SEEING threshold gates frame recording; GATED visual feedback on RECORD button |
| v1.0 UI: Child Window Architecture | D2D in preview child HWND — zero bleed, no clip hacks, true bounded preview box |
| v1.0 UI: SharpCap-Style Panel | All 10 sections collapsible, colored circle headers, profile cache, scrollbar layout fix |
| v1.0.1: Floating Mount Controller | Always-on-top WS_EX_TOPMOST safety panel — STOP/jog/rate/tracking/park, RA/Dec readout |
| v1.0.2: Phase 15 ADC Assistant | Edlen refractivity model, Anthem AZ site params, preview overlay + panel readout, severity labels |
| v1.0.3: Phase 11 Lucky Imaging Frame Tagger | sharpness score per frame → WriteSlot → SERWriter → _quality.txt alongside every SER |
| v1.0.4: GetAltAz render loop fix | Serial GetAltAz moved off render thread → detached background thread + atomic cache; RingBuffer release ordering fix |
| v1.0.5: Phase 12 Auto-Exposure Assistant | I-controller, clip detection, HCG floor (182), advisory + auto modes, AE button in CAMERA section |
| v1.0.6: Phase 21 Clipping Warning Overlay | Saturated pixel flash (red, 2.5Hz) in RGBA loop, CLIP% HUD label, C key + toolbar button |
| v1.0.7: Phase 22 Snapshot + Phase 25a Quality Graph | PNG save via WIC (S key), rolling 200-frame sharpness bar chart (Q key), green/amber/red by quality |
| v1.0.8: Phase 41 Pause Capture | P key pauses SER write without closing file; amber PAUSED button; resumes seamlessly |
| v1.0.9: Phase 44 Frame Type Labels + App Icon | Light/Dark/Flat/Bias dropdown in CAPTURE; type in filename; .rc + .ico embedded in exe |
| v1.0.10: Phase 23 RGB Align Display Mode | V key; blue shift up / red down by Edlen dispersion / plate scale; manual ±20px slider; RGB ALIGN HUD |
| v1.0.11: Phase 24 Pixel Value Readout + Notes/Profile link | X key; hover tooltip R/G/B DN + coords; click to lock; notes saved/loaded with profile |
| v1.0.12: Phase 25 Image Boost / Frame Stack FX | B key cycles Normal/Boost2x/Boost4x/Stack3/Stack5/Stack10; display-only; auto-reset on record |

### ✅ Tier 0 — Object-Tracking Reliability — VALIDATED (emulator) 2026-07-11

The closed tracking loop ran end-to-end in the emulator for the first time
(build v1.1.2): GoTo leaves the planet off-center, LOCK runs the measured
guide calibration, and the correction loop walks the planet to center and holds
it against injected drift. Preview frame-hold and the versioned banner landed
alongside during validation.

| Phase | Feature | Status |
|-------|---------|--------|
| Phase 52 | Closed-Loop Tracking Emulation | ✅ validated (emulator) 2026-07-11 |
| Phase 51 | Guide Calibration & Axis Transform | ✅ validated (emulator) 2026-07-11 |
| Phase 53 | Correction-Loop Dynamics | ✅ validated (emulator) 2026-07-11 |
| Phase 54 | Auto-Sync Repair (ASTAP + J2000) | ✅ implemented; needs a real ASTAP install to fully verify |
| Phase 55 | Rig Configuration | ✅ validated (emulator) 2026-07-11 |
| Phase 56 | Frame-Path Memory Safety | ✅ validated (emulator) 2026-07-11 |
| Phase 57 | Mount-Safety & Remote-Control Hardening | ✅ validated (emulator) 2026-07-11 |
| Phase 58 | Camera Fault Recovery | ✅ validated (emulator, USB-drop injection) 2026-07-11 |
| Phase 59 | Capture & Assist Reliability | ✅ validated (emulator) 2026-07-11 |

> **Still owed: on-sky validation.** The emulator cannot exercise the real
> alt-az field rotation, atmospheric seeing, mount backlash, or CPWI/ASCOM
> PulseGuide path. First-hardware-light remains the true test of the guide
> calibration (Phase 51) and correction dynamics (Phase 53).

### 🔲 Next Up

| Phase | Feature | Priority |
|-------|---------|---------|
| Phase 26 | Night Vision Mode | Tier 1 |
| Phase 49 | Help System | Tier 1 |

| Phase 38 | Single-Channel Display Mode | Tier 2 |
| Phase 40 | Notification Bar | Tier 2 |
| Phase 50 | Local AI Assistant | Tier 2 |
| Phase 13 | Sky Survey / Pointing Model | Tier 2 |
| Phase 14 | Session Planner | Tier 2 |
| Phase 16 | Polar Alignment Assistant | Tier 2 |
| Phase 17 | Live Stacking Preview | Tier 3 |
| Phase 18 | AVI/FITS Output | Tier 3 |
| Phase 19 | Multi-Session Stats Dashboard | Tier 3 |
| Phase 20 | Distribution / ASCOM support | Tier 3 |
| OBS Streaming | RTSP H.264 stream | 🚫 Deferred |

---

### 🔧 UI Layout Stabilisation — Resolved (March 2026)

| Issue | Root Cause | Fix |
|-------|-----------|-----|
| Preview image off-centre, status bar text misaligned | No DPI awareness manifest | `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)` at top of `WinMain`; `Shcore.lib` added |
| Right-side frame bar buttons invisible | Unicode glyphs (↑ ★) not in Space Mono — D2D rendered zero-width | Replaced with ASCII `[I]` `[*]` |
| Left-side frame bar buttons missing | Buttons conditionally hidden until tracker flags set | Always draw all three; colour indicates readiness |
| Panel overlapping preview | `AdjustWindowRect` DPI-unaware | `CW_USEDEFAULT` + `SW_SHOWMAXIMIZED` + message pump |
| `previewW` stale | `clientW - PANEL_WIDTH` raced with `WM_SIZE` | `Window::PreviewWidth()` reads from `renderTarget->GetPixelSize()` |
| Spurious 64×64 ROI on startup | Zero-size click snapped to minimum ROI | `< 20px` minimum drag guard in `HandleMouseUp` |
| Histogram missing | `PaintHistogram()` dropped from `PaintAll()` | Restored call between Status and Temperature |
| D2D frame bar buttons still invisible | D2D coordinate space at 96 DPI vs physical pixels | `ToolbarStrip` added — all 7 buttons as native Win32 `BS_PUSHBUTTON` controls above preview |
| D2D matrix multiply ambiguity | `Matrix3x2F::Multiply` not in SDK; `operator*` ambiguous | Manual 3×2 matrix multiply using struct field arithmetic |
| `TOOLBAR_HEIGHT_PX` undefined | Constant removed during `CalcPreviewW` refactor | Restored as `static constexpr int` |
| `OnJogStart` function definition missing | `HeaderClickProc` insertion swallowed function header | Restored `void ControlPanel::OnJogStart(HWND btn) {` |

---

### 🎨 Elite Raduk UI Redesign — SharpCap-Style Panel (In Progress)

Inspired by SharpCap Pro's professional instrument panel layout. Phased implementation:

#### UI Redesign Phase 1 — TELESCOPE Section (Current)
**Goal:** Telescope section matches the SharpCap Scope Controls reference exactly — Az/Alt/RA/Dec/HA readouts, connected status, rate selector, jog d-pad, STOP button, Tracking toggle, Park. All controls functional. Section header uses green circle icon + ▼ collapse.

**Scope Controls reference spec (from screenshot):**
- Header row: `◉ TELESCOPE` (green) + `☑ Connected` checkbox + `≡` menu
- Device name line: `Celestron 8SE (NexStar)  COM3`
- Coordinate readouts: Az, Alt, RA, Dec (4 rows, left label + right value in monospace)
- Rate selector: dropdown or button row (1×–9×, current: 8×)
- Jog d-pad: N/S/E/W with STOP in center (large red)
- `Setup` button (top right)
- `Park` checkbox
- `Tracking` toggle button
- Bottom row: orbit/sync icon buttons

**Files changed:** `ControlPanel.h`, `ControlPanel.cpp`
**PANEL_WIDTH:** Increased from 520 → 560 to give controls more room

#### UI Redesign Phase 2 — FOCUSER Section
EAF section to match SharpCap Focuser Driver style:
- Header: `◉ FOCUSER` (amber) + `☑ Connected` + position readout
- Temperature compensation checkbox
- Coarse/Fine step buttons with numeric spin boxes
- Stop button
- Reverse checkbox
- Setup button

#### UI Redesign Phase 3 — CAMERA Section
Camera sliders with proper spin-box value controls, Auto buttons on WB sliders, compact label/slider/value layout.

#### UI Redesign Phase 4 — CAPTURE + STATUS + NOTES
CAPTURE section with RECORD button, output dir, frame/time limits. STATUS with histogram bars. NOTES multiline edit at bottom.

#### UI Redesign Phase 5 — Full Toolbar + Menu Bar
SharpCap's toolbar has 19 buttons. RadukCapture currently has 7. Full parity requires:

**Current RadukCapture toolbar (7 buttons):**
`[CENTROID]` `[LOCK]` `[ROI]` | `[C] COLOR` `[MAP]` `[GOTO]` `[RETICLE]`

**SharpCap toolbar (19 items) mapped to RadukCapture:**
| SharpCap | RadukCapture | Status |
|---|---|---|
| Live View toggle | — | Not needed (always live) |
| Start Capture | `RECORD` button in panel | ✅ Covered (panel) |
| Quick Capture | — | 🔲 Phase 22 snapshot covers this |
| Stop Capture | `RECORD` button toggles | ✅ Covered |
| Pause capture | — | 🔲 Add to toolbar (Phase 41) |
| Snapshot (save PNG) | — | 🔲 Phase 22 |
| Framing Shot | — | 🔲 Phase 39 |
| Two Monitor Mode | — | 🔲 Phase 42 (Elminster second monitor) |
| Live Stack | — | 🔲 Phase 17 |
| Deep Sky Annotation | — | 🔲 Phase 43 |
| Target Name | Notes field in panel | Partial |
| Frame Type | — | 🔲 Phase 44 (Light/Dark/Flat/Bias labels in filename) |
| Preview FX dropdown | — | 🔲 Phase 25 (Boost/Stack/RGB) |
| Selection Area | ROI rubber-band | ✅ Done |
| Reticule Overlays | `[RETICLE]` button | ✅ Done (1 type) — 🔲 Phase 45 multi-reticle |
| Zoom | Scroll wheel + buttons | ✅ Done |
| Image Histogram | Panel histogram section | ✅ Done |
| Calculate Focus Score | AF button + sharpness HUD | ✅ Done |
| Toggle Hardware | — | 🔲 Phase 46 (connect/disconnect all) |

**New phases from toolbar review:**
- **Phase 41 — Pause Capture:** Pause/resume recording without closing the SER file. Frames continue displaying live but are not written to disk. Amber PAUSED indicator on RECORD button.
- **Phase 42 — Two Monitor Mode:** Send preview to second monitor (Elminster via network share or second display on StarMachine). Preview fills second screen; controls stay on primary.
- **Phase 43 — Deep Sky Image Annotation:** After plate solve, overlay catalog object names/icons on the preview for any DSO in the field. Uses existing SkyObject catalog + ASTAP solve result.
- **Phase 44 — Frame Type Labels:** Light / Dark / Flat / Bias frame type selector. Appended to output filename. Used for session organization and dark library management.
- **Phase 45 — Multi-Reticle Overlays:** Multiple reticle shapes — crosshair (current), circle, rings (collimation), custom — selectable from toolbar dropdown.
- **Phase 46 — Connect/Disconnect All Hardware:** Single toolbar button connects or disconnects camera + EAF + mount simultaneously. Useful for session start/end.

**Tools menu items mapped to RadukCapture:**
| SharpCap Tool | RadukCapture | Status |
|---|---|---|
| Histogram | Panel HISTOGRAM section | ✅ Done |
| Live Stack | — | 🔲 Phase 17 |
| Polar Align | — | 🔲 Phase 16 |
| Plate Solve Only | GoTo panel POINT SOLVE | ✅ Done |
| Plate Solve & Resync | GoTo panel POINT SOLVE→SYNC | ✅ Done |
| Sensor Analysis | — | 🔲 Phase 36 |
| ADC Alignment | — | 🔲 Phase 35 |
| Pixel Value Readout | — | 🔲 Phase 24 |
| Pixel Position / Click to Recentre | — | 🔲 Phase 47 |
| Deep Sky Image Annotation | — | 🔲 Phase 43 |
| Ghosted Image Alignment | — | 🔲 Phase 29 |
| Goto Image (return to previous) | — | 🔲 Phase 48 |
| Goto Catalog Target | GoTo panel | ✅ Done |
| Push-To Assistant | — | 🔲 Phase 30 |
| Focus Assistant | AUTO FOCUS button | ✅ Done (V-curve) |
| Seeing Monitor | Sharpness + rolling score | ✅ Done |
| Feature Tracking | Phase 5 Centroid Tracking | ✅ Done |
| Collimation Assistant | — | ➡ Not applicable (SCT) |
| Solar/Lunar Framing | — | 🔲 Phase 34 |
| Stellar Photometry | — | 🔲 Phase 33 |

**New phases from tools review:**
- **Phase 47 — Pixel Position / Click to Recentre:** After a plate solve, click any pixel to see its RA/Dec/Alt/Az coordinates. Double-click to slew mount to that point. Directional rose shows +/-RA/Dec/Alt/Az axes. Depends on plate solve + mount connection.
- **Phase 48 — Goto Image (Return to Previous):** Load a previously saved SER/PNG, plate-solve its first frame to determine sky coordinates, then slew the mount to those coordinates. Allows returning to the exact same framing from a previous session.

---

## 13. FEATURE ROADMAP — ELITE RADUK COMPLETE

Phases are ordered by **impact × ease**, not just phase number. The priority within each tier is deliberate:
- Tier 0: object-tracking reliability — blockers that must land before first hardware light
- Tier 1: directly improves capture quality or session productivity tonight
- Tier 2: operational polish and automation
- Tier 3: advanced / long-term / nice-to-have

---

### Tier 0 — Object-Tracking Reliability (added 2026-07-06)

Source: `docs/CODE_REVIEW_2026-07-06.md` (end-to-end tracking review). The
June 2026 blocker batch (T1–T16) landed and was verified — guide pulses are now
physically deliverable, and the safety paths (STOP, capture loss, jog
contention, stale centroids) are sound. What remains is the work that makes the
correction loop *correct* and *testable*. Phases 51–54 are hard blockers for
live tracking; 55–58 close the remaining review findings that sit in or beside
the tracking path. Recommended order: **52 → 51 → 53 → 54 → 55 → 56 → 57 → 58**
— the emulation harness comes first because every later phase needs a rig where
tracking bugs are visible before a live night.

> **Implementation status (2026-07-10):** all eight phases are implemented on
> PR #2 (one commit per phase). The code has NOT yet been compiled or run —
> the implementation session had no Windows/MSVC build environment — so the
> acceptance tests below are still owed:
> 1. Emulator build: GoTo Jupiter → CENTROID → LOCK (calibration runs) →
>    planet visibly re-centres; repeat with `RADUK_EMU_ROTATION_DEG=137` to
>    prove the measured transform, and `RADUK_EMU_REMOVE_AFTER_S=30` to
>    exercise fault recovery.
> 2. `RADUK_WS_LAN` note: the APEX dashboard now needs this env var set on
>    StarMachine to connect from Elminster.

#### Phase 51 — Guide Calibration & Axis Transform (review T3) — CRITICAL
The correction loop currently *assumes* +X→East / +Y→North, a fixed 0.203″/px
plate scale, and 100 ms/arcsec pulse sizing. On the alt-az 8SE the sensor↔mount
mapping rotates continuously with the field, and the tracker computes RA/Dec
errors while NexStar pulses drive Az/Alt axes. A wrong sign makes the loop
positive feedback — each pulse pushes the planet further out.
- PHD2-style calibration at the start of Centering: pulse East N ms, measure
  centroid displacement vector; pulse North, measure again; derive the 2×2
  pixel→mount transform and effective ms/arcsec from the response
- Re-derive (or rotate) the transform as altitude changes; scale azimuth
  corrections by 1/cos(alt)
- Persist calibration per rig; refuse to enter Centering with a stale/missing
  calibration; surface calibration quality in the UI
- **Effort: Hard** — new routine + state machine, but the single most important
  change before live tracking

#### Phase 52 — Closed-Loop Tracking Emulation (review E1 + C5) — CRITICAL, do first
`CameraEmulator` renders Jupiter pinned to the ROI centre; `MountEmulator`
pulses nudge coordinates nothing reads back. The tracking loop has therefore
**never run closed**, even once — sign/axis/gain bugs are invisible in every
test the project can currently run. This is the structural reason tracking has
remained the biggest hurdle.
- Feed MountEmulator pointing error (commanded − target) into CameraEmulator so
  the planet renders at centre + error × plate scale; pulses then visibly
  re-centre it
- Injectable drift rate, seeing jitter, and camera-rotation angle (to prove
  Phase 51 calibration finds arbitrary orientations)
- Adversarial SDK semantics: blocking `GetVideoFrame`, dropped frames,
  `ASI_ERROR_CAMERA_REMOVED` injection, honest RAW16 frame generation
- Rename the emulator class `NexStarMount` → `MountEmulator` (ODR landmine) and
  delete the stale duplicate `src/core/SkyMapWindow.h`
- **Effort: Medium** — pays for itself immediately; acceptance test = full
  Search→Center→Lock→drift→correct→lose→reacquire cycle passing with a
  randomized camera rotation

#### Phase 53 — Correction-Loop Dynamics (review T13)
Deadbands (1″/2″) sit below planetary seeing jitter (2–5″), there is no
centroid averaging, and pulses are issued with blocking sleeps so a Locked
cycle takes ~1.35 s, not the stated 4 Hz. The loop will chase noise.
- Average the centroid over a 0.5–2 s window before correcting
- Size deadbands from measured jitter; make aggressiveness/deadband tunable
  from the rig config (Phase 55)
- Schedule pulses without sleeping the correction loop through delivery;
  document the achievable cycle rate honestly
- **Effort: Medium** — validate against Phase 52's injected drift + jitter

#### Phase 54 — Auto-Sync Repair: ASTAP keys + J2000 consistency (review S2 + S5)
Stage-3 auto-sync and the pointing model silently never work: the parser reads
`SOLVEAD=`/`SOLVEDEC=` but ASTAP writes `PLTSOLVD=T`, `CRVAL1/2`, `CDELT1/2`,
`CROTA2`.
- Parse ASTAP's real .ini output; fix the RA-hint hours/degrees mismatch
- `TerminateProcess` timed-out solves; unique temp filenames so a zombie's
  stale output can't poison the next solve
- Reconcile equinox-of-date ephemeris output with J2000 plate solves and the
  J2000 catalog (~22′ discrepancy in 2026) before mixing them in syncs
- **Effort: Easy–Medium** — small parser fix, big payoff: closes the
  GoTo → solve → sync verification loop that tracking acquisition depends on

#### Phase 55 — Rig Configuration (review T17)
Plate scale, sensor size, site lat/long, and guide-rate constants are scattered
literals; a Barlow or bin change silently breaks pulse sizing and ROI math.
- One persisted rig-config struct (site, sensor dims, plate scale, guide rate,
  camera rotation seed) consumed by CentroidTracker, ADC, GoTo, and ROI code
- Populate sensor size from `ASI_CAMERA_INFO` at connect
- **Effort: Easy–Medium**

#### Phase 56 — Frame-Path Memory Safety (review C1 + G1 + G2)
The three memory-safety findings in the path that feeds the tracker and SER
writer:
- RingBuffer: fix the producer-overwrite race (CAS the READY→WRITING
  transition; keep `m_writePos` in sync on the skip path) — torn frames
  currently become garbage centroids at real frame rates
- PreviewRenderer: reallocate `m_fxAccum`/`m_fxFrames` on frame-size change
  (heap overflow with Stack FX active)
- Defer the rubber-band ROI auto-apply callback (PostMessage) instead of firing
  `ring.Allocate()` mid-render (use-after-free)
- **Effort: Medium**

#### Phase 57 — Mount-Safety & Remote-Control Hardening (review G5 + S1 + G3 + T18)
Every remaining path that can move the mount against the user's intent:
- SkyMap confirm popup: store the object (name+coords), not an index into a
  list that rebuilds under the popup
- WebSocket server: clamp `payLen` (~1 MB) before any arithmetic, cap `recvBuf`
  growth, bind 127.0.0.1 by default or require a handshake token — the current
  server exposes unauthenticated mount slewing to the LAN and a one-frame
  remote crash
- Unfuse `load_profile` from the `jog` remote-command branch in main.cpp
- Serial hygiene: hold `m_serialMutex` across `Disconnect()`'s CloseHandle;
  drain fire-and-forget `#` acks so poll parsing stops corrupting; align the
  watchdog header comment (3 s) with the constant (8 s)
- **Effort: Medium**

#### Phase 59 — Capture & Assist Reliability (review C2 + C3 + S3 + S4 + G4-scoped)
The remaining HIGH findings beside the tracking chain — all capture-quality or
shutdown-safety issues that would surface on a real session night:
- SER trailer timestamps derived from each frame's CAPTURE time via a
  QPC↔wall-clock correlation taken at file open (they recorded disk-write
  time, up to ~10s late with a full write-buffer backlog — useless for
  WinJUPOS derotation); QPC math made overflow-safe past 21 days uptime
- CaptureManager reallocates the write buffer when per-slot capacity no
  longer fits the frame (ROI grew → every push failed → zero-frame SER),
  and a failed multi-GB allocation now aborts the start with a visible error
- Autofocus parabola fit rejects upward-opening fits (noisy data previously
  drove the focuser to maximum defocus and reported success)
- Auto-exposure HCG floor is advisory: it no longer snaps gain UP on
  already-bright low-gain profiles, and a clip emergency stuck at minimum
  exposure now cuts gain too
- Autofocus scan thread is joinable (was detached — could outlive shutdown
  and invoke destroyed callbacks)
- **Status: ✅ validated in emulator 2026-07-11 (build v1.1.2); on-sky test still owed**

#### Phase 58 — Camera Fault Recovery (review C4)
Architecture §6.3 describes the "Pipeline Wedge" auto-reset, but
`RestartCapture()` has no callers and all `ASI_ERROR_CODE`s collapse to bool —
a USB drop busy-spins a TIME_CRITICAL thread forever.
- Plumb error codes out of `GetVideoFrame`; distinguish TIMEOUT from
  CAMERA_REMOVED
- Implement §6.3: consecutive-timeout threshold → StopVideoCapture → 100 ms
  flush → StartVideoCapture, with backoff and a UI fault banner
- Validated by Phase 52's CAMERA_REMOVED injection
- **Effort: Medium**

---

### Tier 1 — High Impact, Direct Capture Improvement

#### Phase 12 — Auto-Exposure Assistant
Real-time histogram → suggested gain/exposure to keep planet peak at 70–80% of sensor range. `meanBrightness` and `clipPct` already computed in `FrameAnalyzer`.
- PID controller drives suggestions: "↑ Exposure 0.5ms" or "↓ Gain 20" in sidebar
- `AUTO` toggle applies changes automatically between captures
- Manual mode shows advisory text only — operator decides
- **Effort: Medium** — FrameAnalyzer already has inputs; needs PID logic + camera write path

#### Phase 21 — Clipping Warning Overlay
Flash overexposed pixels red in the preview when sensor is clipping. Prevents blown-out captures during gain adjustment. Display-only — does not affect SER data.
- Extra pass over RGB buffer: if any channel == 255, set pixel to flashing red
- Toggle via toolbar button `[CLIP]` or key `C`
- Clip percentage shown in HUD alongside sharpness: `CLIP 2.3%`
- **Effort: Easy** — single buffer scan, D2D bitmap overlay, ~1 hour

#### Phase 22 — Snapshot (PNG / FITS save)
Single keypress `S` saves the current debayered frame as a PNG (or raw FITS) to the captures folder. Timestamped filename matching SER naming convention.
- `Jupiter_20260322_213045_snap_0001.png`
- Uses existing debayer buffer — just encode to PNG via `stb_image_write` or GDI+ 
- FITS variant writes raw Bayer data with header (target name, RA/Dec, EAF pos, gain, exp)
- Panel button + `S` key shortcut
- **Effort: Easy** — buffer already exists, PNG write is ~20 lines, FITS header ~40 lines

#### Phase 23 — RGB Align Display Mode
Live channel-shift on preview display to visually compensate atmospheric dispersion. Complements Phase 15 ADC measurement — Phase 15 tells you how bad it is, Phase 23 lets you correct it on screen for focusing.
- Uses Phase 15 `ADCAssistant::Compute(alt)` arcsec value to compute pixel offset (arcsec / plate_scale)
- Plate scale for 8SE + ASI678MC: 0.203"/px at f/10
- Shifts blue channel UP by N pixels during debayer/display pass
- Manual fine-tune slider ±20px in FOCUSER section for when ADC hardware is in-loop
- Toggle via toolbar `[RGB]` button
- **Effort: Easy-Medium** — modify debayer copy pass, add slider to panel, ~2 hours

#### Phase 24 — Pixel Value Readout
Click any pixel in the preview → HUD shows R/G/B DN values and coordinates. Essential for accurate exposure calibration and confirming target is not clipped.
- Mouse hover: small tooltip follows cursor showing `R:187 G:201 B:143` 
- Click: locks the readout to that pixel until next click
- Coordinate display: `[1234, 567]` in sensor pixel space (accounting for zoom/pan)
- **Effort: Easy** — mouse position → unscale to frame coords → index into RGB buffer

#### Phase 25 — Image Boost / Frame Stack Display Effects
Software brightness boost and N-frame averaging for display only — helps find faint/dim targets and check focus on challenging objects. Does not affect SER data.
- `BOOST` mode: software gain 2×/4× applied to display output only
- `STACK 3/5/10` mode: rolling average of last N debayered frames in a display buffer
- Stack display updates every N frames; capture continues at full speed unaffected
- Toolbar dropdown `[FX]`: Normal / Boost 2x / Boost 4x / Stack 3 / Stack 5 / Stack 10
- **Effort: Medium** — needs display accumulation buffer separate from ring buffer

---

### Tier 2 — Operational Quality-of-Life

#### Phase 13 — Sky Survey Mode
Automated pointing model builder. 15–20 minute grid sweep before imaging starts.
- 20–40 Az/Alt positions above 30°, plate-solve each one, write to `pointing_model.csv`
- After survey, first GoTo of the night lands in-frame without needing CENTERING stage
- Triggered from GoTo panel: "START SURVEY" button
- **Effort: Medium** — needs slew sequencer loop, ASTAP calls, CSV write

#### Phase 26 — Night Vision Mode
Red-tinted UI for preserving dark adaptation during outdoor sessions.
- All UI chrome, panel labels, and HUD text shift to deep red palette
- Preview image desaturated and red-tinted
- Toggle via `N` key or View menu
- Persists across sessions (saved in config)
- **Effort: Easy** — Theme.h color swap + D2D tint matrix on preview bitmap

#### Phase 14 — Session Planner
Given tonight's date/time/location, compute optimal imaging windows for a target list.
- Rise/set times, minutes above 30° altitude, recommended slew order
- "Tonight" tab in GoTo panel — objects sorted by "imaging minutes remaining tonight"
- Pure ephemeris math — VSOP87 already in `SkyObject.h`. Zero network dependency.
- **Effort: Medium** — ephemeris loops, UI tab in GoTo panel

#### Phase 27 — Dark Frame Subtraction (Live)
Apply a master dark frame to the live preview in real-time. Reduces hot pixel clutter during focusing. Does not affect SER data by default — optional bake-in toggle.
- Load master dark PNG/FITS from disk (captured with same gain/exp/temp)
- Subtract pixel-by-pixel during debayer pass: `output = max(0, raw - dark)`
- Dark frame management in IMAGE section: load/clear/status
- **Effort: Medium** — buffer allocation + per-pixel subtract in debayer pass

#### Phase 28 — Flat Frame Correction (Live)
Apply master flat for vignetting/dust correction. Primarily useful for DSO sessions.
- Load master flat from disk
- Divide each pixel by normalized flat value during display pass
- Combined dark+flat pipeline: `corrected = (raw - dark) / flat_norm`
- **Effort: Medium** — builds directly on Phase 27 dark infrastructure

#### Phase 6.6 — GoTo Map Learning
Auto plate-solve on every Stage 3 centroid lock. Pointing error → `pointing_model.csv`. Subsequent GoTos pre-corrected using weighted average of nearby entries.
- Accuracy improves automatically across a session and night-over-night
- **Effort: Medium-Hard** — needs plate solve trigger, error vector math, CSV persistence

#### Phase 16 — Polar Alignment Assistant
Three-star drift method using mount + ASTAP plate solver.
1. Point near meridian, plate-solve, record
2. Wait 5 minutes, plate-solve again
3. Drift vector → polar alignment error magnitude + correction direction
- Results: "Adjust ALT +0.3° / AZ -0.1°" with live drift graph
- **Effort: Hard** — long observation window, drift math, UI graph

#### Phase 29 — Ghosted Image Alignment Overlay
Overlay previous frame ghosted at 50% opacity to help with framing and target centering. Useful when manually nudging the mount for precise framing.
- `GHOST` toggle button in toolbar
- Captures a reference frame on button press; overlays it at 50% until toggled off
- Reference frame updates on each press
- **Effort: Easy** — store one extra frame buffer, blend in D2D during draw

#### Phase 30 — Push-To Assistant
For sessions when using manual (non-GoTo) mode or when GoTo is unavailable. Plate-solves repeatedly and gives directional nudge instructions to reach a target.
- Same catalog/coordinate entry as GoTo panel
- After each plate solve: shows "Move N 0.5° E 1.2°" until target centered
- Uses existing ASTAP plate solver infrastructure
- **Effort: Medium** — plate solve loop + vector math + directional HUD overlay

---

### Tier 3 — Advanced / Long-Term

#### Phase 35 — ADC Alignment Tool
Active ADC hardware alignment assistant. Measures actual R-G and B-G channel offsets in pixels from a live planetary image and guides ADC prism adjustment. Completes the ADC story: Phase 15 predicts dispersion, Phase 23 corrects display, Phase 35 guides physical ADC hardware adjustment.
- Compute centroid of each colour channel independently on a user-defined ROI
- Display red/blue offset lines relative to green centre (like SharpCap's ADC tool)
- Show offset history graph: red line above zero, blue line below, adjust until both flat
- Black threshold slider to exclude background from centroid calculation
- Rotation and magnitude guidance text: "Rotate ADC clockwise 15°" / "Reduce correction"
- Works on planets, moon limb, or any extended object with colour fringing
- **Effort: Medium** — channel separation + centroid math + history graph in D2D

#### Phase 36 — Sensor Analysis + Smart Histogram
One-time calibration routine that characterises the ASI678MC sensor at all gain levels. Results stored as a JSON file and used to inform optimal gain/exposure recommendations.
- Measure read noise, full well capacity, and dark current vs gain
- Results feed a "Smart Histogram" overlay: red/amber/green zones showing where read noise dominates shot noise
- "Brain" window: given imaging time budget, compute optimal gain + exposure combination
- Particularly valuable for HCG threshold confirmation on ASI678MC (known threshold ~182)
- Requires uniform illumination source (twilight flat panel or overcast sky)
- **Effort: Hard** — automated gain sweep, variance analysis, curve fitting, JSON persistence

#### Phase 37 — Display Stretch (Mini Histogram)
Brightness/contrast stretch of the preview display without affecting SER data. Enables finding faint detail and checking focus on dim targets.
- Mini histogram strip in the IMAGE section of the panel
- Auto-stretch button: sets black/white points to 0.1%/99.9% of pixel distribution
- Manual black/white point sliders
- Locked stretch mode: auto-stretch re-applies every 5 frames
- Display-only — captured SER frames are always raw sensor data
- **Effort: Medium** — histogram percentile calculation + linear remap in display pass

#### Phase 38 — Single-Channel Display Mode
View only the Red, Green, or Blue channel of the Bayer image in isolation. Useful for ADC alignment verification, filter testing, and colour balance calibration.
- Toolbar `[FX]` dropdown: Normal / R only / G only / B only
- In single-channel mode, output a grayscale image from the selected channel's Bayer pixels
- Complements Phase 23 RGB Align — lets you verify each channel independently
- **Effort: Easy** — modify debayer output pass to zero unused channels

#### Phase 39 — Framing Shot (Quick Boost Snapshot)
One-click "framing check" that temporarily boosts gain, takes a single frame, displays it, then restores settings. Useful for checking framing on a faint DSO before switching to planetary settings.
- Press `F` key or toolbar button: saves current gain/exp, sets gain=max, exp=2s, captures one frame
- Displays result full-screen for 5 seconds then restores all settings
- Does not save to disk by default (preview only) — optional save toggle
- **Effort: Easy-Medium** — camera settings save/restore + single-frame capture path

#### Phase 41 — Pause Capture
Pause/resume an active SER recording without closing the file. Frames continue displaying live but are skipped from disk. Amber `PAUSED` indicator replaces `RECORDING` on the RECORD button.
- `P` key or toolbar button toggles pause state
- SER file stays open — resume writes continue seamlessly
- Paused frame count shown: `PAUSED — 142 frames saved`
- **Effort: Easy** — boolean gate in `CaptureManager::SubmitFrame`, no file changes needed

#### Phase 44 — Frame Type Labels + Application Icon
**Frame type labels:** Light / Dark / Flat / Bias frame type selector. Appended to output filename and embedded in SER observer field.
- Dropdown in CAPTURE section: Light (default) / Dark / Flat / Bias
- Output filename: `Jupiter_20260322_Light_0001.ser` / `Dark_0001.ser`
- Dark frames: prompt to cap telescope. Flat frames: prompt for illumination source.

**Application icon + .rc file:**
- Create `src/RadukCapture.rc` — Win32 resource file embedding icon and version info
- Create `assets/RadukCapture.ico` — multi-resolution icon (16, 32, 48, 256px)
  - Dark navy background, cyan aperture ring, star burst — matches Elite Raduk palette
- Add `ICON` resource to `.rc`: replaces the `R` default in title bar and taskbar
- Add `VS_VERSION_INFO` block: FileVersion, ProductVersion, CompanyName, FileDescription
- Wire into CMakeLists.txt: `target_sources(RadukCapture PRIVATE src/RadukCapture.rc)`
- `wc.hIcon` in `Window::Create` changed from `IDI_APPLICATION` to `LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_RADUK))`
- **Effort: Easy** — `.rc` file + icon asset + one CMake line + one Window.cpp change

#### Phase 45 — Multi-Reticle Overlays
Multiple reticle shapes selectable from toolbar dropdown. Currently only crosshair exists.
- Crosshair (current) / Circle / Rings (3 concentric, for collimation) / Custom (resizable box)
- Each reticle: draggable, resizable, rotation angle shown
- Cycle via `RETICLE` button or dropdown
- **Effort: Easy-Medium** — extend `PreviewRenderer::DrawReticle` with shape enum

#### Phase 46 — Connect/Disconnect All Hardware
Single toolbar button connects or disconnects camera + EAF + mount simultaneously.
- `[HW]` toolbar button: green = all connected, amber = partial, red = all disconnected
- On connect: open camera → open EAF → connect mount (same order as startup)
- On disconnect: orderly shutdown of all three (stop capture first if recording)
- **Effort: Medium** — orchestration logic, error handling, status indicator

#### Phase 47 — Pixel Position / Click to Recentre
After a plate solve, hover any pixel to see RA/Dec/Alt/Az coordinates. Double-click to slew mount to that point.
- Requires recent plate solve result + connected mount
- Directional rose overlay showing +/-RA, Dec, Alt, Az axes
- Double-click: "Slew to RA 02h 47m 12s Dec +42° 21' 33"?" confirmation then GoTo
- **Effort: Medium** — WCS math (plate solve result → pixel → sky coords), GoTo integration

#### Phase 48 — Goto Previous Image
Load a previously saved SER file, plate-solve its first frame, slew to those coordinates. Return to exact framing from any previous session.
- "Return to last night's Jupiter session" via file picker
- Plate-solves the first frame of the SER to get sky coordinates
- Feeds into GoTo pipeline — same slew/center/sync path as GoTo panel
- **Effort: Medium** — SER first-frame extraction + plate solve + GoTo invocation

#### Phase 49 — Help System
In-app help window (`?` key or `[?]` toolbar button) showing the full reference guide.
- `RadukCapture-Help.md` maintained alongside the codebase — updated after every release
- Help window: `WS_EX_TOPMOST | WS_EX_TOOLWINDOW` floating panel (~700×900px)
- Content loaded from `RadukCapture-Help.md` at runtime; falls back to embedded string if file not found
- Search bar filters visible sections (type "focus" → shows FOCUSER + Autofocus sections)
- Rendered using a read-only Win32 `EDIT` control with monospace font
- Toolbar button `[?]` added as 9th button
- **Effort: Medium** — file I/O + search + Win32 text display, ~3 hours
Transient status bar below the toolbar that confirms completed actions. Provides clickable link to open the folder where a file was just saved.
- Green bar: "Saved: Jupiter_20260322_213045_0001.ser (1.2 GB) → Open folder"
- Amber bar: warnings (buffer overrun, EAF position limit, mount lost)
- Red bar: errors (disk full, camera timeout, write failure)
- Auto-dismisses after 8 seconds or on click
- **Effort: Easy** — Win32 child window, timer-based fade, ShellExecute for folder open

#### Phase 17 — Live Stacking Preview
Rolling N-frame average in a second window. Useful for DSO sessions and visual quality confirmation.
- Rolling N-frame average; N configurable (16, 32, 64, 128)
- Separate floating window alongside live preview
- **Effort: Hard** — dedicated accumulation buffer, alignment pass, second D2D window

#### Phase 31 — Multi-Star FWHM Focus Score
Add FWHM star-profile measurement to `FrameAnalyzer` as a second focus metric alongside existing Laplacian sharpness. More robust for sparse star fields.
- Detect N brightest stars via peak-finding, measure PSF width via centroid moments
- Plot FWHM vs EAF position on autofocus V-curve for better fit accuracy
- **Effort: Hard** — star detection, centroid math, multi-star averaging

#### Phase 32 — Bahtinov Mask Focus Assistant
Detect Bahtinov diffraction spike angle from live preview. Shows angular deviation from perfect focus with a numeric score and direction arrow.
- FFT-based spike detection on a user-defined ROI
- "Move focuser IN 200 steps" advisory with live angle readout
- Works with any Bahtinov mask physical accessory
- **Effort: Hard** — FFT + spike detection algorithm

#### Phase 18 — AVI / FITS Output
Alternative output formats alongside SER.
- AVI for legacy stacking software compatibility
- FITS for scientific use / astrometric reduction
- `SERWriter` already handles the hard parts — format adapters only
- **Effort: Medium** — format adapters, header writing

#### Phase 19 — Multi-Session Statistics Dashboard
Local HTML page served at `http://starmachine:8080/stats`.
- All sessions this season: date, target, frames, best seeing, stack quality
- Pointing model accuracy over time, most-imaged objects, total hours by target
- Built from CSV/log files that already exist
- **Effort: Medium** — HTTP server (Winsock2), HTML generation from logs

#### Phase 33 — Stellar Photometry Estimation
Measure apparent magnitude of stars/planets in frame using comparison star database. Useful for variable star monitoring and confirming ADC correction quality.
- Cross-reference detected stars with UCAC4/Tycho-2 catalog subset
- Report estimated magnitude ± error for brightest objects in frame
- **Effort: Very Hard** — catalog integration, photon calibration math

#### Phase 50 — Local AI Assistant

Floating chat panel powered by a local LLM (Ollama + llama3.2:3b). Fully offline — no internet required after initial model pull. Toggle with `/` key or `[AI]` toolbar button.

**Stack**
- **Runtime:** Ollama (`https://ollama.com`) running as a local HTTP server on `localhost:11434`
- **Model:** `llama3.2:3b` — ~2GB VRAM, ~1-2s first-token latency on StarMachine GPU
- **Install once:** `ollama pull llama3.2:3b` — after that fully offline forever
- **RadukCapture side:** WinHTTP calls to `http://localhost:11434/api/chat` (streaming NDJSON)

**UI — `AIChatPanel.h`**
- `WS_EX_TOPMOST | WS_EX_TOOLWINDOW` floating window, ~500×620px, right side of screen
- Scrollable read-only EDIT for conversation history (Segoe UI 12pt)
- Single-line EDIT for user input at bottom
- `[Send]` button + Enter key to submit
- `[Clear]` button resets conversation history
- Token streaming: each chunk from Ollama appended to history EDIT in real-time via `PostMessage` from background thread
- Thinking indicator: `...` appended while waiting for first token
- `/` key or `[AI]` toolbar button (10th button, after `[?]`) toggles panel
- Window title shows model name: `RadukCapture AI  ·  llama3.2:3b`

**Context injection — `AIContextBuilder.h`**
Builds a system prompt prepended to every conversation. Refreshed on each new user message. Five context blocks:

```
[IDENTITY]
You are RadukScope, an AI assistant for the RadukCapture astrophotography application.
You are running locally on StarMachine (Anthem, AZ — Bortle 4, elevation 1732ft, UTC-7).
Answer concisely. If unsure, say so. Never invent equipment specs or sky positions.

[APP REFERENCE]
{full contents of RadukCapture-Help.md — loaded once at startup}

[SESSION STATE]  — injected fresh each query
Time: 2026-03-22 21:45 MST  |  LST: 08h 32m
Target: Jupiter  |  Profile: Jupiter
Mount: RA 11h 52m  Dec +1°14'  Alt 42.3°  Az 198°  Tracking: ON
Camera: Gain 300  Exp 8ms  FPS 38.2  Seeing 7.4/10
EAF: 152340  Temp: 18.2°C
Recording: YES  Frames: 4821  Buffer: 12/512  Drops: 0

[TONIGHT'S SKY]  — altitude-sorted, above 15°, updated each query
Jupiter   Alt 42°  Az 198°  Mag -2.7  (currently targeted)
Venus     Alt 28°  Az 245°  Mag -4.4
Saturn    Alt 18°  Az 112°  Mag  0.7
M42       Alt 55°  Az 210°  Mag  4.0  (Great Nebula in Orion)
M45       Alt 61°  Az 188°  Mag  1.6  (Pleiades)
... (top 20 objects above 15°)

[CAPTURES TONIGHT]  — scanned from output directory
Jupiter_20260322_213045_Light_3840x2160_8bit_0001.ser  (1.2 GB)
Jupiter_20260322_214512_Light_3840x2160_8bit_0002.ser  (890 MB)

[OBJECT KNOWLEDGE DATABASE]  — future phase
{SQLite lookup for queried object name — Wikipedia scraped summaries,
 physical data, imaging notes. Stub returns empty string until populated.}
```

**What it can answer:**
- *"How do I use the quality gate?"* → Help.md context
- *"Why are my frames dropping?"* → sees Drops: 0 in session context, checks buffer
- *"What else is good to image tonight near Jupiter?"* → Tonight's sky context
- *"What is M42?"* → Object knowledge DB (once built)
- *"Is the seeing good enough to record?"* → Seeing 7.4/10 in context, gives recommendation
- *"How long have I been recording?"* → Captures tonight context
- *"What's the best exposure for Saturn?"* → Help.md + session context

**Architecture — files**

| File | Responsibility |
|------|---------------|
| `gui/AIChatPanel.h` | Floating Win32 chat window, streaming EDIT display |
| `core/AIAssistant.h` | Ollama HTTP client, streaming JSON parser, background thread |
| `core/AIContextBuilder.h` | System prompt assembly from all five context sources |
| `core/ObjectKnowledgeDB.h` | SQLite stub — returns empty string until knowledge base built |

**Context pipeline**
```
User types question
  → AIContextBuilder::Build(sessionState, catalogSnapshot, capturesList)
      → loads Help.md (cached after first load)
      → injects live session snapshot
      → queries altitude-sorted catalog top 20
      → scans captures directory
      → queries ObjectKnowledgeDB (stub → empty)
  → AIAssistant::SendMessage(systemPrompt, conversationHistory, userMessage)
      → POST http://localhost:11434/api/chat  {stream: true}
      → background thread reads NDJSON chunks
      → each chunk → PostMessage(WM_USER+50, ...) → appends to EDIT
  → User sees streaming response
```

**Object Knowledge Database — future sub-phase 50a**
- SQLite file: `D:\RadukCapture\data\object_knowledge.db`
- Schema: `object_name TEXT, common_name TEXT, type TEXT, summary TEXT, physical_data TEXT, imaging_notes TEXT, source_url TEXT`
- Population: Python scraper (`tools\build_knowledge_db.py`) — reads Messier catalog, queries Wikipedia API for each object, extracts lead paragraphs + infobox data, stores locally. Run once, update periodically.
- ~110 Messier + 7 planets + NGC bright subset (~500 objects total)
- `ObjectKnowledgeDB.h` stub returns `""` until DB file exists — AI gracefully says "I don't have detailed notes on that object yet"

**Installation instructions (add to Help.md)**
```
1. Download Ollama: https://ollama.com/download
2. Install on StarMachine
3. Open Command Prompt: ollama pull llama3.2:3b
4. Ollama runs automatically as a background service
5. Press / in RadukCapture to open the AI assistant
```

**Effort: Medium** — WinHTTP streaming, Win32 chat UI, context builder, ~1 day

#### Phase 12b — Ollama Integration (Advisory)
Local LLM on Elminster reads session telemetry via WebSocket and offers suggestions.
- Suggests capture start/stop, ROI adjustments, target transitions
- Advisory only — all safety-critical ops remain permanently local on StarMachine
- **Effort: Medium** — WebSocket bridge to Ollama API, prompt engineering

#### Phase 34 — Solar / Lunar Framing Assistant
Auto-center solar/lunar disk in frame. Extension of existing Phase 5 centroid tracking adapted for extended objects.
- Detects disk limb via edge detection, computes centroid of disk outline
- Drives mount corrections to keep disk centered as it transits
- Safety: sun filter detection prompt (cannot image sun without filter confirmation)
- **Effort: Hard** — edge detection for extended objects vs point sources

#### Phase 20 — Distribution
ASCOM camera/focuser/mount support for non-ZWO hardware. WiX installer. Auto-update.
- Makes RadukCapture shareable beyond the 8SE + ASI678MC configuration
- **Effort: Very Hard** — ASCOM COM interfaces, installer, update infrastructure

---

### 🚫 Far Future — Deferred

#### OBS Streaming (Phase 7)
RTSP stream from StarMachine GPU (NVENC / Windows Media Foundation) → OBS Media Source on Elminster. ~5–10 Mbps H.264.
- Deferred until home WiFi bandwidth confirmed sufficient for low-latency video
- No timeline — revisit after all Tier 1 and Tier 2 phases complete

---

### Phase Priority Summary

| # | Phase | Tier | Effort | Status |
|---|-------|------|--------|--------|
| 52 | Closed-Loop Tracking Emulation | 0 | Medium | ✅ validated (emu) 2026-07-11 |
| 51 | Guide Calibration & Axis Transform | 0 | Hard | ✅ validated (emu) 2026-07-11 |
| 53 | Correction-Loop Dynamics | 0 | Medium | ✅ validated (emu) 2026-07-11 |
| 54 | Auto-Sync Repair (ASTAP + J2000) | 0 | Easy-Med | ✅ validated (emu) 2026-07-11 |
| 55 | Rig Configuration | 0 | Easy-Med | ✅ validated (emu) 2026-07-11 |
| 56 | Frame-Path Memory Safety | 0 | Medium | ✅ validated (emu) 2026-07-11 |
| 57 | Mount-Safety & Remote Hardening | 0 | Medium | ✅ validated (emu) 2026-07-11 |
| 58 | Camera Fault Recovery | 0 | Medium | ✅ validated (emu) 2026-07-11 |
| 59 | Capture & Assist Reliability | 0 | Medium | ✅ validated (emu) 2026-07-11 |
| 12 | Auto-Exposure Assistant | 1 | Medium | ✅ v1.0.5 |
| 21 | Clipping Warning Overlay | 1 | Easy | ✅ v1.0.6 |
| 22 | Snapshot PNG/FITS | 1 | Easy | ✅ v1.0.7 |
| 25a | Quality Bar Graph | 1 | Easy | ✅ v1.0.7 |
| 41 | Pause Capture | 1 | Easy | ✅ v1.0.8 |
| 44 | Frame Type Labels + App Icon | 1 | Easy | ✅ v1.0.9 |
| 23 | RGB Align Display Mode | 1 | Easy-Med | ✅ v1.0.10 |
| 24 | Pixel Value Readout | 1 | Easy | ✅ v1.0.11 |
| 49 | Help System | 1 | Medium | 🔲 |
| 25 | Image Boost / Frame Stack FX | 1 | Medium | ✅ v1.0.12 |
| 26 | Night Vision Mode | 2 | Easy | 🔲 Next |
| 38 | Single-Channel Display Mode | 2 | Easy | 🔲 |
| 40 | Notification Bar | 2 | Easy | 🔲 |
| 45 | Multi-Reticle Overlays | 2 | Easy-Med | 🔲 |
| 39 | Framing Shot | 2 | Easy-Med | 🔲 |
| 29 | Ghosted Image Overlay | 2 | Easy | 🔲 |
| 13 | Sky Survey / Pointing Model | 2 | Medium | 🔲 |
| 14 | Session Planner | 2 | Medium | 🔲 |
| 37 | Display Stretch Mini Histogram | 2 | Medium | 🔲 |
| 46 | Connect/Disconnect All HW | 2 | Medium | 🔲 |
| 27 | Dark Frame Subtraction | 2 | Medium | 🔲 |
| 28 | Flat Frame Correction | 2 | Medium | 🔲 |
| 30 | Push-To Assistant | 2 | Medium | 🔲 |
| 35 | ADC Alignment Tool | 2 | Medium | 🔲 |
| 47 | Pixel Position / Click to Recentre | 2 | Medium | 🔲 |
| 48 | Goto Previous Image | 2 | Medium | 🔲 |
| 6.6 | GoTo Map Learning | 2 | Med-Hard | 🔲 |
| 42 | Two Monitor Mode | 2 | Hard | 🔲 |
| 16 | Polar Alignment Assistant | 2 | Hard | 🔲 |
| 43 | Deep Sky Image Annotation | 3 | Hard | 🔲 |
| 17 | Live Stacking Preview | 3 | Hard | 🔲 |
| 31 | Multi-Star FWHM Focus | 3 | Hard | 🔲 |
| 32 | Bahtinov Mask Focus | 3 | Hard | 🔲 |
| 18 | AVI/FITS Output | 3 | Medium | 🔲 |
| 19 | Multi-Session Stats Dashboard | 3 | Medium | 🔲 |
| 36 | Sensor Analysis / Smart Histogram | 3 | Hard | 🔲 |
| 33 | Stellar Photometry | 3 | Very Hard | 🔲 |
| 34 | Solar/Lunar Framing | 3 | Hard | 🔲 |
| 12b | Ollama Integration | 3 | Medium | 🔲 |
| 20 | Distribution / ASCOM | 3 | Very Hard | 🔲 |
| — | OBS Streaming | — | Hard | 🚫 Deferred |

---

*RadukCapture v1.0.12 — Built for the stars, from Anthem, Arizona*
### 6.3 Fault Tolerance & Auto-Recovery (Self-Healing)
Planetary imaging requires high-bandwidth USB stability. To prevent session loss during I/O stalls (e.g., drive swaps, OS bus contention), RadukCapture implements a hardware-level auto-reset state machine.

* **Detection:** The `CaptureThread` monitors consecutive `ASI_ERROR_TIMEOUT` returns.
* **Threshold:** If 20 consecutive frames fail to arrive (approx. 500ms latency), the thread flags a "Pipeline Wedge."
* **Recovery Sequence:**
    1.  The thread invokes `ASICamera::RestartCapture()`.
    2.  The SDK `StopVideoCapture` command is issued.
    3.  A **100ms thread sleep** occurs to allow the USB controller to flush and the camera's 256MB DDR3 buffer to clear.
    4.  `StartVideoCapture` is re-issued.
* **Telemetry:** Each reset event increments `recoveryResets` in the `CaptureStats` struct, which is broadcast via WebSocket to the APEX dashboard for real-time health monitoring.
