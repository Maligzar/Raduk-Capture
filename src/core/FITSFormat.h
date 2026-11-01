#pragma once
// ============================================================
// FITSFormat.h — Phase 106: the bytes of a FITS cube
//
// One run is one cube: NAXIS1 x NAXIS2 x NAXIS3 frames, followed by
// a binary table with one row per frame. Without the table FITS
// would lose the per-frame times that make a SER usable for
// derotation.
//
// This header is pure: no Windows, no file, no camera, the same as
// ObservationRecord.h. It only turns values into bytes. Which bytes
// go where on disk is FITSWriter's business, and the test can reach
// everything here with no capture running.
//
// THE RESERVE. The header cannot be written when a recording opens:
// the observation source fires after Open(), and the sky figures only
// exist at the end. So the file starts with kHeaderBlocks of reserved
// space and BuildHeader() fills exactly that. The spare room is BLANK
// CARDS BEFORE END, not blank blocks after it, because a reader starts
// the data at the block following the one that holds END. Blocks after
// END would be read as pixels.
//
// TIME. Frame stamps are the midpoint of integration (P5.1), in the
// SER's own unit: 100 ns ticks since 0001-01-01 UTC. DATE-OBS is the
// START of the exposure, which is what FITS means by it, so it is the
// first midpoint less half the exposure. If the exposure is not known
// neither is the start, and DATE-OBS is omitted rather than guessed;
// DATE-AVG needs only the stamps.
//
// UNKNOWN VALUES ARE OMITTED, never written as zero. The observation
// record's rule, for the record's reason: a zero gain is a real gain.
//
// NO PERSON'S NAME. There is no OBSERVER card, by the owner's decision
// of 2026-11-01: names do not need to be in files that travel.
// ============================================================

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "ObservationRecord.h"

namespace raduk::core::fits {

constexpr size_t kBlockBytes  = 2880;
constexpr size_t kCardBytes   = 80;
constexpr size_t kCardsPerBlock = kBlockBytes / kCardBytes;

// Room for every card BuildHeader can emit, with some to spare for
// the next keyword someone wants.
constexpr size_t kHeaderBlocks = 3;
constexpr size_t kHeaderBytes  = kHeaderBlocks * kBlockBytes;

// A value field holds 70 characters, two of them the quotes.
constexpr size_t kMaxStringChars = 68;

// One table row: MJD_MID (double) + SHARPNESS (float).
constexpr size_t kTableRowBytes = 12;

// ── Padding ──────────────────────────────────────────────────

// How many bytes bring `size` up to a block boundary. Header and table
// padding are spaces and zeros respectively; the caller knows which.
inline size_t PaddingFor(uint64_t size) {
    const size_t rem = (size_t)(size % kBlockBytes);
    return rem == 0 ? 0 : kBlockBytes - rem;
}

// ── Cards ────────────────────────────────────────────────────

namespace detail {

inline std::string PadCard(std::string c) {
    c.resize(kCardBytes, ' ');
    return c;
}

// "KEY     = " with the keyword space-padded to eight columns.
inline std::string KeyPrefix(const char* key) {
    std::string s(key);
    s.resize(8, ' ');
    s += "= ";
    return s;
}

// FITS headers are ASCII 32-126 and nothing else. A target name can
// arrive from anywhere, so anything outside that becomes '?'.
inline char Printable(char c) {
    const unsigned char u = (unsigned char)c;
    return (u >= 32 && u <= 126) ? c : '?';
}

inline std::string WithComment(std::string card, const char* comment) {
    if (comment && *comment && card.size() + 3 < kCardBytes) {
        card += " / ";
        for (const char* p = comment; *p && card.size() < kCardBytes; ++p)
            card += Printable(*p);
    }
    return PadCard(std::move(card));
}

// Fixed format: the value right-justified to end in column 30.
inline std::string FixedValue(const char* key, const std::string& value,
                              const char* comment) {
    std::string card = KeyPrefix(key);
    if (value.size() < 20) card.append(20 - value.size(), ' ');
    card += value;
    return WithComment(std::move(card), comment);
}

} // namespace detail

inline std::string CardBool(const char* key, bool v,
                            const char* comment = nullptr) {
    return detail::FixedValue(key, v ? "T" : "F", comment);
}

inline std::string CardInt(const char* key, int64_t v,
                           const char* comment = nullptr) {
    return detail::FixedValue(key, std::to_string(v), comment);
}

inline std::string CardReal(const char* key, double v, int decimals,
                            const char* comment = nullptr) {
    char b[64];
    std::snprintf(b, sizeof(b), "%.*f", decimals, v);
    return detail::FixedValue(key, b, comment);
}

// A quote inside a string is doubled, and the pair is never split at
// the length limit: half a pair would end the string early.
inline std::string CardString(const char* key, const std::string& v,
                              const char* comment = nullptr) {
    std::string body;
    for (char c : v) {
        const char p = detail::Printable(c);
        const size_t cost = (p == '\'') ? 2 : 1;
        if (body.size() + cost > kMaxStringChars) break;
        body += p;
        if (p == '\'') body += p;
    }
    if (body.size() < 8) body.resize(8, ' ');   // the standard's minimum
    return detail::WithComment(detail::KeyPrefix(key) + "'" + body + "'",
                               comment);
}

inline std::string CardComment(const char* text) {
    std::string card = "COMMENT ";
    for (const char* p = text; *p && card.size() < kCardBytes; ++p)
        card += detail::Printable(*p);
    return detail::PadCard(std::move(card));
}

inline std::string CardEnd() { return detail::PadCard("END"); }

// ── Time ─────────────────────────────────────────────────────

constexpr int64_t kTicksPerDay = 864000000000LL;

// MJD of 0001-01-01, proleptic Gregorian.
constexpr int64_t kMjdOfTickZero = -678575;

// Whole days and the fraction are kept apart until the end: the tick
// count is too large for a double to hold exactly.
inline double MjdFromTicks(int64_t ticks) {
    const int64_t days = ticks / kTicksPerDay;
    const int64_t rem  = ticks % kTicksPerDay;
    return (double)(days + kMjdOfTickZero) + (double)rem / (double)kTicksPerDay;
}

// "2026-11-01T21:30:45.123456", UTC, to the microsecond.
// Howard Hinnant's civil_from_days, shifted to this epoch.
inline std::string IsoFromTicks(int64_t ticks) {
    const int64_t us  = (ticks + 5) / 10;
    const int64_t usPerDay = kTicksPerDay / 10;
    const int64_t day = us / usPerDay;
    int64_t       rem = us % usPerDay;

    const int64_t z   = day + 306;                 // days since 0000-03-01
    const int64_t era = z / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp  = (5 * doy + 2) / 153;
    const int     D   = (int)(doy - (153 * mp + 2) / 5 + 1);
    const int     M   = (int)(mp < 10 ? mp + 3 : mp - 9);
    const int     Y   = (int)(yoe + era * 400 + (M <= 2 ? 1 : 0));

    const int h = (int)(rem / 3600000000LL); rem %= 3600000000LL;
    const int m = (int)(rem / 60000000LL);   rem %= 60000000LL;
    const int s = (int)(rem / 1000000LL);    rem %= 1000000LL;

    char b[40];
    std::snprintf(b, sizeof(b), "%04d-%02d-%02dT%02d:%02d:%02d.%06d",
                  Y, M, D, h, m, s, (int)rem);
    return b;
}

// ── The primary header ───────────────────────────────────────

// What a header needs that the observation record does not hold.
struct HeaderExtras {
    std::string instrument;
    std::string telescope;
    std::string software;
    double      pixelUm = kObsUnknown;
    double      focalMm = kObsUnknown;
};

// First and last frame stamps, as midpoints. hasFrames=false before
// the first frame, which is what the header written at Open() is.
struct RunTimes {
    bool    hasFrames = false;
    int64_t firstMidTicks = 0;
    int64_t lastMidTicks  = 0;
};

// Exactly kHeaderBytes, or empty if the bit depth is not 8 or 16 or
// the cards would not fit. `frames` is NAXIS3.
inline std::string BuildHeader(const ObservationRecord& r,
                               const HeaderExtras& x,
                               const RunTimes& t,
                               int64_t frames) {
    if (r.bitDepth != 8 && r.bitDepth != 16) return std::string();

    std::string h;
    h.reserve(kHeaderBytes);
    const auto add = [&h](std::string card) { h += card; };
    const auto addReal = [&add](const char* key, double v, int decimals,
                                const char* comment) {
        if (ObsKnown(v)) add(CardReal(key, v, decimals, comment));
    };
    const auto addStr = [&add](const char* key, const std::string& v,
                               const char* comment) {
        if (!v.empty()) add(CardString(key, v, comment));
    };

    // The mandatory five, in the only order the standard allows.
    add(CardBool("SIMPLE", true, "conforms to FITS"));
    add(CardInt("BITPIX", r.bitDepth, "unsigned samples"));
    add(CardInt("NAXIS", 3));
    add(CardInt("NAXIS1", r.width, "width, px"));
    add(CardInt("NAXIS2", r.height, "height, px"));
    add(CardInt("NAXIS3", frames, "frames"));
    add(CardBool("EXTEND", true, "FRAMES table follows"));
    if (r.bitDepth == 16) {
        // FITS has no unsigned 16-bit integer: this is how it spells one.
        add(CardReal("BSCALE", 1.0, 1));
        add(CardInt("BZERO", 32768, "stored = sample - 32768"));
    }

    addStr("OBJECT",   r.target,    "target");
    addStr("IMAGETYP", r.frameType, "Light, Dark, Flat or Bias");
    addStr("INSTRUME", x.instrument, nullptr);
    addStr("TELESCOP", x.telescope,  nullptr);
    addStr("SWCREATE", x.software,   nullptr);

    if (t.hasFrames) {
        add(CardString("TIMESYS", "UTC"));
        if (ObsKnown(r.exposureMs)) {
            const int64_t half = (int64_t)std::llround(r.exposureMs * 5000.0);
            add(CardString("DATE-OBS", IsoFromTicks(t.firstMidTicks - half),
                           "start of the first exposure"));
            add(CardString("DATE-END", IsoFromTicks(t.lastMidTicks + half),
                           "end of the last exposure"));
        }
        add(CardString("DATE-AVG",
                       IsoFromTicks(t.firstMidTicks +
                                    (t.lastMidTicks - t.firstMidTicks) / 2),
                       "midway through the run"));
    }

    if (ObsKnown(r.exposureMs))
        add(CardReal("EXPTIME", r.exposureMs / 1000.0, 6, "per frame, s"));
    addReal("GAIN", r.gain, 2, "camera gain");
    addReal("GAMMA",  r.gamma,  0, "camera gamma, 50 neutral");
    addReal("WB_RED", r.wbRed,  0, "camera white balance, red");
    addReal("WB_BLUE", r.wbBlue, 0, "camera white balance, blue");
    if (r.bin > 0) {
        add(CardInt("XBINNING", r.bin));
        add(CardInt("YBINNING", r.bin));
    }
    addReal("XPIXSZ", x.pixelUm, 3, "um");
    addReal("YPIXSZ", x.pixelUm, 3, "um");
    addReal("FOCALLEN", x.focalMm, 1, "mm");
    if (r.roiX >= 0) add(CardInt("XORGSUBF", r.roiX, "ROI start, px"));
    if (r.roiY >= 0) add(CardInt("YORGSUBF", r.roiY, "ROI start, px"));

    // The ROI's start is forced even in both axes where it is set
    // (main.cpp, the ROI placement), so the pattern at the first pixel
    // is the sensor's own and XBAYROFF has nothing to say.
    // "MONO" is the other value the record holds, and it is four
    // letters too.
    if (r.colour.size() == 4 && r.colour != "MONO")
        add(CardString("BAYERPAT", r.colour));
    add(CardString("ROWORDER", "TOP-DOWN", "first row is the sensor's top"));

    if (r.HasMountPosition()) {
        add(CardReal("RA",  r.raHours * 15.0, 6, "deg, mount report"));
        add(CardReal("DEC", r.decDeg, 6, "deg, mount report"));
    }
    addReal("OBJCTALT", r.altDeg, 4, "deg at the start");
    addReal("OBJCTAZ",  r.azDeg,  4, "deg at the start");
    addReal("AIRMASS",  r.airmassMean, 3, "mean over the run");
    addReal("SITELAT",  r.siteLatDeg, 8, "deg, +N");
    addReal("SITELONG", r.siteLonDeg, 8, "deg, +E");
    if (r.focuserPos >= 0) add(CardInt("FOCUSPOS", r.focuserPos, "steps"));
    addReal("FOCUSTEM", r.focuserTempC, 2, "C");

    add(CardComment("Per-frame times are in the FRAMES table, column MJD_MID:"));
    add(CardComment("UTC midpoint of integration, not arrival at the host."));
    if (r.HasMountPosition())
        add(CardComment("RA and DEC are where the mount said it was, unsolved."));

    const size_t cards = h.size() / kCardBytes;
    if (cards + 1 > kHeaderBlocks * kCardsPerBlock) return std::string();
    h.append((kHeaderBlocks * kCardsPerBlock - cards - 1) * kCardBytes, ' ');
    h += CardEnd();
    return h;
}

// ── Samples ──────────────────────────────────────────────────

// Little-endian unsigned 16-bit in, FITS out: big-endian, offset by
// BZERO. Subtracting 32768 is flipping the top bit, so this is a swap
// and an XOR with no arithmetic. RAW8 needs none of it: BITPIX 8 is
// unsigned already.
//
// Into a separate buffer, because the writer thread holds a const slot.
inline void ToFits16(const uint8_t* src, uint8_t* dst, size_t sizeBytes) {
    const size_t n = sizeBytes & ~(size_t)1;
    for (size_t i = 0; i < n; i += 2) {
        dst[i]     = (uint8_t)(src[i + 1] ^ 0x80);
        dst[i + 1] = src[i];
    }
}

// ── The FRAMES table ─────────────────────────────────────────

// One header block, padded with spaces. Follows the padded image.
inline std::string BuildTableHeader(int64_t rows) {
    std::string h;
    h += CardString("XTENSION", "BINTABLE", "binary table");
    h += CardInt("BITPIX", 8);
    h += CardInt("NAXIS", 2);
    h += CardInt("NAXIS1", (int64_t)kTableRowBytes, "bytes per row");
    h += CardInt("NAXIS2", rows, "frames");
    h += CardInt("PCOUNT", 0);
    h += CardInt("GCOUNT", 1);
    h += CardInt("TFIELDS", 2);
    h += CardString("EXTNAME", "FRAMES");
    h += CardString("TTYPE1", "MJD_MID", "UTC midpoint of integration");
    h += CardString("TFORM1", "1D");
    h += CardString("TUNIT1", "d");
    h += CardString("TTYPE2", "SHARPNESS", "0-100, as _quality.txt");
    h += CardString("TFORM2", "1E");
    h += CardEnd();
    h.append(PaddingFor(h.size()), ' ');
    return h;
}

inline void PutBigEndian(uint8_t* dst, const void* src, size_t n) {
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; ++i) dst[i] = s[n - 1 - i];
}

// kTableRowBytes at dst. Assumes a little-endian host, as this whole
// application does.
inline void PutTableRow(uint8_t* dst, double mjdMid, float sharpness) {
    PutBigEndian(dst,     &mjdMid,    sizeof(mjdMid));
    PutBigEndian(dst + 8, &sharpness, sizeof(sharpness));
}

} // namespace raduk::core::fits
