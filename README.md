# RadukCapture

High-speed planetary capture suite for Windows, built for the Celestron
NexStar 8SE + ZWO ASI678MC (and adaptable to similar rigs). Pure Win32 +
Direct2D — no frameworks.

## Highlights

- **SER v3 recording** with capture-time trailer timestamps (WinJUPOS-ready),
  RAM-first write buffering, lucky-imaging quality scores per frame
- **Closed-loop planet tracking** — three-stage centroid acquisition with
  PHD2-style measured guide calibration (camera rotation, plate scale, and
  guide rate are measured, never assumed)
- **GoTo + sky map + plate solving** — Messier/planet catalog, D2D polar sky
  map, ASTAP plate-solve → mount sync
- **EAF V-curve autofocus**, auto-exposure assistant, atmospheric-dispersion
  (ADC) advisor, capture quality gating
- **Full device emulators** — camera, mount, and focuser simulate the real
  SDK semantics (including a closed tracking loop and fault injection), so
  the entire app runs with no hardware attached
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

Set your observing site in `rig.ini` next to the executable (created on
first save; defaults to latitude/longitude 0,0 until you do):

```ini
[Rig]
SiteLatDeg=0.0
SiteLonDeg=0.0
PlateScaleArcsecPx=0.203
GuideRateArcsecPerSec=7.5
```

## License

MIT — see [LICENSE](LICENSE). The ZWO SDKs under `external/` remain the
property of Suzhou ZWO Co., Ltd. under their own terms.
