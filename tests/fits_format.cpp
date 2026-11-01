// ============================================================
//  What a FITS cube's bytes claim
//
//  Phase 106. The reader below is written against the standard
//  and shares no code with FITSFormat.h: card lookup by slicing
//  columns, value parsing by hand. If the two shared a helper, a
//  wrong column would agree with itself.
//
//  What cannot be gated here: whether Siril debayers it to the
//  right colours, which needs a real file opened in a real
//  program, and is owed in the phase's status row.
// ============================================================

#include "core/FITSFormat.h"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

using namespace raduk::core;
using namespace raduk::core::fits;

static int g_fail = 0;

static void Check(bool ok, const char* what) {
    if (ok) {
        printf("  ok    %s\n", what);
    } else {
        printf("  FAIL  %s\n", what);
        ++g_fail;
    }
}

// ── The independent reader ───────────────────────────────────

static std::string CardAt(const std::string& h, size_t i) {
    return h.substr(i * 80, 80);
}

static std::string Rtrim(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

// The card for `key` before END, or "" if there is none.
static std::string Find(const std::string& h, const std::string& key) {
    for (size_t i = 0; i * 80 < h.size(); ++i) {
        const std::string c = CardAt(h, i);
        const std::string k = Rtrim(c.substr(0, 8));
        if (k == "END") return "";
        if (k == key && c.substr(8, 2) == "= ") return c;
    }
    return "";
}

static bool Has(const std::string& h, const std::string& key) {
    return !Find(h, key).empty();
}

// Columns 11-80, up to the comment, for a number or a logical.
static std::string RawValue(const std::string& card) {
    if (card.size() < 11) return "";     // the keyword was not there
    std::string v = card.substr(10);
    const size_t slash = v.find('/');
    if (slash != std::string::npos) v = v.substr(0, slash);
    size_t a = v.find_first_not_of(' ');
    return a == std::string::npos ? "" : Rtrim(v.substr(a));
}

static double Number(const std::string& h, const std::string& key) {
    return atof(RawValue(Find(h, key)).c_str());
}

// A string value: undoubles quotes, drops trailing padding.
static std::string Text(const std::string& h, const std::string& key) {
    const std::string c = Find(h, key);
    if (c.size() < 11 || c[10] != '\'') return "<not a string>";
    std::string out;
    for (size_t i = 11; i < 80; ++i) {
        if (c[i] == '\'') {
            if (i + 1 < 80 && c[i + 1] == '\'') { out += '\''; ++i; continue; }
            return Rtrim(out);
        }
        out += c[i];
    }
    return "<unterminated>";
}

// The block holding END, counting from 0, or -1.
static int EndBlock(const std::string& h) {
    for (size_t i = 0; i * 80 < h.size(); ++i)
        if (Rtrim(CardAt(h, i)) == "END") return (int)(i / 36);
    return -1;
}

static bool AllPrintable(const std::string& h) {
    for (char c : h) {
        const unsigned char u = (unsigned char)c;
        if (u < 32 || u > 126) return false;
    }
    return true;
}

static uint16_t Stored16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

// As a FITS reader does it: signed big-endian, then physical = stored + BZERO.
static int Physical16(const uint8_t* p) {
    return (int)(int16_t)Stored16(p) + 32768;
}

// ── Fixtures ─────────────────────────────────────────────────

static ObservationRecord FullRecord() {
    ObservationRecord r;
    r.target = "Jupiter"; r.frameType = "Light";
    r.width = 640; r.height = 480; r.bitDepth = 16; r.bin = 1;
    r.roiX = 1600; r.roiY = 840; r.colour = "RGGB";
    r.exposureMs = 10.0; r.gain = 300.0;
    r.focuserPos = 168000; r.focuserTempC = 12.5;
    r.raHours = 6.0; r.decDeg = 23.25;
    r.altDeg = 41.5; r.azDeg = 182.25;
    r.airmassMean = 1.507;
    r.siteLatDeg = 47.60621234; r.siteLonDeg = -122.33207891;
    return r;
}

static HeaderExtras FullExtras() {
    HeaderExtras x;
    x.instrument = "ZWO ASI678MC"; x.telescope = "Celestron 8SE";
    x.software = "RadukCapture 2.1.0";
    x.pixelUm = 2.0; x.focalMm = 2032.0;
    return x;
}

// 2026-11-01 21:30:45.000000 UTC. The day counts here and at the leap
// day below came from Python's datetime, not from the code under test.
static const int64_t kNoonish =
    739920LL * kTicksPerDay + (21LL * 3600 + 30 * 60 + 45) * 10000000LL;

int main() {
    printf("FITS format\n");

    // ── Time, against dates worked out by hand ─────────────────
    Check(MjdFromTicks(0) == -678575.0, "MJD of tick zero");
    Check(MjdFromTicks(678576LL * kTicksPerDay) == 1.0,
          "one day after the MJD epoch is MJD 1");
    Check(IsoFromTicks(0) == "0001-01-01T00:00:00.000000", "ISO of tick zero");
    Check(IsoFromTicks(kNoonish) == "2026-11-01T21:30:45.000000",
          "a known evening");
    Check(IsoFromTicks(738944LL * kTicksPerDay) == "2024-02-29T00:00:00.000000",
          "a leap day");
    Check(IsoFromTicks(kNoonish + 15) == "2026-11-01T21:30:45.000002",
          "ticks round to the microsecond");
    Check(IsoFromTicks(kTicksPerDay - 1) == "0001-01-02T00:00:00.000000",
          "rounding carries across midnight");
    {
        // 1 us resolution, at today's MJD, in a double.
        const double a = MjdFromTicks(kNoonish);
        const double b = MjdFromTicks(kNoonish + 10);
        const double us = (b - a) * 86400e6;
        Check(us > 0.5 && us < 1.5, "MJD_MID resolves a microsecond");
    }

    // ── Cards ──────────────────────────────────────────────────
    Check(CardInt("NAXIS1", 640).size() == 80, "a card is 80 characters");
    Check(CardInt("NAXIS1", 640)[29] == '0' && CardInt("NAXIS1", 640)[30] == ' ',
          "an integer ends in column 30");
    Check(CardBool("SIMPLE", true)[29] == 'T', "a logical sits in column 30");
    {
        const std::string h = CardString("OBJECT", "O'Brien");
        Check(Text(h + CardEnd(), "OBJECT") == "O'Brien", "a quote round trips");
    }
    {
        const std::string h = CardString("OBJECT", "M1");
        Check(h.substr(10, 10) == "'M1      '", "a short string pads to 8");
    }
    {
        // 67 characters then a quote: doubling would need 69, and half
        // a pair would terminate the string early.
        const std::string v = std::string(67, 'x') + "'y";
        const std::string c = CardString("OBJECT", v);
        Check(c.size() == 80 && Text(c + CardEnd(), "OBJECT") == std::string(67, 'x'),
              "the limit never splits a doubled quote");
    }
    {
        const std::string c = CardString("OBJECT", std::string(200, 'z'));
        Check(c.size() == 80 && Text(c + CardEnd(), "OBJECT").size() == 68,
              "a long name stops at 68 characters");
    }
    {
        const std::string c = CardString("OBJECT", "Caf\xC3\xA9\x01");
        Check(AllPrintable(c) && Text(c + CardEnd(), "OBJECT") == "Caf???",
              "non-ASCII and control bytes do not reach the header");
    }
    {
        const std::string c = CardReal("KEY", 1.0, 1,
            "a comment far too long to fit on one card, which goes on and on "
            "well past column eighty and has to stop somewhere");
        Check(c.size() == 80, "a long comment does not grow the card");
    }

    // ── A full header ──────────────────────────────────────────
    RunTimes t;
    t.hasFrames     = true;
    t.firstMidTicks = kNoonish;
    t.lastMidTicks  = kNoonish + 60LL * 10000000LL;

    const std::string h = BuildHeader(FullRecord(), FullExtras(), t, 6000);

    Check(h.size() == kHeaderBytes, "the header fills the reserve exactly");
    Check(h.size() % 2880 == 0, "the header is whole blocks");
    Check(AllPrintable(h), "the header is printable ASCII");
    Check(EndBlock(h) == (int)kHeaderBlocks - 1,
          "END is in the last reserved block");
    Check(Rtrim(CardAt(h, kHeaderBlocks * 36 - 1)) == "END",
          "END is the last card, blanks before it");
    Check(Rtrim(CardAt(h, 0).substr(0, 8)) == "SIMPLE" &&
          Rtrim(CardAt(h, 1).substr(0, 8)) == "BITPIX" &&
          Rtrim(CardAt(h, 2).substr(0, 8)) == "NAXIS"  &&
          Rtrim(CardAt(h, 3).substr(0, 8)) == "NAXIS1" &&
          Rtrim(CardAt(h, 4).substr(0, 8)) == "NAXIS2" &&
          Rtrim(CardAt(h, 5).substr(0, 8)) == "NAXIS3",
          "the mandatory cards come first, in order");

    Check(Number(h, "BITPIX") == 16 && Number(h, "NAXIS") == 3, "16-bit cube");
    Check(Number(h, "NAXIS1") == 640 && Number(h, "NAXIS2") == 480 &&
          Number(h, "NAXIS3") == 6000, "dimensions: width, height, frames");
    Check(Number(h, "BZERO") == 32768 && Number(h, "BSCALE") == 1,
          "BZERO marks the data unsigned");
    Check(RawValue(Find(h, "EXTEND")) == "T", "EXTEND announces the table");

    Check(Text(h, "DATE-OBS") == "2026-11-01T21:30:44.995000",
          "DATE-OBS is the start: first midpoint less half the exposure");
    Check(Text(h, "DATE-END") == "2026-11-01T21:31:45.005000",
          "DATE-END is the last midpoint plus half");
    Check(Text(h, "DATE-AVG") == "2026-11-01T21:31:15.000000",
          "DATE-AVG is the middle of the run");
    Check(Text(h, "TIMESYS") == "UTC", "the time system is stated");

    Check(Number(h, "EXPTIME") == 0.010, "EXPTIME is in seconds");
    Check(Number(h, "RA") == 90.0, "RA is in degrees, from hours");
    Check(Text(h, "BAYERPAT") == "RGGB", "BAYERPAT");
    Check(Text(h, "ROWORDER") == "TOP-DOWN", "ROWORDER states what was written");
    Check(Text(h, "OBJECT") == "Jupiter", "OBJECT");
    Check(Number(h, "XORGSUBF") == 1600 && Number(h, "YORGSUBF") == 840,
          "the ROI's place on the sensor");

    // The owner's call, 2026-11-01: full precision, no names.
    Check(std::fabs(Number(h, "SITELAT") - 47.60621234) < 1e-9,
          "SITELAT at full precision");
    Check(std::fabs(Number(h, "SITELONG") + 122.33207891) < 1e-9,
          "SITELONG at full precision");
    Check(!Has(h, "OBSERVER"), "no OBSERVER card");
    Check(h.find("Raduk ") == std::string::npos,
          "the only Raduk is the software");

    // ── Unknown means absent ───────────────────────────────────
    {
        ObservationRecord r;
        r.width = 64; r.height = 48; r.bitDepth = 8;
        const std::string m = BuildHeader(r, HeaderExtras{}, RunTimes{}, 0);
        Check(m.size() == kHeaderBytes, "a bare record still makes a header");
        Check(Number(m, "BITPIX") == 8 && !Has(m, "BZERO") && !Has(m, "BSCALE"),
              "8-bit is unsigned already");
        const char* absent[] = { "EXPTIME", "GAIN", "RA", "DEC", "OBJECT",
                                 "XORGSUBF", "YORGSUBF", "XBINNING", "SITELAT",
                                 "SITELONG", "AIRMASS", "FOCUSPOS", "FOCUSTEM",
                                 "BAYERPAT", "DATE-OBS", "DATE-AVG", "TIMESYS",
                                 "XPIXSZ", "FOCALLEN", "INSTRUME" };
        bool none = true;
        for (const char* k : absent) if (Has(m, k)) none = false;
        Check(none, "an unknown value produces no card, not a zero");
        Check(Number(m, "NAXIS3") == 0 && EndBlock(m) == (int)kHeaderBlocks - 1,
              "the zero-frame header written at open is valid");
    }
    {
        // The start needs the exposure. The middle does not.
        ObservationRecord r = FullRecord();
        r.exposureMs = kObsUnknown;
        const std::string m = BuildHeader(r, FullExtras(), t, 6000);
        Check(!Has(m, "DATE-OBS") && !Has(m, "DATE-END") && Has(m, "DATE-AVG"),
              "no exposure: the start is omitted, the middle is not");
    }
    {
        RunTimes one;
        one.hasFrames = true;
        one.firstMidTicks = one.lastMidTicks = kNoonish;
        const std::string m = BuildHeader(FullRecord(), FullExtras(), one, 1);
        Check(Number(m, "NAXIS3") == 1 &&
              Text(m, "DATE-AVG") == "2026-11-01T21:30:45.000000" &&
              Text(m, "DATE-OBS") == "2026-11-01T21:30:44.995000",
              "a one-frame run");
    }
    {
        ObservationRecord r = FullRecord();
        r.colour = "MONO";
        Check(!Has(BuildHeader(r, FullExtras(), t, 1), "BAYERPAT"),
              "a mono frame claims no pattern");
        r.bitDepth = 12;
        Check(BuildHeader(r, FullExtras(), t, 1).empty(),
              "a bit depth FITS is not given here is refused");
    }

    // ── Samples ────────────────────────────────────────────────
    {
        const uint16_t samples[] = { 0, 1, 255, 256, 32767, 32768, 65472, 65535 };
        const size_t n = sizeof(samples) / sizeof(samples[0]);
        std::vector<uint8_t> src(n * 2), dst(n * 2, 0xEE);
        for (size_t i = 0; i < n; ++i) {
            src[i * 2]     = (uint8_t)(samples[i] & 0xFF);   // little-endian
            src[i * 2 + 1] = (uint8_t)(samples[i] >> 8);
        }
        const std::vector<uint8_t> before = src;
        ToFits16(src.data(), dst.data(), src.size());

        bool same = true;
        for (size_t i = 0; i < n; ++i)
            if (Physical16(&dst[i * 2]) != (int)samples[i]) same = false;
        Check(same, "every sample survives offset and swap, edges included");
        Check(Stored16(&dst[0]) == 0x8000, "sample 0 stores as -32768");
        Check(Stored16(&dst[5 * 2]) == 0x0000, "sample 32768 stores as 0");
        Check(src == before, "the source slot is not touched");
    }
    {
        // An odd size must not read past the end.
        const uint8_t src[3] = { 0x34, 0x12, 0x99 };
        uint8_t dst[3] = { 0xEE, 0xEE, 0xEE };
        ToFits16(src, dst, 3);
        Check(dst[0] == (0x12 ^ 0x80) && dst[1] == 0x34 && dst[2] == 0xEE,
              "a stray trailing byte is left alone");
    }

    // ── Padding ────────────────────────────────────────────────
    Check(PaddingFor(0) == 0 && PaddingFor(2880) == 0, "a full block needs none");
    Check(PaddingFor(1) == 2879 && PaddingFor(2881) == 2879, "padding to a block");
    {
        // Past 4 GB: a 16-bit full-sensor run gets there in seconds.
        const uint64_t big = 3840ULL * 2160 * 2 * 6000;
        Check((big + PaddingFor(big)) % 2880 == 0, "padding past 32 bits");
    }

    // ── The table ──────────────────────────────────────────────
    {
        const std::string th = BuildTableHeader(6000);
        Check(th.size() == 2880, "the table header is one block");
        Check(AllPrintable(th) && EndBlock(th) == 0, "and it ends");
        Check(Text(th, "XTENSION") == "BINTABLE", "XTENSION");
        Check(Rtrim(CardAt(th, 0).substr(0, 8)) == "XTENSION" &&
              Rtrim(CardAt(th, 1).substr(0, 8)) == "BITPIX" &&
              Rtrim(CardAt(th, 2).substr(0, 8)) == "NAXIS"  &&
              Rtrim(CardAt(th, 3).substr(0, 8)) == "NAXIS1" &&
              Rtrim(CardAt(th, 4).substr(0, 8)) == "NAXIS2" &&
              Rtrim(CardAt(th, 5).substr(0, 8)) == "PCOUNT" &&
              Rtrim(CardAt(th, 6).substr(0, 8)) == "GCOUNT" &&
              Rtrim(CardAt(th, 7).substr(0, 8)) == "TFIELDS",
              "the extension's mandatory cards, in order");
        Check(Number(th, "NAXIS1") == 12 && Number(th, "NAXIS2") == 6000,
              "row size and count");
        Check(Number(th, "TFIELDS") == 2 && Text(th, "TFORM1") == "1D" &&
              Text(th, "TFORM2") == "1E", "columns: a double and a float");
        Check(Text(th, "TTYPE1") == "MJD_MID" && Text(th, "EXTNAME") == "FRAMES",
              "the names the header's comment promises");
        Check(Number(BuildTableHeader(0), "NAXIS2") == 0, "an empty table is valid");
    }
    {
        // 1.5 is 0x3FF8000000000000, and 2.0f is 0x40000000.
        uint8_t row[kTableRowBytes] = {};
        PutTableRow(row, 1.5, 2.0f);
        const uint8_t want[12] = { 0x3F, 0xF8, 0, 0, 0, 0, 0, 0,
                                   0x40, 0x00, 0, 0 };
        Check(memcmp(row, want, sizeof(want)) == 0,
              "a table row is big-endian, double then float");
    }

    if (g_fail == 0) printf("PASS\n"); else printf("FAIL: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
