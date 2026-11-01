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
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")   // timeBeginPeriod, for frame pacing
#include <cstdlib>
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <functional>
#include "ASITypeMap.h"
#include "LunarSurface.h"

namespace raduk::core {

// ── Jupiter emulator static data (namespace scope for MSVC constexpr) ──────

struct JupBelt { float lat, hw, r, g, b; };
// dist is in Jupiter radii; radiusArcsec is the moon's own apparent
// radius. Phase 91: both are angular now, like everything else in the
// synthetic sky -- this used to carry an absolute pixel radius, which
// was the last thing in the scene whose size did not come from the sky.
struct JupMoon { float dist, period_d, radiusArcsec, r, g, b; };

// ── What the emulator is actually pointed at (Phase 81) ──────────
//
// The generator rendered Jupiter unconditionally, whatever profile was
// loaded, so a Saturn session showed Jupiter - and Saturn is the one
// that matters most here, because its rings are the high-contrast
// detail focus and sharpness get judged on.
//
// ── Phase 91: sizes are ANGULAR, not a fraction of the frame ──
//
// Every size here used to be a fraction of frame height, which meant
// the synthetic sky had no field of view at all: the disc and its
// moons were laid out relative to whatever the sensor format happened
// to be, so widening the ROI drew the same picture larger and narrowing
// it drew the same picture smaller. The one question an emulator exists
// to answer indoors -- "will the moons fit in my frame?" -- was the one
// question it could not answer, because the answer was always "the
// frame is exactly as wide as the picture".
//
// Angular radii, in arcseconds, at a representative opposition. The
// renderer divides by the plate scale, so the number of pixels the
// planet covers now follows from the sensor format exactly as it does
// on the real rig.
// ── Phase 94.1: not everything in the sky is a disc ──────────
//
// Until now every target was an extended disc, so the emulator could
// not produce a point source at all -- and a point source is a
// different problem, not a smaller one. Blur a disc and only its limb
// moves, a one-dimensional edge on a two-dimensional object, so the
// sharpness metric changes slowly. Blur a point and all of its energy
// spreads, so the peak falls as the square of the blur. That is why
// focus is judged on a star, and it is why autofocus, FWHM, the
// sharpness graph and the centroid tracker's lock have never been
// exercised against the thing they are actually for.
//
// The background hash sprinkle does not count: single pixels one pixel
// wide, positionally meaningless, with no profile to measure.
enum class EmuKind { Disc, Star, Lunar };

struct EmuTargetDef {
    const char* name;
    float radiusArcsec;    // Disc: apparent RADIUS. Star: Airy radius.
    float oblate;          // polar flattening
    float r, g, b;         // base tint
    bool  belts;           // Jupiter-style banding and Red Spot
    bool  moons;           // Galilean moons
    float ringInner;       // in disc radii; 0 = no rings
    float ringOuter;
    EmuKind kind;          // Disc unless stated
    const char* spectral;  // Star only: the class the colour came from
};
static const EmuTargetDef EMU_TARGETS[] = {
    // name        rad"    oblate  r     g     b     belt  moon  rIn   rOut  kind            spectral
    { "Jupiter",  22.5f, 0.0649f, 0.93f,0.86f,0.68f, true,  true, 0.0f, 0.0f, EmuKind::Disc, nullptr },
    { "Saturn",    9.0f, 0.0980f, 0.94f,0.87f,0.66f, false, false,1.24f,2.33f, EmuKind::Disc, nullptr },
    { "Mars",      7.5f, 0.0059f, 0.86f,0.45f,0.28f, false, false,0.0f, 0.0f, EmuKind::Disc, nullptr },
    { "Venus",    12.5f, 0.0000f, 0.98f,0.96f,0.90f, false, false,0.0f, 0.0f, EmuKind::Disc, nullptr },
    // Half a degree. It overflows every sensor this rig owns, which is
    // what the Moon actually does -- there is no limb in frame at prime
    // focus and there never was on the sky either.
    { "Moon",    900.0f, 0.0000f, 0.80f,0.78f,0.74f, false, false,0.0f, 0.0f, EmuKind::Lunar, nullptr },

    // ── Stars ────────────────────────────────────────────────
    //
    // radiusArcsec here is the first Airy minimum, not a disc: every
    // one of these is a point source far below the resolution of any
    // telescope, so what the sensor records is the instrument's own
    // diffraction pattern and nothing about the star's size. The value
    // is filled from AiryRadiusArcsec() at render time; the number in
    // the table is only a sensible fallback if that is ever bypassed.
    //
    // Colours are from spectral class, which is the whole of "star
    // types" as far as a monochrome-ish Bayer frame can show it. These
    // six span the range anyone would point at, and the last two are
    // the ones worth having: a red M giant and a blue-white A dwarf
    // look genuinely different through a colour sensor, so a white
    // balance or a channel alignment error shows up on them and not on
    // Vega alone.
    //
    // Chosen for being bright and well spread in colour, not for being
    // a catalog. A real star field is phase 94.5 and needs a real
    // catalog; see the plan for why 50 bright stars cannot populate a
    // 13 x 7 arcmin frame.
    { "Vega",      0.68f, 0.0000f, 0.82f,0.86f,1.00f, false, false,0.0f, 0.0f, EmuKind::Star, "A0V"    },
    { "Sirius",    0.68f, 0.0000f, 0.88f,0.91f,1.00f, false, false,0.0f, 0.0f, EmuKind::Star, "A1V"    },
    { "Polaris",   0.68f, 0.0000f, 1.00f,0.98f,0.94f, false, false,0.0f, 0.0f, EmuKind::Star, "F7Ib"   },
    { "Capella",   0.68f, 0.0000f, 1.00f,0.95f,0.78f, false, false,0.0f, 0.0f, EmuKind::Star, "G3III"  },
    { "Arcturus",  0.68f, 0.0000f, 1.00f,0.82f,0.58f, false, false,0.0f, 0.0f, EmuKind::Star, "K1.5III"},
    { "Betelgeuse",0.68f, 0.0000f, 1.00f,0.68f,0.45f, false, false,0.0f, 0.0f, EmuKind::Star, "M1-2Ia" },
};
static const int NUM_EMU_TARGETS =
    (int)(sizeof(EMU_TARGETS)/sizeof(EMU_TARGETS[0]));

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
    //  dist   period    rad"   r      g      b
    {  5.9f,  1.769f, 0.82f, 0.92f, 0.80f, 0.40f }, // Io — yellow-orange
    {  9.4f,  3.551f, 0.51f, 0.90f, 0.90f, 0.88f }, // Europa — white-grey
    { 15.0f,  7.155f, 0.86f, 0.60f, 0.55f, 0.45f }, // Ganymede — brown-grey
    { 26.4f, 16.689f, 0.79f, 0.68f, 0.62f, 0.50f }, // Callisto — dark brown
};
static const int NUM_JUP_MOONS = 4;

class ASICamera : public ICamera {
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
        info.IsColorCam = ASI_TRUE;
        info.BayerPattern = ASI_BAYER_RG;
        info.PixelSize = 2.0;
        printf("[CameraEmulator] Enumerate: 1 emulated camera\n");
        return { info };
    }

    bool Open(int /*cameraIndex*/) override {
        if (m_isOpen) return false;
        strncpy_s(m_info.Name, "ZWO ASI678MC [EMU]", sizeof(m_info.Name));
        m_info.CameraID  = 0;
        m_info.MaxWidth  = 3840;
        m_info.MaxHeight = 2160;
        m_info.IsColorCam = ASI_TRUE;
        m_info.BayerPattern = ASI_BAYER_RG;
        m_info.PixelSize = 2.0;
        m_isOpen = true;
        // Open at the full sensor, like the real camera does. The app's
        // own startup default is 3840x2160 and was being silently
        // clamped here, so the emulator reported a 1280x960 camera and
        // that got persisted as the last session format.
        m_roiW = EMU_MAX_ROI_W; m_roiH = EMU_MAX_ROI_H;
        m_binning = 1; m_imgType = ASI_IMG_RAW8;
        printf("[CameraEmulator] Opened: %s (%dx%d, ROI changeable up to %dx%d)\n",
               m_info.Name, m_roiW, m_roiH, EMU_MAX_ROI_W, EMU_MAX_ROI_H);
        return true;
    }

    void Close() override {
        if (!m_isOpen) return;
        StopVideoCapture();
        m_isOpen = false;
        printf("[CameraEmulator] Closed\n");
    }

    bool IsOpen()      const override { return m_isOpen; }
    bool IsCapturing() const override { return m_isCapturing; }

    // ── Configuration ──────────────────────────────────────────

    // The widest mode the emulator will stream.
    //
    // Phase 91: this was 1280x960, and the limit was real -- the disc
    // was sized as a fraction of the frame, so a 4K frame meant a 4K
    // planet and every one of its 8.3 million pixels took the expensive
    // shaded path. Now that the disc is sized by the sky it covers
    // about 200 px however wide the sensor is, the moons walk their own
    // bounding boxes instead of being tested against every pixel, and
    // the rest of the frame is background. So the cap can be what the
    // sensor actually is.
    //
    // It matters beyond frame rate: at 1280x960 the emulated field was
    // 4.3 arcmin and Jupiter's moons fell outside it, which made the
    // one thing this emulator is for -- checking a field of view
    // indoors -- impossible. The full sensor is 14 arcmin and holds
    // Io, Europa and Ganymede, with Callisto in and out as it swings.
    static constexpr int EMU_MAX_ROI_W = 3840;
    static constexpr int EMU_MAX_ROI_H = 2160;

    bool SetROI(int width, int height, int binning, ASI_IMG_TYPE imgType) {
        if (!m_isOpen) return false;
        // This used to ignore the requested size entirely and hold
        // 1280x960, which made every resolution control untestable
        // without the real camera -- and a switcher whose effect cannot
        // be observed is one that cannot be trusted. Sizes up to the cap
        // are now honoured exactly, and anything larger is clamped and
        // reported, which is what a real camera does with a mode it
        // cannot deliver. Every caller here already reads the result
        // back, so the clamp propagates correctly.
        //
        // Alignment matches the ASI SDK requirement: width %8, height %2.
        int w = std::clamp(width,  8, EMU_MAX_ROI_W);
        int h = std::clamp(height, 2, EMU_MAX_ROI_H);
        w = (w / 8) * 8;
        h = (h / 2) * 2;
        if (w < 8 || h < 2) return false;

        const bool changed = (w != m_roiW || h != m_roiH);
        m_roiW = w; m_roiH = h;
        m_binning = binning; m_imgType = imgType;
        if (changed || width > EMU_MAX_ROI_W || height > EMU_MAX_ROI_H)
            printf("[CameraEmulator] SetROI %dx%d -> %dx%d %s\n",
                   width, height, w, h,
                   imgType == ASI_IMG_RAW16 ? "RAW16" : "RAW8");
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
            // Stored rather than dropped. Not simulating their effect is
            // fine; answering 0 when asked what they are is not, because
            // callers read these back -- the Phase 88 session restore
            // persisted bandwidth 0 from a bench run and would have
            // applied it to the real camera.
            case ASI_BANDWIDTHOVERLOAD: m_usbBandwidth = (int)value; break;
            case ASI_HIGH_SPEED_MODE:   m_highSpeed = (value != 0);  break;
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
            case ASI_BANDWIDTHOVERLOAD: return m_usbBandwidth;
            case ASI_HIGH_SPEED_MODE:   return m_highSpeed ? 1 : 0;
            default:              return 0;
        }
    }

    bool GetControlCaps(ASI_CONTROL_TYPE /*type*/, ASI_CONTROL_CAPS& /*caps*/) const {
        return false;
    }

    const std::vector<ASI_CONTROL_CAPS>& AllControls() const { return m_controls; }

    // ── Convenience setters ────────────────────────────────────

    void SetGain(int gain) override               { m_gain = gain; }
    void SetExposure(double ms) override { m_exposureUs = (long)(ms * 1000.0); }
    void SetGamma(int gamma)             { m_gamma = gamma; }
    void SetWhiteBalance(int r, int b)   { m_wbR = r; m_wbB = b; }
    // These are the setters callers actually use; SetControl is the other
    // door to the same state. Both were empty here, so the emulator kept
    // no record of either and GetControl answered 0 -- which the Phase 88
    // session restore then wrote to rig.ini as the last known bandwidth.
    // Their effect still is not simulated, and does not need to be; what
    // matters is that reading back returns what was set.
    void SetUSBBandwidth(int pct)        { m_usbBandwidth = pct; }
    void SetHighSpeedMode(bool en)       { m_highSpeed = en; }
    void SetFlip(ASI_FLIP_STATUS /*f*/)         {}
    bool SetStartPos(int /*x*/, int /*y*/) override { return true; } // no-op in emulator
    // There is no sensor position to report, so say so: the phase 48
    // observation record then leaves roi_x/roi_y out rather than
    // recording a position the emulator never had.
    bool GetStartPos(int& /*x*/, int& /*y*/) const override { return false; }

    // ── Phase 52: closed-loop pointing input ───────────────────
    // Plate scale of the synthetic frames (arcsec/pixel). This is the
    // scale of the whole synthetic sky: the disc, the rings, the moons
    // and the pointing-error injection all divide by it, because they
    // are all on the same sky and a renderer with two scales is a
    // renderer that is wrong somewhere.
    //
    // Phase 91: was 0.61, chosen only as a deliberate mismatch against
    // the 0.203 CentroidTracker assumes, so the Phase 51 calibration
    // had something real to measure. That intent is kept -- this is
    // still NOT the assumed value -- but 3x was never a realistic error
    // for a rig to have about its own focal length, and at 0.61 the
    // synthetic field was three times too wide for the rig it stands
    // in for. 0.223 is 10% off: the size of error an actual C8 has
    // between its nominal 2032 mm and what it really is, which is the
    // error calibration exists to find.
    static constexpr float EMU_PLATE_SCALE_AS_PX = 0.223f;

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

    // ── Delivery rate (P4.3) ──────────────────────────────────
    //
    // Frames per second the generator aims for. 20 is the historical
    // value and stays the default, so nothing changes for anyone who
    // does not ask. Set before StartVideoCapture: the generator thread
    // reads it once, unguarded, like the other providers here.
    //
    // The real limit is still what the renderer can produce. At the
    // full sensor it cannot make a frame in 8 ms whatever this says;
    // at 640x480, the format high frame rates are actually used at, it
    // can.
    void SetTargetFps(double fps) {
        m_targetFps = (fps < 0.1) ? 0.1 : ((fps > 1000.0) ? 1000.0 : fps);
    }
    double TargetFps() const { return m_targetFps; }

    // Frames per second the generator is really managing, smoothed.
    double GeneratedFps() const {
        return m_genFps.load(std::memory_order_relaxed);
    }
    // The exposure the pacing is honouring, in ms. A camera cannot
    // deliver faster than it exposes, and this says what that floor is.
    double ExposureMsForPacing() const { return (double)m_exposureUs / 1000.0; }
    // How long one frame takes to MAKE, as opposed to how long the
    // generator then waits. If these two do not add up to the period,
    // the time is going somewhere neither of them is looking.
    double RenderMs() const {
        return m_genRenderMs.load(std::memory_order_relaxed);
    }

    // ── Defocus (Phase 73) ────────────────────────────────────
    // Until this existed the synthetic planet was equally sharp at every
    // focuser position, so an autofocus sweep fitted a parabola to noise
    // and no autofocus change could be verified on the bench at all.
    //
    // Provider returns the focuser's current step position. Like the
    // pointing-error provider above, it must be set BEFORE
    // StartVideoCapture() — the generator thread reads it unguarded.
    void SetFocusPositionProvider(std::function<bool(int&)> f) {
        m_focusProvider = std::move(f);
    }
    // The step position at which the image is perfectly sharp. Autofocus
    // is correct if it lands near this value, which makes the bench test
    // an assertion rather than an impression.
    void SetTrueFocusPosition(int steps) { m_trueFocus = steps; }
    // ── Defocus blur per focuser step ─────────────────────────
    //
    // On the real rig: the EAF gives ~2000 steps/mm of focus travel (see
    // AutofocusParams), the 8SE is f/10 so a defocus of d mm spreads a
    // point into a disk of d/10 mm at the sensor, and the ASI678MC pixel
    // is 2.0 um. So one step is 0.0005 mm of travel, 0.00005 mm of blur,
    // 0.025 real px at the real plate scale of 0.203"/px -- which is an
    // ANGULAR quantity once multiplied out: 0.005075 arcsec per step.
    //
    // Getting this wrong matters, and it has been wrong twice in
    // opposite directions. At the un-scaled 0.025 px/step the planet was
    // destroyed 600 steps off focus, so nearly every position in
    // autofocus's +/-3000 sweep sat on the floor of the metric and the
    // V-curve degenerated into a ramp; a parabola fitted to that puts
    // its vertex at one end of the scan. Then phase 91 moved
    // EMU_PLATE_SCALE_AS_PX from 0.61 to 0.223 and this number did not
    // follow, leaving the blur 2.7x too WEAK and the V-curve 2.7x too
    // shallow -- the same failure from the other side.
    //
    // So it is derived rather than restated. A constant that has to be
    // kept in step with another constant by hand will eventually not be.
    //
    // The quantity worth holding fixed is a RATIO, not a pixel count,
    // which is why the old comment's "~5 px at 600 steps, ~25 px at
    // 3000" went stale the moment the scale moved. Blur and disc are
    // both angular, so they rescale together: at 3000 steps off focus
    // the blur is 0.34 of Jupiter's disc diameter, and that is true at
    // 0.61"/px, at 0.223, and at whatever this is set to next.
    // ── Phase 94.1: the diffraction pattern this rig makes ────
    //
    // A star has no size a telescope can resolve, so the image is the
    // instrument's point-spread function. For a circular aperture that
    // is the Airy pattern, and its first dark ring sits at
    // 1.22 * lambda / D.
    //
    // For this rig: 203 mm at 550 nm gives 0.682 arcsec, which at the
    // emulator's 0.223"/px is 3.06 px. That matters, because it means
    // the rings are SAMPLED -- three pixels to the first minimum is
    // enough to draw them, so a well-focused star here should show
    // structure and not a single bright pixel.
    //
    // Derived from the aperture rather than written as 0.68, for the
    // same reason as the blur above: a number that has to be kept in
    // step with another by hand eventually is not.
    static constexpr float APERTURE_MM   = 203.0f;   // C8 / 8SE
    static constexpr float WAVELENGTH_NM = 550.0f;   // visual peak
    static constexpr float AiryRadiusArcsec() {
        // 1.22 * lambda/D in radians, times 206265 arcsec/radian.
        return 1.22f * (WAVELENGTH_NM * 1e-6f) / APERTURE_MM * 206265.0f;
    }
    static constexpr float AiryRadiusPx() {
        return AiryRadiusArcsec() / EMU_PLATE_SCALE_AS_PX;
    }

    // ── Phase 94.1: the Airy profile, as a radial table ──────
    //
    // I(r) = (2*J1(x)/x)^2 with x = 3.8317 * r / rFirstMin. Tabulated
    // once per frame for the same reason the latitude LUT is: a Bessel
    // function per pixel would put this on the slow path, and the
    // profile is radial so one dimension is all it needs.
    //
    // J1 by its power series. It is called a few hundred times to fill
    // the table and never in the pixel loop, so there is no reason to
    // reach for an approximation and carry its error budget.
    static double BesselJ1(double x) {
        const double h = x * 0.5;
        const double q = h * h;          // (x/2)^2
        double term = h;                 // k = 0
        double sum  = term;
        for (int k = 1; k < 40; ++k) {
            // term_k = term_{k-1} * -(x^2/4) / (k*(k+1))
            term *= -q / (double)(k * (k + 1));
            sum  += term;
            if (term < 1e-18 && term > -1e-18) break;
        }
        return sum;
    }

    // First zero of J1. The pattern's first dark ring.
    static constexpr double AIRY_X1 = 3.8317059702075123;

    // Six Airy radii covers the core and about four rings; past that
    // the profile is under a thousandth of the peak and the background
    // noise owns those pixels. 256 entries over that span is about
    // one entry per eighth of a pixel at this plate scale.
    static constexpr int   AIRY_N    = 256;
    static constexpr float AIRY_SPAN = 6.0f;

    // Fill lut[0..N-1] with the Airy intensity from r = 0 to
    // r = spanAiryRadii * rFirstMin, normalised to 1.0 at the core.
    static void BuildAiryLUT(float* lut, int n, double spanAiryRadii) {
        for (int i = 0; i < n; ++i) {
            const double rr = spanAiryRadii * (double)i / (double)(n - 1);
            const double x  = AIRY_X1 * rr;
            double v;
            if (x < 1e-6) {
                v = 1.0;                       // 2*J1(x)/x -> 1 as x -> 0
            } else {
                const double t = 2.0 * BesselJ1(x) / x;
                v = t * t;
            }
            lut[i] = (float)v;
        }
    }

    static constexpr float BLUR_ARCSEC_PER_STEP = 0.005075f;
    static constexpr float DefaultBlurPxPerStep() {
        return BLUR_ARCSEC_PER_STEP / EMU_PLATE_SCALE_AS_PX;
    }

    void SetBlurPxPerStep(float v) { m_blurPxPerStep = v; }

    // Freeze the planet's surface, rotation and moons, so the ONLY thing
    // varying between frames is defocus (plus jitter/drift if enabled).
    //
    // Needed because an autofocus sweep takes ~45s, over which the
    // emulator's Jupiter rotates and its belt turbulence and Great Red Spot
    // move — and that changes Laplacian variance by about as much as one
    // 600-step blur increment does. Measured: position 47600 at blur
    // radius 3 superpixels scored 6.4% while 48800 at radius 0 — no blur
    // whatsoever — scored 5.5%, which no pure blur model can produce. With
    // the surface frozen the V-curve is a function of focus alone, which is
    // what makes a bench focus run repeatable.
    void SetFreezeSurface(bool on) { m_freezeSurface = on; }

    // Per-pixel sensor noise, peak amplitude in DN (0 = off).
    //
    // The planet is rendered analytically, so without this every frame
    // is identically sharp and perfectly clean - which makes the whole
    // point of lucky imaging unmeasurable on the bench. Averaging N
    // aligned frames cuts independent noise by sqrt(N), and that gain
    // is most of what a quick-look stack buys; with no noise present a
    // perfect stack can at best EQUAL a single frame.
    //
    // Deterministic hash of (x, y, frame) rather than rand(): no
    // shared state on the generator thread, and runs are repeatable.
    void SetNoiseAmplitude(int dn) { m_noiseAmp = dn < 0 ? 0 : dn; }

    // Atmospheric blur, arcsec FWHM-ish, varying frame to frame.
    //
    // SetSeeingJitterArcsec only MOVES the planet - it never softens it,
    // so every emulated frame was equally sharp. That left nothing for
    // the parts of this app whose whole job is telling good frames from
    // bad: the rolling seeing score, the capture quality gate, and the
    // quick-look stacker's top-15% selection all had a flat distribution
    // to work on and could not be exercised at all.
    //
    // Deliberately skewed rather than symmetric, because that is the
    // premise lucky imaging rests on: most frames are mushy and a few
    // are sharp. Blur runs from 0.25x to 1.0x of the set amount, biased
    // toward the blurry end, so selecting the best 15% is a real choice
    // and not an arbitrary one.
    void SetSeeingBlurArcsec(float as) { m_seeingBlurAs = as < 0 ? 0 : as; }

    // Expose the synthetic disc to a given peak level, 0 = leave the
    // gain/exposure result alone.
    //
    // brightness = (gain/100)*(exposureUs/8000) clamped to [0.35, 3.5]
    // puts the disc at 230-254 DN under most profiles - effectively
    // saturated. A real planetary imager deliberately exposes to about
    // 70-80% to keep headroom, so the emulator was reproducing an
    // exposure nobody would actually use.
    //
    // That is not cosmetic. A clipped disc has its NOISE clipped away
    // too while the sky keeps all of it, so any noise floor measured on
    // the sky is too high for the frame as a whole. Two attempts at
    // subtracting a noise floor from the sharpness metric over-corrected
    // to zero because of exactly this, and the question of whether that
    // subtraction is sound could not be answered on the bench at all.
    void SetPeakTargetDn(int dn) { m_peakTargetDn = dn < 0 ? 0 : dn; }

    // Render the named target instead of always Jupiter. Matched on a
    // leading substring so profile names like "Jupiter" and target
    // names like "Jupiter (Io transit)" both resolve; anything
    // unrecognised stays on Jupiter, which is the useful default for a
    // generic planetary test.
    void SetTargetName(const std::string& name) {
        for (int i = 0; i < NUM_EMU_TARGETS; i++) {
            const char* n = EMU_TARGETS[i].name;
            if (_strnicmp(name.c_str(), n, strlen(n)) == 0) {
                m_target = i;
                // The override belongs to the OLD target. Cleared here
                // rather than left for the caller, because a Jupiter
                // radius still applied after switching to Mars is a
                // disc three times too big and nothing says so.
                m_radiusOverrideAs = 0.0f;
                printf("[CameraEmulator] Target: %s\n", n);
                return;
            }
        }
        m_target = 0;
        m_radiusOverrideAs = 0.0f;
    }
    const char* TargetName() const { return EMU_TARGETS[m_target].name; }

    // ── Phase 94.2: the apparent size the sky is offering tonight ──
    //
    // EMU_TARGETS carries a radius "at a representative opposition",
    // which is a constant standing in for something that is not
    // constant. Mars runs 3.5" to 25" across its synodic cycle, a
    // factor of seven, so the table is wrong nearly always and most
    // wrong exactly when it matters -- a Mars session near conjunction
    // sees a disc seven times too big and an emulator that says the
    // framing is fine when it is not.
    //
    // Pushed in rather than fetched. The ephemeris lives in
    // src/sky/SkyObject.h, this file is src/core, and every other
    // input here arrives through a setter or a provider; an emulator
    // that reaches out for its own facts is a different design from
    // the one this file already has. main.cpp computes it beside
    // SetTargetName, which is already called at exactly the moments
    // the answer can change.
    //
    // Zero or negative clears the override and the table takes over
    // again, which is what a star or any target with no ephemeris
    // needs.
    void SetTargetAngularRadiusArcsec(float arcsec) {
        m_radiusOverrideAs = (arcsec > 0.0f) ? arcsec : 0.0f;
        if (m_radiusOverrideAs > 0.0f)
            printf("[CameraEmulator] %s apparent radius %.2f\" "
                   "(table says %.2f\")\n",
                   EMU_TARGETS[m_target].name, m_radiusOverrideAs,
                   EMU_TARGETS[m_target].radiusArcsec);
    }

    // ── Phase 94.3: which way the sunlight is coming from ────
    //
    // The lunar phase, 0 new, 0.25 first quarter, 0.5 full, which is
    // MoonRADec's own convention. Pushed in for the same reason the
    // apparent size is: the ephemeris lives in src/sky and this is
    // src/core.
    //
    // Default 0.25 rather than 0.5. A full Moon is the WORST case for
    // this feature -- the Sun is behind the observer, no shadow falls
    // anywhere, and the surface goes back to being a flat bright
    // field. A quarter puts the terminator through the middle of the
    // frame, which is what the emulator exists to show.
    void SetLunarPhase(float frac) { m_lunarPhase = frac; }
    float LunarPhase() const { return m_lunarPhase; }

    // What the renderer should actually use. One place, so the disc,
    // the rings and the moons cannot disagree about how big the
    // target is.
    float EffectiveRadiusArcsec() const {
        return m_radiusOverrideAs > 0.0f ? m_radiusOverrideAs
                                         : EMU_TARGETS[m_target].radiusArcsec;
    }

    // ── Video capture ──────────────────────────────────────────

    bool StartVideoCapture() override {
        if (!m_isOpen || m_isCapturing) return false;
        m_isCapturing = true;
        m_frameCounter = 0;

        // ── 1 ms timer resolution, for the life of the capture ──
        //
        // Frame pacing is a sleep, and at the default granularity a
        // sleep rounds up to the next 15.6 ms tick -- so whatever rate
        // is asked for, delivery lands on a multiple of 64 fps. That is
        // the whole of the emulator's long-reported "16 fps" (62.4 ms
        // is four ticks) and it reappeared the moment the 20 fps cap
        // came off: every format plateaued at 64.
        //
        // Window::Run already raises it for the app's own loop, which
        // is why the app saw 19.6 fps where a bench harness saw 15.9.
        // Depending on that is depending on a side effect of the GUI;
        // the generator needs it whoever is running it. The calls nest
        // and are reference counted, so doing both is correct.
        timeBeginPeriod(1);
        m_timerRaised = true;

        // Pre-render one frame synchronously so GetVideoFrame succeeds immediately
        // — prevents NO SIGNAL flash on startup before generator thread runs
        {
            const int W = m_roiW, H = m_roiH;
            std::vector<uint8_t> firstFrame(W * H, 0);
            const float CX=W*0.5f,CY=H*0.5f,JR=H*0.22f,JRy=JR*(1.f-0.0649f);
            const float PI=3.14159265f, iR=1.f/JR, iRy=1.f/JRy;
            float brightness = (m_gain/100.0f)*(m_exposureUs/8000.0f);
            if(brightness<0.35f)brightness=0.35f; if(brightness>3.5f)brightness=3.5f;
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

    void StopVideoCapture() override {
        if (!m_isCapturing) return;
        m_isCapturing = false;
        if (m_genThread.joinable()) m_genThread.join();
        // Released here rather than at Close: raised timer resolution
        // costs power across the whole system, and a stopped camera has
        // no reason to hold it.
        if (m_timerRaised) { timeEndPeriod(1); m_timerRaised = false; }
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

    bool RestartCapture() override {
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

    int GetDroppedFrames() const override { return 0; }

    // ── Info ───────────────────────────────────────────────────

    const ASI_CAMERA_INFO& Info() const { return m_info; }
    int   CameraId()    const override { return 0; }
    int   MaxWidth()    const { return 3840; }
    int   MaxHeight()   const { return 2160; }
    float Temperature() const { return m_sensorTemp; }
    std::string Name()  const { return std::string(m_info.Name); }

    static int FrameSize(int width, int height, ASI_IMG_TYPE imgType) {
        // This used to ignore its arguments and answer 1280x960 always,
        // which was true while the ROI was locked there. Once the ROI can
        // change it is a lie that happens to be safe -- it over-allocates
        // the ring for a smaller ROI rather than under-allocating it --
        // but it also made every byte count the app reported wrong, which
        // is exactly the figure used to judge whether a format change
        // did what was asked.
        const int W = width, H = height;
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

    // Saturn's rings: a tilted annulus with the Cassini division.
    //
    // Worth rendering properly rather than as a blank band, because the
    // division and the A/B brightness step are the highest-contrast detail
    // on the target and are exactly what focus and the sharpness metric
    // get judged on in a real Saturn session.
    static bool RingSample(int px, int py, float cx, float cy, float JR,
                           const EmuTargetDef& tg, float brightness,
                           uint8_t& r8, uint8_t& g8, uint8_t& b8) {
        const float RING_TILT = 0.44f;          // ~26 deg ring opening
        const float outR = JR * tg.ringOuter;
        const float dx   = (px - cx) / outR;
        const float dy   = (py - cy) / (outR * RING_TILT);
        const float rr   = sqrtf(dx*dx + dy*dy);
        const float inFrac = tg.ringInner / tg.ringOuter;
        if (rr > 1.0f || rr < inFrac) return false;

        // Cassini division at ~0.72 of the way out, then the brighter B
        // ring inside it and the fainter A ring outside.
        const float band = (rr - inFrac) / (1.0f - inFrac);   // 0 in, 1 out
        float lum;
        if (band > 0.70f && band < 0.78f)      lum = 0.18f;   // Cassini
        else if (band < 0.70f)                 lum = 0.92f;   // B ring
        else                                   lum = 0.60f;   // A ring

        const float v = lum * brightness;
        auto clamp8 = [](float f) -> uint8_t {
            const float s = f * 255.0f;
            return (uint8_t)(s > 255.0f ? 255.0f : (s < 0.0f ? 0.0f : s));
        };
        r8 = clamp8(v * tg.r);
        g8 = clamp8(v * tg.g);
        b8 = clamp8(v * tg.b);
        return true;
    }

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
        const EmuTargetDef& TG = EMU_TARGETS[m_target];
        // Phase 91: pixels per arcsecond, so the disc is the size the
        // sky makes it and the frame decides how much sky is in it.
        // A floor of 2 px keeps a distant target from collapsing to
        // nothing if the scale is ever configured absurdly.
        const float JR  = (std::max)(2.0f,
                              EffectiveRadiusArcsec() / EMU_PLATE_SCALE_AS_PX);
        const float JRy = JR*(1.0f-TG.oblate);
        const float ROT = 9.9f*3600.0f, PI = 3.14159265f;
        const float iR = 1.0f/JR, iRy = 1.0f/JRy;
        static const int LUT_N = 512;
        struct LUTEntry { uint8_t r, g, b; };
        std::vector<LUTEntry> latLUT(LUT_N);
        struct MoonPos { float x, y, r; uint8_t r8, g8, b8; };
        MoonPos moonPos[NUM_JUP_MOONS];
        std::vector<uint8_t> backBuf(W*H, 0);
        // ── Frame pacing (P4.3) ───────────────────────────
        //
        // This was a fixed 50 ms, so the emulator could never deliver
        // more than 20 fps. Planetary capture is a 100-200 fps game at
        // a small ROI, which meant the decoupled recording path, the
        // run-sized write buffer and the SER writer had never once been
        // exercised at the rate they were built for -- only at a fifth
        // of it. The cap, not the renderer, was the limit: a 640x480
        // frame takes a couple of milliseconds to make.
        //
        // sleep_for(TARGET_MS - renderMs) was also the wrong shape. It
        // sleeps for a DURATION measured after the work, so every
        // frame's error accumulates into the next, and at the default
        // Windows timer granularity it rounded 50 ms up to 62.4 --
        // which is where the emulator's long-reported "16 fps" came
        // from. sleep_until a running deadline has no such drift.
        const double periodMs = 1000.0 / m_targetFps;
        auto nextDue = std::chrono::steady_clock::now();
        auto startTime = std::chrono::steady_clock::now();

        // Phase 94.1: the Airy profile depends on nothing that changes
        // between frames, so it is tabulated once here rather than 256
        // Bessel evaluations per frame. Same reasoning as phase 91
        // hoisting the atomic frame counter out of the pixel loop.
        std::vector<float> airyLUT((size_t)AIRY_N);
        BuildAiryLUT(airyLUT.data(), AIRY_N, AIRY_SPAN);

        while (m_isCapturing) {
            auto frameStart = std::chrono::steady_clock::now();
            float timeS = std::chrono::duration<float>(frameStart - startTime).count();
            float brightness = (m_gain/100.0f)*(m_exposureUs/8000.0f);
            // Brightness floor raised 0.1 → 0.35: profile auto-load on GoTo
            // can set sub-millisecond exposures (Moon/Venus profiles), and at
            // the old floor the synthetic disk peaked below the analyzer's
            // rmax>20 centroid gate — the tracking test target went black
            // (PK 0%) and acquisition could never lock. The emulator is a
            // test rig: the disk must stay trackable at any profile while
            // still responding to gain/exposure above the floor.
            if (brightness < 0.35f) brightness = 0.35f;
            if (brightness > 3.5f) brightness = 3.5f;
            // Frozen surface clock: see SetFreezeSurface. Only defocus
            // (and jitter/drift) should move while a focus sweep runs.
            const float tSurf = m_freezeSurface ? 0.0f : timeS;
            float rotPhase = fmodf(tSurf/ROT, 1.0f);
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
                RGB col;
                if (TG.belts) {
                    col = JupiterSurfaceColor(lat_deg, midLon, tSurf);
                } else {
                    // Flat tint with a faint latitude banding so there
                    // is still some detail for the sharpness metric to
                    // bite on - a perfectly smooth disc would make every
                    // focus position measure identically.
                    const float band =
                        1.0f + 0.05f * sinf(lat_deg * 0.18f + midLon*6.2832f);
                    col = { TG.r*band, TG.g*band, TG.b*band };
                }
                latLUT[i] = {(uint8_t)(col.r*255.f>255?255:col.r*255.f),
                             (uint8_t)(col.g*255.f>255?255:col.g*255.f),
                             (uint8_t)(col.b*255.f>255?255:col.b*255.f)};
            }

            // Expose to the requested peak. The LUT above does not depend
            // on brightness, so its maximum is the disc's brightest
            // surface value and scaling against it lands the peak exactly.
            if (m_peakTargetDn > 0) {
                int lutMax = 1;
                for (int i = 0; i < LUT_N; i++) {
                    const LUTEntry& e = latLUT[i];
                    if (e.r > lutMax) lutMax = e.r;
                    if (e.g > lutMax) lutMax = e.g;
                    if (e.b > lutMax) lutMax = e.b;
                }
                brightness = (float)m_peakTargetDn / (float)lutMax;
            }

            // Precompute moon positions once per frame
            for (int m = 0; m < (TG.moons ? NUM_JUP_MOONS : 0); m++) {
                const JupMoon& moon = JUP_MOONS[m];
                float angle = (2.f*PI*tSurf/(moon.period_d*86400.f)) + m*1.1f;
                moonPos[m].x  = PCX + JR*moon.dist*cosf(angle);
                moonPos[m].y  = PCY + JRy*0.15f*sinf(angle);
                // Two pixels minimum: below that a moon stops being a
                // thing you can see and the emulator stops being able
                // to show you whether it is in frame.
                moonPos[m].r  = (std::max)(2.0f,
                                    moon.radiusArcsec / EMU_PLATE_SCALE_AS_PX);
                float b = brightness>1.f?1.f:brightness;
                moonPos[m].r8 = (uint8_t)(moon.r*b*255.f);
                moonPos[m].g8 = (uint8_t)(moon.g*b*255.f);
                moonPos[m].b8 = (uint8_t)(moon.b*b*255.f);
            }
            const uint8_t bgVal = (uint8_t)(4.f*brightness);

            // Phase 91: hoisted out of the per-pixel loop.
            //
            // m_frameCounter is a std::atomic, and it was being read
            // once per PIXEL -- 8.3 million atomic loads a frame at the
            // full sensor. On x86 the load itself is cheap, but the
            // compiler may not hoist it or vectorise around it, so the
            // whole inner loop was pinned to scalar code for the sake
            // of a value that does not change within a frame. Same for
            // the noise amplitude and the ring flag: members reachable
            // through `this`, reloaded because the writes to backBuf
            // could in principle alias them.
            const uint32_t frameSalt = (uint32_t)m_frameCounter * 83492791u;
            const int      noiseAmp  = m_noiseAmp;
            const bool     hasRings  = (TG.ringOuter > 0.0f);
            // Phase 94.1: a star has no disc, so every pixel takes the
            // background branch and the point source is painted
            // afterwards in its own bounding box, the way the moons
            // are. One hoisted bool rather than a second copy of the
            // background, hash sprinkle and noise code.
            const bool     isStar    = (TG.kind == EmuKind::Star);
            // Phase 94.3: the Moon is a patch of a sphere rather than
            // a shape inside the frame, so it owns the whole pixel
            // loop instead of being painted into it.
            const bool     isLunar   = (TG.kind == EmuKind::Lunar);

            // Main loop — LUT lookup + sqrtf only, no asinf/sinf/fmod per pixel
            //
            // Skipped entirely for the Moon, which writes every pixel
            // itself below. Without this guard the frame was painted
            // twice: 8.3 million background pixels at the full sensor,
            // all of them immediately overwritten.
            for (int py = 0; !isLunar && py < H; py++) {
                float dy  = (py-PCY)*iRy, dy2 = dy*dy;
                for (int px = 0; px < W; px++) {
                    float dx = (px-PCX)*iR;
                    float r2 = dx*dx + dy2;
                    uint8_t rv, gv, bv;
                    if (!isStar && !isLunar && r2 <= 1.0f) {
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
                    } else if (hasRings &&
                               RingSample(px, py, PCX, PCY, JR, TG,
                                          brightness, rv, gv, bv)) {
                        // On the ring system - RingSample filled rv/gv/bv.
                    } else {
                        uint32_t h = (uint32_t)(px*7919+py*6271);
                        h^=h>>16; h*=0x45d9f3b; h^=h>>16;
                        // Masks, not modulo. Two integer divisions per
                        // background pixel is 16 million divisions on a
                        // full-sensor frame, for a sprinkle of faint
                        // stars. 1-in-4096 is the same sparseness.
                        uint8_t sv = ((h & 4095u)==0) ?
                            (uint8_t)((((h>>12)&255u)/255.f)*brightness*255.f)
                            : bgVal;
                        rv=gv=bv=sv;
                    }
                    bool eR=(py%2==0), eC=(px%2==0);
                    uint8_t mv = (eR&&eC)?rv:(!eR&&!eC)?bv:gv;
                    if (noiseAmp > 0) {
                        uint32_t hh = (uint32_t)px * 73856093u
                                    ^ (uint32_t)py * 19349663u
                                    ^ frameSalt;
                        hh ^= hh >> 13; hh *= 0x5bd1e995u; hh ^= hh >> 15;
                        const int nz = (int)(hh & 1023u) - 512;   // -512..511
                        // The +m_noiseAmp pedestal stands in for a sensor
                        // bias offset, and is not cosmetic. This emulator's
                        // sky sits near 4 DN, so without it the negative
                        // half of the noise clips at zero, the background
                        // noise measures about half its true amplitude, and
                        // any estimator calibrated on the background
                        // under-corrects. Real sensors carry an offset for
                        // exactly this reason.
                        int val = (int)mv + noiseAmp
                                + (nz * noiseAmp) / 512;
                        mv = (uint8_t)(val < 0 ? 0 : (val > 255 ? 255 : val));
                    }
                    backBuf[py*W+px] = mv;
                }
            }

            // ── The Moon, as a piece of a sphere ──────────────
            //
            // Every other target is a shape inside the frame. This one
            // is bigger than the frame: at 0.223"/px the full sensor
            // spans 0.476 of the lunar diameter and a 640x480 ROI only
            // 0.079. So there is no limb to draw and no disc to fill;
            // what the sensor sees is a patch, and each pixel has to
            // be turned into a place on the sphere.
            //
            // Takes the whole pixel loop rather than painting over it,
            // because unlike the star there is no background left to
            // preserve: the Moon fills the frame.
            if (isLunar) {
                const SunDir sun = MoonSunDirection((double)m_lunarPhase);
                // Pixels per lunar radius. EffectiveRadiusArcsec is the
                // table's 900" until 94.2 learns the Moon's distance.
                // Floored, as the disc radius and the Airy radius are.
                // Nothing can reach zero today -- the Moon never takes
                // the live-size override -- but a radius of a few
                // pixels would push every u,v past the limb and render
                // a black frame with nothing to say why.
                const double Rpx = (std::max)(4.0,
                                       (double)EffectiveRadiusArcsec()
                                     / (double)EMU_PLATE_SCALE_AS_PX);
                // Crater cell size in degrees. Chosen so a cell is a
                // few tens of pixels at the full sensor: smaller and
                // the craters are noise, larger and there are none in
                // a planetary ROI.
                const double cellDeg = 5.0;
                const float  tint[3] = { TG.r, TG.g, TG.b };

                // ── Expose to the patch, not to the whole Moon ───
                //
                // m_peakTargetDn means "put the brightest thing in the
                // frame at this level", and the disc path honours that
                // by normalising against its own surface LUT. Applied
                // as a flat multiplier instead, it leaves a dim patch
                // dim -- and the patch IS dim where it matters most:
                // at a 640x480 ROI the frame spans 8 per cent of the
                // lunar radius, so a first-quarter Moon centred on the
                // disc puts the whole frame within a hair of the
                // terminator, where grazing illumination is a few per
                // cent of full. Physically right, and useless to focus
                // on: the first frame measured 6 DN.
                //
                // A real imager answers that by exposing longer. So
                // does this, by finding the frame's own maximum on a
                // coarse grid and scaling to it. Every 16th pixel is
                // enough: the illumination gradient across a patch
                // this small is smooth, and the craters that are not
                // only ever make the true peak brighter than the
                // estimate, which clips rather than darkens.
                double lumMax = 0.0;
                if (m_peakTargetDn > 0) {
                    for (int sy = 0; sy < H; sy += 16) {
                        const double v = ((double)sy - PCY) / Rpx;
                        for (int sx = 0; sx < W; sx += 16) {
                            const double u = ((double)sx - PCX) / Rpx;
                            const double il = LunarIllumination(u, v, sun);
                            if (il <= 0.0) continue;
                            double la = 0.0, lo = 0.0;
                            LunarLatLon(u, v, la, lo);
                            const double l = il * LunarAlbedo(la, lo);
                            if (l > lumMax) lumMax = l;
                        }
                    }
                    // A frame entirely on the night side has nothing to
                    // expose to. Leave it dark rather than amplifying
                    // the background by a huge factor.
                    if (lumMax < 1e-4) lumMax = 0.0;
                }

                for (int py = 0; py < H; py++) {
                    const double v = ((double)py - PCY) / Rpx;
                    for (int px = 0; px < W; px++) {
                        const double u = ((double)px - PCX) / Rpx;

                        const double illum = LunarIllumination(u, v, sun);
                        uint8_t mv;
                        if (illum < 0.0) {
                            // Past the limb. Sky, not Moon -- only
                            // reachable at a very wide field or a
                            // badly configured scale, but it must not
                            // wrap into the far side of the sphere.
                            mv = bgVal;
                        } else {
                            double lat = 0.0, lon = 0.0;
                            LunarLatLon(u, v, lat, lon);
                            const double alb = LunarAlbedo(lat, lon);
                            const double shade =
                                LunarCraterShade(lat, lon, sun, illum, cellDeg);

                            // Lambertian, which is wrong for regolith
                            // (the real Moon is far flatter across the
                            // disc and surges at full) but right in the
                            // one place this feature cares about: it
                            // goes to zero at the terminator, and it is
                            // the gradient there that focus is judged
                            // on.
                            double lum = illum * alb * shade;
                            if (lum < 0.0) lum = 0.0;

                            const bool eR = (py%2==0), eC = (px%2==0);
                            const float chan = (eR&&eC) ? tint[0]
                                             : (!eR&&!eC) ? tint[2] : tint[1];
                            double dn = lum * chan * 255.0 * brightness;
                            if (m_peakTargetDn > 0 && lumMax > 0.0)
                                dn = (lum / lumMax) * chan
                                   * (double)m_peakTargetDn;
                            // The night side keeps the sky pedestal
                            // rather than going to absolute zero: a
                            // real frame has a bias offset and noise
                            // there, and anything measuring a floor
                            // needs to find one.
                            if (dn < (double)bgVal) dn = (double)bgVal;
                            mv = (uint8_t)(dn > 255.0 ? 255.0 : dn);
                        }

                        if (noiseAmp > 0) {
                            uint32_t hh = (uint32_t)px * 73856093u
                                        ^ (uint32_t)py * 19349663u
                                        ^ frameSalt;
                            hh ^= hh >> 13; hh *= 0x5bd1e995u; hh ^= hh >> 15;
                            const int nz = (int)(hh & 1023u) - 512;
                            int val = (int)mv + noiseAmp
                                    + (nz * noiseAmp) / 512;
                            mv = (uint8_t)(val < 0 ? 0 : (val > 255 ? 255 : val));
                        }
                        backBuf[py*W+px] = mv;
                    }
                }
            }

            // ── The star, if that is what this is ─────────────
            //
            // Painted sharp. Defocus and seeing are a later pass over
            // the whole frame, so putting any blur in here would
            // double-count them -- and composing this way is what
            // gives the emulator the behaviour worth having: at low
            // seeing and near focus the rings survive, and at 2 to 3
            // arcsec they are gone, which is what a real night does to
            // a 0.68 arcsec pattern.
            //
            // The core is allowed to clip. A bright star at a planetary
            // exposure saturates, and the outer rings are what is left
            // to look at, which is also true at the eyepiece.
            if (isStar) {
                // Six Airy radii covers the core and about four rings;
                // beyond that the profile is under a thousandth of the
                // peak and the background noise owns those pixels.
                const float airyPx = (std::max)(1.0f, AiryRadiusPx());
                const float reach  = airyPx * AIRY_SPAN;

                // Peak to the requested exposure if one was asked for,
                // otherwise follow gain and exposure like everything
                // else in the frame, so changing them still does
                // something to a star. Floored well clear of the 4 DN
                // sky: a star that reads 5 DN would make every focus
                // position measure the background and nothing else.
                float peak = (m_peakTargetDn > 0)
                           ? (float)m_peakTargetDn
                           : 220.0f * brightness;
                if (peak < 40.0f)  peak = 40.0f;
                if (peak > 255.0f) peak = 255.0f;

                int x0 = (int)floorf(PCX - reach), x1 = (int)ceilf(PCX + reach);
                int y0 = (int)floorf(PCY - reach), y1 = (int)ceilf(PCY + reach);
                if (!(x1 < 0 || y1 < 0 || x0 >= W || y0 >= H)) {
                    if (x0 < 0) x0 = 0;  if (y0 < 0) y0 = 0;
                    if (x1 > W-1) x1 = W-1;  if (y1 > H-1) y1 = H-1;
                    const float invReach = (float)(AIRY_N - 1) / reach;
                    for (int py = y0; py <= y1; py++) {
                        const float sdy = py - PCY, sdy2 = sdy*sdy;
                        for (int px = x0; px <= x1; px++) {
                            const float sdx = px - PCX;
                            const float rpx = sqrtf(sdx*sdx + sdy2);
                            if (rpx > reach) continue;
                            int li = (int)(rpx * invReach + 0.5f);
                            if (li < 0) li = 0;
                            if (li >= AIRY_N) li = AIRY_N - 1;
                            const float inten = airyLUT[li] * peak;
                            if (inten < 0.5f) continue;   // below one DN
                            // Spectral colour, same Bayer parity the
                            // disc and the moons use.
                            const bool eR = (py%2==0), eC = (px%2==0);
                            const float chan = (eR&&eC) ? TG.r
                                             : (!eR&&!eC) ? TG.b : TG.g;
                            int val = (int)(inten * chan);
                            // Add to the background rather than
                            // replacing it, so the sprinkle and the
                            // noise pedestal underneath stay.
                            val += (int)backBuf[py*W+px];
                            if (val > 255) val = 255;
                            backBuf[py*W+px] = (uint8_t)val;
                        }
                    }
                }
            }

            // ── Moons, after the disc ─────────────────────────
            //
            // Phase 91: these used to be four distance tests inside the
            // per-pixel loop, which on a full-sensor frame is 33 million
            // tests to paint about three hundred pixels. Each moon now
            // walks only its own bounding box, clipped to the frame --
            // and clipping is the whole point, because a moon at
            // elongation genuinely falls off the edge of this sensor and
            // the emulator has to be able to show that.
            for (int m = 0; m < (TG.moons ? NUM_JUP_MOONS : 0); m++) {
                const float mx = moonPos[m].x, my = moonPos[m].y;
                const float mr = moonPos[m].r, mr2 = mr*mr;
                int x0 = (int)floorf(mx - mr), x1 = (int)ceilf(mx + mr);
                int y0 = (int)floorf(my - mr), y1 = (int)ceilf(my + mr);
                if (x1 < 0 || y1 < 0 || x0 >= W || y0 >= H) continue;
                if (x0 < 0) x0 = 0;  if (y0 < 0) y0 = 0;
                if (x1 > W-1) x1 = W-1;  if (y1 > H-1) y1 = H-1;
                for (int py = y0; py <= y1; py++) {
                    const float mdy = py - my, mdy2 = mdy*mdy;
                    for (int px = x0; px <= x1; px++) {
                        const float mdx = px - mx;
                        if (mdx*mdx + mdy2 >= mr2) continue;
                        const bool eR = (py%2==0), eC = (px%2==0);
                        uint8_t mv = (eR&&eC) ? moonPos[m].r8
                                   : (!eR&&!eC) ? moonPos[m].b8
                                   : moonPos[m].g8;
                        if (noiseAmp > 0) {
                            uint32_t hh = (uint32_t)px * 73856093u
                                        ^ (uint32_t)py * 19349663u
                                        ^ frameSalt;
                            hh ^= hh >> 13; hh *= 0x5bd1e995u; hh ^= hh >> 15;
                            const int nz = (int)(hh & 1023u) - 512;
                            int val = (int)mv + noiseAmp
                                    + (nz * noiseAmp) / 512;
                            mv = (uint8_t)(val < 0 ? 0 : (val > 255 ? 255 : val));
                        }
                        backBuf[py*W+px] = mv;
                    }
                }
            }

            // ── Defocus (Phase 73) ────────────────────────────
            // Soften the frame by however far the focuser is from true
            // focus, so sharpness actually varies with EAF position and an
            // autofocus V-curve has something real to fit.
            int focusPos = 0;
            if (m_focusProvider && m_focusProvider(focusPos)) {
                float diaPx = fabsf((float)(focusPos - m_trueFocus))
                            * m_blurPxPerStep;
                if (m_seeingBlurAs > 0.0f) {
                    // Per-frame seeing draw, skewed toward blurry.
                    uint32_t sh = (uint32_t)m_frameCounter * 2654435761u;
                    sh ^= sh >> 15; sh *= 0x2545f491u; sh ^= sh >> 13;
                    const float u = (float)(sh & 0xFFFFu) / 65535.0f;
                    // Review pass 2: this was 0.25 + 0.75*u*u, commented
                    // "skewed toward blurry" while doing the opposite --
                    // u*u pushes the draw toward its 0.25 floor, so 71%
                    // of frames came out on the SHARP side of the range.
                    // That is backwards for simulating seeing, and it
                    // flatters exactly the features the switch exists to
                    // test: lucky imaging works because most frames are
                    // mediocre and a few are excellent. sqrt(u) skews the
                    // other way, so a good frame is the exception.
                    const float draw = 0.25f + 0.75f * sqrtf(u);
                    const float seeingPx = m_seeingBlurAs
                                         / EMU_PLATE_SCALE_AS_PX * draw;
                    // Independent blurs add in quadrature, not linearly.
                    diaPx = sqrtf(diaPx * diaPx + seeingPx * seeingPx);
                }
                // diaPx is the blur DIAMETER in sensor pixels. Three box
                // passes of radius r superpixels add variance, giving an
                // equivalent Gaussian sigma of sqrt((2r+1)^2-1)/2
                // superpixels; wanting dia ~ 4 sigma and 1 superpixel = 2
                // sensor px inverts to:
                float rf = (sqrtf(diaPx * diaPx / 16.0f + 1.0f) - 1.0f) * 0.5f;
                if (rf > 64.0f) rf = 64.0f;     // bound the cost at 4K

                // Fractional radius by blending two integer blur levels.
                // Integer superpixel radii alone are far too coarse here:
                // r stays 0 until ~800 steps off focus, so the whole middle
                // of autofocus's sweep would measure identically sharp and
                // the fit could not locate focus to better than +/-800
                // steps. Blending r and r+1 makes defocus continuous, which
                // is what gives the V-curve a findable vertex.
                const int   r0   = (int)rf;
                const float frac = rf - (float)r0;
                if (r0 >= 1 || frac > 0.01f) {
                    if (m_blurTmp.size() != backBuf.size())
                        m_blurTmp.resize(backBuf.size());
                    memcpy(m_blurTmp.data(), backBuf.data(), backBuf.size());
                    BlurBayer(backBuf.data(),   W, H, r0,
                              m_blurScratch, m_blurRing, m_blurSum, m_blurMeanA);
                    BlurBayer(m_blurTmp.data(), W, H, r0 + 1,
                              m_blurScratch, m_blurRing, m_blurSum, m_blurMeanB);
                    const int fx = (int)(frac * 256.0f + 0.5f);
                    uint8_t* a = backBuf.data();
                    const uint8_t* b = m_blurTmp.data();
                    for (size_t i = 0; i < backBuf.size(); i++)
                        a[i] = (uint8_t)(((int)a[i] * (256 - fx)
                                        + (int)b[i] * fx) >> 8);
                    if ((m_frameCounter % 60) == 0)
                        printf("[EMU] defocus: pos=%d true=%d  dia=%.1fpx "
                               "(rf=%.2f superpx)\n",
                               focusPos, m_trueFocus, diaPx, rf);
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
            // Phase 91: the emulator's frame rate is a property of this
            // renderer, and it was only ever known by inference from the
            // app's fps readout -- which also contains the app. Say it
            // directly, once every few seconds.
            m_genRenderMs.store((double)renderMs, std::memory_order_relaxed);
            if ((m_frameCounter % 120) == 0)
                printf("[EMU] render %dx%d: %d ms (%.1f fps ceiling)\n",
                       W, H, renderMs,
                       renderMs > 0 ? 1000.0 / renderMs : 999.0);
            // A real camera cannot deliver frames faster than it can
            // expose them, and an emulator that does would validate the
            // pipeline against a regime that cannot occur live -- the
            // same trap the Phase 52 note describes for GetVideoFrame.
            const double expMs  = (double)m_exposureUs / 1000.0;
            const double period = (periodMs > expMs) ? periodMs : expMs;

            // What the generator is ACTUALLY producing, as opposed to
            // what it was asked for. Separating this from the rate the
            // app takes frames at is the measurement that was missing
            // every time this came up: "the camera is slow" and "we
            // are slow at collecting" look identical from outside.
            {
                const auto nowG = std::chrono::steady_clock::now();
                if (m_genLast.time_since_epoch().count() != 0) {
                    const double dt = std::chrono::duration<double>(
                                          nowG - m_genLast).count();
                    if (dt > 0.0) {
                        const double inst = 1.0 / dt;
                        const double prev = m_genFps.load(
                                                std::memory_order_relaxed);
                        m_genFps.store(prev <= 0.0 ? inst
                                                   : prev * 0.9 + inst * 0.1,
                                       std::memory_order_relaxed);
                    }
                }
                m_genLast = nowG;
            }

            nextDue += std::chrono::microseconds((long long)(period * 1000.0));
            const auto now2 = std::chrono::steady_clock::now();
            if (nextDue > now2) {
                std::this_thread::sleep_until(nextDue);
            } else {
                // Behind. Do not try to catch up: a burst of frames
                // emitted back to back is not something a camera does,
                // and it would hide the overrun being measured.
                nextDue = now2;
            }
        }
    }

    ASI_CAMERA_INFO             m_info{};
    bool                        m_isOpen      = false;
    bool                        m_isCapturing = false;
    int                         m_roiW = 3840, m_roiH = 2160;
    int                         m_usbBandwidth = 80;
    bool                        m_highSpeed    = true;
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

    // Phase 73 defocus — set before StartVideoCapture()
    std::function<bool(int&)>   m_focusProvider;
    // Real focus for this rig, measured at 168187 on a good night. The
    // focuser starts at 166500, so the default sits 1500 steps out -
    // comfortably inside autofocus's +/-3000 sweep and close to what a
    // real session starts from.
    int                         m_trueFocus     = 168000;
    float                       m_blurPxPerStep = DefaultBlurPxPerStep();
    bool                        m_freezeSurface = false;
    int                         m_noiseAmp      = 0;
    float                       m_seeingBlurAs  = 0.0f;
    double                      m_targetFps     = 20.0;   // P4.3
    bool                        m_timerRaised   = false;  // P4.3
    std::atomic<double>         m_genFps{0.0};            // P4.3
    std::atomic<double>         m_genRenderMs{0.0};       // P4.3
    std::chrono::steady_clock::time_point m_genLast{};    // generator only
    int                         m_peakTargetDn  = 0;
    int                         m_target        = 0;   // index into EMU_TARGETS
    // Phase 94.2: live apparent radius, 0 = use the table.
    float                       m_radiusOverrideAs = 0.0f;
    // Phase 94.3: lunar phase fraction, 0.25 = first quarter.
    float                       m_lunarPhase       = 0.25f;
    std::vector<int>            m_blurScratch;   // generator thread only
    std::vector<int>            m_blurRing;      // P4.4a column-tile window
    std::vector<int>            m_blurSum;       // P4.4a column-tile sums
    // One per radius in use, so the two blends do not rebuild each
    // other's table every frame.
    std::vector<uint8_t>        m_blurMeanA, m_blurMeanB;
    std::vector<uint8_t>        m_blurTmp;       // generator thread only

    // ── sum / win without dividing (P4.4a) ────────────────────
    //
    // The box filter divides once per OUTPUT ELEMENT, and there are six
    // full-frame separable passes per rendered frame: at 3840x2160
    // that is fifty million integer divisions, which turned out to cost
    // more than the cache misses the tiling above was chasing.
    //
    // A running sum of a box of uint8 is bounded by 255*win, so a table
    // of every value it can take is a few hundred bytes and sits in L1.
    // It is built by doing the division, so it is exact rather than
    // approximately exact -- no reciprocal, no rounding to argue about,
    // and the output is bit-identical to dividing.
    static const uint8_t* MeanTable(int win, std::vector<uint8_t>& t) {
        const int n = 255 * win + 1;
        if ((int)t.size() != n) {
            t.resize(n);
            for (int i = 0; i < n; i++) t[i] = (uint8_t)(i / win);
        }
        return t.data();
    }

    // Box-blur one strided 1-D run in place, with edge replication.
    // Running sum, so cost is O(count) and independent of radius.
    static void BoxBlur1D(uint8_t* p, int count, int stride, int r,
                          std::vector<int>& scratch,
                          const uint8_t* mean) {
        if (r < 1 || count < 2) return;
        scratch.resize(count);
        for (int i = 0; i < count; i++)
            scratch[i] = p[(size_t)i * stride];
        const int win = 2 * r + 1;
        auto clampIdx = [count](int i) {
            return i < 0 ? 0 : (i >= count ? count - 1 : i);
        };
        long sum = 0;
        for (int k = -r; k <= r; k++) sum += scratch[clampIdx(k)];
        for (int i = 0; i < count; i++) {
            p[(size_t)i * stride] = mean[sum];
            sum += scratch[clampIdx(i + r + 1)] - scratch[clampIdx(i - r)];
        }
    }

    // Separable blur over the RGGB mosaic. Both passes step by 2 so a pixel
    // is only ever averaged with same-colour neighbours — blurring the
    // mosaic directly would mix R, G and B and produce colour fringes
    // rather than defocus. Radius is in superpixels (1 superpixel = 2 px).
    //
    // THREE box passes, not one. A single box filter's transfer function is
    // a sinc: it has zeros and sidelobes, so it does not attenuate high
    // spatial frequencies monotonically, and at some radii it lets aliased
    // detail through amplified. Against FrameAnalyzer's 4-superpixel
    // sampling lattice that produced a sharpness curve where blur radius 1
    // measured 24.9% and radius 0 — no blur at all — measured 4.9%, which
    // made the V-curve unusable. Three box passes approximate a Gaussian,
    // whose response is monotonic with no sidelobes, so more defocus always
    // measures softer. Cost is 3x an O(pixels) pass, which is acceptable
    // for a 20fps emulator and only runs when out of focus.
    // ── The vertical pass, in column tiles (P4.4a) ────────────
    //
    // Mathematically identical to running BoxBlur1D down each column:
    // same box, same window, same edge replication. The difference is
    // the order the memory is touched in.
    //
    // Down a single column the step is 2*W bytes -- 7680 at the full
    // sensor -- so every element is its own cache line, the walk evicts
    // the lines the NEXT column will want, and the whole frame is
    // re-fetched once per column. That is why the blur cost 33 ms at
    // 2.07 MP and 494 ms at 8.29 MP: four times the pixels, fifteen
    // times the time, which is not pixel count, it is misses.
    //
    // A tile of adjacent columns is filtered together instead. The
    // running sums for the whole tile live in a small array that stays
    // hot, and each row touch is TILE contiguous bytes, so every line
    // fetched is used in full. Reads stay ahead of writes: output row k
    // is written before row k+r+1 is read.
    static void BoxBlurColumnsTiled(uint8_t* buf, int W, int H, int r,
                                    int rowPhase,
                                    std::vector<int>& ring,
                                    std::vector<int>& sum,
                                    const uint8_t* mean) {
        if (r < 1 || rowPhase >= H) return;
        const int count = (H - rowPhase + 1) / 2;
        if (count < 2) return;
        const int win  = 2 * r + 1;
        const int TILE = 256;

        auto RowPtr = [&](int k) { return buf + (size_t)(rowPhase + 2 * k) * W; };
        auto ClampK = [count](int k) {
            return k < 0 ? 0 : (k >= count ? count - 1 : k);
        };

        ring.assign((size_t)win * TILE, 0);
        sum.assign(TILE, 0);

        for (int x0 = 0; x0 < W; x0 += TILE) {
            const int tw = (W - x0 < TILE) ? (W - x0) : TILE;

            // Prime with the window centred on row 0: elements -r..+r,
            // edge-replicated. Element m lives in slot (m + r) % win.
            for (int t = 0; t < tw; t++) sum[t] = 0;
            for (int j = -r; j <= r; j++) {
                const uint8_t* src = RowPtr(ClampK(j)) + x0;
                int* slot = &ring[(size_t)((j + r) % win) * TILE];
                for (int t = 0; t < tw; t++) { slot[t] = src[t]; sum[t] += src[t]; }
            }

            for (int k = 0; k < count; k++) {
                uint8_t* dst = RowPtr(k) + x0;
                for (int t = 0; t < tw; t++)
                    dst[t] = mean[sum[t]];

                // Leaving element k-r and arriving element k+r+1 share
                // slot k % win, so the arrival overwrites the departure.
                const uint8_t* in = RowPtr(ClampK(k + r + 1)) + x0;
                int* slot = &ring[(size_t)(k % win) * TILE];
                for (int t = 0; t < tw; t++) {
                    sum[t] += in[t] - slot[t];
                    slot[t] = in[t];
                }
            }
        }
    }

    static void BlurBayerBoxPass(uint8_t* buf, int W, int H, int r,
                                 std::vector<int>& scratch,
                                 std::vector<int>& ring,
                                 std::vector<int>& sum,
                                 const uint8_t* mean) {
        // Horizontal first. Its stride is 2 BYTES, so the row is walked
        // essentially sequentially and there is nothing to win here.
        for (int py = 0; py < H; py++)
            for (int c0 = 0; c0 < 2 && c0 < W; c0++)
                BoxBlur1D(buf + (size_t)py * W + c0,
                          (W - c0 + 1) / 2, 2, r, scratch, mean);
        for (int r0 = 0; r0 < 2 && r0 < H; r0++)
            BoxBlurColumnsTiled(buf, W, H, r, r0, ring, sum, mean);
    }

    static void BlurBayer(uint8_t* buf, int W, int H, int r,
                          std::vector<int>& scratch,
                          std::vector<int>& ring,
                          std::vector<int>& sum,
                          std::vector<uint8_t>& meanTab) {
        if (r < 1) return;
        const uint8_t* mean = MeanTable(2 * r + 1, meanTab);
        for (int pass = 0; pass < 3; pass++)
            BlurBayerBoxPass(buf, W, H, r, scratch, ring, sum, mean);
    }

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
