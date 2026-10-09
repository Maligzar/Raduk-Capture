# Auto-tracking findings — 2026-10-08 Saturn session

Field report: CENTROID, ALIGN and LOCK were each tested on Saturn and
none of them held the planet on the reticle. Saturn was sharp (AUTO
FOCUS, EAF 168187 @ 32.0 C) and sharp captures were taken, so this is
purely a tracking failure, not acquisition or focus.

Root cause: **the mount was never aligned**, and nothing in the app
either performs an alignment or reports that one is missing.

---

## Verified in code

### 1. ★ALIGN is a sync, not an alignment

`SetAlignCallback` in `src/main.cpp` calls `mount->SyncRADec(ra, dec)`
and nothing else. It never calls `SetTrackingMode`, so it cannot start
the drive. A sync states where the mount is pointed; it does not align
it and does not enable tracking.

The return value of `SyncRADec` is discarded, and the notification
reads `★ Aligned to <name>` unconditionally — so a rejected sync still
reports success. On an unaligned NexStar the `S` command is expected to
fail, which makes ALIGN a silent no-op that claims to have worked. This
is the single most misleading behaviour found.

### 2. `SetAlignCallback` is registered twice

Two consecutive registrations in `src/main.cpp`; the setter assigns
rather than appends, so the first is dead code. The surviving one is a
superset (it adds the toast), so behaviour is unaffected — but the dead
copy should go.

### 3. `IsAligned()` is never consulted by the acquisition path

`IMount::IsAligned()` is implemented (NexStar `J` command) but only
`GoToPanel` calls it. CENTROID, LOCK, calibration and the tracking
toggle all proceed without checking, so an unaligned mount produces
silent failure at four separate stages instead of one clear refusal.

### 4. The app has no alignment routine

Only sync. Alignment has to be done on the hand controller
(Solar System Align on the planet is the fastest route for this rig).

---

## Why every acquisition stage fails with TRK: OFF

Drift at 0.203"/px, sidereal 15.04"/s = **74.1 px/s**, 1.72 px/frame at
43 FPS.

| Stage | Requirement | Reality with tracking off |
|-------|-------------|---------------------------|
| Centroid | frame max > 20/255 (7.8%) | fails when the planet is faint or defocused; no centroid at all |
| `IsStable()` | 5 consecutive valid centroids | unreachable without centroids |
| LOCK button | stage==Searching && IsStable() | **silently no-ops**, no log, no message |
| Calibration | measure 800 ms pulse response | pulse moves 29.6 px; drift moves 111 px over pulse+settle — 4x the signal |
| Centering | 8 consecutive frames within 10 px | drift alone covers 13.8 px in 8 frames — arithmetically impossible |
| Locked | correct the residual | authority 7.5"/s vs drift 15.04"/s — half what is needed |

Four of six are direct consequences of the mount not tracking. No
change to the tracker can out-run 15"/s with 7.5"/s of pulse authority,
so alignment is a precondition, not a tuning issue.

---

## Phase 61 item 0 — sharpness follows the planet (DONE)

`FrameAnalyzer` measured sharpness on a crop fixed at the geometric
centre of the frame, while the centroid search deliberately scanned the
full frame. The analyzer would therefore locate an off-centre planet and
report in the same breath that it had no sharpness.

256 px is ~52 arcsec at 0.203"/px, so a planet a minute of arc off
centre measured pure background: `SHARP PK 0% / AV 0%` with the planet
plainly visible. AUTO FOCUS reads the same number, which is why it
refused with "No usable star/planet - sharpness too low" all session.

Fixed: the crop is now centred on the previous frame's centroid when one
is valid, clamped to stay wholly inside the frame, falling back to frame
centre otherwise. A 30-frame (~0.7 s) hold keeps the crop over the
planet when it briefly drops under the centroid threshold -- without it,
an AUTO FOCUS sweep would measure background at the ends of its travel
and put a false floor in the V-curve.

Note for anyone reading the HUD: `PK` and `AV` are the peak and mean of
the rolling SHARPNESS history, not brightness. Clipping is reported
separately via `clipPct` / the [CLIP] overlay. Confusing the two leads
to "reduce exposure" advice when the planet is simply sharp.

---

## Remaining Phase 61 — fail loudly instead of silently

1. **Check `IsAligned()` before acquisition.** Refuse CENTROID/LOCK with
   a plain message ("mount not aligned — align on the hand control
   first") rather than proceeding into a doomed calibration.
2. **Make LOCK explain its refusal.** Currently
   `if (stage==Searching && IsStable())` with no `else`. Log and notify
   which condition failed.
3. **Surface the centroid dim-gate.** When frame max <= 20, say "planet
   too faint to centroid" instead of producing no centroid silently.
4. **Tell the truth in the ALIGN toast.** Report the result of
   `SyncRADec`, and call it "Synced", not "Aligned".
5. **Warn when `TRK: OFF`** at the point acquisition is started.
6. Delete the duplicate `SetAlignCallback` registration.

None of these change the physics. They convert four silent failures
into one accurate message, which is what cost the 2026-10-08 session.

---

## Not yet verified

That an unaligned NexStar refuses tracking and rejects sync is standard
behaviour for these mounts and is consistent with everything observed
(`TRK: OFF` persisting after `ApplyProfile` sends `alt_az`), but it has
not been measured directly on this mount. Confirming it is the first
step: align on the hand control, then re-test CENTROID -> LOCK
unchanged. If tracking holds and lock succeeds, the diagnosis is
closed and Phase 61 is purely about error reporting.
