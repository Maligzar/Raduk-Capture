# Phase 149 plan: ready for any ZWO camera

The owner set this direction on 2026-11-13. The app is a planetary imager today, but it must also image deep-sky objects and stars, with the ASI678MC and with the ZWO cameras that follow it. Status: `PLANNED`. Every phase from 146 on follows the rules at the end of this plan.

## Where it stands
**The seam exists.** P5.2 (2026-10-30) put `ICamera` and `IFocuser` under every call site, and `ICamera::Describe()` already reports what a camera is: `name`, `maxWidth`/`maxHeight`, `isColor`, `bayer` (RG/BG/GR/GB), `pixelSizeUm`, `bitDepth` (`src/core/ICamera.h:103`).

**Much of the app ignores it and assumes the ASI678MC.** Found 2026-11-13:

| What is assumed | Where |
|---|---|
| The camera's name | `CaptureWriter.h` `kCaptureInstrument` (records, FITS `INSTRUME`, the submission sheet), `main.cpp:327` and `:8472`, `APEXTelemetry.h:50`, `PhemuSheet.h:69`, `ShareView.h:62`, the AI prompt's equipment line |
| 3840×2160 | `main.cpp:933` (start-up ROI), `ToolbarStrip.h:44` (FULL), `PreviewRenderer.h:130` (`MAX_SENSOR_W`), `PreviewRenderer.cpp:1210`, `TargetProfile.h:569`, `APEXTelemetry.h:61` |
| 0.203"/px and the 0.13° field | `RigConfig.h:136` (a typed figure, not derived), `PreviewRenderer.cpp:836`, `SkyMapWindow.h:864`, `ASTAPSolver.h:119` |
| Video mode only | `ICamera` has `StartVideoCapture`/`GetFrame` and no single-exposure path; ZWO's SDK has one (`ASIStartExposure`, `ASIGetExpStatus`, `ASIGetDataAfterExp`; recalled) |
| No cooler | `ControlId::TargetTemperature` is mapped, but there is no cooler on/off, power or fan, and nothing reads `IsCoolerCam` |
| Profiles tuned for the 678 | gain and exposure starting points in `TargetProfile.h` |

## Stages
- **149.0 One description of the imaging train.**
  - `CameraInfo` from the camera, plus rig.ini `FocalLengthMm` (and the reducer or Barlow factor) for the optics.
  - Plate scale derived as 206.265 × pixel µm / focal length mm, with rig.ini `PlateScaleArcsecPx` kept as a measured override, since the plate scale is clear-night item 3.
  - The field of view derived from the plate scale and the sensor.
  - Every site in the table reads from this instead of a literal. When no camera is connected, the last one seen is remembered in rig.ini, so start-up and the emulator still have figures.
  - Gated by a test computing the 678's 0.203" and 0.13° from its own description, and a 26 MP APS-C description producing its own.
- **149.1 Capabilities, not models.**
  - `CameraInfo` gains `hasCooler`, `isUSB3`, `maxExposureUs`, `supportedBins`, `supportedBitDepths`, and `hasSnapMode` (true for ZWO).
  - Read from `ASI_CAMERA_INFO` and the control caps, never from the model name.
  - The UI shows only what the camera has: the cooler section only with a cooler, bins only as listed.
  - The emulator gains a second personality, a cooled mono, so every branch runs indoors.
- **149.2 Cooling.**
  - `ICamera`: cooler on and off, target °C, power %, and the fan and anti-dew heater where present.
  - A cool-down and warm-up ramp (a few °C a minute, a setting) so a cooled sensor is not shocked.
  - Temperature and power logged and in the record and FITS (`CCD-TEMP`, `SET-TEMP`).
  - The ASI678MC has no cooler, so this runs on the emulator until a cooled camera arrives.
- **149.3 Single long exposures.**
  - `ICamera::StartExposure`/`ExposureStatus`/`ReadExposure`, beside video mode, built on ZWO's snap mode.
  - Video stays the planetary path; a single exposure is the deep-sky path (seconds to minutes).
  - The capture thread gains a second loop for it: one frame at a time, cancellable, with a timeout from the exposure length (the 135.3 wedge budget generalised).
- **149.4 More than one camera at once.**
  - A guide camera beside the imaging camera (for example a ZWO mini guide camera; the owner's choice later).
  - `DeviceFactory` opens by index or serial, roles are kept in rig.ini (`ImagingCamera`, `GuideCamera`), and each camera has its own capture thread.
  - Only the multi-instance plumbing here; guiding on a star is a later phase.
- **149.5 The filter wheel.**
  - An `IFilterWheel` beside `IFocuser`, with a ZWO EFW implementation (EFW SDK; its calls to be confirmed against the SDK header when built) and an emulator.
  - Named filters in rig.ini.
  - The record's `filter` and FITS `FILTER` (phase 143) are filled from the wheel instead of typed.
  - Per-filter focus offsets for the EAF, which matter for mono imaging.

## Rules for every phase from 146 on
1. **Never name a camera model, a resolution, a pixel size or a plate scale in code.** Read them from `CameraInfo` and the rig's optics.
2. **Never assume colour.** Mono is a first-class case, and every Bayer path checks `bayer != None`.
3. **Never assume 8-bit or 16-bit only.** Sensors deliver 8, 12, 14 or 16 bits, left-aligned in 16-bit words on ZWO. Store the native depth; do not shift away information.
4. **Never assume video.** A frame may take minutes. Timeouts, progress and cancellation scale with the exposure.
5. **Never assume one device of a kind.** Pass the device; do not reach for a global.
6. **Formats keep their roles.**
   - SER and AVI are video formats.
   - FITS is the primary format for deep sky: the native depth, plus the header the deep-sky tools read.
   - TIFF is for exchange.
7. **Nothing ever needs the internet** (the owner, 2026-11-13). Capture, focus, guiding, plate solving, the sky map, the assistant and every file the app writes work in airplane mode. Anything fetched online is an enrichment: it is cached, shows its age, says plainly when it is stale or missing, and never blocks start-up or a capture. A new feature that needs a download ships its data or a prefetch for it. See [Phase 151](Phase151-Plan-Airplane-Mode.md).

## Not in scope here
- Deep-sky capture itself. Sequences, dithering, calibration frames, guiding on a star and stacking are [Phase 150](Phase150-Plan-Deep-Sky-Outline.md).
- Non-ZWO cameras. The interface allows them; the owner's plans are in the ZWO family.

## Versioning
Every PR bumps PATCH; the PR that completes the phase bumps MINOR ([`ROADMAP-Next.md`](ROADMAP-Next.md#next-in-order)). Six stages, about four PRs. 149.0 should come first and soon: it removes the most assumptions and blocks nothing.
