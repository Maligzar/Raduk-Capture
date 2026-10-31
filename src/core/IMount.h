#pragma once
// ============================================================
// IMount.h — Abstract mount interface for RadukCapture
//
// Both ASCOMMount and NexStarMount implement this interface.
// The rest of the codebase programs against IMount* only —
// swapping the backend is a single-line change in DeviceFactory.
//
// PulseGuide is defined here (Phase 5) — implemented by
// ASCOMMount via ITelescope.PulseGuide(), stubbed by emulator.
// ============================================================

#include <string>

namespace raduk::core {

// Guide directions — match ASCOM ITelescopeV3 GuideDirections enum
enum class GuideDirection {
    North = 0,
    South = 1,
    East  = 2,
    West  = 3
};

class IMount {
public:
    virtual ~IMount() = default;

    // ── Connection ─────────────────────────────────────────────
    // For ASCOM: progID is the driver ProgID (e.g. "CPWI.Telescope")
    // For NexStar: progID is the COM port (e.g. "COM3")
    virtual bool Connect(const std::string& progID, int baudRate = 9600) = 0;
    virtual void Disconnect() = 0;
    virtual bool IsConnected() const = 0;

    // ── Position readout ───────────────────────────────────────
    virtual bool GetRADec(double& raHours, double& decDegrees) = 0;
    virtual bool GetAltAz(double& altDegrees, double& azDegrees) = 0;

    // ── GoTo ───────────────────────────────────────────────────
    virtual bool GoToRADec(double raHours, double decDegrees) = 0;
    virtual bool IsSlewing() = 0;
    virtual void CancelGoTo() = 0;

    // ── Tracking ───────────────────────────────────────────────
    enum TrackingMode { OFF = 0, ALT_AZ = 1, EQ_NORTH = 2, EQ_SOUTH = 3 };
    virtual bool         SetTrackingMode(TrackingMode mode) = 0;
    virtual TrackingMode GetTrackingMode() = 0;
    // Phase 69: false when the backend could not read the mode and is
    // therefore guessing. Virtual with a true default because the
    // other backends always know their own state -- only the serial
    // NexStar path can fail to obtain an answer, and it used to report
    // that failure as OFF, which is a different claim entirely.
    // Virtual rather than a downcast: a static_cast to NexStarMount
    // here previously corrupted ASCOMMount instances.
    virtual bool TrackingModeKnown() const { return true; }

    // ── Tracking RATE, which is a different thing ──────────────
    //
    // Phase 97. TrackingMode above is the mount's FRAME -- alt-az or
    // equatorial. The rate is how fast it turns, and the application
    // has never had an abstraction for it: ASCOMMount wrote the
    // literal 0 at two sites with the comment
    // "DriveRates: 0=Sidereal, 1=Lunar, 2=Solar, 3=King" beside it,
    // so it knew the alternatives existed and pinned sidereal anyway.
    //
    // ── What that costs, measured ──────────────────────────────
    //
    // The Moon moves 13.176 deg/day east against the stars, which a
    // sidereal mount loses at 0.55 arcsec/s, or 2.7 px/s on this rig.
    // The answer depends entirely on the timescale, and conflating
    // the three is how people come to believe sidereal blurs lunar
    // frames:
    //
    //   per EXPOSURE  20 ms -> 0.05 px.  Nothing.
    //   per RUN       60 s  -> 163 px, 4% of the frame. The stacker
    //                          removes it.
    //   per MOSAIC    8 min -> 277 arcsec against a 156-arcsec
    //                          overlap at 20%. The drift exceeds the
    //                          WHOLE overlap by 1.8x, so a mosaic in
    //                          fixed offsets does not close.
    //
    // So this matters for mosaics and barely at all otherwise, which
    // is why it arrives with MosaicPlan.h rather than on its own.
    //
    // King rate is deliberately absent. It corrects for refraction on
    // a long sidereal exposure, which is a deep-sky concern; this is a
    // lucky-imaging rig whose runs are seconds long.
    enum TrackingRate { RATE_SIDEREAL = 0, RATE_LUNAR = 1, RATE_SOLAR = 2 };

    // Returns false when the backend cannot set a rate, so a caller
    // can say so rather than believe it worked. The default is that
    // false: a backend opts in, exactly as SlewVariable does.
    virtual bool SetTrackingRate(TrackingRate /*rate*/) { return false; }
    virtual bool SupportsTrackingRate() const { return false; }

    // The rate for a target, by name. Here rather than in each caller
    // because "is this the Moon" is a decision that must come out the
    // same everywhere.
    //
    // Matched on a WORD, not on equality. The catalogue calls them
    // exactly "Moon" and "Sun", but this is also fed PROFILE names,
    // which the owner chooses -- and a lunar session is far more
    // likely to be filed as "Moon - Copernicus" or "Moon Tycho" than
    // as the bare word. Requiring equality would hand every one of
    // those sidereal rate without a murmur, which is precisely the
    // silent near-miss this phase spent its time hunting.
    //
    // The separator requirement is what keeps it honest: "Moonrise"
    // and "Sunset" are not the Moon and the Sun, and a bare prefix
    // test would claim they were.
    static TrackingRate RateForTarget(const char* name) {
        if (!name) return RATE_SIDEREAL;
        auto lower = [](char c) {
            return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        };
        // True when `name` begins with `word` and what follows is
        // either nothing or a separator.
        auto leads = [&](const char* word) {
            const char* a = name;
            for (; *word; ++a, ++word)
                if (!*a || lower(*a) != lower(*word)) return false;
            return *a == '\0' || *a == ' ' || *a == '-' || *a == '_' ||
                   *a == ':'  || *a == ',' || *a == '.' || *a == '\t';
        };
        if (leads("Moon")) return RATE_LUNAR;
        if (leads("Sun"))  return RATE_SOLAR;
        return RATE_SIDEREAL;
    }

    // ── Jog slewing ────────────────────────────────────────────
    virtual bool SlewFixed(int axis, int direction, int rate) = 0;
    virtual void StopSlew(int axis) = 0;

    // Slew at an arbitrary rate, in arcseconds per second.
    //
    // SlewFixed above takes one of nine presets, and on this mount the
    // top of that scale is brutal: roughly 0.27 deg/s at rate 6, then
    // 0.5, then 1, then 4. The bottom is no better for framing -- the
    // slowest preset is one times sidereal, and final centring at high
    // magnification wants a fraction of that.
    //
    // NexStar has always had the command for it. Nothing in this app
    // used it, so the jog has been a nine-position switch, and an
    // analogue stick plugged into a nine-position switch is still a
    // nine-position switch (P4.1).
    //
    // Returns false when the backend cannot do it, so a caller can
    // fall back to SlewFixed rather than silently not moving. The
    // default is that false: a backend has to opt in.
    virtual bool SlewVariable(int /*axis*/, int /*direction*/,
                              double /*arcsecPerSec*/) { return false; }
    virtual bool SupportsVariableRate() const { return false; }

    // ── Sync ───────────────────────────────────────────────────
    virtual bool SyncRADec(double raHours, double decDegrees) = 0;

    // ── Alignment ──────────────────────────────────────────────
    virtual bool IsAligned() = 0;

    // ── Guide pulses (Phase 5 centroid tracking) ───────────────
    // Send a correction pulse in the given direction for durationMs milliseconds.
    // ASCOMMount: maps to ITelescope.PulseGuide() (returns immediately;
    //             check IsPulseGuiding() before sending the next pulse).
    // NexStarMount: software-timed slew — BLOCKS the calling thread for
    //               durationMs. Only call from the correction thread.
    // Emulator: instantaneous position nudge.
    virtual bool PulseGuide(GuideDirection dir, int durationMs) = 0;
    virtual bool IsPulseGuiding() = 0;

    // ── SAFETY: watchdog heartbeat ─────────────────────────────
    // Called by the main render loop every frame. NexStarMount uses it to
    // feed its runaway-mount watchdog; other backends ignore it. Virtual so
    // callers never need to downcast (a static_cast to NexStarMount here
    // previously corrupted ASCOMMount instances on the CPWI path).
    virtual void Heartbeat() {}

    // ── Info ───────────────────────────────────────────────────
    virtual bool GetVersion(int& major, int& minor) = 0;
    virtual const std::string& ConnectionId() const = 0;

    // ── Static helpers (same for all implementations) ──────────
    static std::string FormatRA(double raHours) {
        if (raHours < 0) raHours += 24.0;
        int h = (int)raHours;
        int m = (int)((raHours - h) * 60.0);
        int s = (int)(((raHours - h) * 60.0 - m) * 60.0);
        char buf[32];
        sprintf_s(buf, "%02dh %02dm %02ds", h, m, s);
        return std::string(buf);
    }

    static std::string FormatDec(double decDegrees) {
        char sign = (decDegrees >= 0) ? '+' : '-';
        double abs_dec = (decDegrees < 0) ? -decDegrees : decDegrees;
        int d = (int)abs_dec;
        int m = (int)((abs_dec - d) * 60.0);
        int s = (int)(((abs_dec - d) * 60.0 - m) * 60.0);
        char buf[32];
        sprintf_s(buf, "%c%02d\xB0 %02d' %02d\"", sign, d, m, s);
        return std::string(buf);
    }

    static const char* TrackingModeName(TrackingMode mode) {
        switch (mode) {
            case OFF:      return "OFF";
            case ALT_AZ:   return "ALT-AZ";
            case EQ_NORTH: return "EQ-NORTH";
            case EQ_SOUTH: return "EQ-SOUTH";
            default:       return "UNKNOWN";
        }
    }
};


// ── The jog rate presets, in one place ────────────────────────
//
// What rate 1..9 actually means, in arcseconds per second. These
// numbers already existed inside MountEmulator and nowhere else, so
// the emulator knew what a rate meant and the rest of the app --
// including the code talking to the real mount -- did not. Now that
// a rate can be sent as a number rather than an index, there has to
// be one table and everything has to read it.
//
// 15.041 arcsec/s is one times sidereal.
inline double JogRateArcsecPerSec(int preset) {
    static const double kRate[10] = {
        0.0,        // 0 = stopped
        7.52,       // 1 = 0.5x sidereal
        15.04,      // 2 = 1x sidereal
        60.2,       // 3 = 4x
        120.3,      // 4 = 8x
        240.7,      // 5 = 16x     <- FINE
        481.3,      // 6 = 32x
        962.6,      // 7 = 64x     <- MED
        3600.0,     // 8 = 1 deg/s <- FAST
        10800.0     // 9 = 3 deg/s <- max slew
    };
    // A PRESET INDEX, 0-9, and never a signed rate. Direction is a
    // separate argument everywhere in this codebase, so a caller holding
    // a signed rate must pass its magnitude and apply the sign itself.
    //
    // Worth stating because the clamp below is a trap: a negative lands
    // on kRate[0], which is "stopped", so a caller that passes -9 gets
    // silence rather than an error. MountEmulator did exactly that and
    // every westward and southward jog on the bench did nothing.
    if (preset < 0) preset = 0;
    if (preset > 9) preset = 9;
    return kRate[preset];
}

} // namespace raduk::core
