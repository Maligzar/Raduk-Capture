#pragma once
// ============================================================
//  PixelFormat.h -- phase 149.0b: the app's own name for a frame's format
//
//  ICamera's ImageType has been brand-neutral since P5.2, but FrameSlot
//  and TargetProfile kept ZWO's ASI_IMG_TYPE, and the profile wrote it to
//  disk as ZWO's integer (img_type=2 meant RAW16 only in ZWO's
//  numbering). The owner's review, 2026-11-13: close that gap while there
//  is one backend and one developer who remembers why, because every
//  profile saved with the vendor's number makes the change dearer.
//
//  So the app's format is ImageType (a mono camera is Raw8 or Raw16 with
//  CameraInfo.bayer == None), named on disk by words, and a profile
//  carries a format version:
//
//    version 1 (no key): [roi] img_type = ZWO's ASI_IMG_TYPE integer
//    version 2:          [profile] format_version = 2
//                        [roi] pixel_format = raw8 | raw16 | rgb24 | y8
//
//  A version-1 file is read through ZWO's table below (the one place that
//  table lives outside the ZWO backend, because old files carry it) and
//  is written at version 2 on its next save.
//
//  Pure: tests/pixel_format.cpp.
// ============================================================

#include "ICamera.h"

#include <string>

namespace raduk::core {

using PixelFormat = ImageType;

inline constexpr int kProfileFormatVersion = 2;

inline const char* PixelFormatName(ImageType t) {
    switch (t) {
        case ImageType::Raw8:  return "raw8";
        case ImageType::Raw16: return "raw16";
        case ImageType::Rgb24: return "rgb24";
        case ImageType::Y8:    return "y8";
    }
    return "raw8";
}

inline bool PixelFormatFromName(const std::string& s, ImageType& out) {
    if (s == "raw8")  { out = ImageType::Raw8;  return true; }
    if (s == "raw16") { out = ImageType::Raw16; return true; }
    if (s == "rgb24") { out = ImageType::Rgb24; return true; }
    if (s == "y8")    { out = ImageType::Y8;    return true; }
    return false;
}

// Version-1 profiles stored ZWO's ASI_IMG_TYPE value: RAW8 0, RGB24 1,
// RAW16 2, Y8 3. Anything else reads as Raw8, as the old loader's default.
inline ImageType PixelFormatFromLegacyZwo(int v) {
    switch (v) {
        case 1:  return ImageType::Rgb24;
        case 2:  return ImageType::Raw16;
        case 3:  return ImageType::Y8;
        default: return ImageType::Raw8;
    }
}

inline int BitDepthOf(ImageType t) { return t == ImageType::Raw16 ? 16 : 8; }
inline bool IsBayerRaw(ImageType t) { return t == ImageType::Raw8 || t == ImageType::Raw16; }

}  // namespace raduk::core
