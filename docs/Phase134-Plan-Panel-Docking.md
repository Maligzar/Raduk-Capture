# Phase 134 plan: panels that dock together

Asked for by the owner on 2026-11-11. The ask was two things, and the owner
chose both:

1. **Lego snapping.** Panels such as Histogram, Status, Sharpness and
   Temperature snap edge to edge, and once joined they move as one.
2. **A dock window.** Panels go into one container, and the container is made
   fullscreen on a monitor.

The browser dashboard cannot hold these panels. They are native Win32 windows
painting with Direct2D and GDI, and a web page cannot host them. So the dock is
a window of the app. The dashboard stays the remote view.

Phase 100 deleted an older "docking". That was the control panel's undock
button, already an empty stub. Nothing in that decision applies here.

## What exists, which this builds on

- **Chrome.** Every operator panel uses `WindowChrome.h` (phase 119), through
  `Attach` or `Install`. Moving and resizing are the native loops: `HTCAPTION`
  for moves, and the frame edges plus a 6 px top grab for resizing. That makes
  the chrome's subclass the one place to see `WM_MOVING`, `WM_SIZING` and
  `WM_ENTERSIZEMOVE`/`WM_EXITSIZEMOVE` for every panel. Today nothing anywhere
  handles those messages.
- **Workspace.** `WorkspaceLayout.h` saves each role's monitor, rectangle,
  mode (`maximized` or `fullscreen`) and open or closed state per monitor
  count. Ctrl+Shift+End saves it and Ctrl+Shift+Home gathers the windows.
- **Fullscreen.** Each panel has the same pattern: drop `WS_CAPTION`, fill
  `rcMonitor`, and restore the saved placement. The chrome hides itself when
  the caption is gone.
- **Testable geometry.** `ChromeLayout.h`, `ToolbarLayout.h` and
  `AudienceLayout.h` are pure headers with bench tests. The docking arithmetic
  follows that pattern.

**Scope.** These resizable, main-window-owned panels are in scope:

- histogram, sharpness, temperature, status, adcalign;
- skymap, skymap2, goto;
- the player.

These stay out of the first pass:

- the share view and object panel, which are audience screens with their own
  fullscreen;
- the topmost tools: help, AI chat, the mount controller and the object info
  popup. Two of these are also fixed-size.

## Design

### Groups come from geometry, not from a list

Two panels belong together when they touch: an edge of one lies flush (within
1 px) against an edge of the other, and they overlap along that edge by at
least 40 px. A group is everything connected that way. It is worked out from
the rectangles whenever it is needed, so there is no membership list to drift
from what is on screen, and the workspace file needs no new keys for it. Move
a panel away and it has left. Put it back flush and it has joined again.

### Snapping (`WM_MOVING`, `WM_SIZING`)

While a panel or group is dragged, any of its outer edges within 12 px (at
96 DPI, scaled) of another panel's edge, or of the monitor's work area, jumps
flush. The snap applies on each axis separately, and only where the two
overlap on the other axis, so a panel far below another does not jump sideways
to its edge. Resizing snaps the moving edge the same way. A thin line in the
accent colour (red at night) marks the joint being made, and goes when the
mouse is released.

### Moving a group

Dragging a panel's title bar moves its whole group. During the native move
loop, `WM_MOVING` gives the proposed rectangle. The other members follow by
the same delta in one `DeferWindowPos` batch with `SWP_NOACTIVATE |
SWP_NOZORDER`. A group snaps as one block against panels outside it.

**Shift+drag pulls one panel out** of its group. It moves alone and snaps
normally. This modifier is the owner's call; see the end.

### Shared edges resize together

Dragging an edge two members share works like a splitter: one grows and the
other shrinks, and the joint stays closed. An outer edge of the group resizes
only its own panel, and the neighbours along that side follow so the outline
stays square. Every panel gets a minimum size through `WM_GETMINMAXINFO`
(240 x 120). None has one today, and without it a splitter could crush a
panel to nothing.

### A group fullscreen on its monitor

F11 on a panel that is in a group fills the monitor with the whole group:

- each member is mapped proportionally from the group's bounding box onto
  `rcMonitor`;
- edges are rounded so the members tile the screen with no gap and no
  overlap;
- captions are hidden, the same way single-panel fullscreen hides them, so it
  reads as one instrument.

F11 or Esc restores every member to where it was. F11 on a panel alone keeps
today's single-panel fullscreen.

### The dock window

`RadukDock` is a framed window of its own, with a title bar and a dock icon on
the rail. Panels go into it by:

- dragging them over it: drop zones for left, right, top, bottom and centre
  appear, as Visual Studio does;
- right-clicking a title bar and choosing **Dock**.

Inside, the layout is a split tree: each split divides its area between two
children at a ratio, and a leaf is a panel.

**Panels are not reparented.** A docked panel stays the top-level window it
is. Its owner is set to the dock, which keeps it above the dock, and it is
positioned over the dock's slot. Its own code, render target and night mode
are untouched. The dock hides each docked panel's caption and draws its own
tab labels and splitters, so it reads as one window. Moving the dock moves its
panels. Fullscreen on the dock (F11, or the chrome's maximise with Shift)
fills the monitor, and the split tree relays out onto it.

Undocking is a drag on a panel's tab out of the dock, or **Undock** from its
menu.

Reparenting into true child windows was considered and rejected:

- every panel would need its styles, chrome and focus handling reworked;
- a Direct2D `HwndRenderTarget` must be rebuilt when its window changes
  parent;
- a bug there leaves a panel invisible on a night.

Positioned owned windows give the same picture with far less risk.

### Persistence

Lego groups need nothing new: the rectangles are already saved, and the
groups follow from them.

The dock adds:

- one placement token, `docked=<dock id>`, on each member's line;
- one `[dock.N]` line holding the split tree as text, for example
  `H(0.62, V(0.5, histogram, sharpness), status)`;
- a `groupfull` mode on the group's lines, so a fullscreen group comes back
  fullscreen.

Ctrl+Shift+End saves all of it. Ctrl+Shift+Home gathers a group as one block,
keeping its shape, rather than cascading its members apart.

## Stages

One stage is one PR, merged after the owner has looked, as with phase 132.

| Stage | What | Risk |
|---|---|---|
| 134.0 | `DockGeometry.h`, pure, with `tests/dock_geometry.cpp`: snap, adjacency and groups, group move, shared-edge resize, proportional fill with no gaps, split-tree layout and its text form. No pixel changes | None |
| 134.1 | Snapping on move and resize, the joint line, and minimum sizes. Hooked once in the chrome's subclass. `rig.ini` `PanelSnap=0` turns it off | Low |
| 134.2 | Groups move together, Shift+drag pulls one out, and shared edges resize together | Medium |
| 134.3 | Group fullscreen on F11, restore, and the `groupfull` mode saved | Medium |
| 134.4 | The dock window: drop zones, split tree, tabs and splitters, Dock and Undock, fullscreen | Medium, the largest |
| 134.5 | Persistence of docks, and Ctrl+Shift+Home keeping groups whole | Low |

## Safety rails

- **Out of bounds.** The preview path, and capture, tracking, mount and
  focuser logic. Docking only moves windows. A panel's painting changes only
  where the dock hides its caption.
- **Off switch.** `PanelSnap=0` turns snapping and grouping off. Docks still
  work if made deliberately.
- **Night first.** The joint line, drop zones, tabs and splitters use the
  tokens and are checked in night vision before day.
- **Gates.**
  - `tests/dock_geometry.cpp` for all the arithmetic;
  - the scenario runner for one scripted drag that snaps, joins, moves and
    fullscreens a group;
  - the 132.0 screenshot baseline. Only the dock and the windows inside it
    may change.
- **Monitors at different scaling.** Per-monitor DPI is still open (119.6).
  Until it lands, a group may not span two monitors with different scaling. A
  drag across that line pulls the panel out of its group rather than
  producing a mis-scaled group.

## Decided 2026-11-11

The owner approved the plan's defaults:

- **Pull-out modifier:** Shift+drag.
- **Snap distance:** 12 px at 96 DPI, scaled.
- **Panels that may dock:** the list under Scope above. The share view, the
  object panel and the topmost tools stay out for now.

## As built

### 134.0, 2026-11-11: the geometry

`src/gui/DockGeometry.h` (namespace `raduk::gui::dock`) is pure: no
Windows headers, and `(std::min)`/`(std::max)` throughout, so it is safe
to include after `windows.h`. That was checked by compiling it with
`windows.h`'s `min` and `max` macros defined. No window, and no pixel,
changes in this stage.

**Snap.** Each axis snaps on its own, to the nearest edge within the
threshold. Moving shifts the panel and keeps its size. Resizing moves only
the dragged edges. A panel is a target when the two overlap on the other
axis, or come within the snap distance of it. That last part was found by
the test: a panel dropped 5 px under another should close the gap and line
up its left edge in the same move, and under a strict overlap rule it did
neither. The joints made are returned for 134.1's accent line. Lining up
edges is not a joint.

**Groups.** `Touching` means flush within 1 px with at least 40 px of
overlap, so a corner contact does not join. `Groups` takes the connected
components with a union-find; each id is the lowest index in its group.

**Move and resize.** `MoveGroup` translates the group by the leader's delta.
`ResizeEdge` moves every group member with an edge on the dragged line: the
same side, so the outline stays square, and the opposite side, so the joint
stays closed. It clamps so that none of them goes below 240 x 120.

**Fill a monitor.** Each coordinate goes through one rounding function of
its own value, so joints that met still meet. The test checks that areas
sum exactly to the monitor's area, on 1920x1080 and on an odd 1441x900 at
negative coordinates.

**Split tree.** `Layout` tiles the area exactly, with the splitters.
`ToText` and `ParseText` round-trip. Ratios are clamped to 0.1 to 0.9.
Damaged text, and anything nested more than 32 deep, returns nothing rather
than half a tree.

**Gated.** `tests/dock_geometry.cpp`, 38 checks, passes under g++ with
AddressSanitizer and UBSan here. It is added to `tests/run-tests.ps1`, so
MSVC CI runs it.

### 134.1, 2026-11-11: panels snap

`src/gui/PanelDock.h` is the Win32 side:
- a list of the panels that dock;
- the rectangles of the others that can be targets: visible, not minimised,
  not maximised, not fullscreen, checked on every message, so nothing ever
  needs unregistering;
- the monitor's work area;
- distances scaled to the window's DPI.

`WindowChrome::Filter` calls it for registered panels only:
- **`WM_MOVING` and `WM_SIZING`:** `dock::Snap` adjusts the proposed
  rectangle in place, and the message is answered TRUE.
- **Shift held:** the panel is placed freely.
- **`WM_GETMINMAXINFO`:** sets 240 x 120, scaled. These are the first minimum
  sizes any panel has had.

Every in-scope panel already passes all of its messages through `Filter`:
the `Attach` windows first in their proc, and GoTo through `Install`'s hook.
No panel handled any of these three messages itself, so nothing is skipped.

`main.cpp` registers skymap, skymap2, goto, histogram, sharpness,
temperature, status and adcalign after placing them, and the player where it
is created. It logs `[DOCK] N panels snap to each other`. `rig.ini`
`PanelSnap=0` turns snapping off; the minimum sizes stay.

`DockGeometry.h` gains `EdgesFromSizing`, which maps `WM_SIZING`'s `WMSZ_*`
to the edge mask so a corner drag snaps both of its edges. It is gated in
`tests/dock_geometry.cpp` section 1b.

**Deferred: the accent joint line.** It moves to 134.2. It needs an overlay
window, and it earns its place once joints mean a group.

**Not seen.** This cannot run here. Owner check:
- drag Histogram near Status and it snaps flush;
- with Shift held, it does not;
- the screen edge snaps;
- no panel goes below 240 x 120;
- `PanelSnap=0` turns snapping off.

### 134.2, 2026-11-11: groups move and resize together

**The snapshot.** On `WM_ENTERSIZEMOVE`, `panel_dock::BeginDrag` records
every target panel's rectangle and `dock::Groups` over them. Every
`WM_MOVING` and `WM_SIZING` in the drag works from that snapshot rather
than from where members have got to, so rounding never accumulates. The
snapshot is dropped on `WM_EXITSIZEMOVE`. Both messages still reach the
window.

**Moving.** The new `dock::MoveGroupSnapped` shifts the group's bounding
box by the leader's delta and snaps it against panels outside the group and
the work area. It never snaps against the group's own members, which would
pin it in place. Every member gets the corrected delta. The other members
are moved in one `DeferWindowPos` batch. They receive no `WM_MOVING`, so
nothing recurses.

**Resizing.** The dragged edges snap against panels outside the group.
`ResizeEdge` then carries every member with an edge on the same line, from
the snapshot. That is the splitter: the joint stays closed and nothing goes
below 240 x 120, scaled. A corner drag does each of its edges in turn.

**Solo.** A drag is solo, behaving exactly as in 134.1, when:
- Shift is held as it starts;
- the panel is alone;
- its group spans monitors with different DPI, until 119.6.

A solo panel has left its group as soon as it is no longer flush, because
groups are only geometry.

**Gated.** `tests/dock_geometry.cpp` 3b checks that:
- the group's outer edge snaps to an outside panel;
- members keep their offsets and sizes;
- it never snaps to its own members;
- with nothing near, the result equals `MoveGroup`.

The splitter is section 4, from 134.0.

**Deferred: the joint accent line.** It moves to 134.3. Group fullscreen
needs a "this is a group" cue too, and one overlay can serve both.

**Not seen.** Owner check on the rig:
- snap Histogram under Status, drag Status, and both move;
- Shift-drag Histogram out, and it leaves the group;
- drag their shared edge, and both resize, stopping at 240 x 120;
- a lone panel behaves exactly as in 134.1.

### 134.3, 2026-11-11: a group fills its monitor

**Entering.** `WindowChrome::Filter` catches F11 on a registered panel. It
does this before its caption test, because a member in group fullscreen has
no caption. If the panel's group has two or more members, all at the same
DPI, `panel_dock::EnterGroupFull` does four things:
1. saves each member's rectangle and style;
2. maps the rectangles onto `rcMonitor` with `dock::FillMonitor`, which
   leaves no gap at any joint;
3. removes `WS_CAPTION` and `WS_THICKFRAME`;
4. places each member with `SWP_FRAMECHANGED`.

The chrome hides itself when the caption goes, exactly as in single
fullscreen.

**Restoring.** F11 or Esc on any member puts every style and rectangle back.

**Lone panels.** A panel alone, or in a mixed-DPI group, falls through to
its own F11, single fullscreen as before. The panels' own `m_fullscreen`
flags stay false during group fullscreen, so their Esc handlers do nothing
that conflicts.

**Saving the layout.** Ctrl+Shift+End restores a fullscreen group before
`CaptureCurrent`, so stretched rectangles are never saved as normal ones.
Bringing a group back fullscreen after a restart is 134.5's persistence.

**No new arithmetic.** `FillMonitor` has been gated since 134.0, section 5,
including an odd-sized monitor at negative coordinates.

**Deferred, a third time: the joint accent line.** It moves to 134.4. The
dock window needs an overlay for drop zones and splitters, and one overlay
serves all three. 134.4 must deliver it or drop it in writing.

**Not seen.** Owner check on the rig:
- group three panels and press F11 on any one: they fill the monitor in
  proportion, with no gaps and no title bars;
- F11 or Esc restores them exactly;
- a lone panel still goes single fullscreen;
- Ctrl+Shift+End during group fullscreen saves the normal positions.
