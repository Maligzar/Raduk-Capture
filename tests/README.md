# Tests

Bench tests. No camera, no mount, no sky — they run anywhere the code
builds, which is what lets them gate a merge.

## Running them

```bat
tests\run-tests.bat
```

Exits **0** if everything passed and **non-zero** if anything failed, so
it can be used as a gate:

```bat
tests\run-tests.bat && build_all.bat
```

## Running them from Linux, with caveats

The gate is MSVC and `run-tests.ps1`. That is the only run that counts.
But most of these tests can also be built with mingw-w64 and run under
wine, which is worth knowing because it turns "written, not built" into
something closer to evidence when the author is not on the Windows box:

```bash
sudo apt-get install -y mingw-w64 wine64
x86_64-w64-mingw32-g++ -std=c++20 -O1 -static -static-libgcc -static-libstdc++ \
  -I src -I external/ASI_SDK/include \
  -o out/<name>.exe tests/<name>.cpp <extra sources> \
  -lole32 -loleaut32 -lws2_32 -lbcrypt -lwindowscodecs -luuid
WINEDEBUG=-all wine out/<name>.exe
```

**What it does not prove.** mingw is not MSVC: it accepts and rejects
different things, so a green run here is evidence and not a gate.

With one harness-only shim (below), 25 of 27 suites build and pass this
way. The two that fail do so for the environment, not the code:
`object_knowledge` wants its database and `ai_context_facts` wants the
project's docs and session logs, and with no `D:` drive present `Paths`
resolves beside the executable where neither exists. Both pass on the rig.

**The shim.** mingw's CRT does not export `_get_timezone`, which MSVC does
and which anything pulling `RigConfig` reaches through `<ctime>`. That
blocked `ai_context_facts`, `mount_jog`, `leoben_weather` and `push_to`
from linking. A zero-offset stub, kept OUT of this repository because it
exists only to work around a toolchain gap, unblocks all four.
(`pixel_wcs`, added with phase 47b, does not need it: `PixelWCS.h` and
`SolveResult.h` deliberately pull in nothing but `<cmath>`, `<cstdio>`
and `<string>`, which is what lets the convention-sensitive arithmetic
be tested at all.)

```cpp
extern "C" int   _get_timezone(long* t) { if (t) *t = 0; return 0; }
extern "C" void* __imp__get_timezone = (void*)&_get_timezone;
```

Only safe for tests whose assertions do not depend on the local timezone.

Other link libraries worth knowing: `-lbcrypt` for `ws_server_loopback`,
`-lwindowscodecs -luuid` for `preview_frame`, and `-lwinmm` for anything
pulling `CameraEmulator.h`, because mingw ignores the
`#pragma comment(lib, ...)` that MSVC honours.

It did earn its keep once: building under a stricter toolchain is what
found `AIContextBuilder.h` using `uint32_t` with no `<cstdint>`, which
MSVC had been hiding with a transitive include.

## Using them as a merge gate

Nothing should merge to `master` with a failing test. The runner's exit
code is the whole contract — a test's `main()` returns 0 on success and
non-zero on failure, and the runner propagates that.

This matters more here than in most projects. A planetary rig gets a
small number of usable nights a year, and the failures that hurt are the
quiet ones: a readout that is confidently wrong, a recording that writes
a 0-byte file while the UI reports it working. Both have happened. A
test that always reports success is worse than no test, because it is
believed.

## Adding one

1. Drop a `.cpp` in this folder with a `main()` returning 0 / non-zero.
2. Add one line to `$Tests` in `run-tests.ps1`, naming any extra source
   files it needs to link.

That is all — the runner finds the compiler, builds into `_build\`, runs
it and reports.

## What does NOT belong here

Anything needing hardware or sky. Those cannot gate anything, because
they cannot be run on demand. They belong in
[`docs/CLEAR-NIGHT-CHECKLIST.md`](../docs/CLEAR-NIGHT-CHECKLIST.md),
which exists precisely to keep that distinction honest.

## Current tests

Fourteen of the suites in `run-tests.ps1` are described here, all
gating. Each runs on the bench with no camera, mount or sky attached.

| Test | Covers |
|---|---|
| `histogram_accuracy` | Bayer channel assignment, what the G channel contains, clip percentages in 8- and 16-bit, left-aligned RAW16 handling, sampling coverage. Findings in [`docs/HISTOGRAM-ACCURACY.md`](../docs/HISTOGRAM-ACCURACY.md). |
| `adc_alignment` | That the dispersion the ADC alignment window reports is the displacement actually in the frame: a known shift comes back, sub-pixel residuals resolve, direction is right, and — the one that matters — a banded planet with no dispersion reads zero rather than inventing a residual out of its cloud belts. |
| `sensor_analysis` | The numbers the averaged histogram cannot show: Gr/Gb split measured and judged, per-channel headroom in stops, which channel limits the exposure, and that clipping reports a fraction rather than an invented stop count. |
| `write_buffer_sizing` | That the write buffer is sized to the run and to the machine rather than to a flat 512 slots, never drops below the old floor, and never takes more than half of free memory. |
| `sky_conditions` | Airmass against Kasten-Young, the resolution cost of altitude, the imaging window thresholds, and that a parked mount gets no projection rather than a confident wrong one. |
| `capture_thread_submit` | That recording runs off the capture thread rather than the render loop: the frame limit flags without stopping, the quality gate skips rather than drops, and submission sustains far more than the old 62 fps ceiling. |
| `ai_context_facts` | What the assistant is told about the rig — pixel pitch against plate scale, the live capture format, and the rule that the observing site never reaches the prompt at more than one decimal. |
| `object_knowledge` | That the SQLite knowledge base is reachable at all (it fails *gracefully* when the DLL is missing, which is how an unread 2.4 MB database hid for months), and that names resolve across catalogue forms. |
| `object_panel_text` | The sentences the object panel writes: light travel time, frame fill, brightness against the naked-eye limit, and the fields it refuses to print rather than printing nonsense. |
| `slew_variable` | The variable-rate slew encoding, including the integer overflow that made an absurd rate encode as *stop*. |
| `planet_apparent_size` | Apparent diameter and Earth-planet distance for all seven planets, swept across years and checked against published ranges — a wrong constant or a confused unit shows up as a range violation. |
| `profile_notes` | That a SAVE cannot erase a profile's stored note, including the `saturn.ini` case that actually happened, while still allowing a deliberate clear. |
| `recentre` | Click-to-recentre: the pixel→pulse solve, its refusals, and a round trip that pushes the solved pulses back through the measured matrix for square, rotated, mirrored and skewed cameras. |
| `observation_record` | Phase 48. The record beside each capture round-trips every field; an unknown value stays unknown instead of becoming 0; the reader survives CRLF, comments, a newer version and a forged newline. Recall: a named target goes where the catalogue says it is now, and every refusal refuses — no mount, unaligned, no site, under 15°, near the Sun. Framing restore lands the target on its recorded pixel for square, rotated, mirrored and skewed cameras. |

## Manual harnesses

`tests\manual-*` are **not** in `run-tests.ps1` and are not meant to be.
Each needs something a clean checkout does not have — hardware, a real
recording, third-party software, or a running app — and a gate that
cannot run on demand is not a gate. Run them by hand when the question
they answer comes up.

| Harness | Answers |
|---|---|
| `manual-autostakkert-launch.cpp` | Does the app's own launch path actually stack a file? Drives `AutoStakkertRunner` exactly as `main.cpp` does. Needs AutoStakkert installed and a recording. |
| `manual-doc-truth.ps1` | Do the planning documents still say true things? Unresolvable commit citations, a section heading arguing with its own body, one phase described two ways in two files, dangling cross-references, and source files missing from the code map. Reads prose, so it reports things that are fine and misses things that are not — read each finding before believing it. |
| `manual-emulator-blur-equivalence.cpp` | Is the retiled defocus blur the *same* blur? Checksums before and after. |
| `manual-emulator-defocus-cost.cpp` | What does the emulator's defocus cost per frame? |
| `manual-emulator-delivery-rate.cpp` | What frame rate does the emulator actually deliver? |
| `manual-goto-layout-bounds.ps1` | Does the GoTo panel lay out inside its window? |
| `manual-night-vision-coverage.ps1` | How much blue is left on screen with night vision on? Measured, not asserted. |
| `manual-record-throughput.ps1` | What does the write path sustain, in MB/s, with how many drops? |
| `manual-window-coverage.ps1` | Do all the windows land on the screens they are meant to? |
