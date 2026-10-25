# Roadmap and Phase History

What is built, what is not, and what was deliberately decided against.

The project uses two parallel records, and the distinction matters:

| Record | Is |
|--------|-----|
| [`RadukCapture-Architecture.md`](https://github.com/Maligzar/Raduk-Capture/blob/master/RadukCapture-Architecture.md) phase table | The roadmap of **planned** features |
| [`RadukCapture-Help.md`](https://github.com/Maligzar/Raduk-Capture/blob/master/RadukCapture-Help.md) phase history | The record of **what shipped** |

> **Phases 60–84 are not in the roadmap table.** They were reactive work —
> reviews, defect fixes, and things the rig turned out to need — rather than
> items from the plan.

That split is worth keeping in mind when reading either document: roughly a
third of the development effort went into work that was never planned,
which is itself the honest shape of a project built against real hardware.

---

## Built and in use

**Core capture:** SER v3 recording with capture-time trailer timestamps,
RAM-first write buffering, per-frame lucky-imaging quality scores,
resolution and bit-depth presets on the toolbar through one shared
reconfigure path.

**Control:** closed-loop planet tracking with measured guide calibration,
GoTo with sky map and ASTAP plate solving, EAF V-curve autofocus with an
honest report, NexStar direct serial with a runaway watchdog, cord-wrap
tracking, keep-out sector.

**Analysis:** per-channel histograms, the shared Bayer sharpness metric,
relative seeing score, capture quality gate, quick-look stacker, dark and
flat masters (preview only).

**Guidance:** readiness check, atmospheric dispersion advisor over the
fitted filter's band, auto-exposure assistant, local AI assistant and
advisory loop.

**Display:** night vision across the whole interface, display stretch with
black and white points, single-channel R/G/B view, four-shape movable
reticle, framing shot, six display FX modes, clipping warning, quality bar
graph, RGB align, pixel readout.

**Infrastructure:** full device emulators with closed tracking loop, fault
injection, defocus and sensor noise; WebSocket telemetry and browser
dashboard; session logging; profile management.

---

## Tier 0 — reliability, validated in emulation 2026-07-11

The reliability block, and the one place where the review references in the
source point:

| Phase | Feature | Effort |
|-------|---------|--------|
| 51 | Guide calibration and axis transform | Hard |
| 52 | Closed-loop tracking emulation | Medium |
| 53 | Correction-loop dynamics | Medium |
| 54 | Auto-sync repair (ASTAP + J2000) | Easy-Med |
| 55 | Rig configuration | Easy-Med |
| 56 | Frame-path memory safety | Medium |
| 57 | Mount safety and remote hardening | Medium |
| 58 | Camera fault recovery | Medium |
| 59 | Capture and assist reliability | Medium |

"Validated (emu)" is the honest claim: validated in emulation, which is not
the same as validated on sky. See
[what the emulator cannot tell you](Emulator-and-Testing#what-the-emulator-cannot-tell-you).

### Phase 58 — the self-healing path, in detail

Worth recording because it is the only automatic recovery in the system:

| Step | Behaviour |
|------|-----------|
| **Detection** | `CaptureThread` monitors consecutive `ASI_ERROR_TIMEOUT` returns |
| **Threshold** | 20 consecutive frames fail to arrive (~500 ms latency) → "pipeline wedge" |
| **Recovery** | `ASICamera::RestartCapture()`: `StopVideoCapture`, **100 ms sleep** to let the USB controller flush and the camera's 256 MB DDR3 buffer clear, then `StartVideoCapture` |
| **Telemetry** | Each reset increments a counter in `CaptureStats`, broadcast over WebSocket for health monitoring |

The 100 ms sleep is the load-bearing part: restarting immediately leaves the
camera's internal buffer in the state that wedged it.

---

## Outstanding plan

> **Nothing in these rows is half-built: each is unstarted.** That claim is
> from the architecture document, and it is a useful property — there is no
> partially-wired feature waiting to surprise you, with one exception noted
> below.

### Tier 5 — competitive position

Added 2026-10-19, out of the product comparison written up in
[Comparison With Other Software](Comparison-With-Other-Software). Seven
phases, P5.1 to P5.7, live in
[`docs/ROADMAP-Next.md`](https://github.com/Maligzar/Raduk-Capture/blob/master/docs/ROADMAP-Next.md):
exposure-midpoint SER timestamps, `ICamera` and `IFocuser` interfaces,
the generic ASCOM path reconsidered, distribution split out of the
very-hard phase 20, the CMake floor, fuzzing and authenticating the
WebSocket command path, and a licence decision. P5.6 entered the live
queue on its safety dimension, since the command set moves a telescope;
it is now built, with a token required on every connection (see
[Telemetry and Dashboard](Telemetry-and-Dashboard#every-connection-needs-a-token-p56)). P5.7 is kept out of the queue as an owner
decision rather than a task.

The comparison also re-weighted work already on the list. By its
weighting the highest-value unticked item in the project is clear-night
item 3, checking the plate scale against the sky: every arcsecond figure
the application reports derives from a number that was calculated and
never verified, and the fix costs no code.

### Tier 2

| Phase | Feature | Effort |
|-------|---------|--------|
| 46 | Connect / disconnect all hardware | Medium |
| 30 | Push-to assistant | Medium |
| 47 | Pixel position / click to recentre | Medium |
| 48 | Go to previous image (framing recall) | Medium |
| 16 | Polar alignment assistant | Hard |

### Tier 3

| Phase | Feature | Effort |
|-------|---------|--------|
| 31 | Multi-star FWHM focus | Hard |
| 32 | Bahtinov mask focus | Hard |
| 18 | AVI / FITS output | Medium |
| 19 | Multi-session stats dashboard | Medium |
| 43 | Deep sky image annotation | Hard |
| 34 | Solar / lunar framing | Hard |
| 33 | Stellar photometry | Very hard |
| 20 | Distribution / ASCOM support | Very hard |

### Partial

| Phase | State |
|-------|-------|
| **6.6 GoTo map learning** | **◑ partial.** Auto solve+sync on lock ships; the accumulating map **reports but does not correct** — see [why](Sky-GoTo-and-Plate-Solving#why-it-reports-and-does-not-correct) |
| 17 Live stacking preview | ◑ Superseded by the quick-look stacker (Phase 74) |
| Phase 4 APEX items | `apex_bridge.py` RC telemetry forwarding, weather-aware capture suggestions |
| Phase 77 AutoStakkert | ✅ **CLI and in-app launch both verified 2026-10-18** — 50.4 s via the app's own path, 8 files, with and without another AS instance open. The earlier "in-app launch does not work" is withdrawn; what caused it is unexplained and no longer reproducible |

### Deferred indefinitely

**OBS / RTSP streaming (Phase 7).** H.264 from the capture PC GPU (NVENC or
Media Foundation) to OBS on the monitoring PC, ~5–10 Mbps.

> Deferred until home WiFi bandwidth is confirmed sufficient for
> low-latency video. No timeline — revisit after all Tier 1 and Tier 2
> phases complete.

### Superseded

**Milestone 8 — ASCOM mount layer.** Built and tested, then superseded by
NexStar direct. All known CPWI ASCOM drivers (CPWI Telescope, DeviceHub,
ASIMount, JustAHub, all simulators) are blacklisted in
`ASCOMMount::FindCPWIDriver()`. NexStar direct is faster, has no COM or
registry dependencies, and supports all required commands.
`ASCOMMount.h` is kept for potential future use.

---

## Deliberately not built

Distinct from "not yet" — these are decisions:

**Full stacking.** AutoStakkert does multi-point alignment and drizzle
better, it is free, and nothing here would beat it. The
[quick-look stacker](Image-Analysis#quick-look-stacker) answers "is this
sharp" at the telescope; Phase 77 added the handoff rather than a
competitor.

**Calibration frames in the recording.** Dark and flat correction applies
to the preview only. The SER stays raw, because a stacker does this better
with the full frame set and raw data stays useful when a master turns out to
have been wrong.

**Automatic pointing correction.** See Phase 6.6 above. The sign is
backwards and the recorded quantity is not the one a correction needs;
settling which is right is a measurement on a real mount.

**Auto-unwinding the cord wrap.** Shedding a turn means rotating up to
360°, a long motion that can snag something by itself. The app forecasts
and warns; the observer unwinds.

---

## Where the version is declared

**`src/Version.h`, and nowhere else.** Everything else derives from it at
configure time.

```c
#define RADUK_VERSION_W  L"2.1.0"
#define RADUK_VERSION_A   "2.1.0"
```

To bump the version, edit those two lines. Nothing else. The form must be
`MAJOR.MINOR.PATCH` — the configure step rejects anything else — so the
`v2.0` tag is declared as `2.0.0`.

### How it propagates

`CMakeLists.txt` reads `src/Version.h` **before `project()`**, parses
`RADUK_VERSION_A` with a regex, and uses it as the project version. It then
expands `src/RadukCapture.rc.in` into `${CMAKE_BINARY_DIR}/RadukCapture.rc`
and compiles that:

| Consumer | Gets | Form |
|----------|------|------|
| Title bar | `RADUK_VERSION_A` directly, plus `main.cpp`'s compile timestamp | `2.1.0` |
| `project(RadukCapture VERSION ...)` | Parsed from the header | `2.1.0` |
| `FILEVERSION` / `PRODUCTVERSION` | `RADUK_VERSION_COMMA` | `2,1,0,0` |
| `FileVersion` / `ProductVersion` strings | `RADUK_VERSION_DOT` | `2.1.0.0` |
| Icon path in the resource | `RADUK_ICON_PATH`, absolute | — |

The generated `.rc` is a build artefact and is not committed. The template
`src/RadukCapture.rc.in` is. The icon is named by absolute path so the
generated file resolves it from the build directory without depending on
the RC compiler's include path.

### Three guards

Deriving a value is only an improvement if the derivation fails loudly:

| Guard | Fires when | Result |
|-------|-----------|--------|
| `RADUK_VERSION_A` regex | The macro is missing or not `MAJOR.MINOR.PATCH` | `FATAL_ERROR` naming the file and the required form |
| `RADUK_VERSION_W` vs `_A` | The two macros disagree — a bump that updated one and not the other | `FATAL_ERROR` printing both values |
| `CMAKE_CONFIGURE_DEPENDS` on `Version.h` | Always | Editing the header re-runs configure on the next build, so the resource follows without a manual re-configure |

That second guard matters because nothing in the compiler checks it. The
wide macro feeds the title bar and the narrow one feeds the log and the
resource, so a half-done bump would put two different versions in one
binary.

`file(READ)` creates no configure dependency on its own, which is what the
third guard supplies. Without it a version bump would silently keep the
previous number in the resource until someone re-configured by hand — the
same class of staleness the whole arrangement exists to prevent.

### What this replaced, and why

The version used to live in **five hand-maintained declarations across four
files**, and they had drifted:

| File | Read |
|------|------|
| `src/Version.h` | 1.1.9 |
| `CMakeLists.txt` | 1.0.5 |
| `src/RadukCapture.rc` | 1.0.8.0 |
| `assets/RadukCapture.rc` | 1.0.8.0 |

while `RadukCapture-Help.md` documented **v1.2.3** as shipped. So the
authoritative *runtime* number — the one in the title bar — was the stalest
of the four, which is exactly the failure the `Version.h` comment is written
to prevent:

> The title bar shows it together with the compile timestamp of `main.cpp`
> — so a stale exe (launched from the wrong build dir, or run before a
> rebuild finished) is immediately obvious. The compile timestamp refreshes
> automatically whenever `main.cpp` recompiles; bumping the version here
> forces that recompile, so the two always move together.

The mechanism was sound and the bump had been missed, four times over. The
lesson is about the count rather than the numbers: one declaration that is
checked on every configure stays honest, five updated by hand do not.

`assets/RadukCapture.rc` was a dead duplicate — nothing in CMake or the
build scripts referenced it, and it differed from `src/RadukCapture.rc` only
in the icon path separator. Both were deleted when the template replaced
them.

---

## Version history

Condensed from the help file's phase history. The full table is in
[`RadukCapture-Help.md`](https://github.com/Maligzar/Raduk-Capture/blob/master/RadukCapture-Help.md#phase-history).

| Version | Highlights |
|---------|-----------|
| v0.9 | Phases 1–6: all devices communicating, centroid tracking, GoTo panel |
| v1.0 | Full Win32 control panel and Direct2D preview |
| v1.0.1–1.0.15 | Mount controller, ADC advisor, lucky-imaging tagger, auto-exposure, clipping overlay, snapshots, quality graph, pause, frame types, RGB align, pixel readout, display FX, night vision, single channel, help system |
| v1.1.0–1.1.3 | Version in title bar, hold-last-frame, initial public release |
| v1.1.4 | Hotkeys from the message pump; arrow-key jog fixes; mouse reaches the preview; sharpness crop follows the planet; port auto-detect; ROI and LOCK state **why** they refuse |
| v1.1.5 | Site and clock to the mount; one-point ALIGN; true polar sky map; tracking verified by readback; **one-button acquisition**; guide rate measured; site auto-detect; ADC pressure from elevation |
| v1.1.6 | Faint-vs-absent acquisition messages; `TRK: UNKNOWN`; azimuth surfaced; cord-wrap tracking; align helper |
| v1.1.7 | Pre-slew cord-wrap forecast; keep-out sector; rig setup view; **readiness check** with live refresh |
| v1.1.8 | `Esc` stops unconditionally; `Space` and `A` actually bound; exposure slider ceiling 10 → 60 ms |
| v1.1.9 | READY button |
| v1.2.0 | **Autofocus: real per-position averaging, honest reporting, reverts a bad fit.** Sharpness metric fixed — was measuring colour, not focus. Emulator models defocus and noise. Quick-look stacker |
| v1.2.1 | Jog rate reaches the arrow keys; motion plot |
| v1.2.2 | Per-frame quality scores; one shared sharpness definition; a ceiling that survives sensor noise; capture gate fixed |
| v1.2.3 | Both configurations build from one script; ADC over the fitted filter's band; toolbar resolution and bit depth; one shared capture-format path; profiles apply resolution and limit; CAPTURE and PROFILE merged; assistant told the format and display modes; startup restores last camera state; night vision whole-interface and persisted; multi-reticle with move/size/rotate; framing shot; dark and flat masters; display stretch and real R/G/B histogram; advisory loop |
| v1.2.4–1.2.7 | Labels in the help file only — `Version.h` stayed at 1.2.3. Five-monitor workspace, status/histogram/sharpness/ADC windows, `U` to clear the view, share view |
| v2.0 | Matches the `v2.0` tag. Declared as `2.0.0` from 2026-10-25, ending a stretch in which the source, the help file and the tag all named different numbers |
| v2.1 | Phase 48: an observation record beside every capture, and `Shift+G` framing recall |
| v2.2 | B3: the browser dashboard shows the telescope's view, a small JPEG about twice a second; every dashboard socket gets a 500 ms send timeout |
| v2.3 | **The current version.** `[DASH]` on the toolbar opens the browser dashboard with the access token already in it, and it connects by itself. No tag yet |

### Two version boundaries that invalidate old numbers

**v1.2.0 — sharpness changed scale.** The metric was computing a
red-minus-green colour difference rather than detail. Readings from earlier
sessions no longer compare, and autofocus results from before this version
were not meaningful.

**v1.2.2 — the seeing score became relative.** It is now relative to the
best recent frame rather than an absolute scale. **A capture GATE threshold
set before this does not behave as you remember** — see
[Troubleshooting](Troubleshooting#nothing-is-being-recorded-or-almost-nothing).

---

## Review trail

The `review C1` / `G4` / `S1` / `T17` references scattered through the
source point into [`docs/`](https://github.com/Maligzar/Raduk-Capture/tree/master/docs):

| Document | Covers |
|----------|--------|
| `CODE_REVIEW_2026-06-10.md` | Full sweep, 28 KB |
| `CODE_REVIEW_2026-07-06.md` | Follow-up |
| `SECURITY_REVIEW_2026-07-11.md` | Pre-public-release privacy and attack surface. **§2 records an action still outstanding** |
| `Tracking-Findings-2026-10-08.md` | Tracking behaviour on the real rig |
| `Tracking-Review-2026-10-09.md` | Tracking review |
| `Control-Review-2026-10-09.md` | Control path |
| `CLEAR-NIGHT-CHECKLIST.md` | The manual on-sky test plan |
| `Phase66/67/68-Plan-*.md` | Phase design documents |
| `NEXT-SESSION.md` | Working notes |

Phase 80's review pass alone fixed eight confirmed defects. These documents
carry the reasoning behind a change, which a test cannot; the regression
record proper is
[the test suite](Emulator-and-Testing#the-test-suite-and-what-it-does-not-cover),
18 suites gating merges. This line used to say the project had
no automated test suite — corrected 2026-10-18.

---

**Next:** [Code Map](Code-Map) · [Emulator and Testing](Emulator-and-Testing) · [Glossary](Glossary)
