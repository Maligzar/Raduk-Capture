# seeing-things/zwo — Comparison Against RadukCapture

Reviewed 2026-11-07 from a clone of `https://github.com/seeing-things/zwo`
(head `8ad701a`, "Replace tabs with spaces"). Everything below about that
repository was read directly from its source, not from secondary
descriptions. Everything about RadukCapture was read from this tree at
`5196025`.

## Bottom line

The zwo repository is mostly vendored ZWO SDK releases (0.7.0503 through
1.18) plus four small pieces of original work: `capture` (about 1,800
lines of C++), `zwo_fixer` (an SDK patch shim), SWIG Python bindings, and
reverse-engineering artifacts. It is a Linux research tool built for one
camera, the ASI178.

Its two headline techniques do **not** transfer to RadukCapture:

- `capture` bypasses `ASIGetVideoData` entirely and drives the camera's
  bulk endpoint with raw libusb transfers, after replaying the
  reverse-engineered vendor control sequence that starts streaming
  (`camera.cpp`, `start_streaming`). That sequence writes sensor register
  `0x3000` and FPGA register 0 for the IMX178. The ASI678MC is a different
  sensor (IMX678) with a different product ID, and RadukCapture runs on
  Windows, where the camera is bound to ZWO's driver rather than libusb.
  Porting this means re-reversing the 678's protocol and owning it through
  every firmware update. The gain claimed (`BANDWIDTHOVERLOAD` 100 instead
  of about 94 before drops, on the author's PC) is small and specific to
  that camera.
- `zwo_fixer` is a PLT hook on Linux ELF builds of specific old SDK
  versions. It fixes a real bug, a 500 ms stall in
  `CCameraFX3::startAsyncXfer` when `libusb_cancel_transfer` returns
  `LIBUSB_ERROR_NOT_FOUND`, by rewriting two SDK globals. It cannot run on
  Windows, and whether the current SDK still carries the bug is unknown.

RadukCapture is ahead on almost every other axis: capture-time
timestamps (zwo stamps at disk-write time, after queueing), a RAM write
buffer sized to the whole run, wedge and fault recovery, per-frame
quality scoring, a full emulator, and tests. zwo's AGC (a 1-percentile
servo on one scalar mapped to exposure then gain) is simpler than
`AutoExposureAssistant` and is not a candidate.

What zwo does better is narrower: it treats **the disk and the process
dying as normal events**. Three of its habits expose real gaps here, and
one of RadukCapture's own numbers turns out to be off by a factor of
twenty. Those four items are planned as
[Phase 133](../../docs/Phase133-Plan-Recording-Hardening.md).

## Side by side

| Concern | seeing-things/zwo `capture` | RadukCapture | Take |
|---|---|---|---|
| Frame acquisition | Raw libusb bulk transfers, 2 in flight, 100 ms timeout | `ASIGetVideoData`, timeout `2 x exposure + 500 ms` | Keep ours (portability, supportability) |
| Frame integrity | Checks sync words `0x7e5a` / `0xf03c` and the frame index in the raw USB payload | `ASIGetDroppedFrames` polled every loop | Not portable: the SDK strips those words. Ours is the equivalent available |
| Buffering | Pool of 64 refcounted frames, zero copy, fan-out by refcount | Preview ring plus a separate SPSC `WriteBuffer`, one memcpy per frame, sized to the whole run | Keep ours. The copy is about 0.3 GB/s at the worst format, far below memory bandwidth |
| Preview | Hand a frame to preview only when its queue is empty | Latest-frame ring | Equivalent |
| Thread priority | `SCHED_RR` 10 on camera and disk threads | Capture `THREAD_PRIORITY_TIME_CRITICAL` plus core pinning; writer `ABOVE_NORMAL` | Keep ours; the whole-run buffer makes writer priority mostly moot |
| Timestamps | Taken when the disk thread writes, so they include queue latency | Exposure midpoint estimated at arrival, on the capture thread | Ours is clearly better |
| SER header frame count | Header is `mmap`ed; `FrameCount` is live in the file after every frame | Patched once, in `Close()` | **Take theirs** (133.2) |
| Disk filling up mid-run | Checks free space every 100 frames; below 100 MiB it stops writing and keeps running | No in-run check. A failed write ends the writer thread but leaves the run "recording" | **Take theirs, and fix the bug it exposes** (133.1) |
| Empty file | Deleted on close if 0 frames were written | Kept | **Take theirs** (133.4) |
| Existing file | Asks before overwriting | Picks the next free run number | Ours is better for unattended runs |
| Wedge recovery | None (exits on `NO_DEVICE`) | Restart after 20 consecutive timeouts | Ours, with the threshold corrected (133.3) |

## What the comparison turned up in RadukCapture

### 1. A failed disk write does not stop the run

`CaptureManager::WriterLoop` (`src/core/CaptureManager.cpp`) breaks out
of its loop on the first failed `WriteFrame` and logs
"Disk write error — stopping". Nothing else happens. `m_status.recording`
stays true, so `SubmitFromCaptureThread` keeps pushing frames into a
`WriteBuffer` that nobody drains. With the Phase 76 sizing that buffer can
be half of free physical memory, so the app can go on "recording" into
RAM for the rest of the run, the panel shows a run in progress, and every
frame after the failure is lost. Autorun checks free space before each run;
a manual run checks nothing after it starts.

zwo avoids reaching that failure at all: it checks `statvfs` every 100
frames and stops writing at 100 MiB free, so the file it leaves behind is
intact.

### 2. A crash mid-run leaves a SER file that says it holds no frames

`SERWriter::Open` writes `FrameCount = 0` and only `Close()` patches it.
If the process dies, or the machine loses power, the file on disk has
valid frame data and a header that says zero frames. Most readers treat
that as empty. The timestamp trailer is also lost, which is unavoidable,
but the frames themselves are recoverable if the count is right.

zwo keeps the header memory-mapped and increments `FrameCount` in place
after each frame, so the count on disk is always current.

### 3. Wedge detection takes about ten seconds, not half a second

`CaptureThread::Run` restarts capture after `WEDGE_TIMEOUT_COUNT = 20`
consecutive timeouts, and the comment there says that is "~500ms of no
frames at planetary exposures". It is not. Each `GetFrame` call waits
`2 x exposure + 500 ms` before it times out, so 20 of them take at least
10 seconds (10.3 s at an 8 ms exposure). Architecture section 6.3 repeats
the claim without the number. For Jupiter that is a lost tenth of a
derotation window; for a satellite pass (Phases 116 to 118, 130, 131) it
can be most of the pass.

### 4. Zero-frame files accumulate

A run stopped before any frame passes the gate, or refused at its first
check, leaves a 178-byte SER behind. Harmless, but it clutters the
capture folder and the observation log points at it.

## Not taken, and why

- **Raw USB transport.** Covered above. If drops ever become the limiting
  problem on the ASI678MC, the order to try things is: lower
  `usb_bandwidth`, move the camera to its own USB 3 root controller, then
  measure whether `ASIGetVideoData` itself is the bottleneck. A custom
  transport is the last step, not the first.
- **Zero-copy refcounted pool.** Fewer copies, more shared ownership to
  get wrong, and the copy is not a measured cost here.
- **Realtime priority for the writer.** zwo needs it because its pool is
  64 frames. Ours holds the run.
- **Unbuffered `O_DIRECT`-style writes** (`write_benchmark.cpp`). The
  Windows equivalent is `FILE_FLAG_NO_BUFFERING`. The 2026-10-14
  measurement showed 141 to 423 MB/s dips, but with the buffer sized to
  the run those dips no longer reach the camera. Revisit only if a
  long unbounded run shows writer backlog.
- **Python bindings.** No consumer here.

## Small things worth stealing anyway

- zwo names every thread (`pthread_setname_np`). The Windows equivalent is
  `SetThreadDescription`, which makes capture, writer, analyser and
  WebSocket threads readable in Visual Studio, WPA and crash dumps. Five
  lines; folded into 133.5.
