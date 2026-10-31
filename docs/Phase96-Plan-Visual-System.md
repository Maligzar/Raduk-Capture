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
| **96.3** | Audience surfaces: share view and object panel get the full treatment. Hero framing, ornament, depth, the richness the screenshots are actually showing | M | Where the references genuinely apply, and where a win is most visible to other people |
| **96.4** | Operator surfaces: the same system at low volume. Depth and spacing on the control panel, toolbar and GoTo panel; chrome recedes, the image gains | L | The biggest surface area and the most restraint required. Benefits most from 96.2 existing first |
| **96.5** | Motion: transitions and easing on panel reveals, mode changes, state transitions | S | Disproportionate effect on perceived quality for the code it costs, but worthless on top of an inconsistent system, so it goes last |

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
