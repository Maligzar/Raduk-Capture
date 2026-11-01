# Phase 108 — the derotation limit

A planet turns during a run, and past a certain length the stack loses
detail the telescope delivered. Planetary imagers carry that length in
their heads. The app knew the planet's size and the telescope, and said
nothing. The checklist's "roughly three minutes" was the only trace of it
anywhere.

## The window

A feature at the centre of the disc moves fastest, at `pi * D / P`, for
apparent diameter `D` and rotation period `P`. The window is the time it
takes to cross the Rayleigh limit, `1.22 lambda / aperture`, at 550 nm:
0.68 arcsec on this C8. Jupiter at 44 arcsec comes to about 176 s, which
is the figure imagers use, reached without tuning anything to get it.

- **The disc is tonight's,** from `PlanetApparentSizeAt`, so the window
  follows the planet through the year. Saturn uses the globe, not the rings.
- **Jupiter is System II.** Venus is its 4-day cloud tops, because the
  clouds are all a camera sees. The Sun is synodic.
- **The Moon has no window.** It keeps one face to us.
- **The tilt of the axis is ignored,** which makes the window slightly
  short for a tilted planet. That is the safe way round.

## Warn by default

`[DEROT] WARN` says the window at the start of a run and warns when it
passes. `[DEROT] STOP` ends the run there. Warning is the default because
WinJUPOS can derotate a longer run, and a run that ends on its own when
nobody asked it to is worse than a blurred one. The stop sits on top of
the frame or time limit, because the planetary default is a frame limit
and the window is in seconds. It is checked on the capture thread and
acted on by the render loop, as every other limit is.

Only Lights get a window. A mosaic clears it, since the config otherwise
still holds the last planet's.

## WinJUPOS names

WinJUPOS reads `yyyy-mm-dd-hhmm_t` from a file name, in UT, where `t` is
tenths of a minute, and the time has to be the middle of the run. That is
not known until the run ends, so a finished Light is renamed as it closes,
with the stamp in front of the existing name. The `_quality.txt` sidecar
moves with it and the observation record is written afterwards, so it
names the new file. If a rename fails, both keep their old names and the
log says so. Darks, flats and bias keep plain names.

On by default, `WinJuposNames=0` in `rig.ini` turns it off. There is no
button, because it is not something to change during a session.
`ApertureMm` is also in `rig.ini`.

## What gates it

`tests/derotation_limit.cpp`: the window against the three-minute figure,
which bodies count, the stamp at the edges of a minute and a year, and the
rename. `tests/capture_thread_submit.cpp`: the stop on a frame-limited run,
the finished file's name, and a dark left alone. 12 planted bugs caught.

## Owed

- One run in the app: the button, the notices, and that the toolbar still
  fits (checklist 32).
- WinJUPOS reading the name. The format comes from forum reports of
  SharpCap and FireCapture files, not from WinJUPOS's own documentation.
