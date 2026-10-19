# Next session — outstanding work

Rewritten 2026-10-11 at Phase 84. The previous version was written at
Phase 70 and ended with "Nothing else is planned", which stopped being
true the same week.

Nothing below is half-finished in the working tree. Everything is either
unstarted or waiting on a clear night.

---

## 1. The real outstanding item is still a clear night

**The full list is now its own document: [CLEAR-NIGHT-CHECKLIST.md](CLEAR-NIGHT-CHECKLIST.md).** It is
ordered for use at the telescope, with what to look at and what to do
with the answer. The summary below is kept for context.

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

- **Should a profile own the capture format?** ANSWERED 2026-10-11:
  neither. Startup simply retains the last settings (Phase 88), and a
  profile load still applies its own format when you pick a target.
- **Phase 35, the ADC alignment tool.** ANSWERED 2026-10-13: there is no
  corrector in the imaging train yet, and the tool was built anyway and
  gated behind an *ADC fitted* checkbox on the READY screen, off by
  default. With it off the alignment window, the preview arrow and the
  FOCUSER readout all stay dark. Tick it when a corrector is bought and
  the whole set turns on.
- **Should dark subtraction feed the sharpness metric?** Hot pixels do
  inflate a Laplacian, so dark-subtracting before analysis would improve
  focusing. It would also move the capture gate and the SHARP_CEIL scale
  under a system tuned against raw frames. Worth doing deliberately, with
  before/after numbers on a real night -- not as a side effect.

## 3. AutoStakkert — withdrawn, it works

**Corrected 2026-10-18.** This said AutoStakkert would not start on
this machine and was "proven not to be ours", with a reboot and a
reinstall as the next steps. None of that survived measurement: three
runs on one recording — standalone, through the app's own launch path,
and through it again with AutoStakkert already open — all stacked, all
in about 51 s, with no reinstall and no reboot between them. There was
never anything wrong with the app's code.

What broke it originally is unexplained and not reproducible. The trap
worth remembering is that launching from Git Bash rewrites the
`/switches` into paths and produces an identical-looking failure; the
log distinguishes them. `tests\manual-autostakkert-launch.cpp` re-runs
the measurement on demand.

## 4. Review pass 2 — closed

All 12 unverified findings were judged on 2026-10-11. The findings
themselves were gone (the reviewing agents died on a usage limit and
the transcript kept only their file:line labels), so each location was
recovered from `bf2fee1` and reviewed fresh.

**5 real and fixed**, the worst being that the capture quality gate was
off at every launch: `GateEnabled()` answered false on a null handle,
and the checkbox only exists while CAPTURE is expanded, which stopped
being normal in Phase 79. Also: the AutoStakkert launch path was widened
byte-by-byte, the emulator's seeing distribution was skewed the wrong
way, the `[SHARP]` log printed a figure `Sharpness.h` documents as
meaningless, and MotionMonitor's crosshair comment had been false for
two phases.

**4 dismissed** with reasons recorded in the commit. **3 noted, not
changed** — the quick-look stacker re-anchoring on a single jumped
frame is the one most worth revisiting.

The fourth review dimension is still worth adding next time: **"a value
that lives only in its own control disappears when that control does."**
The gate bug is the fourth instance this session, after the focuser GoTo
field, the toolbar state cache, and the profile's capture fields. A
collapsed section is now the default, so this will keep happening.

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
