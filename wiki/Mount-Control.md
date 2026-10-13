# Mount Control

Direct NexStar serial on an auto-detected COM port. No CPWI, no ASCOM, no
intermediary. This page covers the protocol, the threading, and the safety
machinery — of which there is a lot, because a runaway alt-az fork can
damage itself.

[`src/core/NexStarMount.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/NexStarMount.h) ·
[`.cpp`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/NexStarMount.cpp)

```
StarSense hand control
    │  USB-serial (auto-detected port)
    ▼
RadukCapture (NexStar direct) ── position, jog, GoTo, tracking, guide pulses
```

---

## Threading

```
Main thread ──reads──> atomic cache <──writes── Poll thread (every 2-4 s)
     │                                              │
     │ Heartbeat() every render frame               │ GetRADec, GetAltAz,
     ▼                                              │ GetTrackingMode
Watchdog thread ──8 s timeout──> EmergencyStop()    │
                                                serial mutex
```

**The main thread never blocks on serial.** A background poll thread reads
position and tracking mode and stores them in an atomic cache; the main
thread reads the cache. The UI stays fully responsive even when the hand
control is slow.

Poll interval is 2 s normally, backed off to 4 s during tracking to reduce
serial contention with the correction loop, and gated further during
recording by [`USBTrafficManager`](Device-Layer#usb-arbitration).

### Fire-and-forget commands and deferred acks

Slew and stop commands use `SendCommandNoWait` — no response wait. Their
deferred `#` acknowledgements are **counted** in `m_pendingAcks` and
drained before the next synchronous command, so a late ack cannot be
mistaken for the reply to a different question. That counter is the entire
reason fire-and-forget is safe here; without it, reply parsing corrupts as
soon as two commands overlap.

---

## Protocol commands in use

| Command | Purpose |
|---------|---------|
| `K<x>` → `<x>#` | Echo. Used by [`SerialPortScan`](Device-Layer#serial-port-discovery) to identify a real hand control |
| `z` | Get precise RA/Dec (18 bytes) |
| `r`-family (18-byte form) | GoTo precise RA/Dec |
| `L` | Is a GoTo in progress |
| `M` | Cancel GoTo |
| `t` | Get tracking mode |
| `T<mode>` | Set tracking mode |
| `s` (18-byte form) | Sync precise RA/Dec |
| `w` | Get location |
| `V` | Get firmware version |
| `J` | Is alignment complete |
| `e` | Get precise Alt/Az |
| `P` passthrough | Fixed-rate slew, and the emergency stop |

### The emergency stop, verbatim

```cpp
char cmd[8] = { 'P', 2, 16, 36, 0, 0, 0, 0 };   // axis 0 (RA/Az), rate 0
char cmd[8] = { 'P', 2, 17, 36, 0, 0, 0, 0 };   // axis 1 (Dec/Alt), rate 0
char cmd[1] = { 'M' };                           // cancel GoTo
```

Raw `WriteFile` under the serial mutex, no response wait, `m_pendingAcks`
incremented for each. Callable from the watchdog, the crash handler, or the
UI.

---

## PulseGuide

**On the NexStar backend, `PulseGuide` blocks the calling thread for the
pulse duration.** This is the single most important behavioural fact about
the guiding path, and it is not what you would guess from the name.

> **Why it is software-timed.** The NexStar `P` passthrough has **no
> portable timed-pulse form**. The AUX guide command (msgId `0x26`) only
> exists on newer MC firmware. The previous implementation sent
> `MC_MOVE_POS`/`MC_MOVE_NEG` with `msgLen = 1`, which **never forwarded
> the duration bytes at all** — pulses were no-ops at best.

So a pulse is: start a fixed-rate slew at the slowest rate, hold for
`durationMs`, then stop.

```cpp
constexpr int GUIDE_RATE   = 1;      // ~0.5x sidereal, ~7.5 arcsec/s
constexpr int MAX_PULSE_MS = 1000;   // hard safety cap

if (durationMs < 10) return true;    // below mechanical resolution
if (durationMs > MAX_PULSE_MS) durationMs = MAX_PULSE_MS;

if (!SlewFixed(axis, direction, GUIDE_RATE)) return false;
std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));

// The stop MUST reach the mount — a started-but-unstopped pulse is a runaway.
for (int attempt = 0; attempt < 3; attempt++) {
    if (SlewFixed(axis, 0, 0)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}
EmergencyStop();     // escalate — the stop would not send
return false;
```

Two consequences:

1. **Only the correction thread may call it.** Calling it from the GUI
   thread would freeze the interface for up to a second.
2. **The correction loop's effective rate is not its configured rate.** A
   locked cycle firing both axes takes up to
   `S3_RATE_MS + 2 × (maxPulse + 50)` ≈ 1.35 s, so ~1–4 Hz rather than a
   fixed 4 Hz. See [Tracking and Guiding](Tracking-and-Guiding).

The three-attempt stop retry escalating to `EmergencyStop()` is the
pattern to notice: a pulse that starts and does not stop is a runaway
mount, so "the stop command failed" is not an error to log and move past.

---

## The watchdog

A second background thread monitors a heartbeat timestamp. The main render
loop calls `Heartbeat()` **every frame**. If the heartbeat stops — app
freeze, crash, deadlock — the watchdog fires after
`WATCHDOG_TIMEOUT_MS = 8000`:

1. `StopSlew` on both axes
2. `CancelGoTo`
3. Log the emergency stop

> **Why 8 seconds.** It allows two maximum guide pulses (~550 ms each) plus
> Direct2D render headroom, while still catching a real freeze. Shorter and
> a slow render tick during a double-axis correction would trip it;
> much longer and a frozen app keeps a telescope moving.

`Heartbeat()` is a virtual on `IMount` with a no-op default, so callers
never downcast — a `static_cast` to `NexStarMount` here previously
corrupted `ASCOMMount` instances on the CPWI path.

### Crash handler

`main.cpp` registers both an `UnhandledExceptionFilter` (`CrashHandler`)
and an `atexit` handler (`EmergencyStopOnExit`), each holding a
file-scope mount pointer. A crash that bypasses normal shutdown still stops
the mount. This is one of only two pieces of file-scope state in the
application, and it is there because the alternative is hardware damage.

---

## Tracking verified by readback

Phase 64. Tracking state is confirmed by reading it back, not by the hand
control acknowledging the command.

`SetTrackingMode()` sends `T<mode>` and then re-reads with `t`. And
`TrackingModeKnown()` exists so an unreadable state reports `UNKNOWN`:

> The serial path used to report a failed read as `OFF`, which is a
> different claim entirely. `OFF` means the mount is not driving. `UNKNOWN`
> means we could not ask. One of those should stop you from starting an
> acquisition; the other is a display caveat.

This matters directly to [acquisition](Tracking-and-Guiding): with the
mount not driving, a planet crosses the 10 px dwell window in **0.14 s**,
so Centering can never converge and calibration measures about four times
more drift than pulse response. Every downstream stage fails, and before
Phase 69 none of them could say why. `StartAutoAcquire()` now refuses with
a reason, and the tracking check is the important one.

---

## Jog and slew rates

| Control | Rate |
|---------|------|
| `9×` / FAST / MED / FINE selector | Sets both the on-screen jog buttons **and the arrow keys** |
| Arrow keys | Hold to slew, release to stop |
| `Space` / `Esc` | Emergency stop, unconditionally, including in fullscreen |

Rates are 1–9, roughly 1× sidereal to 3°/s.

> **The rate selector was broken in a way worth recording.** Before v1.2.1
> the mount controller panel and the main panel each kept their own copy of
> the rate and never told each other, so picking FINE highlighted the
> button but left the arrow keys slewing at 9×. If you tried to slow the
> arrow keys down and nothing happened, that was why. The two selectors now
> stay in step (Phase 75).

Arrow-key jog also suppresses auto-repeat and stops on application
deactivation (Phase 60) — an Alt+Tab used to leave the mount slewing.

And while jogging, [tracking corrections are suspended](Tracking-and-Guiding)
plus a 500 ms settle, so a manual nudge does not fight the correction loop.

---

## Motion monitor

[`src/gui/MotionMonitor.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/MotionMonitor.h)

A trail at the bottom of the mount controller showing where the telescope
has actually moved over the last ten seconds, with current speed:

```
CAMERA  4.8"/s   ±8"
```

> **What it is for.** At FINE rate a short press moves the telescope a few
> arcseconds. Nothing on screen changes enough to notice, so **a command
> that did nothing and a command that did something small look
> identical.** This tells them apart: press an arrow key and either the
> trail moves or it does not.

| Source | Comes from | Resolution |
|--------|-----------|------------|
| `CAMERA` | The planet's centroid, every analysed frame | Sub-arcsecond, effectively instant. **Preferred whenever available** |
| `MOUNT` | Alt/az read back over serial | Polled every 500 ms, or 2 s while recording — a short nudge appears as one jump, not a line |

The active source is **drawn on the plot**, because the two have very
different time resolution and a reading that does not say where it came
from invites the wrong conclusion.

A switch to `MOUNT` is itself information: it usually means the planet has
left the frame, which is worth knowing at once rather than a minute later.

The ±8″ figure is the plot width and **rescales itself** — read the scale
before judging how far the trail has travelled. A trail drifting steadily
with no input is normal with tracking off, and is exactly what tracking
exists to cancel.

---

## Cord wrap

[`src/core/CordWrap.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/CordWrap.h)

An alt-az fork rotates in azimuth with the USB3 camera cable, the EAF
cable, power, and serial all hanging off the moving part. Enough rotation
one way strains the connectors and can pull the camera out mid-capture.
Nothing tracked this before Phase 67.

> **The quantity that matters is cumulative travel, not azimuth.** 350° and
> 10° are adjacent on a compass but **340° apart in travel** if the mount
> went the long way round. A cable does not care where the tube points; it
> cares how many turns have accumulated since it was last dressed.
>
> **A useful consequence: this needs no alignment.** Absolute azimuth is
> meaningless until the mount is aligned, but the *difference* between
> consecutive readings is valid from the moment it connects — which is
> exactly when people slew around hunting for a target.

| Behaviour | Detail |
|-----------|--------|
| Readout appears | Once travel exceeds 30°, so a fresh session is not cluttered with `+0` |
| Signed | 180° out and 180° back reads 0 — the cable is slack again |
| GoTo slews excluded | `MAX_STEP_DEG = 90`. Polls arrive every couple of seconds; even a fast 4°/s slew covers under 10° in that time, so anything approaching a half turn means the samples are not continuous |
| Limit | `WrapLimitDeg` in `rig.ini`, default 360°. Warns **once** past it |
| Reset | **Press CABLES OK** whenever you free or re-dress the cables. Nothing else resets it — the app cannot see a cable, so it has to be told |

### Pre-slew forecast

A GoTo you start is checked before it runs:

```
  Travelled since CABLES OK:  +340°
  This slew adds:              +80°
  Would leave you at:         +420°   (limit 360°)
```

> **The forecast assumes the shortest turn,** because the protocol carries
> coordinates, not a direction — the mount chooses its own path. If even
> the short way breaks the limit, the long way certainly does, **so the
> warning is never a false alarm**, and a slew that happens to *unwind*
> stays silent.

Unwinding is left to the observer rather than driven automatically:
shedding a turn means rotating up to 360°, a long motion that can snag
something by itself. Jog in the direction given and watch the wrap figure
beside Az fall.

Only slews **you** start are checked. The sky survey fires dozens
unattended and would otherwise raise a dialog on each.

---

## Keep-out sector

An optional arc of azimuth marking something you cannot point at — a wall,
a tree, the house. A GoTo whose target lands inside it asks first.

> **Direction is part of the definition and cannot be guessed.** 300° → 60°
> is the 120° sector crossing north; 60° → 300° is the 240° sector the other
> way round. The convention is **clockwise from start to end**. Equal values
> mean no sector, which is the default.

Set it in the **SETUP** view, where it is drawn as a wedge so you can see
which sector you have actually described, or as `KeepOutStartDeg` /
`KeepOutEndDeg` in `rig.ini`.

Independently, the sky map enforces a **15° horizon floor**: objects below
it show `BELOW HORIZON` in red and the `SLEW` button is not offered at all.

---

## Align helper

Phase 67 step 4. The problem: a one-point align needs the mount pointed
north, and "which way is north" is not a question anyone can answer
reliably in the dark.

The helper takes **one number** — `ObserverBearingDeg`, the bearing from
the mount to where you stand, degrees true — and expresses everything
relative to it, in terms of your own landmarks. One number, because
everything the helper says is relative to it, and it is a question
answerable in the dark.

`-1` means unasked. `0` is a legal bearing (due north), so the sentinel
cannot be 0. `ShowAlignHelper = 0` in `rig.ini` suppresses it, set by
"Don't show again".

### `★ ALIGN`

One-point align on a centred object: syncs the mount to the object's
computed position **and** switches tracking on. Planets are offered first
in the target list. The result is reported honestly, with three distinct
outcomes rather than one:

- `Aligned … tracking ALT-AZ`
- `Synced … mount refused tracking`
- `Mount refused the sync`

---

## Site and clock

Phase 62 sends the observing site and the clock to the mount, which is what
makes alt-az tracking possible at all. Site resolution order:

1. An explicit `rig.ini` entry — **always wins**
2. Windows Location (WiFi BSSID positioning, ~1 km)
3. GPS hardware, if present
4. The mount itself, asked on connect

`RigConfig::AdoptSite()` **refuses to overwrite a site whose source is
`manual`**: a surveyed coordinate must not be replaced by a WiFi estimate.
See [Configuration Reference](Configuration-Reference) and
[Sky, GoTo and Plate Solving](Sky-GoTo-and-Plate-Solving).

---

**Next:** [Tracking and Guiding](Tracking-and-Guiding) · [Sky, GoTo and Plate Solving](Sky-GoTo-and-Plate-Solving) · [Device Layer](Device-Layer)
