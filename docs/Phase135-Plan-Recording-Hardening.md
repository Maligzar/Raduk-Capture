# Phase 135 plan: recording that survives the disk and the process

Planned 2026-11-07. Source: the comparison against `seeing-things/zwo` in
[`seeing-things-zwo.md`](../research_notes/Astronomy%20capture%20software%20comparison/seeing-things-zwo.md).
**Renumbered 2026-11-11.** Written as Phase 133 in PR #91, but 133 is the
field-proof phase on `master`, so it is 135 here and PR #91 is superseded
by the PR that carried this file. **Re-checked against `master`
(`2fb9c5e`) on 2026-11-11: every defect below is still present.**
`CaptureManager::WriterLoop` still breaks on a failed `WriteFrame` and
leaves the run recording; `SERWriter` still writes `FrameCount = 0` and
patches it only in `Close()`; `CaptureThread` still counts 20 timeouts.

**Ranked 2026-11-11: 135.1 first on the bench** (category 1, it loses a
night's frames silently). 135.2 to 135.5 follow the roadmap's live list.

Each stage is one PR with its own test, and `master` stays field-ready
between them.

## 135.1 A failed write ends the run, and the disk is watched during it

**Defect.** `CaptureManager::WriterLoop` exits on a failed `WriteFrame`
but leaves `m_status.recording` true. The capture thread keeps filling a
`WriteBuffer` that is no longer drained, the UI reports a live run, and
every later frame is lost.

**Change.**

- Add `std::atomic<bool> m_writeFailed`. The writer sets it before it
  breaks. `SubmitFromCaptureThread` returns false when it is set, and the
  render loop treats it like `m_limitReached`: stop through the one
  `endRecording` path so USB polling and the observation record behave as
  for any other stop. The stop reason is "disk write failed", shown, not
  just logged.
- In the writer, check free space on the output volume once a second
  (time based, not every N frames, since frame size varies by 24x across
  formats). Below a floor, stop cleanly: close the file so header, trailer
  and sidecar are written, then raise the same flag with reason
  "disk nearly full". Floor: the larger of 256 MiB and the bytes still
  pending in the `WriteBuffer` plus the trailer, so the frames already
  captured always fit.
- `GetDiskFreeSpaceExW` runs on the writer thread, never the capture
  thread.

**Test.** `tests/capture_writer.cpp`: a writer stub that fails on frame N
sets the flag, the run stops, and no frame is submitted after it. The
floor arithmetic is a pure function (`DiskFloorBytes(pending, frameBytes,
frames)`) checked at its edges.

## 135.2 The SER header is right on disk while the run is going

**Defect.** `FrameCount` is written as 0 at open and patched only in
`Close()`. A crash or power loss leaves a file of valid frames whose header
says it has none.

**Change.** In `SERWriter::WriteFrame`, once a second (by the frame's own
timestamp, so no extra clock calls), `fflush`, seek to offset 38, write the
count, seek back to the end. Cost is one small write per second. The
trailer is still written only at close; a crash loses timestamps, not
frames. Same treatment for `FITSWriter` if its header has an equivalent
count (check `NAXIS3`).

zwo's approach is a memory-mapped header. That is not worth it here:
`MapViewOfFile` on a file also being appended through the CRT mixes two
I/O paths for one 4-byte field.

**Test.** Write K frames without calling `Close()`, read the header back
from disk with a separate handle, and check `FrameCount` is within one
second's worth of K.

## 135.3 Wedge detection measured in time, not timeouts

**Defect.** `CaptureThread::Run` restarts after 20 consecutive timeouts
and its comment says that is about 500 ms. Each timeout is
`2 x exposure + 500 ms`, so the real figure is at least 10 s. Architecture
section 6.3 states the behavior without the number.

**Change.** Track the time of the last good frame. Declare a wedge when
nothing has arrived for `max(1.5 s, 10 x expected frame interval)`, where
the expected interval is `1 / measured fps` or the exposure, whichever is
longer. Keep the per-call SDK timeout as it is: it is ZWO's own guidance
and shortening it risks false timeouts at long exposures. Instead, make
the loop check the wedge clock after each timeout.

That alone only helps if a single call cannot outlast the wedge budget, so
cap the per-call timeout at the wedge budget too. At planetary exposures
that is about 1.5 s against today's 516 ms per call, so recovery comes in
1.5 to 2 s instead of 10.

Correct the comment and architecture section 6.3 to give the measured
figure.

**Test.** New `tests/capture_wedge.cpp`. No existing test drives
`CaptureThread::Run` itself (`capture_thread_submit.cpp` covers
`CaptureManager`'s side), so this needs an `ICamera` stub that stops
delivering frames on command. Assert the restart happens within the
budget, and that a long-exposure configuration (lunar or deep sky, seconds
per frame) does not restart spuriously. Pulling the budget arithmetic into
a pure `WedgeBudgetMs(exposureMs, fps)` keeps most of the checking out of
the threaded test.

**Owes a clear night.** Whether 1.5 s is long enough to ride out a USB
hiccup without a needless restart can only be learned on the real camera.
Add it to `CLEAR-NIGHT-CHECKLIST.md`.

## 135.4 Zero-frame files are removed

**Change.** In `CaptureManager::StopRecording`, after `Close()`, if
`framesRecorded == 0`, delete the file and its sidecar, skip the WinJUPOS
rename, and do not write an observation record pointing at it. Log what was
deleted.

**Test.** Start and stop with the gate closed; the output directory is
unchanged.

## 135.5 Thread names

`SetThreadDescription` on the capture, writer, analyser, WebSocket and
emulator threads. No test; check once in the Visual Studio threads window.

## Explicitly out of scope

The raw libusb transport and the SDK patch shim from the zwo repository.
Reasons are in the comparison note.
