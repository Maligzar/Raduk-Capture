# Phase 140 plan: exposure set by the planet, and a view that is bright enough to use

Planned 2026-11-11, from the owner's report: auto-exposure keeps pushing
exposure far too high, every SER in the capture folder is overexposed, and
the short-exposure, high-gain settings Saturn and Jupiter want make the
live view hard to see. Not built.

## The cause, found in the code (inferred, to be confirmed by 140.0)

Auto-exposure steers on `AnalysisResult::highlightDN`, the 99.9th
percentile of luminance over the **whole frame** (`FrameAnalyzer.cpp`,
`HIGHLIGHT_PCTILE = 0.999`). That reads the planet only when the planet
covers more than 0.1% of the frame.

At 0.203"/px on the full 3840x2160 sensor, 0.1% is about 8,300 pixels.

| Target | Size | Pixels | Share of the full frame |
|---|---|---|---|
| Saturn's globe | about 18" | about 6,000 | about 0.07% |
| Jupiter | about 44" | about 37,000 | about 0.45% |

So on Saturn at the full sensor, the percentile lands on the sky or the
halo beside the planet. AE reads "too dark" and adds light until that
halo reaches 185 to 215 DN, by which time the globe is far past clipping.

The clip emergency cannot stop it. It fires when more than 1.5% of the
**frame** is clipped, and no planet at this scale covers 1.5% of the full
sensor; even Jupiter is under half a percent. The two guards share one
blind spot: both measure the frame, and the planet is a small part of it.

A cropped ROI hides the fault: at 640x480, Saturn is about 2% of the frame
and Jupiter about 12%, so the percentile lands on the planet. Whether the
owner's captures were full-sensor or cropped decides how much of the
overexposure this explains.

There is a second, separate pressure toward long exposures. The light
ladder (`LightLadder.h`, rails from `CameraRails::RailsFor`) spends
exposure up to a 20 ms knee before it raises gain past the HCG floor of
182. The owner's direction is the opposite order for Jupiter and Saturn:
short exposures to freeze the seeing, with gain carrying the light.

## Stages

| Stage | What | Where | Size |
|---|---|---|---|
| **140.0** | **Audit the captures.** `tools/ser_exposure_audit.py` (written with this plan, standard library only, read-only) reports for each SER the share of the target that is clipped, the whole-frame percentile AE saw, and the target's share of the frame. The owner runs it on the capture folder; the table goes into this plan. It confirms or refutes the cause above before any code moves | The owner's machine | — |
| **140.1** | **AE meters the target, not the frame.** The highlight is taken over the target's own pixels: the disc the area-flux finder or the lock already locates, or a mask of pixels above half the frame's peak when neither has one. The clip emergency counts clipped pixels as a share of the target, not the frame. With no target found, AE holds and says why, rather than steering on the sky. Gate: `tests/ae_target_metering.cpp`, a Saturn-sized disc on a full-sensor frame, which the current code drives to clipping and the new code holds at 185 to 215 DN; the same disc in a 640x480 ROI gives the same answer both ways | Bench | S-M |
| **140.2** | **Short exposure first, gain for the light.** Per-planet exposure ceilings in the target profiles, and the ladder's knee taken from them, so on Jupiter and Saturn AE goes to gain before it lengthens exposure. Starting ceilings are practitioner convention for an 8-inch at f/10 (recalled, about 5 to 10 ms for Jupiter and 10 to 20 ms for Saturn); they are defaults to be tuned on the sky, not facts. The derotation limit is unaffected | Bench, then the sky | S |
| **140.3** | **A view brightened for the eye, never for the data.** An auto-stretch on the live preview, metered on the target as 140.1 does, so a short, high-gain planet is easy to see. It reuses the SER player's `AutoStretch`. It is display only, as the existing 2x and 4x boosts are. A visible DISPLAY STRETCHED tag stays on while it is active, and the histogram and every exposure readout keep showing raw levels, so a brightened view is never mistaken for a brighter capture. On by default for planets | Bench, then eyes on a screen | S-M |

## Order

140.0 first, because it costs no code and settles the cause. 140.1 is
category 1 by the roadmap's ranking, since it spoils every capture of a
small planet. 140.2 goes with it. 140.3 follows: the owner needs it as
soon as exposures shorten, because a correctly exposed planet at high gain
and a few milliseconds will look dimmer on screen than the overexposed one
did.

## What it owes the sky

- 140.1: on Saturn at the full sensor, AE settles with the globe at 185
  to 215 DN and the audit shows under 1% of the target clipped.
- 140.2: the exposure ladder, clear-night item 55. Four exposures from
  2 to 15 ms, gain set so each peaks near 70%, stacked and processed
  identically. The best sets the band; until then the band is a guess.
- 140.3: whether the stretched view is what the owner wants to frame and
  focus by.

## 140.0 result, 2026-11-11: the owner's audit of D:\RadukCapture\captures

41 files from the 2026-11-07 night, run by the owner with
`tools/ser_exposure_audit.py`. **It does not support "every SER is
overexposed", and it does confirm the metering fault on small targets.**

- **39 of 41 files have no clipping on the target.** The two that do are
  Uranus: `0918_9` (640x480, 19.5 ms, gain 528) has 48% of the disc
  clipped, and `0920_2` (full sensor) 3.9%.
- **Saturn is mostly under-exposed, not over.** Peaks run from 19% to 82%
  of full scale. The 8-bit files sit at 19-32%, most of the 16-bit
  640x480 files at 20-42%, and the best at 72-82% (`0409` to `0417`,
  `0801` to `0803`). The usual planetary aim is about 60-80% (recalled
  practitioner convention, and the 185-215 DN AE target is the same
  idea).
- **Exposures were already very short.** Saturn ran at 0.1 to 6.3 ms,
  mostly under 2 ms, with gain 278 to 466. So the "too much exposure"
  report is not in these files. The thin signal is: at 0.7 ms and gain
  389 the brightest part of Saturn reached about 20-30% of full scale.
- **The metering fault shows exactly where predicted.** Uranus covers
  0.04 to 0.12% of the frame. In `0918_9` AE's whole-frame percentile read
  148 DN while the disc was clipped, and AE had run gain to 528, near its
  ceiling. On the full-sensor 8-bit Saturn file `0409_5` (0.081% of the
  frame) AE read 81 DN against a peak of 183 DN.

**What this changes.**

- **140.1 stands, and is confirmed.** On a small target AE steers on the
  sky. The cost seen was clipped Uranus; on Saturn the same fault reads
  low, so AE would add light.
- **140.2 changes from "ceilings" to a band.** The captures show the
  opposite failure to the one planned for: exposures far below what the
  seeing requires, with gain doing work it cannot do. Gain amplifies; it
  adds no photons. Above the HCG floor (182) read noise is already near
  its minimum, so beyond it a short exposure at high gain is a noisy frame
  made bright, and the shot noise is set by the photons the exposure
  collected. 140.2 sets a per-planet exposure band, with a floor as well
  as a ceiling, so AE lengthens exposure toward the ceiling before
  raising gain beyond the floor, and never drops under the floor without
  saying so. The owner's "low exposure, high gain" holds against long
  exposures; the floor stops it going so low that the signal is lost.
  Band values are to be set with the owner and tuned on the sky.
- **Why the files look overexposed to the owner is still open.** Two
  candidates, both outside these numbers: (1) the 16-bit files predate
  #125 (2026-11-10), so PIPP and AutoStakkert byte-swapped them into
  noise unless they were repaired; (2) a viewer's auto-stretch or the
  preview's 2x and 4x boosts showing a dim planet as blown. Asked of the
  owner 2026-11-11.

Audit caveats: 16-bit samples are read by their high byte, which the
sensible peaks across sessions support; "target" is pixels above half
the frame's peak, so a faint limb is not counted; five frames per file.
