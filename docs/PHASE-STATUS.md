# Phase status

**This file is the only place a phase's status is asserted.** If another
document disagrees with this one, this one is right and the other is a
defect — report it rather than believing it.

Written 2026-10-18, after a pass found phase 77 carrying four different
statuses at once across six files. Each correction had been a hand edit to
one copy, and nothing ever checked the rest.

## How to read a row

`STATE (venue) date — evidence · owes: what is still missing`

| State | Means |
|---|---|
| `PLANNED` | Nothing written. A plan is not evidence, so these rows carry none. |
| `BUILT` | The code exists and compiles. Nothing but the compiler has run it. |
| `WIRED` | Reachable from the app's own path, and the app has done it at least once. |
| `GATED` | A bench test asserts its behaviour and gates a merge. **The test is named.** |
| `MEASURED` | A number was taken on the real thing. **The number, venue and date are given.** |
| `PARTIAL` | Part ships and part does not. **Which half is missing is stated.** |
| `BACKLOGGED` | Off the list, with the condition that would bring it back. |
| `DROPPED` | Off the list for good, with why. |

Venue is `(bench)` — no hardware; `(emu)` — against the emulators; `(rig)` —
real hardware indoors; `(sky)` — under the sky, which is the only venue that
settles a pointing or seeing claim.

**`owes:` is mandatory below `MEASURED (sky)`** and must name where the
remaining debt is tracked, so nothing can quietly be considered finished.

These words replaced ✅ / 🔲 / ◑. A glyph carries no evidence and no date,
which is how eleven of them went stale without anyone noticing. A word with
a mandatory evidence clause goes stale *visibly*: the evidence stops
matching.

## Open, and recently closed

| # | Phase | Status |
|---|---|---|
| 46 | Connect / disconnect all hardware | `BUILT (bench) 2026-10-18` — `src/core/HardwareLink.h`, `MountProxy.h`; `GATED tests/hardware_link.cpp` · **owes: the button has never been pressed in the running app.** Not `WIRED` until it has |
| 47 | Click to recentre | `GATED (bench) 2026-10-18` — `tests/recentre.cpp`; round trip exact against the measured matrix for square, rotated, mirrored and skewed cameras · owes: clear-night item 14 |
| 47b | Pixel position readout + directional rose | `PLANNED` — the half of 47 that genuinely needs a plate solve, because the guide calibration is a relative transform and these are absolute coordinates |
| 48 | Framing recall ("go to previous image") | `PLANNED` — design settled 2026-10-18: an observation record written beside each capture, not a plate solve of the first frame. A planetary crop has no solvable star field |
| 77 | AutoStakkert handoff | `MEASURED (rig) 2026-10-18` — 50.4 s through the app's own `StackAsync`, 8 files; three routes, same 158 MB recording · owes: nothing. `tests/manual-autostakkert-launch.cpp` re-runs it |
| 6.6 | GoTo map learning | `PARTIAL` — auto solve+sync on lock ships; **the accumulating pointing map reports and does not correct**, which is the half that would help |
| 35 | ADC alignment tool | `GATED (bench)` — `tests/adc_alignment.cpp` · owes: a corrector to align. The window exists and is deliberately dark until *ADC fitted* is ticked on the READY screen, because there is none in the train |
| 30 | Push-to assistant | `PLANNED` |
| 31 / 32 | Multi-star FWHM / Bahtinov focus | `PLANNED` — star focus; autofocus is tuned for a planetary disc |
| 18 | AVI / FITS output | `PLANNED` — phase 48's observation record is the prerequisite, since FITS wants the metadata it collects |
| 19 | Multi-session stats dashboard | `PLANNED` — same prerequisite |
| 43 | Deep sky image annotation | `PLANNED` |
| 33 | Stellar photometry | `PLANNED` |
| 34 | Solar / lunar framing | `PLANNED` |
| 20 | Distribution / ASCOM support | `PLANNED` |
| P4.1 | Xbox controller input | `BACKLOGGED` — no controller, no receiver and no Bluetooth in this machine. Feasibility proven (`8fed05a`); returns when the hardware does |
| 16 | Polar alignment assistant | `BACKLOGGED` — the mount is alt-az and there is no wedge, so the procedure does not apply. Returns with a wedge, and probably a new mount, which makes it a different project |
| 17 | Live stacking preview | `DROPPED` — superseded by the quick-look stacker (phase 74), which answers the question this was for |
| 7 | OBS / RTSP streaming | `DROPPED` |

## Recently shipped, where the status was contested

| # | Phase | Status |
|---|---|---|
| 42 | Five-monitor workspace | `WIRED (rig)` — `66d62a1`, in use on five screens; placement matched on device interface path · owes: nothing |
| 36 | Sensor analysis / smart histogram | `GATED (bench)` — `tests/sensor_analysis.cpp` |
| 92 | Object panel | `GATED (bench)` — `tests/object_knowledge.cpp`, `tests/object_panel_text.cpp` · **owes: the prose clips and bleeds — see the backlog in `ROADMAP-Next.md`** |
| 50 / 12b | Local AI assistant, Ollama | `GATED (bench)` — `tests/ai_context_facts.cpp`, which also gates the rule that the observing site never reaches the model at more than one decimal |
| 91 | Emulator field of view | `MEASURED (bench)` — `4af73e6`; the 1280×960 cap is gone and the full sensor is exercised |

## Everything else

Shipped and uncontested. The record of **what shipped in which release** is
the Phase History table in
[`../RadukCapture-Help.md`](../RadukCapture-Help.md) — that is its axis, and
it is not restated here.

What a clear night still owes is
[`CLEAR-NIGHT-CHECKLIST.md`](CLEAR-NIGHT-CHECKLIST.md), referenced by item
number from the `owes:` clauses above. The ranked queue of what to do next
is [`ROADMAP-Next.md`](ROADMAP-Next.md), which ranks but does not assert
status.
