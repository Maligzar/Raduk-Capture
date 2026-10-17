// Phase 93: does the emulator's DEFOCUS cost scale with the frame?
//
// emu_fps2 measured 63 ms flat at every format and concluded the
// renderer is not per-pixel. It never set a focus provider, so the
// whole defocus block was skipped -- and in the app the focuser IS
// connected and the EAF is not sitting exactly on true focus. This
// runs both ways at each format.

#include "core/CameraEmulator.h"

#include <cstdio>
#include <chrono>
#include <vector>
#include <algorithm>

using Clock = std::chrono::steady_clock;

static double Run(int W, int H, int focusPos) {
    raduk::core::ASICamera cam;
    if (!cam.Open(0)) return -1.0;
    cam.SetROI(W, H, 1, ASI_IMG_RAW8);
    int aW=W, aH=H, aB=1; ASI_IMG_TYPE aT=ASI_IMG_RAW8;
    cam.GetROI(aW, aH, aB, aT);
    cam.SetExposure(2.25);
    if (focusPos >= 0)
        cam.SetFocusPositionProvider([focusPos](int& p){ p = focusPos; return true; });
    cam.StartVideoCapture();

    std::vector<uint8_t> buf((size_t)aW * aH);
    for (int i = 0; i < 6; i++) cam.GetVideoFrame(buf.data(), (int)buf.size(), 8000);

    const int N = 25;
    std::vector<double> gaps; gaps.reserve(N);
    auto prev = Clock::now();
    for (int i = 0; i < N; i++)
        if (cam.GetVideoFrame(buf.data(), (int)buf.size(), 8000)) {
            const auto now = Clock::now();
            gaps.push_back(std::chrono::duration<double, std::milli>(now - prev).count());
            prev = now;
        }
    cam.StopVideoCapture();
    cam.Close();
    if (gaps.empty()) return -1.0;
    std::sort(gaps.begin(), gaps.end());
    return gaps[gaps.size()/2];
}

int main() {
    // 168000 is the emulator's true focus. 166500 is where the app's
    // emulated EAF actually sits, 1500 steps out.
    printf("\n=== emulator render cost, in focus vs defocused ===\n\n");
    printf("  %-12s %12s %12s %10s\n", "format", "no focuser", "EAF 166500", "ratio");
    const int fmts[4][2] = {{640,480},{1280,960},{1920,1080},{3840,2160}};
    for (auto& f : fmts) {
        const double a = Run(f[0], f[1], -1);
        const double b = Run(f[0], f[1], 166500);
        printf("  %4dx%-7d %9.1f ms %9.1f ms %9.1fx\n",
               f[0], f[1], a, b, (a > 0) ? b / a : 0.0);
    }
    printf("\n  TARGET_MS is 50 and sleep_for rounds to the 15.6 ms tick,\n"
           "  so ~63 ms is the floor whatever the render costs.\n\n");
    return 0;
}
