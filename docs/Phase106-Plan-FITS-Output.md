# Phase 106 — FITS output

Roadmap row 18, priority M. The row says "FITS leads, AVI follows". This
phase is FITS only: AVI is a legacy container the post-processing chain
does not want, and it stays on the row until someone asks for it.

**Closed at the bench 2026-11-01; what it owes is its row in `PHASE-STATUS.md`.** The sections below are the plan as it was revised during the work.

**State when written, 2026-11-01:** `PLANNED`. No FITS code exists in
`src/`. The `FITS:` comment the observation record emits (phase 48) is
the only mention, and it was put there for this phase.

---

## 1. What the code decides before any design does

Read from the tree, not assumed. Each of these changes the shape of the
phase.

| Finding | Where | Consequence |
|---|---|---|
| The observation source fires **after** the writer opens, and the sky figures only exist at the end | `CaptureManager.cpp:77`, `:96`, `:168` | The header cannot be written at `Open()`. It has to be reserved there and filled at `Close()` |
| The sky accumulator **and** the quality sidecar live inside `SERWriter` | `SERWriter.h:67`, `SERWriter.cpp:138` | A second writer loses both unless they move out first. The sidecar is not a SER feature |
| The tick arithmetic is private to `SERWriter` | `SERWriter.cpp:86` | The QPC-to-wall-clock correlation has to be shared, or two writers hold two definitions of time, which is what phase 105 just removed one of |
| `CaptureManager` holds a concrete `SERWriter`, and `SkySummary` is its nested type | `CaptureManager.h:260` | A seam is needed. It sits on the writer thread beside a disk write, so a virtual call costs nothing worth measuring. Unlike P5.2, this is not the hot path |
| The record says `colour = "RGGB"` unconditionally, and `Open()` is called with the Bayer default | `CaptureManager.cpp:77`, `:95` | Fine while every format is Bayer. FITS writes that claim into a keyword consumers act on, so it must come from the frame's real type, for both formats |
| `doStackFile` hands any finished path to AutoStakkert when `autoStackOnStop` is set | `main.cpp:3792` | AutoStakkert's own accepted inputs are avi/ser/as3/dat and image formats (`SERWriter.cpp:125`), so a FITS cube must be presumed unreadable until tried. The hand-off must not fail silently on it |
| `.ser` is hardcoded in the name builder and assumed in two readers | `CaptureManager.cpp:395`, `AIContextBuilder.h:794`, `main.cpp:3019` | The first is required for this phase. The other two mean a FITS run would be invisible to the assistant and to framing recall |
| `CaptureBasePath` strips any extension, not just `.ser` | `ObservationRecord.h:151` | The sidecars and framing recall's own path logic already work. One thing that needs no change |

---

## 2. Decisions

**One cube per run, not one file per frame.** A run is thousands of
frames, and thousands of small files per capture at this frame rate is
a filesystem load the write path has never been measured against. A
cube is also the direct analogue of a SER, including patching the count
at the end.

**The header is reserved at `Open()` and written at `Close()`.** A fixed
run of 2880-byte blocks, enough for every keyword below, blank until
`Close()`. Only `Close()` holds the complete record: the end time, the
altitude range and the airmass. `NAXIS3` is known then too.

**A run that dies mid-capture.** SER leaves frames with `FrameCount = 0`,
recoverable by reading the file size. To match that, `Open()` writes a
minimal valid header at once (`NAXIS3 = 0`), and `Close()` rewrites it.
Recovery tooling is out of scope. Not leaving an unidentifiable file is
not.

**Byte order is done on a scratch buffer on the writer thread, never in
the slot.** FITS is big-endian and its 16-bit integers are signed, so
unsigned data is stored with `BZERO = 32768`: swap, then flip the sign
bit. `Peek()` gives a `const` slot. RAW8 needs none of it (`BITPIX = 8`
is unsigned), so RAW8 FITS should cost what SER costs. RAW16 is the only
case with a new cost, and section 5 measures it.

**`ROWORDER = 'TOP-DOWN'` is written, and the pixels are not flipped.**
FITS's traditional reading is bottom-up, and a consumer that assumes it
reads `BAYERPAT` against the wrong row and swaps red for blue. This is
the same question as phase 47b's row-order flag and clear-night item 15,
which is still unsettled against a real solve. The keyword states what
was written. It settles nothing about how a solver numbers rows.

**`BAYERPAT` needs no offset.** Checked in 106.1: the ROI placement
forces the start to a multiple of 8 across and 2 down (`main.cpp:3283`),
so the first pixel is always the sensor's own `RGGB` and
`XBAYROFF`/`YBAYROFF` are not written.

**Timestamps go in a binary table extension after the image.** Without
this FITS would lose the per-frame times that make SER usable for
derotation. Two columns, one row per frame:

| Column | Type | |
|---|---|---|
| `MJD_MID` | `D` | UTC midpoint of integration, as P5.1 stamps it. A double at this MJD resolves about 0.6 us |
| `SHARPNESS` | `E` | The same score as `_quality.txt`, which is still written |

**`DATE-OBS` is the start of the exposure, not the stamp.** That is what
the FITS convention means by it, and ours are midpoints since P5.1. So
`DATE-OBS` is the first midpoint minus half the exposure, `DATE-END` is
the last plus half, and `DATE-AVG` is written too. A comment card names
the convention, the requirement P5.1 set for every file format.

**Unknown values are omitted, never zero.** The observation record's
rule, carried over unchanged.

**The site is written at full precision.** The owner's call, 2026-11-01.
The one-decimal rule in `tests/ai_context_facts.cpp` is about what the
assistant may repeat, and it stays where it is.

**No person's name goes in the file.** The owner's call, 2026-11-01.
`OBSERVER` is not written at all, because names do not need to be in
files that travel. `TELESCOP`, `INSTRUME` and `SWCREATE` name equipment
and software, and they stay.

**The reserve is filled with blank cards before `END`, not after it.** A
reader starts the data at the block after the one holding `END`, so
blank blocks after `END` would be read as pixels. `END` has to land in
the last reserved block.

**SER stays the default**, and the setting is global. A profile does not
own it, for the reason given on 2026-10-11 about the capture format:
startup retains the last settings.

**`FOCALLEN` is not written.** `RigConfig` stores only the plate scale,
which has never been checked against the sky, and deriving a focal
length from it would state an unverified figure as a fact.

### Keywords

| Keyword | From | Unit |
|---|---|---|
| `SIMPLE`, `BITPIX`, `NAXIS`, `NAXIS1-3`, `BZERO`, `BSCALE` | frame | |
| `EXTEND = T` | fixed: the table follows | |
| `DATE-OBS`, `DATE-END`, `DATE-AVG` | frame stamps | ISO 8601 UTC |
| `EXPTIME` | `exposureMs` | s |
| `OBJECT`, `FRAME`/`IMAGETYP` | `target`, `frameType` | |
| `GAIN`, `XBINNING`, `YBINNING` | camera | |
| `XPIXSZ`, `YPIXSZ` | the camera's `Describe()` | um |
| `XORGSUBF`, `YORGSUBF` | `roiX`, `roiY` | px |
| `BAYERPAT`, `ROWORDER` | frame type | |
| `RA`, `DEC`, `OBJCTALT`, `OBJCTAZ`, `AIRMASS` | mount, sky summary | deg |
| `SITELAT`, `SITELONG` | `RigConfig` | deg |
| `FOCUSPOS`, `FOCUSTEM` | focuser | steps, C |
| `INSTRUME`, `TELESCOP`, `SWCREATE` | as the SER header; version | |

The instrument strings are hardcoded defaults on `SERWriter::Open`. They
move to one shared place rather than being copied, and they become
`ICamera`'s answer where it has one.

`RA` and `DEC` are where the mount says it pointed, unsolved. A comment
card says so, because a consumer will otherwise take them for a WCS. No
`CRVAL`/`CD` keywords are written.

---

## 3. Stages

**106.1 · The format, and nothing else.** `src/core/FITSFormat.h`, pure
like `ObservationRecord.h`: no Windows, no file. Card formatting, string
quoting (doubled single quotes, ASCII only, the 68-character limit),
block padding, the header from a record, the sample swap, the table.
`tests/fits_format.cpp` gates it.

**106.2 · The seam.** `ICaptureWriter` with `Open`, `WriteFrame`,
`Close`, `Path`, and a shared `SkyAccumulator`, `FrameClock` and quality
sidecar writer pulled out of `SERWriter`. No format is added and **SER
output must be byte-identical before and after**, apart from its own
`DateTime` stamps. This lands on its own, because it touches the path
that has never lost a frame.

**106.3 · The writer.** `FITSWriter` over 106.1 and 106.2. `Open()`
reserves, `WriteFrame` swaps and appends, `Close()` pads the data unit,
appends the table and rewrites the header. `CaptureManager` chooses by a
`CaptureConfig` format field and the real frame type replaces the
hardcoded `RGGB`.

The record is now built before the file opens, because it carries the
camera's real Bayer pattern. `BuildFilePath` takes its extension from
the format here too: a FITS file named `.ser` is not a state worth
having for one stage.

**106.4 · The surfaces.** The format selector, saved in `rig.ini`. The
five `SetConfig` sites in `main.cpp` have to keep the format, since at
least one builds a fresh `CaptureConfig`. `AIContextBuilder` and the framing-recall
file filter learn `.fits`. `doStackFile` refuses a FITS path **in the
log and on the status line** if AutoStakkert turns out not to read it,
rather than launching and failing.

**106.5 · Measurement and close-out.** Section 5, the status row, the
help file, and the documentation corrections in section 6.

---

## 4. What gates it

**`tests/fits_format.cpp`,** with a reader written in the test against
the standard, sharing no code with the writer:

- every card is exactly 80 characters, every header and data unit a
  multiple of 2880 bytes, padding is spaces in the header and zeros in
  the data
- RAW16 round trip: `0`, `1`, `32767`, `32768`, `65535` survive
  swap, offset and reading back, on real left-aligned samples
- RAW8 passes through unchanged
- quotes in a target name, a name past 68 characters, a non-ASCII name
- an unknown value produces no card at all
- `DATE-OBS` is the start and `DATE-AVG` the middle, for a known exposure
- `MJD_MID` round trips to better than a microsecond against the SER
  tick figure for the same stamp
- `SITELAT` and `SITELONG` round trip at full precision
- no `OBSERVER` card exists
- `END` is in the last reserved block, whatever the keyword count
- a zero-frame run, and a one-frame run, are both valid files

**`tests/fits_writer.cpp`,** through the real `CaptureManager`, the way
`tests/rec_timer.cpp` does it: N frames in, the file parsed back, every
pixel and every timestamp compared, and a run abandoned without
`Close()` still identifiable. Added to `tests/run-tests.ps1` with the
existing MSVC gate.

**The 106.2 regression:** the same frames through the old and new
`SERWriter`, compared byte for byte outside the date fields.

**Run each test against a planted bug first**, as P5.6 did with its
fuzzer. A swap applied twice and a dropped `BZERO` must both fail.

---

## 5. Measured, not argued

| Measurement | Against | Passes if |
|---|---|---|
| RAW16 FITS to the capture drive, emulator, full sensor, zero drops | SER's 293 MB/s run (`9f886d6`) on the same drive | Drops stay at zero. The throughput figure is recorded whatever it is |
| The swap's time per frame, read from `[PERF]` | RAW16 at 3840x2160 is about 16.6 MB per frame | Stated in the status row. If it costs frames, the swap gets vectorised before the phase closes |
| RAW8 FITS, same run | SER | No measurable difference |

**An independent validator: done 2026-11-01.** A throwaway Python
environment with `astropy` 8.0.2 opened an 8-bit and a 16-bit cube from
the real `FITSWriter`. `verify('exception')` was clean, every pixel
matched, 16-bit rescaled to `uint16`, and the `FRAMES` table read back
at the written spacing. It was a one-off check, not a merge gate, so the
test suite gains no Python dependency.

**Owed to the owner, in the running app** (the `ask-user-for-in-app-tests`
rule, a step list at the end of the phase):

1. Record a short RAW16 cube and a short RAW8 cube on the emulator.
2. Open both in Siril: does it read the cube as a sequence, and does
   debayering give the right colours? This is the `ROWORDER` and
   `BAYERPAT` check, and it cannot be gated.
3. Drag one onto AutoStakkert. The answer decides what `doStackFile`
   does, and it is the only unknown left that changes code.

Until step 2 passes the row is `GATED (bench)` and not `WIRED`.

---

## 6. Documentation corrections, in the same close-out

Found while reviewing the roadmap for this phase, and cheap to fix while
the files are open:

- `PHASE-STATUS.md` row 92 still says the prose clips and bleeds. Phase
  103 addressed that. B1's third instance, the derived-lines overprint,
  has to be checked before B1 is closed rather than closed by assumption.
- `wiki/Roadmap-and-Phase-History.md` lists 46, 30, 47, 48 and 34 under
  "each is unstarted".
- The Done table in `ROADMAP-Next.md` stops at P5.6, and row 7 still
  says seven features remain.
- `NEXT-SESSION.md` dates from 2026-10-11 and counts 26 phases unstarted.

---

## 7. Answered by the owner, 2026-11-01

- **The site:** full precision.
- **`OBSERVER`:** names do not need to be in files that travel.

The second answer reaches past FITS: `SERWriter::Open` still defaults
`Observer` to `Raduk`, and a SER travels too. 106.2 requires SER output
to stay byte-identical, so that change is not made there. It is one line
and lands separately.

---

## 8. Not in this phase

- AVI. It stays on row 18.
- Reading FITS: no import, no quick-look from a FITS file.
- Per-frame files, compression, `fpack`.
- WCS keywords. Those belong to a solve, not to a capture.
- Dark and flat metadata beyond the existing `frameType`.
- Recovering a run that was never closed.
