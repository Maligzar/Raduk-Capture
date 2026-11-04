# Phase 120 plan — consolidating the toolbar and the side panel

> *"Lots of room to work with. I think we can consolidate between the top and the
> side panels into a more streamlined approach."*

Built 2026-11-01 as proposed. See "As built" at the end. Not yet looked at by the owner.

## What is duplicated or split

| Finding | Where |
|---|---|
| Frame rate shown three times | Top readout, LIVE line, status bar |
| Resolution and drops shown twice | LIVE line and status bar |
| Temperature shown three times | Temperature overlay, focuser row, status bar |
| Camera settings split | ROI and bit depth in the toolbar, the rest in CAMERA |
| Tracking split | CENTROID, LOCK and FOLLOW in the toolbar, PLANET LOCK in TELESCOPE |
| Going to a target split | Toolbar GOTO opens a window, `TGT` in TELESCOPE slews |
| Target named twice | `TGT` and CAPTURE's `Target` |
| View controls split | COLOR, RETICLE and CLIP in the toolbar, ZOOM and channel in IMAGE |
| RECORD can scroll away | Bottom of a long panel |

## Proposed layout

- **One status strip** across the top replaces the top readout, the LIVE line and
  the status bar. Each figure appears once.
- **A left icon rail** holds things that open or leave: map, GoTo and passes, runs,
  stack, dashboard, assistant, help, exit. The toolbar's two rows go away.
- **View controls float over the image,** where their effect is, and hide in
  fullscreen. The preview itself is not changed, only what sits around it.
- **The side panel becomes four tabs:** Camera (with ROI and bit depth), Focus,
  Mount (with centroid, lock, follow and the one target box), Output (file, type
  and frames). Sections stop competing for one long scroll.
- **RECORD is pinned** below the tabs on every tab.

Tabs hide things, so anything needed mid-run must stay visible. The status strip,
RECORD, and an active tracking state are always on screen.

## Keep

Every shortcut, and the rule that the image is the content. The `Ctrl+Shift`
chords and panel state survive unchanged. Night vision reads by position and
shape, so a fixed tab order helps it.

## Stages

| Stage | What | Size |
|---|---|---|
| 120.1 | One status strip, and the readouts it replaces removed | M |
| 120.2 | Move camera and tracking controls into the panel | M |
| 120.3 | Tabs, with RECORD pinned | L |
| 120.4 | Icon rail replaces the toolbar | M |
| 120.5 | View controls over the image | M |

`ToolbarLayout.h` and its test retire at 120.4, and the toolbar's dark-room item,
checklist 34, is rewritten. Moving from the panel's literal offsets to tabs is the
natural moment to measure text, which also absorbs most of 96.4b.

## As built

- **Status strip.** `StatusStripWindow` in `StatusBar.h` is a child window with its
  own render target, drawn 5 times a second. The status bar left the preview's
  per-frame path, which gives the image its 32 px. The top banner is gone, and the
  LIVE line is now `FRAMES: n`, kept because it is the reset popup's click target.
- **Rail and view strip.** `ToolbarStrip` keeps all logic and state. `ToolbarLayout.h`
  now gives each control one home, and `tests/toolbar_layout.cpp` pins that.
- **Tabs.** Built on the collapse system: a section outside the current tab is
  "collapsed". Section headers no longer collapse, so their chevrons mean nothing.
  The old saved collapse state is ignored.
- **RECORD** is created last on every tab, at the foot of the panel. In a window too
  short to hold a tab it follows the content and scrolls.

Also fixed: Status window text now ends in an ellipsis rather than running into the
next column, and bar text goes dark where it crosses the fill.

**Not done from the plan.** `TGT` and CAPTURE's `Target` are still two boxes. The view
strip does not hide in fullscreen. Temperature still shows in three places. The panel's
offsets are still literal. Nobody has seen any of this on screen.

Decided by default: tabs, rail on the left.

## Polish pass

Asked for by the owner from a screenshot of the running app: larger type, using the
room the tabs freed. Panel text went to `Type::STRONG` (16) with rows of 21, 30 and
26 px, and about 30 fixed columns grew by a third to hold it. Rail, view strip and
status strip went up a step, and the strip no longer wraps. The mount window's
buttons are dark and owner-drawn.

Found on the way: titles read "R", "M" and so on, because wide-registered windows
fell through to the narrow `DefWindowProc`, which cut each at its first character.
All now call `DefWindowProcW`.
