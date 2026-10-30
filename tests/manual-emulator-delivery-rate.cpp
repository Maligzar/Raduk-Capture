// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// P4.3: at 120 fps, what does a connected focuser cost?
#include "core/CameraEmulator.h"
#include <cstdio>
#include <chrono>
#include <vector>
using Clock = std::chrono::steady_clock;

static void Run(int W, int H, double want, int focusPos, float pxStep,
                const char* label) {
    raduk::core::ASICamera cam;
    if (!cam.Open(0)) return;
    cam.SetROI(W, H, 1, ASI_IMG_RAW8);
    int aW=W,aH=H,aB=1; ASI_IMG_TYPE aT=ASI_IMG_RAW8; cam.GetROI(aW,aH,aB,aT);
    cam.SetExposure(2.25);
    cam.SetTargetFps(want);
    cam.SetBlurPxPerStep(pxStep);
    if (focusPos >= 0)
        cam.SetFocusPositionProvider([focusPos](int& p){ p = focusPos; return true; });
    cam.StartVideoCapture();
    std::vector<uint8_t> buf((size_t)aW*aH);
    for (int i=0;i<20;i++) cam.GetVideoFrame(buf.data(),(int)buf.size(),5000);
    const auto t0 = Clock::now();
    int got=0; for (int i=0;i<150;i++) if (cam.GetVideoFrame(buf.data(),(int)buf.size(),5000)) got++;
    const double secs = std::chrono::duration<double>(Clock::now()-t0).count();
    const double fps = got/secs;
    cam.StopVideoCapture(); cam.Close();
    printf("  %4dx%-5d asked %3.0f  %-28s got %6.1f fps  render %5.1f ms  %6.1f MB/s\n",
           aW, aH, want, label, fps, cam.RenderMs(), (double)aW*aH*fps/1e6);
}

int main() {
    printf("\n=== what a connected focuser costs at 120 fps ===\n\n");
    Run(1280, 960, 120, -1,     0.0083f, "no focuser at all");
    Run(1280, 960, 120, 168000, 0.0083f, "focuser, AT true focus");
    Run(1280, 960, 120, 166500, 0.0083f, "focuser, 1500 steps out");
    Run(1280, 960, 120, 166500, 0.0f,    "focuser out, blur disabled");
    printf("\n");
    Run(640, 480, 200, 166500, 0.0083f,  "focuser 1500 out");
    Run(640, 480, 200, 168000, 0.0083f,  "focuser AT true focus");
    printf("\n");
    return 0;
}
