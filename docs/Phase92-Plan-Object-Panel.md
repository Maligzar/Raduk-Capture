# Phase 92 plan — the object panel

A screen that says *what you are looking at*, to sit on one of the
display monitors beside the share view, fill it, and be good enough to
stand in front of.

> *"an informational panel that I will put in one of the display
> windows. This goal is to display details about the object that we are
> viewing. It should be able to full screen, and be high polished in
> Hollywood style since it will be one of the education items."*

---

## 1. Where this sits

Phase 89 gave the rig a second audience. The share view is the
**photograph** — the planet, large, with nothing in the way. This is
the **caption**, and the two are meant to be read together across two
screens:

| | Share view (89) | Object panel (92) |
|---|---|---|
| Shows | What the telescope sees, now | What the thing is |
| Changes | 30 times a second | When the target changes |
| Answers | "look at that" | "what am I looking at" |

It is the same room, the same people, and the same rule: the owner
works on display 1, everything else is for other people.

---

## 2. The data — checked, and it is not what the code implies

This is the part worth reading before any of the design. **A polished
panel with nothing to say is wallpaper**, and the current state of the
knowledge base is not what the existing code suggests.

Measured against `data/object_knowledge.db`, 2.4 MB, built March 2026:

| Table | Rows | What is really there |
|---|---|---|
| `objects` | **14,520** | name, common name, catalog ids, type, subtype, constellation, RA/Dec, magnitude, surface brightness, size (arcmin and ly), distance (ly and pc), spectral type |
| `narratives` | 679 | **651** have a summary longer than 50 characters |
| `imaging_notes` | **26** | aperture, filters, notes, season, difficulty |

So: physical data for fourteen and a half thousand objects, prose for
about six hundred and fifty, imaging notes for twenty-six.

### Four findings that change the shape of the work

**1. `sqlite3.dll` is not in either build output, so none of this has
ever been readable by the running app.**

`ObjectKnowledgeDB::Open()` loads SQLite dynamically and returns
gracefully when the DLL is missing — which is correct behaviour and
also why nobody noticed. The 2.4 MB database has been sitting unused
since March, and the object info panel has been falling back to
"basic ephemeris data only" every time it opened.

**This is phase 92's first task and it is not a panel task.** Nothing
here is worth designing until the data is reachable.

**2. The `mythology` column has zero rows populated.**

`ObjectInfoPanel`'s own header draws a layout with a
`DISCOVERY / MYTHOLOGY` section. The column exists, the query selects
it, and there is no mythology in the database at all. Either the
builder never populated it or the source did not provide it. A section
that is always empty should not be in the design, and a plan that
promises one is promising content that does not exist.

**3. Names do not line up between the app and the database.**

The sky map calls it `M13 Hercules Cluster`. The database calls it
`M013`, with `M013, NGC6205` in `catalog_ids`. `QueryObject()` matches
on `name='...'` exactly, so that lookup misses. Any panel driven by
"whatever the GoTo target is called" needs a resolver that tries the
name, the zero-padded Messier form, the catalog ids and the common
name before giving up.

**4. The planets are well covered, which is the good news.**

Jupiter, Saturn, Mars, Venus and the Moon all have rows *and*
narratives — Venus 1000 characters, Mars 794, Jupiter 632. Since this
is a planetary rig and a star party will be pointed at a planet most
of the evening, the single most important case is the one that works
best.

### What this means for the design

**The panel cannot depend on prose.** Narrative covers 651 of 14,520
objects, about 4%. A design whose main element is a paragraph is a
design that is blank 96% of the time.

So the panel is built in **layers**, and each layer stands alone:

| Layer | Coverage | Source |
|---|---|---|
| Live sky position | 100% | The app already computes it |
| Physical facts | 100% of catalogued objects | `objects` table |
| **Derived comparisons** | 100% | Computed from the facts |
| Narrative | ~4% | `narratives` table |
| Imaging notes | 26 objects | `imaging_notes` table |

**The derived layer is the idea that makes this work.** The single
most effective educational device is a comparison, and comparisons can
be *computed* rather than written:

- *"The light you are looking at left in 1799."* — from `distance_ly`
  and the date. One line of code, works for every object with a
  distance.
- *"Twenty-five thousand light years away. If the Sun were here, the
  nearest star would be four."*
- *"Eleven times the diameter of Earth."* — from `size_ly` or a
  planet's known radius.
- *"At tonight's 0.203 arcsec per pixel it covers 1,140 pixels of your
  sensor."* — the app already knows the plate scale and the format,
  and phase 92 should use the live values rather than constants, for
  the reason P3.6 option C just established.

Handwritten text covers 4%. Derived comparisons cover everything, and
they are more interesting anyway.

---

## 3. What is on it

Four regions, in reading order, on a 16:9 screen seen from three
metres.

```
┌──────────────────────────────────────────────────────────────┐
│                                                              │
│   JUPITER                                        ♃           │
│   The largest planet in the solar system                     │
│                                                              │
│   ──────────────────────────────────────────────────────     │
│                                                              │
│   43 LIGHT-MINUTES          MAGNITUDE −2.5                   │
│   628 million km            47° above the horizon            │
│                                                              │
│   ┌────────────────────┐    Eleven Earths across.            │
│   │                    │    The light arriving now left      │
│   │   [scale diagram]  │    Jupiter 43 minutes ago.          │
│   │                    │    Rising — best at 01:40.          │
│   └────────────────────┘                                     │
│                                                              │
│   Jupiter is the fifth planet from the Sun and the largest   │
│   in the Solar System. A gas giant with a mass more than     │
│   two and a half times that of all the other planets...      │
│                                                              │
│                                            RadukCapture ·    │
└──────────────────────────────────────────────────────────────┘
```

**Headline** — the name, very large, and one line saying what kind of
thing it is. If a bystander reads nothing else, they read this.

**Figures** — four to six numbers, each with a unit a person uses.
Distance in light-minutes for planets and light-years for everything
else; kilometres beside it because "43 light-minutes" means nothing
until you have seen the other number. Magnitude. Altitude, live.

**The derived panel** — the comparisons above, and a simple diagram.
Not a photograph (see §5).

**Narrative** — the summary when there is one, and **the region simply
is not drawn when there is not.** No "no data available", no empty box
with a heading. An absent section should be invisible.

---

## 4. Hollywood, honestly

The request is for polish, and polish is the point: this stands in
front of people at an event. But science-fiction interfaces have a
characteristic failure, and it is worth naming before any of it is
built.

**Film UIs are full of numbers that mean nothing.** Scrolling hex,
sweeping radar arcs, bar graphs that animate because animation looks
busy. That is the opposite of an educational panel. Every element here
has to be true, or it teaches people that the display is decoration
and they stop reading it.

So the rule for this phase: **everything on screen is a real
measurement, a real fact, or a label.** No filler readouts. No
animated graphs of nothing. If a region has no content, it is not
drawn.

Within that, the polish is in craft rather than clutter:

- **Typography does the work.** A very large condensed name, generous
  letter-spacing on the small caps labels, and a lot of empty space.
  The project already carries Barlow Condensed and Space Mono and they
  are a good pair for this.
- **Depth, not decoration.** A dark field with a slow radial gradient,
  a thin accent rule under the headline, soft shadow behind the
  headline so it sits above the background rather than on it.
- **Motion with a reason.** The panel changes when the target changes,
  so it should *transition* rather than cut: a 400 ms cross-fade, the
  figures counting from the old value to the new. That is motion that
  carries information. A continuously animating element is not.
- **One accent colour, from the object.** A warm amber for Jupiter, a
  pale gold for Saturn, blue-white for Vega — derived from the
  spectral type or the planet, so the screen's colour means something
  too.
- **Live elements earn their movement.** The altitude and the "best
  at" time genuinely change, so they may tick. Nothing else does.

### Night vision

**The panel honours it, and that is a real cost.** At a dark site the
owner's dark adaptation matters more than the panel looking its best,
and a bright screen three metres away ruins it for everyone.

But a red Hollywood panel is a worse Hollywood panel, and this is
explicitly an education item. So: the panel follows night vision like
everything else, **and** the phase should check whether a dim amber
reads better than the standard red tint at this size. That is a
question to answer by looking at it on the night, not in the plan.

---

## 5. Why there are no photographs

The obvious instinct is to put a Hubble image of the object on the
panel. Three reasons not to, and one thing to do instead.

- **Licensing.** Most of the good images are not ours to ship, and
  "it is only for a star party" is how that goes wrong.
- **It competes with the point of the evening.** The share view is
  showing people the *live* image from the telescope in the garden.
  Putting a space-telescope photograph beside it invites exactly the
  comparison you do not want.
- **Disk and build.** Images for 14,520 objects is a different project.

Instead: **a drawn diagram, computed from the data.** A scale
comparison against Earth for planets; for deep-sky objects, the
object's angular size drawn against the camera's field of view, which
is a genuinely useful thing nobody ever shows — "this is how much of
the frame it fills". The app already knows the plate scale and the
capture format, and phase 92 reads them live.

---

## 6. How it is built

**Direct2D, not Win32 controls.** The sky map, the preview and the
share view are all D2D, and that is the right call here: gradients,
soft shadows, cross-fades and free typography are what this needs, and
every one of them fights a control-based layout. It also means the
panel inherits the lessons already paid for — fonts rebuilt on resize
(P3.3), the factory released last (the sky map's startup crash), the
window owning its own render target.

Structure follows `ShareView` closely, because that window is the
nearest thing to this and it works:

- Its own top-level window, class `RadukObjectPanel`.
- `WS_EX_NOACTIVATE` is **not** used. Phase 89 learned that it makes a
  window permanently unfocusable, and with a borderless fullscreen
  window that leaves no way out. `SW_SHOWNA` on show is what keeps
  focus on the control screen.
- Borderless fill of its monitor, `F11` from inside and a
  `Ctrl+Shift+<key>` from the control screen, which is the escape
  route phase 89 had to add after the fact.
- A `workspace` role so it is placed and saved by name, not pinned to
  a display number — `\\.\DISPLAY2` has renumbered twice.
- Fonts rebuilt from the window height on `WM_SIZE`.

**What drives it:** the same source the share view's caption uses —
`gotoPanel.CurrentTarget()`, falling back to the active profile. When
that string changes, resolve it against the database (§2 finding 3),
rebuild the content, and cross-fade.

---

## 7. Phasing

**92a · Make the data reachable.** Ship `sqlite3.dll`, confirm the DB
opens, and add a structured accessor. The existing `Query()` returns a
flat prose blob built for an LLM prompt; a display needs fields. A
`struct ObjectRecord` with the columns, and a `Resolve(name)` that
tries name, `M###` zero-padded, `catalog_ids` and `common_name`.

This phase is worth doing **even if the panel is never built**,
because the object info panel and the AI assistant both already depend
on this database and both have been silently getting nothing.

Gate it with a test: a handful of known objects — Jupiter, M13, Vega,
NGC 6205 — resolve to the right row, and the one that is stored as
`M013` is found when asked for `M13`.

**92b · The panel, plain.** The window, the layout, the live wiring,
the layered content with absent sections not drawn. No animation, no
gradients. Verify it says true things about a dozen objects.

**92c · The polish.** Typography pass, accent colour from spectral
type, the radial field, the cross-fade, the counting figures, the
scale diagram. This is the phase the request is actually about, and it
goes last because polishing a panel that is showing the wrong data is
wasted work.

---

## 8. What it must not do

- **Never invent a fact.** If the distance is unknown, the distance is
  not shown. A panel that makes something up in front of an audience
  is worse than one that is blank, and this is the failure mode the
  assistant already had to be fenced against (P3.6).
- **Never take focus.** Phase 89's whole bug.
- **No controls.** Nothing on it responds to a click. It is a screen
  people look at, and the owner drives it from display 1.
- **No safety-relevant state.** It does not show whether the mount is
  slewing, and nothing on it can move anything.

---

## 9. Risks

| Risk | Handling |
|---|---|
| `sqlite3.dll` is a new runtime dependency | It already is one, silently. 92a makes it explicit and logged, and the app must still start cleanly without it |
| The DB's 4% narrative coverage disappoints | Why the derived layer exists. Test the panel against an object with no prose *first*, not last |
| "Hollywood" drifts into decoration | The rule in §4: every element is a measurement, a fact or a label |
| A second full-screen D2D window costs frames | The share view measured at 2.0 ms a pass after the upload cache. This one redraws only when the target changes, so it is cheaper still |
| Name resolution silently fails and the panel goes blank | Log every unresolved name. A star party is the worst place to discover a lookup bug |

---

## 10. How to know it worked

- Someone who does not own a telescope reads it and can say what they
  were looking at, and one true thing about it, afterwards.
- It is correct for an object with no narrative — most of them.
- It is correct for `M13`, which today's lookup cannot find.
- The owner never has to touch it during an evening.
- It survives a screenshot. The site coordinates are not on it, and
  §8's "never invent a fact" means nothing on it is embarrassing in
  one.
