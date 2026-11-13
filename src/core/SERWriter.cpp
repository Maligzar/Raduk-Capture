// ============================================================
// SERWriter.cpp — SER v3 file writer
// Out-of-line definitions for SERWriter.h.
// ============================================================

#include "SERWriter.h"
#include "CoreLog.h"

namespace raduk::core {

bool SERWriter::Open(const std::string& path,
                     int width, int height,
                     int bitDepth,          // 8 or 16
                     bool bayerRGGB) {

    if (m_file) Close();
    m_lastPatchUs = 0;
    m_patchedOnce = false;

    m_file = fopen(path.c_str(), "wb");
    if (!m_file) {
        raduk::core::CoreLog("[SERWriter] Failed to open: %s\n", path.c_str());
        return false;
    }

    m_path         = path;
    m_width        = width;
    m_height       = height;
    m_bitDepth     = bitDepth;
    m_framesWritten = 0;
    m_bytesWritten  = 0;

    m_clock.Start();

    // Build header
    SERHeader hdr = {};
    memcpy(hdr.FileID, "LUCAM-RECORDER", 14);
    hdr.LuID              = 0;
    hdr.ColorID           = (int32_t)(bayerRGGB
                                ? SERColorId::BAYER_RGGB
                                : SERColorId::MONO);
    // 0 over little-endian samples. The SER spec says 1 means
    // little-endian, but FireCapture writes 0 and PIPP reads by that
    // habit: with 1, PIPP byte-swapped every 16-bit pixel into noise
    // (2026-11-10, an 11,836-frame Saturn file). 8-bit is unaffected.
    hdr.LittleEndian      = kSerLittleEndianFlag;
    hdr.ImageWidth        = width;
    hdr.ImageHeight       = height;
    hdr.PixelDepthPerPlane = bitDepth;
    hdr.FrameCount        = 0;  // filled at Close()
    ZeroFill(hdr.Observer,   "Raduk",            sizeof(hdr.Observer));
    ZeroFill(hdr.Instrument, CaptureInstrument().c_str(), sizeof(hdr.Instrument));
    ZeroFill(hdr.Telescope,  CaptureTelescope().c_str(),  sizeof(hdr.Telescope));
    hdr.DateTime    = CurrentFileTime();
    hdr.DateTimeUTC = FrameClock::NowUtcTicks();

    if (fwrite(&hdr, sizeof(hdr), 1, m_file) != 1) {
        raduk::core::CoreLog("[SERWriter] Failed to write header\n");
        fclose(m_file); m_file = nullptr;
        return false;
    }
    m_bytesWritten += sizeof(hdr);

    raduk::core::CoreLog("[SERWriter] Opened: %s  %dx%d  %d-bit\n",
           path.c_str(), width, height, bitDepth);
    return true;
}

bool SERWriter::WriteFrame(const uint8_t* data, size_t sizeBytes,
                           uint64_t timestampUs, float sharpness) {
    if (!m_file || !data || sizeBytes == 0) return false;

    size_t written = fwrite(data, 1, sizeBytes, m_file);
    if (written != sizeBytes) {
        raduk::core::CoreLog("[SERWriter] Write error: only %zu of %zu bytes written\n",
               written, sizeBytes);
        return false;
    }

    m_framesWritten++;
    m_bytesWritten += sizeBytes;
    m_timestamps.push_back(m_clock.TicksFor(timestampUs));
    m_sharpnessScores.push_back(sharpness);  // Phase 11

    // Phase 135.2: FrameCount right on disk while the run goes on. The
    // trailer is still written only at Close: a crash loses timestamps,
    // not frames.
    if (HeaderPatchDue(m_lastPatchUs, timestampUs, !m_patchedOnce)) {
        m_lastPatchUs = timestampUs;
        m_patchedOnce = true;
        PatchFrameCount();
    }
    return true;
}

// Offset 38 of the 178-byte SER header is FrameCount. 64-bit seeks:
// a run passes 4 GB. Seeking flushes, so the four bytes reach the OS.
void SERWriter::PatchFrameCount() {
    static constexpr long long FRAME_COUNT_OFFSET = 38;
    if (fflush(m_file) != 0) return;
    if (_fseeki64(m_file, FRAME_COUNT_OFFSET, SEEK_SET) != 0) return;
    const int32_t fc = (int32_t)m_framesWritten;
    fwrite(&fc, sizeof(fc), 1, m_file);
    _fseeki64(m_file, 0, SEEK_END);
}

bool SERWriter::Close() {
    if (!m_file) return false;

    // Write timestamp trailer (one int64 per frame)
    if (!m_timestamps.empty()) {
        fwrite(m_timestamps.data(),
               sizeof(int64_t), m_timestamps.size(), m_file);
    }

    // Patch frame count in header (offset 38 into the struct)
    static constexpr long long FRAME_COUNT_OFFSET = 38;
    _fseeki64(m_file, FRAME_COUNT_OFFSET, SEEK_SET);
    int32_t fc = (int32_t)m_framesWritten;
    fwrite(&fc, sizeof(fc), 1, m_file);

    fclose(m_file);
    m_file = nullptr;

    raduk::core::CoreLog("[SERWriter] Closed: %s  %u frames  %.1f MB\n",
           m_path.c_str(), m_framesWritten,
           m_bytesWritten / (1024.0 * 1024.0));

    WriteQualitySidecar(m_path, m_framesWritten, m_sharpnessScores, m_sky);

    m_timestamps.clear();
    m_sharpnessScores.clear();
    m_sky.Reset();
    return true;
}

void SERWriter::ZeroFill(char* dst, const char* src, size_t dstSize) {
    memset(dst, 0, dstSize);
    if (src) strncpy_s(dst, dstSize, src, dstSize - 1);
}

int64_t SERWriter::CurrentFileTime() {
    FILETIME ft, local;
    GetSystemTimeAsFileTime(&ft);
    FileTimeToLocalFileTime(&ft, &local);
    ULARGE_INTEGER ul;
    ul.LowPart  = local.dwLowDateTime;
    ul.HighPart = local.dwHighDateTime;
    return (int64_t)ul.QuadPart + FrameClock::kEpochOffset;
}

} // namespace raduk::core
