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

### 132.1, 2026-11-07

Night vision now emits red and nothing else. `Theme::kNightGreenFraction` is
0, and all three paths read it: `NV` (GDI), `NVf` (Direct2D) and the preview's
night-vision pixel loop.

**The plan's premise was half wrong.** The green's stated purpose was to keep
near-blacks from reading as pure black, and the plan was to lift the bottom
surface steps in red to replace it. Measured, it never did that job. Integer
`lum / 10` gave the four surface steps (8, 14, 24, 34) a green of 0, 1, 2
and 3. So there was nothing to replace, and **no ladder luminance changed**.
The green that reached the eye was on the bright signals: 24 on danger, 20 on
primary text, and 25 on every white in the preview image.

**Wider than the plan in two places, both on purpose.**

- **The preview image.** The plan kept `PreviewRenderer` off limits. But the
  image is the largest bright area on the screen at night, and it carried the
  same tenth of green. Leaving it would have fixed the chrome and kept most of
  the light. The change is one constant in the existing night-vision branch,
  now read from the token. It is a `constexpr` zero, so the per-pixel cost is
  unchanged. Day mode does not run that branch.
- **The reticle** was `ColorF(1, 0.1, 0.1)` in both modes and never went
  through `NVf`. At night it is now pure red at full strength. Sending it
  through `NVf` would have dimmed it to about 0.29, and the eye lines the
  planet up against it. Day mode is unchanged.

Comments that quoted `(lum, lum/10, 0)` (`Theme::Raw`, `HighlightOver`,
`MountController.h`) now say `(lum, 0, 0)`. `Raw`'s double-tint warning is
corrected from 0.284 to 0.2126 of the brightness.

**Gated.** `tests/night_hierarchy.cpp` section 6b checks that the token ships
at zero, that every ladder token is `(lum, 0, 0)` at its designed luminance in
GDI and has no green or blue in Direct2D, that a full-weight highlight cannot
put green back, that danger is `(240, 0, 0)`, and that an untokened white is
`(255, 0, 0)`. Section 6's "a tenth in green" check is now "no green".
`night_hierarchy`, `operator_palette`, `visual_tokens`, `audience_layout`
and `motion` pass under mingw-w64 and wine. `window_chrome` fails there exactly
as it does on unchanged master: wine cannot load the embedded fonts. CI runs it
under MSVC.

**Expected in the 132.0 diff:** every `night/` shot changes, slightly, on its
brightest elements. No `day/` shot changes.

**Run on the rig, 2026-11-07.** MSVC `run-tests`: all 69 pass. The 132.0
diff against a reference saved from `b462ccc` minutes earlier, with
`-Expect "night/"`: every night window changed, no day window did. That run
also exposed a 132.0 bug: the main window's title carries the build stamp, so
every rebuild reported it MISSING and NEW and never compared it.
`ShotDiff::StripBuildStamp` now cuts the stamp from the key (gated in
`tests/shot_diff.cpp` section 2), and the main window then reads `same` in day.
A later rerun against the same, by then half-hour-old reference flagged GoTo,
Status, MOUNT CONTROL and the second sky map in day. The heatmaps show sky
data that moved with the real clock (the object list, alt, airmass, seeing),
not this change. `ui-baseline.ps1` now says to compare soon after `-Save`.

**Owed.** A look at the eyepiece on a dark night. Danger and primary text lose
a little warmth, which is the whole change. The preview's planet loses the
orange cast the green gave it and reads as pure red.

### 132.2, 2026-11-07

No application pixel changes. The app does not yet read any of this. 132.3 and
132.9 are the first stages that use it.

- **`docs/design/Instrument-Design-Language.md`**, the one-page rule book. It
  covers the ten colour roles and what each is not for, the one-signal-colour
  rule, state shown as shape and text before colour (circle for fault, triangle
  for warning, square for live, diamond for signal), the type rules, the surface
  rules, motion, and night.
- **`src/core/WebTheme.h`**, pure. A `Palette` of ten roles. `PaletteCss` writes
  day values on `:root` and the night ladder in red only under
  `:root[data-mode="night"]`. `FontFaceCss` embeds a font as base64.
  `BaseCss` holds the shared instrument styles, with `.sig` signals that differ
  in shape. There is also a WCAG contrast check with floors: 7:1 for Text and
  4.5:1 for the other foreground roles, against Deep.
- **`src/gui/WebPalette.h`**, the only bridge from `Theme.h` to the web side.
  It is separate so that a candidate palette can be rendered before it is a token.
- **`tools/specimen.cpp`** and **`tests/specimen.ps1`** write
  `data\specimen.html`. It shows today's palette and three candidates as sample
  panels in day and night, with swatches and contrast, all four fonts embedded,
  and no network requests. The candidates are **A. Brass**, **B. Verdigris** and
  **C. Steel**. All three are the same warm graphite family and differ only in
  the signal colour. Every one clears the floors: text about 14:1, and Fault
  is the lowest at 4.7 to 4.8:1.

**Found by drawing it.** Today's night ladder puts Signal (140) below Text
(200). In night vision a primary button labelled in the signal colour is
therefore dimmer than an ordinary one beside it. You can see it on the
specimen: RECORD is dimmer than AUTOFOCUS. This is the ladder as 96.1 designed
it, not something this stage introduced, but the instrument rule that state
shows as shape first is what has to carry a primary control at night. 132.3
gives the primary button a heavier outline rather than moving the ladder.

**Gated.** `tests/web_theme.cpp`, 30 checks:
- the colour arithmetic, including the 4.54:1 edge of `#767676` on white;
- all ten roles equal their `Theme.h` tokens in both spellings;
- every role's night luminance equals Theme's ladder;
- a page's night colours are exactly what `Theme::NV` paints;
- every role is declared for both day and night, and night is red only;
- fault and warning differ in shape;
- there is no URL and no `@import`;
- base64 padding;
- an empty font declares no face;
- the contrast floor catches a label nearly the colour of its panel.

It passes under mingw-w64 and wine. The specimen was generated the same way,
then checked in a Playwright render: 1280 px wide, and 390 px with no
horizontal scroll.

**Owed: the owner picks A, B, C, or asks for changes.** 132.3 starts from that
choice.

### 132.3a, 2026-11-07: the Steel palette, and the control panel

The owner picked **C. Steel** from the specimen.

**Tokens.** All ten `Theme.h` values now hold Steel. The old values are kept in a
comment beside them. `Rgb::` and the night ladder are now **built from the
tokens** rather than spelling the same bytes a second time, so any later palette
change is a single edit. The night luminances were kept, so **night vision looks
the same**. A new overload, `Theme::Color(argb, alpha)`, gives a token at its own
opacity.

**This changes every surface, not only the control panel.** The tokens are
global, so every window already painting through them changed with the swap.
What the remaining sub-stages (132.3b to 132.3e) owe is each surface's
**literal** colours and its instrument detailing. Until those land, the sky map's
own deep blue and its per-planet colours, among other spots, will sit on
graphite chrome. That is a mixed look, not a broken one.

**Fourteen literal spellings of the old token colours would have broken at
night**, and they were moved onto the tokens in this same change. A literal that
happened to equal a token's old bytes reached that token's designed night
luminance only by coincidence. Once the token moved, the literal would have
dropped to a derived luminance, which is the defect 96.4 fixed. Measured before
the swap, these were in:

- the histogram, sharpness and temperature backgrounds, and their red;
- the preview's red brush;
- the status bar's six brushes and its live dot;
- the sky map's default planet colour;
- the GoTo dialog background in `main.cpp`.

Also fixed along the way:

- The preview's bar highlight was raw cyan in night vision. It is now the signal
  token, tinted.
- The status bar's border and text were off-token literals. They now use the
  tokens.

**Control panel, the instrument rules applied:**

- **Section headers** were a coloured circle with an initial, and a title in the
  section's own colour: cyan for camera, amber for the focuser, green for the
  mount, red for capture. Night vision turned those into six shades of one red.
  They are now an engraved legend: a dim, bold Barlow Condensed title tracked by
  one pixel, a dim chevron, and a hairline with a lit edge under it.
- **Labels inside a section** no longer take the section's colour. They are all
  `TEXT_DIM`. The focuser position is a readout, so it is now `TEXT`.
- **RECORD** at rest has a neutral face, a 2 px signal-colour outline (the
  heaviest line on the panel) and its word in `TEXT`. That fixes the problem
  132.2 found, where the primary control was dimmer than an ordinary one at
  night. Recording and paused tint the face toward Fault and Warning.
- **AUTO FOCUS** is neutral at rest and shows Warning only while the focuser is
  moving.
- **AE** is dim when off, the signal colour when advisory, and Live when on auto.
- **PLANET LOCK is now Live with a square (■ TRACKING), not red with a dot.** This
  is a change of meaning, made on purpose: the design language gives tracking to
  Live and keeps Fault for danger and recording. The mount-control caption
  follows it.
- Every hand-mixed teal, navy, brown and crimson fill on those buttons is now
  `Theme::Mix` between the panel and a token. At night that keeps them on the
  ladder and red-only.

**Not changed:** the histogram's per-channel R, G and B tints (that is data, and
hue is the information), the dead `DrawSectionHeader`, and type sizes (96.4b).

**Tests updated, each because it pinned the old bytes:**

- `operator_palette` section 3 now checks that DEEP *is* the token. In section 4,
  "the wrong order is darker" became "the wrong order misses the ratio": on
  graphite it comes out brighter, not darker. The property under test is the
  miss.
- `audience_layout` reads the tokens instead of old literals. Those literals
  had silently fallen through to derived luminances.
- `web_theme` and `motion` read the new values.

Run under mingw-w64 and wine, all of these pass: `night_hierarchy`,
`operator_palette`, `visual_tokens`, `audience_layout`, `motion`, `web_theme`,
`object_panel_fit`, `share_caption`, `toolbar_layout` and `camera_panel`.
`ControlPanel.cpp`, `PreviewRenderer.cpp` and `main.cpp` (which includes every
window header) compile cleanly with mingw.

**Owed on the rig:** run `.\tests\ui-baseline.ps1 -Save` on master *before* this
merges, then `-Expect` set to every window, since a token swap is meant to change
all of them. Look at the control panel in daylight and in night vision, and
check the new section headers for text clipping.

**Run on the rig, 2026-11-07.** Both configurations build under MSVC, and
`run-tests` passes all 70. The reference was saved from `a565bae` (132.2), and
the diff was run with `-Expect "day/"`, so that any night change would be
flagged. That run used the build-stamp key fix from #101. Every day window
changed. In night vision, everything read `same` except two windows. GoTo's
change is its object list reordering as the sky turned, not this stage. The
main window's change is all in the control panel: the legends, the dimmed
labels, RECORD and AE, which is what this stage set out to change. The section
headers do not clip, in day or night.

**Found, not caused by this stage: the sliders leak non-red light at night.**
Sampled from the night shots, a slider's track background is the *day* DEEP
colour: (7, 13, 22) before this stage, (18, 19, 22) after. The rail is the
theme's grey (160, 160, 160) in both. The `WM_CTLCOLORSTATIC` brush does go
through `NV`, but the trackbar keeps the background it took in day mode, and
the visual style draws the rail and thumb itself. Every slider on the control
panel is affected; there are nine on the Camera tab alone. The tests cannot see this, because the colours are drawn by
the common control, not by the app.

**Fixed in 132.3a.** Sliders are de-themed and custom-drawn
(`Theme::DrawSlider` on `NM_CUSTOMDRAW`, colours from `Theme::SliderColor`).
Invalidating was not enough: a trackbar keeps its own rendering, so
`ForceRepaint` now resets each slider's range to itself with redraw on
(`Theme::RepaintSliders`), and the slider rebuilds without moving. The same
scan found a second leak: the edit boxes' sunken `WS_EX_CLIENTEDGE` frame,
which Windows draws in system white and greys. `Theme::StyleEditFrame` paints
that frame from `BORDER` and `BG_VOID` on `WM_NCPAINT`, and the same repaint
redraws it with `RDW_FRAME`. A pixel scan of the whole night control panel
(567,088 pixels) now finds **no pixel with green or blue**. In day mode, the
thumbs are now flat graphite blocks and the edit frames are a flat token
hairline, both in the Steel look. Gated by `tests/night_hierarchy.cpp`
section 9 (every slider part is red only at night, and the thumb stands out
from its groove).

### 132.3b, 2026-11-07: GoTo and the sky map

**The sky map had eight brushes that skipped night vision.** These were the
Sun's fill and label, the Moon's lit disk, shadow and label, each planet's fill
and glow ring, and `DrawObjectLabel`, which paints every object's name. None of
them went through `Theme::NVf`. In night vision the Sun was yellow, Neptune and
Uranus were blue, Mars was orange, and the labels were their day colours. All
eight now go through `NVf`. The Moon's unlit limb is now the map's own Deep at
the same opacity, so it reads as shadow on any palette.

**A gate so this cannot ship again.** `tests/night_coverage.cpp` reads the source
of the nine Direct2D windows. It fails on any `CreateSolidColorBrush` or `Clear`
that takes a bare `D2D1::ColorF` literal outside `Theme::NVf`. Pure black is
allowed, because it is black in both modes, and so is a variable, whose colour
is set elsewhere. **Run against master it fails at exactly those eight sites**
(SkyMapWindow.h lines 639, 664, 685, 687, 714, 748, 756 and 1122), and it passes
on this branch. A source scan cannot see a literal hidden inside a helper. What
it does is stop this particular mistake from shipping again.

**The sky map's own colours are now tokens, apart from the data.**

- The map and sky disk were deep-space blue. They are now Void and Deep.
- Altitude rings and cardinal lines use Line, and the horizon uses the signal
  colour.
- Stars use Text and deep-sky objects use the signal colour. Faded deep-sky
  objects use Dim.
- The scope crosshair and field of view are Live, because that is the mount's
  position.
- The status line, the info box heading and the compass letters are legends, so
  they use Dim. The info box frame uses Line.
- The confirm popup sits on Panel with a Line frame. Below the horizon it turns
  Fault. SLEW is Live on a Live-tinted face. CANCEL is Dim on Line: cancelling
  is not a fault, and it was red.
- **Kept as data:** the planets' own colours (Mars red and Neptune blue are
  information), the Sun, the Moon, the near-Earth objects and the satellites.
  They are now tinted at night like everything else.

**GoTo.**

- **Buttons are owner-drawn.** These were the system-grey buttons 119.4 left in
  this window, and night vision could not reach them. The face was the control
  panel's, inline in its `WM_DRAWITEM`. It now lives in `src/gui/FlatButton.h`,
  which the control panel, the GoTo window and its action popup (Slew, Info,
  Cancel) all call. The face gains a lit top edge, which the control panel's
  buttons pick up too.
- **Labels and status lines** were all drawn in the signal colour. They are now
  Text.
- **Lists:** `Theme::StyleStockControls` coloured every list view in the app with
  `(10, 18, 28)` on `(190, 214, 230)`. Those were off-token, so every list stayed
  blue-black under the 132.3a palette and took a derived night shade. Lists are
  now Deep and Text.

**Not in this stage:** list headers and the GoTo tab strip. These still use the
system face. Both need custom drawing: a header is a separate control that
notifies the list view rather than this window, and a de-themed tab strip falls
back to classic grey. They are 119.4's last open item. They get a stage of their
own after 132.3e, rather than being added to this one.

**Checked here.** `night_coverage` passes natively, and it fails on master as
described above. `night_hierarchy`, `operator_palette`, `visual_tokens`,
`audience_layout` and `web_theme` pass under mingw-w64 and wine. `window_chrome`
fails under wine exactly as it does on master: wine cannot load the embedded
fonts. `ControlPanel.cpp` and `main.cpp`, which includes the GoTo and sky map
headers, compile cleanly with mingw.

**Expected in the 132.0 diff:** the sky map in both modes (night changes on its
data objects), GoTo and its popup by day, every window with a list, and the
control panel's buttons gaining the lit top edge.
