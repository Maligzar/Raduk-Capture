# Phase 112 — the camera controls review

> *"all adjustments allow for manual change and appear like exposure and
> are captures as saved settings under capture"*
> *"gamma does not seem to be doing anything"*
> *"when using auto exposure we should see the settings change ... AE
> should be changing all settings not just exposure. the point is for AE
> auto and auto focus to work together to deliver a sharp image that does
> not rely on the human eye"*
> *"when I run the autofocuser the AE should show as also on and running
> automatically this way a user knows what is happening"*

All of it is built. The owner chose the ladder, option A in section 4.4.

---

## 1. Findings

### 1.1 The sliders were not showing the camera

Every section starts collapsed, so the startup and profile pushes found
null handles and did nothing. `RebuildControls` then built the sliders
from constants: gain 100, exposure 0.08 ms, gamma 50, WB 52/47, USB 80,
whatever the camera was doing. Opening CAMERA showed a fiction, nudging a
slider wrote from it, and each collapse toggle reset it. This is most of
why AE could not be watched. The rebuild now ends in `SyncFromCamera`,
and the remote `set_gain`, `set_exposure` and `set_gamma` call it too.

The AE button and status were rebuilt the same way, so expanding any
section showed `AE OFF` / `AE: off` while AE kept running. Both restored.

### 1.2 Only exposure could be typed, and its box wrote when untouched

Gain, gamma, WB red, WB blue and USB now have the same box: Enter
commits, Escape restores, out of range clamps and shows the clamp, and
anything else restores. The rule is `src/core/CameraEntry.h`.

Found on the way: exposure's box committed on every focus loss, typed
in or not. Clicking through a DSO profile's `30000+` wrote 60 ms to the
camera, and a box holding the caret while AE ran undid AE on leaving.
All six now commit only if edited (`EM_GETMODIFY`).

### 1.3 What is saved

| where | exposure, gain | gamma, WB, USB | high speed |
|---|---|---|---|
| Profile, `[camera]` | yes | yes | yes |
| Session, `rig.ini` | yes | yes | forced on at startup |
| **Capture record** | yes | **no, now yes** | **no, now yes** |
| FITS header | yes | **no, now yes**, not USB | no |

I read "saved under capture" as the record written beside each capture,
because that is the one place exposure was and the others were not. The
AE mode goes in the record as well. Profiles and sessions already held
every value, read from the camera. If "under capture" meant something
else, such as the `[capture]` block of the profile file, that is a
small move.

A control the camera does not list is left out of the record, not
written as 0, and SAVE keeps the profile's gamma, because `GetControl`
answers 0 for those.

### 1.4 AE ran through autofocus, and through recordings

Nothing connected them. The score is a Laplacian variance, so it goes as
the square of the signal: a 10% exposure change moves it 21%. Defocus
dims the highlight, and AE answers by adding light, so it was bending
the curve the fit reads, worst at the ends of the sweep. Recordings had
the same problem more plainly: the record states one exposure for the
run, and a stack of several matches no dark.

`AutoExposureAssistant::SetHold` now stops it during both, ahead of the
clip emergency too, and it settles before its first decision afterwards.
The hold during recording is my decision, and is the one to reverse if
you want AE to chase brightness through a long Moon run.

### 1.5 Faults in AE's arithmetic, fixed by the ladder

- The gain branch treated gain as linear (`gain * factor`). It is 0.1 dB
  a unit, so 60 units is a doubling, and at gain 200 a 1.2 factor gave
  ×1.58. It overshot.
- The emulator's brightness was linear in gain too. It is dB now,
  measured at 93 to 185 DN for 60 units, and that **changes the look of
  every profile in the emulator**: Jupiter at gain 200 is 1.6× brighter
  than it was.
- `snapExp` rounded to 0.25 ms below 5 ms, so phase 98's dead zone was
  partly back: at 2 ms an error under about 32 DN rounded to no change.
  Gone, along with `main.cpp`'s flat 0.05 ms gate on the same steps.
- The correction was a difference over 255, so a highlight of 60 DN that
  wanted 3× got 1.29×, and a far-off start took 50 s, longer than
  autofocus waits. It now works from the ratio, in stops.
- `RailsFor` had no gain limits. It has the ladder's figures now.

---

## 2. Gamma: which it is

| layer | verdict |
|---|---|
| App | **Sends it.** Slider, typed value, profile, session, reconnect and remote all reach `SetGamma`. |
| `ASICamera` | **Sends it, and ignores the answer.** `SetControl` returns a status that `SetGamma` drops. |
| `CameraEmulator` | **Stored it, applied it to nothing. This is what you saw.** White balance had the same fault. |
| Real SDK | **Probably refused, not established.** Forum reports say ZWO removed gamma in SDK 1.13.1.12, January 2018. The repo has SDK 1.41, and the 678MC is newer than that removal. No session has listed this camera's controls. |

So it is an emulator fault now and may be a real one later. Fixed both
ways:

- The emulator applies gamma and white balance to the finished frame.
  Its curve is its own, with 50 as identity and higher lifting midtones.
  It claims nothing about ZWO.
- At startup the log prints every control the camera lists, which
  answers the question on first connection. A control not listed greys
  out and shows `n/a`, instead of moving a slider that does nothing.
- `RADUK_EMU_NO_GAMMA=1` makes the emulator that camera, to see it.

Also unknown: whether WB changes RAW data on this camera. ZWO's manual
calls RAW8 original data and does not say. The checklist measures it.

Sources: [SharpCap forum, gamma removal](https://forums.sharpcap.co.uk/viewtopic.php?p=4016),
[ZWO software manual](https://i.zwoastro.com/zwo-website/manuals/ZWO_ASI_Cameras_Software_Manual_EN(Windows).pdf).

---

## 3. AE on the sliders

With 1.1 fixed, the panel shows the camera at all times and AE's writes
land on real values. `UpdateSlider` keeps phase 98's drag guard and now
updates gain's box too, skipping any box holding the caret.

AE ADV still moves nothing, by definition. AE AUTO moves a slider at
most once in 3 s: one tick to act and five to settle.

---

## 4. AE and autofocus together

### 4.1 The physics

- The sharpness score scales with signal squared, so **every setting
  that changes signal must stand still for a sweep**: exposure, gain,
  gamma, WB.
- Gamma is worse than scaling. It is nonlinear, so it changes the shape
  of the curve.
- Scores from different settings are not comparable, so AE moving after
  AF does not undo the focus but does end comparison with the sweep.
- Exposure sets how much seeing is frozen and gain sets the noise. They
  are not interchangeable once light is matched.

### 4.2 The sequence, built

Your instruction made this the design. Button, `A` key and remote all
take one path:

1. AE switches to AUTO on the panel and CAMERA opens, so the sliders can
   be watched.
2. AE settles: on target, or against a limit it cannot pass. After 20 s,
   or if a recording is running, the sweep goes anyway.
3. The sweep runs with AE held. The panel reads `AE: held for autofocus`.
4. AE resumes and **stays on**, because the sharper disc is brighter and
   that correction is wanted.

### 4.3 What AE should not move

- **Gamma: never.** It may not exist on this camera, and it is the one
  setting that bends the metric's shape. Recommended: leave it at 50.
- **USB: never.** It affects no pixel.

### 4.4 How light is shared between exposure and gain: A, chosen, built

| | order when more light is needed | for | against |
|---|---|---|---|
| **A. Ladder (recommended)** | exposure to a knee, gain to a knee, then exposure to the rail, then gain to the rail. Reverse when dimming | keeps exposure short, which is what freezes seeing, and moves both sliders | two new numbers per target, unproven on sky |
| B. Today's order | exposure to the 50 ms rail, then gain | no new numbers | a dim planet goes to 50 ms and blurs before gain moves, and gain rarely moves at all |
| C. Sharpness trials | after AF, try exposure pairs at matched light and keep the best score | the fullest answer to "math, not the eye" | seeing varies on the timescale of the trial, so it needs real sky to design; a later phase on top of A |

`src/core/LightLadder.h` is one path, walked forwards for more light and
backwards for less. Knees on a planet: 20 ms and gain 350, with the HCG
floor at 182. The Moon and star profiles have no floor, because they sit
below HCG on purpose, and DSO has knees of 60 s and 300. All of these
are guesses from the profiles' own values until tested on sky.

White balance is a separate question: AE could set it once, from the
channel highlights, before the sweep. That is only worth building once
the checklist shows WB changes the raw frames.

---

## 5. Verification

`tests/camera_controls.cpp` runs the real emulator and measures frames:
typed-value rules, gamma and WB per Bayer site through both setters,
listed controls, 60 gain units as a stop, the record round trip
including unknown and zero, AE holds and resume, what `Settled` means,
every rung of the ladder in both directions, and AE closing the loop on
a simulated camera from dim, clipped, hopeless and Moon starts.

No test covers the panel itself: the boxes, greying, `SyncFromCamera`,
or the autofocus path in `main.cpp`. Those are in the checklist.

29 planted bugs, all caught, after one miss: the emulator's old linear gain passed a loose tolerance, which was tightened. They cover the clamps, the frame response, the record fields, the hold, every rung of the ladder, the unit of a stop, the correction's sign and step limit, and the HCG floor.
