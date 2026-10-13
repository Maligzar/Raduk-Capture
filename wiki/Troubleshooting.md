# Troubleshooting

Organised by what you would actually notice, not by technical fault name.

---

## Nothing on screen

### Black preview / no signal

- Check the USB connection to the ASI678MC
- Check the camera is not in use by another application — close FireCapture
- Restart RadukCapture

### The planet is not in frame

Work through these in order; they distinguish genuinely different faults:

1. **Press `B`** for `BOOST ×2` / `×4`. Brightens the display only.
2. **Check the lens cap.** This catches everybody eventually.
3. **Watch the [motion plot](Mount-Control#motion-monitor)** in `MOUNT CTRL`
   while pressing an arrow key. A moving trail means the command reaches the
   mount and you are pointed at the wrong patch of sky. A flat trail means
   the mount is not moving, which is a different problem entirely.
4. **Check alignment.** See [TRK stays OFF](#the-mount-will-not-track-trk-stays-off).

---

## Mount

### Mount not responding

The serial port is **auto-detected** — RadukCapture probes every COM port
Windows reports as present and keeps the one answering the NexStar echo. The
port a USB-serial cable gets is assigned by Windows and **changes when the
cable moves to a different USB socket**, so it is never assumed.

The startup log line names what happened:

| Log line | Meaning |
|----------|---------|
| `Mount: OK (COM4 responded)` | Hand control found |
| `Mount: OK (COM4 owned by CPWI)` | Port exists but CPWI holds it |
| `Mount: NOT FOUND (probed: COM3, COM4)` | Nothing answered on any present port |

If NOT FOUND:

- Hand control powered on, cable seated at **both** ends
- The USB-serial adapter appears under Device Manager → Ports (COM & LPT)
  **with no warning icon** — listed but not working means a driver problem
- If CPWI is running, disconnect the mount in CPWI first
- `RELEASE` in the TELESCOPE section hands the port back to CPWI

To pin a port and skip the scan:

```ini
[Rig]
MountPort=COM4
```

A pinned port is tried first; **if it does not answer the scan continues
anyway**, so a stale entry falls back to auto-detect rather than breaking the
connection.

### The mount will not track (TRK stays OFF)

> An alt-az mount computes its altitude and azimuth rates from **latitude
> and local time**. Without them the calculation is underdetermined and the
> mount declines to track, however often it is asked.

RadukCapture sends both on connect. Check the log:

```
[NexStarMount] Site  -> ... : accepted
[NexStarMount] Clock -> ... : accepted
```

**If those are accepted and `TRK` still reads `OFF`, the mount is not
aligned.** Tracking is gated behind the hand control's own alignment, and
nothing in this application can substitute for it:

> **Hand control → Align → Quick Align.** It needs **no stars** — level the
> tube, point north, confirm. GoTo accuracy will be mediocre; tracking will
> work, which is what matters if you find targets by hand.

The app notices an unaligned mount at startup and helps with the hard part.
"Point the tube north" is an abstraction in a dark garden, so the
[align helper](Mount-Control#align-helper) asks one question — *standing
where you normally sit, the scope is to my …?* — and answers in your
landmarks:

> North is behind you. Point the tube back towards yourself — north is on
> your side of the mount, so the tube should face your desk.

**The tube ending up aimed at you is correct and counter-intuitive, which is
why it says so plainly.** The answer is saved as `ObserverBearingDeg`, so
you are asked once per rig, not once per session.

Afterwards the log should read `Mount reports ALIGNED - leaving its site and
clock untouched`: **the app will not re-push a site to an aligned mount,
because changing time or location can invalidate a Celestron's model.**

`TRK` reflects a **readback** of the mount's actual state. A command the
mount merely acknowledges is not reported as success.

### `TRK: UNKNOWN`

A third state: the mode could not be read at all after several tries. **A
link problem, not a tracking problem.** It used to be reported as `OFF`,
which sent at least one session chasing the wrong fault.

### Small moves seem to do nothing

- **Check the rate is what you think it is.** The rate buttons live only in
  the floating `MOUNT CTRL` panel, and they set the arrow keys too. Before
  v1.2.1 the two did not talk to each other and the arrow keys stayed at 9×
  whatever the panel showed.
- **Watch the motion plot** while pressing. Moving trail = the command is
  reaching the mount and the move is too small to see in the preview. Flat
  trail = it is not.
- **At `FINE` a quarter-second press is a few arcseconds.** That is intended
  — it is for centring a planet, not finding one. Use `9×` to travel.
- **If the plot reads `MOUNT` rather than `CAMERA`** it is only updating
  twice a second, so a brief nudge shows as one step rather than a smooth
  line. **That is the readout being coarse, not the mount being stuck.**

### GoTo misses the target

- Run `POINT SOLVE → SYNC` to correct the mount model at the current
  position
- Ensure StarSense alignment was performed before the session
- The [pointing model](Sky-GoTo-and-Plate-Solving#pointing-model) reports a
  predicted error but **deliberately does not apply it** — see that page for
  the two reasons

---

## Acquisition

### `[CENTROID]` refuses to start

It says why, in the notification bar and the log. The usual cause is
tracking:

| Message | Cause and fix |
|---------|---------------|
| *"mount is not tracking"* | See above. **With the mount undriven the planet drifts 15″/s, which is 74 px/s at this plate scale; the centring dwell window is 10 px, so it is crossed in 0.14 s and can never be satisfied** |
| *"planet too faint to detect (frame peak 3%, needs above 8%)"* | It is in frame but under the detection threshold. **Raise exposure or gain** |
| *"no planet detected in frame"* | Nothing bright enough is there at all. **Go and find it first** |

> Those last two used to read identically, which mattered because they need
> opposite responses.

`[ROI]` is **stage 3 of that sequence, not a manual crop tool.** To set an
ROI by hand, drag a box on the preview.

---

## Recording

### Nothing is being recorded, or almost nothing

**Check the GATE threshold first.** The gate only records frames whose
seeing score beats it, and **the meaning of that number changed in
v1.2.2** — the score is now relative to the best recent frame rather than an
absolute scale. **A threshold set before then will not behave as you
remember.**

Re-pick it against the live readout: watch the seeing number for a minute,
note how low it dips, and set the gate a little above that. **Set too high
it records nothing at all, which looks exactly like a broken capture.**

See [Image Analysis](Image-Analysis#seeing-score).

### Frames dropping (`LOST` > 0)

- **Lower the USB bandwidth slider** in the CAMERA section (40–100%)
- Close other USB-intensive applications
- The write buffer is 512 slots — **sustained** drops mean disk write speed
  is insufficient, not a transient burst
- Consider 8-bit instead of 16-bit: half the data rate

### I cannot change the data rate / file size

Two controls, both on the toolbar: the resolution buttons and `8b`/`16b`. **A
SER file is uncompressed, so the rate is simply width × height × depth ×
frame rate — there is no separate bitrate to set.**

The USB Bandwidth slider is a different thing: it limits how hard the camera
pushes the bus, and is what to lower when `LOST` climbs.

Both resolution and bit-depth buttons **close the current recording**, since
a SER file cannot change format part-way through. Set the format first.

---

## Focus

### Autofocus fails or does not converge

- **Check the log for `[SHARP] CEILING`.** After a few minutes of imaging the
  app judges its own sharpness scale and says so if it is wrong. **A ceiling
  set too low clips the focus curve flat, and autofocus then has no peak to
  find. This is the single most likely reason a sweep returns something far
  from focus.** It names the constant to change.
- **Ensure the EAF is connected** — position shows a number, not dashes
- **Centre the target first.** Sharpness is measured on a 256×256 crop that
  follows the detected planet; with nothing detected it falls back to frame
  centre and **reads background noise**
- **Read what it reports.** `focus not bracketed` means true focus is outside
  the ±3000-step sweep — focus roughly by hand and re-run. `Focus got worse …
  reverted to start` means the fit landed somewhere worse and the focuser has
  been put back, usually for want of contrast
- **Check the EAF is not at travel limits** — safe zone 10,000–490,000
- A `sharp=0.0%` result **was a bug before v1.2.0**, not a diagnosis. If you
  see it now, the target really is too faint

A move that makes no progress for 5 s is reported as stalled. Long moves are
fine — the sweep's first hop is 3000 steps and is allowed to take as long as
it needs.

See [Autofocus](Autofocus).

---

## The picture looks wrong

**Before changing any camera setting, check whether a display mode is on.**
These change only what you see; the recording is unaffected, and nothing in
the camera is broken.

| What you see | Likely mode | Turn it off with |
|--------------|-------------|------------------|
| Greyscale / monochrome | RAW debayer | `[C] COLOR` |
| Everything red | Night vision | `N` |
| Grey, but one colour's detail | Single channel R/G/B | `H` until it reads `CH: ALL` |
| Unnaturally bright or smeared | Display FX / stack | `B` until it reads `FX: NORMAL` |
| Briefly very bright and noisy | Framing shot — restores itself after 5 s | wait |
| Circles or brackets drawn on it | Reticle | `R` until it turns off |

**If none of those are on, the cause is exposure, focus, or the sky — not the
display.**

The [AI assistant](AI-Assistant#display-modes-phase-87) is given this same
list of what is currently on, so asking it "why does my image look like
this" works.

### My saved file looks noisier than the screen

**That is correct.** Dark and flat correction applies to the preview only;
the recorded SER stays raw. See
[Image Analysis](Image-Analysis#dark-and-flat-masters).

### The view has gone narrow / I cropped and cannot get back

**Press `FULL`** on the toolbar. `[ROI]` and dragging a box both narrow the
sensor read-out, and before v1.2.3 nothing widened it again — restarting the
app was the only way back.

If `FULL` gives you less than 3840×2160, the camera clamped the request and
the app is showing you what it actually got. **In the emulator that is always
the case, by design** — see
[Emulator and Testing](Emulator-and-Testing#camera).

### The image looks too dark or too flat

Try the [display stretch](Rendering-and-UI#display-stretch-l) first — `L`,
and the Black/White sliders. **It costs nothing and changes no data. Raising
gain or exposure to fix how something *looks* makes the recording worse.**

---

## Site and configuration

### Observing site wrong or missing

Resolved at startup in this order; the first that answers wins:

1. `SiteLatDeg` / `SiteLonDeg` in `rig.ini`, if present
2. **Windows Location** — typically ~100 m, far better than the 5.6 km
   needed to land a target in the frame
3. A USB GPS receiver on any serial port (NMEA, passive listen)
4. The site the mount itself already holds
5. A dialog asking for it

> **If none answers, no site is sent to the mount.** An unknown site is left
> unknown rather than defaulted to 0,0 — which is a real place in the Gulf of
> Guinea and would break tracking, the sky map and the ADC model **while
> looking perfectly configured.**

Windows Location needs Settings → Privacy → Location, including **"Let
desktop apps access your location"**. If refused, the log says so and names
the setting.

To pin by hand, set both coordinates **and `SiteSource=manual`**. An entry
marked manual is never replaced by an automatic estimate.

### ADC reading seems wrong

In order of likelihood:

1. **Check the filter first.** The `Filter:` line directly under the ADC
   readout says which band the figure was computed over. **If that does not
   match the glass actually on the camera, the number is wrong by as much as
   4× — this is the most likely cause.** Fix it in SETUP; see
   [Advisors](Advisors#what-the-filter-setting-is-worth-numerically).
2. **Plate-solve once and read the log.** The figure scales with plate scale,
   and `PlateScaleArcsecPx` is *calculated* rather than measured. Every
   successful solve logs `Plate scale: solved X"/px vs rig.ini Y"/px` and
   flags anything more than 5% out.
3. **Check `SiteElevationM`.** It drives the pressure model — ~10% high at
   sea level, ~15% low on a mountain if unset.
4. **ADC uses cached mount altitude** — verify the mount is tracking and
   RA/Dec is updating in the TELESCOPE section.
5. Single-arm mount errors compound at low altitude.

### The guide pulses seem the wrong size

Run a [calibration](Tracking-and-Guiding#guide-calibration) and read its
message. A measured guide rate more than 2× away from `rig.ini` appends:

```
guide rate measured 2.51"/s vs rig.ini 7.50"/s <- WRONG, set GuideRateArcsecPerSec
```

That message exists precisely because a wrong `GuideRateArcsecPerSec` throws
off pulse sizing and the drift readout with nothing else to indicate it.

---

## AutoStakkert handoff does nothing

Known: the verified command line works from a shell but **not when launched
from inside the application** — AutoStakkert starts, shows its window, and
sits. See
[Image Analysis](Image-Analysis#autostakkert-handoff) and the source header
for the investigation state.

---

## Diagnostic log locations

| File | Contents |
|------|----------|
| `raduk_log.txt` (beside the exe) | Crash-safe application log, including the autofocus V-curve and the `[SHARP] CEILING` self-check |
| `D:\RadukCapture\logs\session_*.log` | Timestamped session events |
| `<capture>_quality.txt` | Per-frame sharpness, mean, peak |
| `data/pointing_model.csv` | Pointing error history with temperature and seeing |

> **Prefer the log over the console.** `printf` goes to an `AllocConsole`
> window that vanishes when closed, which is why several diagnostics were
> moved to `Log()` — the autofocus V-curve, the guide-rate comparison, the
> pointing-model prediction. If a document tells you to "check the console",
> check `raduk_log.txt` too.

---

**Next:** [Operations Guide](Operations-Guide) · [Emulator and Testing](Emulator-and-Testing) · [Glossary](Glossary)
