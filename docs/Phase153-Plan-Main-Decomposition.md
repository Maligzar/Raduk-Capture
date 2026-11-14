# Phase 153 plan: taking the wiring out of main.cpp

From the owner's weakness review, 2026-11-13. Status: `PLANNED`.

## The problem
- `src/main.cpp` is 9,047 lines (2026-11-13) and holds nearly all the callback wiring: AE before autofocus, autofocus's fine-pass feed, Autorun, recording start and stop, observation records, Jupiter's events, the assistant's snapshot, the dock, the dashboard commands.
- The architecture document calls it the largest risk in the tree. Most `owes:` clauses in PHASE-STATUS come down to wiring there.
- **No test compiles it.** CI builds it, and the scenario runner (phase 125) drives it in the emulator build, but only along the paths a scenario takes.
- The rest of the code is testable because it is pure: `AutorunRunner`, `FocusDirection.h`, `JupiterPlan.h`. main.cpp is where those pure pieces meet the UI and the devices, untested.

## The approach: an application core, extracted by strangling

The owner's review (2026-11-13) recommends an `AppController` that takes the device and writer interfaces. main.cpp then shrinks to four jobs: parse the config, construct the real backends, construct the core, and run the render loop. With that, the emulators and a fake clock can drive the **whole application** headless, not just the runners. Adopted, with one caution: a single `AppController` holding everything main.cpp holds today would move the god object instead of removing it. So:
- **`AppCore`** is the composition root and the headless entry point. It owns the controllers below and the fake-able seams (a clock, a logger, a UI sink), and nothing else.
- **The controllers** hold the behaviour, each small enough to test alone.
- **153.0 comes first:** `AppCore`, plus a headless harness (`tests/app_headless.cpp`). It builds `AppCore` with the emulators, a fake clock and a recording UI sink, and runs a whole night in seconds: connect, GoTo, lock, autofocus, record, stop. At first the controllers are thin wrappers that call the existing main.cpp code; each later stage moves the real logic in behind a harness that already exists.

A rewrite of 9,000 lines of working, field-used wiring is the riskiest change this project could make. So it is strangled out instead:
A rewrite of 9,000 lines of working, field-used wiring is the riskiest change this project could make. Instead:
- **Controllers.** Each subsystem's wiring moves into a class in `src/app/` that takes its dependencies as interfaces (`ICamera`, `IFocuser`, `IMount`, a clock, a logger, a small UI sink) and owns its state. main.cpp constructs the controllers and connects them, and nothing else.
- **A test per controller.** Each one is compiled in `tests/` against fakes: the emulator devices already exist, plus a fake UI sink that records what would have been shown.
- **One controller per PR.** Each PR keeps behaviour identical, proved by the controller's new test plus the scenario runner's existing scenarios passing before and after.

## Order (by how much `owes:` sits behind each, and how often it changes)
1. **153.1 `AutofocusController`**: AE settling before the sweep, the fine pass's 30 Hz edge feed, readiness, and the Moon path. Phase 145 just added wiring here, and it has never run on the sky.
2. **153.2 `RecordingController`**: start and stop, `endRecording`, `onComplete`, observation records, the DeTeCt and submission lists, auto-stack.
3. **153.3 `AutorunController`**: the sequence wiring round `AutorunRunner`, including Jupiter's deferral.
4. **153.4 `JupiterServices`**: `J`, the disc line, and the assistant's Jupiter snapshot.
5. **153.5 `TrackingController`**: LOCK, GoTo hand-backs, remote stops.
6. **Later, as touched:** the dock, the dashboard commands, the mutual-event recording.

## 153.0, as built (2026-11-13)
- **`src/app/AppCore.h`** owns the devices and the frame pipeline, in RunApp's order: camera, focuser, autofocus, AE, the mount proxy, the ring, the capture thread, the analyzer, the tracker, the capture manager.
  - `main.cpp` builds one and binds references under the old names, so the rest of RunApp is unchanged. Their configuration and starting order stay in `main.cpp` for now.
- **The seams.**
  - `src/app/Clock.h` holds `IClock`, `SteadyClock` and `ManualClock`.
  - `src/app/UiSink.h` holds `IUiSink`, plus a null sink and a recording sink. `main.cpp`'s sink sends the core's lines to the log; status and toasts are hooked as controllers start to use them.
- **The first behaviour moved.** Autofocus's two feeds, the fine pass's edge score and the coarse sweep's 1000/HFD, now live in `src/app/AutofocusFeed.h`. It is pure and tested by `tests/app_feed.cpp`, and runs on the core's clock.
- **`tests/app_headless.cpp`.** It builds the same `AppCore` on the emulators and runs in groups (`connect`, `focus`, `record`, or `all`), so a change runs its own group. **Real time, not a fake clock, for now:** the emulators, `AutofocusRoutine`, `CaptureManager` and `CentroidTracker` sleep and read `steady_clock` on their own threads. The clock reaches each one as its controller is extracted, and the harness gets faster with it. GoTo and lock join at 153.5.
- **CI cannot run the scenarios (tried 2026-11-13/14).** On GitHub's Windows runner the emulator exe hung for 150 s before it made its scenario sandbox or wrote a line of log: early start-up (DPI awareness, the bundled fonts, or scenario set-up) stalls without an interactive desktop. The step was removed. The before-and-after scenario gate for each extraction runs on the imaging PC (`tests\run-scenarios.ps1`), and `app_headless` is CI's check.

## 153.1, as built (2026-11-14)
- **`src/app/AutofocusController.h`** holds what main.cpp did round `AutofocusRoutine`:
  - the button's NOT NOW and press-twice confirm (139.3);
  - starting with AE set to AUTO, and the sweep held until AE settles, a recording starts, or the settle limit passes (112);
  - the Moon path (145.3);
  - abort, and the quiet cancel the emergency paths use;
  - the completion messages.

  It is a template over the routine and AE, so `tests/af_controller.cpp` drives it with fakes on a manual clock. The panel and the session log are reached through `AutofocusHooks`.
- **`AppCore` owns it** (`core.afCtl`). main.cpp's lambdas, `afWaitingForAE`, `afWaitStart` and `afConfirmUntil` are gone; every "is autofocus busy" test reads `core.afCtl.Busy()`.
- **`app_headless focus`** now starts and finishes through the controller, on emulated frames.

## 153.2, as built (2026-11-14)
- **`src/app/RecordingController.h`** holds what main.cpp did round `CaptureManager`:
  - `Begin`: start, USB throttled, the derotation window announced;
  - `End`: the one stop, USB restored;
  - `Service`, every frame: the quality gate and the writer's hints published, the disk stop, the limit and the derotation stop acted on, and the window warning once;
  - `Complete`: whether a sequence must stop, and whether to auto-stack. It is a decision; Autorun and AutoStakkert act on it in main.cpp.
- **The derotation window's state** lives in the controller. main.cpp still works the window out, from the GoTo panel and the ephemeris.
- **Tests.** `tests/rec_controller.cpp` drives it with a fake capture manager. `app_headless record` starts, services and finishes through it.

## 153.3, as built (2026-11-14)
- **`src/app/AutorunController.h`** holds what main.cpp did round `AutorunRunner`:
  - `Start`: reset the sequence's state and the seeing monitor.
  - `Tick` from the readout:
    - the runner's slow inputs, only when it is about to check;
    - its action carried out (focus, a run through the RECORD path, the one stop, an abort);
    - each run noted with file, frames, seeing score and Jupiter note;
    - the seeing hold and the Jupiter deferral announced once a change;
    - the toolbar kept in step;
    - at the end, the summary, the ranked list and the notice.
  - `Pressing` tells the sequence's own RECORD press from a person's.
- **`AppCore` now owns the seeing monitor as well** (`core.seeing`). main.cpp's nine sequence variables are gone; Jupiter's disc line and the deferral wording stay in main.cpp as hooks.
- **`tests/autorun_controller.cpp`** drives it on the real `AutorunRunner` and `SeeingMonitor`, with the rig faked behind the hooks. It covers a whole sequence, a refused run, an abort mid-run, a first-check refusal, a failed sweep, the prime deferral and the seeing hold.

## A rule from now on
A phase that adds wiring adds it to a controller, not to main.cpp, from the moment that subsystem's controller exists. PHASE-STATUS records main.cpp's line count at each extraction.

## The headless harness and the scenario runner
The scenario runner (phase 125) drives the real executable through its UI, in real time. The harness drives `AppCore` directly, on a fake clock, in seconds, and runs in CI on every push. The scenarios stay as the end-to-end check, and each one that `AppCore` can express is mirrored in the harness.

## Measures of done
- main.cpp under about 2,500 lines: construction and connection only.
- Every controller has a bench test.
- The scenario runner passes unchanged.

## Versioning
Every PR bumps PATCH; the PR that completes the phase bumps MINOR ([`ROADMAP-Next.md`](ROADMAP-Next.md#next-in-order)). About six PRs.
