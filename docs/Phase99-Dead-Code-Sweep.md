# Phase 99 — the dead code sweep

Four dead things surfaced incidentally across phases 96 to 98:
`Theme::DEEP`, a palette token painted by nothing; `DrawSectionHeader`,
a painter with no callers; and two never-created labels that made
`UpdateCaptureStatus` abort on its first line. Three of the four were
found only because something else went looking.

This looked deliberately, across 167 files and 55,313 lines.

---

## 1. Method

Scripted candidate-finding, then **every hit verified by hand**. A
regex cannot see a virtual dispatch, a function pointer taken by
address, or a name that appears only in a comment — and this codebase
has very long comments that quote code, so string literals and comments
are stripped before counting.

The detector was calibrated against the known four: it finds them, and
correctly no longer reports `UpdateCaptureStatus` now that phase 98
wired it.

### It produced false positives, and that is the point of verifying

A threshold of "two mentions or fewer" is right for an out-of-line
method (declaration plus definition) and **wrong for a one-line inline
accessor**, where the definition plus a single call site also totals
two. `CentroidTracker::ResumeCorrections`, `DriftRA` and `DriftDec` were
all flagged and are all used, from `main.cpp`.

So the counts below are what the script found; the verdicts are what
reading each one established.

---

## 2. Fixed here

These are mechanical: superseded code, duplicated constants, and one
loose end of my own. None changes behaviour except the label, which
appears.

| | |
|---|---|
| `m_lblRecBuffer` + `ID_LBL_REC_BUFFER` | **created.** Phase 98 wired `UpdateCaptureStatus` and created two of the three labels it writes to, so its RAM line stayed a no-op under a null guard. My loose end. |
| `WebSocketServer::MAX_WS_PAYLOAD` | **derived, not restated** — see below. |
| `CentroidTracker::PLATE_SCALE_ARCSEC_PX` | **deleted.** A third hardcoded plate scale. |
| `ControlPanel::MakeSectionLabel` | **deleted.** Superseded painter, no callers. |
| `ID_HDR_CAMERA/EAF/MOUNT`, `ID_LBL_EXP_VAL`, `PANEL_HEIGHT` | **deleted.** |
| `STAR_METRICS_PROVEN_ON_SKY` | **asserted** by its own test at last. |

### The one that looked alarming and was not

`MAX_WS_PAYLOAD` is documented as clamping "the 64-bit wire length
before any arithmetic (a hostile frame could integer-wrap totalLen or
terminate the app via a huge resize)" — and nothing read it. On a
websocket server that drives a mount and a focuser, an unenforced input
bound is the worst thing this sweep could have turned up.

**It is not a hole.** The parser moved into `WsProtocol.h` and took the
check with it: `ws::kMaxFramePayload`, the same megabyte, clamped before
the arithmetic at the one place that does the arithmetic, with
`kMaxMessage` capping reassembled fragments and `MAX_RECV_BUF` capping
buffer growth.

What *was* wrong is that this was a second copy while the comment
claimed it "stays the server's own guard as well". Two copies of a
security-relevant bound is how one gets tightened and the other does
not. It now derives from the enforced one.

### The third plate scale

`CentroidTracker::PLATE_SCALE_ARCSEC_PX = 0.203f`, read by nothing. The
rig's real figure is `RigConfig::plateScaleArcsecPx` from `rig.ini`, and
the emulator deliberately runs 0.223 so phase 51's guide calibration has
a real error to measure. Anything that started using this constant would
silently ignore both.

This project has been bitten by a stale literal **twice**: the
emulator's blur per step, derived for a plate scale that then moved, and
the plate scale itself. A dead third copy is that bug waiting.

---

## 3. Not fixed: four unfinished features

These are not litter. Each is a feature someone started, and deleting
one is as defensible as finishing it — which makes it the owner's call,
not a side effect of a sweep.

### 3.1 Docking is a stub wired to nothing

- `m_btnDock` is assigned `nullptr` twice in `RebuildControls` and
  **never to a real window**.
- `ID_BTN_DOCK` therefore has a `WM_COMMAND` case that can never fire.
- `ToggleDock()` is reachable only from that case.
- `SetDockLabel()` has no callers and writes to a null handle.
- `main.cpp:3768` wires `SetDockToggleCallback([&]() {})` — **an empty
  lambda.**

So even if the button existed and were pressed, the callback would do
nothing. The whole feature is scaffolding.

### 3.2 Five unreachable entry points on `CentroidTracker`

`StartCentering()`, `ClearCalibration()`, `SetDriftCallback()`,
`AutoAdvancing()`, `DwellCount()`, and the constant
`GUIDE_RATE_AS_PER_S`.

**The tracker's core is live** — `Run`, `Reset`, `IsRunning`,
`SetMount`, `IsStable`, `IsDwellMet` and the drift accessors are all
used. This is five controls on a working subsystem, not a dead one.
`StartCentering` and `ClearCalibration` in particular read like real
operations an operator would want.

### 3.3 Three callbacks wired on one side only

| hook | state |
|---|---|
| `m_onRecordStop` | declared, has a setter, **never set and never invoked** |
| `m_onPlanetLock` | **invoked** in the .cpp, never set |
| `m_onSolveSync` | **invoked** in the .cpp, never set |

The last two fire into empty `std::function`s every time. And
`m_onRecordStop` is pointed: had that hook been live, phase 98's
stuck RECORD button would have had an obvious place to fix.

### 3.4 Four practical exposure rails nothing enforces

```
EXP_PLANET_MIN_MS =    1.0   // below this = oversampling seeing
EXP_PLANET_MAX_MS =   50.0   // above this = motion blur on planets
EXP_DSO_MIN_MS    = 1000.0   // 1 second minimum sub
EXP_DSO_MAX_MS    = 300000.0 // 5 minute maximum sub
```

Documented with their reasons, and no code checks any of them. A
planetary profile can be saved at 500 ms and nothing says a word.
`EXP_MIN_MS` and `EXP_MAX_MS`, the hardware bounds, *are* used —
these four are the advisory ones, and enforcing them is a behaviour
change rather than a repair.

---

## 4. What the sweep says about the codebase

Nine dead items in 55,313 lines is not a bad ratio, and most of what it
found is superseded rather than broken.

But the shape is consistent and worth naming: **the gap in this project
is not between "wrong" and "right", it is between "written" and
"reachable".** Every instance follows the same path — something is
built, something else supersedes it or the wiring is never finished, and
the declaration survives looking exactly like a feature. A null-guarded
write to a control nobody created is indistinguishable from working
code at a glance, and `UpdateCaptureStatus` proved it can hide two
reported bugs at once.

The cheap defence is the one applied above: when a constant exists in
two places, make the second derive from the first rather than restate
it.

---

## 5. Verification

Suite **37 pass / 2 fail / 0 build-fail** (`ai_context_facts` and
`object_knowledge` are pre-existing and environmental). Every GUI
translation unit, `WebSocketServer.cpp` and `main.cpp` compile clean in
both configurations. `star_metrics` gained an assertion and still
passes.

Nothing here was deleted without first establishing, by reading every
call site, that nothing reaches it.
