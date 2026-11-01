// ============================================================
// FITSWriter.cpp — one run as one FITS cube
// Out-of-line definitions for FITSWriter.h.
// ============================================================

#include "FITSWriter.h"

namespace raduk::core {

static fits::HeaderExtras ExtrasFor(const ObservationRecord& r) {
    fits::HeaderExtras x;
    x.instrument = kCaptureInstrument;
    x.telescope  = kCaptureTelescope;
    x.software   = "RadukCapture";
    if (!r.appVersion.empty()) x.software += " " + r.appVersion;
    x.pixelUm    = r.pixelUm;
    return x;
}

bool FITSWriter::Open(const std::string& path, int width, int height,
                      int bitDepth, bool bayerRGGB) {
    if (m_file) Close();
    if (width <= 0 || height <= 0) return false;

    // A bare record: this header only has to identify the file until
    // Close() replaces it.
    ObservationRecord r;
    r.width    = width;
    r.height   = height;
    r.bitDepth = bitDepth;
    r.colour   = bayerRGGB ? "RGGB" : "MONO";

    const std::string header =
        fits::BuildHeader(r, ExtrasFor(r), fits::RunTimes{}, 0);
    if (header.empty()) {
        printf("[FITSWriter] Refused %d-bit: %s\n", bitDepth, path.c_str());
        return false;
    }

    m_file = fopen(path.c_str(), "wb");
    if (!m_file) {
        printf("[FITSWriter] Failed to open: %s\n", path.c_str());
        return false;
    }
    if (fwrite(header.data(), 1, header.size(), m_file) != header.size()) {
        printf("[FITSWriter] Failed to write header\n");
        fclose(m_file); m_file = nullptr;
        return false;
    }

    m_path          = path;
    m_record        = r;
    m_frameBytes    = (size_t)width * height * (bitDepth / 8);
    m_framesWritten = 0;
    m_timestamps.clear();
    m_sharpnessScores.clear();
    m_clock.Start();
    if (bitDepth == 16) m_swap.resize(m_frameBytes);

    printf("[FITSWriter] Opened: %s  %dx%d  %d-bit\n",
           path.c_str(), width, height, bitDepth);
    return true;
}

bool FITSWriter::WriteFrame(const uint8_t* data, size_t sizeBytes,
                            uint64_t timestampUs, float sharpness) {
    if (!m_file || !data || sizeBytes != m_frameBytes) return false;

    const uint8_t* out = data;
    if (m_record.bitDepth == 16) {
        fits::ToFits16(data, m_swap.data(), sizeBytes);
        out = m_swap.data();
    }
    if (fwrite(out, 1, sizeBytes, m_file) != sizeBytes) {
        printf("[FITSWriter] Write error on frame %u\n", m_framesWritten);
        return false;
    }

    m_framesWritten++;
    m_timestamps.push_back(m_clock.TicksFor(timestampUs));
    m_sharpnessScores.push_back(sharpness);
    return true;
}

bool FITSWriter::Close() {
    if (!m_file) return false;

    // The image's padding, then the table, then its padding: zeros in
    // both cases, because this is data rather than a header.
    bool ok = true;
    const auto zeros = [&](size_t n) {
        static const uint8_t block[fits::kBlockBytes] = {};
        ok = (n == 0 || fwrite(block, 1, n, m_file) == n) && ok;
    };

    zeros(fits::PaddingFor((uint64_t)m_framesWritten * m_frameBytes));

    const std::string tableHeader = fits::BuildTableHeader(m_framesWritten);
    ok = fwrite(tableHeader.data(), 1, tableHeader.size(), m_file)
             == tableHeader.size() && ok;

    std::vector<uint8_t> rows(m_timestamps.size() * fits::kTableRowBytes);
    for (size_t i = 0; i < m_timestamps.size(); ++i)
        fits::PutTableRow(rows.data() + i * fits::kTableRowBytes,
                          fits::MjdFromTicks(m_timestamps[i]),
                          m_sharpnessScores[i]);
    if (!rows.empty())
        ok = fwrite(rows.data(), 1, rows.size(), m_file) == rows.size() && ok;
    zeros(fits::PaddingFor(rows.size()));

    // The whole record, in the space Open() reserved.
    fits::RunTimes t;
    if (!m_timestamps.empty()) {
        t.hasFrames     = true;
        t.firstMidTicks = m_timestamps.front();
        t.lastMidTicks  = m_timestamps.back();
    }
    const std::string header = fits::BuildHeader(
        m_record, ExtrasFor(m_record), t, m_framesWritten);
    fseek(m_file, 0, SEEK_SET);
    ok = !header.empty() &&
         fwrite(header.data(), 1, header.size(), m_file) == header.size() && ok;

    ok = fclose(m_file) == 0 && ok;
    m_file = nullptr;

    if (!ok) printf("[FITSWriter] Close failed: %s\n", m_path.c_str());
    printf("[FITSWriter] Closed: %s  %u frames\n",
           m_path.c_str(), m_framesWritten);

    WriteQualitySidecar(m_path, m_framesWritten, m_sharpnessScores, m_sky);

    m_timestamps.clear();
    m_sharpnessScores.clear();
    m_sky.Reset();
    return ok;
}

} // namespace raduk::core
