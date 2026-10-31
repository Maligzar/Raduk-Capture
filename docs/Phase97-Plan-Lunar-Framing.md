# Phase 97 — solar and lunar framing

Roadmap row 34, priority H, unblocked by 94.3. Also the direct answer
to something the owner raised while planning 94:

> *"The moon will be harder because the size of my sensor will make the
> moon very large so will need to account for that."*

---

## 1. The measurement the phase turns on

| | |
|---|---|
| Plate scale | 0.203 arcsec/px |
| Full sensor | 3840 × 2160 → **12.99′ × 7.31′** |
| Moon | 29.4′ to 33.5′ |
| So the Moon is | **2.4 frames wide, 4.2 tall** |

It has never fitted. And the application could not say so, because it
did not know how big the Moon was: `SkyObject.h` computes the Moon's
ecliptic longitude and latitude from the Meeus Ch.47 series and **skips
the distance term**, and `SunRADec` stops before the radius vector.

That matters more here than it would elsewhere. The Moon's apparent
diameter swings **fourteen per cent** over an anomalistic month, and on
this plate scale that is the difference between fifteen panels and
eighteen. "About half a degree" is not a number anyone can plan from.

---

## 2. The finding: a mosaic does not close under sidereal tracking

The Moon moves 13.176°/day east against the stars, so a sidereal mount
loses it at 0.55″/s — **2.71 px/s** on this rig.

The consequence depends entirely on the timescale, and conflating the
three is how people come to believe sidereal tracking blurs lunar
frames:

| Timescale | Drift | Verdict |
|---|---|---|
| per exposure, 20 ms | **0.05 px** | nothing at all |
| per run, 60 s | 163 px, 4% of frame | the stacker aligns it out |
| per mosaic, 8.3 min | **272″ against an 88″ overlap** | does not close |

**The drift exceeds the entire overlap by a factor of three.** Not
because the panels are blurred — at 20 ms they are sharp — but because
the last panel is pointing at a different part of the Moon than the plan
thinks. The deadline before the overlap is gone is **80 seconds**, and
the mosaic takes 496.

At lunar rate the residual is the error in a mean rate against the true
one, a few per cent, which is 14″ over the same run. It closes.

### And the app was pinning sidereal

`ASCOMMount.h` wrote the literal `0` at two sites — including **on every
GoTo** — with the comment `DriveRates: 0=Sidereal, 1=Lunar, 2=Solar,
3=King` beside it. So it knew the alternatives existed. There was no
rate abstraction anywhere: `IMount::TrackingMode` is the mount's *frame*
(alt-az or equatorial), not its rate.

Choosing lunar rate and then slewing to the Moon would have put it
straight back to sidereal, which is the one sequence anybody would
actually perform.

---

## 3. What was built

| | |
|---|---|
| `src/sky/LunarSolarSize.h` | The Meeus Ch.47 distance series (ten largest terms), the Ch.25 solar radius vector, apparent diameters, and the drift rates |
| `src/sky/MosaicPlan.h` | The panel grid, serpentine ordering, tangent-plane offsets, the cos(dec) conversion, and the time and drift budget |
| `IMount::TrackingRate` | Sidereal / lunar / solar, opt-in per backend like `SlewVariable`, with `RateForTarget` |
| `ASCOMMount` | Implements it, and **remembers** it so GoTo stops resetting it |
| `main.cpp` | Sets the rate in the profile-apply funnel, which every target change passes through |

Both new headers are pure, for the reason `PixelWCS.h` and
`StarMetrics.h` are: a distance series with a sign wrong still produces
a diameter in the right ballpark, a semidiameter mistaken for a diameter
is a clean factor of two, and an RA offset missing its cos(dec) is
correct on the equator and wrong everywhere else. None of those look
wrong.

### Three traps worth naming

**The sign of the leading distance term is negative**, so a Moon at
perigee is *closer* than the mean. Flip it and the diameter is still in
exactly the right range with the variation inverted, which one night's
number would never reveal. The test pins the phase, not just the extent.

**Panels across is not `ceil(span / step)`.** With *n* panels the covered
extent is `(n − 1) × step + frame`, because the first and last
contribute their whole width and only the gaps between them are steps.
The naive form asks for two panels for a target exactly one frame wide.

**cos(dec).** One arcsec east *on the sky* is one arcsec of RA only on
the equator. The Moon reaches declination 28, where the factor is 1.13 —
small enough to look like a tracking error, large enough to lose the
overlap.

---

## 4. Two errors of mine, both caught by the test

**The perigee comparison.** The test swept 40 days and compared the
minimum against the published 356,500 km extreme, and failed by 6,000
km with nothing wrong in the series. Perigee distance is not fixed: it
varies month to month, and the extreme happens only a few times a year
when perigee coincides with syzygy. A typical perigee is 363,300. Over
a **year** the series lands within **320 km** — a far tighter check than
the one that failed. The constants are now named
`MOON_PERIGEE_EXTREME_KM` and `MOON_PERIGEE_MEAN_KM` so the distinction
cannot be lost again.

**The empty corners.** The test asserted that the bounding grid's
corners hold no Moon. At the full sensor they do: the frame is 13′ wide
against a 31′ Moon, so three columns cover it and the outer two sit only
10.4′ off-axis, their near edges well inside the limb. A 3-wide grid has
no corners to speak of.

Skipping corners pays at a small ROI — a 640×480 crop needs 18 × 24 =
432 panels of which 56 are skippable — but that mosaic is **2.9 hours**
of capture, so the real answer is the full sensor, not a cleverer grid.
That is now what the test says.

---

### Two more, found by re-reading the diff rather than by a test

**The drift budget took the smaller overlap with no justification.** The
Moon's drift is predominantly *east*, so it eats the horizontal overlap
(156″ here), not the vertical (88″). Taking the smaller is a
*conservative bound on an unmodelled direction* — the declination
component is real and `MOON_DRIFT_ARCSEC_PER_SEC` is a mean motion
along the path, not a vector — but the code said none of that. It is now
`DriftBudgetArcsec` with the reasoning attached and a test that pins
which overlap is used, so the choice is explicit rather than incidental.
It does not change the verdict: 272″ exceeds half of either overlap by
more than three times.

**`RateForTarget` required exact equality.** It is fed *profile* names,
which the owner chooses, and a lunar session is far more likely to be
filed as "Moon - Copernicus" or "Moon Tycho" than as the bare word.
Equality would have handed every one of those sidereal rate without a
murmur — the same silent near-miss this phase spent its time hunting,
reintroduced by the fix for it. Now a word match requiring a separator,
so feature profiles get lunar rate and "Moonrise" does not.

## 4b. 97.4, and the latent defect it uncovered

**Phase 97 computed both diameters and nothing consumed them**, and
finding out why turned up something larger.

`PlanetApparentSizeAt` is the one path by which an apparent size
reaches anything: the object panel's `APPARENT SIZE` figure, its "fills
N% of the frame" sentence, its scale diagram, and the emulator's
rendered disc all come through it. 94.2 built it because `size_arcmin`
is NULL for every planet in the knowledge database.

**It is NULL for the Moon and the Sun as well, and they were not
covered** — the lookup is against `kPlanetBodies`, which holds the
seven planets. So the two largest objects this rig can point at had no
apparent size anywhere in the application: no figure, no frame-fill
sentence, and a scale diagram that drew the sensor outline with nothing
inside it. The Moon is the one the owner asked about by name.

Routing them through that same function rather than a parallel one
fixes every caller at once, including one that was not on the list: the
emulator rendered the Moon at the **900-arcsec mean radius** in its
target table, a figure that cannot follow the 14% monthly swing. Indoor
practice at the wrong apparent size teaches the wrong framing, which is
the whole reason the emulator exists.

### And the defect

`LunarSolarSize.h` and `SkyObject.h` both defined
`raduk::sky::AU_KM`, so **any translation unit including both failed to
compile.** It sat on master through #53 because nothing included both:
`lunar_framing.cpp` reaches `LunarSolarSize.h` through `MosaicPlan.h`
only, and `main.cpp` had `SkyObject.h` and not the other. 97.4 is the
first thing that needs them together, which is how it surfaced.

The phase-97 compile matrix checked `main.cpp` in both configurations,
all four mount backends, and both new headers under native `g++` — and
none of those is the combination. **A new header in an established
namespace has to be compiled alongside its siblings, not only on its
own.** `SkyObject.h` now includes `LunarSolarSize.h` and keeps no
duplicate, which is the correct layering anyway since the latter
depends on nothing but `<cmath>`.

### What the operator is told

`DescribeFraming` in `MosaicPlan.h` builds the report and `GoToPanel`
logs it after a slew, down the `m_onLog` channel that already carries
the plate-scale comparison after every solve. Deliberately not panel
layout: a framing report has nothing to add on the great majority of
targets, which makes it exactly the wrong thing to reserve permanent
pixels for.

```
Jupiter is 0.6' across and the frame is 13.0' x 7.3': it fits in one.
```
```
Moon is 31.3' across and the frame is 13.0' x 7.3': 2.4 frames wide, 4.3 tall.
3 x 6 grid, 18 panels, all of them on the disc.
About 8.3 minutes at 20 s a panel plus 8 s to settle.
Drift over that is 272" against a 44" budget: THE MOSAIC WILL NOT
CLOSE. The frames stay sharp; the last panel just is not where the plan
says. 80 s is the deadline. Set the target's tracking rate.
```

The "frames stay sharp" clause is load-bearing. An operator told only
that there is drift will reach for a shorter exposure, which fixes
nothing — the failure is geometric, not photographic.

**An emergent result worth having:** a *solar* mosaic closes under
plain sidereal where a lunar one cannot, because the Sun drifts
thirteen times slower. It is the larger of the two bodies at the moment
this was measured, so it is the drift rate deciding it and not the
size. Assuming one rate for both would have been wrong in the safe
direction for the Sun and the unsafe one for the Moon.

## 5. What is not done

- **No panel layout.** The report goes to the log, by the reasoning
  above. If it earns permanent pixels the GoTo panel is where they go.
- **The mosaic is still not executed.** The plan says where to point
  and the report says whether it will work; nothing drives the mount
  through the panels. That remains the larger half.
- **No lunar feature targeting.** Pointing at a named crater needs a
  crater catalogue with selenographic coordinates plus libration. 94.3
  has `LunarLatLon` and that is the start of it, not the end.
- **No mosaic execution.** Nothing drives the mount through the panels
  and captures each one. The plan says where to go; a sequencer would
  take it there. That is the larger half and it wants the UI first.
- **The emulator declines the rate.** `IMount::SetTrackingRate` defaults
  to false and `MountEmulator` does not override it, so the drift
  finding can only be confirmed on the real mount. Accepting the rate
  and doing nothing would be worse than declining it: a caller would
  plan a mosaic believing it was tracked.

## 6. Solar, and the thing that must be said once

The arithmetic covers the Sun because it is the same geometry and the
roadmap row names it. **The optics do not.** An 8″ SCT pointed at the
Sun without a full-aperture front filter destroys the corrector, the
camera behind it, and anything at the eyepiece. A rear-cell filter is
not sufficient: the heat is concentrated before it reaches one.

Stated once, as a fact about the hardware, and not repeated.

---

## 7. How it was verified

`tests/lunar_framing.cpp`, 63 assertions:

- the distance series against **published extremes over a year** —
  356,819 / 406,501 against 356,500 / 406,700
- the sign of the leading term, by the half-month separation **within
  one month**, which the year sweep cannot establish
- diameter not semidiameter, with the 15.5′ half-answer explicitly ruled
  out
- the Sun larger in January than July, at perihelion
- the panel count at the exactly-one-frame boundary
- the serpentine starting each row where the last ended
- every grid cell visited exactly once, offsets symmetric about centre
- cos(dec) doubling at declination 60, clamping at the pole
- and §7: that the drift exceeds the whole overlap under sidereal and
  fits inside it at lunar rate

Nothing here has been checked against a real lunar session.
`LUNAR_FRAMING_PROVEN_ON_SKY` and `MOSAIC_PROVEN_ON_SKY` both say so.
