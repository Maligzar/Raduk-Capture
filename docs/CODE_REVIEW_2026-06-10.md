# RadukCapture — Complete Code Review (2026-06-10)

Scope: full `src/` tree, CMake configuration, and emulator-vs-hardware divergence.
Context: the app has only ever run against the emulators (`build_emu`); first
hardware light has not happened. Findings are therefore weighted toward bugs that
the emulator masks and that will only appear live. Object tracking
(CentroidTracker → IMount → NexStar/ASCOM) was reviewed line-by-line as the
stated highest-risk area.

Severity: **CRITICAL** = will corrupt memory, runaway the mount, or make the
feature non-functional on hardware. **HIGH** = likely live failure or safety gap.
**MEDIUM** = wrong results / robustness gap. **LOW** = minor or latent.

> **Fix status (this branch):** T1, T2, T4, T5, T7 (also closes most of T6),
> T8, T9, T10, T11, T12, T14, T15, T16 are FIXED in the commits on this PR.
> T3 (guide calibration) is deliberately deferred to its own change — it is a
> new routine, not a patch. Parts 2-4 findings are not yet addressed except
> where noted.

---

## Part 1 — Object tracking chain

### T1. CRITICAL — `NexStarMount::PulseGuide` sends a malformed command; guide pulses cannot work
`src/core/NexStarMount.cpp:312-332`

The command built is `{'P', 1, axis, 36|37, hi, lo, 0, 0}`:

- Byte 1 (message length) is `1`, which tells the hand controller to forward
  **only the message ID** (byte 3) to the motor controller. The two duration
  bytes placed at positions 4–5 are never forwarded.
- Message IDs 36/37 (`0x24`/`0x25`) are `MC_MOVE_POS`/`MC_MOVE_NEG` — the
  *fixed-rate slew* commands, which expect a **rate** byte, not a duration.
  Sent with no data byte, behavior is firmware-dependent: ignored at best,
  or interpreted as a move with an unspecified rate at worst — and nothing
  ever sends a timed stop.

So on real hardware, Stage 2/3 corrections either do nothing (planet drifts
out, tracker loops on "lost") or start untimed motion. The emulator hides this
completely because `MountEmulator::PulseGuide` just nudges its internal
position (`MountEmulator.h:256-271`).

Fix options:
- Use the real NexStar AUX guide command: `{'P', 4, axis, 0x26, rate, dur, 0, 0}`
  where `rate` is signed percent-of-sidereal and `dur` is in **10 ms units,
  max 2550 ms** (newer HC firmware only) — then read and discard the `#` ack.
- Or implement software-timed pulses: `SlewFixed(axis, dir, 1)` → sleep →
  `StopSlew(axis)`, which works on all firmware.
Also note `IsPulseGuiding()` is hardcoded `false` (`NexStarMount.h:88`), so the
"check before sending next" contract in `IMount.h:62` is unenforceable.

### T2. CRITICAL — Unconditional downcast to `NexStarMount` corrupts memory on the CPWI/ASCOM path
`src/main.cpp:1981-1984`

```cpp
if (mount && mount->IsConnected())
    static_cast<raduk::core::NexStarMount*>(mount)->Heartbeat();
```

`mount` is an `IMount*` that can point at an `ASCOMMount` (the *primary*
hardware path per `DeviceFactory.h:10-16`). `Heartbeat()` is non-virtual and
writes an 8-byte atomic at `NexStarMount`'s member offset — into an
`ASCOMMount` object, every render tick. That is undefined behavior /
silent memory corruption in exactly the configuration recommended for Phase 5
tracking. (In emulator builds it happens to be safe only because
`MountEmulator.h:28` names its class `NexStarMount`.)

Fix: add `virtual void Heartbeat() {}` to `IMount` and override it in
`NexStarMount`, or `dynamic_cast`/store a typed pointer at connect time.

### T3. CRITICAL — No guide calibration: pixel→direction mapping is assumed, not measured
`src/core/CentroidTracker.h:303-330`

The correction loop assumes:
- +X pixel error → East pulse, +Y error → North pulse (fixed),
- a fixed plate scale (`PLATE_SCALE_ARCSEC_PX = 0.203`, line 67),
- a fixed pulse-rate conversion (`MS_PER_ARCSEC = 100`, line 314).

On real hardware the mapping between sensor axes and mount axes depends on
camera rotation in the focuser, diagonal/mirror flips, pier side, and — on an
alt-az NexStar 8SE — **field rotation that changes continuously with Az/Alt**.
If either axis's sign is wrong, the proportional loop is positive feedback:
each pulse increases the error until the planet leaves the ROI. There is no
way to even configure the sign.

Fix: add a calibration step at the start of Centering (PHD2-style): pulse East
for N ms, measure centroid displacement vector, pulse North, measure again;
derive the 2×2 transform (and effective ms/arcsec) from the response. On an
alt-az mount, re-derive or rotate the transform as altitude changes, and scale
azimuth corrections by 1/cos(alt). This is the single most important change
before live testing.

### T4. CRITICAL — Planet ephemeris geocentric conversion has a sign error (two copies)
`src/gui/MountController.h:336-340` and `src/sky/SkyObject.h:156-166`

Both planet-position implementations convert heliocentric → geocentric with the
Earth on the **wrong side of the Sun**:

```cpp
double sX = -sR*cos(sunLon*D2R);   // this is Earth's heliocentric position
double gx = xecl + sX;             // helio_planet + helio_Earth  ← wrong
```

Geocentric = helio_planet − helio_Earth (equivalently helio_planet + geo_Sun),
i.e. the sign of `sX/sY` (or the `+`) must flip. The error vector is ~2 AU:
tens of degrees of sky error for Venus/Mars, several degrees for Jupiter/Saturn.

Consequences for tracking specifically:
- GoTo to a planet misses by far more than the FOV → acquisition never sees a
  centroid.
- Worse: the **ALIGN** popup (`MountController.h:353+`) lets the user center a
  planet and then calls `SyncRADec()` with these wrong coordinates — poisoning
  the mount's alignment model for everything afterwards.

The Moon code is separate (already geocentric) and unaffected by this bug.

### T5. HIGH — The big red STOP button does not stop the tracker
`src/gui/MountController.h:130-136`, `src/main.cpp:813-816`

`MountController::EmergencyStop()` stops both axes and fires the callback —
which in main.cpp **only logs**. If Stage 2/3 corrections are active, the
correction thread issues the next `PulseGuide` within 250–500 ms and the mount
moves again after the user pressed STOP on the always-on-top safety panel.
The ESC path gets this right (`main.cpp:1559-1569` calls `tracker.Reset()`);
the panel path must do the same.

### T6. HIGH — Watchdog does not cover the correction thread
`src/core/NexStarMount.cpp:454-493`, `src/core/CentroidTracker.h:262-334`

The heartbeat is fed by the main render loop (`main.cpp:1983`). If the main
thread freezes, the watchdog fires `EmergencyStop()` — but the CentroidTracker
correction thread is a *separate* thread that is still alive and keeps sending
pulses, re-moving the mount after every watchdog stop. The watchdog should
also set a flag the tracker checks (or `CentroidTracker` should require a
recent main-thread heartbeat before pulsing).

### T7. HIGH — Render loop re-submits stale centroids; loss timeout defeated, counters inflated
`src/main.cpp:2058-2068`, `src/core/CentroidTracker.h:161-193`

`SubmitCentroid()` is called **every render tick** with `lastAnalysis`, which
only changes when the analyzer publishes a new result. Effects:

- `m_lastCentroidTime` is refreshed with stale data each tick, so the
  `CENTROID_TIMEOUT_MS` (2 s) safety can never fire while the render loop runs.
  If the camera stalls (USB drop — common live), the tracker keeps pulsing
  toward a frozen centroid indefinitely.
- `STABLE_FRAMES = 5` / `DWELL_FRAMES = 8` count render ticks (~60 Hz), not
  distinct analyses (~2–10 Hz). "Stable for 5 frames" is effectively
  "one analysis result", gutting the stage-gating logic.

Fix: only submit when `analyzer.HasResult()` delivered a new result this tick
(move the submit inside the `GetResult()` branch).

### T8. HIGH — Jogging from the MountController panel fights the active guide loop
`src/main.cpp:803-812` vs `src/main.cpp:1790-1804`

The keyboard jog path suspends corrections and resumes after a 500 ms settle.
The MountController panel jog callback does neither — during Stage 2/3 the
correction thread fires pulses against the user's jog. Also:

- The resume path spawns a detached 500 ms timer per release
  (`main.cpp:1798-1801`); press → release → press again within 500 ms and the
  first timer resumes corrections **mid-jog**. Use a generation counter or a
  single resettable timer.

### T9. HIGH — Mouse-capture loss leaves the mount slewing (all three jog surfaces)
`src/gui/MountController.h:599-615`, `src/gui/ControlPanel.cpp:795-818`,
`src/gui/Window.cpp:400-421`

Jog stop relies exclusively on `WM_LBUTTONUP` / `WM_KEYUP`:

- Neither button subclass handles `WM_CAPTURECHANGED`/`WM_CANCELMODE`. If
  capture is stolen mid-press (Alt-Tab, popup, system menu, screen-off), the
  stop is never sent. The per-frame heartbeat keeps the watchdog fed, so it
  won't save you.
- `MountController` `WM_LBUTTONUP` only sends stop `if (IsConnected())` —
  a transient connection-flag drop mid-jog also leaves the slew running.
  Always send the stop unconditionally.
- ControlPanel's `WM_MOUSELEAVE` case is dead code (`TrackMouseEvent` never
  called); keyboard jog has no `WM_KILLFOCUS` failsafe.

### T10. HIGH — 16-bit frames: `(uint8_t)(value >> 4)` aliases — analyzer output garbage in RAW16
`src/core/FrameAnalyzer.h:168-174, 278-287`

ASI RAW16 data is left-aligned 0–65535; `>> 4` leaves 0–4095 and the `uint8_t`
cast then wraps (4096→0, 4351→255, …). Histograms, mean, clip %, and the
centroid's first-pass `rmax`/threshold become meaningless for any moderately
bright pixel. Should be `>> 8`. (Latent today since the app drives RAW8, but
it will bite the moment RAW16 is enabled — and the emulator's half-written
RAW16 frames mask it, see C12.)

### T11. HIGH — Second acquisition cycle crops the ROI in the wrong place
`src/main.cpp:1469-1491`

`doROILock` treats the centroid as full-sensor coordinates and clamps against
a hardcoded 3840×2160. After a lost-centroid reset, the ROI is deliberately
left cropped (`main.cpp:1527`), so the next Searching→Centering→ROI cycle
computes `startX/startY` from **ROI-relative** centroid coordinates and
`SetStartPos` places the crop wrong (planet likely outside it). Either add the
current ROI origin to the centroid, or restore full frame on Reset. Also query
the sensor size from `ASI_CAMERA_INFO` instead of hardcoding 3840×2160.

### T12. HIGH — Auto-acquisition after GoTo only works while the GoTo panel is visible
`src/main.cpp:2232-2233`, `src/gui/GoToPanel.h:1255-1265`

`m_onGoToComplete` (which arms the 3-s settle → `WM_USER+10` → auto
StartSearching) fires from `GoToPanel::Tick()`, but main.cpp only ticks the
panel `if (gotoPanel.IsVisible())` — and it is hidden by default. The
advertised sky-map-click flow therefore never auto-starts acquisition, and a
stale `m_lastSlewing == true` fires a spurious "SLEW COMPLETE" the next time
the panel is opened, arming acquisition unexpectedly. Tick the slew-status
state machine unconditionally.

### T13. MEDIUM — Control-loop dynamics will "chase the seeing"
`src/core/CentroidTracker.h:69-85, 303-330`

- Deadbands (1″/2″) are well below typical seeing-induced planetary centroid
  jitter (2–5″), so at 2–4 Hz the loop will fire pulses at noise constantly.
  Planetary trackers normally average the centroid over ~0.5–2 s before
  correcting.
- The pulses are issued sequentially with blocking sleeps (`ms + 50` each),
  so a Locked-stage cycle can take ~1.35 s, not the stated 4 Hz; the comment
  and constants disagree with the achievable rate.
- `MS_PER_ARCSEC = 100` assumes a specific guide rate; it should fall out of
  the T3 calibration rather than being a constant.

### T14. MEDIUM — Centroid search only covers the central 50% of the frame
`src/core/FrameAnalyzer.h:272-273`

Stage 1 is documented as "full frame" (`CentroidTracker.h:9-10`), but the
analyzer searches x∈[w/4, 3w/4], y∈[h/4, 3h/4]. After a GoTo with a few
arcmin of pointing error (guaranteed — see T4 and the ~1-2° ephemeris
accuracy), the planet can easily sit in the outer 75% of the area and never be
acquired. Search the full frame during Searching, or at least make the region
configurable.

### T15. MEDIUM — Dwell callback can never fire
`src/core/CentroidTracker.h:207, 361`

`m_dwellPending` is consumed in `Tick()` and reset in `_ResetCounters()` but
**never set** anywhere, so `SetDwellCallback` (`main.cpp:1531-1534`) is dead.
Harmless today (the toolbar polls `IsDwellMet()`), but the dead path invites
someone to rely on it later. Either set it in `SubmitCentroid` when the dwell
threshold is first met, or delete the callback.

### T16. MEDIUM — MountController rate buttons are cosmetic
`src/gui/MountController.h:41-43, 669-672, 763`, `src/main.cpp:806`

`m_jogRate` is set by the 9x/FAST/MED/FINE buttons but never passed to the jog
callback; main.cpp hardcodes `SlewFixed(axis, dir, 9)`. A user who selects
FINE gets a full-speed slam at rate 9. Pass the rate through the callback.

### T17. MEDIUM — Hardcoded site, sensor, and rig constants
`src/gui/MountController.h:226-234, 368`, `src/main.cpp:673, 1475-1476`,
`src/core/CentroidTracker.h:67`

The site latitude/longitude, 3840×2160 sensor, and the
0.203″/px plate scale are scattered as literals. Fine for one rig, but they
should live in one config struct so a Barlow/bin change doesn't silently break
pulse sizing (T3/T13) and ROI math (T11). (The `ApproxLST` math itself is
correct, including the west-longitude sign.)

### T18. LOW — Misc tracking-chain notes
- `NexStarMount.h:50` comment says 3 s watchdog; the constant is 8000 ms.
  Header comment at line 8 says poll every 2 s; `PollLoop` sleeps 4 s.
- Fire-and-forget `P` commands leave the HC's `#` acks in the RX buffer; the
  next `SendCommand`'s `PurgeComm` usually clears them, but an ack arriving
  *after* the purge intermittently corrupts the next reply parse (poll
  failures, wrong `IsSlewing`). Consider draining acks or sequencing pulses
  through the poll thread.
- `Disconnect()` closes the serial handle without holding `m_serialMutex`
  (`NexStarMount.cpp:107`); a concurrent UI call (e.g. `GetAltAz` from the
  detached thread at `main.cpp:2273-2278`) can race a `ReadFile` against
  `CloseHandle`.
- `ASCOMMount` IDispatch calls are made from the correction thread without
  `CoInitializeEx` on that thread (implicit-MTA usually saves you on Win10+,
  but it's worth initializing explicitly).
- `MountEmulator.h:28` defining a class literally named `NexStarMount` is an
  ODR landmine — one stray include of both headers and the build (or worse,
  the linker) breaks. Rename to `MountEmulator` and `using NexStarMount = …`
  under the ifdef if needed.

---

## Part 2 — Capture pipeline

### C1. CRITICAL — RingBuffer producer stomps slots the consumer is acquiring
`src/core/RingBuffer.h:104-127`

When the producer's CAS fails it loads the state and, unless it sees
`READING`, does an unconditional `state.store(WRITING)` — but the GUI thread's
`GetLatestFrame` can CAS `READY→READING` between that load and store. Result:
torn frames in the preview *and* in the SER file (CaptureManager memcpys from
the slot), and the consumer's later `ReleaseRead` stores `FREE` over the
producer's `WRITING`. Once the ring wraps (after the first 64 frames) every
acquisition takes this path, so at real frame rates this fires constantly.
The skip-to-`(pos+1)` path also desyncs `m_writePos` (the "latest" index then
points at the skipped/old slot, and the next acquire immediately overwrites
the newest frame).

### C2. HIGH — SER timestamps record disk-write time, not capture time
`src/core/SERWriter.h:124-137`

`WriteFrame` ignores its `timestampUs` parameter and stamps
`CurrentFileTimeUTC()` when the writer thread drains the frame — up to ~10 s
late with a 512-slot backlog, with bursts of near-identical stamps. For
planetary work (WinJUPOS derotation) the trailer is then unusable. Note
`FrameSlot::timestamp` is QPC-since-boot, so a boot-time↔FILETIME correlation
needs to be added to plumb true capture time through. (Header layout,
.NET-epoch constant, and FrameCount patch offset were verified correct.)

### C3. HIGH — Write buffer not reallocated on frame-size change; allocation failure ignored
`src/core/CaptureManager.h:85-89`

The guard checks only slot *count*, never per-slot capacity: record at a small
ROI, enlarge, record again → every `Push` fails the capacity check and you get
a zero-frame SER with console spam only. Also `Allocate()`'s return is
discarded; at 512 slots × full frame (~4.2 GB) a failed VirtualAlloc means
recording "starts" but drops everything.

### C4. HIGH — Camera removal becomes an unrecoverable TIME_CRITICAL busy-spin
`src/core/ASICamera.cpp:123-125`, `src/core/CaptureThread.cpp:148-152`

`GetVideoFrame` collapses all `ASI_ERROR_CODE`s to bool, so
`ASI_ERROR_CAMERA_REMOVED` (returns immediately) is indistinguishable from a
timeout; the capture loop then spins flat-out on a pinned core at
`THREAD_PRIORITY_TIME_CRITICAL`. USB drops are routine with ASI cameras.
`RestartCapture()` exists but is never called. Handle error codes: back off,
attempt restart, surface a fault to the UI.

### C5. HIGH — Emulator semantics are opposite to the real SDK
`src/core/CameraEmulator.h:219-240`

Emulator `GetVideoFrame` is non-blocking and re-serves the same frame (real
`ASIGetVideoData` blocks for a new frame, delivered once), returns success on
buffer-size mismatches the SDK rejects, pins ROI to 1280×960 while reporting
success, and generates only `W*H` bytes in RAW16 mode (half of each frame is
stale garbage). Everything timing-related — FPS, backpressure, ring overwrite,
ROI changes — has been validated under a regime that cannot occur live.

### C6. MEDIUM — Pipeline correctness details
- `CameraEmulator.h:430,169,213,220,346` — `m_isCapturing`/`m_isOpen`/
  `m_gain`/`m_exposureUs` are plain bools/ints shared across three threads
  (UB; `StopVideoCapture`'s join can hang under optimization).
- `Timestamp.h:23-24` — `QuadPart * 1000000ULL` overflows after ~21 days of
  uptime at 10 MHz QPC (sooner at TSC rates); use div/mod form.
- `CaptureManager.h:162-167` — frame limit checks frames *written*, while the
  queued backlog is fully drained on stop: a 1000-frame limit can yield ~1200.
- `CaptureManager.h:92` + `SERWriter.h:96-99` — ColorID hardcoded BAYER_RGGB
  (ignore `ASI_CAMERA_INFO::BayerPattern`/`IsColorCam`); `LittleEndian=1`
  conflicts with the de-facto SER convention (FireCapture writes 0) — bites
  when RAW16 lands.
- `GPUDebayer.h:164-167` — `CreateSharedBitmap` from a texture on a different
  D3D11 device than the HWND target's is unsupported; on real machines the GPU
  path will silently fail and the CPU fallback masks it. `_DEBUG` builds also
  require SDK debug layers (`D3D11_CREATE_DEVICE_DEBUG`) or the device fails.
- Low: writer reads `slot->dataSize` after `Pop()` (stats races);
  `StopRecording` check-then-act not atomic; `Close()` ignores fwrite/fseek
  results (disk-full → FrameCount=0 file); `CreateDirectoryA` creates only the
  leaf dir; overrun stats are dead/meaningless; stub field `IsColor` vs SDK's
  `IsColorCam` means shared code only compiles in one flavor.

---

## Part 3 — GUI layer

### G1. CRITICAL — Heap overflow when frame size changes with Stack FX active
`src/gui/PreviewRenderer.cpp:116-119, 491, 1081-1097`

`SetFrameSize()` memsets `m_fxAccum` with the *new* `w*h*3*4` size over a
buffer allocated for the old size and never reallocates `m_fxAccum`/
`m_fxFrames[]`; growing the ROI with Stack3/5/10 on writes far past the
allocations.

### G2. CRITICAL — Rubber-band ROI auto-apply frees the frame the render loop is using
`src/gui/PreviewRenderer.cpp:308-316` → `src/main.cpp:1244-1296, 2191`

The 2-second auto-apply fires `m_onROIChange` from inside `RenderFrame()`
while main.cpp still holds the `FrameSlot*` from `GetLatestFrame()`. The
callback calls `ring.Allocate()` (which VirtualFrees all slot data and
deletes the slot array); the render loop then calls `ring.ReleaseRead(frame)`
into freed memory. The button/remote ROI paths are safe; only this path is
broken — defer the callback (PostMessage) instead of invoking mid-render.

### G3. HIGH — `load_profile` remote command is fused into the `jog` branch
`src/main.cpp:1908-1942`

There is no `else if (rc.cmd == "load_profile")` — the profile-matching code
sits inside the `jog` handler. APEX `load_profile` does nothing, and every jog
additionally attempts to load a profile named "N"/"S"/"E"/"W".

### G4. HIGH — Autofocus completion callback runs GUI code on a detached worker thread
`src/main.cpp:1352-1370`, `src/core/AutofocusRoutine.h:144-155, 489`

The callback calls `controls.SetAutofocusRunning/Status` (blocking
cross-thread `WM_SETTEXT` + non-atomic members the main thread reads), and
`Abort()` only detaches — at shutdown the scan thread can outlive `RunApp()`
locals and invoke a destroyed `std::function`/ControlPanel (use-after-free).
Same detached-thread-captures-`this` pattern: GoToPanel's 15-minute
`SurveyThread` (`GoToPanel.h:755, 786-867`), `AIAssistant::Cancel`
(`AIAssistant.h:74-84`), and the short-lived helpers at
`main.cpp:730, 1798, 2273`.

### G5. HIGH — SkyMap confirm popup can slew to the wrong object / out-of-bounds index
`src/gui/SkyMapWindow.h:855-860, 906, 977, 1014`

`m_confirmIdx` indexes `m_objects`, which is rebuilt by the 30-s timer, wheel
zoom, and pan release while the popup is open; the SLEW handler does
`m_objects[m_confirmIdx]` with only a `>= 0` check. Mount-safety issue: store
the object (or name+coords) in the popup, not an index.

### G6. MEDIUM — GUI robustness details
- `PreviewRenderer.cpp:419-424` — D2D device-lost (`D2DERR_RECREATE_TARGET`)
  only printf'd; preview permanently black after a GPU reset (SkyMapWindow
  handles this correctly).
- `GoToPanel.h:1441-1446, 346-387` — ASTAP worker thread calls
  `SetWindowTextW` (the "thread-safe" comment is wrong — it's a blocking
  cross-thread SendMessage) and writes `m_hasSolution`/`m_solvedRA`/… that
  `DoSync()` reads unguarded on the main thread.
- `main.cpp:950-951` — `SetClassLongPtrW(GCLP_HBRBACKGROUND)` on the shared
  `"STATIC"` class changes every static control in the process + leaks a brush
  per editor open.
- `src/core/SkyMapWindow.h` is a stale 851-line copy of
  `src/gui/SkyMapWindow.h` defining the same class, and `src/core` is on the
  include path — delete it before an unqualified include picks it up (ODR).
- `ControlPanel.cpp:2434-2453` — two HFONTs leaked per collapse/expand;
  `ControlPanel.cpp:1163-1165` — readout loop overwrites the focuser GO TO
  edit box every 500 ms while the user types.
- `main.cpp:1116-1122` — profile-editor modal loop swallows `WM_QUIT`.
- Low: `A` (autofocus) key handler never wired in Window.cpp (help advertises
  it; help also claims Space = emergency stop but only ESC is handled);
  `WM_USER+2` posted but never handled (auto-sync flash never appears);
  switching stack depths doesn't zero `m_fxAccum` (stale over-bright average);
  ToolbarStrip rebuilds all buttons on every WM_MOVE losing cached states;
  startup dialog X-close launches hardware mode instead of quitting
  (`main.cpp:409-412, 2556-2569`); `SurveyProvideFrame` chains a one-shot
  auto-solve wrapper that is never removed (`GoToPanel.h:170-177`).

---

## Part 4 — Sky / net / focus / profiles

### S1. CRITICAL — WebSocket server: remote overflow + wide-open hardware control
`src/net/WebSocketServer.h:159, 338-366`

- 64-bit `payLen` from the wire is added unchecked into `totalLen` (integer
  wrap), then `payload.resize(payLen)` throws (terminate) or the unmask loop
  reads out of bounds — one malicious frame kills the app. Clamp `payLen`
  (e.g. 1 MB) before any arithmetic; also cap `recvBuf` growth (memory DoS).
- The server binds `INADDR_ANY` with **no authentication**, and its command
  set slews the mount and moves the focuser. Bind 127.0.0.1 by default or
  require a token in the handshake.

### S2. HIGH — ASTAP integration parses keys ASTAP never writes; timed-out solves leave zombies
`src/sky/ASTAPSolver.h:156-163, 185-204`

The result parser looks for `SOLVEAD=`/`SOLVEDEC=`/`FOVX=`; ASTAP writes
`PLTSOLVD=T`, `CRVAL1/2`, `CDELT1/2`, `CROTA2` — so every solve reports
failure, breaking auto-sync-on-lock and the pointing model. On the 30-s
timeout the process is never `TerminateProcess`'d, and the fixed temp names
(`rc_solve.bmp/.ini`) let the orphan's stale output be read by the *next*
solve. Also the RA hint is hours but the doc comment says degrees.

### S3. HIGH — Autofocus parabola fit accepts upward-opening fits
`src/core/AutofocusRoutine.h:399-413`

Only `|a| < 1e-10` is rejected; with noisy data `a > 0` makes the vertex the
sharpness *minimum* — the routine slews to maximum defocus and reports
success. Require `a < 0`.

### S4. HIGH — Auto-exposure HCG floor raises gain on already-bright low-gain profiles
`src/core/AutoExposureAssistant.h:128-143, 199-216`

When too bright at min exposure with gain below 182 (Venus/Moon profiles),
`std::clamp(newGain, 182, 570)` snaps gain *up*, brightening a clipped image;
the emergency-clip branch only ever cuts exposure, so a clipped image at
`expMinMs` is unrecoverable. Make the HCG floor advisory, not a clamp.

### S5. MEDIUM — Astronomy correctness details
- `src/sky/SkyObject.h:12, 120-177` — planet/Sun/Moon positions are
  equinox-of-date but consumed as J2000 (~22′ error in 2026 — ~3× the solver's
  0.13° FOV when mixed with J2000 plate solves and the J2000 catalog).
- `PointingModel.h` — `m_sessionEntries` never populated
  (`BestSessionError()` always 0); azimuth wraparound ignored in the neighbor
  metric (corrections near due north use wrong neighbors); `sprintf_s` with
  unbounded object name can terminate the process; `CreateDirectoryW` only
  creates the leaf dir.
- `SkyObject.h:516-525` — Moon's `magnitude` overwritten with phase (0–1),
  breaking magnitude sorting/filtering; `gmtime()` shared-static + unchecked
  NULL.
- `ObjectKnowledgeDB.h:127-132` — `std::stoi` on unbounded digits throws
  uncaught (`m9999999999999` in chat crashes the app).
- `ProfileManager.h:137-165` — INI values never clamped against
  `CameraRails`; corrupted profile passes gain=99999 / img_type=42 straight
  toward the SDK.
- `AIAssistant.h:87-99, 217-224` — `IsOllamaRunning()` always true
  (`WinHttpConnect` doesn't contact the server); end-of-stream spins the retry
  loop for 120 s.
- Low: WS pongs don't echo payload / unmasked frames accepted (RFC 6455);
  `Broadcast()` holds the client mutex across blocking `send`s; ADC pressure
  constant is for 850 m not 528 m; APEX JSON mis-escapes UTF-8;
  `EAFEmulator` `m_isOpen`/`m_target` races; autofocus `nSteps==1` divides by
  zero; AI context says 2.9 µm pixels vs 2.0 µm elsewhere.

---

## Suggested order of attack before first hardware light

1. **T1 + T3** — make `PulseGuide` actually pulse, and add guide calibration.
   Without both, Stage 2/3 cannot work live; with wrong direction it actively
   runs away.
2. **T2** — fix the `Heartbeat()` downcast (one-line `virtual` fix) before any
  CPWI session.
3. **Safety set: T5, T6, T8, T9, G5** — every path that can leave the mount
   moving against the user's intent.
4. **T4 + S2** — planet ephemeris sign and ASTAP key parsing, so GoTo can put
   the planet in the FOV and auto-sync can verify it.
5. **C1, G1, G2** — the memory-safety trio in the frame path.
6. **T7, T11, T12** — acquisition flow correctness (stale resubmission, ROI
   re-crop, hidden-panel state machine).
7. **S1** — clamp the WebSocket frame length and bind localhost before using
   the dashboard anywhere but a trusted LAN.
8. Everything else as listed.

A final structural note: the emulator masks nearly every finding in Parts 1–2
(`PulseGuide` nudges position directly, `GetVideoFrame` never blocks, ROI is
pinned, errors collapse to bool). Before live testing it is worth an
"adversarial emulator" pass — make the emulator block like the SDK, drop
frames, inject `CAMERA_REMOVED`, randomize guide-axis orientation — so the
control loop and error paths can be exercised without risking the mount.
