# Driving the generic ASCOM path indoors

This is P5.3's remaining debt, and the only item in the queue that needs
neither a clear sky nor hardware. Half an hour, at a desk, in daylight.

## Why it exists

The app has two mount backends. `NexStarMount` talks to the hand control
over serial and is exercised constantly. `ASCOMMount` talks to any
`ITelescopeV3` driver through COM, and until now it had **no bench story
at all**: every driver that could answer without a telescope on the desk
was refused on purpose, so the generic path was argued for rather than
demonstrated.

In a project that emulates its camera, its focuser and its own mount,
that was the one device path with nothing behind it.

## What was in the way, and is no longer

Two gates, not one. The second was found on 2026-10-30 while setting
this up.

`ASCOMMount::IsRefusedByDefault` refuses every simulator, and
`AscomProgId` in `rig.ini` was always documented as bypassing it. That
part worked.

But `main.cpp` then checked `!am->IsSimulator()` on the connected driver
and dropped it regardless, logging `simulator detected, ignoring`. So
naming a simulator connected to it and threw it away, and this run could
not be performed. `AcceptConnectedDriver` now holds that decision and
follows the same rule as the first gate: **refuse a simulator discovery
found, keep one the owner named.** Both halves are pinned by
`tests/ascom_driver_choice.cpp`.

## Before you start

You need the ASCOM Platform installed. Anything from the last several
years includes at least one telescope simulator, but **which** one and
what it is called has changed across versions, so do not guess the
ProgID. Read it from the same registry key the app itself enumerates:

```
HKEY_LOCAL_MACHINE\SOFTWARE\ASCOM\Telescope Drivers
HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\ASCOM\Telescope Drivers
```

Each subkey name **is** a ProgID. Pick the simulator one. On the
platforms this code already knows about that is one of
`ASCOM.Simulator.Telescope`, `ASCOM.OmniSim.Telescope` or
`ScopeSim.Telescope`, all three of which the refusal list names and all
three of which are accepted when pinned.

Easier, if you would rather not open `regedit`: start the app with no
`AscomProgId` set and no CPWI running, and read `raduk_log.txt`. The
discovery step prints every driver it found:

```
[ASCOMMount] Enumerating ASCOM telescope drivers from registry...
[ASCOMMount]   Found driver: <ProgID>
```

## The run

**1.** Make sure CPWI is not running and the hand control is unplugged,
so nothing competes for the mount slot.

**2.** In `rig.ini`, set the ProgID you found:

```ini
AscomProgId=ASCOM.Simulator.Telescope
```

**3.** Start the app in its hardware configuration, not the emulator. The
emulator build never reaches the ASCOM path at all, so an emulator run
proves nothing here.

**4.** Expect this in `raduk_log.txt`, and treat it as the first
checkpoint:

```
[ASCOMMount] AscomProgId pins the driver: <ProgID>
[ASCOMMount] Driver name: ...
[ASCOMMount] *** SIMULATED MOUNT, named in rig.ini ***
[INIT] Mount: ASCOM (<ProgID>) - *** SIMULATED, nothing will move ***
```

and a warning in the notification bar reading
`Mount is SIMULATED (AscomProgId in rig.ini)`.

If instead you see `simulator discovered, not named; ignoring`, the
ProgID did not match what is in `rig.ini`; check the spelling.

## What to actually test, and in what order

The point is the **generic COM path**, not the simulator. Each of these
goes through `IDispatch` by name and any of them can fail on its own.

| | What it proves |
|---|---|
| The mount shows as connected, with coordinates | `Connect`, `RightAscension`, `Declination`, `Altitude`, `Azimuth` |
| Site and clock were accepted at connect | `SiteLatitude`, `SiteLongitude`, `UTCDate` |
| A GoTo from the GoTo panel moves the reported position | `SlewToCoordinates`, `Slewing` |
| Jogging in all four directions changes alt/az | `MoveAxis` both axes, both signs |
| `STOP` halts a slew in progress | `AbortSlew` |
| Sync, after a solve or by hand | `SyncToCoordinates` |
| Tracking on and off | `Tracking` |

Work down that list and write down which ones answered. **A partial pass
is the valuable outcome**, because it names exactly which property the
generic path is missing, and that is a thing a unit test cannot tell you.

## Known not to be covered by this

- **Pulse guiding.** `PulseGuide` on a simulator moves a number, which
  says the call is wired but nothing about whether the guide
  calibration's measured matrix is right. That stays a sky item.
- **The simulator's own quirks.** It will accept slews below the
  horizon and through the meridian that a real mount refuses, so this
  run cannot exercise the keep-out sector or the cord-wrap forecast.
- **CPWI.** This proves the generic path, not the Celestron-specific
  one, which is still the path the rig actually uses.

## When you are done

Clear `AscomProgId` in `rig.ini`, or discovery will keep handing back
the simulator and the real mount will never be found. The log says so
on every start, and the notification bar warns every session, but it is
still the one thing to remember.

Then update P5.3 in [`PHASE-STATUS.md`](PHASE-STATUS.md) with what
answered and what did not.
