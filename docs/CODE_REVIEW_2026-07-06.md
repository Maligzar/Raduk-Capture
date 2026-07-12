# RadukCapture — Object-Tracking End-to-End Review (2026-07-06)

Follow-up to `CODE_REVIEW_2026-06-10.md`. Scope: verify every claimed fix in the
object-tracking chain (FrameAnalyzer → CentroidTracker → IMount →
NexStar/ASCOM/Emulator → main.cpp wiring → GoTo/SkyMap/MountController), and
re-review the chain end-to-end for anything the first pass missed. The question
this review answers: **can the object-tracking feature actually function**, and
what stands between the current code and a working first hardware light.

---

## Part 1 — Verification of the 2026-06 fixes (tracking chain)

Every fix claimed in the T1–T16 batch was re-read against the current code.

| Finding | Status | Verified at |
|---------|--------|-------------|
| T1 PulseGuide malformed command | ✅ FIXED | `NexStarMount.cpp:317-343` — software-timed pulse (SlewFixed rate 1 → sleep → stop), 1000 ms hard cap, stop retried 3× then EmergencyStop |
| T2 Heartbeat downcast corrupts ASCOMMount | ✅ FIXED | `IMount.h:75` virtual no-op; `NexStarMount.h:99` override; `main.cpp:2049-2050` calls through interface |
| T4 Planet ephemeris geocentric sign | ✅ FIXED (both copies) | `MountController.h:336-345`, `SkyObject.h:155-169` — geocentric = helio_planet + geo_Sun |
| T5 STOP button doesn't stop tracker | ✅ FIXED | `main.cpp:1616-1636` — shared `doEmergencyStop` resets tracker, aborts AF, stops recording; re-registered on MountController |
| T7 Stale centroid re-submission | ✅ FIXED | `main.cpp:2128-2147` — submit gated on `freshAnalysis`; counters now count analyses, 2 s loss cutoff live |
| T8 Panel jog fights guide loop | ✅ FIXED | `main.cpp:819-850` — generation-counter suspend/resume; wired to MountController, ControlPanel and keyboard jog |
| T9 Capture-loss leaves mount slewing | ✅ FIXED | `MountController.h:629-633` WM_CAPTURECHANGED; `ControlPanel.cpp:810`; stop sent unconditionally (`main.cpp:840-845, 1863-1866`) |
| T10 RAW16 `>>4` aliasing | ✅ FIXED | `FrameAnalyzer.h` — all four read sites now `>> 8` |
| T11 Second acquisition cycle mis-crops ROI | ✅ FIXED | `main.cpp:1517-1539` — `roiStartX/Y` added, clamped to `cam.MaxWidth()/MaxHeight()` |
| T12 Auto-acquisition needs visible panel | ✅ FIXED | `GoToPanel.h:280-284` `TickSlewStatus()`; `main.cpp:2309-2312` ticks it when hidden |
| T14 Centroid search central 50% only | ✅ FIXED | `FrameAnalyzer.h:276-278` — full-frame search |
| T15 Dwell callback never fires | ✅ FIXED | `CentroidTracker.h:193-196` — `m_dwellPending` set at threshold |
| T16 Rate buttons cosmetic | ✅ FIXED | jog callback carries `rate` (`MountController.h:620`, `main.cpp:833-837`); keyboard path uses `controls.JogRate()` |
| T6 Watchdog doesn't cover correction thread | ✅ MOSTLY CLOSED by T7 | If the main thread freezes, submissions stop → 2 s centroid timeout → correction thread stops pulsing. Residual: a wedged correction thread mid-pulse is covered by the stop-retry/EmergencyStop in T1's fix |

The T1–T16 batch is real and correctly implemented. The tracking chain is
substantially safer than at the June review.

---

## Part 2 — What is still broken (ordered by impact on tracking)

### E1. CRITICAL (new) — The tracking loop has never been closed, even in emulation
`CameraEmulator.h:331-376` renders Jupiter pinned to the ROI centre
(`CX = W*0.5f, CY = H*0.5f`) at all times. `MountEmulator::PulseGuide`
(`MountEmulator.h:255-270`) nudges internal RA/Dec that **nothing feeds back
into the frame generator**. Consequences:

- In emulation the planet is always already centred: Stage 2 dwell is met
  instantly, Stage 3 corrections never exceed the deadband, and the
  loss/reacquire path never runs. Tracking "passes" trivially no matter what
  the correction logic does.
- A sign flip, axis swap, or gain error in the correction loop — the exact
  class of bug that killed T1/T3 — is **invisible** in every test the project
  can currently run. This is the structural reason tracking has stayed the
  biggest hurdle: there is no rig on which the feature can fail before a live
  night on real hardware.
- The header comment ("planet disk that drifts slowly across the sensor") does
  not match the code — drift/loss handling is untested too.

Fix: couple the emulators. CameraEmulator takes a pointing-error input derived
from MountEmulator's (commanded − target) position, renders the planet at
centre + error × plate-scale, and lets the emulator inject drift, seeing
jitter, and a configurable camera-rotation angle. Then the full
Search→Center→Lock→correct→lose→reacquire cycle can be exercised, including
deliberately wrong axis orientations. Pair with the June review's
"adversarial emulator" items (blocking GetVideoFrame, dropped frames,
`ASI_ERROR_CAMERA_REMOVED` injection — C5).

### T3. CRITICAL (unchanged) — No guide calibration
`CentroidTracker.h:314-341` still assumes +X→East / +Y→North, a fixed
`PLATE_SCALE_ARCSEC_PX = 0.203`, and `MS_PER_ARCSEC = 100`. On the alt-az
NexStar 8SE the sensor↔mount mapping rotates continuously with the field; a
wrong sign turns the proportional loop into positive feedback. Additionally the
NexStar pulse path slews **Az/Alt axes** while the tracker computes **RA/Dec
errors** — without a measured transform these are only aligned by luck.
Deliberately deferred in June; it remains the single most important change
before live testing. (PHD2-style: pulse East, measure displacement vector,
pulse North, measure; derive the 2×2 transform + ms/arcsec; re-derive as
altitude changes; scale azimuth by 1/cos(alt).)

### T13. HIGH (unchanged) — Correction-loop dynamics chase the seeing
`CentroidTracker.h:69-85, 325-352`. Deadbands (1″/2″) sit below typical
planetary centroid jitter (2–5″); no centroid averaging before correcting;
pulses issue sequentially with blocking sleeps (`ms + 50` each) so a Locked
cycle can take ~1.35 s, not the stated 4 Hz. The loop will fire pulses at noise
and under-deliver on rate. Average the centroid over ~0.5–2 s, size the
deadband from measured jitter, and stop sleeping the loop through pulse
delivery.

### S2. HIGH (unchanged) — Auto-sync-on-lock can never succeed
`ASTAPSolver.h:182-197` still parses `SOLVEAD=`/`SOLVEDEC=`/`FOVX=`; ASTAP
writes `PLTSOLVD=T`, `CRVAL1/2`, `CDELT1/2`, `CROTA2`. Every solve reports
failure, so the Stage-3 auto-sync (`main.cpp:1556-1566`) and the pointing model
silently do nothing. Timed-out solves still leave a zombie astap.exe and reused
temp filenames let a stale result poison the next solve.

### C1. HIGH (unchanged) — RingBuffer producer/consumer race feeds torn frames to the analyzer
`RingBuffer.h:98-121`. On CAS failure the producer loads state and, unless it
saw READING, unconditionally stores WRITING — the GUI can CAS READY→READING in
between; the skip path also desyncs `m_writePos`. Torn frames become garbage
centroids fed straight into the correction loop. Fires constantly once the ring
wraps at real frame rates.

### G5. HIGH (unchanged) — SkyMap confirm popup can slew to the wrong object
`SkyMapWindow.h` — the popup still stores an index into `m_objects`, which the
30 s timer / zoom / pan rebuild while the popup is open. Mount-safety issue.

### S1. HIGH (unchanged) — WebSocket server: unauthenticated mount control + remote crash
`WebSocketServer.h:159, 328-373`. Binds INADDR_ANY, no auth, and its command
set jogs/slews the mount (`main.cpp:1972+`). 64-bit `payLen` is still added and
`resize()`d unclamped — one malicious frame terminates the app (or worse, the
`hdrLen+maskLen+payLen` sum wraps). `recvBuf` growth is unbounded.

### G3. MEDIUM (unchanged) — `load_profile` remote command fused into `jog`
`main.cpp:1972-2006`. There is still no `else if (rc.cmd == "load_profile")` —
the profile-matching block sits inside the `jog` handler. APEX `load_profile`
does nothing; every remote jog tries to load a profile named "N"/"S"/"E"/"W".

### C4. MEDIUM — Camera removal: tracking now stops, but the app spins and never recovers
`ASICamera.cpp:123-125`, `CaptureThread.cpp`. Error codes still collapse to
bool; `ASI_ERROR_CAMERA_REMOVED` busy-spins a TIME_CRITICAL thread.
`RestartCapture()` (`ASICamera.cpp:116`) **has zero callers** — the
"Pipeline Wedge" auto-recovery described in Architecture §6.3 is documented
but not implemented. (Mitigated for the mount: T7's fix means the 2 s centroid
timeout halts corrections when frames stop.)

### T17. MEDIUM (unchanged) — Hardcoded rig constants
Plate scale (`CentroidTracker.h:67`), site lat/long (`main.cpp:683`,
`MountController.h`), sensor size. A Barlow/bin change silently breaks pulse
sizing and ROI math. Needs one persisted rig-config struct.

### T18/S5 remnants (unchanged, low-medium)
- `NexStarMount::Disconnect` (`NexStarMount.cpp:106-108`) closes the serial
  handle without `m_serialMutex` — races an in-flight `ReadFile` on the
  correction/UI threads.
- Fire-and-forget `P` acks still pollute the RX buffer; a `#` arriving after
  the next command's purge intermittently corrupts poll parsing.
- `MountEmulator.h:28` still defines a class literally named `NexStarMount`
  (ODR landmine), and the stale duplicate `src/core/SkyMapWindow.h` still
  exists alongside `src/gui/SkyMapWindow.h`.
- `NexStarMount.h:15` comment says 3000 ms watchdog; the constant is 8000 ms.
- S5: planet/Moon positions are equinox-of-date but consumed as J2000 (~22′ in
  2026) — matters once plate-solve auto-sync (S2) starts working.
- Emulator `GetVideoFrame` semantics remain opposite the real SDK (C5) —
  non-blocking, re-serves frames, tolerates size mismatches.

Parts 2–4 of the June review (capture pipeline, GUI, sky/net details) otherwise
stand as written; G1/G2 (Stack-FX heap overflow, rubber-band ROI use-after-free)
remain open and are in the frame path the tracker depends on.

---

## Verdict

The June blocker batch was applied correctly: the command layer can now
physically deliver a timed guide pulse, the safety paths (STOP, capture loss,
jog contention, stale centroids) are sound, and acquisition state flows
correctly through GoTo → settle → Search → Center → Lock.

Tracking still cannot be *trusted* to function, for two reasons:

1. **The correction geometry is assumed, not measured** (T3) — on an alt-az
   mount the current fixed mapping is guaranteed wrong at most orientations,
   and a wrong sign is a runaway.
2. **No environment exists where the loop can be tested closed** (E1) — the
   emulator renders the planet at centre regardless of mount motion, so every
   remaining geometry/dynamics bug stays invisible until a live night.

Roadmap phases 51–58 (Architecture doc, "Tier 0 — Object-Tracking
Reliability") sequence the fixes: close the loop in emulation first (52), then
calibration (51), dynamics (53), auto-sync (54), rig config (55), and the
memory/safety/recovery set (56–58).
