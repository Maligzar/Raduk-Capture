# Phase 125 — the app tests itself

Asked for by the owner on 2026-11-06. Checks in the running app had become a
list of steps for the owner, and the list grew faster than anyone could work
through it. The goal is for Claude to test the app, not the owner.

An earlier attempt (phase 48) drove the app from outside, through window messages
and the WebSocket. It cost a lot, and once started a second instance that made an
8 GB recording. This phase runs the script from inside the app instead.

## How it works

```
build_emu\Release\RadukCapture.exe --scenario tests\scenarios\autorun.scn
.\tests\run-scenarios.ps1            # all of them
.\tests\run-scenarios.ps1 autorun    # those whose name contains it
```

The emulator build reads the script, runs it against the live app on the UI
thread, writes `report.txt`, closes itself and exits 0 on a pass.
`tests\run-scenarios.ps1` runs each scenario, prints a table, and deletes the
recordings afterwards.

**What a step can see** (`src/core/TestProbe.h`):

- every `Log` line, and the session log as it is written (`[SESSION]`);
- notification-bar messages (`[NOTICE]`);
- panel status lines (`[STATUS]`);
- rail, toolbar and PLANET LOCK labels (`[BUTTON] RUNS -> #1 (lit)`).

**What a step can do:**

- `press` a button by its label;
- send a `key`;
- `goto` a target;
- open a `tab`;
- `wait`;
- take a `shot` of every window to PNG. That is how Claude looks at layout and
  text fit.

**Checks:**

- `expect S TEXT` waits for a line since the last action, in any order. Each
  line counts once.
- `absent S TEXT` fails if the text appears.

**Setup lines:**

- `rig` writes into the sandbox's rig.ini.
- `env` sets environment variables.
- `profile` edits the sandbox's copy of a profile, so a test records 150 frames
  at 640x480 instead of 12 GB.

## Never touching the owner's things

Each run gets `test_runs\<scenario>_<time>\`, with its own logs, captures, data
(the pointing map included), profiles, rig.ini and window layout. Session stats
never see it. The WebSocket port stays closed. A scenario refuses to start
beside a running copy, because the two would share the log. `RigConfig` reloads
from the sandbox: something reads it before `WinMain`, and the first version
saved the owner's values over the scenario's.

## What the scenarios found

These are real defects in the app, each now fixed and checked by the scenario
that found it.

1. **PLANET LOCK after a GoTo did nothing.** A GoTo leaves a plain search
   running. Pressing PLANET LOCK then only lit the button, because the tracker
   was already "running". The planet was never calibrated, centred or locked.
   Now the press turns the search into the full sequence, after the same mount
   checks a fresh start makes.
2. **The OUTPUT tab's values were lost whenever that tab was closed.** Since
   phase 120 its controls exist only while it is open, and the getters read only
   the controls. From any other tab, every recording took 1000 frames whatever
   the profile said, and was named `Capture_` instead of `Jupiter_`. A sequence
   planned 50 s runs from the wrong limit. The values were cached all along;
   the getters now use the cache.
3. **Escape left PLANET LOCK lit.** The emergency stop reset the tracker, but
   the button still read TRACKING. Phase 102 fixed this for the panel's STOP
   and not for Escape.

Two more findings are not fixed here:

- **The Moon cannot be locked when it fills the frame** (checklist 33). Guide
  calibration repeats "North response too small", because pulsing the mount
  barely moves the brightness-weighted centre of a crop that is all surface.
  Surface following only starts after a lock, so it never starts.
  `_known-surface-follow.scn` records this. It needs its own phase.
- **The live preview is black in screenshots.** Its swap chain is not captured
  by `PrintWindow`. Every other window is.

## The scenarios

| Scenario | Checks | Checklist |
|---|---|---|
| `smoke` | Starts in the sandbox, every window captured, STATS writes its page, closes | — |
| `autorun` | Lock, autofocus, two runs of exactly 150 frames named `Jupiter_`, the gap, the end line, the ranked list, then RUNS stopping one partway | 36 |
| `seeing-hold` | A 100% bar: the sequence waits, goes when the sky is at its best, gives up after the hold, ranks the runs | 37 |
| `recording` | From CAMERA and from OUTPUT: target naming, the profile's limit, the derotation notice, the WinJUPOS rename | 28, 32 |
| `buttons` | POINT SOLVE always answers; PLANET LOCK after a GoTo locks; STOP and Escape both drop the lock | 23, 24 |
| `autofocus` | AE settles first, holds during the sweep and stays on; an abort during settling stops cleanly | 35 |
| `_known-surface-follow` | The Moon on a drifting mount. Fails, as described above | 33 |

`tests/scenario_runner.cpp` gates the script reader and step logic on a fake
clock, and is part of `tests/run-tests.ps1`. The scenarios themselves are not:
they take minutes and open windows. Claude runs them after each phase.

## What they cannot cover

Anything that needs the real camera, mount or sky stays on the clear-night
checklist. Typing into the camera boxes (part of item 35) has no step yet. The
recording timer (28) is painted, not logged, so only a screenshot shows it.
