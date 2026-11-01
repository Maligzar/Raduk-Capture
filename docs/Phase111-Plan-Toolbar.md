# Phase 111: the toolbar's two rows

## Why

The owner's report: the buttons were getting crowded. Measured, it was worse.
24 buttons needed 1,508 px in one row that never wrapped, and the strip is the
window less the 560 px panel, about 1,360 px on a 1920 monitor. On that screen
8b, 16b and EXIT had no room at all.

Phase 96 never covered this strip. It was stock `BS_PUSHBUTTON`s, which cannot
follow night vision and could show "active" only by bold type or a marker in
the label.

## What changed

- **Two rows, grouped by task.** Acquire and capture format on top; view and
  everything that opens something else below. Each row's right group is anchored
  to the strip's right edge.
- **`src/gui/ToolbarLayout.h`.** Ids, tables, placement and the strip's height,
  as arithmetic with no windows.h, so a test can reach them.
- **Owner-drawn, with three states.** Idle has nothing, Ready has an amber
  outline, Active has a filled face, a bar and bold type. They differ in shape
  as well as colour because night vision flattens hue.
- **`TOOLBAR_HEIGHT` is one constant, 58.** It was the literal 30 in four files.
  The preview loses 28 px of height.
- **Resize moves buttons** rather than destroying them, which retires the state
  caches that existed only to repaint after a rebuild.
- **Labels lost their brackets and prefixes.** `[C]`, `[*]` and `[I]` were not
  keys: MAP is `M`, GOTO is `G`, and `C` is CLIP. A lit reticle names its shape.

## Not done

Side rails. The space beside the preview exists only while the image is
letterboxed, so it changes with resolution and zoom.

## Verification

`tests/toolbar_layout.cpp` holds every id to one place inside the strip at 620,
1360 and 3280 px, and every button's widest label to its width. Checklist 34
covers what a test cannot see.
