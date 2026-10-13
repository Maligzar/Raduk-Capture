# Recording and SER Format

SER v3 is the planetary standard: uncompressed, per-frame UTC timestamps,
RAW8/RAW16 with a declared Bayer pattern. Files written here open directly
in AutoStakkert!, Registax, Siril, and PIPP.

[`src/core/SERWriter.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/SERWriter.h) ·
[`src/core/SERWriter.cpp`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/SERWriter.cpp)

---

## File layout

```
┌──────────────────────────────┐
│ SERHeader — 178 bytes        │  little-endian, fixed size
├──────────────────────────────┤
│ Frame 0   (w × h × bytes/px) │  raw Bayer, uncompressed
│ Frame 1                      │
│ ...                          │
│ Frame N-1                    │
├──────────────────────────────┤
│ Timestamp trailer            │  N × int64, 100 ns ticks since 0001-01-01
└──────────────────────────────┘
```

`FrameCount` in the header is written as `0` at `Open()` and patched by
`fseek` to `FRAME_COUNT_OFFSET` at `Close()` — the count is not known until
the recording ends.

### `SERHeader`

```cpp
#pragma pack(push, 1)
struct SERHeader {
    char     FileID[14];         // "LUCAM-RECORDER"
    int32_t  LuID;               // 0
    int32_t  ColorID;            // SERColorId
    int32_t  LittleEndian;       // 1
    int32_t  ImageWidth;
    int32_t  ImageHeight;
    int32_t  PixelDepthPerPlane; // 8 or 16
    int32_t  FrameCount;         // patched at Close()
    char     Observer[40];       // "Raduk"
    char     Instrument[40];     // "ZWO ASI678MC"
    char     Telescope[40];      // "Celestron 8SE"
    int64_t  DateTime;           // ticks since 0001-01-01
    int64_t  DateTimeUTC;
};
#pragma pack(pop)
static_assert(sizeof(SERHeader) == 178, "SERHeader must be 178 bytes");
```

The `static_assert` is doing real work: `#pragma pack(1)` plus a
`static_assert` on the total is the only thing standing between a correct
file and one that every stacker rejects.

### Colour IDs

| `SERColorId` | Value | Used for |
|--------------|-------|----------|
| `MONO` | 0 | `bayerRGGB = false` |
| `BAYER_RGGB` | 8 | **The default** — ASI678MC native pattern |
| `BAYER_GRBG` | 9 | Defined, not used by this rig |
| `BAYER_GBRG` | 10 | Defined, not used |
| `BAYER_BGGR` | 11 | Defined, not used |
| `RGB` | 100 | Defined, not used |
| `BGR` | 101 | Defined, not used |

---

## Timestamps, and why they are capture-time

This is the detail that makes the files usable for WinJUPOS derotation, and
it was a bug worth understanding.

**The problem.** Frames are timestamped by the capture thread with a QPC
microsecond counter. The writer thread drains them asynchronously and could
lag up to ~10 seconds behind with a full 512-slot backlog. Writing "now"
into the trailer as each frame was drained produced timestamps that bore no
relation to when the photons arrived — useless for derotation, where the
whole point is knowing precisely when each frame was exposed.

**The fix (Phase 59, review C2).** Correlate the QPC clock with wall time
**once**, at `Open()`:

```cpp
m_qpcBaseUs = Timestamp::Now();        // QPC microseconds
m_ftBase    = CurrentFileTimeUTC();    // FILETIME 100ns ticks
```

Then each trailer entry is derived from the frame's own capture stamp:

```cpp
int64_t ts = (timestampUs != 0)
    ? m_ftBase + ((int64_t)timestampUs - (int64_t)m_qpcBaseUs) * 10
    : CurrentFileTimeUTC();   // fallback: no capture stamp available
```

### Epoch conversion

Windows `FILETIME` counts 100 ns ticks since 1601-01-01. SER counts ticks
since 0001-01-01, which is `504911232000000000` ticks earlier:

```cpp
static constexpr int64_t EPOCH_OFFSET = 504911232000000000LL;
```

---

## File naming

`BuildFilePath()` composes:

```
<outputDir>\<prefix>_<frameType>_<W>x<H>_RAW<bits>_<YYYYMMDD>_<HHMMSS>.ser
```

For example `D:\RadukCapture\captures\Jupiter_Light_1280x960_RAW8_20260314_213045.ser`.

`frameType` is one of Light / Dark / Flat / Bias (Phase 44), so a folder of
captures is self-describing without a notes file.

---

## The `_quality.txt` sidecar

Every recording gets one, named by replacing the `.ser` extension:

```
# RadukCapture quality scores - sharpness, one per frame
# Frames: 2151
# Format: one score per line, frame order
42.18
39.77
45.03
...
# Mean: 41.22  Peak: 58.90
```

> **AutoStakkert does not read this file.** The claim that it did stood in
> the source since Phase 11 and is false. AS!4 was tested directly against
> a recording with this sidecar beside it: it ran its own
> `BufferAndAnalyze` and never opened the file. No external-quality import
> path exists anywhere in the binary, and its accepted inputs are
> avi/ser/as3/dat and image formats — there is no `.txt` input at all. A
> single global score could not drive its selection regardless, because AS
> grades quality **per alignment point** and each point stacks a different
> subset of frames.
>
> The file is kept because it is useful *here*: it records how the seeing
> ran across a capture, and it is what a morning-after review reads to
> decide which runs are worth stacking.

**The scores are per-frame as of Phase 76.** Before that they were whatever
the ~2 Hz analyzer last happened to emit, repeated across long runs of
frames — which could not rank anything. The writer thread now scores each
frame itself using the `cropCX`/`cropCY` hint from the producer so the
measurement lands on the planet rather than on background noise. See
[Image Analysis](Image-Analysis#sharpness).

**The header comment is ASCII only, deliberately.** This file is opened in
text editors and spreadsheets that assume the system codepage, and the
UTF-8 em-dash that used to be in the header showed up as mojibake in every
one of them.

---

## Choosing a bit depth

| | 8-bit (RAW8) | 16-bit (RAW16) |
|---|---|---|
| Bytes per pixel | 1 | 2 |
| Full-frame rate | ~47.5 fps | ~24 fps |
| Full-frame throughput | ~750 MB/s | ~1.5 GB/s |
| Use for | **Planets.** Half the data, so disk and USB keep up at high frame rates; stacking hundreds of frames recovers the precision anyway | Long exposures, faint targets, anything to be stretched hard — when a single frame has to carry the detail |

Watch `LOST` in the status bar when you double the data rate.

**Not the same as USB bandwidth.** That is a separate 40–100% slider in the
CAMERA panel section controlling how hard the camera pushes the bus, not
how much data each frame holds. Lower it if frames drop.

---

## Output files per session

All in the configured output directory, default `D:\RadukCapture\captures`:

| File | Contents |
|------|----------|
| `*.ser` | Raw Bayer video frames, SER v3 |
| `*_quality.txt` | Per-frame sharpness scores plus mean and peak |
| `*_snap_*.png` | Single-frame PNG snapshots (`S` key, WIC encoder) |
| `session_log.txt` | Session events, temperatures, status |

Session logs also go to `D:\RadukCapture\logs\session_<timestamp>.log`
via [`SessionLog`](Code-Map), and the crash-safe application log is
`raduk_log.txt`.

---

## What is not written

- **No AVI.** Not built.
- **No FITS.** The plate solver *reads* FITS; nothing here writes it.
- **The recording is raw.** Dark and flat correction is applied to the
  preview only — the SER keeps raw sensor data, because a stacker does this
  better with the full frame set, and raw data stays useful when a master
  turns out to have been wrong. See
  [Image Analysis](Image-Analysis#dark-and-flat-masters).

---

**Next:** [Image Analysis](Image-Analysis) · [Capture Pipeline](Capture-Pipeline) · [Operations Guide](Operations-Guide)
