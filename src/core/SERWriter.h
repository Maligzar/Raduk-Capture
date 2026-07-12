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
              const char* telescope  = "Celestron 8SE") {

        if (m_file) Close();

        m_file = fopen(path.c_str(), "wb");
        if (!m_file) {
            printf("[SERWriter] Failed to open: %s\n", path.c_str());
            return false;
        }

        m_path         = path;
        m_width        = width;
        m_height       = height;
        m_bitDepth     = bitDepth;
        m_framesWritten = 0;
        m_bytesWritten  = 0;

        // Phase 59 (review C2): correlate the QPC clock (FrameSlot capture
        // stamps) with wall time ONCE, here. Each trailer entry is then
        // derived from the frame's capture QPC — not from when the writer
        // thread drained it, which lagged up to ~10s with a full 512-slot
        // backlog and made the trailer useless for WinJUPOS derotation.
        m_qpcBaseUs = Timestamp::Now();
        m_ftBase    = CurrentFileTimeUTC();

        // Build header
        SERHeader hdr = {};
        memcpy(hdr.FileID, "LUCAM-RECORDER", 14);
        hdr.LuID              = 0;
        hdr.ColorID           = (int32_t)(bayerRGGB
                                    ? SERColorId::BAYER_RGGB
                                    : SERColorId::MONO);
        hdr.LittleEndian      = 1;
        hdr.ImageWidth        = width;
        hdr.ImageHeight       = height;
        hdr.PixelDepthPerPlane = bitDepth;
        hdr.FrameCount        = 0;  // filled at Close()
        ZeroFill(hdr.Observer,   observer,   sizeof(hdr.Observer));
        ZeroFill(hdr.Instrument, instrument, sizeof(hdr.Instrument));
        ZeroFill(hdr.Telescope,  telescope,  sizeof(hdr.Telescope));
        hdr.DateTime    = CurrentFileTime();
        hdr.DateTimeUTC = CurrentFileTimeUTC();

        if (fwrite(&hdr, sizeof(hdr), 1, m_file) != 1) {
            printf("[SERWriter] Failed to write header\n");
            fclose(m_file); m_file = nullptr;
            return false;
        }
        m_bytesWritten += sizeof(hdr);

        printf("[SERWriter] Opened: %s  %dx%d  %d-bit\n",
               path.c_str(), width, height, bitDepth);
        return true;
    }

    // Write one frame with optional sharpness score (Phase 11 lucky imaging).
    // timestampUs: the frame's CAPTURE time (QPC microseconds, FrameSlot
    // convention) — converted to wall clock via the Open()-time correlation.
    // sharpness: 0-100 from FrameAnalyzer::sharpnessPct. 0 = unknown.
    bool WriteFrame(const uint8_t* data, size_t sizeBytes,
                    uint64_t timestampUs, float sharpness = 0.0f) {
        if (!m_file || !data || sizeBytes == 0) return false;

        size_t written = fwrite(data, 1, sizeBytes, m_file);
        if (written != sizeBytes) {
            printf("[SERWriter] Write error: only %zu of %zu bytes written\n",
                   written, sizeBytes);
            return false;
        }

        m_framesWritten++;
        m_bytesWritten += sizeBytes;
        // Capture-time trailer entry (100ns ticks): base + (qpc − qpcBase).
        // A zero timestamp (no capture stamp available) falls back to now.
        int64_t ts = (timestampUs != 0)
            ? m_ftBase + ((int64_t)timestampUs - (int64_t)m_qpcBaseUs) * 10
            : CurrentFileTimeUTC();
        m_timestamps.push_back(ts);
        m_sharpnessScores.push_back(sharpness);  // Phase 11

        return true;
    }

    // Finalize: write timestamp trailer, patch frame count, write quality file.
    bool Close() {
        if (!m_file) return false;

        // Write timestamp trailer (one int64 per frame)
        if (!m_timestamps.empty()) {
            fwrite(m_timestamps.data(),
                   sizeof(int64_t), m_timestamps.size(), m_file);
        }

        // Patch frame count in header (offset 38 into the struct)
        static constexpr long FRAME_COUNT_OFFSET = 38;
        fseek(m_file, FRAME_COUNT_OFFSET, SEEK_SET);
        int32_t fc = (int32_t)m_framesWritten;
        fwrite(&fc, sizeof(fc), 1, m_file);

        fclose(m_file);
        m_file = nullptr;

        printf("[SERWriter] Closed: %s  %u frames  %.1f MB\n",
               m_path.c_str(), m_framesWritten,
               m_bytesWritten / (1024.0 * 1024.0));

        // ── Phase 11: Write quality file ──────────────────────
        // One sharpness score per line (0.0-100.0), frame order.
        // AutoStakkert! reads this automatically for lucky stacking.
        // File: same name as SER but with _quality.txt extension.
        if (!m_sharpnessScores.empty()) {
            std::string qualPath = m_path;
            // Replace .ser extension with _quality.txt
            auto dotPos = qualPath.rfind('.');
            if (dotPos != std::string::npos)
                qualPath = qualPath.substr(0, dotPos);
            qualPath += "_quality.txt";

            FILE* qf = nullptr;
            if (fopen_s(&qf, qualPath.c_str(), "w") == 0 && qf) {
                // Header comment
                fprintf(qf, "# RadukCapture quality scores — sharpness 0-100\n");
                fprintf(qf, "# Frames: %u\n", m_framesWritten);
                fprintf(qf, "# Format: one score per line, frame order\n");

                float sum = 0, peak = 0;
                for (float s : m_sharpnessScores) {
                    fprintf(qf, "%.2f\n", s);
                    sum += s;
                    if (s > peak) peak = s;
                }
                float mean = m_framesWritten > 0 ? sum / m_framesWritten : 0;
                fprintf(qf, "# Mean: %.2f  Peak: %.2f\n", mean, peak);
                fclose(qf);
                printf("[SERWriter] Quality file: %s  mean=%.1f peak=%.1f\n",
                       qualPath.c_str(), mean, peak);
            }
        }

        m_timestamps.clear();
        m_sharpnessScores.clear();
        return true;
    }

private:
    static void ZeroFill(char* dst, const char* src, size_t dstSize) {
        memset(dst, 0, dstSize);
        if (src) strncpy_s(dst, dstSize, src, dstSize - 1);
    }

    // Windows FILETIME: 100-nanosecond ticks since 1601-01-01.
    // SER uses ticks since 0001-01-01, which is 504911232000000000 ticks earlier.
    static constexpr int64_t EPOCH_OFFSET = 504911232000000000LL;

    static int64_t CurrentFileTimeUTC() {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER ul;
        ul.LowPart  = ft.dwLowDateTime;
        ul.HighPart = ft.dwHighDateTime;
        return (int64_t)ul.QuadPart + EPOCH_OFFSET;
    }

    static int64_t CurrentFileTime() {
        FILETIME ft, local;
        GetSystemTimeAsFileTime(&ft);
        FileTimeToLocalFileTime(&ft, &local);
        ULARGE_INTEGER ul;
        ul.LowPart  = local.dwLowDateTime;
        ul.HighPart = local.dwHighDateTime;
        return (int64_t)ul.QuadPart + EPOCH_OFFSET;
    }

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
};

} // namespace raduk::core
