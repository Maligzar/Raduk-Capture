# Telemetry and Dashboard

An RFC 6455 WebSocket server built directly on Winsock2 — no external
dependency — broadcasting a flat JSON telemetry struct every 500 ms to the
APEX dashboard on the monitoring machine, and accepting a small remote
command set.

---

## WebSocket server

[`src/net/WebSocketServer.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/net/WebSocketServer.h) ·
[`.cpp`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/net/WebSocketServer.cpp)

Minimal, self-contained, pure Win32. Including a **SHA-1 implementation**
used exactly once per client connection to compute the
`Sec-WebSocket-Accept` header (RFC 6455 §4.2.2). That is the entire cost of
not taking a dependency for this.

```cpp
bool Start(uint16_t port = 8765, bool bindAny = false);
```

```cpp
addr.sin_addr.s_addr = bindAny ? INADDR_ANY : htonl(INADDR_LOOPBACK);
```

### Loopback by default — and why

```cpp
// Phase 57 (review S1): loopback-only by default — the WS command set
// slews the mount with no authentication. Set RADUK_WS_LAN=1 to expose.
bool wsLan = GetEnvironmentVariableA("RADUK_WS_LAN", wsEnv, 8) > 0 && ...;
wsServer.Start(8765, wsLan);
```

> The default binds `127.0.0.1` and the LAN exposure is an explicit,
> logged opt-in:
>
> ```
> [INIT] WebSocket server on 0.0.0.0:8765 (LAN exposed — RADUK_WS_LAN=1)
> ```

**Set `RADUK_WS_LAN=1` only on a trusted LAN.** There is no TLS. If the
capture machine is on a network you do not fully control, leave it on
loopback and use a reverse tunnel.

### Every connection needs a token (P5.6)

Loopback was never the whole answer. **Browsers do not apply the
same-origin policy to WebSockets**, so any web page open on the capture PC
could connect to `ws://localhost:8765` and issue `goto_target`
(cross-site WebSocket hijacking). Binding to loopback keeps the LAN out; it
does not keep out the observer's own browser.

So the upgrade request must carry `?token=<WsToken>`. The token is 32
bytes from `BCryptGenRandom`, written as 64 hex characters to `rig.ini`
(untracked) on first launch, compared in constant time, and **never
logged** — the console is the session log, which the AI assistant reads.
A wrong or missing token gets `401` before anything the client sent is
dispatched. The rules fail closed: an empty or hand-weakened token (under
32 hex characters) admits nobody rather than everybody.

The dashboard keeps the token in its own field, in that browser's
`localStorage` only, and adds it at connect time. It is deliberately kept
out of the saved URL, which `apexStorage` also syncs to the helper
server's unauthenticated `/api/store`.

### The parser, tested on its own

The protocol lives in
[`src/net/WsProtocol.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/net/WsProtocol.h),
with no sockets in it, so it can be tested. Three suites gate a merge:

| Suite | What it does |
|---|---|
| `tests/ws_protocol.cpp` | One case per RFC 6455 rule: masking, RSV bits, reserved opcodes, control-frame limits, minimal lengths, fragmentation, UTF-8, the handshake, the token |
| `tests/ws_fuzz.cpp` | Mutated and chunked traffic under AddressSanitizer; a differential (legal traffic must decode identically however it is split); UTF-8 against an independent reference, plus every code point |
| `tests/ws_server_loopback.cpp` | The real server on a real loopback socket: 401 without the token, 101 with it, no `Access-Control-Allow-Origin` |

Defects fixed on the way in: a fragmented command was dispatched as its
**first fragment** and the rest dropped; a frame sent in the same packet
as the handshake was thrown away; unmasked client frames were accepted;
pings got an empty pong; header names were matched case-sensitively.
The server also caps clients at 16 and drops a connection that has not
finished its upgrade in 10 s.

### The include-order trap

```cpp
// CRITICAL: winsock2.h must come before windows.h.
// WIN32_LEAN_AND_MEAN prevents windows.h from pulling in winsock.h (v1),
// which would cause struct redefinition errors against winsock2.h.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
```

`main.cpp` repeats this ordering at the top of the file for the same
reason. It is the first thing in both files, which is where it has to be.

### Broadcast cadence

Every 500 ms from the render loop's readout block. **Session events
accumulate between broadcasts and are flushed as a JSON array each tick**,
so a burst of events during a slew is not lost and does not need its own
channel.

---

## APEX telemetry

[`src/net/APEXTelemetry.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/net/APEXTelemetry.h) ·
[`.cpp`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/net/APEXTelemetry.cpp)

A flat struct whose field names match exactly what the APEX dashboard's RC
tab expects. `ToJSON()` uses `std::ostringstream` — **no external JSON
library.**

| Group | Fields |
|-------|--------|
| Status | `rc_running`, `fps` |
| Camera | `exposure_ms`, `gain`, `gamma`, `wb_red`, `wb_blue`, `usb_bw`, `sensor_temp`, `width`, `height`, `bit_depth` |
| Focuser | `eaf_position`, `eaf_temp`, `eaf_moving` |
| Mount | `mount_ra`, `mount_dec`, `mount_tracking` |
| Recording | `recording`, `frames_recorded`, `frames_dropped`, `limit_frames`, `elapsed_sec`, `bytes_written`, `write_rate_mbs` |
| Buffers | `ram_used_slots`, `ram_total_slots`, `disk_free_gb` |
| Files | `output_dir`, `file_prefix`, `capture_file` |
| Quality | `seeing_score`, `seeing_rms`, `histogram[256]` |
| Events | `session_events[]` |

The `histogram[256]` array is what lets the dashboard draw a live histogram
on the monitoring machine — which is the one piece of telemetry that is
genuinely bulky, and the reason the payload is still small enough for WiFi
is that everything else is scalar.

---

## Remote commands

Parsed from inbound frames:

| Command | Argument | Effect |
|---------|----------|--------|
| `start_capture` | none | Begin recording |
| `stop_capture` | none | Stop recording |
| `load_profile` | name | Apply a named profile — `"Jupiter"`, `"Saturn"`, … |
| `move_eaf` | position | Absolute EAF position |
| `goto_target` | name | GoTo a named object — `"Jupiter"`, `"M42"`, … |

```json
{"cmd":"load_profile","arg":"Jupiter"}
{"cmd":"move_eaf","arg":"14500"}
{"cmd":"move_eaf","arg":14500}
```

Both a quoted and a numeric `arg` are accepted for numeric commands.

### Cross-thread safety

Commands arrive on the WebSocket thread and are **queued, then dispatched
via `PostMessage` to the GUI thread** —
[`src/main.cpp:3130`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/main.cpp#L3130).
Executing `load_profile` on the socket thread would mean calling Win32
control updates from a worker, which is the one rule the threading model
does not bend.

---

## The dashboard

[`RadukCaptureDashboard.html`](https://github.com/Maligzar/Raduk-Capture/blob/master/RadukCaptureDashboard.html)
is a single-file browser dashboard in the repository root (~286 KB,
self-contained). It connects directly to the WebSocket server.

The full APEX integration is a three-column tab on the monitoring machine:
connection and telemetry | preview canvas and histogram | capture
control and seeing monitor, bridged by `apex_bridge.py` connecting to
`ws://<capture-pc>:8765`.

Two Phase 4 items remain deferred:

- `apex_bridge.py` forwarding RC telemetry (emit `rc_running` rather than
  `fc_running`)
- Weather-aware capture suggestions from APEX scoring

**The preview canvas shows no image.** Nothing has ever drawn into it,
and the app sends telemetry and a histogram but no frames. See B3 in
`docs/ROADMAP-Next.md`.

**The dashboard ships no real-world location.** It defaults to 0,0 and
prompts "SET YOUR LOCATION" — see the privacy note below.

---

## RTSP preview stream — designed, not built

Phase 7/8. The intent is H.264 hardware encode (Media Foundation or NVENC)
served over RTSP so OBS on the monitoring machine can take the preview as a
media source at ~5–10 Mbps.

Not built. See [Roadmap and Phase History](Roadmap-and-Phase-History).

The design constraint worth keeping if it is ever built: **raw SER never
crosses the network.** Raw stays on the capture PC, stacks on the capture
PC, and only the finished 10–50 MB stack transfers.

---

## Privacy and network posture

The repository carries a
[security review](https://github.com/Maligzar/Raduk-Capture/blob/master/docs/SECURITY_REVIEW_2026-07-11.md)
from the public-release sweep. Its conclusions, in brief:

**Design change made:** the application no longer ships any real-world
location. Site coordinates default to 0,0 and are set by the operator in
`rig.ini`, which is **untracked and gitignored**. The dashboard prompts for
a location rather than defaulting to one. Session logs and observation
history are untracked. Machine hostnames are resolved at runtime via
`GetComputerName` rather than hardcoded.

**Secrets scan: clean.** No keys, tokens, or credentials. The AI assistant
talks only to a local Ollama instance.

**One finding remains open.** §2 of that review records an action that was
recommended before the repository was advertised publicly and notes that
the mitigation in later commits does not by itself complete it. Read §2
directly before treating the repository as fully sanitised.

**The network surface is small and unauthenticated by design.** One
WebSocket port, loopback by default, with a command set that can move the
telescope. Treat `RADUK_WS_LAN=1` as the security decision it is.

---

**Next:** [Configuration Reference](Configuration-Reference) · [Operations Guide](Operations-Guide) · [Architecture Overview](Architecture-Overview)
