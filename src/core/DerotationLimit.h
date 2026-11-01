#pragma once
// ============================================================
//  How long a run lasts before the planet's rotation blurs it
//
//  Phase 108. A feature at the centre of the disc moves fastest, at
//  pi * diameter / period. The window is the time it takes to cross
//  the telescope's Rayleigh limit, because past that the stack loses
//  detail the optics could have delivered. Jupiter on an 8-inch comes
//  to about three minutes, which is the figure planetary imagers use.
//
//  The axis tilt is ignored, so a tilted planet gets a slightly short
//  window. That is the safe way round.
//
//  Pure, so tests/derotation_limit.cpp needs no camera.
// ============================================================

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>

namespace raduk::core {

struct RotatingBody {
    const char* name;
    double      periodSec;
};

// The rotation an observer sees. Jupiter is System II, which is what
// the belts and the Red Spot follow. Venus is its cloud tops, not its
// 243-day globe, because the clouds are all a camera can see. The Sun
// is synodic. The Moon is absent: it keeps one face to us.
inline constexpr RotatingBody kRotatingBodies[] = {
    { "Mercury", 58.646 * 86400.0 },
    { "Venus",   4.0    * 86400.0 },
    { "Mars",    24.6229 * 3600.0 },
    { "Jupiter", 35730.0 },
    { "Saturn",  10.656  * 3600.0 },
    { "Uranus",  17.24   * 3600.0 },
    { "Neptune", 16.11   * 3600.0 },
    { "Sun",     27.2753 * 86400.0 },
};

// A target string can be a profile name, so "Jupiter - GRS transit"
// and "jupiter" both count. "Sunflower Galaxy" does not count as the
// Sun, and neither does anything that only contains a planet's name.
inline const RotatingBody* FindRotatingBody(const std::string& target) {
    for (const auto& b : kRotatingBodies) {
        size_t n = 0;
        while (b.name[n] && n < target.size() &&
               std::tolower((unsigned char)target[n]) ==
               std::tolower((unsigned char)b.name[n]))
            ++n;
        if (b.name[n]) continue;
        if (n == target.size() || !std::isalpha((unsigned char)target[n]))
            return &b;
    }
    return nullptr;
}

// 1.22 * lambda / D, in arcseconds. 550 nm is the emulator's figure too.
inline double RayleighArcsec(double apertureMm, double wavelengthNm = 550.0) {
    if (apertureMm <= 0.0) return 0.0;
    return 1.22 * (wavelengthNm * 1e-6) / apertureMm * 206264.806;
}

// Seconds until the centre of the disc has moved toleranceArcsec.
// 0 means no window, so nothing should warn or stop.
inline double DerotationWindowSec(double discArcsec, double periodSec,
                                  double toleranceArcsec) {
    if (discArcsec <= 0.0 || periodSec <= 0.0 || toleranceArcsec <= 0.0)
        return 0.0;
    const double pi = 3.14159265358979323846;
    return toleranceArcsec * periodSec / (pi * discArcsec);
}

// ── WinJUPOS naming ─────────────────────────────────────────
//
// WinJUPOS reads yyyy-mm-dd-hhmm_t from a file name, in UT, where t is
// tenths of a minute. It has to be the MIDDLE of the run, which is not
// known until the run ends, so a capture is renamed when it closes
// rather than named this way when it opens.

inline std::string WinJuposStamp(int64_t unixUtc) {
    const time_t tt = (time_t)unixUtc;
    struct tm t{};
    gmtime_s(&t, &tt);
    char b[32];
    snprintf(b, sizeof(b), "%04d-%02d-%02d-%02d%02d_%d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec / 6);
    return b;
}

inline std::string WinJuposPath(const std::string& path, int64_t midUnix) {
    const size_t slash = path.find_last_of("\\/");
    const size_t at = (slash == std::string::npos) ? 0 : slash + 1;
    return path.substr(0, at) + WinJuposStamp(midUnix) + "-" + path.substr(at);
}

// <base>.ser -> <base>_quality.txt, the sidecar every format writes.
inline std::string QualitySidecarFor(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    const size_t slash = path.find_last_of("\\/");
    const bool hasExt = dot != std::string::npos &&
                        (slash == std::string::npos || dot > slash);
    return (hasExt ? path.substr(0, dot) : path) + "_quality.txt";
}

// Renames the capture and its sidecar together, and returns the new
// path. On any failure both keep their old names and the old path comes
// back, because a good capture with a plain name beats a capture
// separated from its scores.
inline std::string RenameForWinJupos(const std::string& path,
                                     int64_t midUnix) {
    namespace fs = std::filesystem;
    const std::string to = WinJuposPath(path, midUnix);
    std::error_code ec;
    if (fs::exists(to, ec)) return path;   // rename would overwrite it
    fs::rename(path, to, ec);
    if (ec) return path;

    const std::string qFrom = QualitySidecarFor(path);
    if (fs::exists(qFrom, ec)) {
        const std::string qTo = QualitySidecarFor(to);
        fs::rename(qFrom, qTo, ec);
        if (ec) {
            std::error_code undo;
            fs::rename(to, path, undo);
            return path;
        }
    }
    return to;
}

} // namespace raduk::core
