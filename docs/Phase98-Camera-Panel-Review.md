# Phase 98 — the camera panel review

> *"there is a problem with the camera sliders they are difficult to
> interact with. also the Auto Exposure feature seems odd. Do a
> complete review of the camera panel"*

Plus three more reports that arrived while it was underway, two of
which turned out to be the same defect as each other.

Everything below was measured, not judged. The two complaints were
both arithmetic.

---

## 1. The sliders

### 1.1 The exposure slider was linear over a 60,000:1 range

425 px of travel, 1 to 60,000 µs: **141 µs per pixel.**

| band | linear | log |
|---|---|---|
| Moon, 1–5 ms | **6.7%** of travel | 25.4% |
| Jupiter, 5–20 ms | 25.0% | 21.5% |
| Saturn, 20–60 ms | **66.7%** | 17.2% |

Two thirds of the control went to the longest exposures and **3.00 ms
could not be set at all** — the reachable neighbours were 2.966 and
3.107.

Exposure is judged in **stops**: 2→4 ms matters exactly as much as
20→40 ms, because both double the light. A linear scale gives the first
a tenth of the room it gives the second. The log scale gives every
doubling the same distance and every step the same 2.53%, so the error
is proportional to the value — the only sensible behaviour for a
quantity spanning five decades.

Mapping in `src/core/ExposureScale.h`, pure so a test can reach it: a
scale that is wrong at one end looks entirely reasonable in the middle,
and the endpoints are exactly where an off-by-one hides.

### 1.2 The keyboard was unusable in both directions at once

`MakeSlider` never called `TBM_SETLINESIZE` or `TBM_SETPAGESIZE`, so the
defaults stood: **arrow = 1 µs** (sixty thousand presses to cross) and
**page = 12 ms**, with nothing whatsoever in between. Now 2.5% and 49%
on exposure, and a hundredth/tenth of the range on every other slider.

### 1.3 You could not see the change while dragging

`commit` was `TB_ENDTRACK || TB_THUMBPOSITION`, so a drag moved the
label and never wrote to the camera. On a 141 µs/px slider that forces
guess → release → look → re-grab, at the one moment the preview is most
worth watching.

Committing on every drag message instead would be 50–100 USB control
transfers a second competing with the frame stream — the bandwidth
`USBManager` exists to protect. So a drag applies at most **eight times
a second**, and always once more on release so the final value is never
the throttle's casualty.

### 1.4 Nothing could be typed

Gain, Exposure, Gamma, WB and USB were all read-only labels. The file
prefix, frame limit, time limit and gate threshold all had edit boxes.
**The camera section was the one place with no numeric entry**, and
dragging was the only input it had.

Exposure now has an edit box, committed on focus-leave and on Enter
(the box is subclassed for that one key, because Win32 does not deliver
Enter to the parent). Escape restores the current value. A value that
will not parse restores it too, so the box can never show something the
camera is not doing. Out of range clamps and reports rather than being
rejected: someone typing 500 into a box labelled ms has asked for half
a second, and the honest answer is the ceiling, shown.

### 1.5 14 px tall on a 17 px pitch

Three pixels of clear space between one slider and the next, six
stacked. A near-miss grabbed the neighbour. Now 20 px on 24.

### 1.6 Auto-exposure fought your hand

`UpdateSlider` was a bare `TBM_SETPOS` with no check for an in-progress
drag, so AE in AUTO could yank the thumb out from under the cursor every
two seconds. It now skips the write while the slider is held, and skips
the edit box while it has the caret.

---

## 2. Auto Exposure: the real defect

### 2.1 It stalled below 3.4 ms and said "limit"

The correction is multiplicative — `exp × (1 + error/255 × iGain)` —
and was discarded unless it moved the exposure by at least a **flat
0.1 ms**. At the smallest error that escapes the ±15 DN deadband:

| exposure | error needed, before | after |
|---|---|---|
| 0.5 ms | **102 DN** — unreachable | 5.1 DN |
| 1.0 ms | **51 DN** | 5.1 DN |
| 2.0 ms | **25.5 DN** | 5.1 DN |
| 3.0 ms | **17 DN** | 5.1 DN |
| 3.5 ms | 14.6 DN — works | 5.1 DN |
| 50 ms | 1.0 DN | 5.1 DN |

Below 3.4 ms the exposure could not move, **and** the gain branch is
gated on being *at* a limit, which it was not — so nothing happened and
it fell through to a message reading `AE: limit`.

**It did not fail. It reported a limit it was nowhere near.** The Moon
lives at 1–5 ms.

The fix is a **relative** minimum step: 1% of the current exposure,
floored at the camera's own 1 µs resolution. The threshold becomes
5.1 DN at *every* exposure — a controller whose sensitivity depends on
where it happens to be sitting is not one — and at 50 ms the step is
0.5 ms, so jitter rejection at the long end **improved** rather than
being traded away.

### 2.2 The stall message printed the wrong quantity

It showed `m_meanBrightness`, while every decision is made on
`m_highlightDN`. On the one occasion the operator most needs to know
what is happening, the panel named a limit it might be nowhere near and
showed a number the controller was not using. It now says which of
"at maximum", "at minimum" or "holding" it actually is, with the figure
it is actually using.

### 2.3 The documented gain was a fifth of the real one

`iGain = 0.10f` with a comment saying "10% correction per 2s tick",
and the expression multiplied it by a bare 5. The real figure has always
been 50%. The constant now says what it does; the arithmetic is
unchanged.

---

## 3. The three later reports

### 3.1 and 3.2 — one dead function, two symptoms

> *"the record button does not depress after the recording ends"*
> *"need to add a timer on the recording"*

`ControlPanel::UpdateCaptureStatus` formats `REC mm:ss` **and** is the
only thing that invalidates the owner-drawn RECORD button on a state
change.

**It was dead twice over.** No callers at all — and its first statement
is `if (!m_lblRecStatus) return;` while that label and its companion
were *declared and never created*, so it would have aborted on its
first line even if something had called it.

So there was no timer because one had been written and never wired, and
the button stuck because the same function was the only thing that would
have repainted it. A run stopped by its own frame limit left it showing
STOP RECORDING indefinitely.

Both labels now exist, the function is called from the readout loop, and
the button's repaint moved to the **state edge** in `UpdateStatus` —
which fixes every stop path at once (frame limit, time limit, disk-full
abort, emergency stop, the mosaic runner's per-panel stops) rather than
the one that happened to be reported, and does not repaint twice a
second forever.

The timer shows elapsed, and **remaining** where a limit is set: exact
on a time limit, estimated from the rate achieved so far on a frame
limit and marked `~` because of it. The nominal frame rate is not what
a run with drops is delivering. Withheld for the first second, because
a prediction off half a second of run is noise with a decimal point.
And the last run's duration survives it ending — the figure was in hand
and discarded at the moment it matters most, when the operator is
deciding whether to do another.

The status window gets the same clock beside its `REC`, that being the
surface glanced at across a room.

**A third defect on the same path, found while there:** the
limit-triggered stop was a bare `capturer.StopRecording()` and skipped
the USB bookkeeping the manual stop does, so after a frame-limited run
**USB stayed in capture mode with the EAF and mount still throttled.**
Both paths now go through one `endRecording`.

### 3.3 Panel state

> *"screen panel locations are being saved but their state is not"*

Exactly so, and it was a **missing field** rather than a broken one.
`Placement` recorded the monitor, the mode and the geometry, and
`PlaceMode` is `Normal | Maximized | Fullscreen` — all three *visible*.
There was no way to write down "this one was closed".

`visible` now exists, defaulting true so every layout file written
before this still means what it meant. The chosen behaviour is **closed
stays closed but keeps its position**, so opening a panel later lands it
where it belongs. `SetWindowPos` does that for free; the only thing that
could reveal a hidden window was the maximise, now gated on it having
been open.

That gate also retires a hand-made workaround: the generated default for
the share view carries a comment saying it is "deliberately not marked
Maximized ... Place() calls ShowWindow(SW_MAXIMIZE) on a maximised role,
which would put an empty black screen up at launch." That was the right
fix for one role with no way to express the general rule. The rule is
expressible now.

---

## 4. The pattern worth naming

`UpdateCaptureStatus` is the **fourth** dead thing these reviews have
turned up, after `Theme::DEEP` (a palette token painted by nothing),
`DrawSectionHeader` (a painter with no callers) and the two
never-created labels here.

Three of the four were found only because something else went looking.
A declaration is not a feature, and in this codebase the gap between
"written" and "reachable" has been where the bugs live.

---

## 5. Verification

`tests/camera_panel.cpp`, 21 assertions: the scale reaches both of its
own ends exactly; it is monotonic and position→µs→position is the
identity, so a writeback cannot nudge the thumb; every band gets at
least a tenth of the travel where the Moon had 6.7%; 3.00 ms is
reachable; the step is the same *fraction* everywhere; arrow and page
are a nudge and a jump rather than 1 µs and 12 ms; the AE dead zone
stalled at four of eight test exposures before and none after, with an
identical threshold across the range and better jitter rejection at
50 ms; and every exposure the help recommends lands within half a step.

Suite **37 pass / 2 fail / 0 build-fail** (`ai_context_facts` and
`object_knowledge` are pre-existing and environmental). Every GUI
translation unit and `main.cpp` clean in both configurations.

**Nothing here has been used on a camera.** The arithmetic is checked;
whether a 20 px slider on a 24 px pitch feels right under a cold hand at
2am is not something a container can know. Clear-night item 19.
