# Sky, GoTo and Plate Solving

Catalog, a true polar sky map, ASTAP plate solving with mount sync, and an
accumulating pointing model that currently **reports** rather than
corrects — for two reasons that are worth reading before you wire it up.

---

## Object catalog

[`src/sky/SkyObject.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/sky/SkyObject.h)

```cpp
struct SkyObject {
    std::string name;
    std::string type;       // "Planet", "Messier", "NGC", "Star"
    double      ra;         // hours, J2000
    double      dec;        // degrees, J2000
    float       magnitude;
    std::string notes;      // e.g. "Great Red Spot visible"
    bool isPlanet() const { return type == "Planet"; }
};
```

| Source | Contents |
|--------|----------|
| Simplified VSOP87 | Solar System planet positions, accurate to ~1 arcmin for 2000–2100 |
| Messier | 110 objects, J2000 |
| NGC subset | Selected visual targets |
| Yale BSC subset | ~100+ bright stars for the sky map (mag ≤ 4.5), up to ~9000 available |

All coordinates are J2000.0 (ICRS), RA in hours, Dec in degrees. Both
catalogs are public domain and compiled in — there is no catalog file to
install for GoTo itself (ASTAP has its own catalog requirement).

---

## GoTo panel (`G`)

[`src/gui/GoToPanel.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/GoToPanel.h)

- **Object list** — 110 Messier + 7 planets, sorted by altitude, highest
  first. Objects below the horizon are greyed out.
- **GOTO** — slew to the selection.
- **Manual RA/Dec** — enter coordinates directly.
- **Plate Solve → Sync** — run ASTAP on the current frame, compute actual
  pointing, sync the mount.
- Public entry point `RequestGoTo(ra, dec, name)`, used by the sky map.

**Hidden by default.** That is deliberate: an always-visible GoTo panel is
an always-available accidental slew during imaging.

Every GoTo the user starts is checked against the
[cord-wrap forecast and the keep-out sector](Mount-Control#cord-wrap)
before it runs. Slews started by an unattended sky survey are not checked,
because it fires dozens and would otherwise raise a dialog on each.

---

## Sky map (`M`)

[`src/gui/SkyMapWindow.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/SkyMapWindow.h)

A Direct2D alt-az polar projection, rebuilt in Phase 63 as a **true
zenith-centred** projection rather than the approximation it started as.

| Feature | Detail |
|---------|--------|
| Observer | From `RigConfig`; LST derived from UTC plus longitude |
| Contents | Bright stars, planets (coloured glowing dots), Messier (cyan) |
| Zoom | Mouse wheel 1×–32×; right-click resets |
| Pan | Click-drag when zoomed |
| Mount marker | Green crosshair plus an FOV circle (0.13° for ASI678MC + C8) |

### Progressive reveal

Magnitude limit rises with zoom, so the map is legible at 1× and useful at
32×:

| Zoom | Limit |
|------|-------|
| 1× | mag < 4.5 |
| 2–4× | mag < 5.5 |
| 4–8× | mag < 6.5 |
| 8×+ | mag < 8.5 |

### Two safety behaviours

**Click-to-confirm.** Clicking an object shows an inline Direct2D
confirmation popup — "Slew to Planet? Alt 42°" — with ✓ SLEW and ✗ CANCEL.
No slew happens on the first click.

> **Why.** The first version fired an immediate GoTo on click. A fat-finger
> click on a star map then commands a telescope, which is not a mistake
> anyone should be able to make in one action.

Clicking outside the popup box, or right-clicking, dismisses without
slewing.

**15° horizon floor.** Objects below 15° altitude show `BELOW HORIZON` in
red and the SLEW button **is not offered at all** — not greyed, not
present. That floor is the observing site's obstruction margin.

---

## Plate solving with ASTAP

[`src/sky/ASTAPSolver.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/sky/ASTAPSolver.h)

ASTAP is a free, fast, local plate solver. The wrapper:

```
1. Save the current frame as a BMP to a temp path
2. Run ASTAP.exe with FOV hints derived from RigConfig's plate scale
3. Parse the resulting .ini for the RA/Dec solution
4. Return the solved field centre
```

ASTAP must be installed at one of the default paths or a configured one,
and its star catalog (H18 or D80) must be downloaded. Absent, solve
requests fail with a clear message rather than hanging.

Runs on its own thread with an atomic cancel flag — a solve takes seconds
and must not block the render loop.

This is the one place the nominal plate scale from `rig.ini` does real
work: it is the FOV hint that keeps the solver's search space small.

---

## Pointing model

[`src/sky/PointingModel.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/sky/PointingModel.h)

Records the difference between where the mount **thinks** it is (NexStar
reported RA/Dec) and where it **actually** is (ASTAP plate solve).
Accumulated across a session and across nights, this is the data a GoTo
pre-correction would need.

Storage is a plain CSV at `data/pointing_model.csv`, no dependencies,
mutex-serialised writes:

```
timestamp, az_deg, alt_deg, ra_expected_h, dec_expected_deg,
ra_actual_h, dec_actual_deg, error_arcsec, temp_c, seeing_score, object_name
```

Recording temperature and seeing alongside each point matters: a pointing
error that correlates with temperature is a different problem from one that
correlates with azimuth.

### `GetCorrection()`

Inverse-distance-squared weighted mean of nearby entries, requiring at
least two points within `radiusDeg`:

```cpp
double w = 1.0 / (dist * dist);
sumDRA  += w * (e->ra_actual  - e->ra_expected);
sumDDec += w * (e->dec_actual - e->dec_expected);
```

### Why it reports and does not correct

Phase 6.6. The map existed, persisted, and interpolated — and **nothing
called it**, so it had been filling up and doing nothing. Wiring it into
`DoGoTo` was the obvious finish, and it is wrong twice over:

> **1. The sign is backwards.** `GetCorrection` returns
> `(actual − expected)`: plate-solved truth minus what the mount claimed. A
> GoTo drives until the mount's *reported* position equals the target,
> which leaves the telescope that error away from it — so the fix is to
> command target **minus** the error. `ApplyPointingCorrection` **adds**
> it. Applied as written, **every GoTo would land twice as far off as it
> does now.**
>
> **2. The data is already spent.** `AutoSolveOnLock` calls `SyncRADec`
> with the solved position immediately *before* recording it, so the
> mount's internal model is corrected at that point and the error just
> recorded no longer exists. Re-applying it later would reintroduce an
> error the mount does not have.

The second is the real problem: it means **the recorded quantity is not the
thing a correction needs**. Doing it properly means either recording
without syncing, so the map is the only correction, or recording post-sync
*residuals*, which measure what the sync failed to fix. Which is right is a
measurement on a real mount, not a decision to take at the keyboard — and a
wrong choice sends a single-arm alt-az mount somewhere unintended.

So the slew is left alone and the prediction is reported:

```
Pointing model predicts +12.4" RA, -3.1" Dec here (from 4 nearby points)
  - NOT applied, see Phase 6.6 note
```

> That makes the accumulated map visible for the first time and produces
> exactly the evidence needed to settle both questions: whether the
> predicted error matches what the mount actually does.

Reported through a **log callback, not `printf`** — which reaches nothing
in a GUI build.

This is the clearest example in the codebase of the "report honestly"
principle: a half-built feature that would have made things worse is wired
to a diagnostic instead of to the hardware, with the two blocking questions
written down.

---

## Shared alt/az maths

[`src/core/SkyMath.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/SkyMath.h)

> **Why this file exists.** This maths existed twice — as private statics in
> `SkyMapWindow` and, after Phase 67, again in `MountEmulator`. The
> cord-wrap predictor needed it a third time, which is the point at which
> duplication stops being tolerable: **three copies drift, and a sky map
> that disagrees with a slew warning is worse than either alone.**

The site comes from `RigConfig`, so callers do not pass it around.
`NormDeg360()` and `ShortestTurnDeg()` live here too — the latter is what
[cord-wrap tracking](Mount-Control#cord-wrap) uses to tell continuous
rotation from a GoTo jump.

---

## Site resolution

[`src/core/SiteLocator.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/SiteLocator.h)

Phase 66. The observing site used to be a hand-maintained `rig.ini` entry,
and a missing or stale one silently pushed a wrong site to the mount.

### How accurate does it need to be?

> At 0.203″/px the camera sees 12.99′ × 7.31′, so a cold GoTo must beat
> half the short axis — 3.65′, or 0.061°. At latitude 34 that is **5.6 km
> of longitude**. Windows Location over WiFi is good to roughly 1 km
> (0.6′), about an order of magnitude better than needed. **GPS hardware is
> unnecessary.**

That is the right way to specify a requirement: derive the tolerance from
the field of view, then pick the cheapest source that beats it.

### "Get it from the router"

> A router does not know where it is. Windows resolves nearby WiFi BSSIDs
> against a positioning database — the router is the *signal*, and this API
> is the supported way to read it.

### Consent and failure

Desktop apps need Settings → Privacy → Location services, with "Let desktop
apps access your location" enabled. A refusal returns `Denied`, which is a
clean outcome that gets reported rather than a hang.

**Everything here is best-effort. Failure means "ask the user", never
"assume somewhere".**

### Resolution order

1. An explicit `rig.ini` entry — **always wins**
2. Windows Location
3. GPS hardware, if present
4. The mount, asked on connect (it is not open yet at startup)

`RigConfig::AdoptSite()` refuses to overwrite a site whose source is
`manual`:

```cpp
if (siteValid && siteSource == "manual") {
    printf("[RigConfig] Keeping the manual site; ignoring %s\n", source);
    return false;
}
```

And it carries an explicit `UNKNOWN_ELEV` sentinel:

> Pass `UNKNOWN_ELEV` when the source has no vertical fix. **A WiFi
> position reports altitude 0.0**, and treating that as sea level would
> overwrite a correct elevation and skew the [ADC](Advisors) pressure
> model.

`DistanceKmTo()` (great-circle) exists so two sources can be checked for
genuine disagreement rather than a difference in precision.

### Why `siteValid` is a separate flag

> 0,0 is both the default **and** a legal coordinate — it is a real spot in
> the Gulf of Guinea. So the two were indistinguishable, and an absent
> `rig.ini` looked exactly like a deliberate site on the equator. Nothing
> could tell "unset" from "set to zero", and a wrong site was pushed to the
> mount in silence.

---

## Site and clock to the mount

Phase 62 sends both to the mount, which is **what makes alt-az tracking
possible at all** — an alt-az mount cannot convert RA/Dec to axis rates
without knowing where and when it is.

See [Mount Control](Mount-Control#site-and-clock) and
[Configuration Reference](Configuration-Reference).

---

**Next:** [Advisors](Advisors) · [Mount Control](Mount-Control) · [AI Assistant](AI-Assistant)
