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

### P3.1b · Status telemetry: all drives, and a disk figure that moves

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

### P3.2 · Hide the viewfinder overlays — `U` / `Shift+U`
**Bench. Small. Partially built already.**

Requested: one key hides everything drawn over the viewfinder *except
the reticle*.

An uncommitted change in the tree already does most of this on
`Shift+O`, gating the histogram/temperature side panels and the quality
graph. What is missing is the rest of the overlay set — caption, zoom
hint, ADC readout, pixel readout, HUD status line — and the binding the
owner asked for. Finish it as `U`, leave the reticle out of the gate,
and keep the setting in `rig.ini` as the current version does.

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

### P3.5 · Mount controller enlarged, laid out like a gamepad
**Bench. Medium.**

Requested: considerably larger, and arranged like an Xbox controller so
it reads at a glance.

Good fit for a redesign because the current panel is 210x326 and fixed —
nothing about it is worth preserving verbatim. A gamepad arrangement
also maps naturally onto what the controls already are: a direction
pad for N/S/E/W, face buttons for STOP and ALIGN, shoulder positions
for rate selection.

It needs the same `WM_SIZE` layout pass as P3.3, so sequence it after
that pattern is established.

This is a **prerequisite for P4.1**: design the on-screen layout around
the controller first, then wire the real controller to it.

---

## P3.6 · Make the assistant worth asking

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
**Bench, then sky. Medium.**

The owner's own sequencing: layout first, functionality later.

Technically straightforward — XInput is a small API and polling it in
the existing readout loop is natural. The design questions are the ones
worth thinking about before writing code:

- **Which stick does what.** Left stick to slew is obvious; the rate
  wants to be a shoulder button rather than a mode you can forget.
- **Dead zones.** A resting stick must not creep the mount. This is the
  one place a bug moves the telescope on its own.
- **A hardware STOP.** Something unmissable by feel in the dark.
- **Focus on the right stick** is the obvious pairing, and worth
  guarding: a fat-fingered focus command during a capture is wasted
  frames.

Real value: it is usable in the dark, by feel, with gloves on, standing
at the telescope rather than at the keyboard. That is a bigger quality
change than the feature list suggests.

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

### D2 · Stock Win32 chrome ignores night vision
**Severity: low-medium. Deliberately deferred 2026-10-12.**

With night vision on, the app's own colours go red — every one of them
runs through `Theme::NV`. What stays bright is the chrome Windows draws
itself: push-button faces, tab controls, list-view backgrounds and
headers, and the title bars on the panels that do not set dark mode.

Measured on the panel screen with night vision on: the help window came
out 60% non-red, AI chat 88%, GoTo 98% — the GoTo figure driven by its
object-catalog list view, which is a large white rectangle.

A fix was prototyped and backed out as over-solving: subclassing every
push button and painting it, because a de-themed classic push button
draws its face from `COLOR_BTNFACE` and ignores the brush
`WM_CTLCOLORBTN` hands back. The cheap parts of that fix are worth
keeping for a future phase:

- `DwmSetWindowAttribute(…, 20, …)` for a dark title bar — three lines
  per window, and the title bar is the brightest single element.
- `LVM_SETBKCOLOR` / `LVM_SETTEXTBKCOLOR` / `LVM_SETTEXTCOLOR` on the
  list views — the largest bright area, and a one-line call each.
- `SetWindowTheme(h, L"", L"")` on edits and combos, which the control
  panel already does and these windows never did.

Leave the buttons alone unless they prove annoying at the eyepiece.

### D4 · The AI runs on CPU because the GPU path cannot work here
**Severity: medium. Diagnosed and worked around 2026-10-12.**

Ollama 0.40.3 ships a llama-server built against a CUDA toolkit newer
than driver 546.01 supports (that driver tops out at CUDA 12.3), so its
kernels fail to load with "device kernel image is invalid" on **either**
card -- the RTX 2060 included. The first guess, that the Pascal P1000
was to blame, was tested by excluding it and proved wrong: the 2060
alone fails identically.

The driver cannot simply be updated, because 546.01 is the last unified
package covering both the Turing and Pascal cards on this machine.

Worked around with , set per-user. Measured at
14-52 tok/s generation, which is usable for short answers. Prompt
processing is the slow part at roughly 100-150 tok/s.

Three ways out, none of them urgent now that it works:

1. **An older Ollama** built against CUDA 12.3 or earlier. Keeps both
   cards and the pinned driver. Needs someone to find which release.
2. **A newer driver**, accepting whatever that does to the P1000.
3. **Leave it on CPU.** For short advisory answers this is fine, and it
   is one less thing coupled to the driver.

### D3 · Night-vision repaint reaches only what is in the layout list
**Severity: low. Introduced 2026-10-12.**

Toggling night vision now repaints every window in `kPlacedWindows`,
which fixed an entire monitor staying bright. Any window *not* in that
list — the object-info popup, for one — still will not repaint until
something else invalidates it. The list is the single place to add new
windows, and this is the second thing that silently depends on it.

---

## Ranked summary

| # | Item | Where | Size | Why here |
|---|---|---|---|---|
| 1 | AI not answering | Bench | S–M | Advertised feature doing nothing; may indicate driver damage |
| 2 | Status panel bleed | Bench | S–M | Corrupts neighbouring UI |
| 3 | Log AI errors | Bench | XS | Prerequisite for 1; third instance of invisible diagnostics |
| 4 | Map → GoTo target sync | Bench | S | Cheapest safety win available |
| 5 | READY modal gate | Bench | M | Safety, explicitly requested |
| 6 | Histogram deep dive (bench half) | Bench | M | Decides whether a core readout means anything |
| 7 | Overlay hide `U` | Bench | S | Half-built already |
| 8 | Status window + out of panel | Bench | M | ✅ done — see P0.2 |
| 8b | Status telemetry: all drives (P3.1b) | Bench | S–M | "Which disk has room" is a pre-capture question |
| 8c | Disk figure reads zero while recording | Bench | S | May be a stuck byte counter, which would affect more than the display |
| 9 | Display identity (D1) | Bench | S–M | Layout silently stops matching |
| 10 | Help window resize + fonts | Bench | M | Proves the pattern cheaply |
| 11 | GoTo + AI resize + fonts | Bench | M–H | Densest layouts in the app |
| 12 | Map contents scaling | Bench | S | Check first — may be closed already |
| 13 | Mount controller as gamepad | Bench | M | Prerequisite for the real controller |
| 14 | Xbox controller input | Both | M | Usable in the dark, by feel |
| 15 | Wiki + doc cleanup | Bench | M | Preserves the reasoning, which is the asset |
| 15b | Assistant worth asking (P3.6) | Bench | M | Not urgent now it is honest about its limits |
| 16 | Night-vision chrome (D2) | Bench | S | Deferred deliberately |
| — | SHARP/PK/AV calibration | **Sky** | — | Clear-night item 1, already instrumented |
| — | Histogram vs real sensor | **Sky** | — | New clear-night item, second half of 6 |
| — | AE convergence | **Sky** | — | Clear-night item 10 |

---

## Live-sky debt, consolidated

Existing items 1–10 in `CLEAR-NIGHT-CHECKLIST.md`, plus two added by
this round:

- **11 · Histogram against a real sensor.** Do the bench half (P2.0)
  first, so the night is spent only on what the bench genuinely cannot
  answer.
- **12 · Sharpness readings on a real target.** Folds into item 1;
  listed so the owner's report is traceable to an answer.
