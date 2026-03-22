#pragma once
// ============================================================
// CameraEmulator.h — Software emulator for ZWO ASI678MC
// Implements the same public interface as ASICamera.
// Generates synthetic RAW8 frames with a moving planet disk.
// Enabled when RADUK_EMULATOR is defined in CMakeLists.txt.
// ============================================================

#include "ASICamera2.h"
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

namespace raduk::core {

class ASICamera {
public:
    ASICamera() = default;
    ~ASICamera() { Close(); }

    ASICamera(const ASICamera&) = delete;
    ASICamera& operator=(const ASICamera&) = delete;

    // ── Lifecycle ──────────────────────────────────────────────

    static std::vector<ASI_CAMERA_INFO> Enumerate() {
        // Always report one emulated ASI678MC
        ASI_CAMERA_INFO info{};
        strncpy_s(info.Name, "ZWO ASI678MC [EMU]", sizeof(info.Name));
        info.CameraID  = 0;
        info.MaxWidth  = 3840;
        info.MaxHeight = 2160;
        info.IsColor   = ASI_TRUE;
        info.BayerPattern = ASI_BAYER_RG;
        info.PixelSize = 2.0;
        printf("[CameraEmulator] Enumerate: 1 emulated camera\n");
        return { info };
    }

    bool Open(int /*cameraIndex*/) {
        if (m_isOpen) return false;
        strncpy_s(m_info.Name, "ZWO ASI678MC [EMU]", sizeof(m_info.Name));
        m_info.CameraID  = 0;
        m_info.MaxWidth  = 3840;
        m_info.MaxHeight = 2160;
        m_info.IsColor   = ASI_TRUE;
        m_info.BayerPattern = ASI_BAYER_RG;
        m_isOpen = true;
        // Default ROI
        m_roiW = 1280; m_roiH = 960;
        m_binning = 1; m_imgType = ASI_IMG_RAW8;
        printf("[CameraEmulator] Opened: %s\n", m_info.Name);
        return true;
    }

    void Close() {
        if (!m_isOpen) return;
        StopVideoCapture();
        m_isOpen = false;
        printf("[CameraEmulator] Closed\n");
    }

    bool IsOpen()      const { return m_isOpen; }
    bool IsCapturing() const { return m_isCapturing; }

    // ── Configuration ──────────────────────────────────────────

    bool SetROI(int width, int height, int binning, ASI_IMG_TYPE imgType) {
        if (!m_isOpen) return false;
        m_roiW = width; m_roiH = height;
        m_binning = binning; m_imgType = imgType;
        printf("[CameraEmulator] ROI set: %dx%d bin%d type=%d\n",
               width, height, binning, (int)imgType);
        return true;
    }

    bool GetROI(int& width, int& height, int& binning, ASI_IMG_TYPE& imgType) const {
        width = m_roiW; height = m_roiH;
        binning = m_binning; imgType = m_imgType;
        return true;
    }

    bool SetControl(ASI_CONTROL_TYPE type, long value, bool /*autoMode*/ = false) {
        switch (type) {
            case ASI_GAIN:             m_gain = (int)value; break;
            case ASI_EXPOSURE:         m_exposureUs = value; break;
            case ASI_GAMMA:            m_gamma = (int)value; break;
            case ASI_WB_R:             m_wbR = (int)value; break;
            case ASI_WB_B:             m_wbB = (int)value; break;
            case ASI_BANDWIDTHOVERLOAD: break;  // no-op
            case ASI_HIGH_SPEED_MODE:  break;
            default: break;
        }
        return true;
    }

    long GetControl(ASI_CONTROL_TYPE type) const {
        switch (type) {
            case ASI_GAIN:        return m_gain;
            case ASI_EXPOSURE:    return m_exposureUs;
            case ASI_GAMMA:       return m_gamma;
            case ASI_WB_R:        return m_wbR;
            case ASI_WB_B:        return m_wbB;
            case ASI_TEMPERATURE: return (long)(m_sensorTemp * 10.0f);
            default:              return 0;
        }
    }

    bool GetControlCaps(ASI_CONTROL_TYPE /*type*/, ASI_CONTROL_CAPS& /*caps*/) const {
        return false;
    }

    const std::vector<ASI_CONTROL_CAPS>& AllControls() const { return m_controls; }

    // ── Convenience setters ────────────────────────────────────

    void SetGain(int gain)               { m_gain = gain; }
    void SetExposure(double ms)          { m_exposureUs = (long)(ms * 1000.0); }
    void SetGamma(int gamma)             { m_gamma = gamma; }
    void SetWhiteBalance(int r, int b)   { m_wbR = r; m_wbB = b; }
    void SetUSBBandwidth(int /*pct*/)    {}
    void SetHighSpeedMode(bool /*en*/)   {}
    void SetFlip(ASI_FLIP_STATUS /*f*/)  {}

    // ── Video capture ──────────────────────────────────────────

    bool StartVideoCapture() {
        if (!m_isOpen || m_isCapturing) return false;
        m_isCapturing = true;
        m_frameCounter = 0;
        m_genThread = std::thread(&ASICamera::GenerateFrames, this);
        printf("[CameraEmulator] Video capture started (%dx%d)\n", m_roiW, m_roiH);
        return true;
    }

    void StopVideoCapture() {
        if (!m_isCapturing) return;
        m_isCapturing = false;
        if (m_genThread.joinable()) m_genThread.join();
        printf("[CameraEmulator] Video capture stopped — %llu frames generated\n",
               m_frameCounter.load());
    }

    // Blocks until a frame is ready or timeout expires.
    // Fills buffer with a synthetic RAW8 Bayer frame.
    bool GetVideoFrame(uint8_t* buffer, int bufferSize, int timeoutMs) {
        if (!m_isCapturing) return false;

        // Wait for frame ready signal
        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            std::unique_lock<std::mutex> lk(m_frameMutex);
            if (m_frameReady) {
                int copyBytes = std::min(bufferSize, (int)m_frameBuffer.size());
                memcpy(buffer, m_frameBuffer.data(), copyBytes);
                m_frameReady = false;
                return true;
            }
            lk.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false; // timeout
    }

    int GetDroppedFrames() const { return 0; }

    // ── Info ───────────────────────────────────────────────────

    const ASI_CAMERA_INFO& Info() const { return m_info; }
    int   CameraId()    const { return 0; }
    int   MaxWidth()    const { return 3840; }
    int   MaxHeight()   const { return 2160; }
    float Temperature() const { return m_sensorTemp; }
    std::string Name()  const { return std::string(m_info.Name); }

    static int FrameSize(int width, int height, ASI_IMG_TYPE imgType) {
        switch (imgType) {
            case ASI_IMG_RAW8:  return width * height;
            case ASI_IMG_RAW16: return width * height * 2;
            case ASI_IMG_RGB24: return width * height * 3;
            default:            return width * height;
        }
    }

    static const char* ErrorString(ASI_ERROR_CODE err) {
        (void)err; return "EMU_OK";
    }

private:
    // ── Synthetic frame generator ──────────────────────────────
    // Runs on a background thread at the exposure-limited FPS.
    // Generates RGGB Bayer RAW8 frames with:
    //   - Dark background gradient
    //   - Bright planet disk that drifts slowly across the sensor
    //   - Brightness modulated by gain and exposure

    void GenerateFrames() {
        double planetX = m_roiW / 2.0;
        double planetY = m_roiH / 2.0;
        double driftX  = 0.08;  // pixels/frame — simulate sidereal drift
        double driftY  = 0.03;
        const double planetR = m_roiW * 0.06; // ~6% of frame width

        while (m_isCapturing) {
            // Frame interval based on exposure (minimum 8ms for realism)
            double expMs = m_exposureUs / 1000.0;
            int intervalMs = std::max(8, (int)expMs);

            // Build frame
            int frameBytes = m_roiW * m_roiH; // RAW8
            {
                std::lock_guard<std::mutex> lk(m_frameMutex);
                m_frameBuffer.resize(frameBytes);

                // Brightness scale: gain (0-570) and exposure (us)
                // Normalized so gain=100, exp=8ms gives ~mid-range brightness
                float brightness = (m_gain / 100.0f) * (m_exposureUs / 8000.0f);
                brightness = std::min(brightness, 4.0f);

                for (int y = 0; y < m_roiH; y++) {
                    for (int x = 0; x < m_roiW; x++) {
                        double dx = x - planetX;
                        double dy = y - planetY;
                        double dist = sqrt(dx*dx + dy*dy);

                        uint8_t val;
                        if (dist < planetR) {
                            // Planet disk: bright with subtle limb darkening
                            double limb = 1.0 - 0.3 * (dist / planetR);
                            val = (uint8_t)std::min(255.0,
                                      220.0 * limb * brightness);
                        } else {
                            // Sky background: dark gradient + noise
                            uint8_t bg = (uint8_t)(8.0f * brightness);
                            // Faint star field: 1 in 2000 pixels
                            if ((x * 31 + y * 17 + m_frameCounter.load() * 7) % 2000 == 0) {
                                val = (uint8_t)std::min(255.0, 180.0 * brightness);
                            } else {
                                val = bg;
                            }
                        }

                        // Write as RGGB Bayer pattern
                        // Even row, even col = R; even row, odd col = G
                        // Odd row, even col = G; odd row, odd col = B
                        bool evenRow = (y % 2 == 0);
                        bool evenCol = (x % 2 == 0);
                        if (evenRow && evenCol) {
                            // R pixel — slight red tint
                            m_frameBuffer[y * m_roiW + x] = std::min(255, (int)(val * 1.05f));
                        } else if (!evenRow && !evenCol) {
                            // B pixel — slight blue tint
                            m_frameBuffer[y * m_roiW + x] = std::min(255, (int)(val * 0.92f));
                        } else {
                            // G pixels
                            m_frameBuffer[y * m_roiW + x] = val;
                        }
                    }
                }

                m_frameReady = true;
            }

            m_frameCounter++;

            // Drift planet position — bounce off edges
            planetX += driftX;
            planetY += driftY;
            if (planetX < planetR || planetX > m_roiW - planetR) driftX = -driftX;
            if (planetY < planetR || planetY > m_roiH - planetR) driftY = -driftY;

            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        }
    }

    ASI_CAMERA_INFO             m_info{};
    bool                        m_isOpen      = false;
    bool                        m_isCapturing = false;
    int                         m_roiW = 1280, m_roiH = 960;
    int                         m_binning = 1;
    ASI_IMG_TYPE                m_imgType = ASI_IMG_RAW8;
    int                         m_gain = 100;
    long                        m_exposureUs = 8000;
    int                         m_gamma = 50;
    int                         m_wbR = 52, m_wbB = 47;
    float                       m_sensorTemp = 22.4f;
    std::vector<ASI_CONTROL_CAPS> m_controls;

    // Frame generation
    std::thread                 m_genThread;
    std::mutex                  m_frameMutex;
    std::vector<uint8_t>        m_frameBuffer;
    bool                        m_frameReady = false;
    std::atomic<uint64_t>       m_frameCounter{0};
};

} // namespace raduk::core
