# Phase 151 plan: airplane mode

The owner's requirement, 2026-11-13: the application must work in airplane mode, and an internet connection is never required. This binds every phase as [rule 7](Phase149-Plan-Any-ZWO-Camera.md#rules-for-every-phase-from-146-on). Status: `PLANNED`.

## Audit, 2026-11-13
Every host the source names was searched for; the results:

| Service | Host | Used by | Without it |
|---|---|---|---|
| JPL Horizons | `ssd.jpl.nasa.gov` | `JupiterDesk` (moon and shadow events), Horizons targets (phase 121) | Jupiter's central meridians and the Red Spot are computed locally (Meeus); moon events come from the cache, three days ahead |
| JPL SBDB close approaches | `ssd-api.jpl.nasa.gov` | `NeoDesk` (NEOs on the map) | The map shows no NEOs |
| CelesTrak | `celestrak.org` | `SatDesk` (satellite elements) | The cached elements, which age: satellite positions degrade over days |
| IMCCE Opale | `opale.imcce.fr` | `PhemuDesk` (mutual events) | The cached season, fetched to its end in 60-day pieces, so it is complete once fetched |
| NTP | `pool.ntp.org` (rig.ini `TimeServer`) | `SntpClient` (mutual-event clock check) | The record says the clock was not confirmed |
| Leoben, the dashboard, Ollama | the LAN or this PC | weather, the remote view, the assistant | Not internet; unaffected |

Planets, the Moon and the Sun are computed locally. ASTAP solves from local catalogs. The knowledge database (SIMBAD) is built once and shipped, not fetched. The dashboard page loads no external assets.

So nothing in capture, focus, guiding or solving needs the internet today. What is not yet proven is that nothing blocks or misleads when it is missing.

## Stages
- **151.0 Prove it.**
  - A global `Offline` state: rig.ini `Offline=1`, or detected when a fetch fails to resolve its host.
  - When offline, no fetch is tried. That avoids WinHTTP timeouts piling up, and their cost.
  - Start-up is timed with the network adapter disabled. It must match the online start-up, and no fetch may run on the UI thread (each Desk already fetches on its own thread; this confirms it).
  - A scenario (phase 125) that runs in the emulator with `Offline=1`: GoTo, lock, autofocus, record, solve, `J`, the sky map, the assistant. It fails if any of them waits on a fetch or reports an error that offers no way forward.
- **151.1 Say how old every fetched thing is.**
  - Each Desk reports its cache's age and coverage in one shared form:
    - "Jupiter moon events: cached 2 days ago, cover to 15 Nov";
    - "Satellite elements: 6 days old: positions may be off by kilometres" (the wording to be set from SGP4's known error growth when built).
  - Shown where the data is used, and in a single status page.
  - Missing data is said plainly; it is never an empty list that looks like "nothing tonight".
- **151.2 Prefetch for a trip.**
  - One action, "Prepare for offline": fetch everything for the next N days (rig.ini, default 14), then report what was fetched and until when.
    - Horizons moon events for the whole window, not three days.
    - The NEO list.
    - CelesTrak elements.
    - The rest of the Opale season.
    - The Horizons targets on the list.
  - For dark-site nights without a connection.
- **151.3 Time without NTP.**
  - The mutual-event recording (141.1) already proceeds with the clock unconfirmed and says so.
  - Add: an optional GPS time source (a USB GPS receiver's NMEA or PPS), the usual offline way to a confirmed clock. Its hardware and driver are to be chosen with the owner before it is designed. Until then the record states the PC clock's last NTP offset and its age.

## Rule for new work
A feature that needs downloaded data must:
- ship the data, or add itself to 151.2's prefetch;
- show the data's age;
- work, or say clearly what it cannot do, with none.

PR reviews check this as they check the version bump.

## Versioning
Every PR bumps PATCH; the PR that completes the phase bumps MINOR ([`ROADMAP-Next.md`](ROADMAP-Next.md#next-in-order)).
