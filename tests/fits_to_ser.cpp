// ============================================================
//  2026-11-09: rescuing a FITS cube as SER
//
//  PIPP refused a 27 GB Saturn cube ("FITS file is too small", one
//  frame). FitsToSer.h converts a cube to SER and recovers the frame
//  count from the file size when the recording was never closed.
//  Checked on small cubes built with the app's own header code:
//    1. a closed cube: NAXIS3 is used, the FRAMES table after it ignored
//    2. a crashed cube: NAXIS3 = 0, every whole frame recovered, a
//       partial last frame dropped
//    3. samples: big-endian BZERO-offset 16-bit becomes little-endian
//    4. (2026-11-10) the FRAMES table's MJD_MID times become the SER's
//       timestamp trailer, to the tick; a cube without one gets none
//       unsigned, exactly; the SER header says so
// ============================================================

#include "core/FitsToSer.h"
#include "core/FITSFormat.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-58s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

// A W x H 16-bit cube of `frames` frames, header claiming `claimed`,
// sample value = frame*1000 + index, optionally a trailing partial frame
// and a fake table after the data.
static void MakeCube(const char* path, int w, int h, int frames, int64_t claimed,
                     bool partial, bool table) {
    ObservationRecord r;
    r.width = w; r.height = h; r.bitDepth = 16; r.colour = "RGGB"; r.target = "Saturn";
    const std::string hdr = fits::BuildHeader(r, fits::HeaderExtras{}, fits::RunTimes{}, claimed);
    FILE* f = fopen(path, "wb");
    fwrite(hdr.data(), 1, hdr.size(), f);
    std::vector<uint8_t> le(w * h * 2), be(w * h * 2);
    for (int i = 0; i < frames + (partial ? 1 : 0); ++i) {
        for (int k = 0; k < w * h; ++k) {
            const uint16_t v = (uint16_t)(i * 1000 + k);
            le[2 * k] = (uint8_t)v; le[2 * k + 1] = (uint8_t)(v >> 8);
        }
        fits::ToFits16(le.data(), be.data(), le.size());
        fwrite(be.data(), 1, (partial && i == frames) ? be.size() / 2 : be.size(), f);
    }
    if (table) { std::vector<uint8_t> pad(2880 * 3, 0x41); fwrite(pad.data(), 1, pad.size(), f); }
    fclose(f);
}

static std::vector<uint8_t> ReadAll(const char* p) {
    FILE* f = fopen(p, "rb");
    std::vector<uint8_t> v;
    if (!f) return v;
    fseek(f, 0, SEEK_END); v.resize((size_t)ftell(f)); fseek(f, 0, SEEK_SET);
    if (!v.empty()) fread(v.data(), 1, v.size(), f);
    fclose(f);
    return v;
}
static int32_t I32(const std::vector<uint8_t>& v, size_t at) { int32_t x; memcpy(&x, &v[at], 4); return x; }

int main() {
    printf("\n=== FITS cube to SER ===\n");
    const int W = 8, H = 4;

    printf("\n1. A closed cube\n");
    MakeCube("t_closed.fits", W, H, 5, 5, false, true);
    Check(fits2ser::Convert("t_closed.fits", "t_closed.ser"), "converts");
    {
        const auto s = ReadAll("t_closed.ser");
        Check(s.size() == 178 + 5 * W * H * 2, "five frames, the table left out",
              std::to_string(s.size()));
        Check(I32(s, 38) == 5 && I32(s, 26) == W && I32(s, 30) == H && I32(s, 34) == 16,
              "header: count, size and depth");
        Check(I32(s, 18) == 8 && I32(s, 22) == 0,
              "RGGB, and LittleEndian 0 over little-endian samples, as PIPP reads it");
        Check(memcmp(&s[0], "LUCAM-RECORDER", 14) == 0, "SER signature");
    }

    printf("\n2. A cube never closed\n");
    MakeCube("t_crash.fits", W, H, 7, 0, true, false);
    {
        FILE* f = fopen("t_crash.fits", "rb");
        const auto h = fits2ser::ReadHeader(f);
        fseek(f, 0, SEEK_END);
        const auto p = fits2ser::PlanFor(h, ftell(f));
        fclose(f);
        Check(h.ok && h.n == 0, "header read: it says 0 frames");
        Check(p.recovered && p.frames == 7, "7 whole frames recovered from the size",
              std::to_string(p.frames));
    }
    Check(fits2ser::Convert("t_crash.fits", "t_crash.ser"), "converts");
    {
        const auto s = ReadAll("t_crash.ser");
        Check(I32(s, 38) == 7 && s.size() == 178 + 7 * W * H * 2, "the partial last frame is dropped");
    }

    printf("\n3. Samples\n");
    {
        const auto s = ReadAll("t_crash.ser");
        bool exact = true;
        for (int i = 0; i < 7 && exact; ++i)
            for (int k = 0; k < W * H; ++k) {
                const size_t at = 178 + ((size_t)i * W * H + k) * 2;
                const uint16_t v = (uint16_t)(s[at] | (s[at + 1] << 8));
                if (v != (uint16_t)(i * 1000 + k)) { exact = false; break; }
            }
        Check(exact, "every sample comes back exactly");
    }
    {
        FILE* f = fopen("t_closed.fits", "rb");
        const auto h = fits2ser::ReadHeader(f);
        fclose(f);
        Check(h.bayer == "RGGB" && h.object == "Saturn" && h.headerBytes % 2880 == 0,
              "Bayer pattern, target and header length read", h.bayer + " " + h.object);
    }
    printf("\n4. Frame times\n");
    {
        // A cube as FITSWriter closes one: frames, padding, the FRAMES table.
        ObservationRecord r;
        r.width = W; r.height = H; r.bitDepth = 16; r.colour = "RGGB"; r.target = "Saturn";
        const std::string hdr = fits::BuildHeader(r, fits::HeaderExtras{}, fits::RunTimes{}, 3);
        FILE* f = fopen("t_times.fits", "wb");
        fwrite(hdr.data(), 1, hdr.size(), f);
        std::vector<uint8_t> frame(W * H * 2, 0);
        for (int i = 0; i < 3; ++i) fwrite(frame.data(), 1, frame.size(), f);
        std::vector<uint8_t> pad(fits::PaddingFor(3 * frame.size()), 0);
        fwrite(pad.data(), 1, pad.size(), f);
        const std::string th = fits::BuildTableHeader(3);
        fwrite(th.data(), 1, th.size(), f);
        // 2026-11-08 01:04:37.250 UTC, then 25 ms apart.
        const int64_t t0 = (int64_t)739927 * fits::kTicksPerDay + (int64_t)(3877.25 * 1e7);
        uint8_t rows[3 * fits::kTableRowBytes];
        for (int i = 0; i < 3; ++i)
            fits::PutTableRow(rows + i * fits::kTableRowBytes,
                              fits::MjdFromTicks(t0 + i * 250000LL), 10.0f);
        fwrite(rows, 1, sizeof rows, f);
        std::vector<uint8_t> pad2(fits::PaddingFor(sizeof rows), 0);
        fwrite(pad2.data(), 1, pad2.size(), f);
        fclose(f);
    }
    Check(fits2ser::Convert("t_times.fits", "t_times.ser"), "converts");
    {
        const auto s = ReadAll("t_times.ser");
        const size_t trailer = 178 + 3 * W * H * 2;
        Check(s.size() == trailer + 3 * 8, "three frames and three timestamps", std::to_string(s.size()));
        if (s.size() == trailer + 24) {
            int64_t a, b, c;
            memcpy(&a, &s[trailer], 8); memcpy(&b, &s[trailer + 8], 8); memcpy(&c, &s[trailer + 16], 8);
            const int64_t t0 = (int64_t)739927 * fits::kTicksPerDay + (int64_t)(3877.25 * 1e7);
            // A double MJD holds about 1 us at this date; the trailer is
            // 100 ns ticks, so agreement to 20 us is the round trip.
            Check(llabs(a - t0) < 200, "the first time, to within 20 us", std::to_string(a - t0));
            Check(llabs((b - a) - 250000) < 200 && llabs((c - b) - 250000) < 200, "25 ms apart");
        }
    }
    {
        const auto s = ReadAll("t_closed.ser");
        Check(s.size() == 178 + 5 * W * H * 2, "a cube with no real FRAMES table gets no trailer");
    }
    for (const char* p : {"t_closed.fits", "t_closed.ser", "t_crash.fits", "t_crash.ser",
                          "t_times.fits", "t_times.ser"}) remove(p);

    printf("\n%s\n", g_fail ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return g_fail ? 1 : 0;
}
