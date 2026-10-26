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
loopback and use a reverse tunnel. `WsLan=1` in `rig.ini` does the same as
the environment variable, and is how this rig is set up.

### The home network without a token (`WsLanOpen`, 2026-10-25)

The owner's decision: on a trusted home network, a device should reach the
dashboard with nothing to type. With `WsLan=1` and `WsLanOpen=1`, a peer on
a **private** address -- 10/8, 172.16/12, 192.168/16, link-local 169.254/16
-- is admitted without the token, and the server logs the address it let in.

The cost was stated when it was chosen: any web page open in a browser
anywhere on that network can send mount commands. Two things stay closed
at no cost to the decision:

- **Loopback still needs the token.** A page in a browser on the capture PC
  is the case P5.6 was about, and `[DASH]` supplies the token there.
- **Public addresses still need it.** If the port were ever forwarded, a
  connection from outside meets the same 401 as before.

The rule is `ws::PeerExemptFromToken` in `src/net/WsProtocol.h`, gated by
`tests/leoben_weather.cpp` and a section of `tests/ws_server_loopback.cpp`.
The firewall rule for it is in the help file.

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

**`[DASH]` on the toolbar opens it already signed in.** The app looks
for the page beside the exe and up to four folders above it (the
repository root, from `build\Release`), asks Windows which browser
handles `http`, and hands it `file:///…/RadukCaptureDashboard.html#token=<WsToken>`.
The browser is named explicitly because opening a `file://` URL
through the `.html` association can drop the fragment. The fragment is
never sent to a server; the dashboard copies the token into its box
and storage, strips it from the address bar with
`history.replaceState`, switches to the RadukCapture tab and connects
to `ws://localhost:8765` before its weather downloads start. Neither
the token nor the URL is logged. The path and URL logic is
`src/core/DashboardLaunch.h`, gated by `tests/dashboard_launch.cpp`.

The full APEX integration is a three-column tab on the monitoring machine:
connection and telemetry | preview canvas and histogram | capture
control and seeing monitor, bridged by `apex_bridge.py` connecting to
`ws://<capture-pc>:8765`.

Two Phase 4 items remain deferred:

- `apex_bridge.py` forwarding RC telemetry (emit `rc_running` rather than
  `fc_running`)
- Weather-aware capture suggestions from APEX scoring

**The preview picture** (B3, 2026-10-25). A dashboard that sends
`{"cmd":"preview","arg":"on"}` receives a small JPEG of the preview
about twice a second, as a binary WebSocket message: at most 640
pixels wide, auto-levelled, around 5 KB. It is a framing aid and
nothing more -- the pixels are decimated and stretched, and raw frames
still never cross the network. Clients that do not ask, like
`apex_bridge.py`, are never sent one. The render loop does only the
decimation, twice a second; encoding and sending run on a thread of
their own, and a client too slow to keep up is skipped, then dropped
after a 500 ms send timeout. Measured at the full sensor: capture rate
unchanged, no drops.

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

**The network surface is small.** One WebSocket port, loopback by default
and token-gated, with a command set that can move the telescope. Treat
`WsLan` -- and `WsLanOpen` above all -- as the security decisions they are.

---

## Weather from Leoben

RadukCapture is a *client* of Leoben as well as a server to it. With
`LeobenUrl` and `LeobenToken` set, `LeobenClient` (`src/net/LeobenClient.h`)
fetches `GET /weather/current` every 10 minutes -- every 2 after a failure
-- with 5 s timeouts on its own thread, and keeps the last good reading.
Parsing, unit conversion and the URL rules are pure, in
`src/net/LeobenWeather.h`, gated by `tests/leoben_weather.cpp`.

A reading is used only while **fresh** (30 minutes). Then:

| Consumer | What it gets |
|---|---|
| ADC model | Live **station** pressure and temperature instead of the standard atmosphere and 15 C; `ADCResult::airLive` says which |
| Status window | The `WX` row: Leoben's score and verdict, cloud, wind, dew risk, age |
| Observation record | `wx_source`, `cloud_pct`, `air_temp_c`, `humidity_pct`, `wind_ms`, `gust_ms`, `dew_point_c`, `pressure_hpa`, `imaging_score`, `wx_verdict` |
| Assistant | One measured weather line, or an explicit "unknown -- do not guess" |

**Leoben's `pressure_inhg` is sea-level pressure** (Open-Meteo's
`pressure_msl`). The ADC needs the air above the telescope, so it is
reduced to station pressure with the site's elevation and temperature
(`StationPressureHpa`). Used raw it would overstate dispersion by ~6% at a
500 m site -- the error Phase 66 took out of the elevation model.

---

**Next:** [Configuration Reference](Configuration-Reference) · [Operations Guide](Operations-Guide) · [Architecture Overview](Architecture-Overview)
