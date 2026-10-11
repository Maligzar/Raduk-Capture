# Next session — outstanding work

Rewritten 2026-10-11 at Phase 84. The previous version was written at
Phase 70 and ended with "Nothing else is planned", which stopped being
true the same week.

Nothing below is half-finished in the working tree. Everything is either
unstarted or waiting on a clear night.

---

## 1. The real outstanding item is still a clear night

This has not changed since the last version of this file, and it has
grown. Phases 60–84 are almost entirely bench-verified.

Verified against hardware: serial port auto-detect, the mount accepting
its site and clock.

Verified in the emulator only: the acquisition loop, the align helper,
the readiness dialog, rig setup, cord-wrap tracking, autofocus
(including a full slew → manual focus → autofocus run on Mars), the
quick-look stacker, the motion plot, the filter/ADC band model, and the
capture-format bar.

Three things specifically cannot be checked without sky or hardware:

- **`SHARP_CEIL = 1000`** in `core/Sharpness.h`. The sharpness percentage
  is scaled against this ceiling, and the value was chosen from emulator
  frames. If real frames run much hotter or colder, the capture gate
  moves with them.
- **The motion plot's camera source through a real FINE nudge.** The
  mount source is proven; the camera source needs a real planet to track.
- **`FULL` at the true 3840×2160.** The emulator streams at most
  1280×960, so the full-sensor path is exercised only by its clamp
  logic. On the real camera FULL should give the whole sensor at origin
  (0,0).

## 2. Decisions waiting on the owner, not on code

- **Should a profile own the capture format?** `TargetProfile` stores
  `roiWidth`, `roiHeight`, `binning` and `imgType`; `ApplyProfile()`
  applies none of them. Wiring them up would make the active profile
  authority over resolution, and since profile 0 is Jupiter at 640×480
  the app would stop starting at full frame — the opposite of what was
  asked for in Phase 84. Left as a decision.
- **Phase 35, the ADC alignment tool.** The advisor now knows the filter
  band and reports dispersion honestly. Whether a tool to help dial a
  corrector in is worth building depends on whether a corrector gets
  bought.

## 3. Known broken, not ours

**AutoStakkert will not start on this machine.** It parses every option
correctly and then never begins processing. Proven not to be
RadukCapture: the identical command fails from a bare command prompt,
and so does the exact command that worked earlier the same day.
`AutoStackOnStop` ships off because of it. Next steps are reboot, then
reinstall. See the Stacking section of the help file for how to tell a
healthy run from a stuck one.

## 4. Review pass 2 was cut short

A 20-agent review ran on 2026-10-10; 6 agents finished and 14
verification passes died on a usage limit. One finding was confirmed and
fixed. **Twelve findings remain unverified** — they are suspicions, not
defects, and should be re-verified rather than fixed on sight. Locations
as of that run (line numbers have since moved):

`AutoStakkertRunner.h` (2 — one of these turned out to be the wchar_t
narrowing fixed in Phase 83, found independently by the compiler),
`ControlPanel.h` (2), `ControlPanel.cpp` (1), `main.cpp` (2),
`QuickLookStacker.h` (1), `CaptureManager.h` (1), `CameraEmulator.h` (2),
`MotionMonitor.h` (2).

A fourth review dimension is worth adding next time: **"a periodic
update silently overwrites or invalidates something the user set."**
Three instances of exactly that were found by hand in one day — the
focuser GoTo field being overwritten mid-keystroke, the toolbar state
cache discarding active markers after a resize, and the ADC figure
silently assuming a filter band.

## 5. The planned feature roadmap

26 planned phases remain unstarted. They live in the Phase Priority
Summary table in `RadukCapture-Architecture.md`, which was corrected on
2026-10-11 — seven phases that had shipped were still showing as
outstanding.

The ones that would most change a night's results:

| Phase | Feature | Why it matters |
|-------|---------|----------------|
| 27/28 | Dark + flat frames | The only calibration gap; currently a stacker's job |
| 47 | Click the preview to recentre | Removes the jog-and-guess loop when a planet is off-centre |
| 37 | Display stretch (mini histogram) | See faint detail without touching capture settings |
| 46 | Connect/disconnect all hardware | One button instead of three, and a clean teardown |
| 31/32 | FWHM / Bahtinov focus | Star focus; autofocus is tuned for a planetary disc |
| 16 | Polar alignment assistant | Alt-az tracking drift is the ceiling on exposure length |

Tier 3 (18/19/33/34/36, 12b, 20) is nice-to-have. Full stacking
(Phase 17) is deliberately out of scope: AutoStakkert does it better,
and the quick-look stacker already answers the question that mattered —
"was that capture soft?" — the same night.

## 6. Testing notes worth keeping

- `RADUK_EMU_UNALIGNED=1` reproduces the whole tracking-blocked path.
- `RADUK_EMU_TRUE_FOCUS` defaults to **168000**, and the emulated EAF
  opens at **166500**, 1500 steps soft. Autofocus searches ±3000, so a
  start further out than that fails by design.
- `GetWindowText`/`SetWindowText` do **not** reach an EDIT control's
  buffer across a process boundary. STATIC and BUTTON text works,
  because it is stored as the window title — which is how the Phase 84
  format buttons were verified from outside the process.
- `SendMessage` with `BM_CLICK` on a button that opens a modal blocks
  the caller. Use `PostMessage`.
- **`printf` does not reach a redirected stdout in this GUI build.** Two
  separate attempts to capture emulator diagnostics that way produced
  147 bytes. Use `Log()`, which writes `raduk_log.txt` and flushes.
- **The startup readiness dialog is modal and blocks everything after
  it.** It cost most of an afternoon in Phase 84: the format buttons
  appeared to be unwired when in fact `main()` had not yet reached the
  line that wires them. If the app seems to ignore new wiring, check
  whether a dialog is sitting open first.
