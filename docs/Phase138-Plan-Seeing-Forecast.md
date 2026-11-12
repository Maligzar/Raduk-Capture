# Phase 138 plan: the seeing forecast against the measured sky

Queued 2026-11-11 (roadmap row 38). Written and built 2026-11-12. Sources
and reasoning are in [`Data-Sources-2026-11-11.md`](../research_notes/Data-Sources-2026-11-11.md).

## Why

Two forecasts of seeing are free:
- **7Timer's seeing class**, from 1 (best) to 8;
- **the jet stream**, Open-Meteo's wind at 200 and 250 hPa. Above about
  126 km/h the seeing is reliably bad.

No one knows whether either predicts this site. meteoblue's paid seeing
forecast waits on that answer. The way to find out is to log each forecast
beside the seeing the camera actually measured, for about ten nights.

## Two halves

**Leoben (its own repository): fetch and pass on.** Leoben adds three
optional keys to `GET /weather/current`, at any level of the reply:

| Key | Meaning | Range |
|---|---|---|
| `seeing_7timer` | 7Timer's `seeing` class for the current hour. Leoben must follow 7Timer's new 302 redirect | 1 to 8 |
| `wind_200hpa_kmh` | Open-Meteo `wind_speed_200hPa`, current hour | km/h |
| `wind_250hpa_kmh` | Open-Meteo `wind_speed_250hPa`, current hour | km/h |

All three are optional. RadukCapture treats a missing or out-of-range value
as "not given", so Leoben can add them one at a time.

**RadukCapture (this repository): log them against the sky.** Built
2026-11-12.
- `LeobenConditions` reads the three keys.
- `src/core/SeeingTally.h` collects SeeingMonitor's normalised sharpness for
  every analysed frame of a target, outside focus sweeps. It runs whether or
  not Autorun is on.
- Once an hour it writes one line to the session log, category `SEEING`.
  Examples:

  ```
  forecast 7Timer seeing 3 of 8, jet 200 hPa 95 km/h, jet 250 hPa 131 km/h (jet above the bad-seeing line) | measured median 33.0, best 50.0 over 812 samples, target Jupiter
  forecast none from Leoben | measured none
  ```

- The measured figure is only comparable within one target, so a change of
  target starts the hour's samples afresh, and the line names the target.
- A forecast is paired only while Leoben's reading is fresh. A stale feed
  logs "none" rather than pairing an old forecast with new seeing.

## Done when

1. Leoben sends the keys.
2. About ten clear nights of `SEEING` lines exist.
3. A short note in `research_notes/` says which source, if either, tracks
   the measured sky here, and so whether meteoblue is worth paying for.

Until then the phase is open.

**Gated.**
- `tests/seeing_tally.cpp` checks the hour, the line's halves, the jet
  call-out, the median, best and count, a new target starting afresh, and a
  stale forecast not being paired.
- `tests/leoben_weather.cpp`, phase 138 section, checks the keys: absent,
  given, and out of range.

## Versioning

Every pull request for this plan bumps PATCH in `src/Version.h` (3.1.0 to
3.1.1) and adds its row, with that version, to the Phase History in
`RadukCapture-Help.md`. The pull request that completes the phase bumps
MINOR instead and resets PATCH. MAJOR and a git tag are the owner's call at a
release. CI fails any pull request that does not raise the version above the
base branch's. The rule is the owner's, 2026-11-12, and is written out in
`src/Version.h`.

The RadukCapture half shipped as 3.1.1. The pull request that writes the
ten-night note completes the phase and bumps MINOR.
