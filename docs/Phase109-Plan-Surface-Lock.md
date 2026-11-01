# Phase 109 — surface lock

The second rung after [phase 107](Phase107-Plan-ROI-Following.md). ROI
following holds a centroid, and a centroid needs a disc inside the frame.
On the Moon or the Sun at this focal length the crop is full, so the
centroid sits in the middle of the frame whatever the image does, and
following has nothing to hold. Those are also the targets with the most
surface to look at.

## Design

- **`src/core/SurfaceLock.h`, pure.** Remember a patch of the surface, find
  it again in each frame, and report where it has gone.
- **Nothing downstream changes.** The answer means what a centroid means:
  where the target has reached, starting from the frame centre.
  `RoiFollower`'s steps, landing check, outlier check and giving up all
  apply as they are, and so does the mount's offload.
- **No new button.** `[FOLLOW]` looks at the first frame after arming. A
  quarter of the frame's edge bright means the target fills it. That is
  latched for the lock, so a mode cannot flip halfway through.
- **Averaged into cells about 160 across.** That mixes the Bayer colours
  into one brightness, cuts the noise, and keeps the cost level as the
  crop grows.
- **Zero-mean normalised correlation,** so seeing and a changing exposure
  do not read as movement. A coarse search, then every cell around the
  best match, then a parabola for the fraction of a cell.
- **The patch has detail along both axes.** A limb or a terminator is a
  strong edge that says nothing about movement along itself, so detail
  counts the weaker axis. Nothing to lock on to holds the window.
- **A move is looked for in both places.** Until a move lands a frame can
  be from either window, and a move is usually wider than the search.
- **A weak match is lost, not believed.** Below 0.6 the window holds, and
  every eighth lost frame searches the whole frame.
- **The reference ages.** An alt-az field rotates, so it is retaken when
  the match weakens or every 1000 frames, never during a move, and the
  reported position stays continuous.

## What gates it

`tests/surface_lock.cpp`: which targets count as filling the frame, the
shift at both bit depths, a wobbling brightness, bland frames and one-way
edges, losing and refinding a patch, a jump past the search, a retake, and
a run through `RoiFollowLink`. 400 frames and 1200 px of drift held within
65 px of centre, 24 moves, all landed, none lost, at 327 us a frame at
640x480. 18 planted bugs caught. One of them needed a fine-grained surface
added, because a smooth one let the search climb to the answer anyway.

## Owed

- The real thing, in the emulator first and then on the Moon (checklist
  33). The surface in the test is synthetic. `kMinDetail` and `kMinScore`
  have never seen a real Moon through seeing, and those two numbers decide
  whether this works there.
- Field rotation is not simulated. The retake exists for it and has only
  been tested as a retake.
- The Sun needs a look of its own. Granulation may be below the detail
  floor, in which case it holds on sunspots or not at all.
- Each retake can add a small part of a cell of error, so a long run can
  wander by a few pixels. That does not matter for a window, and the
  stacker registers the frames anyway.
- A tracker LOCK is still what arms this, and it comes from the analyser's
  centroid. Whether that locks on a full frame has not been observed.
