# Glossary

Domain vocabulary, as this project uses it. Where a term has a specific
meaning *here* that differs from general usage, that is noted.

---

## Astronomy and optics

**Alt-az** — A mount that moves in altitude (up/down) and azimuth
(left/right), rather than tracking the sky's rotation on one axis. Simpler
mechanically, but it needs **latitude and local time** to compute tracking
rates at all, and the field slowly rotates during a long exposure. The
NexStar 8SE is alt-az, which is why
[field rotation ages out the guide calibration](Tracking-and-Guiding#why-calibration-deliberately-does-not-persist).

**Altitude** — Angle above the horizon, 0–90°. Nearly everything gets worse
at low altitude: more air to look through, more dispersion, more
obstructions.

**Azimuth** — Compass bearing, 0–360° true. Note that for
[cord wrap](Mount-Control#cord-wrap) what matters is *cumulative travel*,
not azimuth: 350° and 10° are adjacent on a compass but 340° apart in
travel.

**Arcsecond (″)** — 1/3600 of a degree. The unit of everything that matters
here: plate scale (0.203″/px), seeing (2–5″ of jitter), dispersion
(0.3–5″), guide deadbands (2–2.5″).

**Bortle** — A 1–9 scale of sky darkness; lower is darker.

**Culmination** — When an object is highest in the sky, and therefore best
placed.

**Derotation** — Correcting for a planet's rotation across a long capture.
Requires accurate per-frame timestamps, which is why the
[SER timestamp trailer carries capture time, not write time](Recording-and-SER-Format#timestamps-and-why-they-are-capture-time).

**Dispersion (atmospheric)** — The atmosphere acting as a weak prism,
splitting a target into faint red and blue fringes along the altitude axis.
Worse at low altitude, and dependent entirely on **which wavelengths reach
the sensor**. See [Advisors](Advisors#atmospheric-dispersion-advisor).

**J2000** — The standard coordinate epoch. All catalog coordinates here are
J2000.0 (ICRS).

**LST — Local Sidereal Time** — Star time; what you need with RA to know
what is overhead. Derived from UTC plus longitude.

**Lucky imaging** — Recording many frames and keeping only the sharpest,
exploiting moments when the atmosphere happens to be still. The entire
premise of planetary imaging, and the reason the
[quick-look stacker's percentile gate](Image-Analysis#quick-look-stacker)
exists: *"Lucky imaging exists to reject the mean."*

**Plate scale** — Arcseconds of sky per pixel. Here 206.265 × 2 µm /
2032 mm ≈ **0.203″/px**. The nominal value seeds overlays and solver hints;
the [correction loop uses a measured transform instead](Tracking-and-Guiding#guide-calibration).

**Plate solving** — Identifying star patterns in an image to determine
exactly where the telescope is pointing. Done here by
[ASTAP](Sky-GoTo-and-Plate-Solving#plate-solving-with-astap).

**RA / Dec** — Right ascension (hours) and declination (degrees): celestial
coordinates, fixed to the sky.

**Seeing** — Atmospheric turbulence blurring and shifting the image, 2–5″
typically. **Nothing is broken when the image wobbles.** Note that this
project's `Seeing n/10` readout is **relative**, not an absolute measure of
the night — see below.

**Sidereal** — The rate the sky appears to rotate, ~15″/second. Relevant
number: with the mount not driving, a planet crosses the 10 px dwell window
in **0.14 s**.

**Zenith** — Directly overhead. Dispersion is zero here and rises with
zenith distance.

---

## Hardware

**ASI678MC** — The camera. Sony IMX678, 3840×2160, 2 µm pixels, RGGB Bayer,
USB 3.0.

**Backlash** — Mechanical slop meaning the position reached depends on the
direction of approach. [Autofocus always approaches from below](Autofocus#backlash-approach).

**Bayer / RGGB** — The colour filter mosaic over the sensor: one red, two
green, one blue per 2×2 block. Two consequences run through this codebase:
sharpness taps must be **±2 pixels** (±1 samples a different colour), and
the sampling lattice must be anchored to **even coordinates**.

**DN — Data Number** — A raw pixel value, 0–255 at 8-bit. Used for noise
and exposure levels.

**EAF** — ZWO Electronic Automatic Focuser. Absolute positioning,
0–600,000 steps; this rig focuses near 168,187.

**HCG — High Conversion Gain** — An ASI678MC sensor mode that
**auto-activates at gain ≥ 182**, dropping read noise from ~3.5 e⁻ to
0.8 e⁻. Counter-intuitively this means **always use gain ≥ 182 for
planets**, and it is why the [auto-exposure assistant](Advisors#auto-exposure-assistant)
enforces a gain *floor*.

**NexStar** — Celestron's serial command protocol. Used directly here, with
no ASCOM or CPWI layer.

**ROI — Region of Interest** — A crop of the sensor read-out. Smaller means
less data and higher frame rate. ASI constraints: width a multiple of 8,
height a multiple of 2.

**StarSense AutoAlign** — Celestron's automatic alignment accessory.
**Quick Align** is the no-stars alternative, and the single most common
missing step on a failed night.

**Unity gain** — Where one electron produces one DN, ~100 on this sensor.
Noisy, and avoided for planets.

---

## Software, general

**ASTAP** — Free local plate solver. Requires its own star catalog (H18 or
D80).

**AutoStakkert (AS!4)** — The stacker this project hands off to rather than
competing with. It grades quality **per alignment point**, which is why a
single per-frame score could not drive its selection.

**CPWI** — Celestron's PC control software. Not used; it would hold the COM
port exclusively.

**Ollama** — Local LLM runtime on `localhost:11434`. The
[assistant](AI-Assistant) talks only to this — no cloud, no keys.

**SER** — The planetary video format: uncompressed frames, a 178-byte
header, per-frame UTC timestamps in a trailer. See
[Recording and SER Format](Recording-and-SER-Format).

**VSOP87** — Public-domain planetary ephemeris. Accurate to ~1 arcmin for
2000–2100, which is ample at this field of view.

---

## This project's own terms

**Acquisition stage** — One of `Off`, `Searching`, `Calibrating`,
`Centering`, `Locked`. See [Tracking and Guiding](Tracking-and-Guiding).

**Advisory** — Guidance that produces text and cannot act. `AdvisoryLoop`
holds no device references at all, which is the safety argument rather than
a convention.

**Capture format** — Resolution plus bit depth together. Changed through one
shared path since Phase 84; **changing it closes the current recording**.

**Cord wrap** — Cumulative azimuth travel since `CABLES OK` was last
pressed. Signed: 180° out and back reads 0.

**Dwell** — The centroid staying within `CENTRE_THRESHOLD_PX` (10 px) for
`DWELL_FRAMES` (8) consecutive frames. The gate between Centering and
Locked.

**Framing shot** — `Shift+S`. Gain 570 and a 2-second exposure for five
seconds, then an automatic restore. Refuses while recording.

**Gate** — The capture quality gate. Only frames whose seeing score beats
the threshold are recorded. **Its scale changed in v1.2.2.**

**Keep-out sector** — An azimuth arc you cannot point at. **Clockwise from
start to end**; equal values mean none.

**LIVE / STACK** — Sharpness of the single frame and of the stacked frames.
Relative only; no universal good value.

**Night vision** — `N`. The **whole interface** goes red, not just the
preview. Persisted across sessions.

**Observer bearing** — `ObserverBearingDeg`: from the mount to where you
stand, degrees true. One number, because
[everything the align helper says is relative to it](Mount-Control#align-helper).
`−1` means unasked; **0 is a legal bearing**.

**Overrun** — A ring buffer forced to overwrite an unread frame (preview) or
refusing a frame because it is full (write buffer). The second loses a
recorded frame; the first does not.

**Pipeline wedge** — 20 consecutive camera timeouts. Triggers an automatic
`RestartCapture()`.

**Quality score** — Per-frame sharpness in `_quality.txt`. Measured on the
writer thread since Phase 76. **AutoStakkert does not read this file.**

**Readiness check** — The post-connect verification that
[appears only on a failure](Advisors#readiness-check).

**Seeing score** — `1 + 9 × (sharpness / best recent sharpness)`. **Relative
to the best frame of the last ten seconds, not an absolute measure of the
night.** It touches 10 whenever a frame is the best seen recently. Watch how
far it **dips**: steady air stays 9–10, poor air swings below 5. Read
`sharpnessPct` for absolute comparison.

**`SHARP_CEIL`** — The normalisation constant turning Laplacian variance
into a 0–100% gauge. Currently 1000. Set too low it **clips the focus curve
flat and autofocus has no peak to find.** The app self-checks it and logs
which way to change it.

**Slot states** — `FREE → WRITING → READY → READING → FREE`. `READING`
exists to prevent a use-after-free when the renderer holds a frame.

---

## Units quick reference

| Quantity | Unit here | Note |
|----------|-----------|------|
| RA | hours | 0–24 |
| Dec, altitude, azimuth | degrees | |
| Angular separation, drift, deadbands | arcseconds | |
| Plate scale | arcsec/pixel | 0.203 on this rig |
| Guide rate | arcsec/second | ~7.5 at NexStar rate 1 |
| Guide pulses | milliseconds | Sub-10 ms rejected as noise |
| Exposure | milliseconds | Rail 0.032 – 2,000,000 |
| Focuser | steps | 0 – 600,000 |
| Frame timestamps | QPC microseconds | Converted to 100 ns ticks for SER |
| Wavelength | nanometres | Filter band edges |
| Pressure | hPa | Derived from `SiteElevationM` |
| Elevation | metres | |
| Pixel values | DN | 0–255 at 8-bit; RAW16 shifted `>> 8` |

---

**Back to:** [Home](Home) · [Code Map](Code-Map) · [Operations Guide](Operations-Guide)
