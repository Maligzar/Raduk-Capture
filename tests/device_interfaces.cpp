// ============================================================
//  Driving the emulated devices through ICamera and IFocuser
//
//  Until P5.2 there was no interface and no way to ask for one.
//  CameraEmulator.h defines a class literally named ASICamera and
//  EAFEmulator.h one named EAFFocuser, and DeviceFactory.h
//  includes exactly one header of each pair, so the emulator
//  impersonates the hardware class by name collision. That works
//  and costs nothing at runtime, and it has two consequences.
//
//  A second camera brand cannot be added. Not "is awkward": there
//  is nowhere to put it.
//
//  And nothing checked that the two classes agreed. They are
//  never compiled in the same translation unit, so a signature
//  could drift on one side and only the other build would find
//  out, at the worst possible moment. Both pairs now inherit the
//  interfaces, so the compiler checks the agreement on every
//  build of either, and this test checks that the neutral
//  vocabulary actually carries a frame.
//
//  What this does NOT prove: that the real ZWO wrapper behaves
//  the same. It cannot, with no camera on the bench. It proves
//  the seam exists, holds a frame, and that the ASI translation
//  round-trips.
// ============================================================

#include "core/CameraEmulator.h"
#include "core/EAFEmulator.h"

#include <cstdio>
#include <vector>
#include <cstdint>

using namespace raduk::core;

static int g_fail = 0;

static void Check(bool ok, const char* what, const char* detail = "") {
    if (ok) { printf("  ok    %s\n", what); }
    else    { printf("  FAIL  %s %s\n", what, detail); ++g_fail; }
}

int main() {
    printf("Device interfaces: ICamera and IFocuser over the emulators\n");

    // ---- The translation, in both directions -----------------
    printf("\n1. The ASI vocabulary round-trips\n");
    Check(asimap::FromAsi(asimap::ToAsi(ImageType::Raw8))  == ImageType::Raw8,  "Raw8 survives the round trip");
    Check(asimap::FromAsi(asimap::ToAsi(ImageType::Raw16)) == ImageType::Raw16, "Raw16 survives the round trip");
    Check(asimap::FromAsi(asimap::ToAsi(ImageType::Rgb24)) == ImageType::Rgb24, "Rgb24 survives the round trip");
    Check(asimap::ToAsi(ControlId::Gain)       == ASI_GAIN,     "Gain maps to ASI_GAIN");
    Check(asimap::ToAsi(ControlId::ExposureUs) == ASI_EXPOSURE, "ExposureUs maps to ASI_EXPOSURE");

    // The three outcomes the capture loop tells apart. Folding
    // them into a bool is exactly what this guards against.
    Check(asimap::FromAsi(ASI_SUCCESS)              == CaptureError::Success,       "success is distinct");
    Check(asimap::FromAsi(ASI_ERROR_TIMEOUT)        == CaptureError::Timeout,       "timeout is distinct");
    Check(asimap::FromAsi(ASI_ERROR_CAMERA_REMOVED) == CaptureError::CameraRemoved, "removal is distinct");

    // ---- The focuser, through the interface only -------------
    printf("\n2. A focuser driven as IFocuser\n");
    EAFFocuser realFocuser;
    IFocuser*  foc = &realFocuser;
    Check(foc->Open(0), "opens");
    Check(foc->IsOpen(), "reports open");
    const int start = foc->GetPosition();
    Check(foc->GetMaxPosition() > start, "max position is beyond the current one");
    foc->SetBacklash(0);
    foc->SetReverse(false);
    Check(foc->MoveRelative(100), "accepts a relative move");
    foc->Stop();
    Check(foc->FocuserId() >= 0, "has an id");
    foc->Close();
    Check(!foc->IsOpen(), "reports closed");

    // ---- The camera, through the interface only --------------
    printf("\n3. A camera driven as ICamera\n");
    ASICamera realCamera;
    ICamera*  cam = &realCamera;

    Check(cam->Open(0), "opens");
    Check(cam->IsOpen(), "reports open");

    const CameraInfo info = cam->Describe();
    Check(!info.name.empty(), "describes itself with a name", info.name.c_str());
    Check(info.maxWidth > 0 && info.maxHeight > 0, "reports a sensor size");

    Check(cam->SetRegion(640, 480, 1, ImageType::Raw8), "accepts a region");
    int w = 0, h = 0, bin = 0; ImageType got = ImageType::Raw16;
    Check(cam->GetRegion(w, h, bin, got), "reads the region back");
    Check(w == 640 && h == 480 && got == ImageType::Raw8,
          "the region that comes back is the one that went in");

    cam->SetGain(200);
    cam->SetExposure(5.0);
    Check(cam->GetControlValue(ControlId::Gain) == 200, "gain survives a set and get");
    Check(cam->GetControlValue(ControlId::ExposureUs) == 5000,
          "exposure arrives in microseconds, as the name says");

    ControlRange range{};
    if (cam->GetControlRange(ControlId::Gain, range))
        Check(range.max > range.min, "gain reports a usable range");
    else
        printf("  --    gain range not offered by this backend\n");

    // The hot path, which is the only reason any of this matters.
    Check(cam->StartVideoCapture(), "starts streaming");
    Check(cam->IsCapturing(), "reports capturing");

    std::vector<uint8_t> frame(640 * 480 * 2, 0);
    CaptureError err = CaptureError::Other;
    bool got_one = false;
    for (int attempt = 0; attempt < 50 && !got_one; ++attempt)
        got_one = cam->GetFrame(frame.data(), (int)frame.size(), 1000, err);

    Check(got_one, "a frame arrives through the interface");
    Check(err == CaptureError::Success, "and reports success rather than a folded bool");

    bool anyNonZero = false;
    for (size_t i = 0; i < frame.size() && !anyNonZero; ++i)
        if (frame[i] != 0) anyNonZero = true;
    Check(anyNonZero, "the frame has pixels in it, not just a zeroed buffer");

    cam->StopVideoCapture();
    Check(!cam->IsCapturing(), "stops");
    Check(cam->GetDroppedFrames() >= 0, "reports a drop count");
    cam->Close();
    Check(!cam->IsOpen(), "closes");

    if (g_fail == 0) printf("\nPASS\n"); else printf("\nFAIL: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
