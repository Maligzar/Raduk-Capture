# Roadmap — next phases

Written 2026-10-12, after a long emulator session. Sources: the owner's
findings from that session, the existing roadmap table in
`RadukCapture-Architecture.md`, and `docs/CLEAR-NIGHT-CHECKLIST.md`.

Nothing here is started. This is the ranking and the plan.

---

## How this is ranked

A telescope is used on a small number of usable nights a year. That
makes the ordering principle simple:

1. **Anything that silently corrupts or loses a night's data.**
2. **Anything that could move the telescope by accident, or hurt someone.**
3. **A feature that is advertised and does not work** — worse than one
   that does not exist, because you plan around it.
4. **Things that cost time at the eyepiece**, every session.
5. **Things that only cost comfort.**
6. **Things that cost nothing now and pay later** — docs, structure.

A separate axis cuts across all of it: **can it be settled on the
bench, or does it need sky?** Bench work can happen tonight. Sky work
has to be queued and batched, because a clear night is the scarce
resource and you do not want to spend it discovering that a number was
never calibrated.

---

## Tier 0 — broken now

### P0.1 · AI assistant stopped answering
**Bench. Small, unless it is the driver change.**

Reported: "AI is not answering questions. Did it handle all of the
driver updates OK?"

This is the highest-ranked item because it is a feature that exists,
is on screen, and does nothing — and because the suspected cause is a
machine-level change that may have broken more than the one symptom.

Diagnosis order, cheapest first:

1. Is the model process reachable at all? The panel talks to a local
   Ollama-style endpoint; if that service is not running or moved port,
   nothing in the app is at fault.
2. Did the GPU driver change remove or starve the compute runtime? This
   rig is pinned to a unified pre-R610 branch for the 2060 + P1000 pair
   (see the memory note on the driver constraint). A model that used to
   run on the GPU and now silently falls back — or fails — is the most
   likely explanation for "it stopped after updates".
3. Is the failure visible anywhere? This is the third time in this
   codebase that a feature's only diagnostic was a `printf`, which a
   GUI-subsystem build discards. Whatever the cause, the AI panel needs
   its errors in `raduk_log.txt` before anything else is guessed at.

**Do 3 first regardless.** It is small, and without it every other step
is guesswork.

### P0.2 · Status panel bleeds into everything else
**Bench. Small-medium.**

Reported as part of the status redesign, and separated here because the
bleeding is a defect, not a layout preference. A panel that draws
outside its own bounds corrupts whatever is next to it, which on a
five-screen desk now means other readouts.

Fix the bleed first and in isolation, then do the redesign (P2.1).
Rolling them together risks shipping a redesign that happens to hide
the bug rather than fix it.

---

## Tier 1 — safety

### P1.1 · READY as a modal, everything else greyed
**Bench. Medium.**

Requested explicitly as a safety feature: the READY check becomes its
own window, large, centred on the main screen, holding focus, with
every other screen dimmed until it is acknowledged.

Worth noting what this *re-introduces*. A modal readiness dialog cost
most of an afternoon in Phase 84, because it blocked startup — the app
appeared hung. The lesson then was not "never make it modal", it was
"never let it block the message loop before the UI exists". So:

- Dim the other screens with borderless always-on-top overlay windows,
  one per display, not by disabling input globally.
- Keep the main message loop running throughout. The gate is about
  attention, not about freezing the program.
- It must be dismissible from the keyboard. A modal you can only clear
  with a mouse is a hazard of its own in the dark.

Pairs naturally with **Phase 46 (Connect/Disconnect All Hardware)**,
already on the roadmap at priority 2 — the readiness gate and the
single connect action are the same moment in the session.

### P1.2 · Map selection sets the GoTo target
**Bench. Small.**

Reported: picking an object on the map and slewing should write that
object into the Telescope card's GoTo field, so that a later mis-click
on GOTO sends you to the object you were already going to, rather than
to whatever was left in the box.

This is a small change with a real safety argument, and it is the
cheapest item in Tier 1. It makes the dangerous control's default the
*intended* target rather than a stale one.

---

## Tier 2 — accuracy, and what needs sky

These decide whether the numbers on screen mean anything. Several are
already on the clear-night list; the new reports slot into it.

### P2.0 · Histogram accuracy — deep dive
**Mixed. Bench first, then sky. Medium.**

Asked directly: "Is this histogram accurate?"

Honest answer: **partly bench-answerable, and it should be taken apart
on the bench before a night is spent on it.**

What can be settled without sky, by feeding known synthetic frames
through `FrameAnalyzer` and checking the output against hand
calculation:

- Does the RGGB demosaic pick the right pixels for each channel? A
  half-pixel error here mislabels colour balance permanently.
- Is the G channel the average of Gr and Gb, and is that the right
  choice? It halves G's noise relative to R and B, which biases any
  comparison between channels.
- Do the clip percentages count what they claim — pixels at 255 after
  the 16-bit left-align shift, not after display stretch?
- Does calibration (dark/flat) run before or after the histogram? If
  after, the histogram describes an image nobody is looking at.
- Is it measuring the raw frame or the stretched preview? These give
  completely different answers and only one of them is useful for
  exposure.

What genuinely needs sky: whether the resulting numbers match a real
sensor's behaviour, and whether the 99.9th-percentile highlight that
auto-exposure now drives is the right statistic on a real planet.

**Mark the second half as live-sky debt** — it joins item 10.

Related existing phase: **36, Sensor Analysis / Smart Histogram**
(priority 3, Hard). The deep dive should be done first; it may well
absorb or redirect Phase 36.

### P2.1 · SHARP / PK / AV accuracy
**Live sky. Already debt item 1.**

Reported as questionable, with the correct caveat that it may be the
emulator. It is almost certainly the emulator: `SHARP_CEIL = 1000`
against a noiseless synthetic frame gives readings around 0.5%, and the
app's own watcher already says so — `CEILING TOO HIGH: 400 of 400
readings at or below 10%`.

This cannot be settled on the bench, because the constant exists to be
calibrated against real sensor noise. No new phase; it is clear-night
item 1 and it is already instrumented to report itself.

One bench-side improvement is available: the sharpness window could
show the raw variance alongside the percentage, so the ceiling's effect
is visible rather than inferred.

---

## Tier 3 — the five-screen workspace, finished

Phase 42 delivered placement, persistence and a rescue path. These are
the parts that are now obviously missing once windows live on screens
of their own.

### P3.1 · Status as its own window, and out of the side panel
**Bench. Medium.** *(after P0.2)*

Three parts, in order:

1. Extract the status drawing the way the histogram, sharpness and
   temperature painters were extracted — one implementation, two
   callers.
2. A `StatusWindow` beside the histogram on the chart screen.
3. Remove it from the control panel's GDI strip.

Step 3 is what the owner actually asked for and it is what removes the
bleed's blast radius for good.

### P3.1b · Status telemetry: all drives, and a disk figure that moves ✅ DONE (v1.2.6)

Both parts done. What follows is the original entry, with the outcome
of each part at the end of it.

**Bench. Small-medium. Two separate things.**

The status window exists now (`Shift+D`, on the telemetry screen). What
it shows is the same four numbers the old panel section did. Two
additions were asked for during the session that produced it.

**1 · Every drive, by letter.** Show size and free space for each
logical drive, not just a single figure for the capture directory. The
machine writes SER files that run to gigabytes a minute, and "which
disk still has room" is a question you want answered before a capture
rather than during one.

Mechanically simple: `GetLogicalDriveStringsW` for the list,
`GetDiskFreeSpaceExW` per drive, skip anything that is not
`DRIVE_FIXED` unless it is the capture target. The interesting part is
layout, since the count is not known in advance and the window is
resizable -- one row per drive with a bar, the capture drive marked,
and the rows sized to fit whatever the window currently is.

Worth showing **free** space rather than used. Used is what the current
figure reports, and free is what actually decides whether the night's
capture completes.

**2 · The disk figure reads zero while recording.** Reported under a
synthetic test. The number comes from:

```cpp
float diskGB = capturer.IsRecording()
             ? (float)capturer.BytesWritten() / 1e9f : 0.0f;
```

So it is bytes written this session, not disk usage at all -- the label
says HDD and the bar is scaled against 500 GB, which makes it read as
free space when it is nothing of the kind. If `BytesWritten()` is not
advancing during the emulator's synthetic recording, that is a second
bug underneath the first and needs checking separately: a byte counter
that stays at zero while frames are being written would also mean the
capture-size estimate and any limit-by-size logic are wrong.

Do the diagnosis before the display work. If `BytesWritten()` is
genuinely stuck, that is worth more than the drive listing.

**Outcome, part 2 — the counter was not stuck.** The underlying bug was
found earlier in the session: `StartRecording` was being handed a
hardcoded `8` for bits-per-pixel instead of the ROI's actual depth, so
the write buffer was sized wrong. With that fixed the counter advances
correctly. Measured on the emulator at 1280x960 RAW8: 0.000 GB at
start, 0.070 GB at 4s, 0.188 GB at 10s — 0.0197 GB/s against a
predicted 1,228,800 bytes x 15 FPS = 0.0184 GB/s. Right, not merely
non-zero.

The display problem the entry describes was real and is also fixed: the
row is now `REC`, not `HDD`, and is scaled against free space on the
capture drive rather than a flat 500 GB.

**Outcome, part 1 — one row per fixed drive**, by letter, with the
capture drive marked `*`, the bar showing space used and the spelled-out
number showing space free. Thresholds are in GB rather than percent, for
the reason in the entry above. Verified against `Win32_LogicalDisk`:
C: 352/499 GB, D: 641 GB/1.00 TB, S: 1.45/2.00 TB, percentages 71/64/72
— exact matches.

Two things the entry did not anticipate:

- **The scan runs on a background thread**, every five seconds. A
  sleeping external USB drive can take seconds to answer, and on the UI
  thread that would stall the preview and the capture loop.
  `SetThreadErrorMode(SEM_FAILCRITICALERRORS)` as well, or a removable
  drive with no media pops a system dialog from a background thread
  mid-exposure.
- **Free space is the per-user figure**, not the raw free total. Under a
  disk quota they differ, and the smaller one is what decides whether
  the capture fits.

### P3.2 · Hide the viewfinder overlays — `U` ✅ DONE (v1.2.6)

One key hides everything drawn over the viewfinder *except* the
reticle. Done as `U`, either case.

The earlier `Shift+O` version gated only the side panels and the
quality graph, so pressing it left the caption, zoom hint, ADC arrow,
pixel readout and HUD status line still over the image — the view was
not actually cleared. The rest turned out to be nearly free: caption,
zoom hint, mode labels, pixel readout and the LIVE line all live inside
one `if (m_hudFont && m_titleFont)` block in `PreviewRenderer::Render`,
so gating that block plus the ADC call covered everything outstanding.

The reticle is drawn *before* that block and is deliberately left
ungated: it is the one overlay you look through the image with rather
than read off it, and clearing the view to judge a target is exactly
when you still want to know where centre is.

Measured on the emulator, lit pixels per region, `U` on vs off:

| Region | Overlays on | Overlays off |
|---|---:|---:|
| Caption + zoom hint | 2904 | 22 |
| Histogram panel | 5316 | 218 |
| Temperature panel | 2415 | 218 |
| Sharpness graph + LIVE line | 2976 | 0 |
| Reticle (vertical arm, dark sky) | 320 | **320** |
| Status bar — a GDI control, not an overlay | 4807 | 4817 |

The setting persists in `rig.ini`. The key there is still
`OverlayTelemetry`, from when the gate was narrower; renaming it would
have thrown away the saved state to buy a tidier word.

### P3.3 · Resizable AI / GoTo / Help, with larger fonts
**Bench. Medium-Hard. The biggest of the workspace items.**

Requested: let the three panels use a whole screen through normal OS
windowing, with fonts scaled up to suit.

This is harder than it sounds and the estimate should say so. These
three windows lay their child controls out at absolute pixel positions
computed once at creation. Making them usable at 1920x1080 means:

- A layout pass that runs on `WM_SIZE` rather than only at creation.
- Fonts created from the window size, as the chart windows now do,
  rather than fixed point sizes.
- A minimum size, so the controls cannot be crushed.

Doing this properly for one window first — **Help is the best
candidate**, being mostly a text box and therefore mostly free — would
prove the pattern cheaply before committing to GoTo, which has the
densest layout in the app.

### P3.4 · Map window larger
**Bench. Small — possibly already done.**

Asked: "can the map window be larger now that we can use an entire
screen?"

It already can: it takes a maximise box and `F11` fills whichever
screen it is on, and the capture confirms it running at 1936x1048. So
the question is really whether its *contents* scale — whether the star
labels, the object list and the hit targets grow with the window or
stay at their original pixel sizes. **Check before planning work.** If
they already scale, this is closed; if not, it is the same font-scaling
job as P3.3 and should be done alongside it.

### P3.5 · Mount controller enlarged, laid out like a gamepad — ❌ DROPPED (2026-10-14)

Owner's call: the current mount panel is fine as it is, and the real
Xbox controller does not need an on-screen twin to be wired against.

The earlier entry had this as a prerequisite for P4.1 on the reasoning
"design the on-screen layout around the controller first, then wire the
real controller to it". That reasoning does not survive contact with
what P4.1 actually is. Wiring a physical controller is a mount-command
and input-mapping job; it reads nothing from the on-screen panel and
would not be made easier by the panel resembling a gamepad. The
dependency was invented, not found.

P4.1 is therefore independent and can be done at any time.

---

## P3.6 · Make the assistant worth asking

> **Option C done 2026-10-17, and it found a real defect.** The prompt
> told the model the ASI678MC has **2.9um** pixels. It has 2.0, and the
> plate scale two lines below said 0.203"/px — which is 2.0um at
> 2032mm, not 2.9. The model was handed two facts that cannot both be
> true and no way to choose. `CODE_REVIEW_2026-06-10` caught this at
> line 503 in June and it shipped anyway.
>
> Also done: the plate scale now comes from `rig.ini` instead of being
> typed into the prompt, so a Barlow or a reducer changes it rather
> than leaving a stale number being quoted confidently; and the field
> of view is computed and stated for both the full sensor and the live
> capture format, which is what "will the moons fit" actually asks.
>
> **The written check the entry asked for exists:**
> `tests/ai_context_facts.cpp`, in the gated suite. Fifteen checks on
> the facts the model is given, because a model cannot be right about
> a number it was handed wrongly. It includes the **site-precision
> rule as a test** rather than a comment — no coordinate-sized number
> in the context may carry more than one decimal. Everything in that
> prompt can be quoted back, logged, or pasted into a bug report.
>
> **Still to do: B then A.** B is retrieval over the real corpus and
> remains where the value is. A is a larger model and is still behind
> D4.


**Bench. Medium. Not urgent — it is now honest about what it does not
know, which was the dangerous part.**

The assistant is currently llama3.2:3b, running on CPU, answering only
questions about the app. That is a deliberately narrow box, drawn
because the model outside it was confidently wrong: asked about
planetary exposure it offered "typically around 1-10 seconds", out by
three orders of magnitude.

The box is the right short-term answer. This phase is about earning the
right to widen it.

### What is actually limiting it

Three separate things, worth separating because the fixes differ:

1. **The model is small.** 3B parameters is enough to paraphrase a
   document in front of it and not enough to be trusted on domain
   facts from memory.
2. **It runs on CPU**, because of the driver/CUDA conflict in D4. That
   caps how large a model is practical.
3. **Its knowledge of the rig is a prompt**, rebuilt per question, and
   capped at 8 KB of help text by relevance matching. It has no memory
   of previous sessions and no access to what actually happened on
   them.

### Options, cheapest first

**A · A larger model, still local.** 7-8B instruct models are
materially better at not inventing numbers. On CPU, an 8B at ~5 tok/s
is slow for chat but fine for the advisory loop, which writes one
sentence every few minutes and nobody waits for. Worth measuring before
assuming: the prompt-processing cost matters more than generation here,
because the context is long and the answers are short.

Blocked behind D4 if the GPU is wanted. An 8B on the 2060's 6 GB is
feasible at 4-bit, which makes solving D4 worth more than it looks.

**B · Retrieval over the real corpus.** The help file is already
section-matched. The same approach extended to the architecture notes,
the clear-night checklist and the session logs would let it answer
"what did I do about focus drift last time" — questions nobody else can
answer, because the data is local and private. This is the option with
the highest ratio of usefulness to risk: it widens what the model can
be *right* about without asking it to know more.

**C · Ground the numbers in the app, not the model.** The cases where
it was most wrong are exactly the cases the app already computes:
exposure, gain, plate scale, dispersion, seeing. Rather than letting
the model recall them, give it the app's own figures as facts in the
prompt and let it do what it is good at, which is phrasing. The
advisory loop already works this way and is the model of how to do it —
it was restructured precisely so the model phrases concerns that code
supplies.

**D · A fine-tune or a domain model.** Most work, least certain payoff,
and it would need a corpus that does not exist yet. Listed for
completeness and not recommended.

### Suggested order

**C, then B, then A.** C is cheap and removes the worst failure mode
without touching the model at all. B is where the real value is, and it
gets better as the session logs accumulate. A is the one that costs
hardware work, and it should come last because it makes the other two
better rather than replacing them.

### How to know it worked

The scope rule stays until a candidate passes a written check: a set of
questions with known answers drawn from the rig's own numbers — plate
scale, the exposure slider's range, what the ROI button does, what the
clip percentage means — plus the ones that caught the current model
out, like planetary exposure times. Widen the scope one category at a
time, and only for categories that pass.

Safety stays out of the model's hands regardless of how good it gets.
`LooksLikeSafety()` and the forced safety section are not a workaround
for a small model; they are how a safety-critical answer should be
produced on a telescope shared with children.

---

## Tier 4 — new capability

### P4.1 · Xbox controller input
**Bench — needs a controller, not a clear night. Medium.**

Operate the mount, focuser and capture by feel, in the dark, with
gloves on, standing at the telescope instead of at the keyboard. That
is a bigger quality change than the feature list suggests, and unlike
most of what is left it can be finished indoors.

It also buys something a keyboard physically cannot give: **a
proportional slew rate.** Arrow keys are on or off, so centring a
planet is a series of taps; a stick is a continuous rate control, which
is the right instrument for the job.

#### The blocker, which is not in the controller

`NexStarMount::SlewFixed()` sends only the fixed-rate command:

```
P 2 16|17 36|37 <rate 0-9> 0 0 0
```

Nine discrete rates. Wire a stick to that and the stick is a
nine-position switch — all the analog resolution is thrown away at the
last step, and the thing that made the controller worth having is gone.

The NexStar protocol also has a **variable-rate** command in the same
`P` family:

```
P 3 16|17 6|7 <rate_hi> <rate_lo> 0 0      rate = arcsec/sec x 4
```

So step one is `SlewVariable(axis, dir, arcsecPerSec)` alongside
`SlewFixed`, on the same fire-and-forget path. Small, and independently
useful — continuous rate is also what any future centring or
closed-loop nudge wants, and `GuideRateArcsecPerSec` already exists in
`RigConfig` for a related purpose.

**Verify it against the mount before building on it.** The command is
documented but hand-controller firmware varies. If this mount rejects
it, the fallback is quantising the stick to the nine fixed rates — the
phase still works, it is just coarser, and the roadmap should say which
of the two shipped rather than leaving it ambiguous.

#### Feasibility — checked on this machine, 2026-10-14

| | |
|---|---|
| `XInput1_4.dll` | present (10.0.22621.1) |
| SDK 10.0.26100.0 | `XInput.h` and x64 `xinput.lib` both present |
| Compiles and links | yes — a probe was built and run, all four slots enumerate |
| USB headroom | two independent USB 3 host controllers, Intel and ASMedia, so the camera need not share one with a pad |
| **Bluetooth** | **none. No radio in this machine at all** |

So the software side is free and the hardware side needs one purchase:
a cable, an Xbox Wireless Adapter, or a generic USB Bluetooth dongle.
The adapter is the one that matches the phase's purpose, because a
cable tethers you back to the desk and standing at the telescope is the
entire point.

#### Input layer

XInput: two functions, ships with Windows, made for this controller.

- Poll connected pads in the existing readout loop and use
  `dwPacketNumber` to skip unchanged state.
- **Polling empty slots is fine, contrary to the usual advice.** An
  earlier draft of this entry repeated the received wisdom that
  `XInputGetState` on a disconnected slot is slow enough to need a
  once-a-second scan instead. Measured here it is **21-24 us**, so all
  four empty slots cost 89 us -- 0.27% of a 33 ms frame at 30 fps. That
  warning appears to date from older XInput versions; on this machine
  it is not worth engineering around, and a plain poll every frame
  gives hot-plug detection for free.
- **On disconnect, stop both axes immediately.** A controller battery
  dying mid-slew must not leave the mount running. This is the single
  most important line in the phase.

#### Deadzone and response curve

Pure arithmetic on a struct, so this part is unit-testable with no
hardware and belongs in `tests/` as a merge gate.

**Radial deadzone, not per-axis.** Per-axis thresholds carve a *square*
dead region: push diagonally and each axis clears its own threshold
while the stick is barely off centre, so a near-resting stick creeps
the mount diagonally. Take the magnitude, subtract the deadzone,
rescale the remainder.

`XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE` (7849 of 32767) is a starting
point, not an answer — a worn stick drifts past it. Configurable, and
default generous. This is the one place a bug moves the telescope with
nobody touching it.

**Expo response**, not linear: `rate = maxRate * magnitude^k`, k around
2.5–3. Linear spends the first tenth of stick travel on the entire
useful centring range and the rest on speeds nobody wants. The curve is
what makes the stick feel like an instrument rather than a switch.

#### Arming — the part that deserves the most thought

This telescope is shared with family, children included, and a gamepad
is the most inviting object in the room to a child. The READY gate
exists for that reason and this needs the same care.

- **A deadman, not a mode.** The mount moves only while a shoulder
  button is held; release and it stops. A mode you toggle is a mode you
  forget, and a forgotten armed state plus a knocked stick is an
  unattended slew. The deadman also solves the controller-set-down-on-a
  -table case for free.
- Disarmed on connect, on disconnect, and when the app loses focus.
- A resting stick produces no motion in any state.
- **STOP is one button, ungated by the deadman, and never a combo.**
  Combinations fail in the dark with gloves on, which is exactly when
  STOP is needed.
- The mount panel shows ARMED. "Is it live" has to be answerable by
  looking, not by remembering.

#### Mapping

A starting point, not settled — the ordering wants trying by hand.

| Control | Action |
|---|---|
| Left stick | Slew, proportional. Deadman held |
| D-pad | Single fine nudges, one step per press |
| Shoulder (hold) | Deadman / arm |
| Shoulder (tap) | Step the rate preset |
| Face button | STOP. Ungated, always live |
| Face buttons | Snapshot, record, reticle cycle |
| Start / Back | GoTo panel, READY screen |

**Focus is a trap worth naming.** Analog triggers have no detent, so a
resting hand creeps the focuser, and a stray focus command during a
capture is wasted frames. Put focus on the D-pad as discrete steps, or
behind its own hold — not on a free-running analog axis.

#### Rumble, as an output channel

`XInputSetState`. Worth more here than it sounds, because during a
session you are at the eyepiece and not looking at any of the five
screens: slew limit reached, cord-wrap warning, recording started or
stopped, autofocus finished. The controller is in your hands and not on
the mount, so none of it can shake the image.

#### Configuration

Deadzone, curve exponent, maximum rate, button mapping and rumble
on/off in `rig.ini`, like every other preference.

#### What gates a merge

The deadzone, curve, arming state machine and mapping are all pure
logic driven by synthetic `XINPUT_GAMEPAD` values — `tests/` can cover
every one of them, including the cases that matter: a resting stick
produces zero rate, a diagonal near-centre stick produces zero rate,
STOP works while disarmed, and a disconnect produces a stop command.

What needs the hardware is only the feel of the curve and whether the
mount accepts the variable-rate command. Neither needs sky.

---

### P4.2 · A deep wiki, and a documentation cleanup
**Bench. Medium — mostly writing.**

Requested: a wiki in the repo explaining how the architecture works and
*why the decisions were made*, with the loose Markdown files moved into
it or archived, so the wiki becomes the thing that is maintained.

This ranks below working software but above polish, for one reason:
this codebase's comments carry an unusual amount of hard-won reasoning
— why the flat is normalised per Bayer position, why the stacker needs
three agreeing frames before re-anchoring, why `printf` is useless
here, why a window's title bar can land somewhere unclickable. That
reasoning is worth more than the code it sits next to, and it is
currently scattered across source comments, nine Markdown files and
commit messages.

Suggested shape:

| Section | Source |
|---|---|
| Architecture and data flow | `RadukCapture-Architecture.md` |
| Decisions, and what was tried and rejected | commit messages, source comments |
| Operating the rig | `RadukCapture-Help.md` |
| Calibration and what still needs sky | `CLEAR-NIGHT-CHECKLIST.md` |
| Phase history | the roadmap tables |

Archive rather than delete. The Markdown files are the record of how
the thing was built.

---

## New debt found today

### D1 · Display device names are not stable
**Severity: high for the layout system. Found 2026-10-12.**

Between two captures in the same session, the monitors re-enumerated:
what was `DISPLAY5, 2, 3, 4` became `DISPLAY1, 6, 7, 8`. The primary
changed name. No cable was touched.

This matters because `WorkspaceLayout` resolves monitors by device name
first, on the explicit assumption — written into the Phase 42 plan as
rule 1 — that device names survive what enumeration order does not. On
this two-GPU rig, **they do not**.

Nothing broke, because the fallback chain ends at the primary and the
caption-reachability check stops anything landing off-screen. But the
saved layout silently stopped matching, and every window would have
collapsed onto the primary on the next launch.

The fix is to match on something stable underneath the name. Options,
in order of preference:

1. `DISPLAY_DEVICE.DeviceID` — already captured, currently unused as a
   key. Needs checking that it is actually stable across this rig's
   re-enumerations, which is a bench test taking minutes.
2. The monitor's EDID / serial, via the Setup API — definitive, more code.
3. Position and resolution as a fallback, which is what a nearest-rect
   match already approximates.

**Until this is fixed, a re-enumeration means pressing `Ctrl+Shift+End`
again to re-save the layout.** Worth a line in the help file now.

### D2 · Stock Win32 chrome — the cheap half DONE 2026-10-17

The three things the deferred entry said were worth keeping are done,
and the measurements moved a long way. Percentage of each window that
is NOT red with night vision on, lower being better:

| Window | was | now |
|---|---|---|
| Help | 60% | **10.7%** |
| AI chat | 88% | **16.0%** |
| GoTo | 98% | **39.2%** |

Done: dark title bars via `DwmSetWindowAttribute` (attribute 20, and
19 for pre-20H1), list-view background and text through `LVM_SETBKCOLOR`
and friends, and `SetWindowTheme(h, L"", L"")` on edits and combos so
the app's own `WM_CTLCOLOR*` brushes are what actually gets used.

Applied through the same enumeration D3 uses, so a window added later
is covered without being registered anywhere, and re-applied on every
night-vision toggle — the list colours follow the mode even though the
title bars do not. Title bars are dark in both modes, which is right:
this app is dark whatever it is doing, and a white title bar was never
correct.

**What is left is buttons, list headers and tab strips**, and it is
left on purpose. All three take their face from `COLOR_BTNFACE` once
de-themed, so stripping the theme trades a themed grey for a classic
grey and changes how they look in DAY mode for no night-vision gain.
Colouring them properly means owner-draw or `NM_CUSTOMDRAW`, which is
the expensive half this entry deferred in the first place. GoTo's
remaining 39% is almost entirely its buttons.

Still the standing judgement: leave them unless they prove annoying at
the eyepiece. `tests/manual-night-vision-coverage.ps1` re-measures the
table above if that question comes up again.

### D3 · Repaint and rescue reach only the layout list — DONE 2026-10-17

`kPlacedWindows` is the roster the workspace places and saves BY NAME,
so it has to be maintained by hand: a saved layout needs a role for
each entry. Three other things had come to depend on it being
**complete**, which is a different and much weaker claim. A window
added without a line there fell silently out of all three.

Measured on the running app: **14 top-level windows of ours, 12 roles.**
`RadukObjectInfo` was the one outside, which is the window the original
note named.

Anything that just wants "all our windows" now asks the OS.
`EnumThreadWindows` returns every top-level window on the UI thread,
filtered to our own class prefix — every class this app registers
begins with `Raduk`, and one that did not would not be ours. Three
uses converted:

| | was | now |
|---|---|---|
| Night-vision repaint | the 12 roles | every window we own |
| `Ctrl+Shift+Home` gather | the 12 roles | every window we own |
| Stranded-window rescue on a display change | the 12 roles | every window we own |

The toolbar strip and the notification bar turned out to be CHILDREN
of the main window rather than top-level, so `RDW_ALLCHILDREN` had
them covered all along; the preview is a child too, for the same
reason.

What is left depending on the list is placement, capture and save,
which genuinely need names. That is a real requirement rather than an
accident, and a window with no role simply is not part of the saved
arrangement.

Verified: `tests/manual-window-coverage.ps1` lists our top-level
windows against the roles and names the gap. The repaint itself was
checked by sampling six window bodies across a toggle — all six moved
together, red-dominant to blue-dominant.

---

## Ranked summary

Updated 2026-10-17. Everything above the line is finished and pushed;
the ranking below it is what to pick up next.

### Done

| Item | Shipped as |
|---|---|
| AI assistant stopped answering (P0.1) | HTTP status checked before token reading; errors surfaced |
| Status panel bleed (P0.2) | Status moved to its own window, out of the GDI strip |
| Map selection sets the GoTo target (P1.2) | `b192682` |
| READY as a modal, everything else dimmed (P1.1) | `8cdf3ef` |
| Display identity (D1) | `812672a` — matched on device interface path |
| Wiki + doc cleanup (P4.2) | `43e915e` — 26 pages |
| Histogram accuracy, bench half (P2.0) | `898c546` + the test suite that gates merges |
| Overlay hide on `U` (P3.2) | `ffe917d` |
| Status telemetry: all drives (P3.1b) | `fb99470` |
| ADC alignment tool (phase 35) | `9360842` — gated on *ADC fitted* |
| Five-monitor workspace finished (phase 42) | `66d62a1` |
| Sensor analysis / smart histogram (phase 36) | `665bb78` |
| Write buffer sized to the run, not a flat 512 | `8a8214e` — 292 MB/s drive measured, dips to 141 |
| Recording moved off the render loop onto the capture thread | `37b8c50` — the loop capped it near 62 fps |
| Scope position telemetry: lamp, status row, SER sidecar | `84bdf32` |
| LIVE-line telemetry on the status window | `9e018fd` |
| `Ctrl+Shift+End` confirms the save, after verifying the file | `a59aa50` |
| Xbox controller feasibility checked on the machine | `8fed05a` — works; needs an adapter, no Bluetooth here |
| Mount controller as a gamepad (P3.5) | ❌ dropped — the panel is fine as it is |
| Help and sky map text scale with the window (P3.3 part, P3.4) | `ef736e1` |
| Observing site moved to the map's upper left, at full precision | `20385c1` |
| Share view for a second screen (phase 89) | `c48c6de` — Shift+V, workspace role `share` |
| The emulator gets a field of view (phase 91) | `4af73e6` — angular sizes, cap lifted to the full sensor |
| Share view focus trap, and the sky map's dead F11 | `6ba0167` — `WM_GETDLGCODE`, and `Ctrl+Shift+V` |
| A second sky map (phase 90) | Shift+M, `skymap2` role on the control screen |
| Histogram shows peak level, not the clipped fraction | `228bd4e` + `ab408b6` |
| Full-sensor frame rate — P4.4 diagnosis corrected | `bc88296` — the preview was never slow |
| Preview stops re-deriving pixels it already has | `bc88296` — 27.1 ms → 2.0 ms at the full sensor |
| Emulator defocus blur, 556 ms → 190 ms (P4.4a) | `ecd165c` — proved pixel-identical |
| Emulator delivers 120 fps / 147 MB/s (P4.3) | `99140bc` |
| AI chat text scales with the window (P3.3) | `179f8aa` |
| GoTo panel scales; object names fixed (P3.3) | `dbc4832` — the catalogue had shown first letters only |
| Repaint and rescue find every window (D3) | `a4b50a9` — 14 windows, 12 roles |

---

### Next, in order

Updated 2026-10-17, end of session. P3.3 and D3 are finished and have
moved above the line. **P4.1 is held at the owner's request until the
controller and its receiver are in hand.**

| # | Item | Where | Size | Why here |
|---|---|---|---|---|
| 1 | **Prove the write path at 147 MB/s** | Bench | XS | The one thing P4.3 left owed. Camera → capture thread → ring is proven at 120.3 fps with zero drops; **ring → write buffer → SER file is not**. The run-sized write buffer and the SER writer have never been run above 20 fps. Launch with `RADUK_EMU_FPS=120` and `RADUK_EMU_TRUE_FOCUS` set to the EAF's position, set 1280×960, press record, watch BUF and DROPS. Smallest item on this list and it closes the biggest unknown |
| 2 | **`SlewVariable()` on the mount** | Needs sky to verify | S–M | Promoted out of P4.1, because it is worth having on its own. NexStar's variable-rate command (`P 3 16\|17 6\|7 <hi> <lo>`, rate = arcsec/sec × 4) would make the arrow-key jog proportional instead of the nine fixed rates it has now. It is also the thing that must exist before an analogue stick is anything more than a nine-position switch — so doing it now means P4.1 is ready the day the receiver arrives, rather than starting from here |
| 3 | **Make the assistant worth asking** (P3.6) | Bench | M | Not urgent — it is now honest about what it does not know, which was the dangerous part. The AI context builder has grown a lot since that entry was written and is worth re-reading before planning |
| 4 | **Night-vision chrome** (D2) | Bench | S | Deferred deliberately; stock Win32 chrome stays bright. Confirmed again this session — sampling title bars across a night-vision toggle measures almost nothing, by design |
| 5 | **Gate the `[PERF]` log line** | Bench | XS | Added this session to settle P4.4, and it earns its keep — but it writes to `raduk_log.txt` every 600 passes whether anyone is diagnosing or not. An env var or a key would make it opt-in |
| — | **Xbox controller input** (P4.1) | **HELD** | M | Held 2026-10-17: no controller and no receiver yet, and this machine has no Bluetooth so an adapter is needed. Feasibility is already proven on the machine (`8fed05a`) and item 2 above removes its one software prerequisite |
| — | **Emulator blur, further** (P4.4a tail) | Bench | S | Only if full-sensor bench work becomes routine. 128 ms of honest work remains and would need SIMD or fewer passes. Focusing the emulated EAF skips it entirely today |
| — | **AI runs on CPU** (D4) | — | — | Not actionable in code. Driver 546.01 caps at CUDA 12.3; see the memory note about keeping Windows Update away from the GPU drivers |

### P4.3 · Emulator frame rate — DONE 2026-10-17

`RADUK_EMU_FPS=N` lifts the 20 fps cap. Measured in the app, not just
on the bench: **1280×960 at 120.3 fps, 147.5 MB/s, zero drops** —
which is the rate and the throughput clear-night item 13 exists to
de-risk, and which had never once been reached indoors.

Three things were wrong, not one:

1. `TARGET_MS = 50` was a hard 20 fps ceiling.
2. `sleep_for(TARGET_MS - renderMs)` sleeps for a DURATION measured
   after the work, so each frame's error fed the next, and at the
   default timer granularity it rounded 50 ms up to 62.4. That is the
   whole of the long-reported "16 fps". It is `sleep_until` a running
   deadline now.
3. Lifting the cap alone left every format pinned at exactly 64 fps —
   15.6 ms, the timer tick again. `Window::Run` raises the resolution
   for the app's own loop, which is why the app saw 19.6 where a bench
   harness saw 15.9; depending on that is depending on a side effect
   of the GUI. The generator raises it for the life of a capture now.

A frame is also never delivered faster than it is exposed. An emulator
that beat its own exposure would validate the pipeline against a
regime that cannot occur live, which is the trap the Phase 52 note
describes for `GetVideoFrame`.

**The one thing to know when using it:** the defocus blur dominates at
high rates. Measured at 1280×960 asking 120 —

| | delivered | render |
|---|---|---|
| no focuser | 120.2 fps | 4.0 ms |
| focuser **at** true focus | 120.0 fps | 4.0 ms |
| focuser 1500 steps out | **41.8 fps** | 22.0 ms |

So set `RADUK_EMU_TRUE_FOCUS` to wherever the emulated EAF actually
is, or `RADUK_EMU_BLUR_PX_STEP=0`, for a high-rate bench run. Leave it
defocused for autofocus work, where the blur is the point.

**Still unproven: the WRITE path at 120 fps.** The run above exercises
camera to capture thread to ring, which is most of the pipeline and
all of what was previously impossible. Driving an actual recording at
that rate needs the record button pressed — the WM_COMMAND id did not
reach the panel from outside — so the run-sized write buffer and the
SER writer have still not been seen at 147 MB/s. That is now a
five-minute manual check rather than a blocked one: launch with
`RADUK_EMU_FPS=120`, set 1280×960, press record, watch BUF and DROPS.

`tests/manual-emulator-delivery-rate.cpp` reproduces the table.

### P4.3 · the original entry, **premise corrected 2026-10-17**

> **The diagnosis below was wrong, and the correction is worth more
> than the plan was.** Driven directly, with no app around it, the
> renderer costs 63 ms at *every* format:
>
> | Format | Mean gap | fps | Throughput |
> |---|---|---|---|
> | 640×480 | 63.50 ms | 15.7 | 5 MB/s |
> | 1280×960 | 63.44 ms | 15.8 | 19 MB/s |
> | 1920×1080 | 63.43 ms | 15.8 | 33 MB/s |
> | 3840×2160 | 62.97 ms | 15.9 | **132 MB/s** |
>
> Flat across a 27× range of pixel counts. Whatever that 63 ms is, it
> is not per-pixel rendering. It is `sleep_for(TARGET_MS - renderMs)`
> rounding up to the 15.6 ms Windows timer granularity — 63 ms is
> four ticks — which is the **third** time that granularity has
> turned up as a ceiling in this codebase, after `Window::Run()` and
> the recording path.
>
> So `timeBeginPeriod(1)` or a precise wait is most of the fix, and
> the pre-rendered replay buffer this entry was written to justify may
> not be needed at all. Measure again after the sleep is fixed before
> building it.
>
> It also means the emulator's old 1280×960 cap had no performance
> justification even before phase 91 lifted it: 3840×2160 already
> delivered 132 MB/s, which is most of the way to the 147 MB/s clear-
> night item 13 wants to exercise.

The original entry follows, for the record.

The emulator delivers 16.1 fps measured directly, with no app around
it — not because of a frame-rate cap but because it *renders* a
limb-darkened banded planet with moons and seeing wander every frame,
at 62 ms a go. It overruns its own 50 ms `TARGET_MS` cap.

That is the only reason the capture pipeline has never been exercised
above 20 fps, and it is why clear-night item 13 exists at all.

Optimising the renderer is the wrong fix. Render a few dozen frames
once, keep them in memory, and serve them round-robin at a configured
rate. Then 120 fps and 147 MB/s are reachable on the bench, and the
decoupled recording path, the run-sized write buffer and the SER
writer can all be tested at the rate they were built for — indoors,
repeatably, and in a form `tests/` could eventually gate.

The frames need not be interesting. Nothing being measured at that
rate cares what the planet looks like; it cares about bytes per second
and whether any go missing.

### P4.4 · Full-sensor frame rate — **diagnosis was wrong, 2026-10-17**

> This entry claimed the display pipeline costs about 65 ns per sensor
> pixel per frame, that the full-sensor preview therefore runs at 1.7
> fps, and that this is true of the real rig. **All three are wrong**,
> and the error was the same one that produced the first P4.3
> diagnosis: a number was read off the screen and reasoned about
> instead of being measured at its source.
>
> **The 1.7 fps was the CAPTURE rate, not the preview's.** The preview
> and the camera rate are two different numbers and the LIVE line
> shows the camera's. Instrumented, the render loop was making **31
> passes a second** at 3840×2160 and 60 at 1920×1080 — it was never
> slow.
>
> | | pass | render | panels | present | capture |
> |---|---|---|---|---|---|
> | 1920×1080 | 16.7 ms | 7.0 | 0.8 | 0.5 | 10.4 fps |
> | 3840×2160 | 32.0 ms | 27.1 | 1.1 | 0.5 | 1.9 fps |
>
> **The slow capture is the emulator's defocus blur, and nothing
> else.** It had been measured at a flat 63 ms per frame at every
> format — but that benchmark never set a focus provider, so the whole
> defocus block was skipped, while in the app the focuser is connected
> and the emulated EAF sits 1500 steps off true focus. Measured both
> ways:
>
> | Format | no focuser | defocused |
> |---|---|---|
> | 640×480 | 62.3 ms | 62.1 ms |
> | 1280×960 | 61.9 ms | 63.5 ms |
> | 1920×1080 | 62.0 ms | **95.9 ms** |
> | 3840×2160 | 62.1 ms | **556.4 ms** |
>
> 556 ms is 1.8 fps, which is exactly what the app reports. 95.9 ms is
> 10.4 fps, likewise. The app accounts for none of it.
>
> **So there is nothing here for the real rig.** A real ASI678MC has no
> software blur; it delivers what USB and the sensor allow. Nothing in
> this entry describes a limitation in the field, and the help's
> warning that "full sensor is slow to preview" was wrong and has been
> removed.

### P4.4a · The emulator's defocus blur — DONE 2026-10-17

The blur cost 494 ms of the 556 ms a defocused full-sensor frame took.
Two causes, and the one in the title turned out to be the smaller.

**The stride.** The vertical pass walked each column with a step of
2*W bytes — 7680 at the full sensor — so every element was its own
cache line and the walk evicted what the next column needed. It is
done in tiles of 256 adjacent columns now, with the running sums for
the whole tile in a small hot array, so each row touch is contiguous
and every line fetched gets used. **556 → 348 ms.**

**The division, which was bigger.** The box filter divides once per
output element, and there are six full-frame separable passes per
rendered frame: fifty million integer divisions at 3840x2160. A
running sum over uint8 is bounded by 255*win, so a table of every
value it can take is a few hundred bytes and lives in L1. It is built
*by dividing*, so it is exact rather than approximately exact.
**348 → 190 ms.**

| | before | after |
|---|---|---|
| 1920×1080, defocused | 95.9 ms | 63.5 ms (at the floor) |
| 3840×2160, defocused | 556.4 ms | **190.3 ms** |
| app capture rate, full sensor | 1.8 fps | **5.8 fps** |

**Proved equivalent, not assumed.** `tests/manual-emulator-blur-
equivalence.cpp` grabs a defocused frame with the surface frozen and
the noise off and prints a checksum. Before and after match exactly at
both formats — `937f6ad165b01a5c` and `935a387ea2ff8f76` — so the
rewrite changed the speed and nothing else. That mattered: a blur that
is merely *close* would quietly move every sharpness measurement the
autofocus work depends on.

Still worth knowing: focusing the emulated EAF to 168000 skips the
blur entirely and gives ~16 fps at the full sensor, because
`TARGET_MS` is 50 and the app's `timeBeginPeriod(1)` lets the sleep
hit it. Further gains would need SIMD or fewer passes, and the
remaining 128 ms is honest work rather than waste.

### P4.5 · Preview no longer re-derives pixels it already has — DONE
**Shipped 2026-10-17.**

Found while measuring the above and kept, because it is a real saving
on the real rig and not just the bench. The render loop ran at 31
passes a second against a camera delivering 1.9, so **94% of passes
were re-debayering, re-stretching and re-uploading a frame already on
screen** — roughly 100 MB of memory traffic a pass at the full sensor,
near 3 GB a second, for a picture identical to the one already there.

The pixel work is now gated on the frame number and on a key covering
everything that changes what the pixels look like; the bitmap is drawn
again regardless, so every overlay stays live and interactive.

| | before | after |
|---|---|---|
| render, 3840×2160 | 27.1 ms | **2.0 ms** |
| loop | 31 passes/s | **57 passes/s** |

It did **not** raise the capture rate, which is how the bandwidth-
starvation theory got ruled out and the defocus blur found. Kept on its
own merits.

### Blocked on weather

`CLEAR-NIGHT-CHECKLIST.md` items 1–13. Nothing here can be advanced
indoors, and the list exists to keep that distinction honest.

Three worth doing first on the first clear night, because other numbers
depend on them:

- **Item 13 — the capture pipeline at real frame rates.** The largest
  untested surface in the app and the one with the most to gain. The
  whole write path has only ever run at 15–20 fps, because that is all
  the emulator renders; the camera is specified far above it. Recording
  was moved off the render loop and the write buffer resized to suit on
  2026-10-14, and neither change has seen a real frame. One measurement
  settles whether it worked: frames in the SER against delivered FPS ×
  duration.
- **Item 1 — `SHARP_CEIL`**, which biases the capture gate and
  autofocus.
- **Item 3 — plate scale**, never once checked against the sky, and
  every arcsecond figure in the app derives from it, the ADC tool's
  included.

### Unstarted features, not yet scheduled

Nothing below is half-built. Ranked by value to this rig rather than by
the original tier numbers.

| Phase | Feature | Effort | Note |
|---|---|---|---|
| 47 | Pixel position / click to recentre | M | Pairs naturally with the controller work |
| 46 | Connect / disconnect all hardware | M | Operational quality; currently all-or-nothing at startup |
| 48 | Go to previous image (framing recall) | M | |
| 30 | Push-to assistant | M | |
| 31/32 | FWHM / Bahtinov focus | H | Star focus; autofocus is tuned for a planetary disc |
| 18 | AVI / FITS output | M | |
| 19 | Multi-session stats dashboard | M | |
| 34 | Solar / lunar framing | H | |
| 43 | Deep sky image annotation | H | |
| 33 | Stellar photometry | VH | |
| 20 | Distribution / ASCOM support | VH | |
| 16 | Polar alignment assistant | H | **Worth confirming this applies.** The 8SE is alt-az unless it is on a wedge, and polar alignment is a wedge-only procedure |

### Partial, and known broken

| Item | State |
|---|---|
| 6.6 GoTo map learning | ◑ Auto solve+sync on lock ships; the accumulating map **reports but does not correct** |
| 77 AutoStakkert in-app launch | Broken, and proven not to be ours — the identical command fails from a bare prompt |

---

## Live-sky debt, consolidated

Existing items 1–10 in `CLEAR-NIGHT-CHECKLIST.md`, plus two added by
this round:

- **11 · Histogram against a real sensor.** Do the bench half (P2.0)
  first, so the night is spent only on what the bench genuinely cannot
  answer.
- **12 · Sharpness readings on a real target.** Folds into item 1;
  listed so the owner's report is traceable to an answer.

- **13 · The capture pipeline at real frame rates.** Added
  2026-10-14 after measuring that the emulator caps at 16 fps and the
  render loop capped recording at ~62. Both the decoupled recording
  path and the run-sized write buffer are unexercised above 16 fps.
