# Getting Started

Two build configurations from one CMake project. The emulator build needs no
hardware and no SDKs, so you can do real development indoors with the
telescope packed away.

---

## Prerequisites

| Requirement | Notes |
|-------------|-------|
| Visual Studio with MSVC, C++20 | `set(CMAKE_CXX_STANDARD 20)` is enforced, not optional |
| CMake ≥ 4.2 | `cmake_minimum_required(VERSION 4.2)` |
| Windows SDK | Direct2D, DirectWrite, Direct3D 11, WIC, Winsock2, WinHTTP |
| ZWO ASI SDK | **Hardware build only.** Already vendored in `external/ASI_SDK` |
| ZWO EAF SDK | **Hardware build only.** Already vendored in `external/EAF_SDK` |

Optional, for features that degrade gracefully when absent:

| Optional | Enables | Absent behaviour |
|----------|---------|------------------|
| [ASTAP](https://www.hnsky.org/astap.htm) + star catalog (H18 or D80) | Plate solving and mount sync | Solve requests fail with a clear message |
| [Ollama](https://ollama.com) on `localhost:11434` | The AI assistant (`W` and the chat panel) | Chat panel reports no local model |
| `sqlite3.dll` beside the exe | Rich object knowledge blocks for the assistant | `ObjectKnowledgeDB` returns `""` gracefully; no compile-time dependency |
| AutoStakkert! 4 | `K` hands a finished recording off for stacking | Button reports it could not find AS |

---

## Build

### Emulator build — no hardware, no SDKs

```bat
cmake -S . -B build_emu -DRADUK_EMULATOR=ON
cmake --build build_emu --config Release
build_emu\Release\RadukCapture.exe
```

`RADUK_EMULATOR=ON` does three things: it defines the `RADUK_EMULATOR`
preprocessor macro, it drops `ASICamera.cpp` / `EAFFocuser.cpp` /
`NexStarMount.cpp` from the source list, and it links without the ZWO
import libraries. `DeviceFactory.h` then resolves `ASICamera`,
`EAFFocuser`, and the mount to the emulator classes at compile time — see
[Device Layer](Device-Layer).

### Hardware build — requires the SDKs

```bat
cmake -S . -B build
cmake --build build --config Release
build\Release\RadukCapture.exe
```

The hardware build links `ASICamera2.lib` and `EAF_focuser.lib` from
`external/`, and a `POST_BUILD` step copies both DLLs next to the
executable.

### Convenience scripts

| Script | Does |
|--------|------|
| `build_all.bat` | Builds **both** configurations |
| `build_emulator.bat` | Emulator configuration only |
| `launch_emulator.bat` | Builds if needed, then launches the emulator build |
| `kill_radukcapture.bat` | Force-terminates a hung instance |

> **Why `build_all.bat` exists.** Phase 80 found the hardware build had
> been silently broken for four phases while all development happened in
> the emulator configuration. Both configurations now build from one
> script, so a break in either is caught immediately. If you only ever run
> one of them, you will eventually ship the other one broken.

---

## Things the build does that are not obvious

**`/utf-8` is mandatory on MSVC.** Without it, Unicode characters in string
literals corrupt (`Â×`, `Â·`) when the system codepage is not UTF-8. The
UI is full of degree signs, arcsecond marks and box-drawing characters.

**The help file is copied on every build, by a separate target.**
`AIContextBuilder` loads `RadukCapture-Help.md` as the assistant's entire
application reference, looking next to the exe first. A `POST_BUILD` step
on the executable only fires when the exe *relinks*, so editing the help
alone left the deployed copy stale — and that stale copy is the one the
assistant actually reads. `add_custom_target(copy_help ALL)` runs
regardless of whether any source changed.

**Data directories are created at build time.** `D:/RadukCapture/captures`,
`/logs`, and `/profiles`. If your drive layout differs, change the output
directory in the capture config; see
[Configuration Reference](Configuration-Reference).

**The version is declared in four files and they must stay in step.**
`src/Version.h` is the authoritative one — the title bar shows
`RADUK_VERSION_A` plus the compile timestamp of `main.cpp`, so a stale exe
is immediately obvious. `CMakeLists.txt` and the two `.rc` files carry it
too. All four read **1.2.3**; see
[Roadmap and Phase History](Roadmap-and-Phase-History#where-the-version-is-declared)
for what to touch when bumping it.

---

## First launch

### Hardware build

`main.cpp` runs `CheckHardware()` before creating any window. If any of the
three devices is missing, a startup dialog asks whether to launch in
emulator mode instead. **No mixing of real and emulated devices is
permitted** — it is all three or none, so a test result is never ambiguous
about what it was testing.

Startup order, from `RunApp()` in
[`src/main.cpp:515`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/main.cpp#L515):

```
1. LogInit()                    raduk_log.txt, crash-safe
2. SessionLog::Open()           dated session log beside captures
3. ProfileManager::Init()       8 built-in profiles, written if absent
4. ASICamera::Enumerate()       hardware build only
5. RigConfig site resolution    rig.ini wins; only an unknown site asks Windows
6. Camera open + format restore last session's ROI, bit depth, camera state
7. EAF open
8. Mount connect                SerialPortScan probes for the NexStar echo
9. Ring buffer + capture thread
10. Analyzer, tracker, telemetry server, capture manager
11. Window, control panel, toolbar, panels
12. Readiness check             shown only on a failure
13. Render loop
```

### Emulator build

Launches straight into the render loop with a simulated planet. Worth
knowing before you judge what you see:

- The emulated camera **reports** the real 3840×2160 sensor but will only
  **stream up to 1280×960**. Larger requests are clamped and logged, the way
  a real camera reports a mode it cannot deliver. So `FULL` and `1920` both
  land on 1280×960 in the emulator, while `1280` and `640` are exact.
- The simulated EAF opens at **166,500** — 1,500 steps short of the 168,000
  default true focus, so the image starts slightly soft and an autofocus
  run has something real to do.
- By default every simulated frame is equally sharp and noise-free. Set
  `RADUK_EMU_SEEING_AS` and `RADUK_EMU_NOISE` before testing anything that
  depends on frame-to-frame quality variation — the seeing score, the
  capture gate, and the [quick-look stacker](Image-Analysis) all have
  nothing to choose between without them.

Full switch list: [Emulator and Testing](Emulator-and-Testing).

---

## Minimum configuration before a real night

`rig.ini` sits next to the executable, is **not tracked by git**, and is the
only place your observing site is stored. It is created on first save and
defaults to latitude/longitude 0,0 until you set it.

```ini
[Rig]
SiteLatDeg=0.0
SiteLonDeg=0.0
SiteElevationM=0.0
PlateScaleArcsecPx=0.203
GuideRateArcsecPerSec=7.5
; The filter physically fitted to the camera. Not detectable --
; the dispersion model computes across this band, so a wrong
; value moves the ADC figure by up to 4x.
FilterName=UV/IR cut
FilterLowNm=400
FilterHighNm=700
```

Three of these bias results and cannot be detected by the software:

| Key | Gets it wrong by |
|-----|------------------|
| `SiteLatDeg` / `SiteLonDeg` | Breaks alt/az maths, the sky map, and GoTo entirely |
| `SiteElevationM` | Skews the [ADC](Advisors) pressure model — ~10% high at sea level, ~15% low on a mountain |
| `FilterLowNm` / `FilterHighNm` | Moves the ADC dispersion figure by up to 4× |

Since Phase 77 the three biasing values report themselves rather than
sitting silent. The site can also be auto-detected from Windows Location,
GPS, or the mount, and `AdoptSite()` **refuses to overwrite a site marked
`manual`** — a surveyed coordinate must not be replaced by a WiFi estimate.

Full key reference: [Configuration Reference](Configuration-Reference).

---

**Next:** [Architecture Overview](Architecture-Overview) · [Operations Guide](Operations-Guide) · [Emulator and Testing](Emulator-and-Testing)
