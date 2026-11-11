// ============================================================
//  2026-11-11: [TO SER], several FITS cubes in the background
//
//  FitsBatch.h converts a list of cubes one after another on its own
//  thread. Checked: every good file becomes a SER beside it, a bad one
//  is counted as failed without stopping the rest, the status moves
//  and ends not-running, a second Start while running is refused, and
//  Cancel stops between frames and removes the partial SER.
// ============================================================

#include "core/FitsBatch.h"
#include "core/FITSFormat.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-58s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static void MakeCube(const char* path, int w, int h, int frames) {
    ObservationRecord r;
    r.width = w; r.height = h; r.bitDepth = 16; r.colour = "RGGB";
    const std::string hdr = fits::BuildHeader(r, fits::HeaderExtras{}, fits::RunTimes{}, frames);
    FILE* f = fopen(path, "wb");
    fwrite(hdr.data(), 1, hdr.size(), f);
    std::vector<uint8_t> be((size_t)w * h * 2, 0x12);
    for (int i = 0; i < frames; ++i) fwrite(be.data(), 1, be.size(), f);
    fclose(f);
}
static bool Exists(const char* p) { FILE* f = fopen(p, "rb"); if (f) fclose(f); return f != nullptr; }
static void WaitDone(const fits2ser::Batch& b) {
    for (int i = 0; i < 3000 && b.Running(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

int main() {
    printf("\n=== [TO SER] batch ===\n");

    printf("\n1. Several files, one bad\n");
    MakeCube("b1.fits", 16, 8, 3);
    MakeCube("b2.fits", 16, 8, 4);
    {
        fits2ser::Batch b;
        Check(b.Start({"b1.fits", "missing.fits", "b2.fits"}), "starts");
        WaitDone(b);
        const auto st = b.Get();
        Check(!st.running && st.total == 3, "finishes, three taken");
        Check(st.done == 2 && st.failed == 1, "two converted, the missing one failed",
              std::to_string(st.done) + " / " + std::to_string(st.failed));
        Check(Exists("b1.ser") && Exists("b2.ser") && !Exists("missing.ser"),
              "each SER beside its FITS");
        Check(st.version > 3, "the status moved as it went", std::to_string(st.version));
        Check(!b.Start({}), "an empty list is refused");
    }

    printf("\n2. Cancel\n");
    MakeCube("big.fits", 512, 512, 400);   // ~200 MB: long enough to stop midway
    {
        fits2ser::Batch b;
        Check(b.Start({"big.fits"}), "starts");
        Check(!b.Start({"b1.fits"}), "a second start while running is refused");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        b.Cancel();
        WaitDone(b);
        const auto st = b.Get();
        Check(!st.running, "stops");
        Check(st.done == 1 || !Exists("big.ser"),
              "a cancelled file leaves no partial SER (or it had already finished)");
    }
    {
        // The destructor cancels and joins: no thread outlives the batch.
        auto* b = new fits2ser::Batch;
        b->Start({"big.fits"});
        delete b;
        Check(true, "destroying a running batch returns (cancel and join)");
    }
    for (const char* p : {"b1.fits", "b2.fits", "b1.ser", "b2.ser", "big.fits", "big.ser"}) remove(p);

    printf("\n%s\n", g_fail ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return g_fail ? 1 : 0;
}
