# CI on Star Machine

**Optional, revised 2026-11-11.** The build-and-test workflow
(`.github/workflows/build.yml`) runs on GitHub's `windows-latest` for every
pull request and push to master. For this public repository those runs
are free: the owner's billing page that evening showed RadukCapture's
Actions usage of $13.37 wholly discounted, $0 billable, and the evening's
slow starts were GitHub's queue, not a limit. Star Machine, the owner's
own PC, is the manual alternative: **Actions → Build and test → Run
workflow → runner: star-machine**, once a self-hosted runner with that
label is installed. Nothing else is sent to it.

## Why, and the one thing that must be set first

This repository is public. Two things follow.

- **GitHub's own runners are free for a public repository** on the
  standard machine sizes; the owner's billing page confirmed it for this
  repository (above).
- **GitHub advises that self-hosted runners should almost never be used
  with public repositories**, because anyone can open a pull request from
  a fork, and a fork can change the workflow file to run its own code on
  the runner, here on Star Machine. The workflow sends only manual runs
  there, but a fork can edit the workflow too, so that is not the
  protection. **The protection is a repository setting**, and it must be
  on before a runner is installed:

  GitHub → this repository → Settings → Actions → General → the section on
  fork pull request workflows → require approval for **all** external
  contributors. (Wording recalled; GitHub renames these settings from time
  to time.)

  With it on, nothing from a fork runs until the owner approves it, and
  the owner should never approve one that touches `.github/` or that
  they have not read.

## Installing the runner

On Star Machine:

1. GitHub → this repository → Settings → Actions → Runners → **New
   self-hosted runner** → Windows, x64. GitHub shows a short list of
   PowerShell commands: make a folder (for example `C:\actions-runner`,
   not inside `D:\RadukCapture`), download, check, and configure. They
   carry a one-time token: run them as shown and do not paste the token
   anywhere else.
2. When the configure step asks for additional labels, enter
   `star-machine`. Accept the default runner group and name.
3. When it asks whether to run as a service, say yes, so it starts with
   Windows and needs nobody logged in.
4. Back on the Runners page, the runner should show **Idle**.

What it needs is what building by hand needs: Visual Studio with the
Desktop development with C++ workload (the tests find it through
`vswhere`, as `tests/run-tests.ps1` always has), CMake (found on the PATH,
in `D:\Program Files\CMake`, in `C:\Program Files\CMake`, or Visual
Studio's own copy), and Git for the checkout.

## What a run does on Star Machine

- It checks the code out into the runner's own folder, builds the
  emulator and hardware configurations there, and runs the bench tests,
  the same `tests\run-tests.ps1` as by hand. It does not touch the
  working copy in `D:\RadukCapture`.
- It does read the real data folder the way the app does: `Paths` finds
  an existing `D:\RadukCapture`, so `tests/object_knowledge.cpp` opens
  `D:\RadukCapture\data\object_knowledge.db`. Running the tests by hand
  has always done the same.
- A running RadukCapture does not get in its way: the build is in a
  different folder from the app's.
- While Star Machine is off or the runner is stopped, runs queue. GitHub
  gives up on a run that has queued for about a day (recalled).

## Spending fewer runs

- A new push to a pull request cancels the run still going for the one
  before it.
- Changes only to `README.md`, `LICENSE`, `research_notes/`, `reports/`,
  `wiki/` or `.gitignore` do not start a run: no test reads them.
  `docs/` still does, because `tests/ai_context_facts.cpp` scans it.

## Why GitHub's machines stay the default

They run whether Star Machine is on or not, they are a stock machine (no
`D:` drive, no `rig.ini`, no hardware), which is the check that a fresh
clone builds for anyone else, and no pull request from a fork can reach
the owner's PC through them.
