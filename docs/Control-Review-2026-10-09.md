# Control review — sliders, buttons, hotkeys (2026-10-09)

A pass over every control ID, slider range and key binding, checking
that nothing added across Phases 60–70 conflicts with what was already
there. Fixes are in Phase 71 (`a2ded88`).

Five defects. Three of them predate Phase 60 — the review found them
because it was looking, not because the recent work caused them.

---

## Defects found

### 1. The emergency stop was conditional on a display mode

`Esc` exited fullscreen *instead* of stopping the mount:

```cpp
if (m_fullscreen) { m_onFullscreenToggle(); }
else              { m_onEmergencyStop();    }
```

So while fullscreen there was **no emergency stop key at all** — and
fullscreen is precisely when someone is watching the preview rather
than the panel with its STOP button. An emergency stop that depends on
a display mode is not one. `Esc` now always stops; `F11` was already
bound for fullscreen and is unaffected.

### 2. `Space` was documented as the emergency stop and bound to nothing

The help has listed it since v1.0.15. `VK_SPACE` appeared nowhere in
the codebase. Someone reaching for the key the manual names, with the
mount running away, got silence.

Now bound. This does mean `Space` no longer activates a focused button
in the main window. That is acceptable here: the four modal dialogs run
their own message loops, so `RouteThreadKey` never sees their keys and
`Space` behaves normally in all of them. It only changes the main
window and panel, where buttons are mouse-driven.

### 3. `A` for autofocus was dead

Documented in the help, and `SetAutofocusToggleHandler` was duly
registered in `main.cpp` — but no `case 'A'` ever invoked it. The
handler had been unreachable since it was written.

### 4. The exposure slider used two unit systems

The same control, two conventions:

| Site | Code | Implied unit |
|------|------|--------------|
| Creation | `initExp * 0.01f` | hundredths of a ms |
| Move handler | `pos / 1000.0` | **microseconds** |

A position was therefore labelled one way at startup and **ten times
differently** the moment it was touched — and the startup label was the
wrong one, since the camera follows the move handler.

Its ceiling was wrong too. Range `1–10000` µs is a **10 ms** maximum,
below both the 20–30 ms the help recommends for Saturn and the 50 ms
the auto-exposure assistant is allowed to reach. Neither could be
dialled in by hand, and nudging the slider would drag a correct
exposure back down. Now `1–60000` µs, matching the move handler's own
comment.

### 5. `PLANET LOCK ACTIVE` vanished when acquisition ended

`m_lblMountControl` has two writers. Phase 70 made `UpdatePlanetLockUI`
defer to the live acquisition status, but when that status went empty
the label was simply blanked — so an active planet lock stopped being
indicated until something else happened to call the other writer. It
now hands the label back instead of leaving it empty.

### Also removed

The panel's arrow-key forwarding to the main window, unreachable since
Phase 60 began consuming keys before dispatch. Left in place it was a
second path into the jog handler, waiting to double-fire the moment the
routing rules changed.

---

## Checked and clean

- **Control IDs.** All 90 `ControlPanel` enumerators expanded to
  concrete values: no duplicates, and no block within three of the next
  declared base. Sequential enumerators hide this — a block only
  collides once it grows — so it was worth expanding rather than
  eyeballing. The two sub-bases inside the 1600 block
  (`ID_COMBO_PROFILE` 1600–1603, `ID_EDIT_GOTO_TARGET` 1650–1653) have
  46 free between them.
- **Dialog IDs.** The four modal dialogs (2001+, 3001+, 4001+, 5001+)
  are each internally unique. Cross-dialog collision is impossible
  anyway — different parents — and `ID_TICK0 + n` in the readiness
  dialog tops out at 4007, clear of its 4001–4004 block.
- **The three-button TELESCOPE row** cannot clip: `PANEL_WIDTH` is a
  fixed 560, so the panel never shrinks and each button gets ~178 px.
  The concern that prompted this check was unfounded.
- **No duplicate key bindings.** 17 in `HandleKeyDown`, 3 in
  `HandleChar`, no overlap. Duplicates within one switch would not
  compile, but a key bound in both paths would, and none is.
- **Remaining slider ranges** match the profile editor's clamps exactly:
  gain 0–570, gamma 1–100, WB 0–100, USB 40–100.

---

## Verification

Keys were posted to the running emulator rather than reasoned about:
`Space` and `Esc` each produced `[SAFETY] EMERGENCY STOP` in the
session log, and `A` ran a full autofocus sweep to
`[FOCUSER] AutoFocus: pos=51800`.
