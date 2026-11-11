#pragma once
// ============================================================
// SerReader.h -- read a SER recording back, one frame at a time
//
// 2026-11-10: PIPP's preview of an 11,836-frame recording showed noise
// on frame 1 and offered no way to look further in. The app could write
// SER but never read one. This is the reading half, for the PLAY window
// (gui/PlayerWindow.h); kept free of Windows so tests/ser_reader.cpp
// can check it byte for byte.
//
// What it copes with:
//
//   - An unclosed recording. SERWriter::Open writes FrameCount = 0 and
//     only Close() writes the real count and the timestamp trailer; a
//     crash leaves every frame on disk and a header saying there are
//     none. As FitsToSer.h does for FITS, the count then comes from the
//     file's size: whole frames after the header, a partial one dropped.
//   - Files of any size. One frame is read per call, at a 64-bit offset;
//     nothing is mapped or loaded whole.
//   - 16-bit byte order. The SER spec says LittleEndian = 1 means
//     little-endian samples, but FireCapture writes 0 over little-endian
//     data and PIPP reads by that habit. This app wrote 1 until
//     2026-11-10 and now writes 0 (SERWriter.h, kSerLittleEndianFlag);
//     files from both eras exist. A reader that trusts the flag shows
//     half of them as noise. So the order is
//     decided from the data: the right order is the smooth one, because
//     the high byte changes slowly between neighbouring pixels and the
//     low byte does not. A tie (blank frames) reads little-endian.
//   - 9-15 bit depths (values not left-aligned) are scaled to 16 bits
//     for display and measurement.
// ============================================================

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace raduk::core::ser {

constexpr int64_t kHeaderBytes = 178;

enum ColorId : int32_t {
    MONO = 0,
    BAYER_RGGB = 8, BAYER_GRBG = 9, BAYER_GBRG = 10, BAYER_BGGR = 11,
    RGB = 100, BGR = 101,
};

struct Info {
    bool    ok = false;
    std::string error;           // why not, when !ok

    int32_t colorId = MONO;
    int32_t width = 0, height = 0;
    int32_t depth = 8;           // PixelDepthPerPlane as written
    int32_t littleEndianFlag = 1;
    int32_t headerFrames = 0;    // FrameCount as written
    int64_t frames = 0;          // frames this reader will serve
    int64_t frameBytes = 0;
    int64_t fileBytes = 0;
    bool    recovered = false;   // header count unusable; counted from size
    bool    hasTimestamps = false;
    bool    swap16 = false;      // 16-bit samples need a byte swap on this host
    int64_t dateTimeUtc = 0;     // ticks (100 ns) since 0001-01-01
    std::string observer, instrument, telescope;

    int  Planes() const { return (colorId == RGB || colorId == BGR) ? 3 : 1; }
    int  BytesPerSample() const { return depth > 8 ? 2 : 1; }
    bool IsBayer() const { return colorId >= BAYER_RGGB && colorId <= BAYER_BGGR; }
};

inline const char* ColorName(int32_t id) {
    switch (id) {
        case MONO: return "MONO";
        case BAYER_RGGB: return "RGGB";  case BAYER_GRBG: return "GRBG";
        case BAYER_GBRG: return "GBRG";  case BAYER_BGGR: return "BGGR";
        case RGB: return "RGB";          case BGR: return "BGR";
        default: return "?";
    }
}

namespace detail {
inline int Seek(FILE* f, int64_t off) {
#ifdef _WIN32
    return _fseeki64(f, off, SEEK_SET);
#else
    return fseeko(f, (off_t)off, SEEK_SET);
#endif
}
inline int64_t SizeOf(FILE* f) {
#ifdef _WIN32
    if (_fseeki64(f, 0, SEEK_END) != 0) return -1;
    const int64_t s = _ftelli64(f);
#else
    if (fseeko(f, 0, SEEK_END) != 0) return -1;
    const int64_t s = (int64_t)ftello(f);
#endif
    Seek(f, 0);
    return s;
}
template <typename T> inline T Get(const uint8_t* p) { T v; memcpy(&v, p, sizeof(T)); return v; }
inline std::string Text(const uint8_t* p, size_t n) {
    size_t e = 0;
    while (e < n && p[e]) ++e;
    std::string s((const char*)p, e);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

// Sum of |a - b| between same-colour horizontal neighbours (two apart,
// so a Bayer mosaic compares like with like), read as-is and swapped.
// The correct reading is the smaller.
inline void Roughness(const uint8_t* data, int w, int h, int planes,
                      uint64_t& asIs, uint64_t& swapped) {
    asIs = swapped = 0;
    const int rowSamples = w * planes;
    const int step = planes == 3 ? 3 : 2;
    const int rowStride = (std::max)(1, h / 64);
    for (int y = 0; y < h; y += rowStride) {
        const uint8_t* row = data + (size_t)y * rowSamples * 2;
        for (int i = 0; i + step < rowSamples; i += 1 + (rowSamples / 512)) {
            const uint8_t* a = row + (size_t)i * 2;
            const uint8_t* b = row + (size_t)(i + step) * 2;
            const int le_a = a[0] | (a[1] << 8), le_b = b[0] | (b[1] << 8);
            const int be_a = a[1] | (a[0] << 8), be_b = b[1] | (b[0] << 8);
            asIs    += (uint64_t)std::abs(le_a - le_b);
            swapped += (uint64_t)std::abs(be_a - be_b);
        }
    }
}
} // namespace detail

// Parses a header from its 178 bytes and the file's size. Separate from
// the file so the arithmetic is testable on its own.
inline Info ParseHeader(const uint8_t* h, int64_t fileBytes) {
    Info in;
    in.fileBytes = fileBytes;
    if (fileBytes < kHeaderBytes) { in.error = "too short for a SER header"; return in; }
    if (memcmp(h, "LUCAM-RECORDER", 14) != 0) { in.error = "not a SER file (no LUCAM-RECORDER)"; return in; }
    in.colorId          = detail::Get<int32_t>(h + 18);
    in.littleEndianFlag = detail::Get<int32_t>(h + 22);
    in.width            = detail::Get<int32_t>(h + 26);
    in.height           = detail::Get<int32_t>(h + 30);
    in.depth            = detail::Get<int32_t>(h + 34);
    in.headerFrames     = detail::Get<int32_t>(h + 38);
    in.observer   = detail::Text(h + 42, 40);
    in.instrument = detail::Text(h + 82, 40);
    in.telescope  = detail::Text(h + 122, 40);
    in.dateTimeUtc = detail::Get<int64_t>(h + 170);

    const bool knownColor = in.colorId == MONO || in.IsBayer() ||
                            in.colorId == RGB || in.colorId == BGR;
    if (!knownColor) { in.error = "unknown colour id " + std::to_string(in.colorId); return in; }
    if (in.width <= 0 || in.height <= 0 || in.width > 65536 || in.height > 65536) {
        in.error = "implausible frame size"; return in;
    }
    if (in.depth < 1 || in.depth > 16) { in.error = "unsupported bit depth " + std::to_string(in.depth); return in; }

    in.frameBytes = (int64_t)in.width * in.height * in.Planes() * in.BytesPerSample();
    const int64_t whole = (fileBytes - kHeaderBytes) / in.frameBytes;
    if (in.headerFrames > 0 && in.headerFrames <= whole) {
        in.frames = in.headerFrames;
    } else {
        in.frames = whole;
        in.recovered = true;
    }
    if (in.frames <= 0) { in.error = "no whole frame in the file"; return in; }
    in.hasTimestamps = !in.recovered &&
        fileBytes >= kHeaderBytes + in.frames * in.frameBytes + in.frames * 8;
    in.swap16 = false;   // settled by Reader::Open from the data
    in.ok = true;
    return in;
}

class Reader {
public:
    Reader() = default;
    ~Reader() { Close(); }
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    bool Open(const std::string& path) {
        Close();
        m_path = path;
        m_f = fopen(path.c_str(), "rb");
        if (!m_f) { m_info = {}; m_info.error = "cannot open the file"; return false; }
        const int64_t size = detail::SizeOf(m_f);
        uint8_t h[kHeaderBytes] = {};
        if (size < kHeaderBytes || fread(h, 1, kHeaderBytes, m_f) != (size_t)kHeaderBytes) {
            m_info = {}; m_info.error = "too short for a SER header"; Close(); return false;
        }
        m_info = ParseHeader(h, size);
        if (!m_info.ok) { const std::string e = m_info.error; Close(); m_info.error = e; return false; }

        if (m_info.hasTimestamps) {
            m_stamps.resize((size_t)m_info.frames);
            if (detail::Seek(m_f, kHeaderBytes + m_info.frames * m_info.frameBytes) != 0 ||
                fread(m_stamps.data(), 8, m_stamps.size(), m_f) != m_stamps.size()) {
                m_stamps.clear();
                m_info.hasTimestamps = false;
            }
        }
        if (m_info.BytesPerSample() == 2) DecideByteOrder();
        return true;
    }

    void Close() {
        if (m_f) { fclose(m_f); m_f = nullptr; }
        m_stamps.clear();
    }

    bool IsOpen() const { return m_f != nullptr; }
    const Info& GetInfo() const { return m_info; }
    const std::string& Path() const { return m_path; }

    // The raw frame as stored, frameBytes long.
    bool ReadFrame(int64_t i, std::vector<uint8_t>& out) {
        if (!m_f || i < 0 || i >= m_info.frames) return false;
        out.resize((size_t)m_info.frameBytes);
        if (detail::Seek(m_f, kHeaderBytes + i * m_info.frameBytes) != 0) return false;
        return fread(out.data(), 1, out.size(), m_f) == out.size();
    }

    // 0 when the file has no trailer (an unclosed recording, or a writer
    // that leaves it out).
    int64_t Timestamp(int64_t i) const {
        return (i >= 0 && i < (int64_t)m_stamps.size()) ? m_stamps[(size_t)i] : 0;
    }

    // Frames per second from the trailer's median spacing; 0 if unknown.
    double FrameRate() const {
        if (m_stamps.size() < 3) return 0.0;
        std::vector<int64_t> d;
        const size_t step = (std::max<size_t>)(1, m_stamps.size() / 2000);
        for (size_t i = step; i < m_stamps.size(); i += step)
            d.push_back((m_stamps[i] - m_stamps[i - step]) / (int64_t)step);
        std::nth_element(d.begin(), d.begin() + d.size() / 2, d.end());
        const int64_t med = d[d.size() / 2];
        return med > 0 ? 1e7 / (double)med : 0.0;
    }

private:
    void DecideByteOrder() {
        // Look at up to three frames spread through the file, so one dark
        // or blank frame cannot decide it.
        uint64_t asIs = 0, swapped = 0;
        std::vector<uint8_t> buf;
        const int64_t picks[3] = { 0, m_info.frames / 2, m_info.frames - 1 };
        for (int64_t p : picks) {
            if (!ReadFrame(p, buf)) continue;
            uint64_t a = 0, s = 0;
            detail::Roughness(buf.data(), m_info.width, m_info.height, m_info.Planes(), a, s);
            asIs += a; swapped += s;
        }
        // This host is little-endian, so "as is" means little-endian data.
        // A tie (blank frames) keeps little-endian: what this app writes,
        // and what most files flagged either way actually hold.
        m_info.swap16 = swapped < asIs;
    }

    FILE*                m_f = nullptr;
    std::string          m_path;
    Info                 m_info;
    std::vector<int64_t> m_stamps;
};

// ── Repairing the flag for PIPP ────────────────────────────────
//
// PIPP takes LittleEndian = 0 to mean little-endian samples. Files this
// app wrote before 2026-11-10 say 1 over little-endian samples, and PIPP
// shows them as noise. This rewrites those four bytes in place, to what
// the samples actually are (decided from the data, as Reader does), and
// touches nothing else: instant whatever the file's size.
enum class FlagRepair { Fixed, AlreadyRight, EightBit, Failed };

inline FlagRepair RepairEndianFlag(const std::string& path, std::string* why = nullptr) {
    int32_t want = 0, have = 0;
    {
        Reader r;
        if (!r.Open(path)) { if (why) *why = r.GetInfo().error; return FlagRepair::Failed; }
        const Info& in = r.GetInfo();
        if (in.BytesPerSample() == 1) return FlagRepair::EightBit;
        want = in.swap16 ? 1 : 0;   // big-endian samples: 1; little: 0
        have = in.littleEndianFlag;
    }   // closed before writing
    if (have == want) return FlagRepair::AlreadyRight;
    FILE* f = fopen(path.c_str(), "r+b");
    if (!f) { if (why) *why = "cannot open the file for writing"; return FlagRepair::Failed; }
    const bool ok = detail::Seek(f, 22) == 0 && fwrite(&want, 4, 1, f) == 1;
    const bool closed = fclose(f) == 0;
    if (!ok || !closed) { if (why) *why = "the write failed"; return FlagRepair::Failed; }
    return FlagRepair::Fixed;
}

// True when PIPP would misread this file: 16-bit with the flag not
// matching the samples, as above.
inline bool PippWouldMisread(const Info& in) {
    return in.ok && in.BytesPerSample() == 2 &&
           in.littleEndianFlag != (in.swap16 ? 1 : 0);
}

// ── Samples ────────────────────────────────────────────────────
//
// One frame as 16-bit left-aligned little-endian samples, the layout the
// rest of the app's analysis expects (FrameAnalyzer, TargetFinder): an
// 8-bit file is widened (v << 8), a swapped file swapped, a 12-bit one
// shifted up. RGB/BGR collapse to one luminance plane. `out` holds
// width*height values.
inline void ToPlane16(const uint8_t* raw, const Info& in, std::vector<uint16_t>& out) {
    const size_t n = (size_t)in.width * in.height;
    out.resize(n);
    const int planes = in.Planes();
    const int shift = in.depth > 8 ? 16 - in.depth : 0;
    auto sample = [&](size_t i) -> uint32_t {
        if (in.BytesPerSample() == 1) return (uint32_t)raw[i] << 8;
        const uint8_t* p = raw + i * 2;
        const uint32_t v = in.swap16 ? (uint32_t)(p[1] | (p[0] << 8))
                                     : (uint32_t)(p[0] | (p[1] << 8));
        return (v << shift) & 0xFFFF;
    };
    if (planes == 1) {
        for (size_t i = 0; i < n; ++i) out[i] = (uint16_t)sample(i);
    } else {
        for (size_t i = 0; i < n; ++i)
            out[i] = (uint16_t)((sample(i * 3) + sample(i * 3 + 1) + sample(i * 3 + 2)) / 3);
    }
}

// ── Display ────────────────────────────────────────────────────

struct Stretch {
    uint16_t black = 0, white = 65535;   // in 16-bit units
};

// Black at the sky (the median: a planet covers a small part of the
// frame), white at the 99.98th percentile so a few hot pixels do not set
// it. Merged over the frames given, so playback does not pump as each
// frame finds its own levels.
inline Stretch AutoStretch(const std::vector<const std::vector<uint16_t>*>& planes) {
    std::vector<uint32_t> hist(4096, 0);
    uint64_t total = 0;
    for (const auto* p : planes) {
        for (uint16_t v : *p) hist[v >> 4]++;
        total += p->size();
    }
    Stretch s;
    if (!total) return s;
    auto pct = [&](double f) {
        const uint64_t want = (uint64_t)(f * (double)total);
        uint64_t acc = 0;
        for (int i = 0; i < 4096; ++i) { acc += hist[i]; if (acc > want) return i; }
        return 4095;
    };
    const int b = pct(0.50), w = pct(0.9998);
    s.black = (uint16_t)(b << 4);
    s.white = (uint16_t)(std::min)(65535, (std::max)((b << 4) + 256, (w << 4) + 15));
    return s;
}

// The frame as 8-bit BGRA (alpha 255), width*height*4 bytes. `color`
// false shows a Bayer mosaic as it is (grey); true debayers it the way
// the live preview does, each 2x2 cell's R, mean G and B over all four.
inline void ToBgra(const uint8_t* raw, const Info& in, bool color, Stretch st,
                   std::vector<uint8_t>& bgra) {
    const int w = in.width, h = in.height;
    bgra.assign((size_t)w * h * 4, 255);
    const int planes = in.Planes();
    const int shift = in.depth > 8 ? 16 - in.depth : 0;
    auto sample = [&](size_t i) -> int {
        if (in.BytesPerSample() == 1) return (int)raw[i] << 8;
        const uint8_t* p = raw + i * 2;
        const int v = in.swap16 ? (p[1] | (p[0] << 8)) : (p[0] | (p[1] << 8));
        return (v << shift) & 0xFFFF;
    };
    const int black = st.black;
    // (v - black) * 256 / (range + 1): full range maps an 8-bit file's
    // values back to themselves exactly.
    const int range = (std::max)(1, (int)st.white - (int)st.black) + 1;
    uint8_t lut[4096];
    for (int i = 0; i < 4096; ++i) {
        const int v = ((i << 4) - black) * 256 / range;
        lut[i] = (uint8_t)(std::clamp)(v, 0, 255);
    }
    auto tone = [&](int v16) { return lut[(v16 >> 4) & 4095]; };

    if (planes == 3) {
        const bool bgr = in.colorId == BGR;
        for (size_t i = 0, n = (size_t)w * h; i < n; ++i) {
            const uint8_t a = tone(sample(i * 3)), b = tone(sample(i * 3 + 1)), c = tone(sample(i * 3 + 2));
            uint8_t* o = &bgra[i * 4];
            if (!color) { o[0] = o[1] = o[2] = (uint8_t)((a + b + c) / 3); continue; }
            o[0] = bgr ? a : c; o[1] = b; o[2] = bgr ? c : a;
        }
        return;
    }
    if (!in.IsBayer() || !color) {
        for (size_t i = 0, n = (size_t)w * h; i < n; ++i) {
            uint8_t* o = &bgra[i * 4];
            o[0] = o[1] = o[2] = tone(sample(i));
        }
        return;
    }
    // Where red sits in each 2x2 cell: (rx, ry). Blue is opposite it.
    int rx = 0, ry = 0;
    switch (in.colorId) {
        case BAYER_RGGB: rx = 0; ry = 0; break;
        case BAYER_GRBG: rx = 1; ry = 0; break;
        case BAYER_GBRG: rx = 0; ry = 1; break;
        case BAYER_BGGR: rx = 1; ry = 1; break;
    }
    for (int y = 0; y + 1 < h; y += 2) {
        for (int x = 0; x + 1 < w; x += 2) {
            const size_t i00 = (size_t)y * w + x;
            const int v[2][2] = { { sample(i00), sample(i00 + 1) },
                                  { sample(i00 + w), sample(i00 + w + 1) } };
            const uint8_t r = tone(v[ry][rx]);
            const uint8_t b = tone(v[1 - ry][1 - rx]);
            const uint8_t g = tone((v[ry][1 - rx] + v[1 - ry][rx]) / 2);
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    uint8_t* o = &bgra[((size_t)(y + dy) * w + x + dx) * 4];
                    o[0] = b; o[1] = g; o[2] = r;
                }
        }
    }
    // An odd last row or column: grey, rather than left white.
    if (w & 1) for (int y = 0; y < h; ++y) { uint8_t* o = &bgra[((size_t)y * w + w - 1) * 4]; o[0] = o[1] = o[2] = tone(sample((size_t)y * w + w - 1)); }
    if (h & 1) for (int x = 0; x < w; ++x) { uint8_t* o = &bgra[((size_t)(h - 1) * w + x) * 4]; o[0] = o[1] = o[2] = tone(sample((size_t)(h - 1) * w + x)); }
}

// "21:45:03.125" from SER ticks; "" for 0.
inline std::string TimeOfDay(int64_t ticks) {
    if (ticks <= 0) return "";
    const int64_t day = 864000000000LL;
    const int64_t t = ticks % day;
    const int ms = (int)((t / 10000) % 1000);
    const int s = (int)((t / 10000000) % 60);
    const int m = (int)((t / 600000000) % 60);
    const int hr = (int)(t / 36000000000LL);
    char b[32];
    snprintf(b, sizeof(b), "%02d:%02d:%02d.%03d", hr, m, s, ms);
    return b;
}

} // namespace raduk::core::ser
