#pragma once
// ============================================================
// DeviceFactory.h — Device class selector for RadukCapture
//
// Include this instead of individual device headers.
//
// RADUK_EMULATOR=ON  → emulator classes (software only, no SDKs)
// default (hardware) → real SDK wrappers + ASCOM mount
//
// Mount strategy (hardware mode):
//   Primary:  ASCOMMount — connects to CPWI's ASCOM driver.
//             CPWI retains exclusive COM3 ownership. RadukCapture
//             reads position and sends PulseGuide via ASCOM.
//   Fallback: NexStarMount — direct serial on COM3.
//             Only viable when CPWI is NOT running.
//             Phase 5 PulseGuide not available in fallback mode.
//
// main.cpp connection sequence (hardware):
//   1. Try ASCOMMount::FindCPWIDriver() + Connect()
//   2. If fails, fall back to NexStarMount("COM3")
//   3. Log which path was taken
// ============================================================

#ifdef RADUK_EMULATOR
    #include "CameraEmulator.h"
    #include "EAFEmulator.h"
    #include "MountEmulator.h"

    #pragma message("*** RADUK_EMULATOR: Building in emulator mode ***")
#else
    #include "ASICamera.h"
    #include "EAFFocuser.h"
    #include "NexStarMount.h"
    #include "ASCOMMount.h"
    #include "IMount.h"
#endif

// ── Enumeration, without naming a vendor ──────────────────────
//
// ASICamera::Enumerate() is static and returns ASI_CAMERA_INFO, so
// every caller asking "is there a camera" had to name ZWO's class
// and ZWO's struct to find out. A static cannot be virtual, so the
// answer is a free function here, where the build already knows
// which backend is compiled.
//
// This is the shape a second brand plugs into: ask each backend in
// turn and concatenate. Today there is one, so it asks one.

#include "ICamera.h"
#include "ASITypeMap.h"
#include <vector>

namespace raduk::core {

inline std::vector<CameraInfo> EnumerateCameras() {
    std::vector<CameraInfo> out;
    for (const auto& raw : ASICamera::Enumerate())
        out.push_back(asimap::Describe(raw));
    return out;
}

} // namespace raduk::core
