# Advisors

Four subsystems that produce guidance rather than action: atmospheric
dispersion, auto-exposure, the readiness check, and unprompted advice from
a local model. They share a design stance — **advise, do not act** — and
the readiness check and advisory loop share a second one: **be silent when
there is nothing to say.**

---

## Atmospheric dispersion advisor

[`src/core/ADCAssistant.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/ADCAssistant.h)

Computes the red-versus-blue chromatic separation caused by atmospheric
refraction at a given altitude, and tells you whether a corrector is worth
fitting. No hardware, no threads, no dependencies — a pure function called
from the render loop at ~2 Hz.

### The model

Edlen (1966) refractive index of dry air, simplified two-term Cauchy
expansion:

```
(n-1) × 1e8 = 8342.13 + 2406030/(130 - σ²) + 15997/(38.9 - σ²)
```

where σ = 1/λ in µm⁻¹, scaled to actual pressure and temperature by
`(P/1013.25) × (288.15/T)`.

Dispersion is then the differential refraction between the band edges:

```
Δ_arcsec = (n_blue - n_red) × tan(z) × 206265        z = 90° - altitude
```

Dispersion is always along the altitude axis — vertical in alt-az,
pointing toward zenith.

### Pressure from site elevation

```cpp
const double h = RigConfig::Get().siteElevationM;
const double P = 1013.25 * std::pow(1.0 - 2.25577e-5 * h, 5.25588);
constexpr double T = 288.0;   // ~15 C nominal
```

> **Why this replaced a constant.** Dispersion scales with pressure, so a
> sea-level user was being shown figures ~10% high and a mountain user
> ~15% low, with nothing to indicate it.
>
> It also **corrects** the old constant, which was internally inconsistent:
> its own comment derived it as "sea level 1013, −11.5/1000 m × 528 m",
> which gives ~952 hPa — yet it hardcoded **918**, a value belonging to
> roughly 830 m. Reported figures were about 3.5% low at this site. Small
> in absolute terms (0.2″ on a 5.3″ dispersion) but wrong, and wrong by
> more at any other elevation.

### The band comes from the fitted filter

```cpp
const auto& rigf = RigConfig::Get();
r.bandLowNm  = rigf.filterLowNm;
r.bandHighNm = rigf.filterHighNm;
double n1_red  = refractivity(rigf.filterHighNm / 1000.0);
double n1_blue = refractivity(rigf.filterLowNm  / 1000.0);
```

> **This was the Phase 82 fix, and it is the most consequential one.** The
> edges were hardcoded at 450 and 700 nm — **and 400–700 is precisely a
> UV/IR cut passband, so the model silently assumed that filter was
> fitted.** Remove it and the IMX678 responds out past 1000 nm: the true
> extremes are much further apart, real dispersion is larger, and the
> advice was understated **in the unsafe direction**.
>
> The reverse matters too. An IR-pass filter spans far less refractivity
> than the visible band — which is part of why IR imaging suffers less
> dispersion — and the old fixed figure overstated it badly.

`ADCResult` carries `bandLowNm` / `bandHighNm` so the UI can state what the
figure was computed over. Without that, the same reading means different
things with and without a filter and nothing on screen says which.

### Thresholds

| Separation | `SeverityLabel()` | Flags |
|------------|-------------------|-------|
| < 0.3″ | `NONE` | — |
| < 1.0″ | `MINOR` | `adcRecommended` at ≥ 0.5″ (visible at high magnification) |
| < 2.5″ | `MODERATE` | `adcRequired` at ≥ 2.0″ (clearly degrades planetary detail) |
| < 5.0″ | `SIGNIFICANT` | |
| ≥ 5.0″ | `SEVERE` | |

Valid above ~15° altitude; outside 0–90° it returns zero rather than a
nonsense `tan(z)`.

### What the filter setting is worth, numerically

Same night, same altitude, four filters:

| Filter | 50° alt | 40° alt | 30° alt | Verdict at 40° |
|--------|---------|---------|---------|----------------|
| UV/IR cut (400–700 nm) | 1.13″ | 1.61″ | 2.33″ | recommended |
| None (380–1000 nm) | 1.59″ | 2.25″ | 3.28″ | **REQUIRED** |
| IR pass 685 (685–1000 nm) | 0.29″ | 0.41″ | 0.60″ | not needed |
| Methane 889 (880–900 nm) | 0.01″ | 0.02″ | 0.03″ | not needed |

> **A wrong setting is wrong in both directions that cost you something.**
> Told "UV/IR cut" when the camera is actually bare, the app under-warns at
> 40° — it says *recommended* where the truth crosses into *required*. Told
> "UV/IR cut" when you are shooting IR-pass, it overstates by **4×**, and
> you would fit a corrector for a problem four times smaller than reported.

The application **cannot detect** the filter; nothing in the camera reports
it. So it is configured once in SETUP, persisted in `rig.ini`, and stated
in four places: the FOCUSER panel directly under the ADC readout (so the
assumption sits beside the figure it drives), the SETUP picker, the
readiness check, and the AI assistant's identity block.

Presets in `RigConfig::Presets()`:

| Name | Band | Use |
|------|------|-----|
| `UV/IR cut` | 400–700 nm | Normal colour planetary. The default |
| `None` | 380–1000 nm | Bare sensor. Widest dispersion; an ADC matters most here |
| `IR pass 685` | 685–1000 nm | Cuts through poor seeing; mono-ish detail on Jupiter and Mars |
| `Methane 889` | 880–900 nm | Narrowband Jupiter/Saturn cloud structure. Very dim |

`ApplyFilterPreset()` returns `false` and leaves the band alone for an
unknown name, so a hand-edited `rig.ini` with custom edges is not silently
overwritten. The SETUP picker then shows blank rather than overwriting.

**It is an advisor, not an alignment tool.** It tells you how much
dispersion there is and whether a corrector is worth fitting. It does not
help you dial one in.

Dialling one in is [`src/core/ADCAlignment.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/ADCAlignment.h)
and the `Shift+A` window (Phase 35), which is a different kind of thing
entirely: the advisor predicts from a model and never looks at the
image, while the alignment tool measures the red-to-blue displacement
actually present in the live frame and uses the model only to express
that result as a percentage of it.

Both are gated on `RigConfig::adcFitted`, ticked on the READY screen and
off by default. A readout about a part that is not in the train is noise;
an alignment window showing a residual for one is worse, because it
invites an adjustment to something that is not there.

---

## Auto-exposure assistant

[`src/core/AutoExposureAssistant.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/AutoExposureAssistant.h)

Keeps the histogram peak in the 50–70% range (127–178 DN).

| Design choice | Reason |
|---------------|--------|
| Two modes: **ADVISORY** (text only) and **AUTO** (applies changes) | The default is advice; acting is opt-in |
| **I-controller** (integral only), not P or PID | A P-only controller oscillates with a slow camera loop — each correction's effect is not visible until the next frame cycle |
| **Exposure adjusted first**, gain only when exposure hits its limits | Preserves the noise floor; gain is the expensive knob |
| Clipping detected via `clipPct` | Emergency gain/exposure cut, out of band |
| **HCG floor enforced:** gain never drops below 182 in AUTO | Below 182 the ASI678MC leaves HCG mode and read noise jumps from 0.8 e⁻ to ~3.5 e⁻ |

The HCG floor is the interesting one: naive auto-exposure minimises gain,
which on this sensor is exactly wrong. `GAIN_HCG_THRESHOLD` lives in
`TargetProfile.h` so the floor and the profile defaults cannot disagree.

Usage from the render loop at ~2 Hz:

```cpp
ae.Feed(lastAnalysis.meanBrightness, lastAnalysis.clipPct,
        cam.GetControl(ASI_GAIN), cam.GetControl(ASI_EXPOSURE) / 1000.0);
ae.Tick();
if (ae.IsAuto()) {
    if (ae.SuggestGain()     != ae.CurrentGain())       cam.SetGain(ae.SuggestGain());
    if (ae.SuggestExposure() != ae.CurrentExposureMs()) cam.SetExposure(ae.SuggestExposure());
}
controls.SetAutoExposureStatus(ae.StatusText());
```

Note the shape: the assistant **suggests**, the caller applies. Even in
AUTO mode the class never touches the camera.

---

## Readiness check

[`src/core/Readiness.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/Readiness.h) ·
[`src/gui/ReadinessDialog.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/ReadinessDialog.h)

Phase 68. Runs once the camera and mount are up — not at startup.

> **Why not at startup.** The device check at startup answers "are things
> plugged in". It runs before the camera opens and long before the mount
> connects, so alignment, tracking and focus are all **unknowable** at that
> point. This runs later, where they are not.

### Two design rules, stated in the source

> **The app verifies what it can.** Asking someone to self-certify a fact
> the machine already knows is theatre, and a checklist of those trains
> people to tick without reading. Only three things here need a human, and
> they are the three the software genuinely cannot observe.

> **Silence when ready.** A list that appears every launch becomes a speed
> bump within a week.

### States

```cpp
enum class ReadyState { Pass, Warn, Fail, Unknown, Manual };
//  OK      WARN     FAIL    ????      CHECK
```

`Unknown` is a distinct state from `Fail`, for the same reason
[`TrackingModeKnown()`](Device-Layer#two-virtuals-with-non-obvious-defaults)
exists: "could not determine" is a different claim from "determined to be
broken".

### What it checks

**Verified by the app:** camera streaming, dropped frames, focuser present,
mount connected, **mount aligned**, **tracking actually driving**, observing
site known, disk space, profile loaded.

The two in bold are the ones that will stop a night dead.

**Asked of the human** — three things, and only three: dust cap off, tube
cooled to ambient, cables dressed. These are reminders and **never** cause
the dialog to appear on their own. Ticking all three also resets the
[cord-wrap count](Mount-Control#cord-wrap), since that is what confirming
the cables means.

**Stated rather than asked:** a `Filter` row naming the filter the app
believes is fitted and the band it uses for the ADC figure.

> It changes only when the camera comes off the scope, so being asked about
> it nightly would be noise. It is there to be read on a night the list
> already opened.

### Behaviour

One log line either way:

```
[READY] 7 ok, 0 warn, 1 fail, 0 unknown, 3 to check by hand
[READY]   Alignment  FAIL  mount reports NOT aligned - hand control,
          Align > Quick Align (needs no stars)
```

**The dialog appears only on a failure or warning.**

**It waits up to two seconds for the camera's first frame before running.**

> Without that it raced the capture thread it was checking, reported
> `Camera FAIL — open but no frames have arrived` on a camera that was
> about to work, and then blocked the launch with a modal dialog about it.
> A re-check you ask for yourself does **not** wait, because by then no
> frames means no frames.

**Rows refresh every two seconds while the dialog is open.** Walk to the
mount, run Quick Align, and watch the row turn green without restarting
anything.

**Explain this** opens the Help section for the first failing row.
**Continue** is always available — this is advice, not a gate.

**READY** in the TELESCOPE section reopens it any time, and always shows the
list whatever it says, because you asked for it. `ShowReadinessCheck=0` in
`rig.ini` suppresses the automatic appearance; the button still works.

> **Why even the automatic appearance can be switched off.** A tool that
> cannot be silenced is one people learn to resent.

---

## Advisory loop

[`src/core/AdvisoryLoop.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/AdvisoryLoop.h)

Phase 12b. The [chat panel](AI-Assistant) answers when asked; this watches
and speaks when it has something worth saying. Same Ollama, its own client,
so advice and a conversation never block each other. `W` toggles it;
`AdvisoryEnabled` defaults to **off** — it costs GPU time and not everyone
wants a running commentary.

### Advisory by construction, not by intention

> This class produces a `std::string` and has **no reference to the camera,
> the mount, the focuser, or the capturer.** It cannot act on the rig even
> if the model asks it to, and the caller only ever receives text. That is
> the whole safety argument, and it is why the design is this shape.

### Four rules against becoming noise

> A feature like this gets switched off and left off the moment it becomes
> noise, so:
>
> 1. Asked at most once every `kIntervalS`, and **never while a recording
>    is running** — inference competes with the capture pipeline for the
>    machine, and a dropped frame costs more than any advice is worth.
> 2. Only asked when the rig state has **actually changed** since the last
>    question. Standing still produces no new advice, because nothing has
>    happened to advise about.
> 3. The model is told to answer "OK" when there is nothing useful to say,
>    and **that answer is swallowed.** Silence is a valid result.
> 4. The reply is capped to one short line; anything longer is truncated
>    rather than shown, because the display for this is a notification bar,
>    not a transcript.

It gets a compact digest rather than the full help file the chat panel
receives — this runs unattended and repeatedly, and sending a 79 KB
reference every few minutes would be absurd.

### Why the model does not decide

> The first version handed it the rig state and asked for advice.
> `llama3.2:3b` answered, confidently and wrongly: *"try lowering gain for
> improved dispersion visibility"* and *"UV/IR filter may cause excessive
> noise; try a narrowband filter"*. **Neither is true, both sound
> authoritative, and the person this feature exists for cannot tell the
> difference — which makes wrong advice worse than none.**
>
> So the **caller detects the conditions in code**, where they are
> deterministic and checkable, and the model is given those concerns and
> asked only to phrase one. It cannot invent a cause it was not handed, and
> with no concerns it is not asked at all.

That last sentence is the architectural commitment: the model is a
phrasing layer over conditions detected in C++, not a reasoning layer over
raw state. It is worth holding onto if the feature is ever extended.

`AdvisoryIntervalS` is configurable, 30–3600 s, default 180.

---

**Next:** [AI Assistant](AI-Assistant) · [Configuration Reference](Configuration-Reference) · [Image Analysis](Image-Analysis)
