#pragma once
// ============================================================
// SERWriter.h — SER v3 file writer for RadukCapture
// SER v3 is the standard astronomy format: uncompressed,
// per-frame UTC timestamps, supports RAW8/RAW16 + Bayer CFA.
// Compatible with AutoStakkert!, Registax, Siril, PIPP.
//
// Usage:
//   SERWriter w;
//   w.Open("Jupiter_20260314_213045.ser", width, height, bitDepth, ASI_IMG_RAW8);
//   w.WriteFrame(data, sizeBytes, timestampUs);   // called per frame
//   w.Close();  // writes timestamp trailer, finalizes header
// ============================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <ctime>
#include <windows.h>
#include "Timestamp.h"

namespace raduk::core {

// SER v3 color IDs
enum class SERColorId : int32_t {
    MONO       = 0,
    BAYER_RGGB = 8,
    BAYER_GRBG = 9,
    BAYER_GBRG = 10,
    BAYER_BGGR = 11,
    RGB        = 100,
    BGR        = 101,
};

// SER v3 header — 178 bytes, little-endian
#pragma pack(push, 1)
struct SERHeader {
    char     FileID[14];        // "LUCAM-RECORDER"
    int32_t  LuID;              // 0
    int32_t  ColorID;           // SERColorId
    int32_t  LittleEndian;      // 0 = big, 1 = little
    int32_t  ImageWidth;
    int32_t  ImageHeight;
    int32_t  PixelDepthPerPlane; // 8 or 16
    int32_t  FrameCount;        // filled in at Close()
    char     Observer[40];
    char     Instrument[40];
    char     Telescope[40];
    int64_t  DateTime;          // ticks since 0001-01-01 (Windows FILETIME compatible)
    int64_t  DateTimeUTC;       // same, in UTC
};
#pragma pack(pop)
static_assert(sizeof(SERHeader) == 178, "SERHeader must be 178 bytes");

class SERWriter {
public:
    SERWriter() = default;
    ~SERWriter() { if (IsOpen()) Close(); }

    // Where the scope was pointing while this ran, recorded into the
    // sidecar. Call as often as you like; the first and last are kept
    // along with the extremes, which is the right granularity -- a
    // planet's altitude moves a quarter of a degree a minute, so
    // per-frame values would be the same number thousands of times.
    void NoteSkyConditions(double altitudeDeg, double azimuthDeg,
                           double airmass, double seeingFactor,
                           double dispersionArcsec) {
        if (!m_skySeen) {
            m_skyAltStart = altitudeDeg;
            m_skyAltMin = m_skyAltMax = altitudeDeg;
            m_skyAzStart = azimuthDeg;
            m_skySeen = true;
        }
        m_skyAltEnd = altitudeDeg;
        m_skyAzEnd  = azimuthDeg;
        if (altitudeDeg < m_skyAltMin) m_skyAltMin = altitudeDeg;
        if (altitudeDeg > m_skyAltMax) m_skyAltMax = altitudeDeg;
        m_skyAirmassSum += airmass;
        m_skyAirmassMax  = (airmass > m_skyAirmassMax) ? airmass : m_skyAirmassMax;
        m_skySeeingEnd   = seeingFactor;
        m_skyDispersion  = dispersionArcsec;
        m_skySamples++;
    }

    SERWriter(const SERWriter&) = delete;
    SERWriter& operator=(const SERWriter&) = delete;

    bool IsOpen() const { return m_file != nullptr; }
    uint32_t FramesWritten() const { return m_framesWritten; }
    uint64_t BytesWritten() const { return m_bytesWritten; }
    const std::string& Path() const { return m_path; }

    bool Open(const std::string& path,
              int width, int height,
              int bitDepth,          // 8 or 16
              bool bayerRGGB = true,
              const char* observer   = "Raduk",
              const char* instrument = "ZWO ASI678MC",
              const char* telescope  = "Celestron 8SE");

    // Write one frame with optional sharpness score (Phase 11 lucky imaging).
    // timestampUs: the frame's CAPTURE time (QPC microseconds, FrameSlot
    // convention) — converted to wall clock via the Open()-time correlation.
    // sharpness: 0-100 from FrameAnalyzer::sharpnessPct. 0 = unknown.
    bool WriteFrame(const uint8_t* data, size_t sizeBytes,
                    uint64_t timestampUs, float sharpness = 0.0f);

    // Finalize: write timestamp trailer, patch frame count, write quality file.
    bool Close();

private:
    static void ZeroFill(char* dst, const char* src, size_t dstSize);

    // Windows FILETIME: 100-nanosecond ticks since 1601-01-01.
    // SER uses ticks since 0001-01-01, which is 504911232000000000 ticks earlier.
    static constexpr int64_t EPOCH_OFFSET = 504911232000000000LL;

    static int64_t CurrentFileTimeUTC();

    static int64_t CurrentFileTime();

    FILE*                 m_file         = nullptr;
    std::string           m_path;
    int                   m_width        = 0;
    int                   m_height       = 0;
    int                   m_bitDepth     = 8;
    uint32_t              m_framesWritten = 0;
    uint64_t              m_bytesWritten  = 0;
    // Phase 59 (C2): QPC↔wall-clock correlation captured at Open()
    uint64_t              m_qpcBaseUs = 0;
    int64_t               m_ftBase    = 0;
    std::vector<int64_t>  m_timestamps;
    std::vector<float>    m_sharpnessScores;  // Phase 11: one per frame

    // Scope position across the run, for the sidecar.
    bool   m_skySeen        = false;
    int    m_skySamples     = 0;
    double m_skyAltStart = 0, m_skyAltEnd = 0, m_skyAltMin = 0, m_skyAltMax = 0;
    double m_skyAzStart  = 0, m_skyAzEnd  = 0;
    double m_skyAirmassSum = 0, m_skyAirmassMax = 0;
    double m_skySeeingEnd  = 1.0, m_skyDispersion = 0.0;
};

} // namespace raduk::core
