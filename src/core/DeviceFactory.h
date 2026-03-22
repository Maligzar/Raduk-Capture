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
