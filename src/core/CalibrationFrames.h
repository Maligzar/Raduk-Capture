// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once
// ============================================================
// CalibrationFrames.h — Phases 27 & 28: master dark and master flat
//
// A dark records what the sensor reports with no light reaching it:
// thermal signal and hot pixels. A flat records how unevenly the optics
// deliver light: vignetting, and dust shadows. Subtract the first and
// divide by the second and what is left is closer to the sky.
//
//   corrected = (raw - dark) * channelMean(flat) / flat
//
// WHY THESE ARE CAPTURED HERE RATHER THAN LOADED FROM A PNG. The specs
// for both phases said "load master dark PNG/FITS from disk". A PNG is
// debayered 8-bit RGB -- it has already been through the very pass this
// correction has to happen before, and its Bayer mosaic is gone. A dark
// that is not in the same raw layout and the same bit depth as the live
// frame cannot be subtracted from it at all. So masters are built in the
// app from the camera, stored as raw bytes with the metadata needed to
// tell whether they still apply, and written beside the captures.
//
// WHY THE FLAT IS NORMALISED PER BAYER POSITION. The four positions of
// an RGGB mosaic sit under different filters and have genuinely
// different sensitivities, so a flat's global mean is not the level any
// single position should be corrected towards. Dividing by one global
// mean would push every channel towards the same level and bake a
// colour cast into every frame -- correcting vignetting at the cost of
// white balance. Each position is normalised by its own mean instead,
// which corrects the shading and leaves the colour alone.
//
// WHAT THIS DOES NOT TOUCH. Correction is applied to the PREVIEW only.
// The recorded SER keeps the raw sensor data, because a stacker does
// this better with the full frame set and raw data stays useful when a
// master turns out to have been wrong. The sharpness metric also reads
// raw -- hot pixels do inflate a Laplacian, so dark-subtracting before
// analysis would genuinely improve focusing, but it would also move the
// capture gate and the SHARP_CEIL scale under a system that was tuned
// against raw frames. That is a change to make deliberately, measured,
// not as a side effect of this.
// ============================================================

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

namespace raduk::core {

class CalibrationFrames {
public:
    struct Meta {
        int    width      = 0;
        int    height     = 0;
        int    bitDepth   = 0;
        int    gain       = -1;
        double exposureMs = 0.0;
        float  tempC      = 0.0f;
        int    averaged   = 0;     // frames that went into the master
    };

    bool HasDark() const { return !m_dark.empty(); }
    bool HasFlat() const { return !m_flat.empty(); }
    const Meta& DarkMeta() const { return m_darkMeta; }
    const Meta& FlatMeta() const { return m_flatMeta; }

    void ClearDark() { m_dark.clear(); m_darkMeta = Meta{}; }
    void ClearFlat() { m_flat.clear(); m_flatNorm.clear(); m_flatMeta = Meta{}; }

    // ── Building a master ──────────────────────────────────────
    // Frames are summed in 64-bit and divided at the end. Averaging is
    // the point: a single dark carries its own read noise into every
    // frame it is subtracted from, which trades fixed-pattern noise for
    // random noise and can easily be a net loss.
    void Begin(bool dark, int w, int h, int bitDepth,
               int gain, double expMs, float tempC, int target) {
        m_accum.assign((size_t)w * h, 0);
        m_accumN     = 0;
        m_accumDark  = dark;
        m_accumTarget= (target < 1) ? 1 : target;
        m_pending    = Meta{ w, h, bitDepth, gain, expMs, tempC, 0 };
        m_building   = true;
    }
    bool Building() const { return m_building; }
    // Which master is being built. The caller needs this AFTER Finish()
    // too, to know which file to write, so Finish() leaves it set.
    bool BuildingDark()  const { return m_accumDark; }
    int  BuildProgress() const { return m_accumN; }
    int  BuildTarget()   const { return m_accumTarget; }

    // Returns true when the last needed frame has been taken.
    bool AddFrame(const uint8_t* data, int w, int h, int bitDepth) {
        if (!m_building || !data) return false;
        // A format change mid-build invalidates everything collected so
        // far; silently mixing sizes would produce a master that looks
        // valid and corrupts every frame it touches.
        if (w != m_pending.width || h != m_pending.height ||
            bitDepth != m_pending.bitDepth) {
            m_building = false;
            m_accum.clear();
            return false;
        }
        const size_t n = (size_t)w * h;
        if (bitDepth == 16) {
            const uint16_t* s = reinterpret_cast<const uint16_t*>(data);
            for (size_t i = 0; i < n; i++) m_accum[i] += s[i];
        } else {
            for (size_t i = 0; i < n; i++) m_accum[i] += data[i];
        }
        m_accumN++;
        return m_accumN >= m_accumTarget;
    }

    // Collapse the accumulator into a master. Returns false if nothing
    // was collected.
    bool Finish() {
        if (!m_building || m_accumN <= 0) { m_building = false; return false; }
        const size_t n = m_accum.size();
        const int    bd = m_pending.bitDepth;
        std::vector<uint8_t> out(n * (bd == 16 ? 2 : 1));
        if (bd == 16) {
            uint16_t* d = reinterpret_cast<uint16_t*>(out.data());
            for (size_t i = 0; i < n; i++)
                d[i] = (uint16_t)(m_accum[i] / (uint64_t)m_accumN);
        } else {
            for (size_t i = 0; i < n; i++)
                out[i] = (uint8_t)(m_accum[i] / (uint64_t)m_accumN);
        }
        m_pending.averaged = m_accumN;
        if (m_accumDark) { m_dark = std::move(out); m_darkMeta = m_pending; }
        else             { m_flat = std::move(out); m_flatMeta = m_pending;
                           BuildFlatNorm(); }
        m_building = false;
        m_accum.clear();
        return true;
    }

    void Abort() { m_building = false; m_accum.clear(); }

    // ── Validity ───────────────────────────────────────────────
    // Returns nullptr when the master applies, or a reason why not.
    // Geometry is a hard stop; gain, exposure and temperature are
    // warnings, because a dark taken at a different gain is wrong but
    // still better than nothing and the user may know why.
    const char* DarkProblem(int w, int h, int bitDepth,
                            int gain, double expMs) const {
        return Problem(m_dark, m_darkMeta, w, h, bitDepth, gain, expMs);
    }
    const char* FlatProblem(int w, int h, int bitDepth) const {
        return Problem(m_flat, m_flatMeta, w, h, bitDepth, -1, 0.0);
    }

    // Whether the master is actually being USED, which is a different
    // question from whether anything is wrong with it. Geometry is the
    // only hard gate: a dark taken at another gain is still subtracted,
    // because it is usually better than nothing and the user may have a
    // reason. Reporting a soft warning as though the correction were off
    // would tell someone their preview is raw when it is not.
    bool DarkApplies(int w, int h, int bitDepth) const {
        return !m_dark.empty() && Fits(m_darkMeta, w, h, bitDepth);
    }
    bool FlatApplies(int w, int h, int bitDepth) const {
        return !m_flatNorm.empty() && Fits(m_flatMeta, w, h, bitDepth);
    }

    // ── Applying ───────────────────────────────────────────────
    // raw and out may not overlap. Returns true if anything was done;
    // false means the caller should display the raw frame untouched.
    bool Apply(const uint8_t* raw, uint8_t* out,
               int w, int h, int bitDepth) const {
        if (!raw || !out) return false;
        const bool useDark = !m_dark.empty() && Fits(m_darkMeta, w, h, bitDepth);
        const bool useFlat = !m_flatNorm.empty() && Fits(m_flatMeta, w, h, bitDepth);
        if (!useDark && !useFlat) return false;

        const size_t n = (size_t)w * h;
        if (bitDepth == 16) {
            const uint16_t* s = reinterpret_cast<const uint16_t*>(raw);
            const uint16_t* d = reinterpret_cast<const uint16_t*>(m_dark.data());
            uint16_t*       o = reinterpret_cast<uint16_t*>(out);
            for (size_t i = 0; i < n; i++) {
                int v = s[i];
                if (useDark) v -= d[i];
                if (v < 0) v = 0;              // clamp, never wrap
                if (useFlat) v = (int)(v * m_flatNorm[i]);
                o[i] = (uint16_t)(v > 65535 ? 65535 : v);
            }
        } else {
            const uint8_t* d = m_dark.data();
            for (size_t i = 0; i < n; i++) {
                int v = raw[i];
                if (useDark) v -= d[i];
                if (v < 0) v = 0;
                if (useFlat) v = (int)(v * m_flatNorm[i]);
                out[i] = (uint8_t)(v > 255 ? 255 : v);
            }
        }
        return true;
    }

    // ── Disk ───────────────────────────────────────────────────
    // One file per master, raw bytes behind a fixed header. Not PNG and
    // not FITS: what has to survive is the exact sensor layout and the
    // metadata that says whether it still applies.
    bool SaveMaster(bool dark, const std::string& path) const {
        const std::vector<uint8_t>& buf = dark ? m_dark : m_flat;
        const Meta& m = dark ? m_darkMeta : m_flatMeta;
        if (buf.empty()) return false;
        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) return false;
        const char magic[8] = { 'R','D','K','C','A','L','0','1' };
        fwrite(magic, 1, 8, f);
        const int32_t kind = dark ? 0 : 1;
        fwrite(&kind, sizeof(kind), 1, f);
        fwrite(&m, sizeof(Meta), 1, f);
        fwrite(buf.data(), 1, buf.size(), f);
        fclose(f);
        return true;
    }

    bool LoadMaster(const std::string& path) {
        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
        char magic[8] = {};
        int32_t kind = -1;
        Meta m{};
        bool ok = fread(magic, 1, 8, f) == 8
               && memcmp(magic, "RDKCAL01", 8) == 0
               && fread(&kind, sizeof(kind), 1, f) == 1
               && fread(&m, sizeof(Meta), 1, f) == 1
               && m.width > 0 && m.height > 0
               && (m.bitDepth == 8 || m.bitDepth == 16)
               && (kind == 0 || kind == 1);
        if (ok) {
            const size_t want = (size_t)m.width * m.height
                              * (m.bitDepth == 16 ? 2 : 1);
            std::vector<uint8_t> buf(want);
            ok = fread(buf.data(), 1, want, f) == want;
            if (ok) {
                if (kind == 0) { m_dark = std::move(buf); m_darkMeta = m; }
                else { m_flat = std::move(buf); m_flatMeta = m; BuildFlatNorm(); }
            }
        }
        fclose(f);
        return ok;
    }

private:
    static bool Fits(const Meta& m, int w, int h, int bitDepth) {
        return m.width == w && m.height == h && m.bitDepth == bitDepth;
    }

    static const char* Problem(const std::vector<uint8_t>& buf, const Meta& m,
                               int w, int h, int bitDepth,
                               int gain, double expMs) {
        if (buf.empty()) return "none loaded";
        if (m.width != w || m.height != h) return "wrong resolution";
        if (m.bitDepth != bitDepth)        return "wrong bit depth";
        if (gain >= 0 && m.gain >= 0 && m.gain != gain)
            return "taken at a different gain";
        if (expMs > 0.0 && m.exposureMs > 0.0 &&
            (m.exposureMs < expMs * 0.9 || m.exposureMs > expMs * 1.1))
            return "taken at a different exposure";
        return nullptr;
    }

    // Per-Bayer-position reciprocal gain, precomputed so Apply() is a
    // multiply rather than a divide per pixel.
    void BuildFlatNorm() {
        m_flatNorm.clear();
        if (m_flat.empty()) return;
        const int w = m_flatMeta.width, h = m_flatMeta.height;
        const int bd = m_flatMeta.bitDepth;
        const size_t n = (size_t)w * h;

        auto at = [&](size_t i) -> double {
            return (bd == 16)
                ? (double)reinterpret_cast<const uint16_t*>(m_flat.data())[i]
                : (double)m_flat[i];
        };

        // Four means, one per mosaic position.
        double sum[4] = {0,0,0,0};
        uint64_t cnt[4] = {0,0,0,0};
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const int q = (y & 1) * 2 + (x & 1);
                sum[q] += at((size_t)y * w + x);
                cnt[q]++;
            }
        }
        double mean[4];
        for (int q = 0; q < 4; q++)
            mean[q] = cnt[q] ? sum[q] / (double)cnt[q] : 0.0;

        m_flatNorm.resize(n, 1.0f);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const size_t i = (size_t)y * w + x;
                const int    q = (y & 1) * 2 + (x & 1);
                const double v = at(i);
                // A near-zero flat pixel means no light got there at all
                // — a blocked corner, or a flat taken with the cap on.
                // Dividing by it would explode that pixel to full scale,
                // so it is left uncorrected instead.
                m_flatNorm[i] = (v > 1.0 && mean[q] > 1.0)
                              ? (float)(mean[q] / v) : 1.0f;
                if (m_flatNorm[i] > 4.0f) m_flatNorm[i] = 4.0f;  // sanity cap
            }
        }
    }

    std::vector<uint8_t> m_dark, m_flat;
    std::vector<float>   m_flatNorm;
    Meta m_darkMeta, m_flatMeta;

    // Accumulator for a build in progress
    std::vector<uint64_t> m_accum;
    Meta m_pending;
    int  m_accumN = 0, m_accumTarget = 0;
    bool m_accumDark = true;
    bool m_building  = false;
};

} // namespace raduk::core
