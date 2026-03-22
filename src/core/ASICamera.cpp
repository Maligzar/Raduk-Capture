// ============================================================
// ASICamera.cpp — ZWO ASI SDK Wrapper for RadukCapture
// Section 3.4 of Architecture Doc
// ============================================================

#include "ASICamera.h"
#include <cstdio>
#include <algorithm>

namespace raduk::core {

// ── Move semantics ─────────────────────────────────────────────

ASICamera::~ASICamera() {
    Close();
}

ASICamera::ASICamera(ASICamera&& other) noexcept
    : m_cameraId(other.m_cameraId)
    , m_info(other.m_info)
    , m_isOpen(other.m_isOpen)
    , m_isCapturing(other.m_isCapturing)
    , m_controls(std::move(other.m_controls))
{
    other.m_cameraId = -1;
    other.m_isOpen = false;
    other.m_isCapturing = false;
}

ASICamera& ASICamera::operator=(ASICamera&& other) noexcept {
    if (this != &other) {
        Close();
        m_cameraId = other.m_cameraId;
        m_info = other.m_info;
        m_isOpen = other.m_isOpen;
        m_isCapturing = other.m_isCapturing;
        m_controls = std::move(other.m_controls);
        other.m_cameraId = -1;
        other.m_isOpen = false;
        other.m_isCapturing = false;
    }
    return *this;
}

// ── Lifecycle ──────────────────────────────────────────────────

std::vector<ASI_CAMERA_INFO> ASICamera::Enumerate() {
    // SDK requirement: must call this before any other ASI call
    int count = ASIGetNumOfConnectedCameras();
    std::vector<ASI_CAMERA_INFO> cameras;
    cameras.reserve(count);

    for (int i = 0; i < count; i++) {
        ASI_CAMERA_INFO info{};
        if (ASIGetCameraProperty(&info, i) == ASI_SUCCESS) {
            cameras.push_back(info);
        }
    }
    return cameras;
}

bool ASICamera::Open(int cameraIndex) {
    // Don't double-open
    if (m_isOpen) {
        printf("[ASICamera] Already open (id=%d), close first\n", m_cameraId);
        return false;
    }

    // Get camera info for this index
    ASI_ERROR_CODE err = ASIGetCameraProperty(&m_info, cameraIndex);
    if (err != ASI_SUCCESS) {
        printf("[ASICamera] GetCameraProperty(%d) failed: %s\n",
               cameraIndex, ErrorString(err));
        return false;
    }

    m_cameraId = m_info.CameraID;

    // SDK sequence: Open must come before Init
    err = ASIOpenCamera(m_cameraId);
    if (err != ASI_SUCCESS) {
        printf("[ASICamera] OpenCamera(%d) failed: %s\n",
               m_cameraId, ErrorString(err));
        m_cameraId = -1;
        return false;
    }

    // Init must be called immediately after Open
    err = ASIInitCamera(m_cameraId);
    if (err != ASI_SUCCESS) {
        printf("[ASICamera] InitCamera(%d) failed: %s\n",
               m_cameraId, ErrorString(err));
        ASICloseCamera(m_cameraId);
        m_cameraId = -1;
        return false;
    }

    // Enumerate all controls (gain, exposure, gamma, WB, etc.)
    int numControls = 0;
    err = ASIGetNumOfControls(m_cameraId, &numControls);
    if (err != ASI_SUCCESS) {
        printf("[ASICamera] GetNumOfControls failed: %s\n", ErrorString(err));
        ASICloseCamera(m_cameraId);
        m_cameraId = -1;
        return false;
    }

    m_controls.clear();
    m_controls.reserve(numControls);
    for (int i = 0; i < numControls; i++) {
        ASI_CONTROL_CAPS caps{};
        if (ASIGetControlCaps(m_cameraId, i, &caps) == ASI_SUCCESS) {
            m_controls.push_back(caps);
        }
    }

    m_isOpen = true;
    printf("[ASICamera] Opened: %s (id=%d, %dx%d, %d controls)\n",
           m_info.Name, m_cameraId,
           m_info.MaxWidth, m_info.MaxHeight,
           (int)m_controls.size());

    return true;
}

void ASICamera::Close() {
    if (!m_isOpen) return;

    if (m_isCapturing) {
        StopVideoCapture();
    }

    ASICloseCamera(m_cameraId);
    printf("[ASICamera] Closed: %s (id=%d)\n", m_info.Name, m_cameraId);

    m_cameraId = -1;
    m_info = {};
    m_isOpen = false;
    m_controls.clear();
}

// ── Configuration ──────────────────────────────────────────────

bool ASICamera::SetROI(int width, int height, int binning, ASI_IMG_TYPE imgType) {
    if (!m_isOpen) return false;

    // SDK constraints: width must be multiple of 8, height multiple of 2
    if (width % 8 != 0) {
        printf("[ASICamera] Warning: width %d not multiple of 8, rounding down\n", width);
        width = (width / 8) * 8;
    }
    if (height % 2 != 0) {
        printf("[ASICamera] Warning: height %d not multiple of 2, rounding down\n", height);
        height = (height / 2) * 2;
    }

    ASI_ERROR_CODE err = ASISetROIFormat(m_cameraId, width, height, binning, imgType);
    if (err != ASI_SUCCESS) {
        printf("[ASICamera] SetROIFormat(%dx%d bin%d) failed: %s\n",
               width, height, binning, ErrorString(err));
        return false;
    }

    printf("[ASICamera] ROI set: %dx%d bin%d type=%d\n",
           width, height, binning, (int)imgType);
    return true;
}

bool ASICamera::GetROI(int& width, int& height, int& binning, ASI_IMG_TYPE& imgType) const {
    if (!m_isOpen) return false;

    ASI_ERROR_CODE err = ASIGetROIFormat(m_cameraId, &width, &height, &binning, &imgType);
    return (err == ASI_SUCCESS);
}

bool ASICamera::SetStartPos(int startX, int startY) {
    if (!m_isOpen) return false;

    // SDK constraints: startX multiple of 8, startY multiple of 2
    startX = (startX / 8) * 8;
    startY = (startY / 2) * 2;

    // Clamp to sensor bounds
    if (startX < 0) startX = 0;
    if (startY < 0) startY = 0;

    ASI_ERROR_CODE err = ASISetStartPos(m_cameraId, startX, startY);
    if (err != ASI_SUCCESS) {
        printf("[ASICamera] SetStartPos(%d, %d) failed: %s\n",
               startX, startY, ErrorString(err));
        return false;
    }

    printf("[ASICamera] Start pos: (%d, %d)\n", startX, startY);
    return true;
}

bool ASICamera::GetStartPos(int& startX, int& startY) const {
    if (!m_isOpen) return false;
    ASI_ERROR_CODE err = ASIGetStartPos(m_cameraId, &startX, &startY);
    return (err == ASI_SUCCESS);
}

bool ASICamera::SetControl(ASI_CONTROL_TYPE type, long value, bool autoMode) {
    if (!m_isOpen) return false;

    ASI_ERROR_CODE err = ASISetControlValue(m_cameraId, type, value,
                                             autoMode ? ASI_TRUE : ASI_FALSE);
    if (err != ASI_SUCCESS) {
        printf("[ASICamera] SetControl(%d, %ld) failed: %s\n",
               (int)type, value, ErrorString(err));
        return false;
    }
    return true;
}

long ASICamera::GetControl(ASI_CONTROL_TYPE type) const {
    if (!m_isOpen) return 0;

    long value = 0;
    ASI_BOOL isAuto = ASI_FALSE;
    ASI_ERROR_CODE err = ASIGetControlValue(m_cameraId, type, &value, &isAuto);
    if (err != ASI_SUCCESS) {
        printf("[ASICamera] GetControl(%d) failed: %s\n",
               (int)type, ErrorString(err));
        return 0;
    }
    return value;
}

bool ASICamera::GetControlCaps(ASI_CONTROL_TYPE type, ASI_CONTROL_CAPS& caps) const {
    const ASI_CONTROL_CAPS* found = FindControl(type);
    if (!found) return false;
    caps = *found;
    return true;
}

// ── Convenience Setters ────────────────────────────────────────

void ASICamera::SetGain(int gain) {
    SetControl(ASI_GAIN, gain);
}

void ASICamera::SetExposure(double milliseconds) {
    // SDK expects exposure in microseconds
    long microSeconds = static_cast<long>(milliseconds * 1000.0);
    SetControl(ASI_EXPOSURE, microSeconds);
}

void ASICamera::SetGamma(int gamma) {
    SetControl(ASI_GAMMA, gamma);
}

void ASICamera::SetWhiteBalance(int red, int blue) {
    SetControl(ASI_WB_R, red);
    SetControl(ASI_WB_B, blue);
}

void ASICamera::SetUSBBandwidth(int percent) {
    SetControl(ASI_BANDWIDTHOVERLOAD, percent);
}

void ASICamera::SetHighSpeedMode(bool enabled) {
    SetControl(ASI_HIGH_SPEED_MODE, enabled ? 1 : 0);
}

void ASICamera::SetFlip(ASI_FLIP_STATUS flip) {
    SetControl(ASI_FLIP, static_cast<long>(flip));
}

// ── Video Capture ──────────────────────────────────────────────

bool ASICamera::StartVideoCapture() {
    if (!m_isOpen || m_isCapturing) return false;

    ASI_ERROR_CODE err = ASIStartVideoCapture(m_cameraId);
    if (err != ASI_SUCCESS) {
        printf("[ASICamera] StartVideoCapture failed: %s\n", ErrorString(err));
        return false;
    }

    m_isCapturing = true;
    printf("[ASICamera] Video capture started\n");
    return true;
}

void ASICamera::StopVideoCapture() {
    if (!m_isOpen || !m_isCapturing) return;

    ASIStopVideoCapture(m_cameraId);
    m_isCapturing = false;
    printf("[ASICamera] Video capture stopped\n");
}

bool ASICamera::GetVideoFrame(uint8_t* buffer, int bufferSize, int timeoutMs) {
    if (!m_isOpen || !m_isCapturing) return false;

    ASI_ERROR_CODE err = ASIGetVideoData(m_cameraId, buffer, bufferSize, timeoutMs);
    return (err == ASI_SUCCESS);
}

int ASICamera::GetDroppedFrames() const {
    if (!m_isOpen) return 0;

    int dropped = 0;
    ASIGetDroppedFrames(m_cameraId, &dropped);
    return dropped;
}

// ── Info ───────────────────────────────────────────────────────

float ASICamera::Temperature() const {
    if (!m_isOpen) return 0.0f;

    // SDK returns temperature × 10 as a long
    long rawTemp = GetControl(ASI_TEMPERATURE);
    return static_cast<float>(rawTemp) / 10.0f;
}

int ASICamera::FrameSize(int width, int height, ASI_IMG_TYPE imgType) {
    switch (imgType) {
        case ASI_IMG_RAW8:  return width * height;
        case ASI_IMG_RAW16: return width * height * 2;
        case ASI_IMG_RGB24: return width * height * 3;
        case ASI_IMG_Y8:    return width * height;
        default:            return width * height * 2; // safe fallback
    }
}

// ── Logging Helper ─────────────────────────────────────────────

const char* ASICamera::ErrorString(ASI_ERROR_CODE err) {
    switch (err) {
        case ASI_SUCCESS:                    return "SUCCESS";
        case ASI_ERROR_INVALID_INDEX:        return "INVALID_INDEX";
        case ASI_ERROR_INVALID_ID:           return "INVALID_ID";
        case ASI_ERROR_INVALID_CONTROL_TYPE: return "INVALID_CONTROL_TYPE";
        case ASI_ERROR_CAMERA_CLOSED:        return "CAMERA_CLOSED";
        case ASI_ERROR_CAMERA_REMOVED:       return "CAMERA_REMOVED";
        case ASI_ERROR_INVALID_PATH:         return "INVALID_PATH";
        case ASI_ERROR_INVALID_FILEFORMAT:   return "INVALID_FILEFORMAT";
        case ASI_ERROR_INVALID_SIZE:         return "INVALID_SIZE";
        case ASI_ERROR_INVALID_IMGTYPE:      return "INVALID_IMGTYPE";
        case ASI_ERROR_OUTOF_BOUNDARY:       return "OUTOF_BOUNDARY";
        case ASI_ERROR_TIMEOUT:              return "TIMEOUT";
        case ASI_ERROR_INVALID_SEQUENCE:     return "INVALID_SEQUENCE";
        case ASI_ERROR_BUFFER_TOO_SMALL:     return "BUFFER_TOO_SMALL";
        case ASI_ERROR_VIDEO_MODE_ACTIVE:    return "VIDEO_MODE_ACTIVE";
        case ASI_ERROR_EXPOSURE_IN_PROGRESS: return "EXPOSURE_IN_PROGRESS";
        case ASI_ERROR_GENERAL_ERROR:        return "GENERAL_ERROR";
        case ASI_ERROR_INVALID_MODE:         return "INVALID_MODE";
        default:                             return "UNKNOWN_ERROR";
    }
}

// ── Private ────────────────────────────────────────────────────

const ASI_CONTROL_CAPS* ASICamera::FindControl(ASI_CONTROL_TYPE type) const {
    for (const auto& caps : m_controls) {
        if (caps.ControlType == type) {
            return &caps;
        }
    }
    return nullptr;
}

} // namespace raduk::core
