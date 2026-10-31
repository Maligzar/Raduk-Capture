# Phase 94 plan — a synthetic sky with real things in it

> *"I want the emulator to show the actual stars and actual planets not
> just the Jupiter or Saturn run everywhere. Even star types would be
> fine. Just so it's varying the focus from star to planet. The moon
> will be harder because the size of my sensor will make the moon very
> large so will need to account for that."*

---

> **Numbered 94, not 93.** This was first written and merged as "phase
> 93", which was already taken: phase 93 is the render-pass upload cache
> in `PreviewRenderer` and the variable-rate slew command. The number was
> checked against the highest row in `PHASE-STATUS.md` and the plan files
> on disk, neither of which mentioned it, and not against the source
> tree, which did. Renumbered on 2026-10-31.

---

## 1. One thing in that ask is backwards

**The Moon is not the hard part. The star field is.**

The Moon already works the way the request asks for. Phase 91 made every
size in the synthetic sky angular, and the Moon sits in the target table
at 900 arcsec radius with a comment saying it overflows every sensor the
rig owns on purpose, *"which is what the Moon actually does"*. The sensor
size is already accounted for.

What the Moon lacks is **anything to look at**: it renders as a flat
tinted disc, and at 0.5 degrees across a 7 arcmin frame the result is a
uniform bright rectangle. For the stated purpose, varying what focus is
judged on, that is worse than useless, because it has no detail at any
focus position. That is a texture problem. It is bounded, local, and
section 6 covers it.

The star field is the opposite: a small amount of rendering code sitting
on top of a data problem that does not fit in this repository. The
numbers are in section 4 and they are the reason this plan is staged
rather than a single piece of work.

---

## 2. What the emulator does today, which is not what it looks like

Worth establishing before any design, because two things in the current
code read as more than they are.

**It is target-centric, not pointing-centric.** `SetTargetName()` is the
only thing that decides what gets drawn, and `main.cpp` calls it from
the loaded profile and the current target. The generator then renders
that object at frame centre, displaced by whatever
`SetPointingErrorProvider` reports. **The emulator has no idea where the
mount is pointing** and no concept of a sky position. It draws the thing
you named, in the middle, always.

Everything in the request beyond "a star instead of a planet" is a
consequence of flipping that, and that is the real architectural work.

**It has no stars.** It looks like it does. The background path is:

```cpp
uint32_t h = (uint32_t)(px*7919+py*6271);
h^=h>>16; h*=0x45d9f3b; h^=h>>16;
uint8_t sv = ((h & 4095u)==0) ? /* bright pixel */ : bgVal;
```

A 1-in-4096 hash sprinkle of single bright pixels. Positionally
meaningless, no magnitude, no point-spread function, one pixel wide. The
comment calls it *"a sprinkle of faint stars"* and as decoration it is
fine, but nothing can be measured against it.

**The consequence nobody has written down:** a single-pixel hash sprinkle
is not solvable, so **no plate solve can ever succeed in the emulator**.
Every solve path in the app is therefore unexercisable indoors: `SOLVE`,
`SOLVE TO ALIGN`, phase 30's push-to loop, phase 47b's coordinate
readout, and phase 48's framing recall. That is a large fraction of the
recent work with no indoor story at all.

**The bright-star catalog cannot populate a field.** `kBrightStars` in
`SkyObject.h` holds 50 entries and the faintest is magnitude 4.66. In a
full-sensor field that is 0.00003 stars. It is a list of things to
*point at*, not a sky to render. It also carries no spectral type, so
"star types" needs a new column, which is cheap for 50 rows.

---

## 3. What the request is actually for, and the cheapest thing that serves it

The stated motive is *"varying the focus from star to planet"*, and that
is the strongest part of the ask, because a point source and an extended
disc are not the same problem and the emulator only has one of them.

| | Planet disc | Star |
|---|---|---|
| Focus is judged on | limb and surface contrast | the size of the blob, and whether rings appear |
| Sharpness metric sees | gradient across a wide feature | a single peak |
| Centroid tracker sees | a filled region | one spot |
| Defocus looks like | soft edges | a ring that opens into a donut |

Autofocus, the FWHM figure, the sharpness graph, the quick-look stacker
and the centroid tracker's lock all behave differently on a point
source, and **not one of them has ever been run against one on the
bench**. That is the gap worth closing first and it needs no catalog at
all: it needs the target table to be able to hold a star.

**It also unblocks something already on the list.** Roadmap row 31/32 is
FWHM and Bahtinov focus, ranked H, and its own entry says *"autofocus is
tuned for a planetary disc"* and that nothing in the tree implements
either measure. Both measures are defined on a point source. So 94.1 is
not merely adjacent to 31/32, it is the prerequisite for 31/32 having
any bench story at all: an FWHM implementation written today could only
be tested against a hash sprinkle one pixel wide. Row 34, solar and
lunar framing, overlaps 94.3 the same way.

Two numbers that make a rendered star worth the effort, both computed
from this rig's own configuration rather than assumed:

**The rig samples the diffraction pattern.** 203 mm aperture at 550 nm
puts the first Airy minimum at 0.682 arcsec, which at the configured
0.203 arcsec/px is **3.4 pixels**. So a correctly rendered star should
show rings, and defocus should break them up. That is exactly what makes
a star the better focus target, and it is renderable.

**Seeing will usually hide them, correctly.** The emulator already has
`SetSeeingBlurArcsec`, and at a typical 2 to 3 arcsec the 0.68 arcsec
Airy pattern washes out entirely. Rendering the pattern and letting the
existing blur convolve it reproduces the real behaviour for free: rings
on a still night, a fuzzy blob on a bad one. The emulator gets to show
why seeing matters, which it currently cannot.

---

## 4. The star field, with the arithmetic that decides it

This is the part to read before committing to anything in section 7.

Field sizes, from the configured 0.203 arcsec/px:

| Format | Field | Area | Fraction of sky |
|---|---|---|---|
| Full sensor 3840x2160 | 12.99 x 7.31 arcmin | 0.0264 sq deg | 6.4e-7 |
| ROI 1280x960 | 4.33 x 3.25 arcmin | 0.0039 sq deg | 9.5e-8 |
| ROI 640x480 | 2.17 x 1.62 arcmin | 0.0010 sq deg | 2.4e-8 |

Expected stars in a **full-sensor** field against limiting magnitude.
All-sky counts are order-of-magnitude figures from memory, not looked up,
so treat the column as a scale and not a specification:

| Limiting mag | All-sky stars (approx) | In one full-sensor field |
|---|---|---|
| 4.66 (today's catalog) | ~500 | 0.00003 |
| 10 | ~350,000 | 0.22 |
| 12 | ~2.5 million | 1.6 |
| 14 | ~15 million | 9.6 |
| 16 | ~70 million | 45 |

**A plate solve needs several stars.** So a solvable synthetic field at
this plate scale needs a catalog to roughly magnitude 14, which is tens
of millions of entries. At the narrower ROIs the rig actually captures
at, it is worse by a further factor of 7 to 27: a 640x480 planetary ROI
needs past magnitude 16 before it reliably holds a handful of stars.

That is not a header file. It is a catalog file, a loader, an on-disk
format, a spatial index, and a licensing question about which catalog.
**It is a data project wearing a rendering project's clothes**, and it
is why this plan does not treat "actual stars" as one item.

It is also, usefully, the same reason plate solving a planetary ROI is
hard on the real sky. The app already half-knows this: the solve button
says *"works great pointed at Jupiter, Saturn, or any bright star"*.

---

## 5. What a synthetic star field would and would not prove

If section 7's stage 5 ever happens, it is worth being clear in advance,
because it is easy to overclaim.

**It would** let every solve path run indoors: push-to, the coordinate
readout, solve-and-sync, framing recall. That is real and large.

**It would not settle phase 47b's `fitsBottomUp` flag**, and it is
tempting to think it would. The flag asks what ASTAP does with the
top-down BMP the app hands it. If the renderer places stars using the
same `WcsRow` helper that `PixelWCS.h` reads them back with, a round
trip proves only that the code agrees with itself, so the check is
circular and would pass just as happily with both halves wrong.

Deriving the renderer's convention independently makes it a genuine
check, but a weaker one than it looks: two independent derivations can
still share a wrong assumption about the BMP. **The real sky stays the
authority on that flag**, and clear-night item 15 stays where it is.

---

## 6. The Moon

Already the right size. What it needs is detail, and the detail has to
be chosen for the one job the Moon does well indoors: **focus is judged
on the terminator**, where shadow relief gives the highest contrast edge
anywhere in the solar system.

So, in order of value:

1. **A terminator**, placed from the real lunar phase. Without it there
   is no high-contrast edge and nothing for autofocus to work on.
2. **Maria**, as a handful of large low-contrast dark regions. Cheap,
   and enough to make the field recognisable rather than uniform.
3. **Craters near the terminator**, with shadows cast away from the Sun.
   This is where focus actually gets judged, so detail belongs here and
   can be skipped in the fully lit part.
4. **Libration and phase angle** only if 1 to 3 prove worth extending.

Note the scale this has to work at: 900 arcsec radius against a 7 arcmin
frame means roughly **one eighth of the lunar diameter is in frame** at
full sensor, and about a twentieth at a 640x480 ROI. So the renderer must
generate a *patch* of a sphere, not a disc with features on it, and the
patch has to move with the pointing error and the seeing jitter like
everything else. That is a different rendering path from every existing
target, all of which fit inside the frame. It is the only part of this
plan that cannot reuse the existing disc code.

---

## 7. Staging, cheapest first

Each stage is independently useful and independently abandonable. The
order is deliberate: the first two are most of the stated value for a
small fraction of the work.

| Stage | What | Size | Why here |
|---|---|---|---|
| **94.1** | A star as a target: point-source render path, Airy pattern, spectral-type colour column on the 50-star catalog | S | This is the actual request. Opens autofocus, FWHM, sharpness and centroid-lock to a point source for the first time |
| **94.2** | Live planet apparent size, replacing the frozen table constants | XS | The ephemeris already computes it and `tests/planet_apparent_size.cpp` already gates it. Mars runs 3.5" to 25", so the constant is wrong nearly always |
| **94.3** | The Moon: terminator, maria, terminator-local craters, spherical-patch path | M | Makes the Moon usable as a focus target. Self-contained |
| **94.4** | Pointing-centric rendering: the mount's RA/Dec decides what is in frame, not a name | M | The architectural flip. Prerequisite for any field, and on its own it makes a GoTo in the emulator show the target arriving off-centre and being centred, which it cannot today |
| **94.5** | A real star field from a real catalog | L, data-bound | Gated on section 4. Go or no-go with the numbers in hand, not a foregone conclusion |

### Recommendation

**Do 94.1 and 94.2.** Together they are days, not weeks, they deliver
the stated goal, and 94.2 is nearly free because the hard part already
exists and is already tested.

**Then stop and look at 94.3 and 94.4 separately.** Both are worth
doing; neither is urgent.

**Treat 94.5 as a separate decision with its own go/no-go**, taken once
94.4 exists, since without pointing-centric rendering a field has
nowhere to be. My own read is that the catalog cost is justified only by
the plate-solve payoff and not by the appearance of the frame, so the
question to answer first is whether solving indoors is worth a catalog
dependency. Confidence: medium. It turns on how much of the remaining
sky-gated work is solve-gated, and that list is currently five phases
long and growing.

---

## 8. What each stage owes the bench

Keeping to the project's rule that a phase states its evidence.

| Stage | Gateable on the bench | Needs the sky |
|---|---|---|
| 94.1 | PSF shape against the computed Airy radius; colour against spectral class; that defocus widens the blob monotonically | that a real star focuses where the emulator says it should |
| 94.2 | apparent size against the ephemeris over a synodic cycle (the existing test already does this; extend it to the emulator's consumption of it) | nothing |
| 94.3 | terminator position against the phase; that the patch tracks pointing error | the Moon, once, to judge whether the detail is plausible |
| 94.4 | that a known RA/Dec and a known pointing put the target at the computed pixel | a GoTo on the real mount |
| 94.5 | that ASTAP solves a rendered field, and that the solved centre matches the commanded pointing | see section 5: the row-order flag stays a sky item |
