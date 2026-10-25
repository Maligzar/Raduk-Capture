# Roadmap — next phases

Written 2026-10-12, after a long emulator session. Sources: the owner's
findings from that session, the existing roadmap table in
`RadukCapture-Architecture.md`, and `docs/CLEAR-NIGHT-CHECKLIST.md`.

**Reviewed whenever a phase closes, not on a schedule — a date here
only ever told you how stale it was.** Most of what follows is
finished: Tiers 0, 1 and 3 entirely, and the debt items D1–D3. The
tier sections are kept as the record of what was planned and why, with
their outcomes marked.

**This file ranks. It does not assert status.** What state a phase is
actually in — and the difference between built, wired, gated and
measured — is [`PHASE-STATUS.md`](PHASE-STATUS.md), which is the only
place that claim is made.

**Reviewed again 2026-10-19,** after a competitive comparison against
the planetary-capture field (`reports/Astronomy capture software
comparison.md`, and the wiki page **Comparison With Other Software**).
That review added **Tier 5**, seven items about competitive position
rather than about defects, re-scoped four entries in the unstarted
table, and promoted clear-night item 3 to the top of the live-sky
list. Nothing in Tiers 0 to 4 changed: the comparison found no new
bug, it found missing surface area.

**The live list is [Next, in order](#next-in-order).** Two live
sections sit above it and are easy to miss: **P4.1, the Xbox
controller**, backlogged on hardware rather than finished, and the
whole of **Tier 5**, which is new work and not a record of old work.
Everything else above the live list is the dated record.

**Reviewed again 2026-10-25,** after P5.5 and P5.6 merged, checking
this file's claims against `git` and the tree rather than against
itself. It found two figures that were never true (no tags, 58
commits), a phase called done here and `BUILT` in
[`PHASE-STATUS.md`](PHASE-STATUS.md), status asserted here for P5.5 and
P5.6 that the status file had no row for, a fix that never reached the
Done table, and a hardcoded-path list about half complete. All
corrected in place; see [Found by the 2026-10-25
review](#found-2026-10-25). The ranking did not change.

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

### P0.1 · AI assistant stopped answering — ✅ DONE
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

### P0.2 · Status panel bleeds into everything else — ✅ DONE
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

### P1.1 · READY as a modal, everything else greyed — ✅ DONE (`8cdf3ef`)
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

### P1.2 · Map selection sets the GoTo target — ✅ DONE (`b192682`)
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

### P2.0 · Histogram accuracy — deep dive — ✅ bench half DONE (`898c546`)
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

### P3.1 · Status as its own window, and out of the side panel — ✅ DONE
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

### P3.3 · Resizable AI / GoTo / Help, with larger fonts — ✅ DONE 2026-10-17
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

### P3.4 · Map window larger — ✅ DONE (`ef736e1`)
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

### P4.2 · A deep wiki, and a documentation cleanup — ✅ DONE (`43e915e`)
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

## Tier 5 — competitive position

Added 2026-10-19, out of a comparison against the planetary-capture
field. The report is `reports/Astronomy capture software comparison.md`
and it lives in the wiki as **Comparison With Other Software**.

Ranked on weighted criteria against the three products that actually
compete for this workload, RadukCapture came fourth: SharpCap 8.25,
FireCapture 7.50, ZWO ASICap 6.05, RadukCapture 5.15. N.I.N.A. and
Sequence Generator Pro were excluded rather than scored, because they
do not do this job at all: N.I.N.A.'s tree writes only TIFF, FITS,
XISF and RAW and contains no SER or AVI writer anywhere in it, and
SGPro's own vendor FAQ states the product is not compatible with
planetary or video cameras. Scoring them on a planetary scale would
have produced a number that looks like information and is not.

The shape of the score matters more than the position. Output fidelity
scored 8, level with SharpCap and FireCapture; planetary decision
support scored 7. Hardware breadth and distribution both scored 1.
**The gap is surface area, not sophistication.** The capture path, the
measured guide solve, the ADC advisor and the emulator are competitive
or better than what the field ships. What loses points is how few
cameras the application drives, how few formats it writes, that it has
no star-focus aid, that nobody can install it, and that nobody else
can contribute to it. Everything in this tier follows from that, and
none of it is a rewrite of something that works.

> ### Caveat: most competitor facts here are second-hand
>
> The comparison was researched on 18 October 2026 under a network
> policy that denied direct access to **firecapture.de,
> sharpcap.co.uk, zwoastro.com, sequencegeneratorpro.com,
> nighttime-imaging.eu, cloudynights.com and archive.org**. Almost
> every vendor-sourced fact therefore arrived through a search index
> that read those pages server-side, not from a page anyone opened and
> read end to end. Quoted strings are second-hand transcriptions.
>
> Two exceptions carry the strongest evidence in the set: the N.I.N.A.
> figures were measured from a full clone of the repository, and the
> ZWO SDK figures came from directly fetched GitHub sources.
> RadukCapture's own figures were measured against this tree.
>
> **Any P5 item whose justification rests on what a competitor does or
> does not do should have that justification re-checked when the
> vendor sites are reachable.** P5.1 is the most exposed, because its
> value as a differentiator depends on nobody else doing it.
>
> Five things the research could not settle, recorded as open
> questions rather than as absences, because inferring them would be
> inventing them:
>
> 1. ASIStudio's UI toolkit.
> 2. SGPro's UI framework.
> 3. Whether SharpCap 4.x has moved off .NET Framework 4.8.
> 4. FireCapture's UI toolkit.
> 5. ASICap's timestamping and buffering behavior.
>
> The last one bears directly on P5.1.

### P5.1 · Exposure-midpoint SER timestamps: effort S

**Bench. Small.**

`src/core/CaptureThread.cpp:147` stamps the frame with
`Timestamp::Now()` immediately after `GetVideoFrameEx` returns, and
`src/core/SERWriter.cpp:87` maps that QPC reading onto a wall clock
correlated once at `Open()`. That is frame-arrival time, not the
midpoint of the integration.

The comparison checked whether that is a deficit and found it is not.
SharpCap's author has acknowledged that SharpCap's SER timestamp is
also the arrival time from the camera SDK, that its FITS path does
subtract the exposure, and that the SER code was never updated to the
better estimate. **So this is parity with the market leader, not a
deficit**, and Phase 59 already fixed the version of it that actually
mattered: before that fix the trailer recorded disk-write time and
could lag the photons by up to ten seconds behind a full backlog,
which makes a file useless for derotation while it still looks
entirely valid.

What is left is a cheap place to lead rather than follow. Subtract
half the exposure and report the midpoint of integration. At a 10 ms
exposure that removes a 5 ms systematic bias: irrelevant to WinJUPOS
derotation over minutes, material to occultation timing, which is the
one planetary workload where absolute time is the measurement rather
than a label on it.

**One correction to the report's framing, found by reading the code.**
The report says the exposure value is already to hand in the writer.
It is not: `SERWriter` has no exposure field at all (see
`src/core/SERWriter.h`), while `CaptureThread` holds it as
`m_exposureMs` and already reads it twenty-odd lines above the stamp
to size the SDK timeout (`src/core/CaptureThread.cpp:120`). The
subtraction therefore belongs at the stamp site, or the exposure has
to be threaded through `WriteFrame`. Still small, but it is not a
one-line change in `SERWriter.cpp`.

Done looks like: the trailer holds midpoints; the convention is
recorded somewhere a future reader can find it, in the header comment
or the quality sidecar, so nobody has to guess which of the two
conventions an old file used; and a test pins the arithmetic.

**The claim to being first should be re-checked before it is made in
public.** No product in the surveyed field is known to write exposure
midpoints, but FireCapture's behavior is unconfirmed and ASICap's is
entirely undocumented. That is second-hand evidence of an absence,
which is the weakest kind there is.

### P5.2 · Camera and focuser behind real interfaces: effort M

**Bench. Medium. No user-visible feature at the end of it, which is
exactly why it has to be a phase rather than an assumption.**

The mount is already polymorphic: `src/core/IMount.h:25` defines
`IMount`, `main.cpp` holds a `std::unique_ptr<IMount>`, and
`ASCOMMount`, `NexStarMount` and `MountEmulator` all implement it.
**The camera and the focuser are not polymorphic at all.** There is no
`ICamera` and no `IFocuser` anywhere in the tree.
`src/core/DeviceFactory.h` picks between `CameraEmulator` /
`EAFEmulator` and `ASICamera` / `EAFFocuser` with an
`#ifdef RADUK_EMULATOR`, by header name, at compile time. So the
application supports exactly one camera family and one focuser, and
the emulator is not a second implementation of a contract, it is a
different build.

Hardware breadth is the single largest scoring deficit in the
comparison: 15% of the weight at a score of 1, against roughly 14
native camera families in SharpCap and around 8 native vendors plus
ASCOM and INDI in FireCapture. None of that can be approached until
there is a seam to put a second driver behind, and this phase is the
seam and nothing else.

Done looks like: `ICamera` and `IFocuser` exist; `ASICamera` and
`CameraEmulator` both implement `ICamera`; the emulator is chosen at
runtime rather than by a compile flag; and the existing emulator build
behaves identically. The last clause is the risk worth naming, because
this touches the hot path and a refactor that quietly adds a virtual
call per frame inside the capture loop should be measured rather than
assumed harmless.

### P5.3 · ASCOM reconsidered, and the premise corrected: effort M

**Bench. Medium.**

`src/core/ASCOMMount.h` is a real 792-line `ITelescopeV3`
implementation over COM `IDispatch` late binding. **It is not
disabled, and the roadmap should not repeat that it is.**
`src/main.cpp:738` calls `ASCOMMount::FindCPWIDriver()` first on the
hardware path and only falls back to `NexStarMount` on the serial port
when no driver resolves, or when the one that does turns out to be a
simulator. The wiki marks the class "Superseded"
(`wiki/Device-Layer.md`, `wiki/Code-Map.md`) and the comparison
inherited that, together with the stronger claim that every known
driver is blacklisted. Neither survives reading `FindCPWIDriver()`:
CPWI is not blacklisted, it is the first ProgID tried.

What *is* foreclosed is the part that would have bought breadth.
`FindCPWIDriver()` (`src/core/ASCOMMount.h:336` onward) prefers three
Celestron ProgIDs, and its sweep of everything else in the registry
skips `ASIMount`, `ASIFocuser`, `DeviceHub`, `JustAHub`, `OmniSim`,
`Simulator` and `ScopeSim`. DeviceHub is the ASCOM hub, which is to
say the one driver whose entire purpose is to put arbitrary other
drivers behind a single connection; it was dropped from the preferred
list as "always misconfigured here" and blacklisted in the sweep as
well. Simulators are rejected twice, once by name and once by
`IsSimulator()` at connect, which also means the generic path cannot
be exercised on the bench at all.

So ASCOM here is live as a Celestron path and closed as a generality
path, and that was decided as a local fix for a COM3 conflict rather
than as a policy about hardware coverage. This phase is the
re-examination, not the re-enabling: the code is built, and what has
to change is a blacklist, a preference order and a simulator rule, not
a subsystem.

Record what would have to be true to widen it, so the next person is
not re-litigating a fait accompli:

- DeviceHub connects and holds a mount on this machine when it is
  configured correctly, or it is shown that it cannot.
- A named non-Celestron ASCOM driver connects, reports position and
  accepts a pulse.
- The simulator exclusion is narrowed to the live hardware path, so
  `ScopeSim` or `OmniSim` can be used to test the generic route
  indoors. As it stands the blacklist makes the ASCOM path untestable
  without a telescope, which is the opposite of what it should be.

### P5.4 · Distribution: installer, tagged releases, and no hardcoded paths: effort M

**Bench. Medium, and none of it needs a new subsystem.**

Distribution carries 15% of the comparison's weight and scored 1. The
pieces are small and individually dull:

- **Hardcoded output paths.** `D:\RadukCapture\...` is compiled into
  `src/main.cpp:104` (the log file itself), `:1034` and `:3757`,
  `src/core/CaptureManager.h:36`, `src/core/SessionLog.h:40-47`,
  `src/net/APEXTelemetry.h:89`, `src/gui/AIChatPanel.h:52-54`,
  `src/gui/ControlPanel.cpp:1347`, `:1352` and `:2280`,
  `src/sky/PointingModel.h:72` and `src/profiles/ProfileManager.h:22`;
  `D:\astap\astap.exe` is in `src/sky/ASTAPSolver.h:54`, and two `D:`
  AutoStakkert locations in `src/core/AutoStakkertRunner.h:175-178`.
  Counted 2026-10-25 by grepping `src/` for `D:\\`; line numbers drift,
  so re-grep rather than trusting this list. The application does not
  run correctly on a machine with no D: drive, which is most machines.
- **Tags, but no releases.** An earlier version of this entry said `git
  tag` was empty against 58 commits. Neither was true: there are 13
  tags, `v1.0.6` to `v2.0`, against 220 commits on `master` (counted
  2026-10-25). What is missing is a release -- no artifact of any kind
  is attached to any of them. The version numbers used to disagree as
  well (`Version.h` 1.2.3, help-file labels up to v1.2.7, newest tag
  `v2.0`); the owner settled that on 2026-10-25 as **2.0**, and
  `src/Version.h` read `2.0.0` (2.1.0 since phase 48). The `v2.0` tag itself still points
  at `6cb4032` from 2026-10-12, 111 commits back, so a release cut
  from today's tree needs a new tag rather than that one.
- **No installer.** There is `build_all.bat`, which is not something a
  person who is not the author can run.

All of this currently sits inside unstarted phase 20, "Distribution /
ASCOM support", marked VH. That marking hides a cheap and separately
valuable piece of work inside a very hard one, and it is a good part
of why none of it has been started. **Split it out.** Phase 20 is
re-scoped to the distribution half, which is this phase; the ASCOM
half goes to P5.3; and neither remaining piece is VH.

Done looks like: a path root resolved at runtime from the executable
location or from `rig.ini`, with no `D:` literal left in `src/`; a
tag on the commit the release is built from, matching `src/Version.h`;
and one artifact that somebody else could
download and run.

### P5.5 · Lower the CMake floor: effort S [DONE 2026-10-25]

**Status: MEASURED (bench) 2026-10-25. The floor is 3.21, as
`cmake_minimum_required(VERSION 3.21...4.2)`.** Every version below
was built in both configurations with MSVC 14.50, and its compile
flags were read, not just its exit code:

| CMake | Result |
|---|---|
| 3.12.4 | not run: no `-S`/`-B` (3.13+), and it fails the C++20 test below anyway |
| 3.16.9 | builds, but **wrong**: maps C++20 to `/std:c++latest`, a C++23 preview on this compiler |
| 3.21.0 | builds, `-std:c++20`. **The floor** |
| 3.22.6 | builds, `-std:c++20` |
| 4.1.2 | builds with Ninja and the VS 18 2026 generator. This is **VS 2026's own bundled CMake, which the 4.2 floor refused** |
| 4.3.0 | builds; generated `build.ninja` byte-identical to the 4.2-floor one |

What the entry did not foresee: "configures and builds" is not enough
of a test. 3.16 passes it and compiles a different language mode, which
is the kind of quiet difference this project exists to refuse. The
`...4.2` policy ceiling keeps current CMake's behaviour exactly as it
was. Kitware builds below 4.2 lack the VS 2026 generator and need
`-G Ninja`. Ubuntu 22.04 ships 3.22 and 24.04 ships 3.28, though
RadukCapture is Windows-only, so that matters only for tooling.

**Bench. Small.**

`CMakeLists.txt` line 1 is `cmake_minimum_required(VERSION 4.2)`. That
is an unusually recent floor, it will refuse to configure on most
current distributions, and nothing in the file is known to need it. It
is an avoidable barrier in front of anyone who tries to build this,
a future owner on a new machine included, and the comparison counted
it under distribution for that reason.

Done looks like: the oldest CMake that actually configures and builds
this project is established by trying it, and that is the number on
line 1. Guessing a lower number without testing would be worse than
leaving it alone, because the failure would move from the first line
of configuration to somewhere deep in the build.

### P5.6 · The WebSocket parser is the one place memory safety is not theoretical: effort S to M [GATED 2026-10-25]

**Status: GATED (bench) 2026-10-25 — `tests/ws_protocol.cpp`,
`tests/ws_fuzz.cpp` (under AddressSanitizer), `tests/ws_server_loopback.cpp`.
owes: one connection from the real dashboard to the real app, and from
`apex_bridge.py` on the streaming PC if that is still in use — it is not in
this repository and will need the token too.**

What was done, and what changed on the way:

- **The threat was wider than this entry said.** Loopback did not keep
  out the observer's own browser: browsers do not apply the same-origin
  policy to WebSockets, so any page open on the capture PC could connect
  to `ws://localhost:8765` and slew the mount. The token is therefore
  required on loopback too, not only under `RADUK_WS_LAN`.
- **Token:** 32 bytes from `BCryptGenRandom`, 64 hex characters in
  `rig.ini` as `WsToken`, sent as `?token=` (browsers cannot set headers
  on a WebSocket), compared in constant time, never logged. Empty or
  under 32 hex characters admits nobody.
- **Parser** moved out of the socket code into `src/net/WsProtocol.h`.
  Defects fixed: a fragmented command was dispatched as its first
  fragment and the rest dropped; a frame arriving with the handshake was
  discarded; unmasked frames, RSV bits and reserved opcodes were
  accepted; pings got an empty pong; header names were case-sensitive;
  `Access-Control-Allow-Origin: *` was sent. `APEXCommand::Parse` called
  `isdigit` on a signed `char` from the network (undefined for bytes ≥ 0x80).
- **Limits added:** 16 clients (Windows `select()` watches 64 sockets at
  most), a 10 s handshake deadline, an 8 KB handshake, reassembled
  messages held to 1 MB.
- **The fuzzer was checked against planted bugs**, since one that has
  never failed proves nothing: removing the wait-for-whole-frame check was
  caught by AddressSanitizer, accepting surrogates by the UTF-8 reference,
  and dropping continuation frames by the differential. A fourth mutant
  (an off-by-one in the UTF-8 truncation test) survived because it is
  equivalent: the byte it reads is the string's terminating NUL, which
  fails the continuation test either way.

**Bench. Small to medium, and it carries a safety argument, which is
why it sits where it does in the queue.**

`src/net/WebSocketServer.h:83` said it plainly, until P5.6 replaced it
with the token check: the server binds 127.0.0.1 by default and **the
command set has NO authentication**.
`RADUK_WS_LAN=1` exposes it on the LAN (`src/main.cpp:935-944`).
Behind that socket is a hand-rolled RFC 6455 frame parser in C++, with
hand-rolled SHA-1 and base64, reading attacker-controlled length
fields, masking, fragmentation and continuation frames
(`src/net/WebSocketServer.cpp`, 356 lines). The commands it accepts
move the mount.

The comparison's language section is otherwise unkind to the
native-code case: nothing in the gathered field record attributes a
dropped astronomy frame to garbage collection, and the interop
objection fails on arithmetic. **The one place the memory-safety
argument lands is here.** Current CISA and NSA joint guidance lists
C# and Java among the memory-safe languages and does not list C++, and
the convergent industry figure for memory safety as a share of
high-severity defects is around 70%. A hand-written network frame
parser is the exact shape of that bug class, and it is this project's
only network-facing attack surface.

Two pieces of work, and neither substitutes for the other:

1. **Fuzz the frame parser** against malformed, truncated, oversized
   and pathologically fragmented frames. The existing clamps
   (`MAX_WS_PAYLOAD` at 1 MB and `MAX_RECV_BUF` at 2 MB,
   `src/net/WebSocketServer.h:80-81`) are the right instinct and are
   not evidence of correctness.
2. **Authenticate the command path** before `RADUK_WS_LAN` is ever
   used in anger. A shared token is enough. Loopback-by-default is a
   good default and is not a substitute for one.

`docs/SECURITY_REVIEW_2026-07-11.md` already raised this: its
attack-surface table carries the row "WebSocket server :8765
(mount/focuser commands, **no auth**)", and Phase 57 hardened the
parsing (the length clamp and the receive cap above) without closing
the authentication gap. **This phase is the follow-through, not a new
discovery.** The honest counterweight from the report is worth keeping
in view while doing it: a managed-language server with no
authentication would not be safer in practice than a fuzzed C++ one
with a token.

### P5.7 · License: PolyForm Noncommercial is the binding constraint on continuity

**Effort: an owner decision, not code. Nothing here can be done by
writing software.**

`LICENSE` is PolyForm Noncommercial 1.0.0. It is source-available and
it is not an OSI open-source license, because it discriminates by
field of use. Three consequences follow mechanically rather than as
opinion: it is GPL-incompatible, it will not be packaged by any Linux
distribution, and it is unavailable to any contributor whose employer
runs a legal review.

The comparison put a number against the thing this is usually weighed
against. **N.I.N.A. is also effectively single-maintainer**, with
6,467 of its 8,152 commits by one author, about 79%. It is in no
danger of disappearing anyway, because MPL-2.0 plus 78 distinct commit
authors means it can continue without him. This project has 220 commits
from essentially one person and a license that keeps everyone else
out. (An earlier version said 58; it was never right.) **For the
bus-factor risk this project already records about itself, the license is doing more damage than the contributor count
is, and MPL-2.0 is the working proof inside this very category.**

That is the finding. The decision is the owner's, and this entry is
not pretending the decision has been taken:

- **Keep PolyForm Noncommercial.** What it buys is precisely aimed: it
  stops a camera manufacturer shipping this capture engine inside a
  product, while permitting every actual amateur to use, modify and
  redistribute it. If protection from commercial appropriation is the
  goal, it is the correct instrument and the continuity cost is simply
  the price of it.
- **Relicense to MPL-2.0.** Weak copyleft, so the file-level
  protection survives; OSI-approved, so packaging, GPL compatibility
  and corporate contributors all become possible. What is given up is
  precisely the protection above: a manufacturer could ship it, subject
  to publishing changes to the covered files.
- **Dual license.** Gets both, and costs a contributor license
  agreement and somebody to administer it. For a one-person project
  that administration is not free and should be priced before it is
  chosen.

Record the decision whichever way it goes. An unmade decision here
reads as the restrictive answer, because that is what it currently is.

---

## New debt found today

### D1 · Display device names are not stable — ✅ DONE (`812672a`)
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

### D4 · The AI runs on CPU — not actionable in code

Referenced from several places above and, until now, defined nowhere.

The local model runs on the CPU because the installed NVIDIA driver
(546.01) caps CUDA at 12.3, below what the runtime wants, and the
RTX 2060's 6 GB would in any case only fit an 8B model at 4-bit. Both
halves are environment, not code: nothing in this repository can move
them, and a driver update is entangled with the five-monitor setup that
the two cards currently drive.

Recorded as debt because it bounds P3.6 option A — a larger
assistant model — and because an undefined reference is how a reader
concludes the document is unreliable.

---

## Ranked summary

Updated 2026-10-25. Everything above the line is finished and pushed;
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
| Write path proven at 293 MB/s, zero drops | `9f886d6` — discharges clear-night item 13 |
| Emulator delivers 120 fps, cap lifted (P4.3) | `99140bc` |
| `[PERF]` log behind `RADUK_PERF=1` | `9f886d6` |
| Night-vision chrome, cheap half (D2) | `9f886d6` — Help 60%→10.7%, AI 88%→16%, GoTo 98%→39% |
| Assistant told the truth about the rig (P3.6 C) | `545cbf9` — the prompt had the wrong pixel pitch since June |
| Object panel (phase 92) | `be572c2` + `c3b567b` — Shift+I, and four defects the unread database was hiding |
| Knowledge base reachable at last (92a) | `b97902d` — sqlite3.dll shipped; `sqlite3_open` needed `sqlite3_initialize` |
| SIMBAD distances: 9 → 107 objects | `190b001` — invalid query, wrong row names, and a counter that lied |
| Histogram shows peak level, not the clipped fraction | `228bd4e` + `ab408b6` |
| Full-sensor frame rate — P4.4 diagnosis corrected | `bc88296` — the preview was never slow |
| Preview stops re-deriving pixels it already has | `bc88296` — 27.1 ms → 2.0 ms at the full sensor |
| Emulator defocus blur, 556 ms → 190 ms (P4.4a) | `ecd165c` — proved pixel-identical |
| AI chat text scales with the window (P3.3) | `179f8aa` |
| GoTo panel scales; object names fixed (P3.3) | `dbc4832` — the catalogue had shown first letters only |
| Repaint and rescue find every window (D3) | `a4b50a9` — 14 windows, 12 roles |
| Assistant reads the project's notes and its own past (P3.6 B) | `85b9d51` — 16 documents, 6 session logs, 8.3 KB prompt |
| Variable-rate slewing (P3.6 list item 1) | `eef6c25` — gated by `tests/slew_variable.cpp` |
| An empty NOTES box can no longer erase a profile's note | `202822f` — the guard saturn.ini needed in `f2f2edc` |
| Apparent size for the planets (list item 3) | `f4f4bda` — computed from the ephemeris, gated by `tests/planet_apparent_size.cpp` |
| Connect / disconnect all hardware (phase 46) | `[HW]` button + `MountProxy`, gated by `tests/hardware_link.cpp` — **built and gated, not wired: the button has never been pressed in the running app** |
| Click to recentre (phase 47, list item 4) | `Ctrl`+click, through the phase 51 calibration, gated by `tests/recentre.cpp` |
| West and south jogs did nothing on the emulator | `b0015dc` (merged as #21) — a signed rate looked up as preset 0, "stopped"; gated by `tests/mount_jog.cpp` |
| The mount panel: a clipped plot, and a D-pad that never existed | `9db7cd7` (merged as #22) |
| Lower the CMake floor (P5.5) | `48e53a9` — 3.21, found by building |
| Fuzz the WebSocket parser, token on every connection (P5.6) | `137dc5c` — gated by `tests/ws_protocol.cpp`, `ws_fuzz.cpp`, `ws_server_loopback.cpp`; the real dashboard connected and jogged the mount 2026-10-25, **owes one remote stop seen to work** |

---

<a name="next-in-order"></a>
### Next, in order

**Reviewed 2026-10-17 (second pass).** Phase 92 and the SIMBAD work
have moved above the line. The knowledge-database rebuild is demoted
to a footnote, because checking it showed it would not do what the
entry claimed.

**Re-read 2026-10-18.** Items 1, 2 and 3 were all already shipped and
the list had not been told, so the first four rows are history and the
queue really starts at the unscheduled features.

**And this list being stale cost an afternoon.** Item 3 was
implemented twice: once on `master` in `f4f4bda`, and again on a branch
cut from `f2f2edc`, an hour older, whose copy of this file still had it
queued. Both arrived at the same design -- the distance was already in
`PlanetRADec`, Saturn needs the ring span -- which is some comfort
about the design and none at all about the process. The branch's copy
was dropped in favour of the one on `master`, which is better sourced
(IAU radii and an exact arctangent, against doubled Meeus
semidiameters and the small-angle form).

The lesson is cheap to apply: **this file is only true of the branch
you are reading it on.** Check `git log master` for the item you are
about to start, not just the table.

**Reviewed 2026-10-19, after the competitive comparison.** The P5
items are inserted rather than appended, and the placement follows
[How this is ranked](#how-this-is-ranked) at the top of this file
rather than the comparison's own scoring, which weights for a market
and not for this telescope. Most of P5 is category 6, things that
cost nothing now and pay later: a seam, a build floor, a tag, a
license decision. A couple are category 4, in that they cost time or
possibility every session.

**P5.6 is the exception and goes in above everything else here**,
because an unauthenticated command path that slews the mount is
category 2 the moment `RADUK_WS_LAN` is set, and the hand-rolled
parser underneath it is the only attacker-reachable C++ in the
project. P5.5 and P5.1 come next on size: both are small and both
finish, which is what category 6 work has to be to be worth queueing
at all. P5.4, P5.3 and P5.2 are category 6 at medium size and sit
below phase 46, which is category 4 and pays back every session.
**P5.7 is not in this list**, because it is a decision rather than
work; it sits with the other owner decisions further down.

**The item that outranks all of it is not here, because it needs
sky.** Clear-night item 3, the plate scale, is category 1 territory:
every arcsecond figure the application reports derives from it, and by
the comparison's own weighting it is the highest-value unticked item
in the project. It costs no code at all. See [Live-sky debt,
consolidated](#live-sky-debt-consolidated).

| # | Item | Where | Size | Why here |
|---|---|---|---|---|
| ~~1~~ | **`SlewVariable()` — DONE 2026-10-17** (`eef6c25`) | Bench | — | Above the line |
| ~~2~~ | **Assistant: retrieval over the real corpus** — DONE 2026-10-17 (`85b9d51`) | Bench | — | Above the line. 16 documents and the six most recent session logs, matched by line and bounded at 8.3 KB |
| ~~3~~ | **Apparent size for the planets** — DONE 2026-10-17 (`f4f4bda`) | Bench | — | Decided for the ephemeris, and it cost almost nothing: `PlanetRADec` was already computing the Earth-planet distance for the declination and throwing it away. IAU 2015 equatorial radii and `2*atan(R/d)`. Saturn is reported twice over, because the 41" ring span is what has to fit the frame and the 18" globe is what "apparent size" means. Swept 16 years against published ranges — `tests/planet_apparent_size.cpp` |
| ~~4~~ | **Phase 47, click to recentre** — DONE 2026-10-18 | Bench | — | `Ctrl`+click an object and it goes to the middle. No new orientation config: the phase 51 guide calibration already MEASURED how the image responds to a pulse, so it contains the camera angle, the mirror flip and the axis signs. The 2x2 solve moved into `src/core/GuideSolve.h` and the guide loop now calls the same one — two copies would be two sign conventions. Gated by `tests/recentre.cpp`, including a round trip that pushes the solved pulses back through the measured matrix |
| ~~5~~ | **Phase 46, connect/disconnect hardware** — BUILT and GATED 2026-10-18; owes one press in the running app | Bench | — | One `[HW]` button, and `MountProxy` so a reconnect can pick a different transport or port without stranding the tracker and the panels that hold the mount pointer. The crash it caused on the way is worth reading: `main.cpp` was static_cast-ing the mount pointer to `MountEmulator` |
| 6 | **P5.6: fuzz the WebSocket frame parser, and authenticate the command path** — WIRED (emu) 2026-10-25: the real dashboard connects with the token and jogs the mount; owes one remote stop seen to work, and `apex_bridge.py` | Bench | S-M | The only safety-flavored item left on the bench. An unauthenticated RFC 6455 parser in C++ that accepts mount commands; loopback-by-default is a good default and is not a control. Raised in the July 2026 security review and hardened in Phase 57 without the auth gap being closed |
| 7 | **Pick again from the unscheduled list** | Bench | varies | Ten features left, none half-built. On value to this rig **phase 48** (framing recall) is next, with phase 30 (push-to assistant) behind it. Nothing of 48 is built yet: PR #21's branch was named `phase48-observation-record` but carried only the jog fix |
| ~~8~~ | **P5.5: lower the CMake floor** — DONE 2026-10-25, floor 3.21 by test | Bench | S | The cheapest item on the list. One line, once the oldest workable version has been established by trying it |
| 9 | **P5.1: exposure-midpoint SER timestamps** | Bench | S | Parity with SharpCap today, and a bounded chance to lead. Needs the exposure threaded to the stamp site, which the report got slightly wrong |
| 10 | **P5.4: installer, tags, and no hardcoded `D:` paths** | Bench | M | The distribution half of phase 20, split out of a VH item that was never going to be started whole |
| 11 | **P5.3: ASCOM reconsidered** | Bench | M | Not re-enabling (it is already live) but re-examining the blacklist that closes the generic path, and making it testable indoors |
| 12 | **P5.2: `ICamera` and `IFocuser`** | Bench | M | Precondition for any hardware breadth at all. No visible feature at the end, and a hot path to not regress |
| — | **Xbox controller** (P4.1) | **HELD** | M | No controller, no receiver, no Bluetooth in this machine. Feasibility proven (`8fed05a`); item 1 removes its software prerequisite. Phase 47's click-to-recentre is the other half of the same idea and is in |
| — | **Rebuild the knowledge database** | Needs network | S | **Demoted: it would not do what was claimed.** `Allen's Star Names` is listed as the mythology source in the builder's docstring and there is **no loader for it**, so a rebuild recovers no mythology for the 670 non-solar objects — that code was never written. The distances it was really wanted for are already in. What is left is Wikipedia summaries for objects that lack one: a 20–30 minute rate-limited fetch for marginal gain |
| — | **Mythology for the deep sky** | Content | M | The honest version of the item above. 670 narratives have no cultural context and no source is wired up to give them one. This is a content-sourcing decision, not a coding task |
| — | **Assistant: a larger model** (P3.6 option A) | — | M | Behind D4. An 8B at 4-bit fits the 2060's 6 GB |
| — | **Emulator blur, further** (P4.4a tail) | Bench | S | Only if full-sensor bench work becomes routine. Focusing the emulated EAF skips it entirely today |
| — | **AI runs on CPU** (D4) | — | — | Not actionable in code. Driver 546.01 caps at CUDA 12.3 |

<a name="found-2026-10-25"></a>
### Found by the 2026-10-25 review

Every item here is a claim this file made that `git` or `grep` could
have refuted in a second, and nothing did.

- **"No tags, 58 commits"** (P5.4, P5.7). 13 tags and 220 commits.
  Corrected in both entries.
- **Three version numbers.** `src/Version.h` was `1.2.3` from
  `a1a64f6` on, though its own header says it is bumped on every push;
  the help file labelled features up to v1.2.7; the newest tag is
  `v2.0`, from 2026-10-12. **Settled by the owner the same day: the
  version is 2.0.** `Version.h` read `2.0.0` (CMake requires
  three parts), and the help file, the architecture note and the wiki
  say v2.0. The v1.2.4 to v1.2.7 labels stay, as dates of change.
- **Phase 46 was "DONE" here and `BUILT` in `PHASE-STATUS.md`.** The
  status file is the one that counts, and the button has never been
  pressed in the running app. This file now says built and gated.
- **P5.5 and P5.6 had status asserted only here.** This file ranks and
  does not assert status, so `PHASE-STATUS.md` now carries a row for
  every P5 item, and P5.6's outstanding connection test is tracked
  there.
- **The jog fix never reached the Done table**, and with it the note
  that the emulator had been blind to two of four jog directions.
  Added, and cross-referenced from clear-night item 14.
- **The `D:` path list was about half the real one.** Expanded, with
  the grep that produced it, because a fixed list is the thing that
  goes stale.
- Two rows were in the Done table twice, a section repeated the one
  above it, and two cited line numbers had drifted. Removed or fixed.

### Found 2026-10-18 — SAVE could erase a profile's note

**The NOTES box is not part of the rig, and SAVE treated it as if it
were.** Every other field SAVE writes is read back off the camera, the
focuser or the panel, where the live value is the true one. The note is
not like that: the box holds a note for ONE profile, and it holds
nothing at all until a profile is loaded into it. So SAVE aimed at a
profile that had not been loaded handed it an empty string. That is how
`saturn.ini` lost

> Longer exposure than Jupiter. Rings add contrast challenge.

during a night on Jupiter; it was typed back in by hand in `f2f2edc`
without the cause being found.

The box was emptier than it looked, too. Every panel section starts
collapsed, so `m_editNotes` is null until NOTES has been opened at all,
and the getter answered `""` for a missing control — which made the
loss unconditional on a fresh run, including for the profile just
loaded, because its note went into the cache that survives a rebuild
and nothing read it back.

Fixed in `202822f`: the accessors go through that cache, the box
records which profile its text came from, and
`NotesBoxMayReplaceStored` decides whether a SAVE may overwrite — keyed
by name, so a rename through EDIT fails safe. Emptying a profile's own
note still clears it. `tests/profile_notes.cpp` pins the rule,
including the `saturn.ini` case.

Same shape as D1 and the DLL below: nothing failed, nothing was logged,
and the loss was discoverable only by going to look for a note that was
no longer there. A kept note is now announced on the notification bar.

### Found by the 2026-10-17 review

**The test suite was passing on a file copied by hand.** `tests/_build`
is gitignored and `run-tests.ps1` never copied `sqlite3.dll`, so
`object_knowledge` passed only on this machine, in this working
directory, because the DLL had been put there during development. On
a clean checkout the gate failed. Reproduced by deleting the file,
then fixed: the suite copies it as part of the run and warns loudly
if the source is missing.

That is the same shape as the defect the test exists to catch —
`ObjectKnowledgeDB` fails *gracefully* without the DLL, so nothing
announces itself; it just quietly reports an empty knowledge base. A
gate that depends on an untracked file is not a gate.

### A decision for the owner, raised 2026-10-19

**P5.7, the license.** PolyForm Noncommercial 1.0.0 is source-available
and is not an OSI open-source license, so it is GPL-incompatible, it
will not be packaged by a distribution, and it is closed to anyone
whose employer runs a legal review. Against a stated bus factor of 1,
that costs more than the commit count does: N.I.N.A. is 79%
one author and survives anyway on MPL-2.0 and 78 contributors. Keep,
relicense or dual-license, with the trade-offs set out in P5.7. Not a
task, and not a recommendation already accepted.

### Both questions answered by the owner, 2026-10-17

**Phase 16, polar alignment: backlogged, not deferred.** The 8SE is
alt-az and there is no wedge, so the procedure does not apply. It
comes back only with a wedge and most likely a new mount, which makes
it a different project rather than a queued feature.

**Phase 77, the AutoStakkert launch: it works. Closed 2026-10-18.**
It had been struck off as "proven not to be ours" because the identical
command failed from a bare prompt, which pointed at AutoStakkert's own
state being wedged on this machine. The owner reported that AS
functions correctly here, so the premise was gone — and re-measuring
closed the question rather than reopening it.

Three runs, same 158 MB Jupiter recording, no reinstall or reboot:

| Route | Time | Result |
|---|---|---|
| Standalone, PowerShell | 51.4 s | stacked |
| Via `StackAsync`, the app's own path | 50.4 s | `ok=1`, 8 files |
| Via `StackAsync`, AS already open | 57.0 s | `ok=1`, 8 files |

All three log `Processing started` 0.45 s after `started...`, which is
the working signature. **There was never anything wrong with the app's
code**, and the third run kills the best remaining theory: a second AS
instance holding a single-instance lock. `StackAsync` does not check
for a running AutoStakkert, and one *was* running during the original
investigation, which made it look like the answer. It is not, so no
guard is needed.

What broke it originally is unexplained and not reproducible —
something about that day's machine state. Recorded as unexplained
rather than given a theory it does not deserve.

One trap found on the way, worth more than the bug: launched from Git
Bash or another MSYS shell, `/switches` are rewritten into paths
before AS sees them, and the symptom is identical. The log tells them
apart — a mangled launch shows **no `command line ...` lines at all**,
a genuine failure-to-begin shows every switch and then stops.

`AutoStackOnStop` stays off, now for its own reason: stacking is
minutes of CPU and should be opted into.
`tests\manual-autostakkert-launch.cpp` re-runs the measurement.

### Still open, carried from before

- **6.6 GoTo map learning** — auto solve+sync on lock ships; the
  accumulating pointing map **reports but does not correct**. Half a
  feature, and the half that is missing is the half that helps.
- **Clear-night items 1–12.** Item 13 is discharged (see below).
  Items 1 (`SHARP_CEIL`) and 3 (plate scale) are the two that other
  numbers depend on, and plate scale now matters more than it did:
  the assistant quotes it, the ADC tool derives from it, and the
  emulator's field of view is built on it.

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

#### P4.3 — the original entry, kept for the record

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

- ~~**Item 13 — the capture pipeline at real frame rates.**~~
  **Settled on the bench 2026-10-17** and no longer a reason to spend
  a clear night. The emulator's 20 fps cap came off (P4.3) and a real
  recording ran at **293 MB/s with zero dropped frames** at
  3840×2160 — 556 frames, every one in the file, sitting on the disk's
  own measured ceiling. The decoupled recording path and the run-sized
  write buffer both held. What a real night still settles is whether a
  real ASI678MC over real USB behaves the same, which is a question
  about the camera rather than about our side of it.
- **Item 14 — click to recentre, on the sky.** Added 2026-10-18. The
  arithmetic is gated on the bench and the round trip is exact against
  the measured matrix, but the matrix itself comes from a real
  calibration on a real star, and no bench test can produce one. What a
  night settles: that a click lands the planet in the middle first
  time, and that the ten-second pulse ceiling is the right place to
  stop and slew instead. Until `b0015dc` (2026-10-18) the emulator
  ignored west and south jogs, so any recentring checked against it
  before then saw only two of four directions.
- **Item 1 — `SHARP_CEIL`**, which biases the capture gate and
  autofocus.
- **Item 3 — plate scale**, never once checked against the sky, and
  every arcsecond figure in the app derives from it, the ADC tool's
  included.

### Backlog — raised, not yet scheduled

#### B1 · The object panel clips its text, and bleeds it over the diagram

Raised 2026-10-18 from a screenshot. Two defects in one section, and the
diagnosis is recorded here so it is not derived a second time.

**It clips.** The long prose description runs off the bottom of the
panel and stops mid-sentence. `ObjectPanel.h`'s one text helper calls
D2D `DrawText` with a *layout* rect and no
`D2D1_DRAW_TEXT_OPTIONS_CLIP`, so the height argument is advisory:
DirectWrite lays the whole string out and every line is drawn, with the
window edge — not the layout — doing the truncating. Nothing measures
first. `EstimateHeight` exists for that job, has **no call sites**, and
its own comment asserts the text "is clipped to the rect either way",
which is false.

**It bleeds.** For a star the prose runs straight across the scale
diagram. The width is `cw2 = aside.empty() ? colW : (colW - gut) * 0.5f`
— `aside` is mythology or discovery notes, which exist only for the nine
solar-system rows. So every star takes the full-width branch, and
because the left column deliberately does not clear the diagram
(correct at half width), the prose paints over it.

A third, smaller instance sits in the derived-lines loop: each line gets
a 1.3em rect while `y` advances 1.15em, so a wrapped derived line
overprints the next.

The fix wants measuring and fitting, not just a smaller font — though
the font should shrink too, since it is derived from window **height**
alone and never from how much there is to say. Two things in this
codebase already do the work: `SkyMapWindow::TextWidth` uses
`CreateTextLayout` + `GetMetrics`, which is exactly the measurement
needed, and `HelpWindow::FontPxFor` is the existing shrink-to-fit loop.
`ObjectPanel` already holds `m_dwrite`.

#### B3 · The dashboard's preview canvas has never shown an image — DONE 2026-10-25

**Fed, not labelled** (the owner's call). The app sends a small JPEG of
the preview, about twice a second, to dashboards that ask for one:

- **Render loop:** every 500 ms, and only while a client is subscribed,
  `DecimateFrame` (`src/net/PreviewFrame.h`) reads one Bayer cell per
  output pixel into a picture at most 640 wide, auto-levelled at the
  99.9th percentile. About 230 thousand cell reads at the full sensor.
- **Its own thread:** `PreviewStreamer` encodes with WIC and keeps only
  the newest picture; a slow consumer replaces, never queues.
- **Server:** binary messages, only to clients that sent
  `{"cmd":"preview","arg":"on"}` (so `apex_bridge.py` is never sent
  one), skipping a client whose socket is full. Every client socket now
  has a 500 ms send timeout, and a short send drops the client -- before
  this, a client that stopped reading could block `Broadcast()` and the
  render loop with it indefinitely.

Gated by `tests/preview_frame.cpp` (Bayer order, RAW16, sizes, the
auto-level, the subscription message, a WIC round trip, newest-wins)
and a new section of `tests/ws_server_loopback.cpp` (only a subscriber
receives it, as one binary frame, and "off" stops it).

Measured 2026-10-25 on the emulator, full sensor (3840x2160), blur
off, `RADUK_EMU_FPS=120`, 15 s per leg, read from the app's own
telemetry by a separate WebSocket client:

| | capture fps | dropped | previews |
|---|---|---|---|
| preview off | 36.5 | 0 | — |
| preview on | 36.7 | 0 | 1.94/s, 5.6 KB each, 11 KB/s |
| off again | 37.0 | 0 | — |

A subscriber that never reads: the healthy client's telemetry kept
coming (60 messages in 30 s), with one 1 s gap when a send to the
stalled client hit the 500 ms timeout and the server dropped it. That
is the designed worst case, and the most a stalled browser can delay
a command, STOP included -- the mount's own watchdog allows 8 s.
Not measured: the same while RECORDING. Recording runs on the capture
thread (`37b8c50`) and the preview touches only the render loop and
its own thread, but that is an argument, not a number.

The entry as raised:

Found 2026-10-25, testing P5.6 with the real dashboard. The RC tab's
centre column is a black `rcPreview` canvas under a HUD, and nothing
has ever drawn into it: no code in the dashboard calls `getContext` on
it, and RadukCapture sends no image over the WebSocket at all —
telemetry and a histogram only. It has been this way since the first
public release (`9bc0036`). The thing meant to feed it was the RTSP
stream, phase 7, which is `DROPPED`.

Category 3 in [How this is ranked](#how-this-is-ranked): a feature
that is on screen and does nothing. Two honest fixes were offered:

- **Say so.** Replace the canvas with a "telemetry only — no image
  feed" panel. Minutes.
- **Feed it.** A small downscaled JPEG of the current preview — a few
  hundred pixels wide, one or two a second — sent as its own message
  type over the existing socket. WIC is already in the tree for the
  `S` snapshot. Medium, because it must never cost the capture loop a
  frame, and the frame-rate sink it would add has to be measured, not
  assumed. The rule in the wiki still holds: raw frames never cross the
  network.

Found in the same test and already fixed: the dashboard could start a
recording and never stop one, because `rcIsCapturing`, the flag its
capture button reads, was declared and never assigned.

#### B2 · Product comparison against other astronomy imaging software

Raised 2026-10-18, **for a separate session to carry out.** Not research
to be done here.

**Precondition:** the documents must be true first. A capability
comparison run against documents that misstate what is built is worse
than none, because it produces a confident gap list with the wrong gaps
in it. That is what the 2026-10-18 truth pass was for; check
[`PHASE-STATUS.md`](PHASE-STATUS.md) is current before starting.

Compare against three groups:

- **Planetary capture peers** — FireCapture, SharpCap, ASICAP, AstroDMx
  Capture. The direct competitors for what this rig actually does:
  high-frame-rate planetary capture with ROI control and SER output.
- **DSO and automation suites** — N.I.N.A., SGP, Ekos/KStars. Where the
  expectations about sequencing, equipment profiles, plate-solve-and-
  centre and scripting come from.
- **Guiding and precision** — PHD2, as the reference for the
  measured-not-assumed calibration this app's tracker already follows,
  and the benchmark for any pointing-accuracy claim.

Dimensions worth judging: capture pipeline and sustained throughput;
sensor and ROI control; autofocus; tracking and guiding accuracy; plate
solving and pointing; **metadata and file-format obligations**, which is
where phase 48's observation record and FITS output get judged;
automation and scripting; ergonomics for night use; hardware breadth;
and data integrity — what each application actually guarantees about
frames reaching the disk.

The deliverable is a document ranking gaps **by value to this rig**, and
separating "genuinely missing" from "deliberately out of scope". Full
stacking is the obvious second case: it is a stated non-goal, because
AutoStakkert does it better and nothing here would beat it.

### Unstarted features, not yet scheduled

Ranked by value to this rig rather than by the original tier numbers.
Whether any of these is partly built is
[`PHASE-STATUS.md`](PHASE-STATUS.md)'s question, not this table's — this
one used to claim "nothing below is half-built", which stopped being
true the moment a phase was split in two.

| Phase | Feature | Effort | Note |
|---|---|---|---|
| ~~47~~ | ~~Pixel position / click to recentre~~ | — | **Done 2026-10-18.** `Ctrl`+click, on the measured guide calibration |
| 47b | Pixel position readout + directional rose | M | The half of phase 47 that a plate solve really is needed for, split out when the other half shipped without one. Absolute RA/Dec under the cursor, and a rose showing which way a jog pushes the image |
| ~~46~~ | ~~Connect / disconnect all hardware~~ | — | **Built and gated 2026-10-18; owes one press in the running app.** `[HW]` button; `MountProxy` keeps the mount pointer stable across a reconnect |
| 48 | Go to previous image (framing recall) | M | Design settled 2026-10-18: an observation record written beside each capture, not a plate solve, since a planetary crop has no solvable star field. **The prerequisite for 18 (FITS) and 19**, which want the metadata it collects |
| 30 | Push-to assistant | M | |
| 31/32 | FWHM / Bahtinov focus | H | Star focus; autofocus is tuned for a planetary disc. **A visible gap in the comparison**: SharpCap ships multi-star FWHM, HFD and Bahtinov aids with Pro auto-scans, FireCapture has a third-party autofocus plugin, ASICap has coarse/fine EAF buttons. Nothing in this tree implements either measure (checked: no FWHM or Bahtinov code in `src/`), and the planetary-disc V-curve does not substitute for star focus |
| 18 | AVI / FITS output | M | **FITS leads, AVI follows, and the comparison raises the value of both.** FITS is the interop format the post-processing chain consumes and is the one output that would let this project touch any photometric or deep-sky workflow; AVI is a legacy container that chain does not want. SharpCap and FireCapture both write FITS; ASICap does not, so "every competitor writes FITS" is too strong, but the two that matter do. Neither writer exists anywhere in `src/` |
| 19 | Multi-session stats dashboard | M | |
| 34 | Solar / lunar framing | H | |
| 43 | Deep sky image annotation | H | |
| 33 | Stellar photometry | VH | |
| 20 | Distribution / ASCOM support | VH | **Re-scoped 2026-10-19: split in two and neither half is VH.** Distribution is P5.4 (M, installer, tags, and removing the hardcoded `D:` paths). ASCOM is P5.3 (M, and the premise was wrong: it is live, not disabled). Carrying them as one VH item is most of why neither has been started |


### Partial, and known broken

| Item | State |
|---|---|
| 6.6 GoTo map learning | ◑ Auto solve+sync on lock ships; the accumulating map **reports but does not correct** |

### Backlogged, not queued

| Item | Why it is not on the list |
|---|---|
| 16 Polar alignment assistant | Needs a wedge, and there is no wedge. Returns with a wedge and probably a new mount — a different project, not a queued feature |


---

<a name="live-sky-debt-consolidated"></a>
## Live-sky debt, consolidated

**Item 3, the plate scale, is the one to do first.** Added to this
list explicitly on 2026-10-19, because being carried inside "existing
items 1 to 10" had let it sit unweighted. By the comparison's own
weighting it is the single highest-value unticked item in the whole
project: `plateScaleArcsecPx = 0.203f` at `src/core/RigConfig.h:136` is
a calculated number that has never once been checked against the sky,
and **every arcsecond figure the application reports derives from it**,
the ADC advisor, the drift readout, the overlay scales, the emulator's
field of view and the figure the assistant quotes included. It also
costs no code: plate-solve one frame and read the comparison line the
app already prints. Nothing else on any list in this document returns
as much for as little.

Existing items 1–10 in `CLEAR-NIGHT-CHECKLIST.md`, plus two added by
this round:

- **11 · Histogram against a real sensor.** Do the bench half (P2.0)
  first, so the night is spent only on what the bench genuinely cannot
  answer.
- **12 · Sharpness readings on a real target.** Folds into item 1;
  listed so the owner's report is traceable to an answer.

- **13 · The capture pipeline at real frame rates.** Added
  2026-10-14, **largely discharged 2026-10-17.** The premise was
  wrong in an instructive way: the emulator's "16 fps" was
  `sleep_for` rounding to the Windows timer tick, not render cost.
  With the cap lifted, a real recording sustained 293 MB/s with no
  drops. Only the real camera over real USB is still untested.
