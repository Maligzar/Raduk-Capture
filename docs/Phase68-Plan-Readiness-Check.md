# Phase 68 plan — pre-session readiness check

A checklist shown once the app is up and the devices are connected,
covering the things that have to be true before imaging works:
alignment, tracking, site, focus, disk. Tick what the app cannot
verify, then continue into the application.

---

## The trap this has to avoid

A modal that appears on every launch and must be dismissed becomes a
speed bump within a week. People learn the position of the button and
click it without reading, and the checklist then actively *costs*
attention while providing none — the user has been trained that this
dialog means nothing.

Two rules keep it honest:

**1. The app verifies what it can.** Most items on this list are
things RadukCapture already knows. A row the machine can check should
check itself and show a result, not ask the user to certify it. Self-
certification of a verifiable fact is theatre.

**2. It only interrupts when something is wrong.** If every automatic
check passes, there is nothing to say and the app should simply start.

The second rule is not speculative — **this codebase already does it.**
`ShowStartupDialog` presents device rows with tick and cross marks and
is called only `if (!hw.AllPresent())`. Phase 68 is that proven idea
widened from "are the devices plugged in" to "is the rig ready", at a
point in startup where more is knowable.

---

## Why it has to run later than the existing dialog

The device dialog runs in `wWinMain` before `RunApp`, which is before
the camera opens and long before the mount connects. At that moment
alignment, tracking and focus are all unknowable.

The readiness check belongs **after device initialisation and the
mount connect**, where `IsAligned()`, `GetTrackingMode()` and the
camera stream can all be interrogated for real.

---

## What the app can check by itself

Every signal below already exists in the codebase. Nothing here needs
new plumbing.

| Check | Source | Why it matters |
|-------|--------|----------------|
| Camera streaming | `cam.IsOpen()`, frame counter advancing | A stalled stream looks like a dark sky |
| Dropped frames | `GetDroppedFrames()` | Non-zero before you start means USB trouble |
| Focuser present | `focuser.IsOpen()`, position sane | Autofocus silently cannot run without it |
| Mount connected | `mount->IsConnected()` | — |
| **Mount aligned** | `IsAligned()` (NexStar `J`) | The precondition everything else rests on |
| **Tracking on** | `GetTrackingMode()` readback | Acquisition is impossible without it |
| Observing site | `RigConfig::SiteIsSet()` | Alt/az rates, sky map, ADC all need it |
| Disk free | `GetDiskFreeSpaceExW` — already used by the panel | A 3000-frame full-frame run is ~25 GB |
| Profile loaded | `profiles.Active()` | — |
| Camera cooling | `cam.Temperature()` vs ambient | Tube currents ruin planetary detail |

The two in bold are the ones that cost a whole session on 2026-10-08.
Both are now machine-checkable, and neither was surfaced anywhere.

---

## What only the user can confirm

Short list on purpose. Every row added here dilutes the ones that
matter.

- **Dust cap off.** Trivial, catastrophic, invisible to software.
- **Scope cooled to ambient.** An 8" SCT wants 45–90 minutes; tube
  currents cap resolution no matter how good the focus. The app can
  show camera temperature as a hint but cannot know the tube's state.
- **Cables dressed and free to rotate.** Pairs with the cord-wrap work
  in Phase 67; ticking it resets the wrap counter.

Three manual ticks is few enough to actually read.

---

## Behaviour

- **All automatic checks pass:** do not show it. Log a one-line
  summary and start. Silence is the reward for a ready rig.
- **Something failed:** show the list, failures first, each with the
  specific fix — not "mount not aligned" but "mount not aligned —
  hand control, Align > Quick Align, needs no stars".
- **Continue anyway** is always available. This is advice, not a
  gate; someone doing daytime testing or bench work must not be
  trapped behind a checklist about the sky.
- **Re-runnable** from a menu, since a failed check is usually
  something you go and fix, then want to re-test without restarting.
- **Live rows.** A failing row should clear itself when the condition
  is fixed — align the mount on the hand control with the dialog open
  and the row turns green on its next poll. That makes the dialog a
  working surface rather than a snapshot to dismiss.

The live-row behaviour is what makes this worth building. Standing at
the mount with the checklist visible, fixing things and watching them
go green, is a genuinely different experience from reading a list and
clicking OK.

---

## The teaching part

The request mentioned explaining how tracking works and what the
pitfalls are. That belongs in the Help file, not in a modal shown
every launch — nobody reads prose they have already dismissed ten
times.

The checklist links to it instead. Each failed row carries a one-line
cause and a link into the relevant Help section, which Phase 66 and
the help update already wrote:

- *Mount will not track (TRK stays OFF)*
- *Acquisition refuses to start*
- *Observing site wrong or missing*
- *Autofocus fails / doesn't converge*

So the explanation is reached at the moment it is relevant, by someone
who has just hit the problem, rather than pushed at someone who has
not.

---

## Storage

```ini
[Rig]
ShowReadinessCheck=auto   ; auto | always | never
```

`auto` is the default and means the behaviour above: appear only on a
failure. `always` suits someone who wants the ritual; `never` is there
because a tool that cannot be turned off is a tool people resent.

The manual ticks are deliberately **not** persisted. A dust cap being
off last night says nothing about tonight, and a remembered tick is a
lie the checklist would be telling on the user's behalf.

---

## Order of work

1. **Readiness model** — one function returning a list of
   `{name, state, detail, helpAnchor}`, with no UI. Testable on its
   own and immediately useful in the log.
2. **Log the summary at startup**, no dialog. Useful from day one and
   proves the checks before any window exists.
3. **The dialog**, shown only on failure, built in code like the
   device dialog and the Phase 66 site dialog.
4. **Live refresh** on a timer so rows clear as they are fixed.
5. **Manual ticks** and the Phase 67 cable reset.
6. **Help links** from failed rows.

Steps 1 and 2 deliver most of the value — a startup line naming
exactly why tonight will not work is the thing that was missing on
2026-10-08.

---

## Out of scope

- Blocking the app. Every path through this is skippable.
- Checking anything the app cannot observe. A row that asks the user
  to certify something unverifiable, and is then never acted on,
  teaches people to tick without reading — which is the failure mode
  this design exists to avoid.
