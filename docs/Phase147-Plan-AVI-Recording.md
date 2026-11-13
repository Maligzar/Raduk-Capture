# Phase 147 plan: uncompressed AVI recording

The owner asked for this on 2026-11-13: uncompressed AVI, as a recording format beside SER and FITS. This was roadmap row 18 ("AVI output, unasked for") until now. Status: `PLANNED`.

## Why uncompressed, and what AVI cannot do
- **Lossy codecs throw away the fine detail lucky imaging stacks**, so planetary AVI is raw frames in a RIFF container. AutoStakkert, RegiStax, PIPP and WinJUPOS read it (recalled; to be confirmed by opening a file in each, 147.3).
- **AVI has no per-frame timestamps**, only a nominal frame rate. SER carries the true time of every frame, and that is what derotation and WinJUPOS measurement want. So AVI is for compatibility. The timestamps are not lost: they go to the quality sidecar and the observation record, as they already do for every format.
- **AVI is 8-bit in practice.** 16-bit uncompressed AVI has no FOURCC most tools read. A RAW16 run with AVI selected is refused before it starts, with the reason ("AVI holds 8-bit; choose SER or FITS for 16-bit"). It never silently drops the low byte.

## Stages
- **147.0 `AVIWriter`** (`src/core/AVIWriter.h/.cpp`), implementing the existing `ICaptureWriter` (`CaptureWriter.h:234`), so `CaptureManager`, the sidecar, the observation record, 135.1's disk checks and 135.4's empty-run removal all apply unchanged.
  - RAW8 Bayer and mono: `Y800` (8-bit grey, `BI_RGB`-compatible header). The Bayer pattern is written into the file name and the sidecar, since AVI cannot carry it; AutoStakkert debayers on request.
  - RGB24: `BI_RGB` 24-bit, rows bottom-up, BGR order, each row padded to 4 bytes.
  - Over 1 GB: OpenDML (AVI 2.0). The first `RIFF AVI ` stays under 1 GB with an `idx1` for old readers. Later data goes in `RIFF AVIX` chunks, indexed by `indx` super-indexes and `ix00` standard indexes, and `dmlh` carries the true frame count. A full-sensor run is several GB, so this is not optional.
  - 135.2's live header: the frame counts (`avih.dwTotalFrames`, `strh.dwLength`, `dmlh`) are patched once a second, so a crash leaves a file that says how many frames it holds. The indexes are written at close, as SER's trailer is; a crashed AVI loses its index but not its frames, which matches SER.
- **147.1 The selector.**
  - `CaptureFormat` gains `AVI` (`CaptureManager.h:38`).
  - The toolbar's SER/FITS choice cycles SER, FITS, AVI, saved as `CaptureFormat=avi` in rig.ini as the others are.
  - `[STACK]` hands AVIs to AutoStakkert as it does SERs.
- **147.2 Tests.** New `tests/avi_writer.cpp`:
  - writes N frames and parses the RIFF back: `avih`, `strh`, `strf`, the `movi` chunk sizes and the `idx1` offsets;
  - each frame's bytes round-trip;
  - RGB24 rows flipped and in BGR order;
  - the OpenDML path, driven with a test-only RIFF size limit of a few MB so it runs in seconds, checking that `AVIX`, `indx`/`ix00` and `dmlh` agree with the frame count;
  - the live header patched mid-run;
  - RAW16 refused.
- **147.3 Readers.** One AVI each of RAW8 and RGB24, at 1280×960 and at full sensor, opened in AutoStakkert, PIPP and VirtualDub (whatever the owner has). A clear-night or emulator-run checklist item, because no reader runs in CI.

## Not in scope
- **A `[TO AVI]` converter.** Not asked. The writer makes it a small addition later.
- **Compressed AVI.**

## Versioning
Every PR bumps PATCH; the PR that completes the phase bumps MINOR ([`ROADMAP-Next.md`](ROADMAP-Next.md#next-in-order)). 147.0 to 147.2 can be one PR (MINOR, since it completes the phase's code); 147.3 is a checklist item.
