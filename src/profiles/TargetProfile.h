#pragma once
// ============================================================
// TargetProfile.h — Camera + EAF + capture profile per target
//
// Settings are research-calibrated for:
//   Camera:    ZWO ASI678MC (Sony IMX678, 2µm pixels, STARVIS 2)
//   Telescope: Celestron 8SE (f/10, 2032mm FL)
//   Plate scale: 0.203 arcsec/pixel
//
// KEY CAMERA FACTS (ASI678MC):
//   - HCG mode auto-activates at gain >= 182
//     → read noise drops from ~3.5e to 0.8e
//     → always use gain >= 182 for planets
//   - Unity gain: ~100 (but noisy — avoid for planets)
//   - Max FPS: 47.5 at 3840×2160, 102+ at 1920×1080
//   - Full well: 11,270e (keep histogram 50-70%)
//   - No amp glow at any gain/exposure
//   - WB neutral: R=52, B=47 (sensor default)
//
// RAILS (hardware limits — never exceed these):
//   Gain:     0 – 570
//   Exposure: 0.032ms – 2000000ms
//   Gamma:    1 – 100
//   WB Red:   0 – 100
//   WB Blue:  0 – 100
//   USB BW:   40 – 100
// ============================================================

#include <string>
#ifdef RADUK_EMULATOR
#  include "ASICamera2Stub.h"
#else
#  include "ASICamera2.h"
#endif

namespace raduk::profiles {

// ── Hardware rails — enforce these in the UI ───────────────────
struct CameraRails {
    // Gain
    static constexpr int GAIN_MIN = 0;
    static constexpr int GAIN_MAX = 570;
    static constexpr int GAIN_HCG_THRESHOLD = 182; // HCG activates here

    // Exposure
    static constexpr double EXP_MIN_MS  = 0.032;
    static constexpr double EXP_MAX_MS  = 2000000.0;

    // Planet imaging practical limits
    static constexpr double EXP_PLANET_MIN_MS =  1.0;  // below this = oversampling seeing
    static constexpr double EXP_PLANET_MAX_MS = 50.0;  // above this = motion blur on planets

    // DSO practical limits
    static constexpr double EXP_DSO_MIN_MS    = 1000.0;   // 1 second minimum sub
    static constexpr double EXP_DSO_MAX_MS    = 300000.0; // 5 minute maximum sub

    // Other controls
    static constexpr int GAMMA_MIN    = 1;
    static constexpr int GAMMA_MAX    = 100;
    static constexpr int WB_MIN       = 0;
    static constexpr int WB_MAX       = 100;
    static constexpr int USB_MIN      = 40;
    static constexpr int USB_MAX      = 100;

    // EAF
    static constexpr int EAF_MIN      = 0;
    static constexpr int EAF_MAX      = 600000;
    static constexpr int EAF_SAFE_DEFAULT = 50000; // safe indoor test position

    // Clamp helpers
    static int ClampGain(int g)    { return g < GAIN_MIN ? GAIN_MIN : g > GAIN_MAX ? GAIN_MAX : g; }
    static int ClampGamma(int g)   { return g < GAMMA_MIN ? GAMMA_MIN : g > GAMMA_MAX ? GAMMA_MAX : g; }
    static int ClampWB(int w)      { return w < WB_MIN ? WB_MIN : w > WB_MAX ? WB_MAX : w; }
    static int ClampUSB(int u)     { return u < USB_MIN ? USB_MIN : u > USB_MAX ? USB_MAX : u; }
    static double ClampExp(double e) { return e < EXP_MIN_MS ? EXP_MIN_MS : e > EXP_MAX_MS ? EXP_MAX_MS : e; }
    static int ClampEAF(int p)     { return p < EAF_MIN ? EAF_MIN : p > EAF_MAX ? EAF_MAX : p; }
    static bool IsHCG(int gain)    { return gain >= GAIN_HCG_THRESHOLD; }
};

struct TargetProfile {
    // ── Identity ───────────────────────────────────────────────
    std::string name;
    std::string filename;
    std::string targetType;  // "planet", "moon", "dso", "star"

    // ── Camera ─────────────────────────────────────────────────
    int         gain         = 200;   // >= 182 for HCG mode (0.8e read noise)
    double      exposureMs   = 8.0;
    int         gamma        = 50;
    int         wbRed        = 52;    // sensor neutral
    int         wbBlue       = 47;    // sensor neutral
    int         usbBandwidth = 80;
    bool        highSpeed    = true;

    // ── ROI ────────────────────────────────────────────────────
    int         roiWidth     = 1280;
    int         roiHeight    = 960;
    int         binning      = 1;
    ASI_IMG_TYPE imgType     = ASI_IMG_RAW8;

    // ── Focuser ────────────────────────────────────────────────
    int         focusPosition = CameraRails::EAF_SAFE_DEFAULT;
    int         stepCoarse    = 100;
    int         stepFine      = 10;

    // ── Mount ──────────────────────────────────────────────────
    std::string trackingMode  = "alt_az";
    int         slewRate      = 9;
    std::string gotoTarget;

    // ── Capture ────────────────────────────────────────────────
    std::string captureFormat = "SER";
    std::string limitType     = "frames";
    int         limitValue    = 3000;
    std::string filePrefix;

    // ── Notes ──────────────────────────────────────────────────
    std::string notes;
    std::string histogram_target; // e.g. "50-70% peak"
};

// ── Research-calibrated default profiles ──────────────────────
//
// All planet settings target:
//   • Gain >= 182 (HCG mode, 0.8e read noise)
//   • 50-70% histogram peak (avoid clipping)
//   • Shortest exposure that hits histogram target (freeze seeing)
//   • ROI cropped to planet size + margin (boost FPS)
//
// Sources:
//   • Cloudy Nights: C8 + ASI678MC real-world reports
//   • ZWO documentation: HCG threshold, gain curve
//   • FloP C8 session: Jupiter 7.5ms, Saturn 10ms
// ─────────────────────────────────────────────────────────────

inline TargetProfile DefaultJupiter() {
    TargetProfile p;
    p.name        = "Jupiter";
    p.filename    = "jupiter.ini";
    p.targetType  = "planet";
    // Jupiter is bright → moderate gain in HCG range, short exposure
    // Real C8+678MC users: gain 180-220, 5-8ms, 60% histogram
    p.gain        = 200;       // HCG active, 0.8e read noise
    p.exposureMs  = 7.5;       // ~5-8ms range for 8SE f/10
    p.gamma       = 50;
    p.wbRed       = 52;        // neutral — Jupiter has warm tones, adjust live
    p.wbBlue      = 47;
    p.usbBandwidth = 80;
    p.highSpeed   = true;
    // ROI: Jupiter subtends ~35-45" → at 0.203"/px ≈ 175-220px disk
    // 640×480 gives margin and pushes FPS > 100
    p.roiWidth    = 640;
    p.roiHeight   = 480;
    p.binning     = 1;
    p.imgType     = ASI_IMG_RAW8;
    p.limitValue  = 5000;      // ~60s at 80fps — plenty for lucky imaging
    p.limitType   = "frames";
    p.filePrefix  = "Jupiter_";
    p.histogram_target = "50-70% peak";
    p.notes = "HCG active (gain 200). ROI 640x480 ~80-100fps. "
              "Adjust gain until histogram peaks 50-70%. "
              "UV/IR cut filter recommended. ADC if alt < 40 deg. "
              "Stack 30% best frames in AutoStakkert.";
    return p;
}

inline TargetProfile DefaultSaturn() {
    TargetProfile p;
    p.name        = "Saturn";
    p.filename    = "saturn.ini";
    p.targetType  = "planet";
    // Saturn is dimmer than Jupiter → higher gain, longer exposure
    // Real C8+678MC: gain 270-300, 10-15ms
    p.gain        = 280;       // HCG, higher gain for dimmer target
    p.exposureMs  = 12.0;      // longer exposure for dimmer planet
    p.gamma       = 50;
    p.wbRed       = 54;        // slightly warm — Saturn has golden tone
    p.wbBlue      = 44;
    p.usbBandwidth = 80;
    p.highSpeed   = true;
    // Saturn disk ~16-18" + rings to ~42" → 640×480 still fits
    p.roiWidth    = 640;
    p.roiHeight   = 480;
    p.binning     = 1;
    p.imgType     = ASI_IMG_RAW8;
    p.limitValue  = 5000;
    p.limitType   = "frames";
    p.filePrefix  = "Saturn_";
    p.histogram_target = "50-70% peak";
    p.notes = "HCG active (gain 280). Dimmer than Jupiter — increase gain "
              "if histogram too low. Rings require careful WB. "
              "ADC strongly recommended at any altitude. "
              "IR685 filter can improve detail.";
    return p;
}

inline TargetProfile DefaultMars() {
    TargetProfile p;
    p.name        = "Mars";
    p.filename    = "mars.ini";
    p.targetType  = "planet";
    // Mars brightness varies hugely — these are near-opposition settings
    // Small disk (7-25" depending on opposition) → tight ROI
    p.gain        = 220;       // HCG, moderate — Mars can be bright near opposition
    p.exposureMs  = 6.0;
    p.gamma       = 55;        // slight boost for surface contrast
    p.wbRed       = 60;        // warm WB — Mars is very red
    p.wbBlue      = 38;
    p.usbBandwidth = 80;
    p.highSpeed   = true;
    p.roiWidth    = 640;
    p.roiHeight   = 480;
    p.binning     = 1;
    p.imgType     = ASI_IMG_RAW8;
    p.limitValue  = 5000;
    p.limitType   = "frames";
    p.filePrefix  = "Mars_";
    p.histogram_target = "50-70% peak";
    p.notes = "WB shifted warm for Mars red coloring. "
              "Best near opposition when disk > 15\". "
              "Reduce gain when near opposition (bright). "
              "IR610 or orange filter can boost surface contrast.";
    return p;
}

inline TargetProfile DefaultVenus() {
    TargetProfile p;
    p.name        = "Venus";
    p.filename    = "venus.ini";
    p.targetType  = "planet";
    // Venus is VERY bright — low gain even with HCG consideration
    // Twilight target — often imaged before dark
    p.gain        = 80;        // below HCG intentionally — Venus too bright otherwise
    p.exposureMs  = 3.0;       // very short — Venus is brilliant
    p.gamma       = 45;
    p.wbRed       = 50;
    p.wbBlue      = 50;        // neutral WB — featureless in visible light
    p.usbBandwidth = 80;
    p.highSpeed   = true;
    p.roiWidth    = 640;
    p.roiHeight   = 480;
    p.binning     = 1;
    p.imgType     = ASI_IMG_RAW8;
    p.limitValue  = 3000;
    p.limitType   = "frames";
    p.filePrefix  = "Venus_";
    p.histogram_target = "40-60% peak";
    p.notes = "Very bright — keep gain LOW. UV filter (350nm) reveals "
              "cloud structure invisible in white light. "
              "Phase is striking — crescent at inferior conjunction. "
              "Best imaged at dusk/dawn when sky not fully dark.";
    return p;
}

inline TargetProfile DefaultMoon() {
    TargetProfile p;
    p.name        = "Moon";
    p.filename    = "moon.ini";
    p.targetType  = "moon";
    // Moon is extremely bright — minimal gain, very short exposure
    // Full sensor for mosaic work, or crop for detail shots
    p.gain        = 0;         // minimum gain — Moon is brilliant
    p.exposureMs  = 2.0;
    p.gamma       = 50;
    p.wbRed       = 52;
    p.wbBlue      = 47;
    p.usbBandwidth = 80;
    p.highSpeed   = true;
    // Full sensor for context; crop to crater of interest for detail
    p.roiWidth    = 1920;
    p.roiHeight   = 1080;
    p.binning     = 1;
    p.imgType     = ASI_IMG_RAW8;
    p.limitValue  = 500;
    p.limitType   = "frames";
    p.filePrefix  = "Moon_";
    p.histogram_target = "40-60% peak";
    p.notes = "Gain=0, very short exposure. Moon fills sensor at f/10 — "
              "use 1920x1080 ROI for full disk at good FPS, or crop "
              "to 640x480 for crater detail at 100+ fps. "
              "Best at quarter phase — terminator shows crater relief.";
    return p;
}

inline TargetProfile DefaultDSO() {
    TargetProfile p;
    p.name        = "DSO";
    p.filename    = "dso.ini";
    p.targetType  = "dso";
    // DSO: completely different regime — long subs, full sensor, RAW16
    // HCG is active at gain 182+ — use high gain for low read noise
    p.gain        = 300;       // HCG well into deep gain — 0.8e read noise
    p.exposureMs  = 60000.0;   // 60 second subs — start here, adjust to sky
    p.gamma       = 50;
    p.wbRed       = 52;
    p.wbBlue      = 47;
    p.usbBandwidth = 80;
    p.highSpeed   = false;     // no need for high speed — long subs
    p.roiWidth    = 3840;
    p.roiHeight   = 2160;      // full sensor for maximum field
    p.binning     = 1;
    p.imgType     = ASI_IMG_RAW16;  // 16-bit for stacking headroom
    p.limitType   = "time";
    p.limitValue  = 1800;      // 30 min session = many 60s subs
    p.filePrefix  = "DSO_";
    p.histogram_target = "10-30% peak (faint DSO)";
    p.notes = "RAW16 for stacking. Full sensor 3840x2160. "
              "HCG active at gain 300 (0.8e read noise). "
              "60s subs at Bortle 4 Anthem is a good starting point. "
              "Dark frames at same gain/temp improve stack quality. "
              "EQ mount strongly recommended for subs > 30s.";
    return p;
}

inline TargetProfile DefaultStar() {
    TargetProfile p;
    p.name        = "Star";
    p.filename    = "star.ini";
    p.targetType  = "star";
    // Focusing aid / alignment star profile — short exposure, bright target
    p.gain        = 150;
    p.exposureMs  = 1.0;       // very short — stars are points
    p.gamma       = 50;
    p.wbRed       = 52;
    p.wbBlue      = 47;
    p.usbBandwidth = 80;
    p.highSpeed   = true;
    p.roiWidth    = 640;
    p.roiHeight   = 480;
    p.binning     = 1;
    p.imgType     = ASI_IMG_RAW8;
    p.limitValue  = 500;
    p.limitType   = "frames";
    p.filePrefix  = "Star_";
    p.histogram_target = "20-40% peak";
    p.notes = "Use for focusing (EAF V-curve) and plate-solve alignment. "
              "Short exposure so star is unsaturated. "
              "Bahtinov mask on OTA improves focus precision dramatically.";
    return p;
}

inline TargetProfile DefaultPlanetFocus() {
    TargetProfile p;
    p.name        = "Planet Focus";
    p.filename    = "planet_focus.ini";
    p.targetType  = "star";
    // Full sensor so Jupiter fills the screen — easier to see and focus
    // Low gain so bright disk doesn't saturate and hide diffraction rings
    // Very short exposure to freeze atmospheric wobble while focusing
    p.gain        = 80;        // below HCG intentionally — bright target, need detail
    p.exposureMs  = 4.0;       // short enough to see rings, long enough for brightness
    p.gamma       = 60;        // slight boost so disk edge is clearly visible
    p.wbRed       = 52;
    p.wbBlue      = 47;
    p.usbBandwidth = 80;
    p.highSpeed   = true;
    // Full sensor — maximum context, see the whole disk with room around it
    // Makes it much easier to centre and judge focus quality
    p.roiWidth    = 1920;
    p.roiHeight   = 1080;
    p.binning     = 1;
    p.imgType     = ASI_IMG_RAW8;
    p.limitValue  = 0;         // no capture limit — focus mode only
    p.limitType   = "frames";
    p.filePrefix  = "Focus_";
    p.histogram_target = "40-60% peak — if clipping reduce gain";
    p.notes = "FOCUSING MODE — full sensor so disk is large and easy to see. "
              "Low gain prevents saturation hiding diffraction rings. "
              "Rack EAF until Jupiter is a large soft blob, then slowly "
              "bring in until equatorial bands just appear. "
              "Switch to Jupiter profile once sharp. "
              "Tip: if totally lost, defocus until you see a large donut shape.";
    return p;
}

} // namespace raduk::profiles
