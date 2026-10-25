# RadukCapture — Public-Release Security & Privacy Review (2026-07-11)

Scope: full repository sweep before opening the repo to public pulls —
personal-data exposure, secrets, committed artifacts, and the application's
network attack surface.

---

## 1. Personal data — FOUND AND REMOVED (this commit)

| Finding | Where it was | Severity | Status |
|---|---|---|---|
| **Full street address** (house number, street, city, ZIP) | `RadukCaptureDashboard.html` — default location constant, UI labels, input placeholders | **CRITICAL** | Removed — dashboard defaults to 0,0 / "SET YOUR LOCATION" |
| Precise home coordinates (4-decimal lat/lon ≈ house-level) | Dashboard, `RigConfig.h` defaults, `GoToPanel.h`, `SkyMapWindow.h`, docs | **HIGH** | Removed — all site defaults are now 0,0; real site lives only in the untracked `rig.ini` |
| Observation/session history with dates & conditions (shows when the operator is home observing) | `apex_data/*.json`, `logs/session_*.log` (committed) | HIGH | Untracked + gitignored |
| Town name + Bortle/elevation profile | Code comments, AI prompt, session-log header, help/architecture docs | MEDIUM | Genericized ("the observing site", "default site profile") |
| Machine hostnames (capture/streaming PCs) | Code comments, dashboard connection defaults, architecture doc | LOW | Genericized ("capture PC" / "streaming PC" / `ws://<capture-pc>`); session log now uses `GetComputerName` at runtime |
| Desktop screenshot | `Moon_serttings_usb_frame_dropped.png` (repo root) | LOW | Removed |

Design change: **the application no longer ships any real-world location.**
Site coordinates default to 0,0 and are set by the operator in `rig.ini`
(written next to the exe, now gitignored). The dashboard prompts for a
location instead of defaulting to one.

## 2. ⚠ Git history still contains everything — ACTION REQUIRED

Removing files/strings in a new commit does **not** remove them from git
history. Anyone cloning the public repo can run `git log -p` and recover the
street address, coordinates, session logs, and observation history from
earlier commits.

**Recommendation (before the repo is made public / advertised):** reset
history to a single fresh initial commit:

```bat
git checkout master && git pull
git checkout --orphan public
git commit -m "RadukCapture — initial public release"
git branch -M public master
git push --force origin master
```

This discards all prior history (including this review trail). If the commit
history has value, the alternative is keeping this repo private and pushing a
fresh single-commit copy to a new public repo. **Do not skip this step** —
the address exposure is only mitigated, not eliminated, until history is gone.

## 3. Secrets scan — CLEAN

- No API keys, tokens, passwords, bearer strings, or cloud credentials found
  (pattern scan across all text files; all "token" hits are LLM streaming
  vocabulary, all "33.863" hits are the hPa→inHg constant).
- The AI assistant talks only to a local Ollama instance
  (`localhost:11434`) — no cloud keys involved.
- No `.env`, private keys, or certificate files tracked.

## 4. Committed artifacts — REMOVED

- `build/` and `build_emu/` (180 files including compiled `.exe`s and MSVC
  project files leaking local `D:\` paths) — untracked, gitignored.
- `.gitignore` added (build output, logs, `apex_data/`, `rig.ini`, IDE files).
- `profiles/TargetProfile.h` — stale duplicate of `src/profiles/TargetProfile.h`
  (ODR risk) — deleted; runtime profile `.ini` files kept.
- **Kept, flagged:** `external/ASI_SDK` and `external/EAF_SDK` (ZWO SDK
  headers + DLLs). Redistribution of vendor SDKs in a public repo is a
  licensing gray area — ZWO's developer terms generally allow shipping the
  runtime DLLs with applications, but review ZWO's current SDK license or
  replace with a "download from ZWO" setup step if in doubt.

## 5. Application attack surface — summary

| Surface | Exposure | State |
|---|---|---|
| WebSocket server :8765 (mount/focuser commands, **no auth**) | Loopback-only by default; LAN requires `RADUK_WS_LAN=1` | Hardened Phase 57 (frame-length clamp, recv cap) — acceptable for trusted LAN; do not port-forward. **Added 2026-10-25, P5.6:** a token is now required on every connection, loopback included, and the parser is fuzzed under AddressSanitizer — see `docs/ROADMAP-Next.md` P5.6 |
| Dashboard (browser) | Calls public weather/astro APIs outbound; connects to the WS above | Location now user-supplied; no inbound surface |
| AI assistant | Outbound to `localhost:11434` (Ollama) only | OK |
| ASTAP plate solver | Launches a local `astap.exe`; temp files uniquely named, process killed on timeout | Hardened Phase 54 |
| Serial/COM + ASCOM | Local device I/O only | OK |

## 6. Known remaining code-hardening items (LOW, tracked from the 2026-06 review)

- `ProfileManager` INI values are not clamped against `CameraRails`
  (a corrupted profile can pass out-of-range gain toward the SDK).
- `ObjectKnowledgeDB` `std::stoi` on unbounded digit strings can throw
  uncaught from chat input (local crash, not remote).
- WS server accepts unmasked frames / pongs don't echo payload (RFC 6455
  strictness; no security impact with the loopback default).
- `Broadcast()` holds the client mutex across blocking `send`s (slow-client
  stall, loopback default limits impact).

None of these block a public release; they are code-quality hardening.

## 7. Public-release checklist

- [x] Personal identifiers scrubbed from code, docs, dashboard
- [x] Site defaults to 0,0; operator configures `rig.ini`
- [x] Runtime data (logs, history, rig.ini) untracked + gitignored
- [x] Build artifacts untracked + gitignored
- [x] No secrets in the tree
- [ ] **History reset (Section 2) — required before going public**
- [ ] Optional: ZWO SDK licensing review (Section 4)
- [ ] Optional: add a LICENSE file (repo currently has none — without one,
      "public" code is all-rights-reserved and technically not reusable)
- [ ] Optional: README.md for the public landing page
