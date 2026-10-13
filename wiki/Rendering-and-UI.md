# Rendering and UI

Direct2D for everything on the preview side, native Win32 controls for the
panel, and a single theme module that makes the whole interface go red
together.

---

## Window layout

See [Architecture Overview](Architecture-Overview#window-architecture) for
the layout and the Win32 details that are load-bearing. The one fact worth
repeating here:

> **D2D is bound to the preview child HWND, not the main window.** The OS
> clips D2D output to the child's bounds automatically — no
> `PushAxisAlignedClip`, no offset transforms, no gap-fill hacks. The
> preview is a true bounded box, and `y = 0` means "below the toolbar".

[`src/gui/Window.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/Window.h) owns
the `ID2D1HwndRenderTarget` and runs the message pump; `Window::Run(fn)`
takes the per-tick lambda that is the render loop.

---

## Preview renderer

[`src/gui/PreviewRenderer.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/PreviewRenderer.h) ·
[`.cpp`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/PreviewRenderer.cpp)

Owns the debayered RGB buffer and every overlay. It is the largest GUI
translation unit, and it owns the pixel buffers that
[`QuickLookStacker`](Image-Analysis#quick-look-stacker) deliberately does
not:

> `QuickLookStacker` holds **no** pixel buffers — `PreviewRenderer` owns
> those, so the dimension-change teardown in `SetFrameSize` stays the
> single place that has to get reallocation right. That was a heap overflow
> once already (Phase 56, review G1).

### Zoom

```cpp
enum class ZoomMode { Fit, One, Two, Four };   // FIT / 50% / 100% / 200%
```

`Z` cycles; the mouse wheel zooms; click-drag pans when zoomed.

### GPU debayer

[`src/gpu/GPUDebayer.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gpu/GPUDebayer.h)

D3D11 compute shader (`cs_5_0`) converting RAW8/RAW16 RGGB to RGBA on the
GPU. **<1 ms versus ~15 ms for the CPU path.**

```
RAW data            →  ID3D11Texture2D (input, dynamic)
Compute shader      →  ID3D11Texture2D (output, RGBA, shared)
IDXGISurface        →  ID2D1Bitmap (D2D DXGI interop)
                    →  ID2D1HwndRenderTarget::DrawBitmap()
```

**Zero CPU readback.** If D3D11 is unavailable it returns `nullptr` and the
caller falls back to the CPU debayer — same graceful-degradation pattern as
the optional dependencies.

One consequence to know when reading the stacker code: `DebayerRGGB` writes
each 2×2 block as **four identical values**, which is why the stacker's
gradient metric takes differences at ±1 *superpixel*. A 1-px difference is
identically zero.

---

## Overlays

All drawn in the same `BeginDraw`/`EndDraw` as the frame, between
`RenderFrame()` and `StatusBar::Draw()`.

| Overlay | Key | What it does |
|---------|-----|--------------|
| Reticle | `R` | Four shapes, movable/sizeable/rotatable — see below |
| Clipping warning | `C` | Any pixel with R, G, or B = 255 flashes red at ~2.5 Hz; `CLIP x.x%` top-right |
| ADC overlay | auto | Bottom-right box: dispersion in arcsec, colour-coded severity, blue arrow for direction. Shown whenever mount altitude > 0° |
| Quality bar graph | `Q` | Rolling 200-frame sharpness history, bottom-left. Green ≥ 60%, amber ≥ 30%, red below. `PK` peak, `AV` mean |
| RGB align | `V` | Shifts blue up and red down in the display debayer to counter dispersion. Auto value from altitude + Edlen; manual ±20 px fine-tune |
| Pixel readout | `X` | Hover for `R:NNN G:NNN B:NNN` plus sensor coordinates; click to lock, `PIXEL LOCK` in the HUD |
| Histogram strip | panel | 128×48 px, bottom-left. Shadows cyan, highlights amber, saturated red |
| HUD status bar | always | `LIVE 3840×2160 RAW8 38.3 FPS FRAMES: 2151 DROPS: 0` — click the resolution readout for a reset-to-full popup |

Watch for a burst of green bars on the quality graph during a good seeing
cell — that is when to capture.

### Reticle

One control, four shapes, cycling `off → crosshair → circle → rings → box →
off`, so a single press still just switches it on.

| Shape | Use |
|-------|-----|
| Crosshair | Centring before tracking. Spans the frame **with a gap at the middle**, so the target itself is never covered |
| Circle | A single ring, for judging whether a disc sits centred |
| Rings | Three concentric rings for **collimation** — defocus a star to a donut and check the donut is centred |
| Box | Corner **brackets** framing a region, for deciding a crop. Brackets rather than a full rectangle so the edge does not hide the detail just inside it |

All four keep a centre dot — the only part that marks an exact pixel, and
what you nudge the planet onto. The toolbar button names the live shape.

**Move, size, rotate** (Phase 45):

| Gesture | Effect |
|---------|--------|
| `Ctrl` + drag | Move anywhere in the frame |
| `Ctrl` + wheel | Bigger or smaller |
| `Shift` + wheel | Rotate, 5° per notch |

> **Modifiers rather than plain gestures, on purpose.** A plain drag is the
> ROI rubber band and a plain wheel is the preview zoom, both used
> constantly. The reticle takes the modifiers so neither is disturbed.

**Rotation applies only to crosshair and box.** A circle and three
concentric rings look identical rotated, so the app **says so** rather than
accepting the input and appearing to do nothing — the same instinct as
`StartAutoAcquire` returning a reason.

Position is stored as a **fraction of the frame**, so it survives a window
resize or a zoom change. **Cycling the reticle off resets position, size,
and rotation** — which is how you recover from a stray `Ctrl`+drag without
hunting for a reset button.

### Display FX (`B`)

Six display-only modes. **SER data is always raw.**

| Mode | Effect |
|------|--------|
| `NORMAL` | Unmodified debayered output |
| `BOOST ×2` / `BOOST ×4` | Each channel multiplied, clamped at 255 |
| `STACK 3 / 5 / 10 BEST` | Aligned stack of the sharpest recent frames — see [quick-look stacker](Image-Analysis#quick-look-stacker) |

**FX resets to NORMAL automatically when recording starts.** You always
capture raw frames.

Use Boost to find a dim target or check focus in poor seeing.

### Display stretch (`L`)

Two sliders in the IMAGE section, Black and White:

```
shown = (value - black) × 255 / (white - black)
```

**This changes only what you see.** Nothing recorded is affected, which is
the point — it is how a faint target becomes visible without touching gain
or exposure, both of which would change the data.

`L` switches it off and on so you can compare, **which is the only reliable
way to tell whether a stretch is helping or just making the noise bigger.**

Status line states its effect honestly in three forms:

| Reading | Meaning |
|---------|---------|
| `Stretch: 20-180` | Doing something |
| `Stretch: on, 0-255 (no effect)` | Window open wide enough to change nothing |
| `Stretch: off (L)` | Disabled |

Sliders cannot cross; the window is held at least 4 levels wide.

Use the histogram below it to choose the points. **The three rows are the
real red, green and blue channels** — before v1.2.3 all three drew the same
luminance figure, which looked like a colour histogram but could not show a
single channel about to clip. See
[Image Analysis](Image-Analysis#per-channel-histograms).

### Framing shot (`Shift+S`)

One press saves gain and exposure, goes to **gain 570 and a 2-second
exposure for five seconds**, then puts both back exactly as they were. The
preview is live throughout; nothing is captured and recording settings are
untouched.

> A faint target is invisible at planetary settings, so checking whether it
> is even in the frame used to mean winding gain and exposure up by hand
> and remembering what they were.

**It refuses while recording, and says so.** Boosting gain mid-file would
bury unusable frames in the middle of a capture, and you would not find out
until you came to stack it.

It is `Shift+S` rather than `F` because a framing shot is a boosted
snapshot, and `F` has stepped the focuser since long before this existed.

### Snapshot (`S`)

Current debayered frame as a PNG via the WIC encoder, into the captures
directory with the same prefix:
`Jupiter_20260322_213045_snap_0001.png`.

---

## Night vision (`N`)

[`src/gui/Theme.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/Theme.h)

The image is reduced to luminance (Rec.709) and pushed into the red
channel, keeping detail visible while giving dark-adapted eyes far less to
re-adapt from. **Remembered between sessions** (Phase 26 step 2) — its own
spec asked for this and it was never stored, so the one setting whose whole
purpose is protecting dark adaptation had to be switched on again every
time, in the dark, which is exactly when a bright screen does the damage.

**The whole interface goes red, not just the image.** Control panel,
toolbar, status bar, floating windows, dialogs. Measured by capturing the
rendered windows:

| Surface | Blue/green-dominant pixels, day | With night vision |
|---------|-------------------------------|-------------------|
| Control panel | 21% | **0%** |
| Status bar | 77% | **0%** |
| Toolbar | — | ~3% |

> The toolbar's residual 3% is the **anti-aliasing of the button labels**
> rather than any colour the app chooses — removing it would mean changing
> how every font in the application is rendered.

### Accent colours converge, unavoidably

> Cyan, amber and green are distinguishable by hue in day mode and all
> become the **same red** at night, because they have almost the same
> brightness. That is unavoidable in a red-only palette: any transform that
> kept them apart would have to put back the blue and green that night
> vision exists to remove. Section headers still carry their names in text.

### One known gap

A few overlays drawn on top of the preview keep their day colours if you
switch night vision on **during** a session. Start with it on, or press `N`
twice, and they follow. The main surfaces are unaffected either way.

Pair it with the `[FX]` brightness modes rather than raising gain if the
screen is still too bright.

---

## Theme

Elite Raduk design tokens, `0xFFRRGGBB`, alpha always opaque.

| Token | Hex | Role |
|-------|-----|------|
| `BG_VOID` | `#04080F` | Preview background, deepest surface |
| `DEEP` | `#07080E` | Panel background |
| `PANEL` | `#0A1520` | Section headers, raised |
| `BORDER` | `#0E1D2E` | Subtle separators |
| `ACCENT` | `#00E5FF` | Cyan — camera, active |
| `ACCENT2` | `#FFB900` | Amber — focuser, secondary |
| `ACCENT3` | `#4DFF91` | Green — mount, live indicators |
| — | `#FF3B5C` | Red — capture |
| — | `#8CAFBE` | Dim — notes |

Typography: **Space Mono** for data, **Barlow Condensed** for labels.

Section colour assignments: CAMERA cyan, FOCUSER amber, TELESCOPE green,
CAPTURE red, STATUS and HISTOGRAM cyan, NOTES dim. A GDI brush/pen cache is
mode-aware so a night-vision switch does not leak day-mode brushes.

---

## Control panel

[`src/gui/ControlPanel.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/ControlPanel.h) ·
[`.cpp`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/ControlPanel.cpp)

A `WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VSCROLL`
window, 560 px wide, with native Win32 controls and GDI-painted sections.
All sections collapsible; **opens with every section collapsed** (Phase 80).

| Section | Contents |
|---------|----------|
| IMAGE | DARK / FLAT / CLEAR calibration masters, Black/White stretch sliders, ZOOM FIT / FULLSCR, `[FX]` |
| CAMERA | Gain, exposure, gamma, WB red/blue, USB bandwidth (40–100%), high-speed mode |
| FOCUSER | Large amber position, coarse/fine button rows, temperature, ADC readout, **`Filter: name (low–high nm)` directly beneath it** |
| TELESCOPE | Mount name and port, Az/Alt/RA/Dec, cord wrap, CABLES OK, SETUP, READY, acquisition status, MOUNT CTRL, TRK, PLANET LOCK, TGT/GOTO, POINT SOLVE → SYNC |
| CAPTURE | Target profile **and** recording settings — merged in Phase 85 |
| STATUS | HDD bar + FPS, BUF bar + LOST |
| HISTOGRAM | Three R/G/B tinted rows on black, plus seeing and sharpness |
| NOTES | Free text, saved with the profile |
| TEMPERATURE | Chart |

> **Why CAPTURE and PROFILE were merged.** They were two sections holding
> the same values, and they **disagreed**. One source of truth for gain,
> exposure, resolution, bit depth and capture limit, or a profile load
> leaves the panel describing something other than what the camera is
> doing.

Two panel mechanics worth knowing because each was a bug: the profile combo
is repopulated from `m_cachedProfileNames` at the end of every
`RebuildControls` (otherwise it is empty after any collapse/expand), and
layout width reserves `SM_CXVSCROLL` so controls do not draw behind the
scrollbar.

The focuser GoTo field is refreshed only when not focused — a periodic
refresh used to overwrite it mid-keystroke, so you could not type into it.

---

## Toolbar

[`src/gui/ToolbarStrip.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/ToolbarStrip.h)

Three groups: acquisition, view, capture format.

| Button | Key | Function |
|--------|-----|----------|
| `[CENTROID]` | — | Acquire: find, calibrate, centre, lock. Press again to cancel |
| `[LOCK]` | — | Manual override — force the calibration step |
| `[ROI]` | — | Crop the sensor around a locked planet (drag a box on the preview for a freehand ROI) |
| `[C] COLOR` | — | COLOR / RAW debayer. **RAW shows raw greyscale sensor data** — useful for judging focus |
| `[*] MAP` | `M` | Sky map |
| `[I] GOTO` | `G` | GoTo panel |
| `[+] RETICLE` | `R` | Reticle, naming the live shape |
| `[CLIP]` | `C` | Clipping warning |
| `[GH]` | — | Ghost / frame-stack FX |
| `[?]` | `?` | Help window |
| `[AI]` | — | AI chat panel |
| `FULL` | — | **Whole sensor, 3840×2160 — the way back from any crop** |
| `1920` / `1280` / `640` | — | 1920×1080 / 1280×960 (what `[ROI]` crops to) / 640×480 |
| `8b` / `16b` | — | RAW8 / RAW16 |
| `EXIT` | — | Graceful shutdown, confirms if recording |

Buttons bold when active. **The live resolution and bit depth each carry a
dot**, so you can see at a glance what you are recording.

`UpdateStates` caches state so the toolbar only repaints on an actual
change — without it, 38 fps `SetWindowTextW` calls made the buttons flash.

---

## Floating and auxiliary windows

| Window | File | Purpose |
|--------|------|---------|
| Mount controller | `MountController.h` | N/S/E/W jog, rate selector, STOP, ★ ALIGN, RA/Dec, [motion plot](Mount-Control#motion-monitor). Always on top |
| Sky map | `SkyMapWindow.h` | [Polar projection](Sky-GoTo-and-Plate-Solving#sky-map-m) |
| GoTo panel | `GoToPanel.h` | [Catalog and plate solve](Sky-GoTo-and-Plate-Solving#goto-panel-g) |
| Help | `HelpWindow.h` | `?`, floating, **live search filter** over the help file |
| AI chat | `AIChatPanel.h` | [Streaming assistant](AI-Assistant) |
| Object info | `ObjectInfoPanel.h` | Knowledge-base content for the selected target |
| Notification bar | `NotificationBar.h` | See below |
| Readiness | `ReadinessDialog.h` | [Readiness check](Advisors#readiness-check), rows refresh every 2 s |
| Rig setup | `RigSetupDialog.h` | Observer bearing, keep-out wedge, wrap limit, filter picker — drawn as a plan so stored numbers can be checked against the actual garden |
| Site | `SiteDialog.h` | Manual site entry |
| Align helper | `AlignHelperDialog.h` | ["Point north" in terms of your landmarks](Mount-Control#align-helper) |

### Notification bar

A thin `WS_CHILD` strip between the toolbar and the preview. Auto-dismisses
after 8 seconds; click anywhere to dismiss.

| Colour | For |
|--------|-----|
| Green | info — file saved, capture stopped, snapshot saved |
| Amber | warn — buffer overrun, EAF limit, mount lost |
| Red | error — disk full, camera timeout, write failure |

File-save notifications carry a `→ Open folder` suffix that opens Explorer
via `ShellExecute`. `Height()` returns 0 or `NOTIF_HEIGHT` so the main
window can fold it into the layout, and a dismiss callback nudges the
preview back up.

---

## Input routing

Hotkeys are routed **from the message pump** (Phase 60), so they work
whatever window or panel has focus.

> Before v1.1.5 they only reached the mount while the main window was
> focused, so clicking a text field or opening Help **silently killed
> them** until you clicked the preview again.

Both `Esc` and `Space` stop the mount, unconditionally.

> Until v1.1.8 `Space` was documented but **bound to nothing**, and `Esc`
> exited fullscreen instead of stopping — so there was no stop key at all
> while fullscreen. `F11` now exits fullscreen; `Esc` always stops the
> mount.

Arrow-key jog suppresses auto-repeat and stops on application
deactivation — an Alt+Tab used to leave the mount slewing.

Mouse reaches the preview (Phase 61) for drag-box ROI and the pixel
readout, which it previously did not.

Full key map: [Operations Guide](Operations-Guide#keyboard).

---

**Next:** [Operations Guide](Operations-Guide) · [Telemetry and Dashboard](Telemetry-and-Dashboard) · [Image Analysis](Image-Analysis)
