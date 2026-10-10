# Next session — outstanding work

Written 2026-10-09 when a usage limit cut the session short. Everything
below is unstarted; nothing is half-finished in the working tree.

## 1. Code review: sliders, buttons and hotkeys (NOT STARTED)

The one task from the last request that was not reached. Goal: confirm
nothing added across Phases 60-70 conflicts with existing controls.

Specifically worth checking:

- **Hotkey collisions.** Phase 60 routes keys from the message pump
  thread-wide, so a key that previously only reached the main window
  now reaches every window. Confirm nothing double-fires, and that the
  `WM_GETDLGCODE` exemption really does leave arrows to the GoTo and
  align list boxes. Letters currently bound: R C S Q P V X B N H Z G M
  F T A, plus Space, arrows, F11, `?`, `/`, `o`.
- **New control IDs.** Several were added in separate phases and never
  cross-checked in one place: `ID_BTN_CABLES`, `ID_BTN_RIGSETUP`
  (ControlPanel, 1500 block); site dialog 2001+; align helper 3001+;
  readiness dialog 4001+; rig setup 5001+. Confirm no overlap inside a
  shared parent.
- **The TELESCOPE row now holds three buttons** where it held one
  (`MOUNT CTRL | CABLES OK | SETUP`, width `(w-4)/3`). Check it does
  not clip at the minimum panel width.
- **Two writers on one label.** `m_lblMountControl` is written by both
  `UpdatePlanetLockUI` and `SetAcquisitionStatus`; Phase 70 made the
  former defer, but confirm there is no path where it still wins.
- **Slider ranges** against the profile editor's clamps, which were
  never re-checked after the SAVE fix started writing live values back.

## 2. Nothing else is planned

Phases 60-70 are complete and pushed. The plan documents for 66, 67
and 68 have no unimplemented steps left.

## 3. The real outstanding item is a clear night

Almost everything from Phase 60 onward is bench-verified only. Verified
against hardware: serial port auto-detect, and the mount accepting its
site and clock. Verified in the emulator: the acquisition loop, the
align helper, the readiness dialog, the rig setup view, cord-wrap
tracking and reset. Everything else compiles and is unit-tested but
has not met the sky.

Suggested first session back:

1. Hand control -> Align -> Quick Align (needs no stars).
2. Launch. Expect `Site adopted from windows`, `Mount reports
   ALIGNED`, and a readiness line with no failures.
3. Centre a planet, press `[CENTROID]` once.

If anything refuses it should now name the precondition rather than
doing nothing silently. That reporting is the part most worth hearing
about.

## Testing notes worth keeping

- `RADUK_EMU_UNALIGNED=1` makes the emulator report itself unaligned,
  which reproduces the whole tracking-blocked path on the bench.
- `GetWindowText`/`SetWindowText` do **not** reach an EDIT control's
  buffer across a process boundary. STATIC and BUTTON text works
  because it is stored as the window title. Drive dialogs through
  rig.ini and a restart, or capture the window with `PrintWindow`.
- `SendMessage` with `BM_CLICK` on a button that opens a modal will
  block the caller. Use `PostMessage`.
