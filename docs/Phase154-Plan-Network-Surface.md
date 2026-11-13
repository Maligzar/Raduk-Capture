# Phase 154 plan: the hand-written network code

From the owner's weakness review, 2026-11-13. Status: `PLANNED`.

## The problem
- The dashboard and telemetry run on a hand-written RFC 6455 WebSocket server (`src/net/WebSocketServer.cpp`), with a hand-written frame parser (`WsProtocol.h`) and hand-written JSON.
- The reason was "no vcpkg". The cost is attack surface and maintenance: this is the only network-facing C++ in the project, and it accepts commands that move a telescope.
- **What mitigates it today** (P5.6, 2026-10-25):
  - loopback only by default (`WsLan` opens it to the network);
  - a token on every connection;
  - the frame parser fuzzed (`tests/ws_fuzz.cpp`, AddressSanitizer);
  - a 500 ms send timeout on every socket.
- **What is not covered:** the HTTP upgrade parsing and the JSON command parsing get only partial fuzzing, and the fuzzer is a random-input loop, not coverage-guided.

## Stages
- **154.0 Measure the surface.**
  - List every byte path from a socket to a decision: the upgrade request, the frame header, masking, fragmentation and continuation, close and ping, the JSON command, the command dispatch.
  - For each, record whether a test or fuzzer reaches it.
- **154.1 Coverage-guided fuzzing.**
  - Build `WsProtocol.h`, the upgrade parser and the JSON command parser as libFuzzer targets with clang on Linux: the parsers are pure, so they build without Windows.
  - Run each for a fixed time in CI, as a separate job so the `windows` gate is not slowed.
  - Keep a corpus in the repo.
  - Every crash becomes a regression test.
- **154.2 Decide what to replace, with the fuzzing in hand.** Options, with no dependency manager in either case:
  - **The JSON:** vendor a small, well-fuzzed header-only parser into `third_party/`. A single header with a compatible licence, pinned to a version and checked in, works offline (rule 7). Or keep ours if 154.1 finds nothing.
  - **The WebSocket server:** Windows' own HTTP server (`http.sys`) with its WebSocket Protocol Component API takes the framing out of our code. That this API pair does the whole handshake and framing is recalled, not confirmed; 154.2 confirms it with a prototype before deciding. The cost is a rewrite of the server's I/O and a dependency on Windows behaviour that cannot be fuzzed.
  - **Keep both, hardened**, if 154.1 runs clean for a long stretch and the prototype is not clearly simpler.

  The decision and its evidence go into this file. The owner's direction (the dashboard on the home network, airplane mode) is a constraint either way.
- **154.3 Defence in depth, whatever 154.2 decides:**
  - a size cap on every message;
  - a rate limit on mount commands;
  - commands that move hardware refused unless the connection authenticated with the token, even on `WsLanOpen` (today `WsLanOpen` exempts private addresses);
  - a log line for every command that moves hardware, with its source address.

## Versioning
Every PR bumps PATCH; the PR that completes the phase bumps MINOR ([`ROADMAP-Next.md`](ROADMAP-Next.md#next-in-order)).
