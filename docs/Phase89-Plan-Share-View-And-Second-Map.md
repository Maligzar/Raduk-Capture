# Phase 89 / 90 plan — a share view, and a second sky map

Two phases from the same request, written 2026-10-17.

> *"build a view window that will go to display 2 and fill the whole
> window. This way my bank of monitors can be used to display and share
> with others what I am seeing and I can keep the monitor 1 for control.
> so we will want to also add a phase that leaves the map up in display
> 5 and lets me bring up a second map in display 1. this will be a great
> feature for star parties."*

**Phase 89 is the share view, and is built now. Phase 90 is the second
map, and is planned here and queued.**

---

## What is actually being asked for

A star party is a different operating mode from a solo night, and it is
the first requirement in this project that has *two audiences*:

| | The owner | Everyone else |
|---|---|---|
| Looking at | Display 1 | The big screens |
| Wants | Controls, numbers, histogram, the lot | The planet, large, and nothing else |
| Tolerates | Clutter — they put it there | Nothing. They are standing up, in the dark, three metres back |

The existing preview serves the first audience and cannot serve the
second: it is a child window inside the control layout, surrounded by
side panels, a status bar, a reticle, and a HUD. Making it bigger does
not help, because *what* is on it is wrong for a bystander.

So this is not "the preview on another screen". It is a second,
deliberately empty window whose only job is to be looked at.

---

## Phase 89 — the share view

### The one rule

**It must cost the capture nothing.**

Everything else here is a preference. This is not. A star party is
still an imaging session, and if putting the image on the wall drops
frames then the feature has traded the data for the show. The night is
the scarce resource; the audience is not.

Three things follow from that rule:

1. **No second debayer.** The share view is handed the finished RGBA
   the preview just built — after calibration, debayer, FX, stacking
   and RGB alignment — and uploads it to its own bitmap. It does not
   re-derive anything from the sensor frame. This also means it shows
   *exactly* what the owner sees, which is the point: pointing at your
   own screen and saying "that bright spot" has to refer to the same
   bright spot.

2. **Its own frame rate, below the preview's.** Nobody standing three
   metres back can tell 30 fps from 60. The view presents at a capped
   rate and silently skips the frames in between, so the cost is
   bounded no matter how fast the camera runs.

3. **Nothing when it is closed.** The hook is a `std::function` that is
   empty unless the window is up, and the window is hidden by default.
   A feature for a handful of nights a year must be free on all the
   others.

### What is on it

Almost nothing, and that is the design.

- The image, aspect-preserved, centred, scaled to fill the screen.
  Letterboxed, never stretched — a stretched planet is not a planet.
- A caption, bottom-left: the target name, large; then one quiet line
  of **8SE · ASI678MC · 12.0 ms · gain 300 · 48 fps**.
- A red **REC** dot while recording, so nobody asks.
- `C` hides the caption, for when someone wants a photograph of the
  screen.

Not on it: reticle, histogram, temperature, sharpness, ROI box, clip
flash, focus readout, buttons. All of that is diagnostic, and a
diagnostic on a screen pointed at the public is noise at best and a
distraction from the planet at worst.

### Which screen it goes on

**Not display 2 by name.** `\\.\DISPLAY2` is not a thing this rig can
rely on — it renumbered under us twice this month (D1), which is why
placement matches on the device interface path. Hard-coding a display
number would break the first time a cable moved, and it would break
*silently*, by putting the public screen somewhere unexpected.

Instead the view is a **workspace role**, `share`, like every other
window. It opens on whatever screen the profile names, fills it, and
`Ctrl+Shift+End` records wherever the owner drags it. The default for
a five-screen rig puts it on the first spare screen that is not the
map's.

It fills that monitor borderlessly — no caption bar, no sizing border.
A title bar across the top of a shared screen is both ugly and a thing
people try to click.

### Focus

Created `WS_EX_NOACTIVATE`. Opening the share view must never move
focus off the control screen: the owner presses `Shift+V` with one
hand while doing something else, and a keystroke that silently
redirects the *next* keystroke — including the arrow keys, which jog
the mount — is a safety problem, not an annoyance.

### Keys

| Key | Where | Action |
|---|---|---|
| `Shift+V` | Main window | Show / hide the share view |
| `C` | Share view | Caption on / off |
| `F11` | Share view | Fill the screen / restore a normal window |
| `Esc` | Share view | Leave fullscreen |

`Shift+V` because plain `V` is already RGB alignment, and `Shift+<letter>`
is what every other window on this desk already uses (`Shift+H`,
`Shift+Q`, `Shift+T`, `Shift+D`, `Shift+A`).

### What it does not do, deliberately

- **It does not follow the preview's zoom.** `Z` cycles Fit/1×/2×/4×
  for pixel-level inspection; that is an owner's tool. The share view
  always fits the whole frame to the screen, which for a 640×480 ROI
  on a 1920×1080 monitor is already a 2.25× enlargement.
- **It does not auto-centre on the planet.** Worth considering later —
  the centroid is already computed by `FrameAnalyzer` — but a view that
  jumps around as the centroid wobbles is worse than a static one, and
  that needs damping that has not been designed.
- **It is not a second control surface.** No clicks do anything.

---

## Phase 90 — a second sky map

### The request

The map stays maximised on the shared screen, where it is doing a job —
a sky map is the single best thing to have on a wall at a star party,
because it answers "what are we looking at and where is it" without
anyone having to ask. Meanwhile the owner wants a map on the control
screen to actually *drive*.

### Feasibility — checked, not assumed

`SkyMapWindow` is already safe to instantiate twice:

| | |
|---|---|
| Window class | `RegisterClassExW` on an existing class fails with `ERROR_CLASS_ALREADY_EXISTS` and is otherwise a no-op; `CreateWindowExW` still succeeds |
| Per-window state | All in the instance, reached through `GWLP_USERDATA` — no file-scope mutable state |
| Catalog | `m_catalog` is a member, so each map has its own copy. A few hundred entries |
| D2D | Each creates its own factory, target and text formats, already rebuilt on resize |
| Site | `OBS_LAT` / `OBS_LON` are file-scope `const`, read-only |

So this is wiring, not surgery.

### The work

1. A second `SkyMapWindow` instance and a `skymap2` workspace role.
2. `Shift+M` to toggle it (`M` stays the first map).
3. Both instances fed from the same places: `SetMountPosition` in the
   readout loop, and `Tick()`.
4. Both wired to the same GoTo and Info callbacks.
5. `kPlacedWindows` gains the role — which is the fifth list that has
   to be kept in step by hand, and is exactly what D3 is about.

### The thing to be careful about

Two windows that can slew the telescope, one of them on a screen the
public can see, at an event where people are standing near the mount.

The map already requires a click-to-confirm before it slews, which is
the right guard and does not need changing. But the second map should
be opened on the **control** screen as the owner asked, and the shared
one left where it is — and if a future phase ever adds a way to open a
map on an arbitrary screen, the confirm popup is the thing that must
not become optional.

### Size

Small. Half a day, most of it the wiring list.

---

## Future work this opens up, not in either phase

- **Auto-centre and crop the share view on the planet**, using the
  centroid `FrameAnalyzer` already produces. Needs damping first.
- **An "about this object" strip** on the share view — the map and the
  object info panel already hold the text.
- **A second share view**, for a rig with more spare screens than this
  one. Nothing in the design prevents it; nothing asks for it yet.

---

## Outcome — Phase 89 shipped 2026-10-17 (`c48c6de`)

Built as planned. `Shift+V`, workspace role `share`,
`WS_EX_NOACTIVATE`, 30 fps cap, fed the finished RGBA through
`PreviewRenderer::SetPresentSink`. Verified borderless at 1920×1080
with Jupiter, three moons and the caption.

Two things the plan did not anticipate:

**A hole in placement, found by adding a window.** `Place()` returned
silently on a role the saved layout did not contain — so `share`, and
every window added after any layout was saved, opened at whatever
coordinates `Create()` used with nothing anywhere saying why. A
generated profile is now kept alongside the loaded one and consulted
per role. The hand-built arrangement still wins wherever it has an
opinion.

**The caption read "Capture".** `GetFilePrefix()` was the fallback for
an empty GoTo target, and its default is the word *Capture*. The
active profile is the target on this rig, so that is the fallback now.

---

## Phase 91 — the emulator gets a field of view (`4af73e6`)

Not in the original plan. It came out of the request that followed:
the widest view was 1280×960 and the moons were never in frame.

The cap was the emulator's, not the camera's — on real hardware FULL
always gave the whole sensor. But lifting it alone would have changed
nothing, because every size in the synthetic scene was a fraction of
frame height. The emulator had no field of view: widening the ROI drew
the same picture larger. Sizes are angular now, so the frame decides
how much sky is in it, exactly as a sensor does.

**And the measurement contradicted the roadmap.** Driven directly the
renderer costs 63 ms at *every* format from 640×480 to 3840×2160 —
flat, so not per-pixel work at all. It is `sleep_for()` rounding to
the 15.6 ms Windows timer granularity. P4.3 was written on the
assumption that the emulator renders at 62 ms a frame; it does not,
and that entry has been corrected rather than quietly left.

## Still open — P4.4

The full-sensor preview runs at 1.7 fps, against 9.7 at 1920×1080.
About 65 ns per sensor pixel per frame, in the app rather than the
emulator, and therefore true of the real rig too. The capture rate
falls with it, which it should not, since recording came off the
render loop in `37b8c50` — that anomaly is the first thing to chase.

Not fixed here. Four plausible full-frame passes and no profile is
exactly the state that produced the wrong P4.3 diagnosis.

---

## Outcome — Phase 90 shipped 2026-10-17

Built as planned, and the feasibility notes held: `SkyMapWindow` took a
second instance with no changes to the class beyond one leak. The
window class was being registered per window, and `RegisterClassExW`
on an existing class fails harmlessly *after* taking the background
brush it was handed — so a second map would have leaked a GDI brush.
Registration is now once per process.

`Shift+M`. Default placement is the `skymap2` role on the **main
window's screen**, which is the whole point: the first map exists to
be looked at by other people, and the second exists where the owner is
working. No size in the default, so `Place()` centres it at its own
560×580 rather than maximising over the preview.

**Wired through a list, not by repeating lines.** The two maps live in
a `kMaps[]` array and every shared concern — mount, GoTo callback,
Info callback, `SetMountPosition`, `Tick()` — iterates it. The plan
listed five wiring points and noted that `kPlacedWindows` would be the
fifth list in this file that has to be kept in step by hand (D3).
Adding a sixth by hand would have been the wrong answer to that.

Both maps still require the click-to-confirm before they slew. That
guard is what makes a map safe to leave on a screen at an event, and
it does not become optional because there are two of them.

Verified with real keystrokes through the message pump: `M` toggles
the big map, `Shift+M` toggles the second independently, both render
their own D2D target, catalog, site panel and mount crosshair.
