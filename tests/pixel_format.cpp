// ============================================================
//  Phase 149.0b: the app's own pixel format (src/core/PixelFormat.h)
//
//    1. Every format's name round-trips; an unknown name is refused.
//    2. Version-1 profiles stored ZWO's integer: each maps to the same
//       format the old loader produced, and junk reads as raw8, as before.
//    3. Every built-in profile carries a format whose name round-trips,
//       and the DSO profile is still 16-bit.
//    4. Bit depth and Bayer from the format.
// ============================================================

#include "profiles/TargetProfile.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace raduk::core;
namespace P = raduk::profiles;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-58s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

int main() {
    printf("Pixel format (phase 149.0b)\n");
    const ImageType all[] = { ImageType::Raw8, ImageType::Raw16, ImageType::Rgb24, ImageType::Y8 };
    for (ImageType t : all) {
        ImageType back = ImageType::Raw8;
        Check(PixelFormatFromName(PixelFormatName(t), back) && back == t,
              std::string("the name round-trips: ") + PixelFormatName(t));
    }
    ImageType junk = ImageType::Raw16;
    Check(!PixelFormatFromName("RAW8", junk) && !PixelFormatFromName("", junk) && junk == ImageType::Raw16,
          "an unknown name is refused and changes nothing");

    // ZWO's ASI_IMG_TYPE: RAW8 0, RGB24 1, RAW16 2, Y8 3.
    Check(PixelFormatFromLegacyZwo(0) == ImageType::Raw8,  "version 1: 0 was RAW8");
    Check(PixelFormatFromLegacyZwo(1) == ImageType::Rgb24, "version 1: 1 was RGB24");
    Check(PixelFormatFromLegacyZwo(2) == ImageType::Raw16, "version 1: 2 was RAW16");
    Check(PixelFormatFromLegacyZwo(3) == ImageType::Y8,    "version 1: 3 was Y8");
    Check(PixelFormatFromLegacyZwo(-1) == ImageType::Raw8 && PixelFormatFromLegacyZwo(42) == ImageType::Raw8,
          "version 1: anything else reads as raw8, as the old loader did");

    const std::vector<P::TargetProfile> builtins = {
        P::DefaultJupiter(), P::DefaultSaturn(), P::DefaultMars(), P::DefaultVenus(),
        P::DefaultMercury(), P::DefaultUranus(), P::DefaultNeptune(), P::DefaultMoon(),
        P::DefaultDSO(), P::DefaultStar(), P::DefaultPlanetFocus() };
    bool allOk = true;
    for (const auto& p : builtins) {
        ImageType back = ImageType::Raw8;
        if (!PixelFormatFromName(PixelFormatName(p.imgType), back) || back != p.imgType) allOk = false;
    }
    Check(allOk, "every built-in profile's format round-trips", std::to_string(builtins.size()) + " profiles");
    Check(P::DefaultDSO().imgType == ImageType::Raw16, "the DSO profile is still 16-bit");
    Check(P::DefaultJupiter().imgType == ImageType::Raw8, "Jupiter is still 8-bit");
    Check(P::TargetProfile{}.formatVersionRead == kProfileFormatVersion, "a new profile is at the current version");

    Check(BitDepthOf(ImageType::Raw16) == 16 && BitDepthOf(ImageType::Raw8) == 8 && BitDepthOf(ImageType::Rgb24) == 8,
          "bit depth from the format");
    Check(IsBayerRaw(ImageType::Raw8) && IsBayerRaw(ImageType::Raw16) && !IsBayerRaw(ImageType::Rgb24) &&
          !IsBayerRaw(ImageType::Y8), "raw Bayer from the format");

    printf(g_fail ? "\n%d FAILED\n" : "\nall passed\n", g_fail);
    return g_fail ? 1 : 0;
}
