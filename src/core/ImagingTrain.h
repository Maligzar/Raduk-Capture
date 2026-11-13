#pragma once
// ============================================================
//  ImagingTrain.h -- phase 149.0: one description of the camera and optics
//
//  The plate scale was a typed figure (0.203"/px in rig.ini and in a dozen
//  comments), the sensor was 3840x2160 in five places, and the camera was
//  "ZWO ASI678MC" in seven. Phase 149's first rule is that none of that is
//  written in code: it comes from the camera (CameraInfo) and the rig's
//  optics, so the next ZWO camera, or a Barlow, changes it everywhere.
//
//    nominal scale = 206.264806 x pixel um / (focal length mm x reducer)
//
//  A plate scale measured on the sky (clear-night item 3; phase 155.3 will
//  write it from agreeing solves) overrides the nominal one when set.
//  The field of view follows from the scale and the sensor.
//
//  The camera's description is remembered in rig.ini, so start-up and the
//  emulator have figures with no camera connected. Its first-run default
//  is this rig's camera, as a configured default, not a code assumption.
//
//  Pure: tests/imaging_train.cpp.
// ============================================================

#include <cmath>
#include <cstdio>
#include <string>

namespace raduk::core {

struct ImagingTrain {
    std::string camera;            // CameraInfo.name
    std::string telescope;         // rig.ini TelescopeName
    int    sensorW = 0, sensorH = 0;
    double pixelUm = 0.0;
    bool   color = true;
    double focalMm = 0.0;          // rig.ini FocalLengthMm
    double reducer = 1.0;          // rig.ini FocalReducer: 0.63 reducer, 2.0 Barlow
    double measuredArcsecPx = 0.0; // rig.ini PlateScaleMeasuredArcsecPx; 0 = not measured

    double EffectiveFocalMm() const { return focalMm * (reducer > 0 ? reducer : 1.0); }

    // UNVERIFIED(item 3): plate-solve one frame and compare the solved scale with this derived figure
    double NominalArcsecPx() const {
        const double f = EffectiveFocalMm();
        return (pixelUm > 0 && f > 0) ? 206.264806 * pixelUm / f : 0.0;
    }
    double ArcsecPx() const { return measuredArcsecPx > 0 ? measuredArcsecPx : NominalArcsecPx(); }
    const char* ScaleSource() const {
        if (measuredArcsecPx > 0) return "measured";
        return NominalArcsecPx() > 0 ? "derived from the camera and focal length" : "unknown";
    }

    double FovWidthDeg(int w = 0) const  { return (w > 0 ? w : sensorW) * ArcsecPx() / 3600.0; }
    double FovHeightDeg(int h = 0) const { return (h > 0 ? h : sensorH) * ArcsecPx() / 3600.0; }
    double FovDiagDeg() const {
        const double a = FovWidthDeg(), b = FovHeightDeg();
        return std::sqrt(a * a + b * b);
    }
    double FocalRatio(double apertureMm) const {
        return apertureMm > 0 ? EffectiveFocalMm() / apertureMm : 0.0;
    }

    // "Celestron 8SE  .  ZWO ASI678MC" for the audience screen.
    std::string RigLine() const {
        if (telescope.empty()) return camera;
        if (camera.empty()) return telescope;
        return telescope + "  \xc2\xb7  " + camera;
    }
    // One line for the log.
    std::string Describe() const {
        char b[320];
        snprintf(b, sizeof(b), "%s, %dx%d, %.2f um, %s; %.0f mm x %.2f; %.3f\"/px (%s); field %.3f x %.3f deg",
                 camera.empty() ? "camera unknown" : camera.c_str(), sensorW, sensorH, pixelUm,
                 color ? "colour" : "mono", focalMm, reducer, ArcsecPx(), ScaleSource(),
                 FovWidthDeg(), FovHeightDeg());
        return b;
    }
};

}  // namespace raduk::core
