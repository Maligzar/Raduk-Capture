# Phase 67 plan — rig orientation and cord-wrap awareness

A one-time setup view that records how the rig is physically arranged:
where the observer sits relative to the mount, which way north is from
there, which side the fork arm is on, and where the cables run. Not an
always-on display — it is configuration, used before a session and then
mostly invisible.

---

## Is it worth building?

Partly. The honest split:

**Worth it.**

- **Cord wrap is a real failure mode.** An alt-az fork rotates in
  azimuth with a USB3 camera cable, an EAF cable, power and serial all
  hanging off the moving part. Wrapping strains connectors and can
  unplug the camera mid-capture. Nothing in the app tracks it today.
- **"Point the tube north" is genuinely hard in the dark.** Quick Align
  needs it, and north is an abstraction when you are standing in a
  front yard at midnight. "Point it at your monitor" is not.
- **The groundwork is nearly free.** Azimuth is *already* read from the
  mount every poll and then discarded (`main.cpp`: `GetAltAz(alt, az)`
  stores only `alt`), and the `Az`/`Alt` labels in the TELESCOPE panel
  are created but never populated — they only ever read `--`.

**Not worth it.**

- A pretty rendering of the telescope. It looks like progress and
  changes nothing.
- Modelling the monitor's exact position. The monitor matters only as a
  landmark the observer can see; its coordinates are irrelevant.
- Precise collision geometry. The app cannot know the camera's
  projection, the dovetail position or the tripod spread. A
  user-defined keep-out arc is honest; a simulated one is not.

---

## The hard constraint: the app cannot steer a GoTo

`NexStarMount::GoToRADec` sends `r` plus the coordinates. The mount
chooses its own path, including which way round in azimuth. So
"route the slew the safe way" is **not** available.

What the app does fully control is `SlewFixed`, the same primitive the
jog buttons use. That makes the workable pattern:

1. Predict the azimuth change a GoTo implies.
2. If it worsens wrap past the configured limit, say so **before**
   slewing and offer to unwind first.
3. Unwind by driving azimuth deliberately with `SlewFixed`, then issue
   the GoTo from a safe starting point.

Warn-and-offer, not silent re-routing. The user stays in charge of a
motion that could tug a cable.

---

## Cord wrap is cumulative rotation, not azimuth

Worth stating because it changes the design: what wraps a cable is how
far the mount has *turned* since the cables were dressed, not where it
is pointing. 350 deg and 10 deg are adjacent in azimuth but 340 deg
apart in travel if it went the long way.

Two consequences:

- Track a running total that unwraps the 0/360 discontinuity, reset by
  an explicit **"cables dressed"** action. This is the quantity that
  matters.
- It works **without alignment**. Absolute azimuth is meaningless until
  the mount is aligned, but relative travel is valid from the moment it
  connects — so wrap protection is available during setup, which is
  exactly when people slew around hunting for a target.

---

## The setup view

A top-down plan, opened from a menu, modal, not part of the normal UI.

Captured:

| Field | How it is asked | Why |
|-------|-----------------|-----|
| Observer bearing | "Standing at the computer, the scope is to my ..." (N/NE/E/...) | Converts between compass directions and what the user can see |
| Fork arm side | "Facing the scope from there, the arm is on my left / right" | Bounds the altitude range before the OTA meets the arm |
| Cable exit | "Cables run off toward ..." — defaults to the observer | Defines the azimuth sector where wrap bites |
| Keep-out arc | Optional: drag an arc over house, tree, wall | Front-yard setups usually have one |
| Wrap limit | Degrees of cumulative travel before warning (default 360) | Depends on cable length and dressing |

The drawing is a circle with the mount at centre, north fixed at the
top, and the observer, cables and keep-out arc drawn at their bearings
— so the user can see at a glance that the picture matches the thing in
front of them. That check is the point of drawing it at all.

Everything derives from one answer: the bearing from mount to observer.
The rest is relative to it, which is what makes this answerable in the
dark by someone who does not know which way north is.

---

## What it enables

### Quick Align helper

The step the user actually asked about. Given the observer bearing,
translate "point the tube north" into their landmarks:

> North is **toward your monitor**. Level the tube and point it that
> way, then run Align > Quick Align on the hand control.

For this rig — scope south of the computer, monitor north of the scope
— north from the mount is toward the desk, which is not obvious from a
compass rose and is obvious from that sentence.

### Cord-wrap gauge

A small indicator in the TELESCOPE panel: cumulative travel, direction,
and distance to the limit. Visible only once travel is non-trivial, so
it does not add clutter on a fresh session. A **"cables dressed"**
button resets it.

### Pre-slew warning

Before a GoTo, estimate the azimuth change and compare against the
limit and the keep-out arc. Warn, and offer to unwind first.

### Azimuth actually displayed

The prerequisite, and a bug fix in its own right: `az` is fetched and
discarded, and the panel labels never populate. Nothing above is
possible until the number reaches the screen.

---

## Storage

A `[Rig]` section alongside the existing site keys:

```ini
ObserverBearingDeg=0     ; mount -> observer, degrees true
ForkArmSide=left         ; facing the mount from the observer
CableExitDeg=0           ; defaults to the observer bearing
KeepOutStartDeg=         ; optional obstruction arc
KeepOutEndDeg=
WrapLimitDeg=360
```

Untracked, like the rest of `rig.ini`. Note that a bearing plus an arm
side is a weak description of someone's garden — it says roughly which
way a telescope faces, nothing about an address — but it belongs in the
untracked file regardless, with the site.

---

## Order of work

1. **Surface azimuth.** Stop discarding `az`, populate the `Az`/`Alt`
   labels. Standalone value, prerequisite for everything else.
2. **Cumulative travel tracking** plus the "cables dressed" reset.
   Useful on its own with no setup view at all — a wrap gauge needs no
   configuration beyond a limit.
3. **Setup view** capturing the bearings, drawn so it can be checked
   against reality.
4. **Quick Align helper** — the smallest piece and the one most likely
   to be used every session.
5. **Pre-slew warning and offered unwind.**
6. **Keep-out arc**, last: most configuration, least certain payoff.

Steps 1 and 2 are worth doing whatever happens to the rest. Step 4 is
cheap once step 3 exists. Step 6 may never earn its keep.

---

## Out of scope

- Steering the mount's own GoTo path. Not possible over the protocol.
- Simulated collision detection between camera, arm and tripod.
- Anything drawn on screen during normal imaging. This is setup
  configuration; once the answers are stored it should be invisible
  apart from the wrap gauge and a warning when one is warranted.
