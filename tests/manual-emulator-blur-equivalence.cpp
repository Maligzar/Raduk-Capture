// Phase 93 / P4.4a: is the retiled blur the SAME blur?
//
// Grabs a defocused frame from the emulator and prints a checksum plus
// a row of samples. Run before and after the change: the numbers must
// match exactly, or the rewrite altered the picture as well as its
// speed.
#include "core/CameraEmulator.h"
#include <cstdio>
#include <vector>

int main(int argc, char** argv) {
    const int W = (argc > 1) ? atoi(argv[1]) : 1920;
    const int H = (argc > 2) ? atoi(argv[2]) : 1080;
    raduk::core::ASICamera cam;
    if (!cam.Open(0)) { printf("open failed\n"); return 1; }
    cam.SetROI(W, H, 1, ASI_IMG_RAW8);
    int aW=W, aH=H, aB=1; ASI_IMG_TYPE aT=ASI_IMG_RAW8;
    cam.GetROI(aW, aH, aB, aT);
    cam.SetExposure(2.25);
    cam.SetFreezeSurface(true);               // same picture every frame
    cam.SetNoiseAmplitude(0);                 // no per-frame randomness
    cam.SetFocusPositionProvider([](int& p){ p = 166500; return true; });
    cam.StartVideoCapture();

    std::vector<uint8_t> buf((size_t)aW * aH);
    for (int i = 0; i < 12; i++) cam.GetVideoFrame(buf.data(), (int)buf.size(), 8000);
    cam.StopVideoCapture();
    cam.Close();

    unsigned long long sum = 0, fnv = 1469598103934665603ULL;
    for (size_t i = 0; i < buf.size(); i++) {
        sum += buf[i];
        fnv ^= buf[i]; fnv *= 1099511628211ULL;
    }
    printf("\n  %dx%d  sum=%llu  fnv=%016llx\n", aW, aH, sum, fnv);
    printf("  samples:");
    for (int i = 0; i < 10; i++)
        printf(" %3u", buf[(size_t)(aH/2) * aW + aW/2 - 5 + i]);
    printf("\n\n");
    return 0;
}
