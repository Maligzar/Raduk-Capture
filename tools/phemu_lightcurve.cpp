// phemu_lightcurve -- a mutual event's light curve for IMCCE (phase 141.2)
//
// Step 1, find the moons on the recording:
//   phemu_lightcurve <event.ser> --list [--frame N]
// lists the moons it finds on frame N (default 0): index, x, y, flux,
// size. Jupiter and anything cut by the frame's edge are left out.
//
// Step 2, measure:
//   phemu_lightcurve <event.ser> --pair XA YA XB YB --ref XR YR
//                    [--sheet <ID>.txt] [--radius PX] [--bin SEC] [--out FILE]
//   or, by the indexes --list printed:
//   phemu_lightcurve <event.ser> --pair-idx IA IB --ref-idx IR [...]
//
// --pair: the two moons of the event (measured together, as IMCCE's
// "flux sat1&3"); --ref: a third moon, the reference. --sheet: the
// observation sheet Shift+J wrote (141.1); the clock offsets on it are
// applied, so the dates are UTC, a line saying so is added to its
// comments, and the .dat is written beside it as <ID>.dat. Without
// --sheet the dates are PC-clock times and the tool says so.
//
// Writes IMCCE's .dat: a header line, then "JD flux_pair flux_ref",
// binned between 0.2 and 2 s, at most 10,000 lines.
//
// Build: CMake builds it beside the app (build\Release\phemu_lightcurve.exe),
// or from a Developer Command Prompt:
//   cl /O2 /EHsc /std:c++20 /I src tools\phemu_lightcurve.cpp

#include "core/MoonPhotometry.h"
#include "core/SerReader.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace raduk::core;

static void Usage() {
    fprintf(stderr,
        "usage: phemu_lightcurve <event.ser> --list [--frame N]\n"
        "       phemu_lightcurve <event.ser> --pair XA YA XB YB --ref XR YR [--sheet ID.txt]\n"
        "                        [--radius PX] [--bin SEC] [--out FILE]\n"
        "       phemu_lightcurve <event.ser> --pair-idx IA IB --ref-idx IR [same options]\n");
}

// "Clock before: PC clock 0.231 s behind UTC (...)" -> +0.231;
// "... ahead of UTC" -> negative; "clock not checked" -> none.
static bool ParseClockLine(const std::string& line, const char* label, double& offset) {
    if (line.rfind(label, 0) != 0) return false;
    const size_t at = line.find("PC clock ");
    if (at == std::string::npos) return false;
    const double v = atof(line.c_str() + at + 9);
    if (line.find(" behind UTC") != std::string::npos) { offset = v; return true; }
    if (line.find(" ahead of UTC") != std::string::npos) { offset = -v; return true; }
    return false;
}

int main(int argc, char** argv) {
    if (argc < 3) { Usage(); return 2; }
    const std::string serPath = argv[1];
    bool list = false;
    int64_t listFrame = 0;
    bool havePair = false, haveRef = false, pairIdx = false, refIdx = false;
    double xa = 0, ya = 0, xb = 0, yb = 0, xr = 0, yr = 0;
    int ia = -1, ib = -1, ir = -1;
    std::string sheetPath, outPath;
    moonphot::Config cfg;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](int n) { if (i + n >= argc) { Usage(); exit(2); } };
        if (a == "--list") list = true;
        else if (a == "--frame")    { need(1); listFrame = atoll(argv[++i]); }
        else if (a == "--pair")     { need(4); xa = atof(argv[++i]); ya = atof(argv[++i]); xb = atof(argv[++i]); yb = atof(argv[++i]); havePair = true; }
        else if (a == "--ref")      { need(2); xr = atof(argv[++i]); yr = atof(argv[++i]); haveRef = true; }
        else if (a == "--pair-idx") { need(2); ia = atoi(argv[++i]); ib = atoi(argv[++i]); pairIdx = true; }
        else if (a == "--ref-idx")  { need(1); ir = atoi(argv[++i]); refIdx = true; }
        else if (a == "--sheet")    { need(1); sheetPath = argv[++i]; }
        else if (a == "--radius")   { need(1); cfg.radius = atof(argv[++i]); }
        else if (a == "--bin")      { need(1); cfg.binSec = atof(argv[++i]); }
        else if (a == "--out")      { need(1); outPath = argv[++i]; }
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); Usage(); return 2; }
    }

    ser::Reader rd;
    if (!rd.Open(serPath)) { fprintf(stderr, "cannot read %s: %s\n", serPath.c_str(), rd.GetInfo().error.c_str()); return 1; }
    const ser::Info& in = rd.GetInfo();
    printf("%s: %dx%d, %d-bit %s, %lld frames%s\n", serPath.c_str(), in.width, in.height, in.depth,
           ser::ColorName(in.colorId), (long long)in.frames,
           in.hasTimestamps ? "" : ", NO frame times");

    std::vector<uint8_t> raw;
    auto frame = [&](int64_t i, std::vector<uint16_t>& plane) {
        if (!rd.ReadFrame(i, raw)) return false;
        ser::ToPlane16(raw.data(), in, plane);
        return true;
    };
    auto when = [&](int64_t i) { return moonphot::JdFromSerTicks(rd.Timestamp(i)); };

    // The moons on one frame, for --list and for --*-idx.
    auto sourcesOn = [&](int64_t f) {
        std::vector<uint16_t> plane;
        if (!frame(f, plane)) return std::vector<moonphot::Source>{};
        return moonphot::FindSources(moonphot::Plane{plane.data(), in.width, in.height});
    };
    if (list || pairIdx || refIdx) {
        const auto src = sourcesOn(list ? listFrame : 0);
        if (list) {
            printf("Moons on frame %lld (brightest first; Jupiter and edge-cut regions left out):\n",
                   (long long)listFrame);
            printf("  idx        x        y           flux   size  note\n");
            for (size_t k = 0; k < src.size(); ++k)
                printf("  %3zu %8.1f %8.1f %14.0f %6d  %s\n", k, src[k].x, src[k].y, src[k].flux,
                       src[k].area, src[k].saturated ? "CLIPPED: lower the gain or exposure" : "");
            if (src.empty()) printf("  none found\n");
            return 0;
        }
        auto pick = [&](int k, double& x, double& y) {
            if (k < 0 || k >= (int)src.size()) { fprintf(stderr, "index %d: --list shows %zu moons\n", k, src.size()); exit(2); }
            x = src[(size_t)k].x; y = src[(size_t)k].y;
        };
        if (pairIdx) { pick(ia, xa, ya); pick(ib, xb, yb); havePair = true; }
        if (refIdx)  { pick(ir, xr, yr); haveRef = true; }
    }
    if (!havePair || !haveRef) { Usage(); return 2; }
    cfg.ax = xa; cfg.ay = ya; cfg.bx = xb; cfg.by = yb; cfg.rx = xr; cfg.ry = yr;

    // The clock offsets from Shift+J's sheet.
    std::vector<std::string> sheet;
    if (!sheetPath.empty()) {
        std::ifstream f(sheetPath);
        if (!f) { fprintf(stderr, "cannot read the sheet %s\n", sheetPath.c_str()); return 1; }
        std::string line;
        while (std::getline(f, line)) {
            double o;
            if (ParseClockLine(line, "Clock before:", o)) { cfg.offsetBeforeSec = o; cfg.haveOffsetBefore = true; }
            if (ParseClockLine(line, "Clock after:", o))  { cfg.offsetAfterSec = o;  cfg.haveOffsetAfter = true; }
            sheet.push_back(line);
        }
        if (outPath.empty()) {
            outPath = sheetPath;
            const size_t dot = outPath.find_last_of('.');
            if (dot != std::string::npos) outPath.erase(dot);
            outPath += ".dat";
        }
    }
    if (outPath.empty()) {
        outPath = serPath;
        const size_t dot = outPath.find_last_of('.');
        if (dot != std::string::npos) outPath.erase(dot);
        outPath += ".dat";
    }

    const moonphot::Result r = moonphot::Run(in.frames, in.width, in.height, frame, when, cfg);
    if (!r.ok) { fprintf(stderr, "no light curve: %s\n", r.why.c_str()); return 1; }

    FILE* o = fopen(outPath.c_str(), "w");
    if (!o) { fprintf(stderr, "cannot write %s\n", outPath.c_str()); return 1; }
    const std::string dat = moonphot::DatText(r);
    fwrite(dat.data(), 1, dat.size(), o);
    fclose(o);

    printf("Light curve: %s\n", outPath.c_str());
    printf("  %zu points, %.2f s bins, aperture radius %.1f px\n", r.bins.size(), r.binSec, r.radius);
    printf("  %d frames measured, %d dropped as clipped, %d lost\n", r.framesUsed, r.framesSaturated, r.framesLost);
    if (r.baseRatio > 0)
        printf("  deepest point: pair/reference at %.1f%% of its start (a quick look, not a fit)\n",
               100.0 * r.minRatio / r.baseRatio);
    if (r.framesSaturated > r.framesUsed / 10)
        printf("  WARNING: over a tenth of the frames clipped; IMCCE asks that the moons never saturate\n");
    if (r.timesCorrected) {
        printf("  dates are UTC: PC times plus the clock offsets from the sheet (%s%s)\n",
               cfg.haveOffsetBefore ? "before" : "", cfg.haveOffsetAfter ? (cfg.haveOffsetBefore ? " and after" : "after") : "");
    } else {
        printf("  WARNING: dates are PC-clock times, NOT corrected to UTC (no clock offsets; use --sheet)\n");
    }

    // Say on the sheet what the .dat is, above its end marker.
    if (!sheetPath.empty()) {
        const size_t slash = outPath.find_last_of("\\/");
        const std::string datName = slash == std::string::npos ? outPath : outPath.substr(slash + 1);
        char note[320];
        snprintf(note, sizeof(note), "Light curve: %s, %zu points, %.2f s bins, aperture %.1f px; dates %s.",
                 datName.c_str(), r.bins.size(), r.binSec, r.radius,
                 r.timesCorrected ? "UTC (PC times plus the clock offsets above, interpolated across the run)"
                                  : "PC-clock times, not corrected");
        std::ofstream w(sheetPath, std::ios::trunc);
        bool added = false;
        for (const auto& line : sheet) {
            if (line.rfind("Light curve: ", 0) == 0) continue;   // replace an earlier run's line
            if (!added && line.rfind("--- end of the sheet", 0) == 0) { w << note << "\n\n"; added = true; }
            w << line << "\n";
        }
        if (!added) w << note << "\n";
        printf("  noted on the sheet: %s\n", sheetPath.c_str());
    }
    return 0;
}
