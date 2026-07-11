#pragma once
// ============================================================
// CameraEmulator.h — Software emulator for ZWO ASI678MC
// Implements the same public interface as ASICamera.
// Generates synthetic RAW8 frames with a moving planet disk.
// Enabled when RADUK_EMULATOR is defined in CMakeLists.txt.
// ============================================================

#ifdef RADUK_EMULATOR
#  include "ASICamera2Stub.h"
#else
#  include "ASICamera2.h"
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX  // keep windows.h min/max macros away from std::min/max
#endif
#include <windows.h>
#include <cstdlib>
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
#include <functional>

namespace raduk::core {

// ── Jupiter emulator static data (namespace scope for MSVC constexpr) ──────

struct JupBelt { float lat, hw, r, g, b; };
struct JupMoon { float dist, period_d, radius_px_base, r, g, b; };

static const JupBelt JUP_BELTS[] = {
    {  60.0f,  8.0f, 0.55f, 0.40f, 0.30f }, // North Polar Region
    {  38.0f,  5.0f, 0.72f, 0.58f, 0.42f }, // North Temperate Belt
    {  24.0f,  6.0f, 0.68f, 0.50f, 0.35f }, // North Equatorial Belt
    {   9.0f,  5.5f, 0.90f, 0.82f, 0.62f }, // Equatorial Zone (bright cream)
    {  -5.0f,  5.0f, 0.64f, 0.48f, 0.33f }, // South Equatorial Belt
    { -18.0f,  4.0f, 0.84f, 0.76f, 0.58f }, // South Tropical Zone
    { -28.0f,  3.5f, 0.70f, 0.56f, 0.40f }, // South Temperate Belt
    { -38.0f,  3.0f, 0.78f, 0.65f, 0.50f }, // South-South Temperate Belt
    { -55.0f,  8.0f, 0.56f, 0.42f, 0.32f }, // South Polar Region
    {   0.0f,  2.0f, 0.86f, 0.80f, 0.60f }, // Equatorial Band (thin bright)
};
static const int NUM_JUP_BELTS = 10;

static const JupMoon JUP_MOONS[] = {
    {  5.9f,  1.769f, 4.5f, 0.92f, 0.80f, 0.40f }, // Io — yellow-orange
    {  9.4f,  3.551f, 4.0f, 0.90f, 0.90f, 0.88f }, // Europa — white-grey
    { 15.0f,  7.155f, 5.5f, 0.60f, 0.55f, 0.45f }, // Ganymede — brown-grey
    { 26.4f, 16.689f, 4.8f, 0.68f, 0.62f, 0.50f }, // Callisto — dark brown
};
static const int NUM_JUP_MOONS = 4;

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
        // Emulator renders at 1280x960 regardless of requested ROI
        // — prevents 8MP render at 20fps which causes frame drops
        m_roiW = 1280; m_roiH = 960;
        m_binning = 1; m_imgType = ASI_IMG_RAW8;
        printf("[CameraEmulator] Opened: %s (fixed 1280x960 render)\n", m_info.Name);
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

    bool SetROI(int /*width*/, int /*height*/, int binning, ASI_IMG_TYPE imgType) {
        if (!m_isOpen) return false;
        // Emulator always renders 1280x960 — ignore requested dimensions
        // m_roiW / m_roiH stay at 1280x960 set in Open()
        m_binning = binning; m_imgType = imgType;
        printf("[CameraEmulator] SetROI: locked to 1280x960 (emulator)\n");
        return true;
    }

    bool GetROI(int& width, int& height, int& binning, ASI_IMG_TYPE& imgType) const {
        width = m_roiW; height = m_roiH;  // already capped
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
    void SetFlip(ASI_FLIP_STATUS /*f*/)         {}
    bool SetStartPos(int /*x*/, int /*y*/)      { return true; } // no-op in emulator

    // ── Phase 52: closed-loop pointing input ───────────────────
    // Plate scale of the synthetic frames (arcsec/pixel). Deliberately NOT
    // the value CentroidTracker assumes for the real rig — a realistic
    // mismatch the Phase 51 calibration must measure, not assume.
    static constexpr float EMU_PLATE_SCALE_AS_PX = 0.61f;

    // Provider returns the mount's pointing error (target − pointing) in
    // arcsec; the planet renders displaced from frame centre by that error.
    // All three must be set BEFORE StartVideoCapture() — the generator
    // thread reads them unguarded.
    void SetPointingErrorProvider(std::function<bool(float&, float&)> f) {
        m_errProvider = std::move(f);
    }
    // Camera orientation in the focuser: rotates the on-sky error vector
    // into sensor axes. Non-zero values prove the tracker's axis transform
    // is measured (Phase 51), not assumed.
    void SetCameraRotationDeg(float deg)  { m_camRotDeg = deg; }
    // Seeing-like centroid wander amplitude, arcsec.
    void SetSeeingJitterArcsec(float amp) { m_jitterAs = amp; }

    // ── Video capture ──────────────────────────────────────────

    bool StartVideoCapture() {
        if (!m_isOpen || m_isCapturing) return false;
        m_isCapturing = true;
        m_frameCounter = 0;

        // Pre-render one frame synchronously so GetVideoFrame succeeds immediately
        // — prevents NO SIGNAL flash on startup before generator thread runs
        {
            const int W = m_roiW, H = m_roiH;
            std::vector<uint8_t> firstFrame(W * H, 0);
            const float CX=W*0.5f,CY=H*0.5f,JR=H*0.22f,JRy=JR*(1.f-0.0649f);
            const float PI=3.14159265f, iR=1.f/JR, iRy=1.f/JRy;
            float brightness = (m_gain/100.0f)*(m_exposureUs/8000.0f);
            if(brightness<0.1f)brightness=0.1f; if(brightness>3.5f)brightness=3.5f;
            for(int py=0;py<H;py++){
                float dy=(py-CY)*iRy, dy2=dy*dy;
                for(int px=0;px<W;px++){
                    float dx=(px-CX)*iR, r2=dx*dx+dy2;
                    uint8_t rv=0,gv=0,bv=0;
                    if(r2<=1.f){
                        // Simple band pattern for first frame — no full trig
                        float normY = (py-CY)/(float)H;
                        float band = fabsf(fmodf(normY*6.f, 1.f)-0.5f);
                        uint8_t lum=(uint8_t)((0.6f+0.4f*band)*brightness*220.f>255?255:(0.6f+0.4f*band)*brightness*220.f);
                        rv=(uint8_t)(lum*1.05f>255?255:lum*1.05f);
                        gv=(uint8_t)(lum*0.90f);
                        bv=(uint8_t)(lum*0.70f);
                    } else {
                        rv=gv=bv=(uint8_t)(4.f*brightness);
                    }
                    bool eR=(py%2==0),eC=(px%2==0);
                    firstFrame[py*W+px]=(eR&&eC)?rv:(!eR&&!eC)?bv:gv;
                }
            }
            std::lock_guard<std::mutex> lk(m_frameMutex);
            m_frameBuffer = std::move(firstFrame);
            m_frameReady  = true;
        }

        m_genThread = std::thread(&ASICamera::GenerateFrames, this);
        m_captureStart = std::chrono::steady_clock::now();
        {
            char envBuf[32] = {};
            m_removeAfterS = 0.0f;
            if (GetEnvironmentVariableA("RADUK_EMU_REMOVE_AFTER_S", envBuf, 32))
                m_removeAfterS = (float)atof(envBuf);
            if (m_removeAfterS > 0)
                printf("[CameraEmulator] USB-drop injection armed at t+%.0fs\n",
                       m_removeAfterS);
        }
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

    // Phase 58: error-code variant, matching ASICamera. Supports USB-drop
    // injection for testing the recovery path: set RADUK_EMU_REMOVE_AFTER_S=N
    // and the "camera" disappears N seconds into capture (CAMERA_REMOVED,
    // immediate return like the SDK) for 10 seconds, then replugs.
    bool GetVideoFrameEx(uint8_t* buffer, int bufferSize, int timeoutMs,
                         ASI_ERROR_CODE& err) {
        if (InRemovedWindow()) { err = ASI_ERROR_CAMERA_REMOVED; return false; }
        bool ok = GetVideoFrame(buffer, bufferSize, timeoutMs);
        err = ok ? ASI_SUCCESS : ASI_ERROR_TIMEOUT;
        return ok;
    }

    bool RestartCapture() {
        // Emulated §6.3 recovery: succeeds unless the camera is currently
        // "unplugged" by the injection window.
        return m_isCapturing && !InRemovedWindow();
    }

    bool GetVideoFrame(uint8_t* buffer, int bufferSize, int timeoutMs) {
        if (!m_isCapturing) return false;

        // Phase 52 — real SDK semantics: ASIGetVideoData blocks until a NEW
        // frame arrives and delivers each frame exactly once. The previous
        // emulator returned the same frame immediately and forever, so FPS,
        // backpressure, and timeout behavior were validated against a regime
        // that cannot occur live.
        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds(timeoutMs);
        do {
            {
                std::lock_guard<std::mutex> lk(m_frameMutex);
                if (m_frameReady && !m_frameBuffer.empty()) {
                    const int n = (int)m_frameBuffer.size(); // W*H rendered bytes
                    if (m_imgType == ASI_IMG_RAW16) {
                        // Honest RAW16: left-aligned 16-bit, full frame —
                        // reject undersized buffers like the SDK does.
                        if (bufferSize < n * 2) return false;
                        uint16_t* out = reinterpret_cast<uint16_t*>(buffer);
                        for (int i = 0; i < n; i++)
                            out[i] = (uint16_t)(m_frameBuffer[i] << 8);
                    } else {
                        if (bufferSize < n) return false;
                        memcpy(buffer, m_frameBuffer.data(), n);
                    }
                    m_frameReady = false; // consumed — next call waits for a new frame
                    return true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);

        return false; // timeout — no new frame within timeoutMs
    }

    int GetDroppedFrames() const { return 0; }

    // ── Info ───────────────────────────────────────────────────

    const ASI_CAMERA_INFO& Info() const { return m_info; }
    int   CameraId()    const { return 0; }
    int   MaxWidth()    const { return 3840; }
    int   MaxHeight()   const { return 2160; }
    float Temperature() const { return m_sensorTemp; }
    std::string Name()  const { return std::string(m_info.Name); }

    static int FrameSize(int /*width*/, int /*height*/, ASI_IMG_TYPE imgType) {
        // Emulator always produces 1280x960 frames
        constexpr int W = 1280, H = 960;
        switch (imgType) {
            case ASI_IMG_RAW8:  return W * H;
            case ASI_IMG_RAW16: return W * H * 2;
            case ASI_IMG_RGB24: return W * H * 3;
            default:            return W * H;
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
    //   - Bright planet disk positioned by the mount's pointing error
    //     (Phase 52 closed loop; centred when no provider is wired)
    //   - Brightness modulated by gain and exposure

    // ── Jupiter Renderer ───────────────────────────────────────
    struct RGB { float r, g, b; };
    struct Belt { float lat, hw, r, g, b; };
    struct Moon { float dist, period_d, radius_px_base, r, g, b; };

    static constexpr float ZONE_R=0.93f,ZONE_G=0.86f,ZONE_B=0.68f;
    static constexpr float POLE_R=0.50f,POLE_G=0.38f,POLE_B=0.30f;
    static constexpr float GRS_LAT=-23.0f,GRS_LON=180.0f;
    static constexpr float GRS_RLAT=7.0f, GRS_RLON=12.0f;

    static RGB JupiterSurfaceColor(float lat_deg, float lon_phase, float timeS) {
        RGB color = {ZONE_R, ZONE_G, ZONE_B};
        float maxCov = 0.0f;
        for (int i = 0; i < NUM_JUP_BELTS; i++) {
            const JupBelt& belt = JUP_BELTS[i];
            float dist = fabsf(lat_deg - belt.lat);
            float turb = 0.6f * sinf(lon_phase*6.2832f*8.0f + belt.lat*0.3f + timeS*0.1f);
            float hw = belt.hw + turb;
            if (hw < 1.0f) hw = 1.0f;
            float cov = 1.0f - dist / hw;
            if (cov > 0.0f && cov > maxCov) {
                maxCov = cov;
                float blend = cov*cov*(3.0f-2.0f*cov);
                color.r = ZONE_R*(1.0f-blend) + belt.r*blend;
                color.g = ZONE_G*(1.0f-blend) + belt.g*blend;
                color.b = ZONE_B*(1.0f-blend) + belt.b*blend;
            }
        }
        float absLat = fabsf(lat_deg);
        if (absLat > 55.0f) {
            float t = (absLat-55.0f)/30.0f; if(t>1.0f)t=1.0f; t=t*t;
            color.r = color.r*(1.0f-t)+POLE_R*t;
            color.g = color.g*(1.0f-t)+POLE_G*t;
            color.b = color.b*(1.0f-t)+POLE_B*t;
        }
        // Great Red Spot
        float dLat = lat_deg - GRS_LAT;
        float grsLon = GRS_LON + timeS*0.5f;
        float dLon = lon_phase*360.0f - grsLon;
        while(dLon> 180.0f) dLon-=360.0f;
        while(dLon<-180.0f) dLon+=360.0f;
        float grsE=(dLat/GRS_RLAT)*(dLat/GRS_RLAT)+(dLon/GRS_RLON)*(dLon/GRS_RLON);
        if (grsE < 1.0f) {
            float gb=(1.0f-grsE); gb=gb*gb*(3.0f-2.0f*gb);
            float core=sqrtf(grsE);
            float gr=(core<0.5f)?0.92f:0.80f;
            float gg=(core<0.5f)?0.55f:0.36f;
            float gbl=(core<0.5f)?0.38f:0.22f;
            color.r=color.r*(1.0f-gb)+gr*gb;
            color.g=color.g*(1.0f-gb)+gg*gb;
            color.b=color.b*(1.0f-gb)+gbl*gb;
        }
        return color;
    }

    void GenerateFrames() {
        const int   W  = m_roiW, H = m_roiH;
        const float CX = W*0.5f, CY = H*0.5f;
        const float JR = H*0.22f, JRy = JR*(1.0f-0.0649f);
        const float ROT = 9.9f*3600.0f, PI = 3.14159265f;
        const float iR = 1.0f/JR, iRy = 1.0f/JRy;
        static const int LUT_N = 512;
        struct LUTEntry { uint8_t r, g, b; };
        std::vector<LUTEntry> latLUT(LUT_N);
        struct MoonPos { float x, y, r; uint8_t r8, g8, b8; };
        MoonPos moonPos[NUM_JUP_MOONS];
        std::vector<uint8_t> backBuf(W*H, 0);
        const int TARGET_MS = 50;
        auto startTime = std::chrono::steady_clock::now();

        while (m_isCapturing) {
            auto frameStart = std::chrono::steady_clock::now();
            float timeS = std::chrono::duration<float>(frameStart - startTime).count();
            float brightness = (m_gain/100.0f)*(m_exposureUs/8000.0f);
            if (brightness < 0.1f) brightness = 0.1f;
            if (brightness > 3.5f) brightness = 3.5f;
            float rotPhase = fmodf(timeS/ROT, 1.0f);
            float midLon = fmodf(0.5f + rotPhase, 1.0f);

            // Phase 52: planet centre = frame centre + pointing error.
            // +RA error maps to +x at zero camera rotation; the rotation
            // models the camera's unknown orientation in the focuser.
            float offX = 0.f, offY = 0.f;
            if (m_errProvider) {
                float eRA = 0.f, eDec = 0.f;
                if (m_errProvider(eRA, eDec)) {
                    const float th   = m_camRotDeg * PI / 180.f;
                    const float pxRA  = eRA  / EMU_PLATE_SCALE_AS_PX;
                    const float pxDec = eDec / EMU_PLATE_SCALE_AS_PX;
                    offX = cosf(th)*pxRA - sinf(th)*pxDec;
                    offY = sinf(th)*pxRA + cosf(th)*pxDec;
                }
            }
            if (m_jitterAs > 0.f) {
                // Two incommensurate sines per axis approximate seeing wander
                const float j = m_jitterAs / EMU_PLATE_SCALE_AS_PX;
                offX += j * (0.6f*sinf(timeS*9.7f)  + 0.4f*sinf(timeS*23.3f));
                offY += j * (0.6f*sinf(timeS*11.3f) + 0.4f*sinf(timeS*19.1f));
            }
            const float PCX = CX + offX, PCY = CY + offY;

            // Build latitude LUT — 512 entries, no per-pixel trig on disk
            for (int i = 0; i < LUT_N; i++) {
                float sinLat = (i/(float)(LUT_N-1))*2.0f - 1.0f;
                float lat_deg = (180.0f/PI)*asinf(sinLat);
                RGB col = JupiterSurfaceColor(lat_deg, midLon, timeS);
                latLUT[i] = {(uint8_t)(col.r*255.f>255?255:col.r*255.f),
                             (uint8_t)(col.g*255.f>255?255:col.g*255.f),
                             (uint8_t)(col.b*255.f>255?255:col.b*255.f)};
            }

            // Precompute moon positions once per frame
            for (int m = 0; m < NUM_JUP_MOONS; m++) {
                const JupMoon& moon = JUP_MOONS[m];
                float angle = (2.f*PI*timeS/(moon.period_d*86400.f)) + m*1.1f;
                moonPos[m].x  = PCX + JR*moon.dist*cosf(angle);
                moonPos[m].y  = PCY + JRy*0.15f*sinf(angle);
                moonPos[m].r  = moon.radius_px_base;
                float b = brightness>1.f?1.f:brightness;
                moonPos[m].r8 = (uint8_t)(moon.r*b*255.f);
                moonPos[m].g8 = (uint8_t)(moon.g*b*255.f);
                moonPos[m].b8 = (uint8_t)(moon.b*b*255.f);
            }
            const uint8_t bgVal = (uint8_t)(4.f*brightness);

            // Main loop — LUT lookup + sqrtf only, no asinf/sinf/fmod per pixel
            for (int py = 0; py < H; py++) {
                float dy  = (py-PCY)*iRy, dy2 = dy*dy;
                for (int px = 0; px < W; px++) {
                    float dx = (px-PCX)*iR;
                    float r2 = dx*dx + dy2;
                    uint8_t rv, gv, bv;
                    if (r2 <= 1.0f) {
                        float dxsq = dx*dx, denom = 1.0f-dxsq;
                        float sinLat = (denom>0.001f) ? dy/sqrtf(denom) : 0.f;
                        if (sinLat<-1.f) sinLat=-1.f;
                        if (sinLat> 1.f) sinLat= 1.f;
                        int li = (int)((sinLat+1.0f)*0.5f*(LUT_N-1)+0.5f);
                        if (li<0) li=0; if (li>=LUT_N) li=LUT_N-1;
                        const LUTEntry& e = latLUT[li];
                        float ld = 0.30f + 0.70f*sqrtf(1.0f-r2);
                        float br = ld*brightness;
                        rv = (uint8_t)(e.r*br>255?255:e.r*br);
                        gv = (uint8_t)(e.g*br>255?255:e.g*br);
                        bv = (uint8_t)(e.b*br>255?255:e.b*br);
                    } else {
                        uint32_t h = (uint32_t)(px*7919+py*6271);
                        h^=h>>16; h*=0x45d9f3b; h^=h>>16;
                        uint8_t sv = ((h%3000)==0) ?
                            (uint8_t)(((h%200)/200.f)*brightness*255.f) : bgVal;
                        rv=gv=bv=sv;
                    }
                    for (int m=0; m<NUM_JUP_MOONS; m++) {
                        float mdx=px-moonPos[m].x, mdy=py-moonPos[m].y, mr=moonPos[m].r;
                        if (mdx*mdx+mdy*mdy < mr*mr) { rv=moonPos[m].r8; gv=moonPos[m].g8; bv=moonPos[m].b8; }
                    }
                    bool eR=(py%2==0), eC=(px%2==0);
                    backBuf[py*W+px] = (eR&&eC)?rv:(!eR&&!eC)?bv:gv;
                }
            }
            {
                std::lock_guard<std::mutex> lk(m_frameMutex);
                m_frameBuffer.swap(backBuf);
                if (backBuf.size()!=(size_t)(W*H)) backBuf.resize(W*H);
                m_frameReady = true;
            }
            m_frameCounter++;
            int renderMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now()-frameStart).count();
            if (renderMs < TARGET_MS)
                std::this_thread::sleep_for(std::chrono::milliseconds(TARGET_MS-renderMs));
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

    // Phase 52 closed-loop inputs — set before StartVideoCapture()
    std::function<bool(float&, float&)> m_errProvider;
    float                       m_camRotDeg = 0.0f;
    float                       m_jitterAs  = 0.0f;

    // Phase 58: USB-drop injection window (RADUK_EMU_REMOVE_AFTER_S)
    std::chrono::steady_clock::time_point m_captureStart{};
    float                       m_removeAfterS = 0.0f;

    bool InRemovedWindow() const {
        if (m_removeAfterS <= 0.0f || !m_isCapturing) return false;
        float t = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - m_captureStart).count();
        return t >= m_removeAfterS && t < m_removeAfterS + 10.0f;
    }
};

} // namespace raduk::core
