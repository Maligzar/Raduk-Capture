# Phase 66 plan — derive the observing site automatically

Today the site lives in `rig.ini`, which the user maintains by hand.
The public build defaults to `0.0, 0.0` on purpose, so a missing or
stale `rig.ini` silently pushes the Gulf of Guinea to the mount and
breaks alt/az tracking, the sky map and the ADC model. That happened
once already during the repository cleanup on 2026-10-09: the file did
not exist at all, and the app had been running on hardcoded defaults
that the public release had since zeroed.

A hand-maintained file is the wrong mechanism for a value the machine
can determine for itself.

---

## How accurate does it actually need to be?

This decides everything else, so it comes first.

At 0.203"/px the camera sees **12.99' x 7.31'**. To land a target
inside the frame on a cold GoTo, pointing must beat half the short
axis — **3.65', or 0.061 deg**. At latitude 33.9 that is:

| | tolerance |
|---|---|
| latitude | 6.8 km |
| longitude | 5.6 km |

Measured against candidate sources:

| Source | Typical error | Pointing error | Verdict |
|---|---|---|---|
| GPS receiver | 50 m | 0.0' | far better than needed |
| Windows Location (WiFi) | 1 km | 0.6' | comfortable |
| IP geolocation, good case | 5 km | 3.2' | marginal |
| IP geolocation, typical | 25 km | 16.2' | too coarse |
| Timezone centre only | 650 km | 422' | useless |

**No GPS hardware is required.** Windows Location Service is roughly
an order of magnitude better than the requirement, and IP geolocation
is the only candidate that is genuinely borderline.

Two caveats that soften the requirement further:

- A sync on target (ALIGN) absorbs static pointing error almost
  entirely for nearby objects. Position accuracy matters most for the
  *first* GoTo of a session, before any sync exists.
- Tracking *rate* error from a 0.06 deg position error is of order
  0.1%, which is about 0.9" per minute — well under seeing. Position
  error degrades pointing, not tracking smoothness.

---

## Sources, in priority order

### 1. `rig.ini`, when explicitly set

An explicit value always wins. Someone who has typed their surveyed
coordinates must never have them silently replaced by a WiFi estimate.
Distinguish "set" from "absent" properly — `0.0, 0.0` is currently
both the default and a legal coordinate, which is ambiguous. Add a
`SiteSource=` key recording how the value was obtained (`manual`,
`windows`, `mount`, `gps`), so the app knows whether it may update it.

### 2. Windows Location Service

`Windows.Devices.Geolocation.Geolocator` via C++/WinRT. The project is
already C++20 and already links `ole32`/`oleaut32` and does COM in
`ASCOMMount`, so the idiom is established; this adds `windowsapp.lib`.

This is also the honest answer to "or your router": a router does not
know where it is. What Windows does internally is look up nearby WiFi
BSSIDs in a positioning database — the router *is* the signal, and
Windows Location is the supported way to use it.

Requires the user to allow location access for desktop apps. If
consent is refused the call returns `Denied`, which is a clean,
detectable outcome rather than a hang.

### 3. The mount's own stored location

`NexStarMount` already sends `W` (set location) and `H` (set time).
Their read counterparts `w` and `h` return the same eight-byte
payloads and are not yet implemented. An aligned hand control already
holds a site, often entered carefully or supplied by a SkySync GPS.

This makes the relationship bidirectional and self-correcting:

- app has a site, mount does not -> push (today's behaviour)
- mount has a site, app does not -> **read it back**
- both have one and they disagree by more than a few km -> say so and
  let the user choose, rather than silently overwriting either

### 4. A serial GPS receiver, if present

The serial scanner from Phase 60 already enumerates present COM ports
and probes them. A second probe for NMEA sentences (`$GPGGA`,
`$GPRMC`) would pick up a USB GPS dongle with no configuration. Worth
doing only if the user actually has one — the accuracy table shows it
is unnecessary, so this is opportunistic, not a goal.

### 5. Ask once, in the app

If nothing above yields a site, show a small dialog at first launch
rather than failing to 0,0 with a log line nobody reads. Offer a
city-name lookup or latitude/longitude entry, and write the result to
`rig.ini` with `SiteSource=manual`.

### 6. Refuse loudly

If the site is still unknown, do not push `0,0` to the mount. Skip the
push, keep the mount's own site, and say plainly that alt/az tracking,
the sky map and the ADC need a location. Sending a knowingly wrong
site is worse than sending none.

---

## Sanity checks

Two cross-checks cost almost nothing and would have caught the 0,0
case immediately.

**Against the OS timezone.** The UTC offset implies a longitude band:
`offset x 15 deg`, give or take the irregular real borders. Flag a
derived longitude more than ~20 deg from that centre. For UTC-7 the
centre is -105; a site at -112.09 is 7 deg away and passes, while 0.0
is 105 deg away and fails loudly.

**Against the mount.** If both the app and the mount hold a site,
compare them. Agreement is reassurance; a large disagreement is a
real finding worth surfacing before an observing session, not after.

---

## Elevation, while we are here

`ADCAssistant` hardcodes `P = 918 hPa` for 528 m. Dispersion is what
the user is already being shown as `ADC: REQUIRED`, and it scales with
pressure. Windows Location and GPS both return altitude, so add
`SiteElevationM` to `RigConfig` and derive pressure from it rather
than from a constant tied to one town.

---

## Storage and privacy

Everything derived is written to `rig.ini`, which is untracked and
`.gitignore`d. That file is already the only place the real site
lives, and the public history was deliberately scrubbed of it — so:

- never write coordinates into source, docs, or anything committed
- do not log full precision at startup; a coarse confirmation such as
  "site: 33.9N 112.1W (windows, +/-1 km)" is enough to verify without
  putting a house on a screenshot
- IP geolocation is **opt-in only**, never a silent fallback: it
  discloses the user's IP to a third party, and the accuracy table
  shows it is the weakest usable source anyway

---

## Proposed order of work

1. `SiteSource` and `SiteElevationM` in `RigConfig`; distinguish
   "unset" from "0,0".
2. Refuse-loudly path: never push a site the app does not have.
3. Timezone sanity check — cheapest guard, catches the worst failure.
4. `w` / `h` read on `NexStarMount`; read-back when the app has no
   site; disagreement reported.
5. Windows Location via C++/WinRT, with consent handling.
6. First-run dialog.
7. ADC pressure from elevation.
8. NMEA GPS probe, only if wanted.

Steps 1-3 remove the silent-wrong-site failure outright and need no
new dependencies. Step 4 reuses protocol work already done. Step 5 is
the one that actually answers the original request, and it is worth
doing after the guards exist, so that a denied permission degrades
into a clear prompt instead of a silent zero.
