# AI Assistant

A local Ollama client given the entire help file as reference plus a live
session snapshot. The design goal is a system that can answer a beginner's
"why does this look wrong" without inventing anything — which turns out to
be mostly a problem of what you put *into* the prompt.

`W` toggles the [advisory loop](Advisors#advisory-loop); the `[AI]` toolbar
button opens the chat panel.

---

## The client

[`src/core/AIAssistant.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/AIAssistant.h)

Phase 50. WinHTTP streaming client against `localhost:11434/api/chat`. No
SDK, no external dependency beyond `winhttp.lib`.

```
POST http://localhost:11434/api/chat
{"model":"llama3.2:3b","stream":true,"messages":[...]}

Response: newline-delimited JSON
{"message":{"role":"assistant","content":"Hello"},"done":false}
{"done":true}
```

NDJSON chunks are parsed on a background thread. **Each content token
fires `OnToken` on that thread** — the caller must `PostMessage` to update
the UI safely. `AIChatPanel` does exactly that.

Running against a local model is a deliberate constraint, not a limitation
to work around: it means no network dependency on a telescope at the bottom
of a garden, no API key, and no cost per question.

---

## Context assembly

[`src/core/AIContextBuilder.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/AIContextBuilder.h)

Six blocks, refreshed on every message:

| Block | Contents |
|-------|----------|
| `[IDENTITY]` | RadukScope persona, site info, **the fitted filter** |
| `[APP REFERENCE]` | The whole of `RadukCapture-Help.md`, cached at startup |
| `[SESSION STATE]` | Live snapshot — see below |
| `[TONIGHT'S SKY]` | Planner data from `GoToPanel` |
| `[CAPTURES TONIGHT]` | SER file listing from the output directory |
| `[OBJECT KNOWLEDGE]` | From the SQLite knowledge base, when available |

`LoadHelpFile()` looks **next to the executable first**, then in the
current directory, and caps at 512 KB.

> This is why `CMakeLists.txt` has a dedicated `copy_help ALL` target. A
> `POST_BUILD` step only fires when the exe relinks, so editing the help
> alone left the deployed copy stale — **and that stale copy is the one the
> assistant actually reads.** Without the copy at all, launching from
> anywhere but the project root left the assistant with "(Help file not
> found — limited app reference available)". It answered anyway, just
> without knowing anything about the application.

---

## The session snapshot

`AISessionState` is filled by `main.cpp` each render tick. What is in it,
and why, is the most interesting part of this subsystem.

### Time, target, mount, camera, focuser, capture

Local time and LST; target and profile name; RA/Dec/Alt/Az and tracking
state; gain, exposure, FPS, seeing score; focuser position and temperature;
recording state, frames recorded and dropped, buffer usage.

### Capture format (Phase 86)

```cpp
int roiWidth, roiHeight, bitDepth;
int sensorWidth, sensorHeight;    // for recognising a crop
std::string limitType; int limitValue;
```

> The toolbar can change these mid-session now, and without them the
> assistant could not answer "am I at full frame", "why is this file so
> big", **or spot that someone is still cropped from an earlier `[ROI]` and
> should press `FULL`.**

Note `sensorWidth`/`sensorHeight` are included purely so the model can tell
a crop from a native mode — the ROI numbers alone do not say.

### Display modes (Phase 87)

```cpp
bool        debayerRaw;      // true = greyscale Bayer, not colour
bool        nightVision;
std::string channelMode;     // "" when showing all channels
std::string displayFX;       // "" when Normal
std::string reticle;         // "" when off
bool        framingShot;     // inside the 5s boost
```

> **Why these are here.** Every one of them changes what the image looks
> like **without changing the capture at all**, and a beginner seeing the
> result has no way to know that.
>
> "Why is my picture grey" is RAW debayer. "Why has everything gone red" is
> night vision. "Why is it suddenly bright and noisy" is a framing shot in
> its five seconds.
>
> Without these the assistant would hunt the camera settings for a fault
> that is not there.

They are plain strings and bools rather than the `gui` enums, so `core`
does not end up depending on `gui` for this.

This block is a good illustration of the general principle: most of the
assistant's usefulness comes from giving it the state that *explains the
symptom*, not the state that describes the hardware.

---

## The instructions it is given

The help file carries a **Notes for the assistant** section, which is the
system prompt in all but name. Condensed:

**Use what you are given.** "Use those numbers rather than asking for them,
and do not state a number that is not in them."

**Safety is not negotiable.** If the Sun is up, or could be, refuse to help
point the telescope anywhere and say why. If the person describes a cable
pulling tight, a grinding noise, or movement they did not ask for, tell
them to press `Space` before anything else.

**Assume no vocabulary.** "The knob that moves the focus" rather than "the
EAF"; "how steady the air is" rather than "seeing", unless they use the
term first.

**Check alignment early.** Planet drifting out of frame, mount will not
track, `TRK` shows OFF — the cause is almost always that Quick Align was
never run. Ask before suggesting anything more elaborate.

**One step at a time**, with what they should expect to see, so they can
tell whether it worked.

**Do not invent numbers.** There is no good value for the sharpness
readings. If asked whether a reading is good, explain that it is relative
and say what to compare it against.

**Dim results are usually the sky, not the rig.** Poor seeing, an uncooled
tube, or a low target explain most disappointing nights, and no setting
fixes them. Say so rather than suggesting changes that will not help.

**Be straight about failure.** "This app reports honestly when autofocus
made things worse or when a check did not pass. Relay that plainly instead
of softening it — a beginner who is told everything is fine when it is not
learns to distrust the whole thing."

**Know what the app cannot see.** The filter, the dust cap, whether the
tube has cooled. The filter is the one that **silently changes a number**:
every dispersion figure assumes the configured band. If someone says they
changed or removed a filter, tell them to set it in SETUP before trusting
any ADC reading. **Do not guess a band from the target or the season.**

**Autofocus is not a find-focus-from-anywhere button.** It searches ±3000
steps around where the focuser already is. Far out of focus means getting
roughly sharp by hand first, or it reports `focus not bracketed` — which is
it working correctly, not failing.

**The display stretch is not an exposure change.** If the image looks too
dark or flat, try the stretch (`L`, Black/White sliders) first: it costs
nothing and changes no data. Raising gain or exposure to fix how something
*looks* makes the recording worse.

**Dark and flat correction change the preview, not the recording.** If
someone asks why their saved file looks noisier than the screen, that is
why, and it is correct.

Those last three are the ones that justify the whole Phase 87 display-mode
block: each is a question whose answer is "a display setting", and without
the state the model cannot see it.

---

## Object knowledge base

[`src/core/ObjectKnowledgeDB.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/ObjectKnowledgeDB.h) ·
built by [`tools/build_knowledge_db.py`](https://github.com/Maligzar/Raduk-Capture/blob/master/tools/build_knowledge_db.py)

Phase 50a. Queries `data/object_knowledge.db` (~2.4 MB SQLite) and returns
a rich multi-paragraph context block for any object:

- Physical data — type, distance, size, magnitude, spectral type
- Wikipedia narrative summary
- Discovery and historical notes
- Mythology and cultural context
- Imaging notes — aperture, filters, what to look for

### Dynamic SQLite loading

```cpp
typedef int (*pfn_sqlite3_open)(const char*, sqlite3**);
// ... resolved from sqlite3.dll at runtime via LoadLibrary/GetProcAddress
```

**No compile-time SQLite dependency, no `.lib`, no vendored source.** Place
`sqlite3.dll` beside `RadukCapture.exe` and the feature appears; omit it and
`ObjectKnowledgeDB` returns `""` gracefully and everything else works.

That pattern — dynamic load, graceful empty result — is used for every
optional dependency in this codebase, and it is why the emulator build
needs nothing but a compiler.

---

## The chat panel

[`src/gui/AIChatPanel.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/AIChatPanel.h) ·
[`src/gui/ObjectInfoPanel.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gui/ObjectInfoPanel.h)

Direct2D-rendered, streams tokens as they arrive, follows night vision like
the rest of the interface. The object info panel surfaces knowledge-base
content for the selected target without a question being asked.

---

## Relationship to the advisory loop

| | Chat panel | [Advisory loop](Advisors#advisory-loop) |
|---|---|---|
| Trigger | You ask | Rig state changed, interval elapsed, not recording |
| Context | Full help file plus full snapshot | Compact digest |
| Output | Streamed conversation | One short line in the notification bar |
| Can act on the rig | No | **No — structurally.** Holds no device references at all |
| Model decides what to say | Within the given context | **No.** Conditions are detected in C++; the model only phrases one |
| Client | Its own | Its own, so the two never block each other |

---

**Next:** [Advisors](Advisors) · [Rendering and UI](Rendering-and-UI) · [Operations Guide](Operations-Guide)
