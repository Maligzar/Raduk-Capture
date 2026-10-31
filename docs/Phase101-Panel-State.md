# Phase 101: the saved panel state, actually applied

## The report

> while the panels are staying located they are not persisting once
> saved or after closing in the state that they are in. they stay in
> the correct location they just don't stay open or close. I want them
> to maintain their last state.

## What was wrong

This is a defect I introduced in phase 98, and it is worth being
precise about how, because the mechanism is the one this codebase keeps
producing: the code was written, it was correct, and it was never
reached.

Phase 98 answered an earlier report of the same shape. The question put
to the owner then was what a *closed* panel should do on restore, and
the answer was "stay closed, but remember where". So `Placement` gained
a `visible` field, `CaptureCurrent` recorded `IsWindowVisible`, the
writer emitted a `closed` token, and the parser read it back. All of
that worked, and still does.

What shipped alongside it was a block in `Place()` headed, in so many
words, **position always, show never**:

```cpp
const bool openNow = IsWindowVisible(hwnd) != FALSE;
if (p.mode == PlaceMode::Maximized && p.visible && openNow)
    ShowWindow(hwnd, SW_MAXIMIZE);
```

That satisfies the closed case exactly. `SetWindowPos` moves a hidden
window perfectly well, so a closed panel kept its position without
being revealed. The case it does not satisfy is the other one: nothing
anywhere ever *showed* a window because the file said it was open. A
panel saved open came back closed.

So the state was recorded faithfully to disk and then thrown away on
the way back in. Locations persisted because `Place` applies them.
Nothing else did, because nothing applied anything else.

## The fix

Three pieces.

**`WorkspaceLayout::StateFor(role)`** reports the saved state as
`Open`, `Closed`, or `Unknown`. It does not touch any window.

Applying it is left to the caller rather than done inside `Place`, for
two reasons. `Place` also runs on every display change, and a monitor
being unplugged is not a reason to reopen a panel somebody closed
during the session. And several of these panels do real work in their
own `Show()`: the share view restores whether it was filling the
screen and arms its hint, the object panel re-renders, the ADC
alignment window takes the foreground. A `ShowWindow` on their handle
would make the window visible while skipping all of it.

**`Unknown` is the absence of an opinion, not a third one.** Only a
real line in the layout file counts. A generated fallback has `visible`
defaulting to true, and honouring that would throw the share view up as
a black full-screen rectangle at first run -- which is the exact
failure the old "deliberately not marked Maximized" workaround in the
generated defaults existed to dodge.

**The startup pass in `main.cpp`** walks a role-to-`Show()`/`Hide()`
table and applies whatever the file says, skipping `Unknown`. Two
details of ordering matter:

- It runs **before** the `Place` loop. `Place` maximises a role only
  when the window is already visible, so a panel saved open *and*
  maximised has to be open by the time `Place` reaches it, or it comes
  back open but restored down.
- The focus hand-back to the control window runs **after** `Place`,
  because placing a window can raise it too. Without it, whichever
  panel happened to be restored last would be holding the keyboard.

## What is tested

`tests/workspace_state.cpp`, 14 checks. The three states read back
correctly, an entry with no visibility token defaults to open (which is
what the writer relies on, since it only ever writes the closed case),
a role with no line on disk reads `Unknown` rather than `Open`, and all
of it survives a `Save`/`Load` round trip -- the writer and the parser
being the two halves that could drift apart.

What is **not** tested is the application itself, which lives in
`main.cpp` and needs windows. That is the gap that let phase 98 ship:
a test of the file format would have passed then too. It is on the
clear-night checklist as an eyes-on item.

## Status

`BUILT` on the bench, `WIRED`. Needs one launch on the rig to confirm:
close two panels, leave two open, Ctrl+Shift+End, restart, and check
all four come back as they were -- including a maximised one.
