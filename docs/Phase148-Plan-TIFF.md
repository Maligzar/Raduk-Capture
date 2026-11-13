# Phase 148 plan: TIFF, three ways

The owner asked for this on 2026-11-13, choosing all three uses:
- a 16-bit snapshot and stack export;
- TIFF-sequence recording;
- SER to TIFF conversion.

Status: `PLANNED`.

## One writer under all three
**148.0 `TiffWriter.h`.** Pure code: no COM, no WIC, so it is tested with g++ here as the rest of the pure headers are. It writes baseline TIFF 6.0, little-endian (`II*`), uncompressed, one strip per image:
- 8 or 16 bits a sample;
- grey (Photometric 1, BlackIsZero) or RGB (Photometric 2, PlanarConfig 1);
- tags: ImageWidth and ImageLength, BitsPerSample, Compression 1, Photometric, StripOffsets, SamplesPerPixel, RowsPerStrip, StripByteCounts, X and Y Resolution, ResolutionUnit, Software (with the app's version), DateTime;
- ImageDescription carrying the target, the mid-exposure UTC, exposure, gain, and the Bayer pattern when the data is not debayered.

RAW16 is written at its native left-aligned values; nothing is rescaled.

Why not WIC's TIFF encoder: it would work, but it cannot be tested off Windows, and the format is simple enough that a hand-written baseline writer is about 150 lines. In CI, WIC reads every file this writer produces, as an independent check that real readers accept it.

## Stages
- **148.1 The 16-bit snapshot.**
  - Today `SaveSnapshotPNG` (`PreviewRenderer.cpp:1705`) saves the *display*: 8-bit, stretched, debayered for the eye. A TIFF snapshot saves the *data* instead: the raw frame, debayered linearly (no stretch, no gamma), 16-bit RGB (48 bits a pixel), or the undebayered Bayer frame as 16-bit grey when the preview is in RAW mode.
  - A RAW8 source is written as 16-bit with its values unchanged (×1), and the description says so, so the file is not mistaken for more depth than it has.
  - A new key beside the PNG snapshot's, assigned when built and listed in the help and the command palette.
- **148.2 TIFF-sequence recording.**
  - A `TiffSequenceWriter` implementing `ICaptureWriter`: a folder per run, `<name>_tif\<name>_000001.tif` and on, each frame a TIFF with its own mid-exposure time in DateTime and ImageDescription.
  - The quality sidecar and observation record as usual. 135.4's empty-run removal deletes the folder.
  - `CaptureFormat` gains `TIF`, and the selector cycles SER, FITS, AVI, TIF.
  - **Risk:** one file per frame. Opening and closing about 50 files a second at full sensor may be what limits throughput, not the bytes. Measure it with the emulator at full sensor (the P4.3 method). If it falls below SER's rate, the selector warns before a run starts, giving the measured figure.
- **148.3 `[TO TIF]`.**
  - Beside `[TO SER]` and built on the same background batch pattern (`FitsBatch.h`, `main.cpp:6030`): pick SER files, get one TIFF folder each.
  - Frames are kept as recorded: Bayer as grey with the pattern noted, 16-bit kept as 16-bit, timestamps from the SER trailer. An optional debayer is left for later.
  - A command-line twin, `tools/ser2tif`, like `fits2ser`.
- **148.4 The stack's output.** AutoStakkert already writes its stacked result itself, into the run's `_AS` folder (`AutoStakkertRunner.h:267`), as TIFF by default (recalled). Confirm by listing that folder after a stack. If it is TIFF, nothing to build, and the help says where to find it. If not, pass AutoStakkert's own output-format option, once its command line is confirmed.

## Tests
New `tests/tiff_writer.cpp`:
- writes 8- and 16-bit grey and RGB and reads the IFD back;
- checks each required tag's type and value, the strip bytes and odd widths;
- checks that 16-bit values round-trip exactly.

Plus `TiffSequenceWriter` through the same harness as `capture_writer` (empty run, a crash mid-run leaving whole frames), and `ser2tif` on a synthetic SER. In CI, WIC opens each test file.

## Versioning
Every PR bumps PATCH; the PR that completes the phase bumps MINOR ([`ROADMAP-Next.md`](ROADMAP-Next.md#next-in-order)). Suggested PRs: 148.0 with 148.1 (PATCH), 148.2 (PATCH), then 148.3 with 148.4, which completes the phase (MINOR).
