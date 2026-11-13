# Phases 159 and 160 plan: QHY and Player One cameras

From the strategic roadmap review the owner brought on 2026-11-13. The owner reopened the hardware scope, which had been "the ZWO universe" since earlier the same day, to take in QHY (phase 159) and Player One (phase 160). Status: `PLANNED`. Part of release 3.6 ([`ROADMAP-Next.md`](ROADMAP-Next.md#next-in-order)).

## What makes this cheap
- **149.0b:** the app's own pixel format. Frames and profiles carry no vendor types, and CI fails on a ZWO name outside the ZWO backend.
- **149.1:** capabilities, not models. The app asks a camera what it can do (bit depths, binning, cooling, gain range, trigger) instead of knowing models. **Both phases need it first.**
- **153:** the wiring in controllers, so a second backend does not touch `main.cpp`.

## Each phase, the same shape
1. **The backend.** `src/core/QHYCamera.h/.cpp` (159) or `src/core/PlayerOneCamera.h/.cpp` (160) implements `ICamera` on the vendor's Windows SDK: QHYCCD's `qhyccd.dll`, Player One's `PlayerOneCamera.dll`. Both names are recalled; every call is confirmed against the SDK headers when built.
   - Video (streaming) mode for planetary work, and single exposures for long frames (149.3's path).
   - Native bit depth kept (rule 3), and Bayer pattern reported per camera (rule 2).
   - Disconnect and reconnect handled the way the ZWO backend handles them.
2. **Discovery.** The device factory lists cameras from every installed SDK. With more than one, the choice is by name and remembered in rig.ini (149.4 is the multi-camera case).
3. **Vendor names stay in the backend.** CI's vendor step is extended: QHY's and Player One's identifiers may appear only in their own backend files and their emulator.
4. **An emulator mode.** The existing camera emulator presents itself through the new backend's capability set, so the app's paths are tested in CI without the camera.
5. **The SDKs.** Each vendor's SDK and its redistribution terms are read before anything is checked in. `external/` gets the headers and import libraries only if their licence allows; otherwise the build finds an installed SDK.
6. **Validation.** It needs a camera of that make, borrowed or bought, which is the owner's call. Until one is run, the backend is `BUILT, NOT VALIDATED`, and it appears in [`QUALIFICATION.md`](Phase157-Plan-Reliability-Qualification.md) only once measured.

## Rules
Phase 149's rules hold. Rule 7: the SDKs run locally; nothing needs the internet.

## Versioning
Every PR bumps PATCH. MINOR marks a milestone release (3.6), not a phase. One or two PRs a vendor.
