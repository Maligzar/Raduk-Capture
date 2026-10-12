# Phase 42 plan — five-monitor workspace

Place every top-level window on a chosen screen, remember where each
one went, and survive the monitor set changing underneath. Supersedes
the original "Two Monitor Mode", which mirrored SharpCap's feature and
assumed a capture PC with one spare screen.

The rig has five working screens as of 2026-10-12 (RTX 2060 + Quadro
P1000 on a single unified driver). Two-screen framing is now the
limiting factor, not the hardware.

---

## This is smaller than the old estimate

Phase 42 was logged as **Hard**. That estimate assumed the preview had
to be torn out of the main window and given a new home. It does not.

The app already runs five independent top-level windows, each with its
own `CreateWindowExW` and message loop participation:

| Window | File |
|---|---|
| Main (preview + toolbar + control panel + status) | `gui/Window.cpp` |
| Sky map | `gui/SkyMapWindow.h` |
| GoTo + session planner | `gui/GoToPanel.h` |
| AI chat | `gui/AIChatPanel.h` |
| Help | `gui/HelpWindow.h` |

Nothing needs to be split apart. The windows that would occupy five
screens are the windows that already exist. What is missing is only
the ability to *say where they go* and have that stick.

Re-estimated **Medium**.

---

## What is actually missing

A grep for monitor awareness across `src/` returns nothing: no
`EnumDisplayMonitors`, no `GetMonitorInfo`, no `MonitorFromWindow`, no
`WINDOWPLACEMENT`, no `WM_DISPLAYCHANGE` handler.

Every window today opens either at a hardcoded origin or relative to
its parent (`GoToPanel.h:121` reads the parent rect and offsets from
it; `ObjectInfoPanel.h:108` does the same). On a five-screen desktop
they all pile onto whichever screen the main window happens to be on.

So the work is three things, in order:

1. Enumerate the monitors and give each a **stable identity**.
2. Persist a per-window placement keyed by that identity.
3. Re-validate placements whenever the topology changes.

---

## The trap this has to avoid

**Saved window coordinates are a loaded gun on a multi-GPU rig.**

Write `x=3840, y=0` into a config file, then lose a monitor — a cable,
a driver reinstall, a DDU run — and the window restores to coordinates
that are no longer on any physical screen. It is not minimised and not
closed. It is invisible, focusable, and accepting keystrokes. The user
sees a feature that stopped working, with nothing to click.

This rig is unusually exposed to it. The five screens are served by
**two GPUs pinned to a frozen driver branch**: the P1000 is Pascal and
the 2060 is Turing, and only a unified pre-R610 package covers both.
Any driver event that disturbs that arrangement reorders or drops
monitors. The monitor set here is less stable than on a single-GPU
machine, so the restore path has to be defensive rather than trusting.

Three rules:

**1. Identity by device name, never by index.** `EnumDisplayMonitors`
order is not contractual and shifts when adapters re-enumerate. Store
`DISPLAY_DEVICE.DeviceID` plus the monitor rect; match on the device
string first and fall back to nearest-rect.

**2. Every restore is clamped.** Intersect each saved rect against the
current virtual desktop before applying it. Any window with less than
roughly a titlebar's worth of visible area gets dropped onto the
primary monitor instead. A window that cannot be seen has no right to
its saved position.

**3. A rescue path that needs no mouse.** One keystroke that gathers
every top-level window onto the primary screen at default size. This
is the escape hatch for the case the first two rules miss, and it must
not depend on clicking something that may be off-screen.

---

## Keep the live preview on the 2060

This is the one hard constraint the hardware imposes.

`PreviewRenderer` draws through Direct2D. A render target created on
the 2060 and presented to a monitor attached to the P1000 forces a
cross-adapter copy through system memory every frame. On a planetary
capture at speed this shows up as a frame rate ceiling and jitter that
looks exactly like a camera or USB problem — and would be debugged as
one.

So:

- The **live preview** window belongs on an output physically wired to
  the **2060**.
- The mostly-static windows — sky map, planner, chat, help — are cheap
  on P1000 outputs. They redraw on interaction, not per frame.

The layout chooser should know which adapter drives each monitor
(`DISPLAY_DEVICE.DeviceString` via `EnumDisplayDevices`) and warn, not
block, when the preview is placed on a non-2060 output. Blocking would
be wrong: a user rearranging cables at 2am knows more than the
heuristic does.

---

## Proposed default layout

Roles, not fixed screens — the user assigns these, and this is only
what a fresh install should guess.

| Screen | Contents | Why |
|---|---|---|
| 1 (2060, primary) | Live preview, full-bleed | Frame rate critical; no cross-adapter copy |
| 2 (2060) | Control panel, toolbar, histogram, status | Adjacent to preview; constant glances between the two |
| 3 | Sky map + GoTo / session planner | Consulted between targets, not during capture |
| 4 | Motion monitor, tracking telemetry, session log | Watched passively; wants to be big and ignorable |
| 5 | AI chat, help, advisory output | Reference material; lowest attention |

The split assumes 2+3 across the two cards. Both cards cap at four
displays, so 4+1 and 3+2 are the other legal arrangements — the code
must not assume any particular division.

---

## Persistence

Reuse the mechanism already in the codebase. `RigConfig` persists
`rig.ini` next to the executable via `GetPrivateProfileString` /
`WritePrivateProfileString`, the same Win32 private-profile calls
`ProfileManager` uses for `profiles/*.ini`. No new dependency and no
new file format.

A `layout.ini` keyed per window:

```ini
[preview]
monitor=\\.\DISPLAY1
adapter=NVIDIA GeForce RTX 2060
x=0
y=0
w=1920
h=1080
maximized=1
```

`adapter` is advisory — recorded so the preview warning above can fire,
never used for matching.

Writes happen on clean shutdown and on `WM_DISPLAYCHANGE`, not on every
`WM_MOVE`. A crash losing the last window nudge is acceptable; a config
file written thousands of times a session is not.

---

## Topology changes at runtime

Handle `WM_DISPLAYCHANGE` in the main window:

1. Re-enumerate monitors and rebuild the identity table.
2. Re-apply saved placements for monitors that came back, by identity.
3. Clamp and rescue anything whose monitor is gone.
4. Persist the new arrangement.

Worth noting that this fires during exactly the moments the rig is most
fragile — driver installs, resolution changes, a monitor waking from
sleep. The handler should be dull and defensive, and it should log what
it did to `SessionLog` so a 3am misbehaviour is diagnosable after the
fact.

---

## Dependency: night vision across five screens

Phase 26 night vision is marked done for the whole interface, with a
known defect recorded in the phase table: **48 cached D2D brushes in
`PreviewRenderer` and `SkyMapWindow` only follow the mode from launch.**

At two screens that is a minor wart. At five it is a red-light session
with two bright white panels in peripheral vision, which defeats the
purpose of the feature. The brush cache needs to respond to a mode
change before this phase can be called finished.

Either fix the cache invalidation as part of Phase 42, or accept that
night vision and the five-screen workspace are mutually exclusive until
it is fixed. The second option should be written down rather than
discovered.

---

## Per-monitor DPI

Five panels of mixed vintage will not share a scale factor. Without
per-monitor DPI awareness v2, a window dragged between screens either
renders blurry or at the wrong size.

Declare PMv2 in the manifest and handle `WM_DPICHANGED` by scaling the
suggested rect. This is worth doing *with* the layout work rather than
after — the two interact, and retrofitting DPI onto saved pixel
coordinates means invalidating every stored layout.

---

## Non-goals

- **Network / remote screens.** The original Phase 42 note mentioned
  sending preview to the streaming PC over a network share. Out of
  scope; that is a streaming problem, not a window-placement problem.
- **Spanning one window across screens.** Bezels make a spanned
  preview worse than a single-screen one. Each window lives on one
  monitor.
- **Automatic layout detection.** No guessing what the user wants from
  monitor geometry. Defaults above, then the user assigns.

---

## Acceptance

- Each of the five top-level windows can be assigned to a named screen.
- Assignments survive a restart.
- Unplugging a monitor leaves no window stranded off-screen.
- The rescue keystroke recovers every window from any state.
- The preview warns, once, when placed off the 2060.
- Night vision is uniform across all five screens, or the limitation is
  documented.
