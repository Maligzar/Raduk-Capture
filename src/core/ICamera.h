#pragma once
// ============================================================
// ICamera.h - Abstract camera interface for RadukCapture
//
// The ZWO wrapper and the emulator both implement this.
//
// WHY THIS IS NOT TYPED IN ASI TERMS
//
// The obvious version of this interface puts ASI_CONTROL_TYPE,
// ASI_IMG_TYPE and ASI_CAMERA_INFO straight onto the virtual
// methods. It would have been an hour's work instead of a day's,
// and it would have been theatre: an interface only ZWO can
// implement is not an interface, it is the ZWO SDK with extra
// steps. The whole point of P5.2 is that a second brand becomes
// possible, and a QHY wrapper cannot be asked to invent ASI
// structs to satisfy a signature.
//
// So the vocabulary here is this application's own. It is small
// because the application's real use of the SDK is small: ten
// controls, three image types, four error outcomes. That was
// measured, not guessed, by counting every ASI_ constant in src/.
//
// Translation to and from the vendor enums lives in ASITypeMap.h,
// at the boundary where it belongs. A second brand adds its own
// map and nothing above the interface changes.
//
// WHAT IS DELIBERATELY NOT HERE
//
//   Enumerate(), FrameSize() and ErrorString() stay static on the
//   concrete classes. They answer questions that precede an
//   instance, and C++ has no virtual statics. A real multi-brand
//   build needs a free factory that asks each backend in turn;
//   that is not this change.
//
//   The concrete classes keep their native ASI-typed methods.
//   Existing call sites still use them, and 43 calls to
//   GetControl(ASI_GAIN) do not need to move in the same commit
//   that introduces the seam. Migrating them is the follow-up,
//   and until it happens this interface is implemented but not
//   yet depended upon.
//
//   The emulator's injection knobs are not promoted. A caller
//   that wants SetSeeingJitterArcsec knows it has an emulator.
// ============================================================

#include <cstdint>
#include <string>

namespace raduk::core {

// The controls this application actually sets or reads. Not the
// SDK's full list: everything here is used somewhere in src/.
enum class ControlId {
    Gain,
    ExposureUs,         // the SDK's unit, microseconds, kept to avoid a lossy hop
    Gamma,
    WhiteBalanceRed,
    WhiteBalanceBlue,
    UsbBandwidth,
    HighSpeedMode,
    Temperature,        // read-only in practice
    Flip,
    TargetTemperature,
};

enum class ImageType {
    Raw8,
    Raw16,
    Rgb24,
    Y8,
};

enum class BayerPattern {
    None,
    RG,
    BG,
    GR,
    GB,
};

// Why a frame did not arrive. The capture loop distinguishes
// these: a timeout is normal at long exposures, a removal is the
// self-healing path, and anything else is a fault to report.
enum class CaptureError {
    Success,
    Timeout,
    CameraRemoved,
    CameraClosed,
    Other,
};

struct CameraInfo {
    std::string  name;
    int          maxWidth    = 0;
    int          maxHeight   = 0;
    bool         isColor     = false;
    BayerPattern bayer       = BayerPattern::None;
    double       pixelSizeUm = 0.0;
    int          bitDepth    = 0;
};

// What a control will accept. Used to clamp the UI rather than
// discover limits by being refused.
struct ControlRange {
    long min         = 0;
    long max         = 0;
    long defaultVal  = 0;
    bool writable    = false;
    bool autoCapable = false;
};

class ICamera {
public:
    virtual ~ICamera() = default;

    // ── Connection ─────────────────────────────────────────────
    virtual bool Open(int cameraIndex) = 0;
    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;

    // ── Geometry ───────────────────────────────────────────────
    virtual bool SetRegion(int width, int height, int binning, ImageType type) = 0;
    virtual bool GetRegion(int& width, int& height, int& binning, ImageType& type) const = 0;
    virtual bool SetStartPos(int startX, int startY) = 0;
    virtual bool GetStartPos(int& startX, int& startY) const = 0;

    // ── Controls ───────────────────────────────────────────────
    virtual bool SetControlValue(ControlId id, long value, bool autoMode = false) = 0;
    virtual long GetControlValue(ControlId id) const = 0;
    virtual bool GetControlRange(ControlId id, ControlRange& range) const = 0;

    // Convenience setters the application uses constantly. They
    // are on the interface rather than left to SetControlValue
    // because a backend may need to do more than write one
    // register, and because the units are worth naming once.
    virtual void SetGain(int gain) = 0;
    virtual void SetExposure(double milliseconds) = 0;

    // ── Streaming ──────────────────────────────────────────────
    virtual bool StartVideoCapture() = 0;
    virtual void StopVideoCapture() = 0;
    virtual bool RestartCapture() = 0;
    virtual bool IsCapturing() const = 0;

    // The hot path. err distinguishes the outcomes above so the
    // caller can tell a normal timeout from an unplugged camera.
    //
    // On the cost of making this virtual: one indirect call per
    // frame, a couple of nanoseconds, against a frame budget of
    // 5,000,000 ns at 200 fps. It is not measurable. The copy of
    // the frame buffer that happens either side of it is what
    // costs.
    virtual bool GetFrame(uint8_t* buffer, int bufferSize, int timeoutMs,
                          CaptureError& err) = 0;

    // ── Readout ────────────────────────────────────────────────
    virtual CameraInfo Describe() const = 0;
    virtual int        GetDroppedFrames() const = 0;
    virtual float      GetTemperatureC() const = 0;
    virtual int        CameraId() const = 0;
};

} // namespace raduk::core
