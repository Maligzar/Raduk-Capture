#pragma once
// ============================================================
// ASICamera.h — ZWO ASI SDK Wrapper for RadukCapture
// Section 3.4 of Architecture Doc
// ============================================================

#ifdef RADUK_EMULATOR
#  include "ASICamera2Stub.h"
#else
#  include "ASICamera2.h"
#include <vector>
#include <string>
#include "ASITypeMap.h"

namespace raduk::core {

class ASICamera : public ICamera {
public:
    ASICamera() = default;
    ~ASICamera();

    ASICamera(const ASICamera&) = delete;
    ASICamera& operator=(const ASICamera&) = delete;
    ASICamera(ASICamera&& other) noexcept;
    ASICamera& operator=(ASICamera&& other) noexcept;

    static std::vector<ASI_CAMERA_INFO> Enumerate();
    bool Open(int cameraIndex) override;
    void Close() override;
    bool IsOpen() const override { return m_isOpen; }

    bool SetROI(int width, int height, int binning, ASI_IMG_TYPE imgType);
    bool SetStartPos(int startX, int startY) override;
    bool GetStartPos(int& startX, int& startY) const override;
    bool GetROI(int& width, int& height, int& binning, ASI_IMG_TYPE& imgType) const;
    bool SetControl(ASI_CONTROL_TYPE type, long value, bool autoMode = false);
    long GetControl(ASI_CONTROL_TYPE type) const;
    bool GetControlCaps(ASI_CONTROL_TYPE type, ASI_CONTROL_CAPS& caps) const;
    const std::vector<ASI_CONTROL_CAPS>& AllControls() const { return m_controls; }

    void SetGain(int gain) override;
    void SetExposure(double milliseconds) override;
    void SetGamma(int gamma);
    void SetWhiteBalance(int red, int blue);
    void SetUSBBandwidth(int percent);
    void SetHighSpeedMode(bool enabled);
    void SetFlip(ASI_FLIP_STATUS flip);

    bool StartVideoCapture() override;
    void StopVideoCapture() override;
    
    // Recovery: Restarts the video stream to clear SDK/USB wedges
    bool RestartCapture() override;
    
    bool IsCapturing() const override { return m_isCapturing; }
    bool GetVideoFrame(uint8_t* buffer, int bufferSize, int timeoutMs);
    // Phase 58 (review C4): error-code-preserving variant — the capture loop
    // must distinguish TIMEOUT (wait/retry) from CAMERA_REMOVED (returns
    // immediately; retrying flat-out busy-spins a TIME_CRITICAL thread).
    bool GetVideoFrameEx(uint8_t* buffer, int bufferSize, int timeoutMs,
                         ASI_ERROR_CODE& err);
    int GetDroppedFrames() const override;

    const ASI_CAMERA_INFO& Info() const { return m_info; }
    int CameraId() const override { return m_cameraId; }
    int MaxWidth() const { return m_info.MaxWidth; }
    int MaxHeight() const { return m_info.MaxHeight; }
    float Temperature() const;
    static int FrameSize(int width, int height, ASI_IMG_TYPE imgType);
    std::string Name() const { return std::string(m_info.Name); }
    static const char* ErrorString(ASI_ERROR_CODE err);

    // ── ICamera ────────────────────────────────────────────────
    // Neutral vocabulary delegating to the ASI-typed methods
    // above. Additive on purpose: the 43 existing GetControl
    // call sites keep working untouched, and migrating them is
    // the follow-up rather than a second risk in this commit.
    bool SetRegion(int width, int height, int binning, ImageType type) override {
        return SetROI(width, height, binning, asimap::ToAsi(type));
    }
    bool GetRegion(int& width, int& height, int& binning, ImageType& type) const override {
        ASI_IMG_TYPE a{};
        const bool ok = GetROI(width, height, binning, a);
        type = asimap::FromAsi(a);
        return ok;
    }
    bool SetControlValue(ControlId id, long value, bool autoMode = false) override {
        return SetControl(asimap::ToAsi(id), value, autoMode);
    }
    long GetControlValue(ControlId id) const override {
        return GetControl(asimap::ToAsi(id));
    }
    bool GetControlRange(ControlId id, ControlRange& range) const override {
        ASI_CONTROL_CAPS caps{};
        if (!GetControlCaps(asimap::ToAsi(id), caps)) return false;
        range.min         = caps.MinValue;
        range.max         = caps.MaxValue;
        range.defaultVal  = caps.DefaultValue;
        range.writable    = (caps.IsWritable == ASI_TRUE);
        range.autoCapable = (caps.IsAutoSupported == ASI_TRUE);
        return true;
    }
    bool GetFrame(uint8_t* buffer, int bufferSize, int timeoutMs,
                  CaptureError& err) override {
        ASI_ERROR_CODE raw = ASI_SUCCESS;
        const bool ok = GetVideoFrameEx(buffer, bufferSize, timeoutMs, raw);
        err = asimap::FromAsi(raw);
        return ok;
    }
    CameraInfo Describe() const override { return asimap::Describe(m_info); }
    float      GetTemperatureC() const override { return Temperature(); }

private:
    int                             m_cameraId = -1;
    ASI_CAMERA_INFO                 m_info{};
    bool                            m_isOpen = false;
    bool                            m_isCapturing = false;
    std::vector<ASI_CONTROL_CAPS>   m_controls;

    const ASI_CONTROL_CAPS* FindControl(ASI_CONTROL_TYPE type) const;
};

} // namespace raduk::core
#endif
