# Phase 129 — the rest of the indoor checks, as scenarios

Picked to keep going while the sky is closed. These are the indoor checklist
items phase 125's scenarios had not reached, run by Claude in the emulator.
Three defects turned up, all fixed here.

## New scenarios

| Scenario | Checks | Checklist |
|---|---|---|
| `camera-boxes` | Typing into the camera boxes. Enter applies, 900 in gain clamps to 570, `abc` and Escape put the camera's value back. Before and after shots show gamma 100 lifting the image and WB red 99 reddening it | 35 |
| `hw-button` | `[HW]` disconnects all three devices, and a second press connects them again | `[HW]` row |
| `panels` | By screenshot: Jupiter's long narrative stays inside the info panel, and the share view's caption is drawn full size | 26, 27 |

New steps:

- `type GAIN 900` types into a camera box and presses Enter. `type ESC:GAIN 300`
  presses Escape instead. `type GAIN ?` only reads the box. Each logs what the
  box shows afterwards.
- `press SHARE VIEW` and `press SHARE VIEW ON` cover Shift+V, which a posted
  key cannot carry.

## Defects found and fixed

1. **Every PNG snapshot had red and blue swapped.** WIC's PNG encoder does not
   take 24-bit RGB: `SetPixelFormat` hands back 24-bit BGR, and
   `SaveSnapshotPNG` wrote the RGB buffer anyway. The live preview was never
   affected, which is why nobody saw it. It surfaced when WB red, in a
   scenario screenshot, turned Jupiter blue. The format the encoder chose is
   now honoured. The window screenshots, which are 32-bit BGRA, were already
   right.
2. **Escape in a camera box left the abandoned number showing.** It cleared the
   edit and waited for a sync to put the camera's value back, and nothing
   syncs on a timer. It now syncs at once.
3. **The share view said "-16° up"** for a target below the horizon. On the
   audience screen it now says "16° below the horizon".

## Not scriptable yet

- **Window restore** (20, 25) needs a restart in the middle of a scenario.
- **STACK and the FITS hand-off** (29, 30) open other programs.
- **The pointing map with a real solve** (44) needs a star field ASTAP can
  solve in the emulator. That is backlogged item 94.4/94.5.
- **A long target name on the share view** (27): no target in the catalogue has
  one.
