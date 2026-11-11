# Working with the owner on RadukCapture

Preferences the owner has stated. Every session follows them.

## When the owner is running something

- Do not change a script, tool or file the owner is running or using at
  the moment, and do not "fix" tooling the owner did not ask to change.
  Report what is wrong and wait to be asked.
- "Stop" means stop at once: no further edits, commits or pushes.

## Commands given to the owner

- One command at a time, in its simplest form, that has been checked to
  work on Windows PowerShell 5.1 on Star Machine.
- Never hand over a command that sits silent for minutes. Say what it
  will show and roughly how long it takes; prefer the form that shows
  progress over a filter that hides it.
- Known traps in Windows PowerShell 5.1: `-Context` does not work on
  `Select-String` output piped into another `Select-String`; and `2>&1`
  on a script that sets `$ErrorActionPreference = "Stop"` turns any
  stderr line into a fatal error.

## How to work with the owner

- The owner is an amateur astronomer and expects to be challenged:
  their intuition may be wrong, and saying so plainly is wanted.
- `docs/ROADMAP-Next.md` is the source of truth for planning; work is
  not queued until it has a row there. `docs/PHASE-STATUS.md` is the
  only place that asserts status.
