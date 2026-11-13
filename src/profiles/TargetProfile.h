#pragma once
#include <algorithm>
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
#include <cstdio>
#include "../core/PixelFormat.h"   // phase 149.0b: no vendor header here any more

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

    // ── Phase 100: the rails finally do something ──────────────
    //
    // Phase 99's sweep found all four of the practical limits above
    // read by nothing, so a planetary profile could be saved at 500 ms
    // and the application would not say a word. The owner's call was
    // to enforce them.
    //
    // Enforcing them turned up how many copies of the planet ceiling
    // there were. FOUR values for one concept:
    //
    //     EXP_PLANET_MAX_MS                       50   (this file)
    //     AutoExposureAssistant::Config default   50
    //     main.cpp's aeCfg.expMaxMs               40   <-- the live one
    //     a comment in ControlPanel.cpp            50
    //
    // So the documented figure was contradicted by the running code,
    // and the slider's own range comment cited the wrong one. This
    // function is now the single source: the assistant derives its
    // bounds from it per target type, and nothing else states them.
    //
    // ── Why the rails are ADVISORY for a human and HARD for the
    //    controller ───────────────────────────────────────────
    //
    // They are practical limits with physical reasons, not hardware
    // bounds -- EXP_MIN_MS and EXP_MAX_MS are those, and they are
    // separately enforced. A 500 ms planetary exposure is almost
    // certainly a mistake and just possibly a deliberate one on a very
    // dim target, so the honest response to a human is to say so, not
    // to overrule them.
    //
    // The auto-exposure assistant is a different matter. It has no
    // judgement to respect, so for it the rails are a clamp.
    struct ExposureRail {
        double      minMs = EXP_MIN_MS;
        double      maxMs = EXP_MAX_MS;
        const char* why   = "";
        bool        advisory = false;   // false = hardware bounds only
        // Phase 112: the auto-exposure ladder, LightLadder.h. A human
        // is not held to these. Starting figures from the stock
        // profiles, UNVERIFIED ON SKY.
        double      expKneeMs = EXP_MAX_MS;
        int         gainFloor = GAIN_MIN;
        int         gainKnee  = GAIN_MAX;
        int         gainMax   = GAIN_MAX;
    };

    // targetType is one of "planet", "moon", "dso", "star" -- a field
    // every stock profile sets and which, until this phase, NOTHING
    // read. It was the fifth dead thing the sweep turned up and it is
    // exactly what choosing a rail needs.
    static ExposureRail RailsFor(const std::string& targetType) {
        ExposureRail r;
        if (targetType == "dso") {
            r.minMs = EXP_DSO_MIN_MS; r.maxMs = EXP_DSO_MAX_MS;
            r.why = "deep sky: under a second is not a sub, over five "
                    "minutes is tracking luck";
            r.advisory = true;
            r.expKneeMs = 60000.0; r.gainKnee = 300;
        } else if (targetType == "planet" || targetType == "moon" ||
                   targetType == "star") {
            // A star profile is a focusing or metrics profile and lives
            // at the same short exposures as a planet, which is why it
            // shares the rail rather than having a third one.
            r.minMs = EXP_PLANET_MIN_MS; r.maxMs = EXP_PLANET_MAX_MS;
            r.why = "lucky imaging: under a millisecond oversamples the "
                    "seeing, over fifty blurs the rotation";
            r.advisory = true;
            r.expKneeMs = 20.0; r.gainKnee = 350;
            // A planet wants HCG. The Moon and the focus profiles sit
            // below it on purpose, and a floor would snap them up.
            if (targetType == "planet") r.gainFloor = GAIN_HCG_THRESHOLD;
        }
        // Anything else gets the hardware bounds and no opinion: an
        // unrecognised type is not licence to invent a limit for it.
        return r;
    }


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

    // ── Phase 100: the rail this profile is held to ────────────
    //
    // Empty when the exposure is inside its rail. A sentence naming
    // the figure, the rail and the reason otherwise -- said once,
    // where the profile is applied, rather than as a dialog to
    // dismiss.
    //
    // ADVISORY for a human, and that is deliberate: see
    // CameraRails::RailsFor. The auto-exposure assistant gets the same
    // rail as a hard clamp, because it has no judgement to respect.
    std::string ExposureWarning() const {
        const CameraRails::ExposureRail r =
            CameraRails::RailsFor(targetType);
        if (!r.advisory) return "";
        if (exposureMs >= r.minMs && exposureMs <= r.maxMs) return "";
        char b[320];
        snprintf(b, sizeof(b),
                 "%s: %.3g ms is outside the %.3g-%.3g ms range for a "
                 "'%s' target (%s). Applied as asked.",
                 name.c_str(), exposureMs, r.minMs, r.maxMs,
                 targetType.c_str(), r.why);
        return b;
    }

    // ── Phase 140.2: the exposure band ──────────────────────────
    //
    // Per target, the exposure auto-exposure keeps inside: it lengthens
    // the exposure up to bandCeilMs at the HCG gain floor before it adds
    // gain, and to dim it lowers gain first and shortens the exposure no
    // further than bandFloorMs. Gain amplifies; only exposure collects
    // light, so a frame made bright by gain at a fraction of a
    // millisecond is a noisy frame. The owner's audit (140.0) found
    // Saturn recorded at 0.1 to 2 ms with peaks at 19 to 42% of full
    // scale. The ceiling keeps the seeing frozen; the floor keeps the
    // signal.
    //
    // 0 = no band (the type's rail, as before). The defaults below are
    // STARTING POINTS from amateur practice for an 8-inch at f/10, not
    // measurements; clear-night item 55 (the exposure ladder) is how to
    // set them, in the profile's .ini under [exposure_band].
    double      bandFloorMs  = 0.0;
    double      bandCeilMs   = 0.0;
    bool HasBand() const { return bandFloorMs > 0.0 && bandCeilMs > bandFloorMs; }

    // The rail auto-exposure is held to: the type's rail, narrowed to the
    // band when there is one, with the gain knee at the floor so exposure
    // takes all the light up to the ceiling before gain moves.
    CameraRails::ExposureRail AERail() const {
        CameraRails::ExposureRail r = CameraRails::RailsFor(targetType);
        if (!HasBand()) return r;
        r.minMs     = (std::max)(r.minMs, bandFloorMs);
        r.maxMs     = (std::min)(r.maxMs, bandCeilMs);
        if (r.maxMs <= r.minMs) return CameraRails::RailsFor(targetType);   // a band outside the rail
        r.expKneeMs = r.maxMs;
        r.gainKnee  = r.gainFloor;
        return r;
    }

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
    raduk::core::ImageType imgType = raduk::core::ImageType::Raw8;   // phase 149.0b
    // The file format version this profile was read from (phase 149.0b):
    // 1 means it still carries ZWO's integer and is rewritten at
    // version 2 on its next save. Not saved itself.
    int         formatVersionRead = raduk::core::kProfileFormatVersion;

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

// ── May a SAVE replace a profile's stored note? ────────────────
//
// SAVE reads the whole rig, but the NOTES box is not part of the rig:
// it holds a note for ONE profile -- the one it was populated from --
// and it reads empty whenever nothing has been loaded into it. So an
// unconditional write hands an empty string to whichever profile the
// combo happens to name, and the note on disk is gone. saturn.ini lost
//
//   "Longer exposure than Jupiter. Rings add contrast challenge."
//
// exactly that way, after a night spent on Jupiter, and it had to be
// typed back in by hand.
//
// A write is allowed when:
//   • the box has something to say -- a note was typed, so record it;
//   • the box belongs to this profile -- its own note is on screen, so
//     an empty box means the user emptied it, and a deliberate clear
//     must still work;
//   • there is nothing on disk to lose.
//
// boxProfile and targetProfile are profile NAMES, not indices: the
// EDIT dialog can rename and reorder profiles under us, and a stale
// index would silently point at a different profile's note. An unknown
// or mismatched owner fails safe -- the stored note stays.
inline bool NotesBoxMayReplaceStored(const std::string& boxText,
                                     const std::string& boxProfile,
                                     const std::string& targetProfile,
                                     const std::string& storedNotes) {
    if (!boxText.empty())                  return true;
    if (storedNotes.empty())               return true;
    return !boxProfile.empty() && boxProfile == targetProfile;
}

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
    p.imgType     = raduk::core::ImageType::Raw8;
    p.limitValue  = 5000;      // ~60s at 80fps — plenty for lucky imaging
    p.limitType   = "frames";
    p.filePrefix  = "Jupiter_";
    p.histogram_target = "50-70% peak";
    p.notes = "HCG active (gain 200). ROI 640x480 ~80-100fps. "
              "Adjust gain until histogram peaks 50-70%. "
              "UV/IR cut filter recommended. ADC if alt < 40 deg. "
              "Stack 30% best frames in AutoStakkert.";
    p.bandFloorMs = 4.0;      // 140.2 starting band, to be set by clear-night item 55
    p.bandCeilMs  = 12.0;
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
    p.imgType     = raduk::core::ImageType::Raw8;
    p.limitValue  = 5000;
    p.limitType   = "frames";
    p.filePrefix  = "Saturn_";
    p.histogram_target = "50-70% peak";
    p.notes = "HCG active (gain 280). Dimmer than Jupiter — increase gain "
              "if histogram too low. Rings require careful WB. "
              "ADC strongly recommended at any altitude. "
              "IR685 filter can improve detail.";
    p.bandFloorMs = 8.0;      // 140.2 starting band, to be set by clear-night item 55
    p.bandCeilMs  = 25.0;
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
    p.imgType     = raduk::core::ImageType::Raw8;
    p.limitValue  = 5000;
    p.limitType   = "frames";
    p.filePrefix  = "Mars_";
    p.histogram_target = "50-70% peak";
    p.notes = "WB shifted warm for Mars red coloring. "
              "Best near opposition when disk > 15\". "
              "Reduce gain when near opposition (bright). "
              "IR610 or orange filter can boost surface contrast.";
    p.bandFloorMs = 3.0;      // 140.2 starting band, to be set by clear-night item 55
    p.bandCeilMs  = 10.0;
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
    p.imgType     = raduk::core::ImageType::Raw8;
    p.limitValue  = 3000;
    p.limitType   = "frames";
    p.filePrefix  = "Venus_";
    p.histogram_target = "40-60% peak";
    p.notes = "Very bright — keep gain LOW. UV filter (350nm) reveals "
              "cloud structure invisible in white light. "
              "Phase is striking — crescent at inferior conjunction. "
              "Best imaged at dusk/dawn when sky not fully dark.";
    p.bandFloorMs = 1.0;      // 140.2 starting band, to be set by clear-night item 55
    p.bandCeilMs  = 5.0;
    return p;
}

// 2026-11-08: the profile list stopped at Venus, so Mercury, Uranus and
// Neptune captured on whatever profile happened to be loaded (Uranus that
// night ran on Saturn's settings). Starting points, to be tuned and saved
// at the scope like the others.
inline TargetProfile DefaultMercury() {
    TargetProfile p;
    p.name        = "Mercury";
    p.filename    = "mercury.ini";
    p.targetType  = "planet";
    // Bright, small, and always low near the Sun: short exposures and a
    // small box for frame rate against poor low-altitude seeing.
    p.gain        = 150;
    p.exposureMs  = 2.0;
    p.gamma       = 50;
    p.wbRed       = 52;
    p.wbBlue      = 47;
    p.usbBandwidth = 90;
    p.highSpeed   = true;
    p.roiWidth    = 640;
    p.roiHeight   = 480;
    p.binning     = 1;
    p.imgType     = raduk::core::ImageType::Raw8;
    p.focusPosition = 168800;
    p.limitValue  = 3000;
    p.limitType   = "frames";
    p.filePrefix  = "Mercury_";
    p.notes = "Starting point. Always low near the Sun: short exposures, "
              "small box, expect poor seeing. A red or IR filter helps.";
    p.bandFloorMs = 1.0;      // 140.2 starting band, to be set by clear-night item 55
    p.bandCeilMs  = 8.0;
    return p;
}

inline TargetProfile DefaultUranus() {
    TargetProfile p;
    p.name        = "Uranus";
    p.filename    = "uranus.ini";
    p.targetType  = "planet";
    // About 3.7" -- ~18 px at this plate scale: a small, dim disc.
    p.gain        = 400;
    p.exposureMs  = 15.0;
    p.gamma       = 50;
    p.wbRed       = 52;
    p.wbBlue      = 50;
    p.usbBandwidth = 90;
    p.highSpeed   = true;
    p.roiWidth    = 640;
    p.roiHeight   = 480;
    p.binning     = 1;
    p.imgType     = raduk::core::ImageType::Raw8;
    p.focusPosition = 168800;
    p.limitValue  = 2000;
    p.limitType   = "frames";
    p.filePrefix  = "Uranus_";
    p.notes = "Starting point. About 3.7\" (~18 px): high gain, longer "
              "exposures than Saturn. Shows as a pale blue-green disc.";
    p.bandFloorMs = 10.0;      // 140.2 starting band, to be set by clear-night item 55
    p.bandCeilMs  = 50.0;
    return p;
}

inline TargetProfile DefaultNeptune() {
    TargetProfile p;
    p.name        = "Neptune";
    p.filename    = "neptune.ini";
    p.targetType  = "planet";
    // About 2.3" -- ~11 px: the dimmest planet.
    p.gain        = 450;
    p.exposureMs  = 30.0;
    p.gamma       = 50;
    p.wbRed       = 50;
    p.wbBlue      = 52;
    p.usbBandwidth = 90;
    p.highSpeed   = true;
    p.roiWidth    = 640;
    p.roiHeight   = 480;
    p.binning     = 1;
    p.imgType     = raduk::core::ImageType::Raw8;
    p.focusPosition = 168800;
    p.limitValue  = 1500;
    p.limitType   = "frames";
    p.filePrefix  = "Neptune_";
    p.notes = "Starting point. About 2.3\" (~11 px): the highest gain and "
              "longest exposures of the planets.";
    p.bandFloorMs = 15.0;      // 140.2 starting band, to be set by clear-night item 55
    p.bandCeilMs  = 50.0;
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
    p.imgType     = raduk::core::ImageType::Raw8;
    p.limitValue  = 500;
    p.limitType   = "frames";
    p.filePrefix  = "Moon_";
    p.histogram_target = "40-60% peak";
    p.notes = "Gain=0, very short exposure. Moon fills sensor at f/10 — "
              "use 1920x1080 ROI for full disk at good FPS, or crop "
              "to 640x480 for crater detail at 100+ fps. "
              "Best at quarter phase — terminator shows crater relief.";
    p.bandFloorMs = 1.0;      // 140.2 starting band, to be set by clear-night item 55
    p.bandCeilMs  = 10.0;
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
    p.roiWidth    = 0;         // 0 = the full sensor, whatever the camera
    p.roiHeight   = 0;         // (phase 149.0)
    p.binning     = 1;
    p.imgType     = raduk::core::ImageType::Raw16;  // 16-bit for stacking headroom
    p.limitType   = "time";
    p.limitValue  = 1800;      // 30 min session = many 60s subs
    p.filePrefix  = "DSO_";
    p.histogram_target = "10-30% peak (faint DSO)";
    p.notes = "RAW16 for stacking. Full sensor. "
              "HCG active at gain 300 (0.8e read noise). "
              "60s subs at a Bortle 4 site is a good starting point. "
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
    p.imgType     = raduk::core::ImageType::Raw8;
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
    p.imgType     = raduk::core::ImageType::Raw8;
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
