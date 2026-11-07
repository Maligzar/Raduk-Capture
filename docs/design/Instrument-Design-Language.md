# The instrument design language

Phase 132's direction, chosen by the owner on 2026-11-07: **a precision
instrument, not a sci-fi screen.** This page is the rule book. `Theme.h` is
where it is enforced, and `src/core/WebTheme.h` carries it to every page the app
writes. When this page and those files disagree, the files are right and this
page is out of date.

## Colour has roles, and only a few

| Role | Token | For | Not for |
|---|---|---|---|
| Void | `BG_VOID` | the preview's background, the deepest surface | anything a person reads |
| Deep | `DEEP` | window and panel backgrounds | |
| Panel | `PANEL` | a raised surface: a card, a cell, a button face | |
| Line | `BORDER` | hairlines and separators | text |
| Text | `TEXT` | primary readouts and values | |
| Dim | `TEXT_DIM` | labels you skim, units, disabled controls | values |
| Signal | `ACCENT` | the one signal colour: active, selected, the current target | decoration |
| Warning | `AMBER` | caution only | a second accent |
| Fault | `RED` | danger and recording only | emphasis |
| Live | `GREEN` | mount connected, tracking, live only | "good" in general |

**One signal colour.** Today's palette has four saturated accents, and they
compete. In the instrument palette, Warning, Fault and Live keep their meanings
but are muted. They appear only when the state is true.

## State is shape and text first, colour second

Night vision turns every colour red, so a state that is shown only by colour is
not shown at night. Every signal carries a shape and a word:

| State | Shape | Example |
|---|---|---|
| Fault, recording | filled circle | `● REC 00:42` |
| Warning | triangle | `▲ DEW RISK` |
| Live | square | `■ CONNECTED` |
| Signal, selected | diamond | `◆ JUPITER` |

`.sig` in `WebTheme.h` draws these in HTML, and the Win32 painters follow it as
each surface moves over in 132.3.

## Type

- **Labels** use Barlow Condensed, upper case, tracked about 0.09 em. They should
  read like an engraved legend, not a heading.
- **Readouts** use Space Mono with tabular figures, so columns line up and a
  changing value does not jitter.
- **Sizes** come from `Theme::Type`: 11, 13, 16, 20 and 28, plus 47 for audience
  surfaces only. No new sizes.

## Surfaces

- **Matte.** No glow, no `text-shadow`, no gradients for decoration.
- **Engraved hairlines.** A 1 px rule in Line with a faint 1 px highlight next
  to it. It reads as a groove cut into the surface.
- **Depth from `Theme::Elev`.** Four levels, each with one shadow. No
  translucency on operator windows.
- **Small radii.** 2 to 3 px. Large rounded corners are the template look this
  phase is removing.

## Motion

None that is not asked for. Arrivals ease out (132.4). Nothing animates while
idle, and reduced motion removes all of it.

## Night

Night vision is a transform of the day palette, not a second palette. Each role
collapses to its designed luminance on Theme's ladder, in red only (132.1). So
changing a day colour never changes the night look, as long as the ladder entry
moves with it, and `tests/web_theme.cpp` checks that it does.

## Checking a palette

`tests\specimen.ps1` writes `data\specimen.html`. It shows each candidate as a
sample panel in day and night, with its contrast against the floors: 7:1 for
Text, and 4.5:1 for every other foreground role, against Deep.
