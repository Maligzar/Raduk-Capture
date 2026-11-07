# Phase 127 — the fast-target work, run end to end

Chosen by the owner while the sky is closed. Phases 116 to 122 built satellite
passes, the night plan, JPL targets and image-only tracking. Each had unit tests
on simulated mounts, and none had ever run as a whole in the app. This phase
runs each one in the emulator through phase 125's scenarios. Five defects
turned up, and all are fixed here.

## Scenario support added

- **`press GOTO:<label>`** presses the GoTo panel's own buttons, or opens its
  tabs (`GOTO:PASSES`). It needs a prefix because FOLLOW also exists in the
  Mount tab.
- **`type JPL <text>`** types into the JPL target box.
- **`arm-pass S`** (emulator only) shifts the satellite clock so the first
  trackable pass's window opens in S seconds, and selects that pass. A real pass
  can be hours away. Only `PreciseJdNow()` moves; every other clock in the app
  stays on real time.
- **`absent`** now also catches lines since the last action, not only lines
  that arrive after the step starts. "Press X, absent Y" means X did not cause Y.
- Pass notes now reach scenarios as `[STATUS]` lines.
- `satellites.tle` is copied into each sandbox, so no download is needed.

## Defects found and fixed

1. **SKY SURVEY started image-only tracking instead of the survey.** Phase 122
   gave TRACK WHAT IS IN FRAME the survey's control ID (2030), and the click
   handler checks it first. The survey has been unreachable since. Checklist 44
   names the survey as the fastest way to fill the pointing map. The Passes list
   also shared 2023 with a label. Both IDs are now unique.
2. **A satellite, JPL or image-only recording kept the last run's settings.**
   Those settings were the "Jupiter" prefix set at startup and a 1000-frame
   limit, so a pass of SL-3 R/B was filed as `Jupiter_...` and stopped
   recording after 50 s at 20 fps, or a few seconds at a planetary frame rate.
   It is now named for the target, and only the pass's own end or STOP closes
   it.
3. **"Worst error on target 0.00 deg" for a track that never got there.** A
   follow stopped while it was still slewing or holding read as perfect. It now
   says "It never reached the target."
4. **The emulator's guide pulses moved RA and Dec at 15"/s.** The real 8SE's
   `PulseGuide` is a fixed-rate slew of the azimuth or altitude motor at guide
   rate 1. So in the emulator:
   - calibration measured the wrong guide rate;
   - one axis's response shrank with cos(dec) instead of cos(alt);
   - image-only tracking, which converts pulses into azimuth and altitude,
     drove a star at dec +74 out of the frame.

   The emulator now turns the azimuth and altitude axes at the configured
   guide rate.
5. **A calibration with one axis barely moving was accepted.** It passed every
   other check, and anything built on it, image-only tracking above all, drove
   the mount degrees off. On this alt-az fork it happens near the zenith, where
   an azimuth move hardly shifts the sky. A calibration is now refused when the
   weaker axis responds at less than a fifth of the stronger, with a message to
   calibrate lower in the sky.

## The scenarios

| Scenario | Checks | Checklist |
|---|---|---|
| `satellite-pass` | Arms a real pass from the elements on disk. Slews, goes on target and records, named for the satellite, and does not stop at the old 1000-frame limit. Stopping reports the worst error (0.07 to 0.09 deg) | 39 |
| `night-plan` | PLAN THE NIGHT plans the passes, RUN THE PLAN starts, and pressing it again stops cleanly | 41 |
| `jpl-follow` | Asks JPL Horizons for the Moon (301), finds its window and slews there. Stopped before the window, it says it never reached the target | 42 |
| `motion` | Locks on Kochab for tonight's calibration, then TRACK WHAT IS IN FRAME follows it from the image alone and holds it | 43 |
| `survey` | SKY SURVEY starts the survey (Survey 1/20) and not image-only tracking | 44 |

Kochab is used because, at this site, it is in the tracking band at any hour.
The emulated mount points where a target really is, and a planet below the
horizon is rightly refused: "the mount left the safe altitude band". `jpl-follow`
makes a real request to JPL Horizons. Only the target's name and the time are
sent, never the site.

## Still for the sky

Checklist 39 to 43: real satellites, real seeing, the real mount's response, and
SatImageLock on a real point source.
