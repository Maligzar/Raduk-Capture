# Threading Model

Six main threads, each with a single owner and a single job, plus the
smaller ones listed under the table. The design goal is
that the capture thread never blocks on anything — not a mutex, not disk
I/O, not a serial port.

---

## The threads

```
+----------------------------------------------------------+
|                    MAIN / GUI THREAD                      |
|  Win32 window, Direct2D preview, control panel, HUD       |
|  Receives the latest frame for display (~30 fps)          |
+----------------+-----------------------+------------------+
                 | (events)              | (display frames)
                 v                       ^
+--------------------+    +---------------------------------+
|   CAPTURE THREAD   |    |      RING BUFFER (lock-free)    |
|  (RT priority)     |--->|      64 frame slots             |
|                    |    |      FREE -> WRITING -> READY   |
|  ASIGetVideoData() |    |      -> READING -> FREE         |
|  Timestamps frames |    +----------+----------+-----------+
|  Monitors drops    |               |          |
+--------------------+               v          v
                            +----------+  +---------------+
+--------------------+      | WRITER   |  | ANALYZER      |
|   MOUNT THREAD     |      | THREAD   |  | THREAD        |
|   (correction loop)|      |          |  | (below normal)|
|                    |      | SER file |  | Histogram     |
|   PulseGuide       |      | async    |  | Sharpness     |
|   Calibration      |      | drain    |  | Centroid      |
+--------------------+      | from RAM |  +-------+-------+
                            +----------+          |
+--------------------+                   +--------v------+
|  AUTOFOCUS THREAD  |                   | WEBSOCKET     |
|  (on demand)       |                   | THREAD        |
|  EAF sweep + fit   |                   | Telemetry     |
+--------------------+                   | every 500 ms  |
                                         +---------------+
```

| Thread | Owner | Priority | Cadence | Job |
|--------|-------|----------|---------|-----|
| Main / GUI | `Window::Run()` | Normal | ~30–40 fps | Message pump, D2D render, panel, readouts, callbacks |
| Capture | `CaptureThread` | Time-critical, optionally CPU-pinned | As fast as the sensor delivers | Frame → timestamp → frame sink (recording, ROI following) → ring slot |
| Writer | `CaptureManager::WriterLoop()` | Above normal | As fast as the disk allows | Drain `WriteBuffer` → `CaptureWriter` (SER or FITS) |
| Analyzer | `FrameAnalyzer::Run()` | Below normal | ~2 Hz effective | Histograms, sharpness, seeing, centroid |
| Correction | `CentroidTracker::Run()` | Above normal | 250 ms (locked) / 500 ms (centering) | Solve pixel error → `PulseGuide` |
| Autofocus | `AutofocusRoutine::ScanThread()` | Normal | On demand | EAF sweep, sample averaging, parabola fit |
| WebSocket | `WebSocketServer` | Normal | 500 ms broadcast | Accept clients, broadcast telemetry, receive commands |

Plus `PreviewStreamer` for dashboard pictures, `LeobenClient` for weather, the
NexStar poll and watchdog threads, and on-demand threads for ASTAP,
AutoStakkert, the assistant and the status window's drive scan.

`SiteLocator` uses a short-lived `std::async` for the Windows
Location query, so a denied or hanging consent prompt never blocks startup.

---

## Capture thread

**Priority.** Started with real-time priority and an optional CPU core pin
(`cpuCore = -1` disables pinning). It is the one thread whose latency
directly costs frames.

**What it does, per frame:** acquire a slot, call `ASIGetVideoData()` with
a timeout derived from the current exposure, stamp it with a QPC
microsecond timestamp and a monotonic frame number, run the frame sink,
commit the slot.

**Recording happens here.** The frame sink is called for every frame, before
the ring commit, and submits it to the `WriteBuffer`. Recording used to run
from the render loop, which took one frame per pass and topped out near
62 fps. The ring now serves only the preview and analyzer, which may skip
frames. Anything added to the sink must not block.

**Statistics it maintains** (`CaptureStats`, all atomic, safe to read from
any thread):

| Field | Meaning |
|-------|---------|
| `framesCaptured` | Monotonic count |
| `timeouts` | SDK returned no frame in the timeout window |
| `errors` | SDK error returns |
| `droppedFrames` | SDK-reported drops |
| `bufferOverruns` | Ring buffer had to overwrite an unread frame |
| `fps` | Measured, recomputed every 0.5 s |
| `restarts` | Pipeline-wedge auto-resets (Phase 58) |
| `cameraFault` | Camera removed, or unrecovered after retries |

The last two are the self-healing path: a wedged pipeline is detected and
restarted rather than left silently dead, and a camera that cannot be
recovered sets a fault flag the UI can report.

---

## Writer thread

Owned by `CaptureManager`, started at `StartRecording()` and joined at
`StopRecording()`. It drains the `WriteBuffer` sequentially and writes to
the active `CaptureWriter`, `SERWriter` or `FITSWriter`.

**It scores frames itself.** Since Phase 76 the writer thread computes each
frame's sharpness rather than trusting a live hint.
But it has no idea where the planet is, and measuring the frame centre
reads background noise whenever the planet is off centre — so the producer
passes `cropCX`/`cropCY` from the analyzer's last known centroid. The
centroid moves slowly relative to the 256 px crop, so a slightly stale
position still places the measurement well.

**Pause does not close the file.** `Pause()` makes `SubmitFrame()` skip
incoming frames while the writer keeps draining whatever is already
buffered, so the file stays open and valid.

---

## Analyzer thread

Below-normal priority, and **lossy on purpose**. `Submit()` copies the
frame under `m_frameMutex` and returns immediately; if the thread is busy
the frame is simply skipped.

> **Why lossy.** You always want the latest analysis result, never a
> backlog of stale work. A queue here would mean the centroid you correct
> against is from three frames ago, which is worse than having no
> correction this tick.

Results are published under `m_resultMutex` and read via `HasResult()` /
`GetResult()` from the render loop. `GetResult()` clears the ready flag,
which is what makes the "once per new result" rule below enforceable.

Effective rate is ~2 Hz, not frame rate. This matters downstream:
autofocus sample collection is paced by it (five samples take ~2.5 s, which
is why the final verification window is 8,000 ms and not 4,000 ms), and the
correction loop's input is a ~2 Hz signal regardless of a 40 fps preview.

---

## Correction thread

Started when the tracker leaves `Off`, stopped on `Reset()`. All shared
state passes through `CentroidTracker::m_mutex`; the correction thread
reads only via `GetSnapshot()`, which copies everything it needs in one
lock acquisition.

**Stage transition ownership is split, and it is documented because it
matters:**

| Transition | Runs on |
|------------|---------|
| To and from `Off` | Main thread |
| `Calibrating` → `Centering` (success) | Correction thread |
| `Calibrating` → `Searching` (failure/abort) | Correction thread |

The main thread only *observes* the middle two. This is why `m_stage` is an
atomic and `m_calPending` / `m_calSuccess` exist: the correction thread
completes a calibration and raises a flag, and `Tick()` on the main thread
fires the user-facing callback.

**The cadence is not what the constants suggest.** `S3_RATE_MS = 250` sets
the loop wake-up rate, but on the NexStar backend `PulseGuide` **blocks for
the pulse duration**. A locked cycle that fires both axes takes up to
`S3_RATE_MS + 2 × (maxPulse + 50)` ≈ 1.35 s worst case, so the effective
correction rate is ~1–4 Hz, not a fixed 4 Hz. See
[Tracking and Guiding](Tracking-and-Guiding).

---

## Autofocus thread

Created per scan, and **joinable rather than detached**.

> **Why it was changed.** Phase 59 found the scan thread was detached, so
> at shutdown it could outlive `RunApp()`'s locals and invoke a destroyed
> `std::function` or `ControlPanel`. It is now joined by the destructor,
> and `Abort()` makes that wait short. `Start()` also reaps the previous
> finished thread before spawning a new one.

The scan thread drives the focuser and sleeps; the *samples* come from the
main thread via `SubmitSharpness()`. Communication is through atomics and
one small mutex-guarded vector. The completion callback fires on the scan
thread, so it must not call Win32 UI functions — `main.cpp` uses
`PostMessage` or atomics from it.

---

## Thread-safety mechanisms, summarised

| Mechanism | Where | Guards against |
|-----------|-------|----------------|
| Lock-free CAS state machine | `RingBuffer`, `WriteBuffer` | Capture thread blocking; torn frames |
| Single mutex + snapshot | `CentroidTracker` | Partial reads of correlated state |
| Two mutexes, in/out | `FrameAnalyzer` | Producer and consumer contending on one lock |
| Atomics for all status | `CaptureStats`, `CaptureStatus`, `AFState` | Needing a lock to read a number for the HUD |
| `std::atomic<bool>` abort flags | Autofocus, calibration | Un-cancellable long operations |
| `PostMessage` from workers | Autofocus and WS command callbacks | Win32 calls off the GUI thread |
| Mutex-serialised file writes | `SessionLog`, `PointingModel` | Interleaved log lines |

---

## Invariants worth memorising

1. **Every `GetLatestFrame()` must be matched by a `ReleaseRead()`.** A
   leaked slot permanently reduces ring capacity and eventually starves the
   capture thread into dropping frames.
2. **`SubmitCentroid()` is called once per *new* analysis result.** Every
   call refreshes the centroid-loss timeout, so re-submitting a stale
   result defeats the 2 s safety cutoff and inflates the stable/dwell
   counters.
3. **The capture thread never waits.** If every slot is busy it drops the
   frame and increments a counter. Dropping a frame is correct; stalling
   the sensor is not.
4. **Nothing on a worker thread calls Win32 UI.** Including, and especially,
   completion callbacks.
5. **`mount->Heartbeat()` is called every render frame.** It feeds the
   NexStar runaway watchdog. A render loop that stalls stops the mount,
   which is the desired failure mode.

---

**Next:** [Capture Pipeline](Capture-Pipeline) · [Tracking and Guiding](Tracking-and-Guiding)
