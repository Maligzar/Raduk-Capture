# Phase 146 plan: the real camera's throughput, measured indoors

The owner chose this on 2026-11-13, from the 2026-10-19 comparison's top item: "Measure the real camera over real USB." Status: `PLANNED`.

## Where it stands
- **The write path is proven; the camera is not.** The 293 MB/s run with zero drops (2026-10-17, `9f886d6`, clear-night item 13) was the camera *emulator* at 3840×2160 RAW8, writing real files to the real disk. It proved our side keeps up, at the disk's ceiling of 292 MB/s. It said nothing about a real ASI678MC over real USB. The comparison's throughput score still rests on the emulator.
- **It does not need a clear night.** Frame rate and drops over USB do not depend on what the sensor sees. A capped OTA, or the camera off the scope with its cap on, streams at full rate as long as the exposure is short.

## What "honest drop accounting" means here
A frame can be lost in four places, and today only two are counted.

| Where | Counted now? | Source |
|---|---|---|
| The camera or USB (the SDK never delivered it) | Yes, as a running total | `ASIGetDroppedFrames` (`CaptureThread.cpp:235`) |
| Our capture thread (buffer full) | Yes | `CaptureManager` stats |
| The writer (disk) | Since 135.1, a failed write stops the run | `DiskStop` |
| Silently: a gap in the frame timestamps the SDK does not report | **No** | Only visible in the SER's timestamp trailer |

## Stages
- **146.0 The drop ledger.**
  - At the end of every run, one session-log line and four observation-record fields:
    - `frames_written`;
    - `dropped_sdk` (the SDK's count at the end minus at the start, so earlier runs are not charged);
    - `dropped_buffer`;
    - `timestamp_gaps`: intervals over 1.5× the run's median interval, with the frames they imply missing.
  - Gaps are counted from the writer's own timestamps as frames arrive, so it costs nothing extra.
  - Gated by a test feeding synthetic timestamps: steady, one gap of 3 intervals, jitter under the threshold, and a counter reset between runs.
- **146.1 `tools/ser_drop_audit`.**
  - Reads any SER's timestamp trailer and reports frames, the median and effective fps, the gaps and the implied missing frames.
  - It works on files from the night of 2026-11-07, so we learn something before 146.2 runs at all.
  - Gated on a synthetic SER with known gaps.
- **146.2 The measurement (the owner, indoors, about 30 minutes).** A written procedure in the help and the checklist:
  - Settings: FULL 3840×2160, RAW8, high-speed mode on, USB bandwidth at the SDK's ceiling, exposure 0.1 ms (so frame rate, not exposure, limits), the camera on its own USB 3 host controller (the machine has two, Intel and ASMedia).
  - Three 60-second runs. Then 1280×960 RAW8 and 3840×2160 RAW16 for the shape of the curve.
  - Record the effective fps, MB/s and all four drop counts.
  - The ZWO spec sheet's maximum frame rate is the comparison. Look it up at the time; it is not stated here from memory.
- **146.3 Write it down.** Add the table to PHASE-STATUS, mark clear-night item 13's remaining question answered, and update the comparison's throughput score with the measured figure.

## Risks
- **USB 3 behaviour depends on the controller and the cable.** If the first run drops frames, try the other controller and a shorter cable before blaming the code. Record which was used.
- **The emulator's 37 fps at full frame was disk-bound, not camera-bound.** The real camera may be faster than the disk can write: 3840×2160 RAW8 at about 47 fps is about 390 MB/s, above the 292 MB/s disk ceiling (inferred). Then the write buffer fills, and 146.0's `dropped_buffer` count says so honestly. That is the right answer, not a failure of the measurement.


## Future cameras and deep sky (the owner's direction, 2026-11-13)
Built to the rules in [Phase 149](Phase149-Plan-Any-ZWO-Camera.md#rules-for-every-phase-from-146-on): no camera model, resolution, bit depth or plate scale in code; mono and colour alike; frames that may take minutes.
- The drop ledger counts per camera and per run, keyed by `CameraInfo.name`, so a second camera, a guide camera, or a successor gets its own figures.
- The audit tool reads any SER, mono or colour, at any bit depth.
- The procedure is written for "the camera's full sensor at its fastest mode", not for 3840×2160. The table keeps a row per camera.
- The ledger tells a transfer-limited run (frames arrive as fast as USB allows) from an exposure-limited one (each frame waits on its exposure), so the same accounting holds for a deep-sky sequence, where only drops and gaps matter, not frame rate.

## Versioning
Every PR bumps PATCH; the PR that completes the phase bumps MINOR ([`ROADMAP-Next.md`](ROADMAP-Next.md#next-in-order)). 146.0 and 146.1 are one PR (PATCH). 146.3 completes the phase (MINOR).
