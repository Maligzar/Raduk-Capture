#pragma once
// ============================================================
// ASITypeMap.h - translation between ICamera's vocabulary and ZWO's
//
// This is the whole cost of keeping ICamera brand-neutral, and it
// is deliberately concentrated in one file so the cost is visible
// and so a second brand knows exactly what it has to supply: its
// own version of this, and nothing else.
//
// Both the real wrapper and the emulator use it, because the
// emulator mimics the ASI SDK's types rather than inventing its
// own. That is why it is here and not inside ASICamera.cpp.
//
// Unmapped values are not silently coerced. An ImageType this
// application does not use returns the Raw8 default and a
// ControlId has no ASI equivalent only if someone adds one
// without extending the switch, which the compiler warns about
// because the switches are exhaustive over the enum.
// ============================================================

#ifdef RADUK_EMULATOR
#  include "ASICamera2Stub.h"
#else
#  include "ASICamera2.h"
#endif

#include "ICamera.h"

namespace raduk::core::asimap {

inline ASI_IMG_TYPE ToAsi(ImageType t) {
    switch (t) {
        case ImageType::Raw8:  return ASI_IMG_RAW8;
        case ImageType::Raw16: return ASI_IMG_RAW16;
        case ImageType::Rgb24: return ASI_IMG_RGB24;
        case ImageType::Y8:    return ASI_IMG_Y8;
    }
    return ASI_IMG_RAW8;
}

inline ImageType FromAsi(ASI_IMG_TYPE t) {
    switch (t) {
        case ASI_IMG_RAW8:  return ImageType::Raw8;
        case ASI_IMG_RAW16: return ImageType::Raw16;
        case ASI_IMG_RGB24: return ImageType::Rgb24;
        case ASI_IMG_Y8:    return ImageType::Y8;
        default:            return ImageType::Raw8;
    }
}

inline ASI_CONTROL_TYPE ToAsi(ControlId id) {
    switch (id) {
        case ControlId::Gain:              return ASI_GAIN;
        case ControlId::ExposureUs:        return ASI_EXPOSURE;
        case ControlId::Gamma:             return ASI_GAMMA;
        case ControlId::WhiteBalanceRed:   return ASI_WB_R;
        case ControlId::WhiteBalanceBlue:  return ASI_WB_B;
        case ControlId::UsbBandwidth:      return ASI_BANDWIDTHOVERLOAD;
        case ControlId::HighSpeedMode:     return ASI_HIGH_SPEED_MODE;
        case ControlId::Temperature:       return ASI_TEMPERATURE;
        case ControlId::Flip:              return ASI_FLIP;
        case ControlId::TargetTemperature: return ASI_TARGET_TEMP;
    }
    return ASI_GAIN;
}

inline BayerPattern FromAsi(ASI_BAYER_PATTERN b) {
    switch (b) {
        case ASI_BAYER_RG: return BayerPattern::RG;
        case ASI_BAYER_BG: return BayerPattern::BG;
        case ASI_BAYER_GR: return BayerPattern::GR;
        case ASI_BAYER_GB: return BayerPattern::GB;
        default:           return BayerPattern::None;
    }
}

// The capture loop treats these three differently: a timeout is
// routine at long exposures, a removal drives the self-healing
// path, and everything else is a fault worth reporting. Folding
// them into a bool would throw that away.
inline CaptureError FromAsi(ASI_ERROR_CODE e) {
    switch (e) {
        case ASI_SUCCESS:               return CaptureError::Success;
        case ASI_ERROR_TIMEOUT:         return CaptureError::Timeout;
        case ASI_ERROR_CAMERA_REMOVED:  return CaptureError::CameraRemoved;
        case ASI_ERROR_CAMERA_CLOSED:   return CaptureError::CameraClosed;
        default:                        return CaptureError::Other;
    }
}

inline CameraInfo Describe(const ASI_CAMERA_INFO& in) {
    CameraInfo out;
    out.name        = in.Name;
    out.maxWidth    = static_cast<int>(in.MaxWidth);
    out.maxHeight   = static_cast<int>(in.MaxHeight);
    out.isColor     = (in.IsColorCam == ASI_TRUE);
    out.bayer       = out.isColor ? FromAsi(in.BayerPattern) : BayerPattern::None;
    out.pixelSizeUm = in.PixelSize;
    out.bitDepth    = in.BitDepth;
    return out;
}

} // namespace raduk::core::asimap
