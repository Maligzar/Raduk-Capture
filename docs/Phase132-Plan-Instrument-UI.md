# Phase 132 — from sci-fi template to precision instrument

Asked for by the owner on 2026-11-07: take the UI from "looks generic,
template-built AI" to a custom, polished look across every surface. That
includes the HTML pages the app opens in Edge, which the app should write
and polish itself. **Planned, not started.** Nothing below is built. The
owner asked that no stage begin until it is called for. **Prioritized by the
owner on 2026-11-07** as the next bench work, ahead of everything but the
clear-night items. 132.0 comes first. Each later stage still waits for the
owner to look at the one before it.

**How this relates to phase 96 and 119.** It finishes their open items rather
than restarting them. 132.3 *is* 96.4b, the type and spacing retrofit that
needs eyes on a screen, done in the chosen direction. 132.3e takes in 119.5,
the real Direct2D shadows. 132.3 also settles the open owner decision from
96.4 (`ACCENT3` outranks `ACCENT2` at night), because the accents are reduced.

## Context

The owner wants Raduk Capture to stop looking like a generic, template-built AI UI
and to have a custom, polished look across every surface. That includes the HTML
pages it opens in Edge. Three constraints shape the plan:

1. **Nothing breaks.** It is a capture instrument. A visual change must never touch
   capture, tracking, or the preview hot path.
2. **Pause at any time.** Clear sky is unpredictable. Every stage leaves `master`
   field-ready, so the owner can stop after any stage and go observing.
3. **The app does the work.** The HTML pages should be written by the app from its
   own tokens and fonts, not hand-kept static files.

**Chosen direction: precision instrument** (owner's pick). Warm graphite neutrals,
matte surfaces with engraved hairlines, one muted signal accent, small-caps tracked
labels, and tabular mono readouts. State is carried by shape and text first, colour
second. That also matches the night-vision rule already in place: hue carries no
meaning at night.

**What makes it look generic today (measured):**
- Neon cyan `#00E5FF` plus a four-accent palette (cyan, amber, green, red) in `Theme.h`.
- Glows, a starfield, a scanline animation, and a loading screen in
  `RadukCaptureDashboard.html` (5,658 lines, static, never written by the app).
- Session stats (`src/core/SessionStats.h`, written to `data\session_stats.html`)
  keeps its own CSS palette, which has already drifted from `Theme.h`.
- Neither HTML page carries the bundled fonts. The dashboard names Barlow Condensed
  and Space Mono, which are not installed, so Edge silently substitutes them, as
  the app itself did before 119.1.
- Owner-drawn buttons have no hover or press state (119.4). Motion has only `EaseIn`.

**Already in place, which this plan builds on rather than replaces:** the `Theme.h`
tokens (`Type::`, `Space::`, `Elev`/`ShadowFor`, `Motion::`, the night ladder, and
`Theme::Rgb`), `BundledFonts.h`, `WindowChrome.h`, `ButtonIcons.h`, the scenario
runner (`--scenario`, `tests/run-scenarios.ps1`), and `WindowShot.h`, which saves
every window to PNG.

## Safety rails, for every stage

- **One stage is one PR,** merged only after the owner has looked at it. `master` is
  field-ready between stages. Each merge is tagged `ui-132.N`, so the previous build
  is always one checkout away if something looks wrong at the telescope.
- **Out of bounds:** `PreviewRenderer` drawing, `src/core` capture, tracking, mount,
  and focuser logic, and the WebSocket protocol, except for one additive field in
  132.10.
- **Gates per stage:** bench tests (run here with mingw-w64 and wine as evidence, and
  with MSVC `tests\run-tests.bat` on the rig as the real gate), the full scenario
  suite, and the visual baseline diff from 132.0. Only the windows the stage names
  may change.
- **New behaviour gets a `rig.ini` switch** (motion, palette, audio), so it can be
  turned off in the field without a rebuild. Colours are not switched; they revert
  through the tag.
- **Night first.** Every visual stage is checked in night vision before day mode.

## Stages

Each block ends at a natural pause point. The ★ stages change what the owner sees.

| Stage | What | Risk | Size |
|---|---|---|---|
| 132.0 | Visual baseline harness: shots of every window, day and night, plus a diff | None, no pixel changes | M |
| 132.1 | Night vision without green | Low | S |
| 132.2 | Design language and token export (C++ to CSS), with a specimen page | None, no app pixels change | M |
| ★132.3 | Instrument palette and type in `Theme.h`, migrated surface by surface | Medium | L, split |
| 132.4 | Motion: cubic ease-out for arrivals | Low | S |
| ★132.5 | Hover and press on owner-drawn buttons | Low | M |
| 132.6 | Focus rings and dark scrollbars | Low | M |
| ★132.7 | Command palette (Ctrl+K) | Low, additive | L |
| 132.8 | Optional audio cues (off by default) | None when off | S |
| ★132.9 | Session stats page rebuilt on shared tokens and embedded fonts | Low | M |
| ★132.10 | Dashboard written by the app, restyled, opened as an Edge app window | Medium | L, split |

### 132.0 Visual baseline harness
- New scenario `tests/scenarios/ui-baseline.scn`. It opens every window (GoTo, sky
  map, histogram, sharpness, temperature, status, ADC, help, assistant, dialogs, and
  the object panel) and takes a `shot` of each in day mode, then again in night vision.
  It uses the existing `shot` step from `WindowShot.h`.
- New pure tool `src/core/ShotDiff.h`, with a thin `tools/shotdiff` CLI. It compares
  two PNG folders by title, reports the share of changed pixels per window, and writes
  a contact sheet `data\ui_diff.html` showing before, after, and a heatmap.
- Gate: `tests/shot_diff.cpp` on synthetic images.
- Result: every later stage arrives with a diff showing exactly which windows changed.

### 132.1 Night vision without green
- `Theme::NV`/`NVf` (`Theme.h:619,636`) emit `RGB(lum, lum/10, 0)`. The 10% green sits
  near the rods' peak sensitivity. Inferred: it probably carries most of the
  rod-visible light the UI emits. Its stated purpose is cosmetic: stopping the
  darkest greys from rendering as pure black.
- Make it a token, `kNightGreenFraction`, default 0. Lift the floor of the bottom
  surface steps in red instead. Route the preview reticle brush
  (`PreviewRenderer.cpp:41`) through `NVf`. That is a brush colour at creation, not
  the frame path.
- Gate: `tests/operator_palette.cpp` asserts G=B=0 in night mode. Owner check: GoTo
  and the control panel stay readable on a dark night.

### 132.2 Design language and token export
- `docs/design/Instrument-Design-Language.md`: one page. It covers the palette roles
  (surface steps, hairline, label, readout, signal accent, warning, fault), the rule
  that a signal shows as shape plus text first, the label style (small caps,
  tracked), the readout style (tabular Space Mono), hairlines, and the motion rules.
- New pure `src/core/WebTheme.h`, which generates CSS from `Theme.h`:
  - `:root` custom properties for day and night,
  - `@font-face` rules with the four bundled TTFs as base64 (OFL notices included),
  - shared base CSS for tables, tiles, headings, and focus.
  
  This is how the HTML pages stay in step with the app automatically.
- `tools/specimen`, or a scenario step, writes `data\specimen.html`, which shows every
  token, the type ramp, signals, a sample panel, and a sample table in day and night.
  **The owner picks the exact graphite steps and the accent here, before any app
  surface changes.** No colours are hard-coded until then.
- Gate: `tests/web_theme.cpp` checks that every CSS value equals its `Theme.h` token,
  byte for byte.

### 132.3 Instrument palette and type (split by surface)
- Swap the token values in `Theme.h` (`ACCENT`, the neutrals, `TEXT`, `TEXT_DIM`, and
  `Rgb::` in step) to the approved specimen. Reduce the four accents to one signal
  accent plus warning and fault. Amber and green stop being decorative and keep only
  a state meaning.
- Take out glow and decorative effects, add engraved hairlines (1 px shadow line plus
  1 px highlight), and use small-caps tracked section labels.
- **Night ladder luminances stay put** (`kNightLadder` keys by RGB, so each new day
  value is mapped to its existing night luminance). Night vision keeps the same
  brightness for every role; only 132.1 changes its colour. The ladder test
  proves the luminances.
- Sub-stages, each its own PR: 132.3a tokens and control panel; 132.3b GoTo and sky
  map (45 `ColorF` sites in `SkyMapWindow.h`); 132.3c telemetry windows; 132.3d
  dialogs, help, and assistant; 132.3e audience surfaces (object panel, share view)
  and 119.5's real Direct2D shadows.
- Literal colours still outside the tokens get moved onto them as each surface is
  touched, the way 96.4 did it.

### 132.4 Motion
- `Theme::Motion::EaseOut(t) = 1-(1-t)^3` and `SlideIn`, with progress from
  `steady_clock` elapsed time. The timer is killed once motion settles, so nothing
  ticks while idle. Arrivals only; no overshoot.
- Applied to `ConfirmToast.h` and `NotificationBar.h`. `rig.ini` `ui_motion=0`
  makes every transition instant.
- Gate: `tests/visual_tokens.cpp` checks monotonicity, endpoints, and 0 when
  disabled.

### 132.5 Hover and press
- Pure state machine `src/gui/ButtonState.h` (idle, hover, pressed, disabled, mapped
  to a ladder step). Subclass the `MakeButton` buttons in `ControlPanel.cpp`, reusing
  the `TrackMouseEvent` pattern from `WindowChrome.h`.
- Hover is one luminance step up plus an engraved top highlight. Press is a 1 px
  content offset and one step down, over `Motion::FAST`. All of it goes through
  `Theme::Rgb`, so night vision stays correct.
- Gate: `tests/button_state.cpp`.

### 132.6 Focus rings and scrollbars
- A 1 px inner focus ring at a low ladder step, shown only for keyboard focus
  (`UISF_HIDEFOCUS`). It replaces `DrawFocusRect` in `ControlPanel.cpp`.
- Stock controls get `SetWindowTheme(L"DarkMode_Explorer")`. Custom-drawn lists (GoTo,
  help, assistant) get a thin owner-painted scrollbar that hides when idle.

### 132.7 Command palette
- `src/gui/ActionRegistry.h` (id, label, keywords, shortcut, enabled predicate,
  invoke). It is filled from the existing handlers in `Window.h` and the control panel
  callbacks. An accelerator table in `main.cpp` provides Ctrl+K and the most-used
  shortcuts, so the shortcut badges are true.
- `src/gui/CommandPalette.h`: a Direct2D popup at `Elev::MODAL`, with a fuzzy filter
  and keyboard only. Badges are tabular mono. Actions that move the rig or start a
  recording need Enter on the top match plus the `ConfirmToast` confirmation. GoTo
  targets come second ("Go to Jupiter").
- It runs on the UI thread, and invoking an action posts to the existing handler
  without taking any lock that capture uses.
- Gates: `tests/command_palette.cpp` covers ranking and filters 1,000 entries in under
  1 ms. A scenario types in the palette during an emulator recording and asserts
  `droppedFrames` stays at 0.

### 132.8 Audio cues
- Record start, record stop, and fault only. `src/gui/ToneSynth.h` synthesizes soft
  sine tones (about 500–900 Hz, 15–20 ms raised-cosine edges, low level) into static
  WAV buffers once. They play through `PlaySound(SND_MEMORY|SND_ASYNC)` on the UI
  thread. `rig.ini` `audio_cues=0` is the default.
- Gate: `tests/tone_synth.cpp` checks the header, the peak level, and click-free edges.

### 132.9 Session stats page
- `SessionStats.h` HTML output drops its private CSS for `WebTheme.h`, with fonts
  embedded and tables in the instrument style (tabular numerals, hairline rules,
  small-caps headers).
- The page opens in night or day mode to match the app at the moment it is written.
  There is a print stylesheet. No network requests at all.
- Gate: the existing session stats tests, plus a check that the page has no `http`
  references.

### 132.10 Dashboard written by the app (split)
- **132.10a Delivery.** The dashboard HTML becomes a resource embedded in the exe.
  On [DASH] the app writes `data\dashboard.html`, injecting `WebTheme` CSS, the fonts,
  and the current night state, then opens it. If `msedge.exe` is found it opens with
  `--app=`, a chromeless window that reads as part of the app and has no address bar
  showing the token. Otherwise it falls back to today's default-browser path
  (`main.cpp:5214`). `DashboardLaunch.h` keeps the tested URL and token-fragment logic.
- **132.10b Shell restyle.** Remove the scanline, starfield, glow `text-shadow`, and
  the fake loading screen. Header, layout, and tokens move to the instrument language.
- **132.10c Panels restyle,** in batches by section of the 5,658-line file. Each batch
  is a PR with before and after screenshots taken by Playwright (runnable in this
  container) against a recorded telemetry fixture.
- **132.10d Live night mode.** Add one additive `nightVision` field to the telemetry
  message (`src/net/WsProtocol.h`), and the dashboard follows the app's toggle.
  Older dashboards ignore the field.

## Not covered, and still open elsewhere

Found by the review on 2026-11-07. These are open UI items this plan does not
take in. They are listed so they are not taken to be covered. Whether any of
them joins a stage is the owner's call.

- **Per-monitor DPI (119.6).** It was always paired with 96.4b, and 119 says it
  depends on that retrofit. 132.3 does the retrofit and leaves DPI out, so it is
  the natural follow-on to 132.3.
- **List headers and tab strips (rest of 119.4, roadmap D2).** These are most of
  why GoTo is still 39% non-red at night. 132.6's `DarkMode_Explorer` may help
  the stock list headers, but nobody has checked that against night vision.
- **Phase 120 leftovers.** `TGT` and CAPTURE's `Target` are still two boxes,
  temperature shows in three places, and the view strip does not hide in
  fullscreen. These fit 132.3a, which touches the control panel anyway.
- **B1 tail.** In the object panel, a derived line long enough to wrap loses its
  second row. This fits 132.3e.

## Not doing
Glass, blur, translucency, and cursor spotlights on operator windows (banned by 119
and harmful at night). A screenshot-tool region HUD or capture shelf. Any change to
the preview frame path. New colour-coded meaning.

## Verification
- Here: the mingw-w64 and wine bench tests named in each stage, and Playwright shots
  of the generated HTML pages (132.2, 132.9, 132.10).
- On the rig: `tests\run-tests.bat`, `tests\run-scenarios.ps1`, and the `ui-baseline`
  shot diff. Only the windows the stage names may show changes. Then the owner looks,
  day and night.
- Each stage ships with an "as built" section in `docs/Phase132-Plan-Instrument-UI.md`
  and an update to `PHASE-STATUS.md`.
- One draft PR per stage, each on its own branch cut from `master`. The plan
  merged on its own in #88, so the first stage PR is 132.0 alone.

## As built

### 132.0, 2026-11-07

No pixel of the application changes. Nothing under `src/gui`, `src/main.cpp`
or `CMakeLists.txt` was touched, so the shipped build is byte for byte what it
was.

- **`src/core/ShotDiff.h`**, pure C++ with no Windows headers. It parses shot
  file names, pairs windows by shot and title, compares pixels, draws the heatmap,
  reads and writes the noise file, judges each window, and writes the contact sheet.
- **`tools/shotdiff.cpp`**, the Windows half. WIC loads and saves the PNGs, and
  `FileUrlFromPath` from `DashboardLaunch.h` builds the image links. It is
  compiled by `tests\ui-baseline.ps1` with `cl`, not by CMake, so the
  application's build is unchanged.
- **`tests/scenarios/ui-baseline.scn`** goes to Jupiter, shows GoTo, Help and the
  assistant, takes a shot named `day`, toggles night vision with `N`, takes one
  named `night`, and toggles it back.
- **`tests/ui-baseline.ps1`** has two modes. `-Save` runs the scenario twice on
  the build before a change. The first run is the reference, and the difference
  between the two runs is each window's noise floor. Without `-Save` it runs once,
  compares against the reference, and exits with the number of unexpected windows.
  `-Expect "Histogram,Sharpness"` names the windows a stage means to change, and
  `-Open` shows the contact sheet.

**Two things the plan did not foresee.**

- **Two runs of one build differ.** Clocks, the sky map, graphs and the emulated
  seeing all move between runs, so "any pixel differs" would flag every live
  window every time. A window now counts as changed only when its differing share
  is over twice its own noise floor plus 0.1%, or when its size changed. A channel
  has to move by more than 8 levels to count as different, so dithering and
  ClearType reflow do not.
- **The trailing number in a shot's file name is z-order.** It moves between runs,
  so windows are paired by shot and title. Two windows with the same title are
  separated by the order of that number, and the size is checked as well.

**Gated.** `tests/shot_diff.cpp` has 44 checks: file names, pairing that ignores
z-order, the tolerance edge, alpha being ignored, size changes, the noise floor in
both directions, the expect list, the heatmap, report ordering and escaping, and a
page that makes no network request. It passes under g++ on Linux. MSVC is still
owed.

**Run here, end to end.** The CLI was cross-built with mingw-w64 and run under
wine on synthetic shots with shuffled z-order. It reported:

| Window | Verdict | Changed | Noise |
|---|---|---|---|
| day/Gone | MISSING | 100% | 0.000% |
| day/New | NEW | 100% | 0.000% |
| day/Status | CHANGED | 9.17% | 0.000% |
| day/Histogram | changed, expected | 17% | 0.000% |
| day/Sky_Map | same | 0.99% | 0.99% |
| night/Histogram | same | 0.000% | 0.000% |

It exited 3, one for each unexpected window. A Playwright render of the contact
sheet confirmed the layout.

**Not covered.** Modal dialogs (rig setup, site, readiness) are not opened,
because a modal would stop the scenario. The object panel needs Shift+I, which the
scenario's `key` step cannot send. Both can be added later, if a stage changes
those surfaces, by giving the scenario runner a show step. That would mean
touching `main.cpp`, which this stage avoided.

**Owed on the rig.**

1. `tests\run-tests.bat`, where `shot_diff` is the new suite.
2. `.\tests\ui-baseline.ps1 -Save` on master. This confirms the scenario passes,
   that Help and the assistant really start hidden in a fresh sandbox, and that
   both runs show the same windows.
3. The same command again without `-Save`. On an unchanged build it should report
   nothing unexpected, which is the check that the noise floor is wide enough.
