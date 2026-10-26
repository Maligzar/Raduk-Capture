# Device Layer

Three devices, three abstractions, and one compile-time switch that
replaces all of them with emulators. The rest of the codebase never knows
which it is talking to.

---

## Compile-time device selection

[`src/core/DeviceFactory.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/DeviceFactory.h)

Include `DeviceFactory.h`, never an individual device header.

```cpp
#ifdef RADUK_EMULATOR
    #include "CameraEmulator.h"
    #include "EAFEmulator.h"
    #include "MountEmulator.h"
    #pragma message("*** RADUK_EMULATOR: Building in emulator mode ***")
#else
    #include "ASICamera.h"
    #include "EAFFocuser.h"
    #include "NexStarMount.h"
    #include "ASCOMMount.h"
    #include "IMount.h"
#endif
```

The emulator headers declare classes with the **same names** —
`ASICamera`, `EAFFocuser` — so call sites are identical. No virtual
dispatch for the camera or focuser, no runtime branch, no possibility of a
mixed build.

| Device | Hardware class | Emulator class | Polymorphic? |
|--------|---------------|----------------|--------------|
| Camera | `ASICamera` (ZWO ASI SDK) | `CameraEmulator`, declared as `ASICamera` | No — same name, swapped header |
| Focuser | `EAFFocuser` (ZWO EAF SDK) | `EAFEmulator`, declared as `EAFFocuser` | No — same name, swapped header |
| Mount | `NexStarMount`, `ASCOMMount` | `MountEmulator` | **Yes** — all implement `IMount` |

> **Why the mount is the only polymorphic one.** There are two real
> backends (serial and ASCOM), so a runtime choice genuinely exists. The
> camera and focuser have exactly one SDK each, so the only variation is
> hardware-versus-emulator, which is a build-time fact.

`CMakeLists.txt` reinforces this: in emulator mode `DEVICE_SOURCES` is
empty, so `ASICamera.cpp`, `EAFFocuser.cpp`, and `NexStarMount.cpp` are not
even compiled, and the ZWO import libraries are not linked.

**No mixing is permitted.** The startup dialog in the hardware build offers
emulator mode as an all-or-nothing fallback when a device is missing, so a
test result is never ambiguous about what it was testing.

---

## Camera — `ASICamera`

[`src/core/ASICamera.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/ASICamera.h) ·
[`.cpp`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/ASICamera.cpp)

```cpp
static std::vector<ASI_CAMERA_INFO> Enumerate();
bool Open(int cameraIndex);
void Close();

bool SetROI(int width, int height, int binning, ASI_IMG_TYPE imgType);
bool SetStartPos(int startX, int startY);
bool GetROI(int& width, int& height, int& binning, ASI_IMG_TYPE& imgType) const;

bool SetControl(ASI_CONTROL_TYPE type, long value, bool autoMode = false);
bool GetControlCaps(ASI_CONTROL_TYPE type, ASI_CONTROL_CAPS& caps) const;
void SetGain(int), SetExposure(double ms), SetGamma(int);
void SetWhiteBalance(int red, int blue);
void SetUSBBandwidth(int percent);
void SetHighSpeedMode(bool), SetFlip(ASI_FLIP_STATUS);

bool StartVideoCapture();
void StopVideoCapture();
bool RestartCapture();
bool GetVideoFrame(uint8_t* buffer, int bufferSize, int timeoutMs);
bool GetVideoFrameEx(...);               // extended error reporting
int  GetDroppedFrames() const;

int  MaxWidth() const, MaxHeight() const;
static int FrameSize(int w, int h, ASI_IMG_TYPE);
static const char* ErrorString(ASI_ERROR_CODE);
```

**`GetROI()` is as important as `SetROI()`.** Every caller reads the
granted size back rather than assuming the request was honoured. This is
what lets the emulator clamp a request to 1280×960 and have the whole
application follow along correctly, and it is what makes a real camera
refusing a mode a handled case instead of a corruption.

`RestartCapture()` backs the Phase 58 self-healing path: a wedged pipeline
is detected and restarted, with `CaptureStats::restarts` counting how often
and `cameraFault` set when recovery fails.

### Camera-specific facts worth knowing

Collected in
[`src/profiles/TargetProfile.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/profiles/TargetProfile.h):

- **HCG mode auto-activates at gain ≥ 182.** Read noise drops from ~3.5 e⁻
  to 0.8 e⁻. Always use gain ≥ 182 for planets — this is why
  `AutoExposureAssistant` enforces an HCG floor rather than minimising
  gain.
- Unity gain is ~100, but noisy; avoided for planetary work.
- Full well 11,270 e⁻ — keep the histogram 50–70%.
- No amp glow at any gain or exposure.
- WB neutral is R=52, B=47 (sensor default).

### Hardware rails

Every profile value is clamped to these:

| Control | Range |
|---------|-------|
| Gain | 0 – 570 |
| Exposure | 0.032 ms – 2,000,000 ms |
| Gamma | 1 – 100 |
| WB Red / WB Blue | 0 – 100 |
| USB bandwidth | 40 – 100 |

---

## Focuser — `EAFFocuser`

[`src/core/EAFFocuser.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/EAFFocuser.h)

```cpp
static int GetCount();
bool  Open(int index);
void  Close();
bool  MoveTo(int position);
bool  MoveRelative(int steps);
bool  Stop();
bool  IsMoving() const;
int   GetPosition() const;
int   GetMaxPosition() const;      // from EAF_INFO::MaxStep
void  SetReverse(bool);
void  SetBacklash(int steps);
float GetTemperature() const;
```

Absolute positioning over a 0–600,000 step range. The real rig focuses near
**168,187** steps. [Autofocus](Autofocus) drives this; the `+`/`-` and
`[`/`]` keys use the active profile's fine and coarse step sizes.

Temperature matters because focus drifts with it — which is why the focuser
position alone is a meaningless number and only the position *at best
focus* carries information.

---

## Mount — `IMount`

[`src/core/IMount.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/IMount.h)

The rest of the codebase programs against `IMount*` only.

```cpp
// Connection — progID is a driver ProgID for ASCOM, a COM port for NexStar
virtual bool Connect(const std::string& progID, int baudRate = 9600) = 0;
virtual void Disconnect() = 0;
virtual bool IsConnected() const = 0;

// Position
virtual bool GetRADec(double& raHours, double& decDegrees) = 0;
virtual bool GetAltAz(double& altDegrees, double& azDegrees) = 0;

// GoTo
virtual bool GoToRADec(double raHours, double decDegrees) = 0;
virtual bool IsSlewing() = 0;
virtual void CancelGoTo() = 0;

// Tracking
enum TrackingMode { OFF = 0, ALT_AZ = 1, EQ_NORTH = 2, EQ_SOUTH = 3 };
virtual bool         SetTrackingMode(TrackingMode) = 0;
virtual TrackingMode GetTrackingMode() = 0;
virtual bool         TrackingModeKnown() const { return true; }

// Jog, sync, alignment
virtual bool SlewFixed(int axis, int direction, int rate) = 0;
virtual void StopSlew(int axis) = 0;
virtual bool SyncRADec(double raHours, double decDegrees) = 0;
virtual bool IsAligned() = 0;

// Guiding
virtual bool PulseGuide(GuideDirection dir, int durationMs) = 0;
virtual bool IsPulseGuiding() = 0;

// Safety
virtual void Heartbeat() {}

// Info + static formatters
virtual bool GetVersion(int& major, int& minor) = 0;
virtual const std::string& ConnectionId() const = 0;
static std::string FormatRA(double), FormatDec(double);
static const char* TrackingModeName(TrackingMode);
```

### Two virtuals with non-obvious defaults

**`TrackingModeKnown()`** returns `true` by default.

> Only the serial NexStar path can fail to obtain an answer, and it used to
> report that failure as `OFF` — **which is a different claim entirely**.
> `OFF` means the mount is not driving; unknown means we could not ask. One
> of those is a reason to stop and fix something, the other is a display
> caveat. The other backends always know their own state, hence the `true`
> default.

**`Heartbeat()`** is a no-op by default.

> `NexStarMount` feeds its runaway watchdog; the ASCOM and emulator
> backends ignore it. **It is virtual specifically so callers never need to
> downcast** — a `static_cast` to `NexStarMount` here previously corrupted
> `ASCOMMount` instances on the CPWI path. The same bug and the same fix
> apply to `TrackingModeKnown()`.

That pair is worth noting as a pattern: twice, an "I know this is really a
`NexStarMount`" downcast silently corrupted the other backend. Both are now
virtuals with safe defaults.

### Backend status

| Backend | Status |
|---------|--------|
| `NexStarMount` | **Primary.** Direct serial, auto-detected port |
| `MountEmulator` | Emulator builds. Closed-loop tracking simulation |
| `ASCOMMount` | **Tried first on a hardware build**, with NexStar serial as the fallback. CPWI heads the preferred ProgID list. Discovery refuses ASIFocuser (not a telescope), ASIMount and the hubs (this rig: COM3, misconfiguration), and the simulators (so an unattended start never points at a pretend mount). `AscomProgId` in `rig.ini` names a driver directly and bypasses all of that |

CPWI is not used. NexStar direct is faster, has no COM or registry
dependencies, and supports every required command. A CPWI handoff path
exists (CPWI releases COM3, RadukCapture takes it, a `RELEASE` gives it
back) but is not normally used.

See [Mount Control](Mount-Control) for the serial protocol, the watchdog,
and the safety machinery.

---

## Serial port discovery

[`src/core/SerialPortScan.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/SerialPortScan.h)

Replaces a hardcoded `"COM3"` that used to be baked into both the startup
hardware check and the mount connect path.

> **Why.** A USB-serial adapter's COM number is assigned by Windows and
> moves when the cable lands on a different USB port, or when another
> device — an ESP32, a GPS dongle — claims the low number first. The port
> must be discovered, not assumed.

Two steps:

1. **Enumerate ports that actually exist**, from
   `HKLM\HARDWARE\DEVICEMAP\SERIALCOMM`. This key lists only *present*
   ports, so an unplugged device is never probed.
2. **Probe each with the NexStar echo command** — send `Kx`, expect `x#`.
   Only a real hand control answers, so a GPS dongle or a dev board on a
   neighbouring COM number is not mistaken for a mount.

A port that exists but cannot be opened (`ERROR_ACCESS_DENIED` /
`ERROR_SHARING_VIOLATION`) is reported as **`busy`**, which is the
signature of CPWI already owning the mount. The caller treats that as
"mount present, drive it over ASCOM instead" rather than "no mount".

`rig.ini`'s `MountPort` pins a port explicitly. A **stale** pinned value
falls back to auto-detect rather than failing the connect.

---

## USB arbitration

[`src/core/USBTrafficManager.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/USBTrafficManager.h) ·
[`.cpp`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/USBTrafficManager.cpp)

When recording, the camera owns the bus. Focuser and mount polling are
throttled to reduce contention and prevent the USB packet collisions that
cause dropped frames.

| | Normal | Capturing |
|---|---|---|
| Focuser poll | 500 ms | **10,000 ms** |
| Mount poll | 500 ms | **2,000 ms** |
| Camera | Always allowed | Always allowed — it owns the bus |

Usage from the render loop:

```cpp
usb.Tick();                                      // each iteration
if (usb.ShouldPollFocuser()) focuser.GetPosition();
if (usb.ShouldPollMount())   mount->GetRADec(ra, dec);
usb.SetCapturing(true /*false*/);                // on record start/stop
```

The practical consequence: **during a recording the focuser readout is up
to 10 seconds stale**, and the mount position up to 2 seconds. That is a
deliberate trade — a stale number on screen costs nothing, a dropped frame
is gone forever. It is also why the [motion monitor](Mount-Control#motion-monitor)
prefers the camera centroid as its source when one is available, and labels
which source it is using.

---

**Next:** [Mount Control](Mount-Control) · [Emulator and Testing](Emulator-and-Testing) · [Capture Pipeline](Capture-Pipeline)
