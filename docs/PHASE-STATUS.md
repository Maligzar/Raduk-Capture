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
| 94 | Real sky in the emulator | `PLANNED 2026-10-31` — [`Phase94-Plan-Real-Sky-Emulator.md`](Phase94-Plan-Real-Sky-Emulator.md), staged 94.1 to 94.5. Nothing written. **Was numbered 93, which was already taken**; renumbered 2026-10-31. Recorded here for one fact found while planning it, which is not a plan but a property of the code today: the emulator's star background is a 1-in-4096 hash sprinkle of single pixels, so a plate solve cannot succeed in the emulator and `SOLVE`, `SOLVE TO ALIGN`, phase 30's push-to loop, phase 47b's readout and phase 48's framing recall have no indoor path |
| 93 | Variable-rate slew, and the render pass | `GATED (bench)` — the real phase 93, which had no row until the number was wanted for something else. Two pieces: `SlewVariable` sends NexStar's `P 3` with the rate as arcsec/sec times four, big-endian, so the jog is no longer a nine-position switch on a mount that always had a variable-rate command, gated by `tests/slew_variable.cpp`; and `PreviewRenderer` only produces pixels when the frame changed, after measuring that at the full sensor 94 per cent of render passes were re-debayering an unchanged frame · owes: the row itself was reconstructed from code comments on 2026-10-31, so its evidence is thinner than a row written at the time |
| 47b | Pixel position readout + directional rose | `BUILT (bench) 2026-10-30` + `GATED (bench)` — `src/sky/PixelWCS.h`, `src/sky/SolveResult.h`, drawn by `PreviewRenderer::DrawPixelReadout` / `DrawDirectionRose`, fed by `GoToPanel::SetWcsCallback`. `tests/pixel_wcs.cpp` gates the TAN deprojection, parity, row order, the CRPIX conversion, the keyword derivation, the rose, the agreement check, staleness and the sexagesimal carry (100 assertions). Green under MSVC: the owner ran `tests\run-tests.ps1` on the `claude/phase47b-pixel-wcs` branch on 2026-10-30 and reported every test passing (their report, not output captured here). **Never run in the app** · owes: (a) one session with a real solve, which is the only thing that can settle `PlateSolution::fitsBottomUp` — clear-night item 15; (b) nothing detects a hand-controller move, so a solve stays trusted until the one-hour ceiling |
| 48 | Framing recall ("go to previous image") | `WIRED (emu) 2026-10-25` + `GATED (bench)` — in the emulator a recording left `_observation.txt` with every field, an app-exit stop left one too, and `Shift+G` from Kochab put up the confirmation, did nothing on No, and on Yes slewed back through `DoGoTo` (profile auto-load fired) to 18h36m53s +38°46′35″, Vega; `tests/observation_record.cpp` gates the record's round trip, every recall refusal (no mount, unaligned, no site, under 15°, within 20° of the Sun) and the framing nudge for square, rotated, mirrored and skewed cameras · owes: `Ctrl+Shift+G` (framing restore) has not run in the app, since it needs a guide calibration; and clear-night: a recall to a real planet on the real mount |
| — | Mount jog, all four directions | `GATED (bench) 2026-10-18` — `b0015dc`, `tests/mount_jog.cpp`; west and south had looked up a signed rate as preset 0, "stopped", on the emulator only · owes: nothing on the bench; a real mount was never affected |
| B3 | Dashboard preview picture | `WIRED (emu) 2026-10-25` — JPEG over the WebSocket to subscribed clients; `GATED tests/preview_frame.cpp` and `tests/ws_server_loopback.cpp`; capture 36.5 / 36.7 / 37.0 fps with preview off / on / off at the full sensor, 0 drops, 1.94 pictures/s at 11 KB/s, measured by a WebSocket client against the running emulator · owes: the owner seeing it on the dashboard in a browser, and a measurement while recording |
| — | Dashboard from the home network (`WsLan`, `WsLanOpen`) | `WIRED (rig) 2026-10-25` — the owner opened the dashboard through Leoben on Elminster: it connected to StarMachine with no token, showed the preview and reconnected by itself after RadukCapture restarted. Gated by `tests/leoben_weather.cpp` (who is exempt) and `tests/ws_server_loopback.cpp` (loopback still needs the token) · owes: nothing |
| — | Weather from Leoben | `GATED (bench) 2026-10-25` — `tests/leoben_weather.cpp`: the reply, sea-level to station pressure, live air in the ADC and its 30-minute expiry, the URL, the record round trip · **owes: a real fetch from Leoben on Elminster** (needs `expose_lan` and `LEOBEN_API_TOKEN` there, `LeobenUrl` and `LeobenToken` here) |
| P5.1 | Exposure-midpoint SER timestamps | `GATED (bench) 2026-10-30` - `tests/ser_timestamp_midpoint.cpp` compiles against the real `Timestamp.h` and all 9 assertions pass. Green under MSVC on 2026-10-30, the gate that counts: the owner ran `tests\run-tests.ps1` and reported every test passing (their report, not output captured here); it had previously only been built with mingw-w64 and run under wine · owes: one SER file opened in WinJUPOS. The residual readout-and-transfer bias is still not removed |
| P5.2 | `ICamera` and `IFocuser` | `GATED (bench) 2026-10-30` - interfaces exist AND are now load-bearing. The call sites moved: 41 `GetControl(ASI_*)`, 7 `cam.Temperature()`, 8 region calls, and the statics `FrameSize`/`Enumerate`/`ErrorString` replaced by free functions (`FrameSizeBytes`, `EnumerateCameras`, `Describe(CaptureError)`). **`CaptureThread` holds an `ICamera*` and carries `CaptureError`, so the hot path names no vendor type at all.** `main.cpp` and `CaptureThread.cpp` both syntax-check clean in emulator AND hardware configurations, which matters because no test compiles either. Green under MSVC on 2026-10-30: the owner ran `tests\run-tests.ps1` and reported every test passing (their report, not output captured here); it had previously only been built with mingw-w64 and run under wine · scope line held deliberately: `FrameSlot::imgType` and `TargetProfile::imgType` keep the vendor enum because the latter persists to rig.ini as an int, and changing that is a saved-profile question, not an interface one · owes: a real second backend to prove the seam rather than argue it |
| P5.3 | ASCOM reconsidered | `GATED (bench) 2026-10-30` - `tests/ascom_driver_choice.cpp` compiles against the real `ASCOMMount.h`, COM headers and all, and all 13 assertions pass. Green under MSVC on 2026-10-30: the owner ran `tests\run-tests.ps1` and reported every test passing (their report, not output captured here); it had previously only been built with mingw-w64 and run under wine `AcceptConnectedDriver` added 2026-10-30: `main.cpp` was dropping any connected simulator unconditionally, so naming one connected and then threw it away and the indoor run this phase exists for could not be done at all. The second gate now follows the first gate's rule - refuse a simulator discovery found, keep one the owner named - and both halves are pinned (20 assertions) · owes: the thing this was for - the generic path driven once indoors against a named simulator. Procedure, and what a partial pass would mean, in [`ASCOM-SIMULATOR-RUN.md`](ASCOM-SIMULATOR-RUN.md) |
| P5.4 | Installer, releases, no hardcoded `D:` paths | `PARTIAL - GATED (bench) 2026-10-30` - the paths half only. `tests/paths_resolution.cpp` passes all 8 assertions, and the fallback was seen working in a real binary: with no `D:` present, `object_knowledge` resolved to the executable's directory and said so. Green under MSVC on 2026-10-30: the owner ran `tests\run-tests.ps1` and reported every test passing (their report, not output captured here); it had previously only been built with mingw-w64 and run under wine · owes: the installer and release halves, which are untouched |
| P5.5 | CMake floor | `MEASURED (bench) 2026-10-25` — `48e53a9`; floor 3.21, built in both configurations with MSVC 14.50 and the compile flags read back; 3.16 builds but maps C++20 to `/std:c++latest` · owes: nothing |
| P5.6 | WebSocket parser fuzzed, token on every connection | `WIRED (emu) 2026-10-25` — the real dashboard connected to the running emulator build with the `WsToken` from its `rig.ini`, and against the same running app a handshake with that token got `101` and one without got `401`. Bench gates: `137dc5c`; `tests/ws_protocol.cpp`, `tests/ws_fuzz.cpp` (AddressSanitizer), `tests/ws_server_loopback.cpp` Commands act through it: the owner jogged the emulated mount from the dashboard, and a remote `start_capture` started a recording (session log, 11:07:06). Stopping one did not work — the dashboard's start/stop flag was never assigned — fixed in the dashboard and checked against a stand-in socket, not yet against the app · **owes: one remote stop seen to end a recording, and `apex_bridge.py` on the streaming PC given the token if it is still in use** — it is not in this repository |
| P5.7 | License | `PLANNED` — an owner decision, not code: keep PolyForm Noncommercial, relicense to MPL-2.0, or dual-license. Unmade |
| 77 | AutoStakkert handoff | `MEASURED (rig) 2026-10-18` — 50.4 s through the app's own `StackAsync`, 8 files; three routes, same 158 MB recording · owes: nothing. `tests/manual-autostakkert-launch.cpp` re-runs it |
| 6.6 | GoTo map learning | `PARTIAL` — auto solve+sync on lock ships; **the accumulating pointing map reports and does not correct**, which is the half that would help |
| 35 | ADC alignment tool | `GATED (bench)` — `tests/adc_alignment.cpp` · owes: a corrector to align. The window exists and is deliberately dark until *ADC fitted* is ticked on the READY screen, because there is none in the train |
| 30 | Push-to assistant | `BUILT (bench) 2026-10-30` + `GATED (bench)` - geometry in `src/sky/PushTo.h`, wired into `GoToPanel`: a `PUSH-TO` button, a repeating solve driven from `Tick()`, and the instruction on the status line. Paces at the solve rate rather than a timer (a solve is 5-30 s and the answer is stale the moment the scope moves); a 1 s floor stops a fast failure spinning. Hints the solver with the TARGET, not the mount, because the mount's position is the one number this feature cannot trust. Stops itself on centred. Tolerance scales with the solved field (`CentredToleranceDeg`, a tenth, floored at 0.05 deg). `tests/push_to.cpp` 30 assertions pass; `main.cpp` compiles clean in both configurations, which is the only way GoToPanel gets compiled at all. Green under MSVC on 2026-10-30: the owner ran `tests\run-tests.ps1` and reported every test passing (their report, not output captured here). **Never run in the app** · owes: one session actually pushing a scope by it - the thread handoff, the cadence and the wording are all judgements no bench can check |
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
