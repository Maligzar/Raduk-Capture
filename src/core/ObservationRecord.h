#pragma once
// ============================================================
// ObservationRecord.h — Phase 48: what a capture was, written beside it
//
// "Go back to last night's Jupiter" needs to know where last night's
// Jupiter was, and the original plan was to plate-solve the first
// frame of the SER. That cannot work here: a planetary capture is a
// small crop around one bright disc, with no star field in it to
// solve. The design settled 2026-10-18 is the other way round -- the
// app already KNOWS where it was pointing, at what, with which
// settings, at the moment it records. It just never wrote it down.
//
// So every recording now leaves <name>_observation.txt beside
// <name>.ser and <name>_quality.txt. Same base name, same naming rule
// as the quality sidecar, same conventions: ASCII only, '#' comments.
// The quality file stays as it is -- per-frame scores, read by people
// and spreadsheets -- and this is the per-RUN record, read by programs.
//
// FORMAT. One "key = value" per line. Unknown values are OMITTED
// rather than written as 0, because a zero gain or a zero altitude is
// a real value and a consumer must be able to tell "not known" from
// it. Unknown keys are ignored on read, so a later version can add
// fields without breaking anything that reads this one. Keys are
// stable; their FITS equivalents are noted in the header the writer
// emits, because phase 18 (FITS output) and phase 19 (the stats
// dashboard) are the next two readers.
//
// This header is pure: no Windows, no camera, no mount. Building the
// record from the live rig happens in main.cpp; turning a record back
// into a mount move happens here, so the test can gate it.
// ============================================================

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <limits>
#include <string>

#include "GuideSolve.h"

namespace raduk::core {

constexpr int    kObservationFormatVersion = 1;
constexpr double kObsUnknown = std::numeric_limits<double>::quiet_NaN();

inline bool ObsKnown(double v) { return !std::isnan(v); }

struct ObservationRecord {
    int         formatVersion = kObservationFormatVersion;

    // What
    std::string file;          // the SER's file name, no folder
    std::string target;        // what the mount was last sent to; may be empty
    std::string profile;       // active camera profile; may be empty
    std::string frameType;     // Light / Dark / Flat / Bias
    std::string appVersion;

    // When -- UTC seconds since 1970. 0 = not known.
    int64_t     startUnix = 0;
    int64_t     endUnix   = 0;
    double      durationSec   = kObsUnknown;
    int         frames        = -1;
    int         framesDropped = -1;

    // Format. width/height ARE the ROI: the camera sends only the ROI.
    int         width    = 0;
    int         height   = 0;
    int         bitDepth = 0;
    int         bin      = 0;
    int         roiX     = -1;     // ROI start on the sensor, binned px
    int         roiY     = -1;
    std::string colour;            // "RGGB" or "MONO", as in the SER header

    // Exposure
    double      exposureMs = kObsUnknown;
    double      gain       = kObsUnknown;

    // Focuser
    int         focuserPos   = -1;
    double      focuserTempC = kObsUnknown;

    // Where the mount said it was pointing when the recording started.
    // RA/Dec is what recall slews back to; alt/az is only what it was.
    double      raHours   = kObsUnknown;
    double      decDeg    = kObsUnknown;
    double      altDeg    = kObsUnknown;
    double      azDeg     = kObsUnknown;

    // Where the target sat in the frame (the tracker's centroid), in
    // frame pixels. This is the framing; the mount position only gets
    // the target into the frame.
    double      targetXPx = kObsUnknown;
    double      targetYPx = kObsUnknown;

    // Across the run, from the same accumulator the quality sidecar's
    // "Scope position" block is written from (SERWriter).
    double      altStartDeg = kObsUnknown, altEndDeg = kObsUnknown;
    double      altMinDeg   = kObsUnknown, altMaxDeg = kObsUnknown;
    double      azEndDeg    = kObsUnknown;
    double      airmassMean = kObsUnknown, airmassMax = kObsUnknown;

    double      siteLatDeg = kObsUnknown;
    double      siteLonDeg = kObsUnknown;

    bool HasMountPosition() const {
        return ObsKnown(raHours) && ObsKnown(decDeg);
    }
    bool HasTargetPixel() const {
        return ObsKnown(targetXPx) && ObsKnown(targetYPx);
    }
};

// ── Paths ─────────────────────────────────────────────────────
//
// The same rule SERWriter uses for _quality.txt: strip the extension,
// append the suffix. Kept identical on purpose, so the three files of
// one capture always sort together.

inline std::string ObsStripSuffix(const std::string& p, const char* suffix) {
    const size_t n = std::char_traits<char>::length(suffix);
    if (p.size() < n) return p;
    for (size_t i = 0; i < n; ++i) {
        const char a = (char)tolower((unsigned char)p[p.size() - n + i]);
        const char b = (char)tolower((unsigned char)suffix[i]);
        if (a != b) return p;
    }
    return p.substr(0, p.size() - n);
}

// <base>.ser, <base>_quality.txt or <base>_observation.txt -> <base>
inline std::string CaptureBasePath(const std::string& anyOfThree) {
    std::string s = anyOfThree;
    std::string t = ObsStripSuffix(s, "_observation.txt");
    if (t != s) return t;
    t = ObsStripSuffix(s, "_quality.txt");
    if (t != s) return t;
    const size_t slash = s.find_last_of("\\/");
    const size_t dot   = s.rfind('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        return s.substr(0, dot);
    return s;
}

inline std::string ObservationPathFor(const std::string& anyOfThree) {
    return CaptureBasePath(anyOfThree) + "_observation.txt";
}

// ── Time ──────────────────────────────────────────────────────

// "2026-10-25T21:30:45Z". Empty for 0.
inline std::string FormatUtcIso(int64_t unixSec) {
    if (unixSec <= 0) return std::string();
    std::time_t t = (std::time_t)unixSec;
    std::tm tmv{};
#ifdef _WIN32
    if (gmtime_s(&tmv, &t) != 0) return std::string();
#else
    if (!gmtime_r(&t, &tmv)) return std::string();
#endif
    char b[32];
    std::snprintf(b, sizeof(b), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return b;
}

// The inverse. 0 on anything that is not exactly that shape.
inline int64_t ParseUtcIso(const std::string& s) {
    int Y = 0, M = 0, D = 0, h = 0, m = 0, sec = 0;
    char z = 0;
#ifdef _MSC_VER
    const int got = sscanf_s(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%c",
                             &Y, &M, &D, &h, &m, &sec, &z, 1u);
#else
    const int got = std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%c",
                                &Y, &M, &D, &h, &m, &sec, &z);
#endif
    if (got != 7 || z != 'Z') return 0;
    if (M < 1 || M > 12 || D < 1 || D > 31 || h > 23 || m > 59 || sec > 60)
        return 0;
    std::tm tmv{};
    tmv.tm_year = Y - 1900; tmv.tm_mon = M - 1; tmv.tm_mday = D;
    tmv.tm_hour = h; tmv.tm_min = m; tmv.tm_sec = sec;
#ifdef _WIN32
    const int64_t t = (int64_t)_mkgmtime(&tmv);
#else
    const int64_t t = (int64_t)timegm(&tmv);
#endif
    return t > 0 ? t : 0;
}

// ── Write ─────────────────────────────────────────────────────

namespace obs_detail {

// One line per value, never a broken file: a value that is not plain
// one-line ASCII is cleaned rather than written raw.
inline std::string Clean(const std::string& v) {
    std::string out;
    out.reserve(v.size());
    for (char c : v) {
        const unsigned char u = (unsigned char)c;
        if (c == '\r' || c == '\n' || c == '\t') out += ' ';
        else if (u < 0x20 || u > 0x7e)            out += '?';
        else                                      out += c;
    }
    while (!out.empty() && out.back()  == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out;
}

inline void Str(std::string& o, const char* k, const std::string& v) {
    const std::string c = Clean(v);
    if (c.empty()) return;
    o += k; o += " = "; o += c; o += "\n";
}
inline void Int(std::string& o, const char* k, long long v, bool known) {
    if (!known) return;
    char b[48]; std::snprintf(b, sizeof(b), "%lld", v);
    o += k; o += " = "; o += b; o += "\n";
}
inline void Num(std::string& o, const char* k, double v, int decimals) {
    if (!ObsKnown(v)) return;
    char b[64]; std::snprintf(b, sizeof(b), "%.*f", decimals, v);
    o += k; o += " = "; o += b; o += "\n";
}

} // namespace obs_detail

inline std::string SerializeObservation(const ObservationRecord& r) {
    using namespace obs_detail;
    std::string o;
    o += "# RadukCapture observation record - one capture, one file\n";
    o += "# Format: key = value, one per line. A key that is absent was not\n";
    o += "# known when the capture ran; it is never written as 0. Unknown keys\n";
    o += "# are ignored by readers, so fields can be added without breaking them.\n";
    o += "# FITS: target=OBJECT start_utc=DATE-OBS exposure_ms/1000=EXPTIME\n";
    o += "#       gain=GAIN focuser_pos=FOCUSPOS focuser_temp_c=FOCTEMP\n";
    o += "#       ra_hours*15=RA dec_deg=DEC bin=XBINNING/YBINNING\n";
    o += "#       colour=BAYERPAT site_lat_deg=SITELAT site_lon_deg=SITELONG\n";
    o += "#\n";

    char fv[48];
    std::snprintf(fv, sizeof(fv), "raduk-observation %d", r.formatVersion);
    Str(o, "format", fv);
    Str(o, "app_version", r.appVersion);
    Str(o, "file",        r.file);
    Str(o, "target",      r.target);
    Str(o, "profile",     r.profile);
    Str(o, "frame_type",  r.frameType);

    Str(o, "start_utc", FormatUtcIso(r.startUnix));
    Str(o, "end_utc",   FormatUtcIso(r.endUnix));
    Num(o, "duration_s", r.durationSec, 2);
    Int(o, "frames",         r.frames,        r.frames >= 0);
    Int(o, "frames_dropped", r.framesDropped, r.framesDropped >= 0);

    Int(o, "width",     r.width,    r.width  > 0);
    Int(o, "height",    r.height,   r.height > 0);
    Int(o, "bit_depth", r.bitDepth, r.bitDepth > 0);
    Int(o, "bin",       r.bin,      r.bin > 0);
    Int(o, "roi_x",     r.roiX,     r.roiX >= 0);
    Int(o, "roi_y",     r.roiY,     r.roiY >= 0);
    Str(o, "colour",    r.colour);

    Num(o, "exposure_ms", r.exposureMs, 3);
    Num(o, "gain",        r.gain,       0);

    Int(o, "focuser_pos",    r.focuserPos, r.focuserPos >= 0);
    Num(o, "focuser_temp_c", r.focuserTempC, 1);

    Num(o, "ra_hours", r.raHours, 6);
    Num(o, "dec_deg",  r.decDeg,  5);
    Num(o, "alt_deg",  r.altDeg,  3);
    Num(o, "az_deg",   r.azDeg,   3);
    Num(o, "target_x_px", r.targetXPx, 1);
    Num(o, "target_y_px", r.targetYPx, 1);

    Num(o, "alt_start_deg", r.altStartDeg, 2);
    Num(o, "alt_end_deg",   r.altEndDeg,   2);
    Num(o, "alt_min_deg",   r.altMinDeg,   2);
    Num(o, "alt_max_deg",   r.altMaxDeg,   2);
    Num(o, "az_end_deg",    r.azEndDeg,    2);
    Num(o, "airmass_mean",  r.airmassMean, 3);
    Num(o, "airmass_max",   r.airmassMax,  3);

    Num(o, "site_lat_deg", r.siteLatDeg, 4);
    Num(o, "site_lon_deg", r.siteLonDeg, 4);
    return o;
}

// ── Read ──────────────────────────────────────────────────────
//
// Tolerant of everything a person or a later version might do to the
// file: comments, blank lines, CRLF, spaces around '=', keys in any
// order, keys it has never heard of. Strict about one thing -- it has
// to SAY it is an observation record, so an arbitrary text file picked
// by mistake is refused instead of read as a record with every field
// unknown.

namespace obs_detail {

inline std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r')) --b;
    return s.substr(a, b - a);
}

inline bool ToDouble(const std::string& v, double& out) {
    if (v.empty()) return false;
    char* end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (end == v.c_str() || *end != '\0' || !std::isfinite(d)) return false;
    out = d;
    return true;
}

inline bool ToInt(const std::string& v, int& out) {
    double d = 0;
    if (!ToDouble(v, d) || d != std::floor(d) || std::fabs(d) > 2.0e9) return false;
    out = (int)d;
    return true;
}

} // namespace obs_detail

inline bool ParseObservation(const std::string& text, ObservationRecord& r,
                             std::string* why = nullptr) {
    using namespace obs_detail;
    r = ObservationRecord{};
    bool sawFormat = false;

    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = Trim(text.substr(pos, nl - pos));
        pos = nl + 1;
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = Trim(line.substr(0, eq));
        const std::string v = Trim(line.substr(eq + 1));

        if (k == "format") {
            int ver = 0;
            if (v.rfind("raduk-observation ", 0) != 0 ||
                !ToInt(Trim(v.substr(18)), ver) || ver < 1) {
                if (why) *why = "not a RadukCapture observation record";
                return false;
            }
            r.formatVersion = ver;
            sawFormat = true;
        }
        else if (k == "app_version") r.appVersion = v;
        else if (k == "file")        r.file       = v;
        else if (k == "target")      r.target     = v;
        else if (k == "profile")     r.profile    = v;
        else if (k == "frame_type")  r.frameType  = v;
        else if (k == "colour")      r.colour     = v;
        else if (k == "start_utc")   r.startUnix  = ParseUtcIso(v);
        else if (k == "end_utc")     r.endUnix    = ParseUtcIso(v);
        else if (k == "duration_s")  ToDouble(v, r.durationSec);
        else if (k == "frames")          ToInt(v, r.frames);
        else if (k == "frames_dropped")  ToInt(v, r.framesDropped);
        else if (k == "width")       ToInt(v, r.width);
        else if (k == "height")      ToInt(v, r.height);
        else if (k == "bit_depth")   ToInt(v, r.bitDepth);
        else if (k == "bin")         ToInt(v, r.bin);
        else if (k == "roi_x")       ToInt(v, r.roiX);
        else if (k == "roi_y")       ToInt(v, r.roiY);
        else if (k == "exposure_ms") ToDouble(v, r.exposureMs);
        else if (k == "gain")        ToDouble(v, r.gain);
        else if (k == "focuser_pos")    ToInt(v, r.focuserPos);
        else if (k == "focuser_temp_c") ToDouble(v, r.focuserTempC);
        else if (k == "ra_hours")    ToDouble(v, r.raHours);
        else if (k == "dec_deg")     ToDouble(v, r.decDeg);
        else if (k == "alt_deg")     ToDouble(v, r.altDeg);
        else if (k == "az_deg")      ToDouble(v, r.azDeg);
        else if (k == "target_x_px") ToDouble(v, r.targetXPx);
        else if (k == "target_y_px") ToDouble(v, r.targetYPx);
        else if (k == "alt_start_deg") ToDouble(v, r.altStartDeg);
        else if (k == "alt_end_deg")   ToDouble(v, r.altEndDeg);
        else if (k == "alt_min_deg")   ToDouble(v, r.altMinDeg);
        else if (k == "alt_max_deg")   ToDouble(v, r.altMaxDeg);
        else if (k == "az_end_deg")    ToDouble(v, r.azEndDeg);
        else if (k == "airmass_mean")  ToDouble(v, r.airmassMean);
        else if (k == "airmass_max")   ToDouble(v, r.airmassMax);
        else if (k == "site_lat_deg")  ToDouble(v, r.siteLatDeg);
        else if (k == "site_lon_deg")  ToDouble(v, r.siteLonDeg);
        // anything else: a later version's field, ignored
    }

    if (!sawFormat) {
        if (why) *why = "not a RadukCapture observation record";
        return false;
    }
    // A half-written RA/Dec pair is no position at all.
    if (!(ObsKnown(r.raHours) && ObsKnown(r.decDeg)) ||
        r.raHours < 0.0 || r.raHours >= 24.0 ||
        r.decDeg < -90.0 || r.decDeg > 90.0) {
        r.raHours = r.decDeg = kObsUnknown;
    }
    return true;
}

// ── Recall, step 1: where to send the mount ──────────────────
//
// The mount move itself is NOT here. It goes through the GoTo panel's
// own DoGoTo, so it gets every gate a GoTo already has -- the keep-out
// sector, the cord-wrap forecast, the profile auto-load -- and phase 48
// adds no new way to move the telescope. This only decides whether a
// recall may be ASKED for, and with which coordinates.
//
// A planet's RA/Dec moves night to night, so for a target the catalogue
// can still find, the catalogue's position NOW is the right one, not
// the one written down. The recorded mount position is for everything
// else -- "Manual coords", a field nobody named.

constexpr double kRecallMinAltDeg = 15.0;   // the Tonight list's own floor
// Stricter than GoTo, which has no Sun check at all: a recall is a
// slew picked from a file, by someone who may not be looking at where
// the Sun is, on a telescope children look through. A solar session is
// still recalled by hand through the GoTo panel, deliberately.
constexpr double kRecallMinSunSepDeg = 20.0;

// Great-circle separation, degrees.
inline double AngularSepDeg(double ra1H, double dec1Deg, double ra2H, double dec2Deg) {
    const double d2r = 3.14159265358979323846 / 180.0;
    const double a1 = ra1H * 15.0 * d2r, a2 = ra2H * 15.0 * d2r;
    const double d1 = dec1Deg * d2r,     d2 = dec2Deg * d2r;
    double c = std::sin(d1) * std::sin(d2) + std::cos(d1) * std::cos(d2) * std::cos(a1 - a2);
    if (c > 1.0) c = 1.0;
    if (c < -1.0) c = -1.0;
    return std::acos(c) / d2r;
}

struct RecallInputs {
    bool   mountConnected = false;
    bool   mountAligned   = false;   // GoTo is disabled until it is
    // The record's target, looked up in the catalogue now. false when
    // there is no target or the catalogue does not know it.
    bool   targetResolved = false;
    double resolvedRaH    = 0.0;
    double resolvedDecDeg = 0.0;
    // Altitude of a position right now, or false if that cannot be
    // known (no site set). Without it there is no horizon check, and a
    // recall does not go ahead without one.
    std::function<bool(double raH, double decDeg, double& altDeg)> altitudeNow;
    // The Sun now. Unknown means refused, not waved through.
    bool   sunKnown  = false;
    double sunRaH    = 0.0;
    double sunDecDeg = 0.0;
};

struct RecallPlan {
    bool        valid = false;
    std::string why;               // why not, when !valid
    double      raHours = 0.0;
    double      decDeg  = 0.0;
    double      altNowDeg = 0.0;
    std::string name;              // passed to DoGoTo; drives the profile auto-load
    bool        fromCatalogue = false;
};

inline RecallPlan PlanRecall(const ObservationRecord& rec, const RecallInputs& in) {
    RecallPlan p;
    if (!in.mountConnected) { p.why = "no mount connected"; return p; }
    if (!in.mountAligned) {
        p.why = "the mount is not aligned - GoTo stays off until it is";
        return p;
    }

    if (!rec.target.empty() && in.targetResolved) {
        p.raHours = in.resolvedRaH;
        p.decDeg  = in.resolvedDecDeg;
        p.name    = rec.target;
        p.fromCatalogue = true;
    } else if (rec.HasMountPosition()) {
        p.raHours = rec.raHours;
        p.decDeg  = rec.decDeg;
        // DoGoTo auto-loads a profile by this name. A recorded target
        // the catalogue no longer knows is still the right name for
        // that; with no target at all, say what it is.
        p.name    = rec.target.empty() ? std::string("Recalled position")
                                       : rec.target;
    } else {
        p.why = "the record has no target the catalogue knows and no mount position";
        return p;
    }

    if (!(p.raHours >= 0.0 && p.raHours < 24.0 &&
          p.decDeg >= -90.0 && p.decDeg <= 90.0)) {
        p.why = "the recorded position is not a sky position";
        return p;
    }

    if (!in.sunKnown) {
        p.why = "the Sun's position is not known - refusing rather than guessing";
        return p;
    }
    {
        const double sep = AngularSepDeg(p.raHours, p.decDeg, in.sunRaH, in.sunDecDeg);
        if (sep < kRecallMinSunSepDeg) {
            char b[120];
            std::snprintf(b, sizeof(b),
                          "it is %.0f deg from the Sun - recall will not slew within %.0f",
                          sep, kRecallMinSunSepDeg);
            p.why = b;
            return p;
        }
    }

    double alt = 0.0;
    if (!in.altitudeNow || !in.altitudeNow(p.raHours, p.decDeg, alt)) {
        p.why = "no site set - the horizon cannot be checked";
        return p;
    }
    p.altNowDeg = alt;
    if (alt < kRecallMinAltDeg) {
        char b[96];
        std::snprintf(b, sizeof(b), "it is at %.1f deg now - below the %.0f deg floor",
                      alt, kRecallMinAltDeg);
        p.why = b;
        return p;
    }
    p.valid = true;
    return p;
}

// ── Recall, step 2: put the target where it was in the frame ─
//
// The GoTo gets the target into the frame and acquisition centres it.
// When the recorded framing was NOT centred -- a planet set to one side
// to take its moons in -- this nudges it back there, through phase 47's
// PlanRecentre and nothing else: same solve, same refusals, same pulse
// ceiling. Moving the target from where it is to where it was is a
// click at the point that would put "where it was" in the middle.

inline RecentrePlan PlanFramingRestore(const ObservationRecord& rec,
                                       float nowX, float nowY,
                                       int frameW, int frameH,
                                       const GuideResponse& resp,
                                       float arcsecPerPx, int maxPulseMs) {
    RecentrePlan p;
    if (!rec.HasTargetPixel()) {
        p.why = "the record has no target position in the frame";
        return p;
    }
    // A pixel offset only means the same thing in the same format.
    if (rec.width != frameW || rec.height != frameH) {
        p.why = "the frame size differs from the recording's";
        return p;
    }
    const float clickX = (float)frameW * 0.5f - ((float)rec.targetXPx - nowX);
    const float clickY = (float)frameH * 0.5f - ((float)rec.targetYPx - nowY);
    if (clickX < 0.0f || clickY < 0.0f ||
        clickX > (float)frameW || clickY > (float)frameH) {
        p.why = "too far to pulse -- slew there instead";
        return p;
    }
    p = PlanRecentre(clickX, clickY, frameW, frameH, resp, arcsecPerPx, maxPulseMs);
    if (!p.valid && p.why == std::string("already centred"))
        p.why = "already at the recorded framing";
    return p;
}

} // namespace raduk::core
