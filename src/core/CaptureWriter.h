#pragma once
// ============================================================
// CaptureWriter.h — Phase 106: what every capture format shares
//
// SERWriter used to own all of this, and none of it is about SER:
// where the scope pointed, how a stamp becomes a wall clock, and the
// quality sidecar. A second format would otherwise have lost the
// sidecar or carried a second definition of time, so it lives here and
// both writers use one copy.
//
// The interface sits on the writer thread beside a disk write, so a
// virtual call per frame costs nothing worth measuring. It is not the
// capture hot path that P5.2 was careful about.
// ============================================================

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <windows.h>
#include "Timestamp.h"

namespace raduk::core {

// The imaging train, as both formats name it.
constexpr const char* kCaptureInstrument = "ZWO ASI678MC";
constexpr const char* kCaptureTelescope  = "Celestron 8SE";

// The sky figures for the observation record (phase 48).
// valid=false when nothing was noted -- no mount, or a run too short
// for a readout tick.
struct SkySummary {
    bool   valid = false;
    double altStart = 0, altEnd = 0, altMin = 0, altMax = 0;
    double azStart = 0, azEnd = 0;
    double airmassMean = 0, airmassMax = 0;
};

// Where the scope was pointing while a run went on. The first and last
// are kept along with the extremes, which is the right granularity --
// a planet's altitude moves a quarter of a degree a minute, so
// per-frame values would be the same number thousands of times.
struct SkyAccumulator {
    bool   seen    = false;
    int    samples = 0;
    double altStart = 0, altEnd = 0, altMin = 0, altMax = 0;
    double azStart  = 0, azEnd  = 0;
    double airmassSum = 0, airmassMax = 0;
    double seeingEnd  = 1.0, dispersion = 0.0;

    void Note(double altitudeDeg, double azimuthDeg, double airmass,
              double seeingFactor, double dispersionArcsec) {
        if (!seen) {
            altStart = altitudeDeg;
            altMin = altMax = altitudeDeg;
            azStart = azimuthDeg;
            seen = true;
        }
        altEnd = altitudeDeg;
        azEnd  = azimuthDeg;
        if (altitudeDeg < altMin) altMin = altitudeDeg;
        if (altitudeDeg > altMax) altMax = altitudeDeg;
        airmassSum += airmass;
        airmassMax  = (airmass > airmassMax) ? airmass : airmassMax;
        seeingEnd   = seeingFactor;
        dispersion  = dispersionArcsec;
        samples++;
    }

    SkySummary Summary() const {
        SkySummary s;
        if (!seen) return s;
        s.valid = true;
        s.altStart = altStart; s.altEnd = altEnd;
        s.altMin   = altMin;   s.altMax = altMax;
        s.azStart  = azStart;  s.azEnd  = azEnd;
        s.airmassMean = (samples > 0) ? airmassSum / samples : 0.0;
        s.airmassMax  = airmassMax;
        return s;
    }

    void Reset() { *this = SkyAccumulator{}; }
};

// A frame's capture stamp as a UTC wall clock, in SER ticks: 100 ns
// since 0001-01-01. Windows FILETIME counts from 1601-01-01, which is
// kEpochOffset ticks later.
//
// Phase 59 (review C2): correlate the QPC clock (FrameSlot capture
// stamps) with wall time ONCE, at Start(). Each entry is then derived
// from the frame's capture QPC -- not from when the writer thread
// drained it, which lagged up to ~10s with a full 512-slot backlog and
// made the trailer useless for WinJUPOS derotation.
class FrameClock {
public:
    static constexpr int64_t kEpochOffset = 504911232000000000LL;

    void Start() {
        m_qpcBaseUs = Timestamp::Now();
        m_ftBase    = NowUtcTicks();
    }

    // Base + (qpc - qpcBase). A zero timestamp (no capture stamp
    // available) falls back to now.
    int64_t TicksFor(uint64_t timestampUs) const {
        return (timestampUs != 0)
            ? m_ftBase + ((int64_t)timestampUs - (int64_t)m_qpcBaseUs) * 10
            : NowUtcTicks();
    }

    static int64_t NowUtcTicks() {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER ul;
        ul.LowPart  = ft.dwLowDateTime;
        ul.HighPart = ft.dwHighDateTime;
        return (int64_t)ul.QuadPart + kEpochOffset;
    }

private:
    uint64_t m_qpcBaseUs = 0;
    int64_t  m_ftBase    = 0;
};

// <base>_quality.txt beside any capture. Written by every format.
inline void WriteQualitySidecar(const std::string& capturePath,
                                uint32_t frames,
                                const std::vector<float>& scores,
                                const SkyAccumulator& sky) {
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
    if (!scores.empty()) {
        std::string qualPath = capturePath;
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
            fprintf(qf, "# Frames: %u\n", frames);
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
            if (sky.seen) {
                const double meanAir = (sky.samples > 0)
                                     ? sky.airmassSum / sky.samples : 0.0;
                fprintf(qf, "#\n");
                fprintf(qf, "# Scope position\n");
                fprintf(qf, "#   Altitude:    %.1f deg at start, %.1f at end (range %.1f to %.1f)\n",
                        sky.altStart, sky.altEnd, sky.altMin, sky.altMax);
                fprintf(qf, "#   Azimuth:     %.1f deg at start, %.1f at end\n",
                        sky.azStart, sky.azEnd);
                fprintf(qf, "#   Airmass:     %.3f mean, %.3f worst (1.000 = overhead)\n",
                        meanAir, sky.airmassMax);
                fprintf(qf, "#   Seeing cost: x%.2f vs the zenith (airmass^0.6)\n",
                        sky.seeingEnd);
                fprintf(qf, "#   Dispersion:  %.2f arcsec red-to-blue, uncorrected\n",
                        sky.dispersion);
                fprintf(qf, "#   Window:      %s\n",
                        sky.altMax >= 60.0 ? "PRIME (60 deg or above)"
                      : sky.altMax >= 45.0 ? "GOOD (45-60 deg)"
                      : sky.altMax >= 30.0 ? "MARGINAL (30-45 deg)"
                                            : "POOR (below 30 deg)");
                fprintf(qf, "#\n");
            }

            float sum = 0, peak = 0;
            for (float s : scores) {
                fprintf(qf, "%.2f\n", s);
                sum += s;
                if (s > peak) peak = s;
            }
            float mean = frames > 0 ? sum / frames : 0;
            fprintf(qf, "# Mean: %.2f  Peak: %.2f\n", mean, peak);
            fclose(qf);
            printf("[Capture] Quality file: %s  mean=%.1f peak=%.1f\n",
                   qualPath.c_str(), mean, peak);
        }
    }
}

// What CaptureManager records through. Everything a recording does to a
// file, and nothing about the file.
class ICaptureWriter {
public:
    virtual ~ICaptureWriter() = default;

    virtual bool Open(const std::string& path, int width, int height,
                      int bitDepth, bool bayerRGGB) = 0;

    // timestampUs: the frame's CAPTURE time (QPC microseconds, FrameSlot
    // convention). sharpness: 0-100 from FrameAnalyzer::sharpnessPct.
    // 0 = unknown.
    virtual bool WriteFrame(const uint8_t* data, size_t sizeBytes,
                            uint64_t timestampUs, float sharpness) = 0;

    // Finalize the file and write the quality sidecar.
    virtual bool Close() = 0;

    virtual const std::string& Path() const = 0;

    virtual void NoteSkyConditions(double altitudeDeg, double azimuthDeg,
                                   double airmass, double seeingFactor,
                                   double dispersionArcsec) = 0;

    // Read it BEFORE Close(), which resets it.
    virtual SkySummary SkyConditionsSummary() const = 0;
};

} // namespace raduk::core
