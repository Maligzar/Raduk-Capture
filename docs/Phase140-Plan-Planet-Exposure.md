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
- 140.2: whether the ceilings freeze the seeing without starving the
  signal, judged by the quality trace in [PLAY].
- 140.3: whether the stretched view is what the owner wants to frame and
  focus by.
