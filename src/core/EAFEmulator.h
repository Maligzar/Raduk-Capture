// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once
// ============================================================
// EAFEmulator.h — Software emulator for ZWO EAF focuser
// Implements the same public interface as EAFFocuser.
// Simulates motor movement at realistic step rate.
// Enabled when RADUK_EMULATOR is defined in CMakeLists.txt.
// ============================================================

#ifdef RADUK_EMULATOR
#  include "EAF_focuser_stub.h"
#else
#  include "EAF_focuser.h"
#endif
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

namespace raduk::core {

class EAFFocuser {
public:
    EAFFocuser()  = default;
    ~EAFFocuser() { Close(); }

    EAFFocuser(const EAFFocuser&) = delete;
    EAFFocuser& operator=(const EAFFocuser&) = delete;

    // ── Lifecycle ──────────────────────────────────────────────

    static int GetCount() {
        printf("[EAFEmulator] GetCount: 1 emulated focuser\n");
        return 1;
    }

    bool Open(int /*index*/) {
        if (m_isOpen) return false;
        // Start where this rig's focuser actually lives. The 8SE with the
        // EAF comes to focus around 150-175k - a measured run landed at
        // 168187 - so the old 50000 was nowhere near reality and made
        // every emulated focus figure meaningless to compare against a
        // real night's log.
        m_position = 166500;
        m_target   = 166500;
        m_isOpen   = true;
        m_moving   = false;
        // Start the motor simulation thread
        m_motorThread = std::thread(&EAFFocuser::MotorLoop, this);
        printf("[EAFEmulator] Opened: id=0, pos=%d, maxStep=600000\n", m_position.load());
        return true;
    }

    void Close() {
        if (!m_isOpen) return;
        m_isOpen = false;
        m_moving = false;
        if (m_motorThread.joinable()) m_motorThread.join();
        printf("[EAFEmulator] Closed\n");
    }

    bool IsOpen() const { return m_isOpen; }

    // ── Motion ─────────────────────────────────────────────────

    bool MoveTo(int position) {
        if (!m_isOpen) return false;
        position = std::clamp(position, 0, 600000);
        std::lock_guard<std::mutex> lk(m_mutex);
        m_target = position;
        m_moving = (m_target != m_position.load());
        printf("[EAFEmulator] MoveTo %d (from %d)\n", position, m_position.load());
        return true;
    }

    bool MoveRelative(int steps) {
        if (!m_isOpen) return false;
        int newPos = std::clamp(m_position.load() + steps, 0, 600000);
        return MoveTo(newPos);
    }

    bool Stop() {
        if (!m_isOpen) return false;
        std::lock_guard<std::mutex> lk(m_mutex);
        m_target = m_position.load();  // Cancel move — target = current
        m_moving = false;
        printf("[EAFEmulator] Stop at %d\n", m_position.load());
        return true;
    }

    bool IsMoving() const {
        return m_moving.load();
    }

    int GetPosition() const {
        return m_position.load();
    }

    int GetMaxPosition() const { return 600000; }

    // ── Configuration ──────────────────────────────────────────

    void SetReverse(bool reversed) {
        m_reversed = reversed;
        printf("[EAFEmulator] Reverse: %s\n", reversed ? "on" : "off");
    }

    void SetBacklash(int steps) {
        m_backlash = steps;
        printf("[EAFEmulator] Backlash: %d steps\n", steps);
    }

    // ── Temperature ────────────────────────────────────────────

    float GetTemperature() const {
        // Stable temperature with minor noise — realistic for a focuser
        float noise = ((m_tempTick.load() % 10) - 5) * 0.02f;
        return 18.3f + noise;
    }

    // ── Info ───────────────────────────────────────────────────

    int FocuserId() const { return 0; }

private:
    // Motor simulation: moves position toward target at ~1000 steps/sec
    void MotorLoop() {
        const int STEPS_PER_TICK = 10;   // steps moved per millisecond tick
        while (m_isOpen) {
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                int pos = m_position.load();
                if (pos != m_target) {
                    int delta = m_target - pos;
                    int step = std::min(std::abs(delta), STEPS_PER_TICK);
                    if (delta < 0) step = -step;
                    m_position.store(pos + step);
                    m_moving = (m_position.load() != m_target);
                } else {
                    m_moving = false;
                }
            }
            m_tempTick++;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    bool                m_isOpen   = false;
    std::atomic<int>    m_position{166500};
    int                 m_target   = 166500;
    std::atomic<bool>   m_moving{false};
    bool                m_reversed = false;
    int                 m_backlash = 0;
    std::mutex          m_mutex;
    std::thread         m_motorThread;
    std::atomic<int>    m_tempTick{0};
};

} // namespace raduk::core
