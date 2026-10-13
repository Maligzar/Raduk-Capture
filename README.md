# RadukCapture

High-speed planetary capture suite for Windows, built for the Celestron
NexStar 8SE + ZWO ASI678MC (and adaptable to similar rigs). Pure Win32 +
Direct2D — no frameworks.

## Highlights

- **SER v3 recording** with capture-time trailer timestamps (WinJUPOS-ready),
  RAM-first write buffering, lucky-imaging quality scores per frame
- **Capture format on the toolbar** — resolution presets from full sensor down
  to 640x480 and 8/16-bit depth, applied through one shared reconfigure path,
  so an ROI crop is always recoverable
- **Target profiles that actually load** — picking an object on the sky map or
  in GoTo applies that target's gain, exposure, resolution, bit depth, focus
  position and capture limit, and the control panel updates to match
- **Closed-loop planet tracking** — three-stage centroid acquisition with
  PHD2-style measured guide calibration (camera rotation, plate scale, and
  guide rate are measured, never assumed)
- **GoTo + sky map + plate solving** — Messier/planet catalog, D2D polar sky
  map, ASTAP plate-solve → mount sync
- **EAF V-curve autofocus** — real per-position averaging, a parabola fit it
  will reject, and an honest report when it made focus worse
- **Quick-look stacker** — aligned, quality-gated preview stack while you
  record, so you know that night whether a capture was soft
- **Atmospheric-dispersion advisor** — Edlen 1966 model over the band of the
  filter you actually have fitted, plus auto-exposure and capture quality
  gating
- **Full device emulators** — camera, mount, and focuser simulate the real
  SDK semantics (closed tracking loop, fault injection, defocus and sensor
  noise, NexStar slew-rate curve, per-planet rendering), so the entire app
  runs with no hardware attached and focus work can be tested indoors
- **Guidance for non-experts** — a readiness check that stays silent when the
  rig is fine, and a local AI assistant (Ollama) given the help file as
  reference plus a live session snapshot: mount position, camera settings,
  capture format and whether it is cropped, which display modes are altering
  the image, seeing, focus, and tonight's imaging windows
- **WebSocket telemetry** + browser dashboard for remote monitoring
  (loopback-only by default; `RADUK_WS_LAN=1` to expose on a trusted LAN)

## Building

Requires Visual Studio (MSVC, C++20) and CMake.

```bat
:: Emulator build — no hardware or SDKs needed
cmake -S . -B build_emu -DRADUK_EMULATOR=ON
cmake --build build_emu --config Release
build_emu\Release\RadukCapture.exe

:: Hardware build — uses the ZWO SDKs in external/
cmake -S . -B build
cmake --build build --config Release
```

## Configuration

`rig.ini` sits next to the executable, is **not tracked by git**, and is the
only place your observing site is stored. It is created on first save and
defaults to latitude/longitude 0,0 until you set it (the app can also
auto-detect, and will refuse to overwrite a site you entered by hand).

```ini
[Rig]
SiteLatDeg=0.0
SiteLonDeg=0.0
SiteElevationM=0.0
PlateScaleArcsecPx=0.203
GuideRateArcsecPerSec=7.5
; The filter physically fitted to the camera. Not detectable --
; the dispersion model computes across this band, so a wrong
; value moves the ADC figure by up to 4x.
FilterName=UV/IR cut
FilterLowNm=400
FilterHighNm=700
```

Full key reference, panel-by-panel documentation and a first-night
walkthrough are in [RadukCapture-Help.md](RadukCapture-Help.md), which the
app also loads as reference context for the AI assistant.

## Documentation

- **[Wiki](https://github.com/Maligzar/Raduk-Capture/wiki)** — deep
  reference: architecture, threading, the capture pipeline, every subsystem,
  and why each is built the way it is. Source lives in [`wiki/`](wiki) and is
  published with `wiki/publish-wiki.sh`.
- [RadukCapture-Help.md](RadukCapture-Help.md) — operator reference and
  first-night walkthrough, also the AI assistant's app knowledge.
- [RadukCapture-Architecture.md](RadukCapture-Architecture.md) —
  architecture and build plan, with the phase roadmap.
- [docs/](docs) — code reviews, security review, tracking findings, phase
  plans, and the clear-night checklist.

## Status and roadmap

Built and in use: capture, tracking, GoTo, autofocus, the ADC advisor, the
quick-look stacker, the readiness check and the device emulators.

Also built: night vision across the whole interface (not just the preview),
a display stretch with black and white points, single-channel (R/G/B) view,
a four-shape reticle (crosshair, circle, collimation rings, framing box) that
can be moved, sized and rotated, and a framing shot that boosts gain and
exposure for five seconds then restores them.

Not built yet, in rough priority order: framing
helpers (click-to-recentre, push-to), focus aids beyond autofocus (FWHM,
Bahtinov), AVI/FITS output, and a polar-alignment assistant. Calibration
frames exist as a preview aid only — the recording stays raw. Full
stacking is deliberately out of scope — AutoStakkert does
that better. See **What is not built yet** in
[RadukCapture-Help.md](RadukCapture-Help.md) and the phase table in
[RadukCapture-Architecture.md](RadukCapture-Architecture.md).

## License

PolyForm Noncommercial 1.0.0 — see [LICENSE](LICENSE). The ZWO SDKs under `external/` remain the
property of Suzhou ZWO Co., Ltd. under their own terms.
