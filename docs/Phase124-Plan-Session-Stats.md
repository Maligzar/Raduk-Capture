# Phase 124 — every session at once (feature 19)

Chosen by the owner from the list left after phase 123. Three things already
recorded each night, and nothing read them back together. The new `[STATS]`
button on the rail reads them all, writes one page, and opens it in the browser.

## What it reads

| Source | What it gives |
|---|---|
| `logs\session_*.log`, one per launch | Rig or emulator, time the app was open, every GoTo and lock, lost locks, emergency stops, seeing every few seconds, and each recording's frames, drops and size |
| `<capture>_observation.txt` (phase 48) | Target, altitude, exposure, gain and the exact duration |
| `data\pointing_model.csv` | Every plate-solved pointing error, and phase 123's verdict |

A recording in a log is matched to its observation record by file name. That
settles the target for files named `Capture_...`, and the duration where a log
said `0.0s`.

## What the page shows

For the rig and the emulator separately:

- **Totals:** nights, sessions, hours open, time recorded, recordings, data
  written, frames dropped, the share of GoTos that locked, and median GoTo-to-lock time.
- **By target:** time recorded, runs, nights, frames, data, altitude range,
  and the usual exposure and gain.
- **By night,** newest first: hours open, time recorded, runs, data, mean and
  best seeing, locks out of GoTos, lost locks, median pointing error, and targets.
- **Pointing map:** phase 123's verdict over that rig's solves.

A night runs noon to noon, so 01:30 belongs to the evening before.

## The rig and the emulator are never mixed

398 logs are on this machine, and 393 of them are the emulator. An emulated
Jupiter at seeing 9.9 averaged into real nights would make every figure a lie.
Each log says which rig it was on its first line, and the page keeps two sets.

**This found a defect in phase 123.** The emulator and the rig wrote the same
`pointing_model.csv`, so the score would have graded emulated solves as if the 8SE
made them. The emulator build now writes `pointing_model_emu.csv`. No harm was
done: the shared file had no rows yet.

## What the real logs say

Run over the logs on this machine:

- **The rig:** 1 night (8 October), 5 sessions, 5.1 h open, 53 recordings
  (52 of Saturn), 222 GB. It had 7 GoTos and no locks; the mount was jogged by hand.
- **The emulator:** 393 sessions and 57 recordings.

## What gates it

`tests/session_stats.cpp`:

- Dates and nights: midnight, leap days, and which evening an early-morning
  moment belongs to.
- One log written the way `SessionLog` writes it: mode, hours through midnight,
  GoTos, the GoTo-to-lock time, lost locks, emergency stops, recordings, and
  targets with the dash stripped.
- The join: a record replaces a `0.0s` duration and names a `Capture_` file.
  The two rigs stay apart, and a night through midnight is one night.
- A target name cannot write HTML into the page.
- If this machine has a logs folder, every real log must parse, and the rig's
  sessions must be exactly the logs that say HARDWARE.

## Owed

Checklist 45: one press of `[STATS]` in the running app.
