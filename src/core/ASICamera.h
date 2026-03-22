#pragma once
// ============================================================
// ASICamera.h — ZWO ASI SDK Wrapper for RadukCapture
// Section 3.4 of Architecture Doc
// ============================================================

#include "ASICamera2.h"
#include <vector>
#include <string>

namespace raduk::core {

class ASICamera {
public:
    ASICamera() = default;
    ~ASICamera();

    // Non-copyable, movable
    ASICamera(const ASICamera&) = delete;
    ASICamera& operator=(const ASICamera&) = delete;
    ASICamera(ASICamera&& other) noexcept;
    ASICamera& operator=(ASICamera&& other) noexcept;

    // ── Lifecycle ──────────────────────────────────────────────

    // Enumerate all connected ASI cameras. Must be called before Open().
    // Returns a list of ASI_CAMERA_INFO structs, one per camera.
    static std::vector<ASI_CAMERA_INFO> Enumerate();

    // Open and initialize camera by index (0-based, from Enumerate).
    // Follows required SDK sequence: Open → Init → EnumerateControls.
    bool Open(int cameraIndex);

    // Close camera and release resources. Safe to call if not open.
    void Close();

    // True if camera is open and initialized.
    bool IsOpen() const { return m_isOpen; }

    // ── Configuration ──────────────────────────────────────────

    // Set ROI (Region of Interest) format.
    // Width must be multiple of 8, height must be multiple of 2.
    // Requires capture to be stopped first.
    bool SetROI(int width, int height, int binning, ASI_IMG_TYPE imgType);

    // Set the top-left start position of the ROI on the full sensor.
    // startX must be multiple of 8, startY must be multiple of 2.
    // Call AFTER SetROI, while capture is stopped.
    bool SetStartPos(int startX, int startY);

    // Get the current ROI start position.
    bool GetStartPos(int& startX, int& startY) const;

    // Get current ROI settings.
    bool GetROI(int& width, int& height, int& binning, ASI_IMG_TYPE& imgType) const;

    // Set any camera control by type. Pass autoMode=true for auto control.
    bool SetControl(ASI_CONTROL_TYPE type, long value, bool autoMode = false);

    // Read current value of a control.
    long GetControl(ASI_CONTROL_TYPE type) const;

    // Get the capabilities (min, max, default, name) for a control type.
    // Returns false if the control type is not supported by this camera.
    bool GetControlCaps(ASI_CONTROL_TYPE type, ASI_CONTROL_CAPS& caps) const;

    // Get all supported control capabilities.
    const std::vector<ASI_CONTROL_CAPS>& AllControls() const { return m_controls; }

    // ── Convenience Setters (matching APEX profiles) ───────────

    // Set gain (0–570 for ASI678MC). Unity gain = ~100 in 12-bit mode.
    void SetGain(int gain);

    // Set exposure in milliseconds (converted to microseconds for SDK).
    void SetExposure(double milliseconds);

    // Set gamma (1–100). Note: gamma is applied in camera firmware.
    void SetGamma(int gamma);

    // Set white balance red and blue channels (0–100 each).
    void SetWhiteBalance(int red, int blue);

    // Set USB bandwidth percentage (40–100).
    // Higher = more bus bandwidth claimed by camera. Start low, increase gradually.
    void SetUSBBandwidth(int percent);

    // Enable/disable high-speed mode (reduces bit depth overhead for max FPS).
    void SetHighSpeedMode(bool enabled);

    // Set image flip mode.
    void SetFlip(ASI_FLIP_STATUS flip);

    // ── Video Capture ──────────────────────────────────────────

    // Start video capture mode. ROI and controls should be set first.
    bool StartVideoCapture();

    // Stop video capture mode. Safe to call if not capturing.
    void StopVideoCapture();

    // True if currently in video capture mode.
    bool IsCapturing() const { return m_isCapturing; }

    // Retrieve one frame from the SDK video buffer.
    // Returns true on success, false on timeout or error.
    // buffer must be large enough for the current ROI + format.
    bool GetVideoFrame(uint8_t* buffer, int bufferSize, int timeoutMs);

    // Get number of frames dropped by the SDK since capture started.
    int GetDroppedFrames() const;

    // ── Info ───────────────────────────────────────────────────

    // Full camera info struct (name, sensor size, pixel size, etc.)
    const ASI_CAMERA_INFO& Info() const { return m_info; }

    // Camera ID used by the SDK.
    int CameraId() const { return m_cameraId; }

    // Max sensor dimensions.
    int MaxWidth() const { return m_info.MaxWidth; }
    int MaxHeight() const { return m_info.MaxHeight; }

    // Sensor temperature in degrees Celsius.
    // SDK returns temperature × 10 as a long, we convert to float.
    float Temperature() const;

    // Calculate frame size in bytes for given dimensions and format.
    static int FrameSize(int width, int height, ASI_IMG_TYPE imgType);

    // Human-readable camera name.
    std::string Name() const { return std::string(m_info.Name); }

    // ── Logging Helper ─────────────────────────────────────────

    // Convert ASI_ERROR_CODE to a human-readable string.
    static const char* ErrorString(ASI_ERROR_CODE err);

private:
    int                             m_cameraId = -1;
    ASI_CAMERA_INFO                 m_info{};
    bool                            m_isOpen = false;
    bool                            m_isCapturing = false;
    std::vector<ASI_CONTROL_CAPS>   m_controls;

    // Find a control in m_controls by type. Returns nullptr if not found.
    const ASI_CONTROL_CAPS* FindControl(ASI_CONTROL_TYPE type) const;
};

} // namespace raduk::core
