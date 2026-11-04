# Phase 119 plan — window chrome, and what the graphics cards can do for it

> *"Plan a phase to implement the application style look and feel that have
> been mentioned. The title bars should all be of higher polish as should their
> frames. Modern 4K graphics could be taken advantage of considering the video
> cards."*

119.1, 119.2 and the pilot of 119.3 are built: see section 9. The owner chose custom title bars.

## 1. What was already decided

This phase finishes phase 96. It does not restart it, and these still stand:

- **Night vision removes colour,** so the look is built from depth, shape, type,
  spacing and motion.
- **Operator surfaces recede and audience surfaces perform.** Display 1 is an
  instrument. The share view and object panel are the show.
- **No new UI framework.** Win32 and Direct2D stay.
- **No touching the preview's hot path.** Chrome stops at the HUD.
- **The venue is a dark room at 2am,** not a screenshot.
- **A palette fix is done when the painting sites are counted,** not when the
  token changes.

Left open by phase 96 and earlier: **96.4b**, the type and spacing retrofit, and
the deliberately deferred **buttons, list headers and tab strips**, which paint
system grey and make up most of what is still not red in the GoTo window.

## 2. What was measured

**The 4K premise does not hold on this rig today.** The four displays Windows
reports are all 1920x1080, and each panel's top listed mode is 1920x1080. Memory
says five monitors, and the owner confirms four panels and one main monitor, so
one was not reported here. There are no 4K pixels known to fill, so spending the GPU on resolution buys nothing yet. The cards are still
useful, for the things phase 96 had to fake: real blur, real shadows and
flip-model presentation. The work should be resolution-independent, so a later
4K panel simply looks sharper.

**The named fonts are not on this machine.** The source asks for Barlow Condensed
and Space Mono 47 times, and neither is in the system or per-user font folders,
and `assets` has none. Windows has been silently substituting them. Much of the
typography phases 92 and 96 tuned has never been seen. This is the biggest
single visual gain available, and it costs almost nothing.

**Title bars never reached the night ladder.** They are dark in both modes,
through `DwmSetWindowAttribute` 19 and 20, but that is only Windows' dark grey
with white text. In night vision, every caption is a bright white line above a
red window. Windows 11 lets the caption, its text and the border be coloured.
This machine, build 22631, has that.

**The rest of the starting point:**

| Item | Now |
|---|---|
| Files creating captioned windows | 19, by hand, each with its own style flags |
| Files setting the dark title bar | 7 |
| Custom-drawn captions and frames | None |
| `WM_DPICHANGED` handling | One window, the main one |
| Direct2D target | `ID2D1HwndRenderTarget` in 7 files, which has no blur |
| GPU use by the UI | Debayer compute shader only |

Phase 96's shadows are nested flat rings, because the plain render target has no
blur. That was the right call then, and it is the thing the cards can now replace.

## 3. Design

### The frame is one thing, in one place

**`src/gui/WindowChrome.h`:** every captioned window calls it once. It replaces
the 19 hand-rolled sets of flags, and it applies the way colour tokens do, so a
window added later cannot miss it. Its tokens join `Theme.h`, and its geometry is
pure and tested, as `ToolbarLayout.h` is.

### What a title bar looks like

| | Operator | Audience |
|---|---|---|
| Height | One compact row, `Space::` based | The same row, hidden when fullscreen |
| Title | `Type::` label in small caps, in the real font | The same |
| Left of title | A thin rule in the window's section accent | Corner marks, as on the sensor frame |
| Active window | Stronger title, accent rule lit | The same |
| Inactive window | Rule off, title dimmed | The same |
| Buttons | Square glyphs, drawn, with a hover state | The same |
| Border | One pixel from the elevation token, DWM rounded corners | The same |
| Close hover | Danger token, the only loud thing in the chrome | The same |

The state of a window reads from shape and weight, so it survives night vision.
The accent rule is decoration by day, and nothing depends on its hue.

**Real custom title bars, not just DWM colours.** Colours alone would give the
right tone but keep Windows' tall caption and plain glyphs. Custom bars cost
care, because they must keep everything people expect:

- Drag, double-click, resizing on all edges, and Windows' Snap Layouts on hover.
- The system menu and keyboard move.
- Windows dropped on a screen that has gone, which phase 42's reachability check
  relies on.
- The existing `F11` borderless fullscreen.

**No Mica or Acrylic.** They let the wallpaper through, which is a brightness
this app cannot control in a dark room, and the rule for night vision forbids it.

### What the graphics cards are for

| Use | Where | Note |
|---|---|---|
| Real Gaussian shadows | Audience surfaces and floating cards | Replaces the rings, with the same `Theme::Elev` ordering |
| Direct2D 1.1 device context | Share view, object panel first | Gives effects, and better text and gradients |
| Flip-model swap chain | The same | Smoother and lower latency |
| Dithered gradients | Audience backgrounds | Removes banding in dark fades on 8-bit panels, which dark fields show worst |
| 4K-ready drawing | Everything | Sizes from DPI, no fixed pixels |

**The cards have other work.** The capture loop always comes first. The P1000 is
a much weaker card and shares the machine with the RTX 2060, and a window on a
screen driven by the other card can cost a cross-adapter copy. So chrome draws on
change, never continuously, and every stage measures capture frame rate before
and after, with zero new dropped frames as the bar.

## 4. How to tell it worked

Tested, in the style of `tests/visual_tokens.cpp`:

- Every captioned window goes through `WindowChrome`. Greppable: no bare
  `WS_CAPTION` outside it.
- Hit testing returns the right zone at every edge and DPI.
- Title text and caption stay on the night ladder at every state.
- Active and inactive differ by more than the minimum luminance step.
- Dwell and DPI values come from tokens, with no literals.
- Capture frame rate and drops are unchanged, measured as phase B3's was.

The rest is the owner's eye, in the dark, on the real monitors.

## 5. Stages

| Stage | What | Size | Why here |
|---|---|---|---|
| **119.1** | Bundle Barlow Condensed and Space Mono privately, for both GDI and DirectWrite. Both are open-font-licensed, which needs checking against the project's licence notice | S | The biggest gain for the least code, and every later stage is judged in the true type |
| **119.2** | Windows 11 caption, text and border colours through one call, following night vision, with rounded corners | S | Fixes the bright white captions at once. Still worthwhile if 119.3 is dropped |
| **119.3** | `WindowChrome`: custom title bars and frames, migrated window by window, starting with the histogram as the pattern | L | The heart of the request. 19 files, and Snap and DPI must keep working |
| **119.4** | Owner-drawn buttons, list headers and tab strips | M | Finishes what phase 96 deferred, and takes GoTo out of system grey |
| **119.5** | Direct2D 1.1 and flip model on the share view and object panel, with real shadows and dithered gradients | M | Where the cards show. Audience surfaces first, because nothing an operator needs depends on them |
| **119.6** | **Future plan, not scheduled.** 96.4b and per-monitor DPI on every window | M | What makes it 4K-ready. Needs someone watching the screen, as 96.4b always did |

Dependencies: 119.1 first. 119.2 and 119.5 stand alone. 119.3 before 119.4 and
any later 119.6, which would otherwise be redone. New chrome from 119.3 on takes
its sizes from DPI from the start, so it adds nothing for 119.6 to retrofit. **Do 119.1 and 119.2 first:** they are
small, and they change how the whole application looks.

## 6. What it will not do

- No wallpaper showing through, and no transparency on operator windows.
- No continuous animation in the chrome, and no GPU use while idle.
- No change to `PreviewRenderer`.
- No new colour-coded meaning.
- No change to the docked main window's panels, beyond 119.4 and 119.6.

## 7. Risks

- **Custom non-client code is where Win32 bites:** maximise overlapping the
  taskbar, a one-pixel white flash on resize, lost Snap Layouts. That is why it
  migrates one window at a time.
- **19 windows at once would hide a regression,** and most only show on a real
  screen. Each batch needs a look from the owner, by step list.
- **The caution rung** is still an open decision. Chrome adds no new signal, so
  this phase does not force it.

## 8. Decisions

Settled by the owner, 2026-11-01:

- **4K stays last, as a future plan.** 119.6 is not scheduled with the rest.
- **Bundling the fonts is fine,** with their licence notices alongside.
- **The rig is four panels and one main monitor,** so five. Windows reported four
  during this plan's measurements, so the fifth's mode is still unmeasured.

- **Custom title bars.**

## 9. As built

**119.1.** Four font files are embedded as resources, with their licences in
`assets/fonts`. Only regular and bold are carried, since the app uses no other
weight. `src/gui/BundledFonts.h` gives them to GDI and DirectWrite, and all 17
text-format sites go through it. The test found what reading would not:
DirectWrite files the font as family "Barlow" at a condensed width, so asking
for "Barlow Condensed" silently found nothing while GDI worked. `FamilyFor` maps
the name.

**119.2.** `Theme::DarkTitleBar` sets rounded corners and coloured caption, text
and border, and it already ran on every window at each night-vision toggle.

**119.3, pilot only.** `ChromeLayout.h` is the pure geometry and
`WindowChrome.h` the Windows half. The histogram is the only window on it. It
paints inside each window's own Direct2D draw, so no overlay window is needed,
and it has no GDI painter yet.

**Gated:** `tests/window_chrome.cpp`, for scaling, button layout, no dead pixel at
five DPIs, edges, maximised behaviour, and both font routes.

**Not seen by anyone yet.** The look, dragging, resizing, Snap Layouts, night
vision on the bar and fullscreen. Real fonts have different widths from whatever
was substituted, and `ControlPanel.cpp` lays out by fixed pixels, so **text may
clip anywhere in the app.** That is the first thing to check.

**119.4, control panel only.** The owner's screenshot confirmed the clipping:
real Barlow Condensed lost the tails of p, g and y in 13 px rows, so "Exposure"
read "Exoosure". `SH` and `BH` went to 17 and 24 and the panel's text to
`Type::BODY`. Every `MakeButton` is now owner-drawn on the dark panel, with an icon
beside the word from `ButtonIcons.h`. Each glyph was rendered and looked at, and
DARK, MOUNT CTRL and browse keep words alone because no honest icon existed.
No hover state yet: owner-draw has none without subclassing. Not seen on screen.

**119.3, second batch.** `WindowChrome::DrawGdi` is the GDI twin of `Draw`.
Sharpness, Temperature and Status are on the bar, so all four telemetry windows
match. Status moves its viewport origin below the bar, so its rows are unchanged.
Then the rest, at the owner's word.

**119.3, all windows.** `WindowChrome::Install` hooks a GDI window with a subclass,
so its proc is unchanged. Fixed layouts (mount, object info, the four dialogs)
have their children moved below the bar and the window grown to fit. GoTo, Help
and the assistant add `CaptionFor` in their own layout. The sky map, object panel
and share view translate their render target, and the sky map's mouse positions
take the same offset. ADC moves its viewport as Status does.

**The main window keeps the Windows bar,** coloured by 119.2. Everything in it is
placed against its top edge, so a custom bar means moving all of it, for a bar that
already matches in tone.

**Readability pass.** Dialogs set no font and showed the system default; they now
use `Type::STRONG`. Mount window: 210 to 270 wide, text at 16, lamp text at 20 and
13. Object info: 16 and 13. GoTo: smallest text 12 to 15. Image overlays: readouts
13 to 15, bar labels 11 to 12. Help and the assistant already scale with the window.

Not seen on screen. Mount's rate row and the alignment popup are untouched, and
the sky map's confirm popup and tooltips keep their sizes.
