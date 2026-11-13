# Phase 156 plan: an unattended live-sky test run, handed to Claude Code

The owner asked for this on 2026-11-13: a phase that runs the automated tests under a live sky and gives the results directly to Claude Code for continued development. It assumes Claude Code is installed, logged in and working on the imaging PC. Status: `PLANNED`.

## The owner's terms
- **Unattended.** The owner starts the run and walks away.
- **Horizon limits.** A target outside the horizon limits or the slew bounds is skipped, not failed.
- **Failures.** A test that fails is recorded and skipped, and the run goes on to the next.
- **It refuses to start** unless the horizon limits, the site and the slew bounds are set.
- **It stops** on clouds, a lost target or a stalled device.
- **It parks** at the end.
- **The checklist is updated** with what passed, and with what failed or was skipped.
- **Claude Code's part** (the default, not yet confirmed by the owner):
  - it analyzes the night, writes a findings report and opens a **draft** pull request;
  - nothing merges without the owner, as today.

## What it builds on
- **The scenario runner (phase 125).**
  - `--scenario`, `tests/run-scenarios.ps1`, `tests/scenarios/*.scn`.
  - Each run gets a sandbox, a `report.txt` and screenshots, using `expect`, `absent`, `press`, `goto` and `tab`.
  - Today it drives the emulator build only.
- **The clear-night checklist** (`CLEAR-NIGHT-CHECKLIST.md`, items 1 to 58) and the **UNVERIFIED register** ([`UNVERIFIED.md`](UNVERIFIED.md), 155.2). Together they say what a night has to measure.
- **The validation mode (155.4, planned).** `validation_<date>.json` holds every solve's scale, the sharpness and seeing distributions, the autofocus sweeps and the exposure. This phase consumes it, so 155.4 comes first.
- **Safety the app already has:**
  - horizon and slew limits, parking and emergency stop;
  - the wedge watchdog (135.3) and the tracker's lost-target detection;
  - Leoben's cloud and dew;
  - the plate solver, the session log, observation records and CoreLog.

## Stages
- **156.0 Sky scenarios on the hardware build.**
  - The runner learns `--sky`: the hardware build, with the real camera, mount and focuser.
  - New verbs:
    - `target <name>` skips the test, as skipped-horizon, when the target is outside the horizon limits or the slew bounds;
    - `require clear` holds for clear sky;
    - `measure <item> <what>` records a value against a checklist item.
  - Each test is a `tests/sky/*.sky.scn`, tagged with the items it settles. The first set:

    | Test | Checklist items |
    |---|---|
    | Plate solve, and the scale it measures | 3, 15 |
    | Real-frame sharpness distribution | 1 |
    | Autofocus with backlash, and the fine pass | 47, 58 |
    | The exposure ladder | 55 |
    | Lock, hand jog, box change | 48, 49, 50 |
    | Throughput at full rate | 13 (and 146.2's indoor figure) |
    | Autorun around a Jupiter event | 57 |
- **156.1 Pre-flight.** It refuses to start, and says which condition failed, unless all of these hold:
  - the site, the horizon limits and the slew bounds are set;
  - the mount is aligned;
  - the camera, focuser and solver answer;
  - the disk has space for the planned recordings.

  It then orders the night's tests by each target's altitude and time.
- **156.2 The supervisor**, which runs alone:
  - **A target outside the limits:** skipped-horizon, and the next test starts.
  - **A test that fails or times out:** recorded with its log excerpt and screenshot, then the next test starts.
  - **Clouds** (Leoben, or the target's signal collapsing): the test stops and is recorded as clouded. The run holds and checks again every few minutes. If the dark window ends first, it parks.
  - **A lost target:** that test fails, and the next starts.
  - **A stalled device** (mount, focuser or camera silent past its watchdog): the run stops, tries to park, and records what stalled.
  - **The end of the list, or the dark window closing:** park, close the camera, write the results.
  - The owner's slew bounds and horizon limits are hard limits. The supervisor never widens them.
- **156.3 The results bundle**, in `field-results/<date>/`:
  - `results.json`, per test: the status (`passed`, `failed`, `skipped-horizon`, `clouded`, `aborted-stall`, `not-reached`), each measured value against the expected one, and the checklist items;
  - `summary.md`, readable by a person;
  - the log excerpts, the screenshots and the night's `validation_<date>.json`;
  - no recordings, which are too large: only their paths and their records.
- **156.3b The checklist, updated with every result.** The owner asked for this on 2026-11-13. It does not depend on Claude Code.
  - `tools/sky_checklist_update.py` (standard library, offline) reads `results.json`. It writes one status line under each item the night touched, replacing that item's previous line, so a re-run never stacks duplicates. For example:
    - `**Sky run 2026-11-20: PASSED** (plate solve 0.204"/px against 0.203 derived)`
    - `**Sky run 2026-11-20: FAILED** (autofocus ended +310 from the coarse peak; log excerpt in field-results/2026-11-20)`
    - `**Sky run 2026-11-20: SKIPPED, below the horizon limit** (Jupiter at 8°)`
    - and likewise for clouded, aborted on a stalled device, and not reached before the dark window closed.
  - A table at the top of the checklist, "Last sky run", lists every item the night tested and its status. Passed, failed and skipped read at a glance.
  - A passed item is marked, not deleted. Removing an item and its UNVERIFIED tags stays a reviewed change, in the pull request.
- **156.4 The handoff to Claude Code.** At the end of the run, `tools/sky_handoff.ps1`:
  1. updates the local clone from `master`, and makes a branch `field/<date>`;
  2. copies in the bundle, runs 156.3b, and commits both. The checklist is updated whatever happens next;
  3. starts Claude Code headless in that clone, with a prompt kept in the repository (`tools/sky_handoff_prompt.md`). The prompt asks it to:
     - read the bundle;
     - compare each measured value with the UNVERIFIED register and the checklist;
     - write `field-results/<date>/findings.md`;
     - propose register changes where a value is confirmed or replaced;
     - make only small fixes the night's evidence clearly supports;
     - push the branch and open a **draft** pull request with the findings as its body;
  4. never merges;
  5. if Claude Code is not logged in or fails, still pushes the bundle and the updated checklist, and logs that the analysis is owed.

  The flags for a headless run are confirmed against the installed Claude Code's `--help` when this is built. Its print mode, `-p`, is recalled, not verified here.
- **156.5 The dress rehearsal, in CI.** The same sky scenarios run against the emulator (`--sky --emulate`): the skips, the cloud hold, the stall stop and the park are tested on every push, not first under the sky. This needs 153.0's headless harness.

## Rules
- **Rule 7, airplane mode.** The run itself needs no internet. Leoben is on the LAN. If there is no connection, the handoff commits locally and pushes when one returns.
- **Phase 149's rules.** Nothing in the sky tests names a camera model, resolution or plate scale. The expected values come from the rig and the register.

## Order
Release 3.4. After 155.3 to 155.5 (the validation mode it consumes) and 153.1 to 153.2 (the controllers it drives). 156.5 can start as soon as 153.0's harness exists.

## Versioning
Every pull request bumps PATCH. MINOR marks a milestone release (3.4 for this phase), not a phase ([`ROADMAP-Next.md`](ROADMAP-Next.md#next-in-order)). Suggested PRs:
1. 156.0 with 156.5;
2. 156.1 with 156.2;
3. 156.3 to 156.4.
