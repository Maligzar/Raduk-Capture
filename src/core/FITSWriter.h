#pragma once
// ============================================================
// FITSWriter.h — Phase 106: one run as one FITS cube
//
// The bytes are FITSFormat.h's. This is only where they go:
//
//   [reserved header][frames...][padding][FRAMES table][padding]
//
// Open() writes a valid header with NAXIS3 = 0, and Close() rewrites
// it in place with the whole record. A run that dies in between leaves
// frames after a header that identifies them, as a SER does with its
// count of 0. Nothing here recovers such a file.
//
// The stamps are held in memory until Close(), as SERWriter holds its
// trailer: the table has to follow the image, whose size is not known
// until the run ends.
// ============================================================

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "CaptureWriter.h"
#include "FITSFormat.h"

namespace raduk::core {

class FITSWriter : public ICaptureWriter {
public:
    FITSWriter() = default;
    ~FITSWriter() override { if (IsOpen()) Close(); }

    FITSWriter(const FITSWriter&) = delete;
    FITSWriter& operator=(const FITSWriter&) = delete;

    bool IsOpen() const { return m_file != nullptr; }
    uint32_t FramesWritten() const { return m_framesWritten; }
    const std::string& Path() const override { return m_path; }

    bool Open(const std::string& path, int width, int height,
              int bitDepth, bool bayerRGGB) override;

    // Refuses a frame of the wrong size. A SER tolerates one until
    // something reads it; here NAXIS would describe a different file.
    bool WriteFrame(const uint8_t* data, size_t sizeBytes,
                    uint64_t timestampUs, float sharpness = 0.0f) override;

    bool Close() override;

    void NoteObservation(const ObservationRecord& r) override { m_record = r; }

    void NoteSkyConditions(double altitudeDeg, double azimuthDeg,
                           double airmass, double seeingFactor,
                           double dispersionArcsec) override {
        m_sky.Note(altitudeDeg, azimuthDeg, airmass, seeingFactor,
                   dispersionArcsec);
    }

    SkySummary SkyConditionsSummary() const override { return m_sky.Summary(); }

private:
    FILE*                 m_file = nullptr;
    std::string           m_path;
    size_t                m_frameBytes    = 0;
    uint32_t              m_framesWritten = 0;
    ObservationRecord     m_record;
    FrameClock            m_clock;
    std::vector<int64_t>  m_timestamps;
    std::vector<float>    m_sharpnessScores;
    std::vector<uint8_t>  m_swap;               // the slot is const
    SkyAccumulator        m_sky;
};

} // namespace raduk::core
