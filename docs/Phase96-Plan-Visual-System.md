# Phase 96 plan — a visual system that survives the dark

> *"plan a phase to raise the bar on visual presentation. see example from
> this league of legends snapshot. same with WOW. there is a modern look
> to this that I think we can build into RadukCapture if you are up for
> the challenge"*

Up for it. But the two references disagree with each other, and this app
disagrees with both, so the first job is working out which parts
transfer.

---

## 1. The measurement that decides the whole phase

**Half the time, this application has no colour at all.**

Night vision (phase 26) collapses every painted colour to its luminance
and pushes it into the red channel, because short wavelengths destroy
dark adaptation. `Theme.h` already says the accent hues converge and
that this is inherent rather than a bug. What it does not say is by how
much. Measured:

| Token | Night-vision luminance |
|---|---|
| `ACCENT` cyan | 182 |
| `ACCENT2` amber | 186 |
| `ACCENT3` green | 209 |
| `TEXT` | 226 |
| `TEXT_DIM` | 169 |
| `ACCENT4` red | 103 |
| `PANEL` | 20 |

Cyan and amber land **four units apart out of 255**. Green sits closer
to body text than to the other accents. Four accent colours become
three indistinguishable brights and one mid.

### And the hierarchy inverts

Look at the last two rows against each other. `TEXT_DIM`, the token for
*secondary labels you are meant to skim past*, reads at 169. `ACCENT4`,
the token for **capture and danger**, reads at 103.

In night vision, dim secondary text is **sixty per cent brighter than
the danger colour.** The thing that should shout is quieter than the
thing that should whisper, on a rig that spends its working life in
exactly that mode. That is a defect, not a preference, and it was found
by measuring the palette rather than by looking at it.

### What this means for the references

The League client's look is, very roughly, forty per cent colour: the
gold ring, the teal accents, the warm-on-cool contrast that makes the
whole frame read as rich. **All of that evaporates here.** Copy it
faithfully and night mode gets a uniform red smear with no hierarchy at
all.

The other sixty per cent survives untouched, because it is built from
things luminance cannot flatten:

| Survives night vision | Does not |
|---|---|
| Depth: shadow, elevation, layering | Hue |
| Silhouette and shape language | Warm/cool contrast |
| Typography: weight, size, tracking | Saturation |
| Spacing and rhythm | Colour-coded state |
| Ornament and line weight | |
| Motion and easing | |

So the phase builds the look out of the surviving column. That is a
constraint worth having: it is also the column that ages best, and the
one a screenshot cannot fake.

---

## 2. Where the two references part company

They are not one style, and the difference matters.

**The League client is 90 per cent chrome.** A hero background, an
ornate frame, a huge shaped call to action. It is a lobby: the interface
*is* the content, so it can be as loud as it likes.

**Battle.net is restrained by comparison.** Flat dark neutrals, a single
blue accent, an underline for the active tab, rounded cards. Its job is
to get out of the way of a carousel.

**RadukCapture is neither.** Its main surface is a live telescope image
at up to 200 fps, and the image *is* the content. Chrome that competes
with it is chrome that has failed. An instrument at 2am is also judged
on something a lobby never is: whether a tired person reads it
correctly at a glance.

So the League treatment is right for part of this application and wrong
for most of it. Which part is already decided.

---

## 3. The split this project already made

Phase 89 and phase 92 established it, and `Phase92-Plan-Object-Panel.md`
states the rule: *"the owner works on display 1, everything else is for
other people."*

| | Operator surfaces | Audience surfaces |
|---|---|---|
| Who | the owner, working | guests, at a star party |
| Examples | control panel, toolbar, GoTo panel, mount controller, HUD | share view (89), object panel (92) |
| Judged on | read correctly, tired, in the dark | impressive from across a room |
| Chrome should | recede | perform |
| Reference | neither, really | **the League client** |

**The screenshots belong on the audience surfaces.** That is where the
hero framing, the ornament and the richness have a job to do, and the
object panel was already written to be *"high polished in Hollywood
style"*. It is also where night vision matters least, because a guest
looking at a display is not the one who needs dark adaptation.

The operator surfaces get the same *system* at a much lower volume:
better depth, better type, better spacing, no ornament competing with
the sky.

---

## 4. What is actually there now

Worth stating, because the starting point is better than "no design".

`Theme.h` is 243 lines and already a real token system: a named palette,
night vision applied at the point of use rather than at definition, and
a GDI object cache keyed on a palette generation counter so a mode
toggle rebuilds exactly once. The comment explaining why that cache
exists describes a bug it was written to prevent. This is not naive
code.

What it does **not** have is anything for the surviving column in
section 1. There is no elevation scale, no corner radius, no spacing
scale, no type ramp. Colour is the only axis the system models, and
colour is the axis that disappears.

**Two painting stacks.** Most panels paint with GDI (`ControlPanel.cpp`
and friends); the preview, sky map, share view and object panel paint
with Direct2D. Gradients exist in exactly three files, all D2D, all
recent. Any token added has to be expressed once and consumed by both,
or the look will land unevenly and drift apart. `Theme.h` already does
this for colour, with `NV()` for GDI and `NVf()` for D2D; the new tokens
follow that shape.

21,497 lines of GUI across fifteen surfaces. This is not a repaint.

---

## 5. How to tell whether it worked

"Looks better" is not testable and this plan will not pretend otherwise.
What **is** testable is the system's invariants, and those are what stop
a design decaying three phases later:

- Every accent keeps a stated minimum luminance separation from
  `TEXT_DIM` **after** the night-vision transform. The inversion in
  section 1 fails this test today, which is how it was found.
- The type ramp is monotonic in size and does not skip a step.
- Elevation levels are ordered, and each has exactly one shadow
  definition rather than per-site guesses.
- No painting site uses a literal colour where a token exists. This is
  greppable and the project has been bitten by the literal-constant
  pattern twice already (the blur per step, the plate scale).

The rest is judged by eye, by the owner, in the dark, which is the only
venue that counts.

---

## 6. Staging

| Stage | What | Size | Why here |
|---|---|---|---|
| ~~**96.1**~~ | ~~Fix the hierarchy inversion~~ **DONE 2026-10-31.** Each token declares its own night luminance as a semantic ladder; danger is now the brightest thing the UI can paint | S | A correctness bug, found by measurement. Independent of everything else and worth doing even if the rest is dropped |
| ~~**96.2**~~ | ~~Extend `Theme.h` into the surviving column~~ **DONE 2026-10-31.** Type ramp, spacing scale, elevation with one shadow per level, plus the invariant tests. Measuring first found the same defect in a second axis: the type sizes were one pixel apart, and the two painting stacks disagreed about the ramp | M | Nothing else can be consistent until this exists. No visible change on its own, which makes it the easiest stage to skip and the most expensive one to skip |
| ~~**96.3**~~ | ~~Audience surfaces: share view and object panel get the full treatment~~ **DONE 2026-10-31.** Scaled type ramp, depth under the caption, corner marks on the sensor frame, figures as raised cards. Measuring first found something larger than a style gap: the two audience surfaces painted **eighteen literal colours and zero palette tokens**, so 96.1's night ladder had never reached either of them. The recording light on the screen pointed at the audience derived to 86 where the ladder puts danger at 240 — dimmer than the caption beneath it | M | Where the references genuinely apply, and where a win is most visible to other people |
| ~~**96.4**~~ | ~~Operator surfaces: the same system at low volume~~ **DONE 2026-10-31**, except the type and spacing retrofit, which is **96.4b** and needs a screen to look at. Measuring found the stage was not about taste at all: **53% of the operator chrome's 156 painting sites were off the ladder**, and the misses included the whole three-tier warning system. In night vision the status window read critical 103, caution 175, good 150, labels 134 — **critical was the dimmest of the four**, and 103 is the exact figure 96.1's plan named as the defect it existed to fix. Coverage is now 78%, depth has GDI primitives that composite on the right side of the night transform, and `Theme::DEEP` turned out to be painted by nothing at all | L | The biggest surface area and the most restraint required. Benefits most from 96.2 existing first |
| **96.4b** | The type and spacing retrofit on the operator surfaces: six near-identical font sizes onto three ramp steps, and the local spacing literals onto `Space::` | M | Deliberately deferred. `ControlPanel.cpp` has **six measured-text calls in 2424 lines** — it lays out by hard pixel offsets, so growing a label inside a static sized to fit clips it, and no one in a container can see that happen. Every off-ramp site is marked in the source with the step it wants, so this is mechanical for whoever has the screen |
| **96.5** | Motion: transitions and easing on panel reveals, mode changes, state transitions | S | Disproportionate effect on perceived quality for the code it costs, but worthless on top of an inconsistent system, so it goes last |

### What 96.3 changed about the plan itself

Two things in the sections above turned out to be wrong, and both were
found by retrofitting rather than by reading.

**Section 5's "no painting site uses a literal colour where a token
exists" was greppable and nobody had grepped it.** The answer on the
two audience surfaces was eighteen literals and no tokens, which meant
96.1 — a correctness fix to a safety-adjacent colour — had landed on
every operator surface and reached neither screen that other people
look at. The REC light derived to 86 against the ladder's 240 for
danger; the caption line beside it derived to 173 against TEXT_DIM's
80. In night vision the recording indicator was the dimmer of the two.
That is the same inversion 96.1 was written to kill, surviving in the
one place a bystander would see it.

**96.2's ramp was a document ramp, not a display one.** It was derived
from the sizes the code already used, and every one of those was on an
operator surface, where the largest thing is a section header. It spans
2.15:1 from body to top. The object panel has a real display headline
and had measured its own answer by eye at 3.63:1. Putting its title on
the 96.2 ramp unchanged would have shrunk the object's name by forty
per cent. `Type::DISPLAY` was added rather than the measurement
discarded, sized so the tuned title is reproduced to within a third of
a pixel at 1080p. The lesson generalises to 96.4: a ramp derived from
one half of the application does not automatically fit the other.

### What 96.4 changed about the plan again

The pattern from 96.3 repeated, and it is now the most reliable finding
of this whole phase: **the fix lands on the token and the code does not
use the token.**

**96.1's headline fix had reached six painting sites.** `ACCENT4` is
`RGB(255, 59, 92)`. Eleven recording and fault indicators across four
files spelled `RGB(255, 60, 90)` — one green unit and two blue units
away — which `NightLuminance` does not recognise, so it fell through to
the derived value of **103**. That is the number 96.1's own plan cites
as the defect. The recording indicator, the dropped-frame warning, the
disk-space critical, the write-buffer warning, the dew-risk warning,
the mount's fault light, the destructive-action toast and the ADC
alignment warning were all still painting it.

**The whole warning system was off the ladder, not just the top tier.**
Critical, caution and good were three literals in the same ternary, and
in night vision they read 103, 175 and 150 against secondary labels at
134. Every tier was wrong and the order was almost exactly reversed.

**And a token can be dead.** `Theme::DEEP` was `(7, 8, 14)` and nothing
in the application ever painted it; eighteen sites spelled
`RGB(7, 13, 22)` and commented it `--bg-deep`. 96.1 gave DEEP a
designed luminance of 14 that no pixel has ever been. `DrawSectionHeader`
in `ControlPanel.cpp` is dead the same way — 96.4 went there first to
adopt depth and would have adopted it nowhere.

So section 5's test, "no painting site uses a literal colour where a
token exists," was the most valuable line in this plan and it took
three stages for anyone to run it. The lesson for 96.5 and after:
**a palette fix is not done when the token changes, it is done when
the painting sites are counted.** `Theme::Rgb::*` exists so the count
stays at 100 for the sites it covers, because a typo is now a compile
error rather than a colour 137 luminance units wrong in the dark.

### One decision left to the owner

Using the device-role accents as warning tiers borrows semantics they
were not designed for, and it shows: `ACCENT3` (mount, live) is 170 and
`ACCENT2` (EAF, secondary) is 110, so **"good" now outranks "caution"**.
For a warning tier that is backwards.

Fixing it properly means the ladder gains a caution rung, and the
ladder is full: 240 down to 80 at the 24-unit minimum separation holds
seven rungs and six are spoken for, so inserting one re-spaces all six
signals and changes the whole operator look in the dark. That is not a
decision a palette pass should make on its own, so it is not made. The
test prints the two numbers and says so.

### Recommendation

**Do 96.1 now, on its own.** It is a measured defect in a safety-adjacent
colour (capture and danger), it is small, and it needs no agreement
about taste.

**Then 96.2.** It is the unglamorous one and it is the one that decides
whether the rest is a system or a series of opinions.

**96.3 before 96.4.** Smaller, higher visible payoff, and it is the half
where the reference material is genuinely applicable, so there is less
to get wrong.

Confidence that 96.1 is worth doing regardless: high, it is arithmetic.
Confidence on the ordering of the rest: medium, and it would change if
a star party were imminent, which would promote 96.3.

---

## 7. What this phase will not do

- **No new UI framework.** Win32 and Direct2D stay. The look comes from
  how they are used.
- **No colour-coded meaning.** Anything that must be distinguishable in
  night vision has to be distinguishable by shape, position, size or
  text. That rule falls out of section 1 and it constrains 96.3 and
  96.4 throughout.
- **No touching the preview's hot path.** `PreviewRenderer` runs per
  frame at up to 200 fps, and phase 91 and 93 both spent real effort
  getting work out of it. Chrome changes stop at the HUD.
- **No judging it by screenshot.** The venue is a dark room at 2am.

---

## 96.5 as built: motion

The stage was scoped as "transitions and easing on panel reveals, mode
changes, state transitions". What measuring found first was that the
app's one real transition had never run.

### The inventory

Four timed visual transitions existed before this stage:

| Site | What | Duration | Curve | Stack |
|---|---|---|---|---|
| `ObjectPanel` | cross-fade on target change | 450 ms | linear | D2D |
| `ShareView` | hint fade-out at end of dwell | 1500 ms ramp, 8 s dwell | linear | D2D |
| `NotificationBar` | appear and vanish | hard cut, 8 s dwell | none | GDI |
| `ConfirmToast` | appear and vanish | hard cut, 5 s dwell | none | GDI |

### The defect

`ObjectPanel`'s cross-fade has no frame source. It arms in
`SetContent`, and the only thing that calls `Draw()` again is the
500 ms readout poll that feeds `SetContent` -- 2 s while capturing.
The file contains no `SetTimer` at all.

So a 450 ms fade got one frame, at `t` near zero and therefore alpha
near zero, and the next frame arrived 500 ms later with the fade
already expired. It did not fade. It blanked the panel and snapped
back a poll later, and while capturing it sat blank for up to two
seconds on a target change.

This is the same shape as the seven dead things phases 96 through 100
turned up: the gap is between written and reachable, not between wrong
and right. The fade is correct code. Nothing ever ran it.

Fixed by arming a `FRAME_MS` timer on the edge that arms the fade and
killing it on the frame the fade completes, so the timer exists only
while a transition is running.

### The curve, and a correction

Both existing fades ramped alpha linearly. Alpha compositing is linear
in luminance; perceived lightness is not. CIE L\* goes as the cube root
of luminance, so a linear alpha ramp front-loads the apparent change.

My first conclusion was that the corrective exponent is 3 -- alpha to
luminance to L\* suggests a cube. That is wrong. `L* = 116·Y^(1/3) − 16`,
and the −16 offset is not negligible when night-mode text only reaches
L\* 42. Measured against the app's own tokens, in `tests/motion.cpp`:

| Case | Half-lightness reached at | Exponent |
|---|---|---|
| night TEXT 200 over DEEP 14 | alpha 0.255 | 1.97 |
| day TEXT over DEEP | alpha 0.186 | 2.42 |

So the curve is a **square**. It is within 0.23 L\* of perceptually
linear in the night case -- below the ~1.0 L\* just-noticeable
difference, i.e. exact for the condition this app is built for -- and
6.1 L\* off by day, where it errs bright, meaning a fade-in arrives a
shade early rather than lurking.

A plain power law is adequate at all only because both surfaces are
near-black: the backdrop carries under 1% of the foreground's luminance
in both cases, so no per-call backdrop luminance is needed. The test
asserts that, so if a later phase lifts the panel background it fails
first.

### The ladder

Half-decade (×√10) anchored at 150 ms, giving true rungs of
150 / 474 / 1500. The anchor is not a preference: the two durations
already in the app were 450 and 1500, and those are rungs 1 and 2 to
within 5%. 450 is kept over 474 because it is the measured value and
24 ms is under two frames at 60 Hz.

Dwells are reading time, not motion, and were three numbers in three
files with nothing saying why. Now two, and the difference is a
decision: a confirmation tells you what you just did, so you already
know it (5 s); a notice tells you something you did not know (8 s).

### Deliberately not done

Two things that look like motion opportunities and are not:

**A pulsing REC indicator.** Phase 98 added a recording timer to the
panel and the capture card. That is changing text, which is already
motion carrying information. A pulse would be redundant with it, and
would add a repaint where none existed.

**A sliding `NotificationBar`.** The bar is a `WS_CHILD` whose height
goes from 0, and showing it pushes the content below it down. That
displacement *is* the motion that tells you something happened. A fade
on top would add nothing, and animating the height would re-run the
main window's layout on every frame.

The one surface that got genuinely new motion is `ConfirmToast`, where
the motion is the information -- something arrived, and it arrived just
now -- and which was the one surface that cut. It is `WS_POPUP`, so
`WS_EX_LAYERED` gives it a compositor-applied opacity and not one line
of its drawing code knows it is fading.

### Still open

**96.4b**, the type and spacing retrofit on the operator surfaces,
remains deferred. `ControlPanel.cpp` lays out by hard pixel offsets
(six measured-text calls in 2,424 lines), every off-ramp font site is
marked in the source with `96.4b` and the step it wants, and it needs
someone watching the screen.

**The caution rung.** `ACCENT3` at 170 outranks `ACCENT2` at 110, so
"good" reads brighter than "caution". The ladder is full, so inserting
a rung re-spaces all six signals. An owner decision, not a bug.
