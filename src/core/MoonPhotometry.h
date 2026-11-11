#pragma once
// ============================================================
//  MoonPhotometry.h -- phase 141.2: a mutual event's light curve
//
//  From an event recording (141.1), the flux of the two moons of the
//  event together and of a third moon as the reference, frame by frame,
//  binned and dated, in the form IMCCE's PHEMU campaign asks for: an
//  ASCII <ID>.dat of "date flux flux" lines, dates in decimal Julian
//  days UTC, at most 10,000 lines, sampled between 5 a second and one
//  every 2 seconds (IMCCE's observation sheet and guide).
//
//  How it measures, and why:
//    - Aperture photometry on the raw Bayer mosaic. Every pixel counts,
//      whatever its colour: the sum is a broad-band flux, which is what
//      an unfiltered or UV/IR-cut recording is. No debayering, so no
//      interpolation enters the numbers.
//    - The background is the median of an annulus round each aperture,
//      subtracted per pixel, so Jupiter's glow and the sky drop out.
//    - The two moons of the event are measured TOGETHER, in one circle
//      round their midpoint wide enough to hold both. That is the only
//      measurement that stays valid as an occultation merges them into
//      one blob, and it is what IMCCE's own example (.dat "flux sat1&3")
//      and type 0 / type 2 on the sheet describe.
//    - Each moon is re-centred every frame while they are apart; once
//      they merge, both follow the merged blob's centroid. The mount's
//      drift and the field's rotation are followed that way.
//    - A frame with any saturated pixel inside an aperture is dropped,
//      and counted: IMCCE's notes say the satellites must never saturate.
//    - Times: each frame's timestamp is the exposure midpoint by the PC
//      clock (CaptureWriter). The clock offsets measured before and after
//      (141.1's sheet) are interpolated linearly across the run and
//      added, so the dates are UTC. Without them the dates are PC time,
//      and the result says so.
//
//  Pure, and free of Windows: tests/moon_photometry.cpp drives it with
//  synthetic frames, tools/phemu_lightcurve.cpp with a real SER.
// ============================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace raduk::core::moonphot {

// 16-bit left-aligned samples (SerReader::ToPlane16); 8-bit 255 becomes
// 65280, so this catches a clipped 8-bit frame as well.
inline constexpr uint16_t kSaturated = 65000;

struct Plane {
    const uint16_t* px = nullptr;
    int w = 0, h = 0;
    uint16_t at(int x, int y) const { return px[(size_t)y * w + x]; }
};

// ── Finding the moons on one frame ────────────────────────────

struct Source {
    double x = 0, y = 0;      // flux-weighted centroid, frame pixels
    double flux = 0;          // above the frame's background
    int    area = 0;          // pixels above threshold
    bool   saturated = false;
};

// Median and a robust spread (1.4826 x MAD) from a sparse sample.
inline void Background(const Plane& p, double& median, double& sigma) {
    std::vector<uint16_t> s;
    const int step = (std::max)(1, (int)std::sqrt((double)p.w * p.h / 40000.0));
    for (int y = 0; y < p.h; y += step)
        for (int x = 0; x < p.w; x += step) s.push_back(p.at(x, y));
    if (s.empty()) { median = 0; sigma = 1; return; }
    std::nth_element(s.begin(), s.begin() + s.size() / 2, s.end());
    median = s[s.size() / 2];
    std::vector<double> dev(s.size());
    for (size_t i = 0; i < s.size(); ++i) dev[i] = std::fabs((double)s[i] - median);
    std::nth_element(dev.begin(), dev.begin() + dev.size() / 2, dev.end());
    sigma = (std::max)(1.0, 1.4826 * dev[dev.size() / 2]);
}

// Connected regions above the background, brightest first. A region
// larger than maxArea is Jupiter (or a cloud of glare) and is left out;
// one smaller than minArea is a hot pixel or noise; one touching the
// frame's edge is cut off (often Jupiter's limb) and cannot be measured.
inline std::vector<Source> FindSources(const Plane& p, int maxArea = 4000, int minArea = 4,
                                       double kSigma = 6.0) {
    double bg, sg;
    Background(p, bg, sg);
    const double thr = bg + kSigma * sg;
    std::vector<uint8_t> seen((size_t)p.w * p.h, 0);
    std::vector<Source> out;
    std::vector<int> stack;
    for (int y = 0; y < p.h; ++y) {
        for (int x = 0; x < p.w; ++x) {
            const size_t i0 = (size_t)y * p.w + x;
            if (seen[i0] || p.px[i0] <= thr) continue;
            Source s;
            bool edge = false;
            double sx = 0, sy = 0, sf = 0;
            stack.assign(1, (int)i0);
            seen[i0] = 1;
            while (!stack.empty()) {
                const int i = stack.back(); stack.pop_back();
                const int cx = i % p.w, cy = i / p.w;
                const double v = (double)p.px[i] - bg;
                sx += v * cx; sy += v * cy; sf += v;
                ++s.area;
                if (cx == 0 || cy == 0 || cx == p.w - 1 || cy == p.h - 1) edge = true;
                if (p.px[i] >= kSaturated) s.saturated = true;
                const int nb[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
                for (auto& d : nb) {
                    const int nx = cx + d[0], ny = cy + d[1];
                    if (nx < 0 || ny < 0 || nx >= p.w || ny >= p.h) continue;
                    const size_t j = (size_t)ny * p.w + nx;
                    if (!seen[j] && p.px[j] > thr) { seen[j] = 1; stack.push_back((int)j); }
                }
            }
            if (edge || s.area < minArea || s.area > maxArea || sf <= 0) continue;
            s.x = sx / sf; s.y = sy / sf; s.flux = sf;
            out.push_back(s);
        }
    }
    std::sort(out.begin(), out.end(), [](const Source& a, const Source& b) { return a.flux > b.flux; });
    return out;
}

// A sensible aperture radius for a moon of this area: twice its
// equivalent radius, at least 4 px, so the wings of the seeing disc are
// inside it.
inline double RadiusFor(int area) {
    return (std::max)(4.0, 2.0 * std::sqrt((double)area / 3.14159265358979));
}

// ── Measuring ─────────────────────────────────────────────────

struct Measure {
    double flux = 0;
    double bgPerPx = 0;
    double cx = 0, cy = 0;     // centroid inside the aperture
    bool   saturated = false;
    bool   ok = false;         // the aperture fitted on the frame
};

// Sum in a circle of radius r at (x, y), less the median of the annulus
// [r + gap, r + gap + width] per pixel. Also the background-subtracted
// centroid inside the circle, for re-centring.
inline Measure Aperture(const Plane& p, double x, double y, double r, double gap = 2.0,
                        double width = 6.0) {
    Measure m;
    const double rOut = r + gap + width;
    if (x - rOut < 0 || y - rOut < 0 || x + rOut >= p.w || y + rOut >= p.h) return m;
    std::vector<uint16_t> ann;
    const int x0 = (int)std::floor(x - rOut), x1 = (int)std::ceil(x + rOut);
    const int y0 = (int)std::floor(y - rOut), y1 = (int)std::ceil(y + rOut);
    for (int yy = y0; yy <= y1; ++yy)
        for (int xx = x0; xx <= x1; ++xx) {
            const double d = std::hypot(xx - x, yy - y);
            if (d >= r + gap && d <= rOut) ann.push_back(p.at(xx, yy));
        }
    if (ann.size() < 8) return m;
    std::nth_element(ann.begin(), ann.begin() + ann.size() / 2, ann.end());
    m.bgPerPx = ann[ann.size() / 2];
    double sum = 0, sx = 0, sy = 0, wsum = 0;
    for (int yy = y0; yy <= y1; ++yy)
        for (int xx = x0; xx <= x1; ++xx) {
            if (std::hypot(xx - x, yy - y) > r) continue;
            const uint16_t v = p.at(xx, yy);
            if (v >= kSaturated) m.saturated = true;
            const double s = (double)v - m.bgPerPx;
            sum += s;
            if (s > 0) { sx += s * xx; sy += s * yy; wsum += s; }
        }
    m.flux = sum;
    m.cx = wsum > 0 ? sx / wsum : x;
    m.cy = wsum > 0 ? sy / wsum : y;
    m.ok = true;
    return m;
}

// ── The run ───────────────────────────────────────────────────

struct Config {
    double ax = 0, ay = 0;     // one moon of the event, frame pixels, first frame
    double bx = 0, by = 0;     // the other
    double rx = 0, ry = 0;     // the reference moon
    double radius = 0;         // 0: from the sources found near the given positions
    double binSec = 0;         // 0: chosen so the file stays within IMCCE's limits
    bool   haveOffsetBefore = false, haveOffsetAfter = false;
    double offsetBeforeSec = 0, offsetAfterSec = 0;   // UTC = PC + offset
    int    maxLines = 10000;   // IMCCE's limit per file
};

struct Bin {
    double jd = 0;             // UTC if offsets were given, else PC time
    double pair = 0, ref = 0;
    int    frames = 0;
};

struct Result {
    bool   ok = false;
    std::string why;
    std::vector<Bin> bins;
    int    framesUsed = 0, framesSaturated = 0, framesLost = 0;
    double radius = 0, binSec = 0;
    bool   timesCorrected = false;
    double minRatio = 0, baseRatio = 0;   // pair/ref, for a quick look at the depth
};

using FrameFn = std::function<bool(int64_t i, std::vector<uint16_t>& plane)>;
using TimeFn  = std::function<double(int64_t i)>;   // Julian date by the PC clock; 0 = unknown

// IMCCE's sampling window is 5 per second to one every 2 seconds. The
// bin is the shortest in that window that keeps the file within the line
// limit, and never shorter than one frame.
inline double ChooseBin(double durationSec, double frameSec, int maxLines) {
    double b = (std::max)(0.2, frameSec);
    if (durationSec / b > maxLines) b = durationSec / maxLines;
    return (std::min)(2.0, b);
}

inline Result Run(int64_t frames, int w, int h, const FrameFn& frame, const TimeFn& when,
                  Config cfg) {
    Result res;
    if (frames < 2) { res.why = "fewer than two frames"; return res; }
    const double jd0 = when(0), jd1 = when(frames - 1);
    if (jd0 <= 0 || jd1 <= jd0) { res.why = "the recording has no frame times (no SER trailer)"; return res; }
    const double runSec = (jd1 - jd0) * 86400.0;

    std::vector<uint16_t> buf;
    if (!frame(0, buf)) { res.why = "the first frame could not be read"; return res; }
    Plane p{buf.data(), w, h};
    if (cfg.radius <= 0) {
        // From whatever sources sit near the three positions given.
        const auto found = FindSources(p);
        int maxArea = 0;
        for (const auto& s : found) {
            const double da = std::hypot(s.x - cfg.ax, s.y - cfg.ay);
            const double db = std::hypot(s.x - cfg.bx, s.y - cfg.by);
            const double dr = std::hypot(s.x - cfg.rx, s.y - cfg.ry);
            if ((std::min)({da, db, dr}) < 15.0) maxArea = (std::max)(maxArea, s.area);
        }
        cfg.radius = RadiusFor(maxArea > 0 ? maxArea : 12);
    }
    res.radius = cfg.radius;
    const double r = cfg.radius;
    const double frameSec = runSec / (double)(frames - 1);
    res.binSec = cfg.binSec > 0 ? cfg.binSec : ChooseBin(runSec, frameSec, cfg.maxLines);

    double ax = cfg.ax, ay = cfg.ay, bx = cfg.bx, by = cfg.by, rx = cfg.rx, ry = cfg.ry;
    // B's motion relative to A, per frame, measured while they are apart
    // and carried through a merge, when the image cannot show it. Over the
    // minutes of an event the moons move in straight lines.
    double vx = 0, vy = 0;
    bool   haveV = false;
    double lastSepX = bx - ax, lastSepY = by - ay;
    Bin cur;
    long long curBin = -1;
    double sumPairBase = 0, sumRefBase = 0; int nBase = 0;
    double minRatio = 1e300;
    for (int64_t i = 0; i < frames; ++i) {
        if (i > 0 && !frame(i, buf)) { ++res.framesLost; continue; }
        p.px = buf.data();
        // Re-centre: each moon on its own while apart, both with the
        // merged blob once they touch.
        const double sep = std::hypot(bx - ax, by - ay);
        if (sep > 2.0 * r) {
            const Measure ma = Aperture(p, ax, ay, r), mb = Aperture(p, bx, by, r);
            if (ma.ok) { ax = ma.cx; ay = ma.cy; }
            if (mb.ok) { bx = mb.cx; by = mb.cy; }
            // The relative motion, smoothed: seeing moves each centroid a
            // little every frame.
            const double sx = bx - ax, sy = by - ay;
            const double dvx = sx - lastSepX, dvy = sy - lastSepY;
            if (!haveV) { vx = dvx; vy = dvy; haveV = (i > 0); }
            else { vx += 0.05 * (dvx - vx); vy += 0.05 * (dvy - vy); }
            lastSepX = sx; lastSepY = sy;
        } else {
            // Merged: follow the blob, and move B against A as it was
            // moving before.
            const double mx = (ax + bx) / 2, my = (ay + by) / 2;
            const Measure mm = Aperture(p, mx, my, sep / 2 + r);
            const double cx = mm.ok ? mm.cx : mx, cy = mm.ok ? mm.cy : my;
            const double sx = (bx - ax) + vx, sy = (by - ay) + vy;
            ax = cx - sx / 2; ay = cy - sy / 2;
            bx = cx + sx / 2; by = cy + sy / 2;
            lastSepX = sx; lastSepY = sy;
        }
        const Measure mr = Aperture(p, rx, ry, r);
        if (mr.ok) { rx = mr.cx; ry = mr.cy; }
        // The pair, in one circle wide enough for both.
        const double sepNow = std::hypot(bx - ax, by - ay);
        const Measure pair = Aperture(p, (ax + bx) / 2, (ay + by) / 2, sepNow / 2 + r);
        if (!pair.ok || !mr.ok) { ++res.framesLost; continue; }
        if (pair.saturated || mr.saturated) { ++res.framesSaturated; continue; }

        // The date: PC time, plus the offset interpolated across the run.
        double jd = when(i);
        if (jd <= 0) { ++res.framesLost; continue; }
        if (cfg.haveOffsetBefore || cfg.haveOffsetAfter) {
            const double f = (jd - jd0) / (jd1 - jd0);
            const double o0 = cfg.haveOffsetBefore ? cfg.offsetBeforeSec : cfg.offsetAfterSec;
            const double o1 = cfg.haveOffsetAfter ? cfg.offsetAfterSec : cfg.offsetBeforeSec;
            jd += (o0 + (o1 - o0) * f) / 86400.0;
            res.timesCorrected = true;
        }
        ++res.framesUsed;
        // Bins by time from the first frame, so each holds binSec of frames.
        // The 1e-4 of a bin absorbs the rounding of a Julian date near
        // 2.46 million (about 40 us), which otherwise puts a frame that sits
        // exactly on a bin edge in the bin before.
        const long long bi = (long long)std::floor((when(i) - jd0) * 86400.0 / res.binSec + 1e-4);
        auto flush = [&]() {
            if (cur.frames == 0) return;
            Bin b{cur.jd / cur.frames, cur.pair / cur.frames, cur.ref / cur.frames, cur.frames};
            res.bins.push_back(b);
            if (b.ref > 0) {
                const double ratio = b.pair / b.ref;
                minRatio = (std::min)(minRatio, ratio);
                // The first tenth of the run is the baseline, before the event.
                if (b.jd - jd0 < (jd1 - jd0) * 0.1) { sumPairBase += b.pair; sumRefBase += b.ref; ++nBase; }
            }
            cur = Bin{};
        };
        if (bi != curBin) { flush(); curBin = bi; }
        cur.jd += jd; cur.pair += pair.flux; cur.ref += mr.flux; ++cur.frames;
        if (i == frames - 1) flush();
    }
    if (res.bins.empty()) { res.why = "no frame could be measured (saturated, or the moons left the frame)"; return res; }
    if ((int)res.bins.size() > cfg.maxLines) res.bins.resize((size_t)cfg.maxLines);
    res.minRatio = minRatio;
    res.baseRatio = (nBase > 0 && sumRefBase > 0) ? sumPairBase / sumRefBase : 0;
    res.ok = true;
    return res;
}

// IMCCE's .dat: one line naming the columns, then "date flux flux".
inline std::string DatText(const Result& r) {
    std::string o = "  date(JD UTC)        flux pair      flux reference\n";
    char b[128];
    for (const Bin& x : r.bins) {
        snprintf(b, sizeof(b), "%.8f %16.1f %16.1f\n", x.jd, x.pair, x.ref);
        o += b;
    }
    return o;
}

// SER ticks (100 ns since 0001-01-01, UTC) to a Julian date.
inline double JdFromSerTicks(int64_t ticks) {
    if (ticks <= 0) return 0.0;
    return 1721425.5 + (double)ticks / 864000000000.0;
}

}  // namespace raduk::core::moonphot
