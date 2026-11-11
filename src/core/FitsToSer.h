#pragma once
// ============================================================
// FitsToSer.h -- turn a FITS cube from this app into a SER
//
// 2026-11-09: PIPP refused a 27.54 GB Saturn cube from 2026-11-07 with
// "FITS file is too small" and listed it as one frame. Two ways that
// happens, and this repairs both:
//
//   - The run never reached FITSWriter::Close(). Open() writes the
//     header with NAXIS3 = 0 and only Close() writes the real count; that
//     night the app crashed on close (F10, fixed in #105). The frames are
//     all on disk, the header just says there are none. The count is
//     recovered from the file's size: whole frames after the header.
//   - A reader that does not take FITS cubes at all. SER is what PIPP,
//     AutoStakkert and every planetary tool read, and CaptureManager.h
//     already notes that AutoStakkert's headless run will not start on
//     our cubes.
//
// Samples: FITS 16-bit is big-endian, signed, offset by BZERO 32768; SER
// is little-endian unsigned. So each
// sample is byte-swapped and its top bit flipped. 8-bit is copied. The
// header's LittleEndian field is 0 over that, as SERWriter writes it
// (kSerLittleEndianFlag): what PIPP expects, not what the spec says.
//
// Times: a closed FITS from FITSWriter ends in a FRAMES table, one
// MJD_MID per frame. Those become the SER's timestamp trailer, so
// WinJUPOS and PLAY see real frame times. A cube with no table (an
// unclosed recording, or another program's) gets no trailer.
//
// Portable C++ (64-bit file offsets), no Windows: tests/fits_to_ser.cpp
// round-trips a cube through it, and tools/fits2ser.cpp is the command.
// ============================================================

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace raduk::core::fits2ser {

struct Header {
    int     bitpix = 0, naxis = 0;
    int64_t w = 0, h = 0, n = 0;   // n = NAXIS3 as written
    double  bzero = 0;
    std::string bayer, object, dateObs;
    int64_t headerBytes = 0;       // to the end of the block holding END
    std::vector<std::string> cards;
    bool ok = false;
};

namespace detail {
inline int Seek(FILE* f, int64_t off) {
#ifdef _WIN32
    return _fseeki64(f, off, SEEK_SET);
#else
    return fseeko(f, (off_t)off, SEEK_SET);
#endif
}
inline int64_t Size(FILE* f) {
#ifdef _WIN32
    _fseeki64(f, 0, SEEK_END);
    const int64_t s = _ftelli64(f);
#else
    fseeko(f, 0, SEEK_END);
    const int64_t s = (int64_t)ftello(f);
#endif
    Seek(f, 0);
    return s;
}
inline std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\'')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\'')) ++i;
    return s.substr(i);
}
// "YYYY-MM-DDTHH:MM:SS[.fff]" -> 100 ns ticks since 0001-01-01 (SER's clock).
inline int64_t TicksFromIso(const std::string& iso) {
    int Y = 0, M = 0, D = 0, hh = 0, mm = 0; double ss = 0;
    if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%lf", &Y, &M, &D, &hh, &mm, &ss) < 3 || Y < 1) return 0;
    auto days = [](int y, int m, int d) {   // days since 0001-01-01, proleptic Gregorian
        static const int cum[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
        const int64_t yy = y - 1;
        int64_t n = yy * 365 + yy / 4 - yy / 100 + yy / 400 + cum[(m - 1) % 12] + (d - 1);
        const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
        if (leap && m > 2) ++n;
        return n;
    };
    const double sec = days(Y, M, D) * 86400.0 + hh * 3600.0 + mm * 60.0 + ss;
    return (int64_t)(sec * 1e7);
}
} // namespace detail

inline Header ReadHeader(FILE* f) {
    Header h;
    char card[81] = {};
    int64_t read = 0;
    bool end = false;
    while (!end && read < 2880LL * 64) {
        if (fread(card, 1, 80, f) != 80) return h;
        read += 80;
        const std::string c(card, 80);
        h.cards.push_back(detail::Trim(c));
        const std::string key = detail::Trim(c.substr(0, 8));
        const std::string val = c.size() > 10 && c[8] == '=' ? c.substr(10) : std::string();
        std::string v = val.substr(0, val.find('/') == std::string::npos ? val.size() : val.find('/'));
        if (!v.empty() && detail::Trim(v).front() != '\'') v = detail::Trim(v);
        if (key == "END") end = true;
        else if (key == "BITPIX") h.bitpix = atoi(v.c_str());
        else if (key == "NAXIS")  h.naxis  = atoi(v.c_str());
        else if (key == "NAXIS1") h.w = atoll(v.c_str());
        else if (key == "NAXIS2") h.h = atoll(v.c_str());
        else if (key == "NAXIS3") h.n = atoll(v.c_str());
        else if (key == "BZERO")  h.bzero = atof(v.c_str());
        else if (key == "BAYERPAT") h.bayer = detail::Trim(val.substr(0, val.find('\'', 1)));
        else if (key == "OBJECT")   h.object = detail::Trim(val.substr(0, val.find('\'', 1)));
        else if (key == "DATE-OBS") h.dateObs = detail::Trim(val.substr(0, val.find('\'', 1)));
    }
    if (!end) return h;
    h.headerBytes = (read + 2879) / 2880 * 2880;
    h.ok = (h.bitpix == 8 || h.bitpix == 16) && h.w > 0 && h.h > 0;
    return h;
}

struct Plan {
    int64_t frameBytes = 0, wholeFrames = 0, frames = 0;
    bool recovered = false;   // the header's count was wrong and the size was used
};

// How many frames to take: the header's NAXIS3 when the file holds that
// many, else every whole frame the file holds.
inline Plan PlanFor(const Header& h, int64_t fileSize) {
    Plan p;
    p.frameBytes = h.w * h.h * (h.bitpix / 8);
    if (p.frameBytes <= 0 || fileSize <= h.headerBytes) return p;
    p.wholeFrames = (fileSize - h.headerBytes) / p.frameBytes;
    if (h.naxis == 3 && h.n > 0 && h.n <= p.wholeFrames) p.frames = h.n;
    else { p.frames = p.wholeFrames; p.recovered = true; }
    return p;
}

// The FRAMES table after the image data, as SER ticks (100 ns since
// 0001-01-01), one per frame. False when there is no such table or it
// does not have exactly `p.frames` rows of MJD_MID.
inline bool ReadFrameTimes(FILE* f, const Header& img, const Plan& p,
                           std::vector<int64_t>& out) {
    out.clear();
    const int64_t data = p.frames * p.frameBytes;
    const int64_t tableAt = img.headerBytes + (data + 2879) / 2880 * 2880;
    if (detail::Seek(f, tableAt) != 0) return false;
    const Header t = ReadHeader(f);
    bool frames = false;
    for (const std::string& c : t.cards)
        if (c.rfind("EXTNAME", 0) == 0 && c.find("FRAMES") != std::string::npos) frames = true;
    if (!frames || t.w != 12 || t.h != p.frames) return false;
    if (detail::Seek(f, tableAt + t.headerBytes) != 0) return false;
    std::vector<unsigned char> rows((size_t)(t.w * t.h));
    if (fread(rows.data(), 1, rows.size(), f) != rows.size()) return false;
    out.resize((size_t)p.frames);
    for (int64_t i = 0; i < p.frames; ++i) {
        unsigned char be[8];
        for (int k = 0; k < 8; ++k) be[k] = rows[(size_t)i * 12 + 7 - k];
        double mjd = 0;
        memcpy(&mjd, be, 8);
        // Whole days and the fraction apart: a tick count is too large
        // for a double to hold exactly. 678575 = MJD 0 in days from 0001.
        const double day = std::floor(mjd);
        out[(size_t)i] = ((int64_t)day + 678575) * 864000000000LL +
                         (int64_t)std::llround((mjd - day) * 864000000000.0);
    }
    return true;
}

// Converts `in` (FITS) to `out` (SER). Reports through `log`; stops,
// removing the partial SER, if `cancel` becomes true. True on success.
inline bool Convert(const std::string& in, const std::string& out,
                    const std::function<void(const char*)>& log = nullptr,
                    const std::atomic<bool>* cancel = nullptr) {
    auto say = [&](const std::string& s) { if (log) log(s.c_str()); };
    FILE* f = fopen(in.c_str(), "rb");
    if (!f) { say("cannot open " + in); return false; }
    const int64_t size = detail::Size(f);
    const Header h = ReadHeader(f);
    if (!h.ok) { fclose(f); say("not a FITS image this tool understands (BITPIX 8 or 16, 2 or 3 axes)"); return false; }
    const Plan p = PlanFor(h, size);
    char b[256];
    snprintf(b, sizeof b, "%lldx%lld, %d-bit, header says %lld frames, file holds %lld whole frames",
             (long long)h.w, (long long)h.h, h.bitpix, (long long)h.n, (long long)p.wholeFrames);
    say(b);
    if (p.recovered)
        say("the header's frame count is wrong (the recording was not closed); taking every whole frame");
    if (p.frames <= 0) { fclose(f); say("no whole frames to convert"); return false; }

    FILE* o = fopen(out.c_str(), "wb");
    if (!o) { fclose(f); say("cannot write " + out); return false; }
    unsigned char hdr[178] = {};
    memcpy(hdr, "LUCAM-RECORDER", 14);
    auto put32 = [&](int at, int32_t v) { memcpy(hdr + at, &v, 4); };
    auto put64 = [&](int at, int64_t v) { memcpy(hdr + at, &v, 8); };
    int32_t color = 0;
    if (h.bayer == "RGGB") color = 8; else if (h.bayer == "GRBG") color = 9;
    else if (h.bayer == "GBRG") color = 10; else if (h.bayer == "BGGR") color = 11;
    put32(14, 0); put32(18, color); put32(22, 0);   // 0 = little-endian, as PIPP reads it
    put32(26, (int32_t)h.w); put32(30, (int32_t)h.h); put32(34, h.bitpix);
    put32(38, (int32_t)p.frames);
    snprintf((char*)hdr + 82, 40, "RadukCapture fits2ser");
    if (!h.object.empty()) snprintf((char*)hdr + 122, 40, "%s", h.object.c_str());
    const int64_t t = detail::TicksFromIso(h.dateObs);
    put64(162, t); put64(170, t);
    bool ok = fwrite(hdr, 1, 178, o) == 178;

    std::vector<unsigned char> buf((size_t)p.frameBytes);
    detail::Seek(f, h.headerBytes);
    const bool flip = h.bitpix == 16 && h.bzero == 32768.0;
    bool cancelled = false;
    for (int64_t i = 0; ok && i < p.frames; ++i) {
        if (cancel && cancel->load()) { cancelled = true; ok = false; break; }
        if (fread(buf.data(), 1, buf.size(), f) != buf.size()) { ok = false; break; }
        if (h.bitpix == 16)
            for (size_t k = 0; k + 1 < buf.size(); k += 2) {
                const unsigned char hi = buf[k], lo = buf[k + 1];
                buf[k] = lo;
                buf[k + 1] = flip ? (unsigned char)(hi ^ 0x80) : hi;
            }
        ok = fwrite(buf.data(), 1, buf.size(), o) == buf.size();
        if (log && (i + 1) % 200 == 0) {
            snprintf(b, sizeof b, "  %lld / %lld frames", (long long)(i + 1), (long long)p.frames);
            say(b);
        }
    }
    // The FRAMES table, if the cube has one for exactly these frames.
    if (ok && !cancelled && !p.recovered) {
        std::vector<int64_t> stamps;
        if (ReadFrameTimes(f, h, p, stamps)) {
            ok = fwrite(stamps.data(), 8, stamps.size(), o) == stamps.size();
            say("frame times copied from the FRAMES table");
        } else {
            say("no FRAMES table for these frames; the SER has no timestamps");
        }
    }
    fclose(f);
    ok = fclose(o) == 0 && ok;
    if (cancelled) { remove(out.c_str()); say("cancelled; the partial SER was removed"); return false; }
    snprintf(b, sizeof b, "%s %lld frames to %s", ok ? "wrote" : "FAILED after", (long long)p.frames, out.c_str());
    say(b);
    return ok;
}

} // namespace raduk::core::fits2ser
