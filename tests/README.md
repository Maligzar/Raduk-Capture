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

| Test | Covers |
|---|---|
| `histogram_accuracy` | Bayer channel assignment, what the G channel contains, clip percentages in 8- and 16-bit, left-aligned RAW16 handling, sampling coverage. Findings in [`docs/HISTOGRAM-ACCURACY.md`](../docs/HISTOGRAM-ACCURACY.md). |
| `adc_alignment` | That the dispersion the ADC alignment window reports is the displacement actually in the frame: a known shift comes back, sub-pixel residuals resolve, direction is right, and — the one that matters — a banded planet with no dispersion reads zero rather than inventing a residual out of its cloud belts. |
| `sensor_analysis` | The numbers the averaged histogram cannot show: Gr/Gb split measured and judged, per-channel headroom in stops, which channel limits the exposure, and that clipping reports a fraction rather than an invented stop count. |
