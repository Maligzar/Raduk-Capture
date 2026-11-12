# Phase 144 plan: Jupiter's events put to work

Chosen by the owner 2026-11-12 ("Jupiter events in Autorun"). It finishes
what Phase 137 left undone: "Autorun and the assistant's snapshot do not
use it yet." Built 2026-11-12, as 3.2.0.

## Why

Phase 137 lists the Red Spot's transits and the moons' events on `J`, so
the information exists. Nothing acts on it, though:

- Autorun takes a focus sweep whenever its count says so, even in the
  middle of a shadow transit that will not come back for days.
- The run ranking does not say which runs show the Red Spot or a shadow.
- The assistant cannot answer "when is the Red Spot up tonight?"

Opposition is about 2027-02-10, so this season's Jupiter nights are the
ones it pays on.

## What it does

**`src/sky/JupiterPlan.h` (pure).**
- `OnDisc(jd)` says what is on the disc at a moment:
  - the Red Spot, when it is within 50 minutes of the meridian (signed
    minutes, at System II's rate);
  - moons and shadows crossing the disc;
  - moons that are occulted or eclipsed.
- `Prime()` is true when the Red Spot is near the meridian, or a moon or
  its shadow is crossing. An occultation or eclipse alone hides a moon,
  so it does not count.
- `DiscLine` puts the moment into words, for example "Red Spot 12 min past
  the meridian; Io shadow transit".
- `PrimeEndJd` says when the current spell ends.
- `TonightLines` gives the next hours, in time order, keeping only what
  the site can see.

The 50-minute window is the usual amateur reckoning for "well placed",
about 30 degrees of longitude. It is inferred, not measured, so it is a
constant until a night says otherwise.

**Autorun (`AutorunRunner`).**
- `Config::deferFocusInPrime` and `primeDeferMaxSec` let a focus sweep
  that falls due during a prime spell wait while the runs go on.
- The wait lasts until the spell ends, or for `AutorunPrimeDeferMin`
  minutes at most (rig.ini; default 30, 0 turns it off, 120 is the
  highest it takes).
- It applies only when the sequence starts on Jupiter. Other targets are
  unchanged.
- The log says when a sweep is put off, and until when.
- The end line counts the runs recorded while a sweep was due.

The reasoning: a sweep costs one to three minutes, focus drifts over tens
of minutes, and a transit does not come back for days. The 30-minute cap
is a starting figure.

**The run ranking.** Each run of a Jupiter sequence carries what was on
the disc as it started. `autorun_<date>.txt` shows it in brackets after
the file name, so the runs to stack for the Red Spot can be told apart.

**The assistant.** The session snapshot gains a Jupiter section, rebuilt
once a minute:
- what is on the disc now;
- the next 8 hours of Red Spot transits (each with its well-placed span)
  and moon events, while Jupiter is above 15 degrees and the Sun is below
  -6 degrees.

The section appears only when Jupiter is the target or the site is
known, so a Saturn night is not padded with Red Spot times. The prompt
labels it as computed, with times in UTC.

## Gates

- `tests/jupiter_plan.cpp`:
  - the Red Spot's signed minutes, zero at the transit Phase 137 finds;
  - the window's edges;
  - prime against not prime;
  - the line text;
  - when a spell ends;
  - the assistant's lines, in order and filtered by the site.
- `tests/autorun.cpp`, the prime-spell section:
  - the cadence is unchanged outside a spell or with the setting off;
  - a long spell gives runs and no sweep;
  - the sweep follows the spell's end;
  - the cap forces it after two runs.
- `tests/ai_context_facts.cpp`: the section reaches the prompt, marked as
  computed and in UTC, and is absent when empty.

## Not done

- The observation record and FITS do not carry the disc line yet. The run
  ranking does.
- Prime spells are not chained: an event that starts inside a spell
  extends the wait only when it is checked again, before the next run.
  Each check sees what is under way at that moment.
- Never run on the sky. The checklist item is 57.

## Versioning

The owner's rule ([`ROADMAP-Next.md`](ROADMAP-Next.md#next-in-order)):
- Every PR bumps PATCH.
- The PR that completes a phase bumps MINOR.

This one PR completes Phase 144, so it takes 3.2.0. If another PR merges
first at 3.2.0, this one becomes the higher number plus one PATCH.
