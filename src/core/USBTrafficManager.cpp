// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// ============================================================
// USBTrafficManager.cpp — USB bus priority during capture
// Out-of-line definitions for USBTrafficManager.h.
// ============================================================

#include "USBTrafficManager.h"

namespace raduk::core {

USBTrafficManager::USBTrafficManager() {
    auto now = Clock::now();
    m_lastFocuserPoll = now;
    m_lastMountPoll   = now;
}

void USBTrafficManager::SetCapturing(bool capturing) {
    if (m_capturing == capturing) return;
    m_capturing = capturing;
    printf("[USBTrafficMgr] Capture mode %s — "
           "EAF poll: %dms, Mount poll: %dms\n",
           capturing ? "ON" : "OFF",
           capturing ? FOCUSER_POLL_CAPTURE_MS : FOCUSER_POLL_NORMAL_MS,
           capturing ? MOUNT_POLL_CAPTURE_MS   : MOUNT_POLL_NORMAL_MS);
}

bool USBTrafficManager::ShouldPollFocuser() {
    auto now = Clock::now();
    int interval = m_capturing ? FOCUSER_POLL_CAPTURE_MS : FOCUSER_POLL_NORMAL_MS;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - m_lastFocuserPoll).count();
    if (ms >= interval) {
        m_lastFocuserPoll = now;
        return true;
    }
    return false;
}

bool USBTrafficManager::ShouldPollMount() {
    auto now = Clock::now();
    int interval = m_capturing ? MOUNT_POLL_CAPTURE_MS : MOUNT_POLL_NORMAL_MS;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - m_lastMountPoll).count();
    if (ms >= interval) {
        m_lastMountPoll = now;
        return true;
    }
    return false;
}

const char* USBTrafficManager::StatusString() const {
    return m_capturing
        ? "CAPTURE — EAF:10s  Mount:2s"
        : "IDLE — EAF:0.5s  Mount:0.5s";
}

} // namespace raduk::core
