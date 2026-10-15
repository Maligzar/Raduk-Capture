// ============================================================
// SERWriter.cpp — SER v3 file writer
// Out-of-line definitions for SERWriter.h.
// ============================================================

#include "SERWriter.h"

namespace raduk::core {

bool SERWriter::Open(const std::string& path,
                     int width, int height,
                     int bitDepth,          // 8 or 16
                     bool bayerRGGB,
                     const char* observer,
                     const char* instrument,
                     const char* telescope) {

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

bool SERWriter::WriteFrame(const uint8_t* data, size_t sizeBytes,
                           uint64_t timestampUs, float sharpness) {
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

bool SERWriter::Close() {
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

    // ── Per-frame quality sidecar ─────────────────────────
    // One sharpness score per line, frame order.
    //
    // AutoStakkert does NOT read this. The claim that it did stood
    // here since Phase 11 and is false: AS!4 was tested directly
    // against a recording with this sidecar beside it, and it ran
    // its own BufferAndAnalyze and never opened the file. No
    // external-quality import exists anywhere in the binary, and
    // its accepted inputs are avi/ser/as3/dat and image formats -
    // no .txt input path at all. A single global score could not
    // drive its selection regardless, because AS grades quality
    // per alignment point and each AP stacks a different subset.
    //
    // The file is kept because it is useful HERE: it is a record of
    // how the seeing ran across a capture, and it is what a
    // morning-after review reads to decide which runs are worth
    // stacking. Scores are produced per frame on the writer thread
    // (Phase 76) - before that they were whatever the ~2Hz analyser
    // last happened to emit, repeated across long runs of frames,
    // which could not rank anything.
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
            // Header comment. ASCII ONLY: this file is read by text
            // editors and spreadsheets that assume the system codepage,
            // and the UTF-8 em-dash that used to be here showed up as
            // mojibake in every one of them.
            fprintf(qf, "# RadukCapture quality scores - sharpness, one per frame\n");
            fprintf(qf, "# Frames: %u\n", m_framesWritten);
            fprintf(qf, "# Format: one score per line, frame order\n");
            // ── Where the scope was pointing ──────────────────
            //
            // Altitude decides how much atmosphere every frame in this
            // file was shot through, which is the biggest thing
            // separating one run from another on the same night. A
            // morning-after review comparing two captures of the same
            // target wants this before it wants anything else.
            //
            // In the header rather than per frame, because a planet's
            // altitude moves about a quarter of a degree a minute: a
            // per-frame column would be the same number thousands of
            // times over. Start, end and extremes carry everything a
            // run-length capture can honestly say.
            //
            // Comment lines, so the body stays exactly one score per
            // line and anything already reading this file keeps working.
            if (m_skySeen) {
                const double meanAir = (m_skySamples > 0)
                                     ? m_skyAirmassSum / m_skySamples : 0.0;
                fprintf(qf, "#\n");
                fprintf(qf, "# Scope position\n");
                fprintf(qf, "#   Altitude:    %.1f deg at start, %.1f at end (range %.1f to %.1f)\n",
                        m_skyAltStart, m_skyAltEnd, m_skyAltMin, m_skyAltMax);
                fprintf(qf, "#   Azimuth:     %.1f deg at start, %.1f at end\n",
                        m_skyAzStart, m_skyAzEnd);
                fprintf(qf, "#   Airmass:     %.3f mean, %.3f worst (1.000 = overhead)\n",
                        meanAir, m_skyAirmassMax);
                fprintf(qf, "#   Seeing cost: x%.2f vs the zenith (airmass^0.6)\n",
                        m_skySeeingEnd);
                fprintf(qf, "#   Dispersion:  %.2f arcsec red-to-blue, uncorrected\n",
                        m_skyDispersion);
                fprintf(qf, "#   Window:      %s\n",
                        m_skyAltMax >= 60.0 ? "PRIME (60 deg or above)"
                      : m_skyAltMax >= 45.0 ? "GOOD (45-60 deg)"
                      : m_skyAltMax >= 30.0 ? "MARGINAL (30-45 deg)"
                                            : "POOR (below 30 deg)");
                fprintf(qf, "#\n");
            }

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
    m_skySeen = false;
    m_skySamples = 0;
    m_skyAirmassSum = m_skyAirmassMax = 0.0;
    return true;
}

void SERWriter::ZeroFill(char* dst, const char* src, size_t dstSize) {
    memset(dst, 0, dstSize);
    if (src) strncpy_s(dst, dstSize, src, dstSize - 1);
}

int64_t SERWriter::CurrentFileTimeUTC() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER ul;
    ul.LowPart  = ft.dwLowDateTime;
    ul.HighPart = ft.dwHighDateTime;
    return (int64_t)ul.QuadPart + EPOCH_OFFSET;
}

int64_t SERWriter::CurrentFileTime() {
    FILETIME ft, local;
    GetSystemTimeAsFileTime(&ft);
    FileTimeToLocalFileTime(&ft, &local);
    ULARGE_INTEGER ul;
    ul.LowPart  = local.dwLowDateTime;
    ul.HighPart = local.dwHighDateTime;
    return (int64_t)ul.QuadPart + EPOCH_OFFSET;
}

} // namespace raduk::core
