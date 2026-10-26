// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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

namespace raduk::core {

class ASICamera {
public:
    ASICamera() = default;
    ~ASICamera();

    ASICamera(const ASICamera&) = delete;
    ASICamera& operator=(const ASICamera&) = delete;
    ASICamera(ASICamera&& other) noexcept;
    ASICamera& operator=(ASICamera&& other) noexcept;

    static std::vector<ASI_CAMERA_INFO> Enumerate();
    bool Open(int cameraIndex);
    void Close();
    bool IsOpen() const { return m_isOpen; }

    bool SetROI(int width, int height, int binning, ASI_IMG_TYPE imgType);
    bool SetStartPos(int startX, int startY);
    bool GetStartPos(int& startX, int& startY) const;
    bool GetROI(int& width, int& height, int& binning, ASI_IMG_TYPE& imgType) const;
    bool SetControl(ASI_CONTROL_TYPE type, long value, bool autoMode = false);
    long GetControl(ASI_CONTROL_TYPE type) const;
    bool GetControlCaps(ASI_CONTROL_TYPE type, ASI_CONTROL_CAPS& caps) const;
    const std::vector<ASI_CONTROL_CAPS>& AllControls() const { return m_controls; }

    void SetGain(int gain);
    void SetExposure(double milliseconds);
    void SetGamma(int gamma);
    void SetWhiteBalance(int red, int blue);
    void SetUSBBandwidth(int percent);
    void SetHighSpeedMode(bool enabled);
    void SetFlip(ASI_FLIP_STATUS flip);

    bool StartVideoCapture();
    void StopVideoCapture();
    
    // Recovery: Restarts the video stream to clear SDK/USB wedges
    bool RestartCapture();
    
    bool IsCapturing() const { return m_isCapturing; }
    bool GetVideoFrame(uint8_t* buffer, int bufferSize, int timeoutMs);
    // Phase 58 (review C4): error-code-preserving variant — the capture loop
    // must distinguish TIMEOUT (wait/retry) from CAMERA_REMOVED (returns
    // immediately; retrying flat-out busy-spins a TIME_CRITICAL thread).
    bool GetVideoFrameEx(uint8_t* buffer, int bufferSize, int timeoutMs,
                         ASI_ERROR_CODE& err);
    int GetDroppedFrames() const;

    const ASI_CAMERA_INFO& Info() const { return m_info; }
    int CameraId() const { return m_cameraId; }
    int MaxWidth() const { return m_info.MaxWidth; }
    int MaxHeight() const { return m_info.MaxHeight; }
    float Temperature() const;
    static int FrameSize(int width, int height, ASI_IMG_TYPE imgType);
    std::string Name() const { return std::string(m_info.Name); }
    static const char* ErrorString(ASI_ERROR_CODE err);

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
