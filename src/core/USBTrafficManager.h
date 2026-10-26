// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once
// ============================================================
// USBTrafficManager.h — USB bus priority during capture
// When recording is active, the ASI camera owns the USB bus.
// EAF and mount polling are throttled to reduce contention
// and prevent USB packet collisions that cause dropped frames.
//
// Usage (from render loop readout update):
//   if (usb.ShouldPollCamera())  { /* always true */        }
//   if (usb.ShouldPollFocuser()) { focuser.GetPosition(); } // throttled
//   if (usb.ShouldPollMount())   { mount.GetRADec(...);   } // throttled
//
// Call usb.SetCapturing(true/false) when recording starts/stops.
// Call usb.Tick() each render loop iteration.
// ============================================================

#include <cstdint>
#include <cstdio>
#include <chrono>

namespace raduk::core {

class USBTrafficManager {
public:
    // Polling intervals (milliseconds)
    // Normal mode: both devices polled at the readout rate (~500ms)
    // Capture mode: EAF slowed to 10s, mount slowed to 2s
    static constexpr int FOCUSER_POLL_NORMAL_MS  =  500;
    static constexpr int FOCUSER_POLL_CAPTURE_MS = 10000;
    static constexpr int MOUNT_POLL_NORMAL_MS    =  500;
    static constexpr int MOUNT_POLL_CAPTURE_MS   = 2000;

    USBTrafficManager();

    // Call when recording starts or stops
    void SetCapturing(bool capturing);

    bool IsCapturing() const { return m_capturing; }

    // Returns true if the focuser should be polled this tick
    bool ShouldPollFocuser();

    // Returns true if the mount should be polled this tick
    bool ShouldPollMount();

    // Camera is always allowed — it owns the bus during capture
    bool ShouldPollCamera() const { return true; }

    // Human-readable status for session log
    const char* StatusString() const;

private:
    using Clock = std::chrono::steady_clock;

    bool                          m_capturing      = false;
    std::chrono::time_point<Clock> m_lastFocuserPoll;
    std::chrono::time_point<Clock> m_lastMountPoll;
};

} // namespace raduk::core
