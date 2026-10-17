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
