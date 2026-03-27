#pragma once
// ============================================================
// MountEmulator.h — Software emulator for Celestron NexStar mount
// Implements IMount — same interface as ASCOMMount and NexStarMount.
// Simulates RA/DEC drift, slewing, tracking modes, PulseGuide.
// Enabled when RADUK_EMULATOR is defined in CMakeLists.txt.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <cstdio>
#include <cmath>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include "IMount.h"

namespace raduk::core {

class NexStarMount : public IMount {
public:
    NexStarMount()  = default;
    ~NexStarMount() override { Disconnect(); }

    NexStarMount(const NexStarMount&) = delete;
    NexStarMount& operator=(const NexStarMount&) = delete;

    // ── Connection ─────────────────────────────────────────────

    bool Connect(const std::string& comPort, int /*baudRate*/ = 9600) override {
        if (m_connected) return false;
        m_comPort   = comPort + " [EMU]";
        m_connected = true;
        // Start with Jupiter's approximate RA/DEC (good test target)
        m_raHours   = 5.5833;  // ~05h 35m (near Orion)
        m_decDeg    = -5.3833; // ~-05° 23'
        m_tracking  = ALT_AZ;
        // Start the simulation thread
        m_simThread = std::thread(&NexStarMount::SimLoop, this);
        printf("[MountEmulator] Connected on %s — simulating NexStar 8SE\n",
               comPort.c_str());
        return true;
    }

    void Disconnect() override {
        if (!m_connected) return;
        m_connected = false;
        if (m_simThread.joinable()) m_simThread.join();
        printf("[MountEmulator] Disconnected\n");
    }

    bool IsConnected() const override { return m_connected; }

    // ── Position readout ───────────────────────────────────────

    bool GetRADec(double& raHours, double& decDegrees) override {
        if (!m_connected) return false;
        std::lock_guard<std::mutex> lk(m_mutex);
        raHours    = m_raHours;
        decDegrees = m_decDeg;
        return true;
    }

    bool GetAltAz(double& altDegrees, double& azDegrees) override {
        if (!m_connected) return false;
        // Simplified: return static values for emulator
        altDegrees = 45.0;
        azDegrees  = 180.0;
        return true;
    }

    // ── GoTo ───────────────────────────────────────────────────

    bool GoToRADec(double raHours, double decDegrees) override {
        if (!m_connected) return false;
        std::lock_guard<std::mutex> lk(m_mutex);
        m_gotoRA  = raHours;
        m_gotoDec = decDegrees;
        m_slewing = true;
        printf("[MountEmulator] GoTo RA=%.4f DEC=%.4f\n", raHours, decDegrees);
        return true;
    }

    bool IsSlewing() override {
        return m_slewing.load();
    }

    void CancelGoTo() override {
        m_slewing = false;
        printf("[MountEmulator] GoTo cancelled\n");
    }

    // ── Safety — NexStarMount compatibility stubs ──────────────
    // main.cpp casts to NexStarMount* for these methods; emulator provides no-ops.
    static constexpr int WATCHDOG_TIMEOUT_MS = 8000;
    void Heartbeat()     {} // no-op — no watchdog in emulator
    void EmergencyStop() { CancelGoTo(); StopSlew(0); StopSlew(1); }

    // ── Tracking ───────────────────────────────────────────────

    bool SetTrackingMode(TrackingMode mode) override {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_tracking = mode;
        printf("[MountEmulator] Tracking: %s\n", TrackingModeName(mode));
        return true;
    }

    TrackingMode GetTrackingMode() override {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_tracking;
    }

    // ── Slewing ────────────────────────────────────────────────

    bool SlewFixed(int axis, int direction, int rate) override {
        std::lock_guard<std::mutex> lk(m_mutex);
        // Store active slew command (axis 0=RA/AZM, 1=DEC/ALT)
        // direction 0=positive, 1=negative; rate 0-9
        if (axis == 0) {
            m_slewRateRA  = (direction == 0) ? rate : -rate;
        } else {
            m_slewRateDec = (direction == 0) ? rate : -rate;
        }
        if (rate > 0) {
            printf("[MountEmulator] Slew axis=%d dir=%d rate=%d\n",
                   axis, direction, rate);
        }
        return true;
    }

    void StopSlew(int axis) override {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (axis == 0) m_slewRateRA  = 0;
        else           m_slewRateDec = 0;
        printf("[MountEmulator] Stop axis=%d\n", axis);
    }

    // ── Sync ───────────────────────────────────────────────────

    bool SyncRADec(double raHours, double decDegrees) override {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_raHours = raHours;
        m_decDeg  = decDegrees;
        printf("[MountEmulator] Sync to RA=%.4f DEC=%.4f\n", raHours, decDegrees);
        return true;
    }

    // ── Info ───────────────────────────────────────────────────

    bool GetVersion(int& major, int& minor) override {
        major = 6; minor = 10;  // Plausible StarSense HC version
        return true;
    }

    bool IsAligned() override { return true; }


    static std::string FormatDec(double decDegrees) {
        char sign = (decDegrees >= 0) ? '+' : '-';
        double abs_dec = fabs(decDegrees);
        int d = (int)abs_dec;
        int m = (int)((abs_dec - d) * 60.0);
        int s = (int)(((abs_dec - d) * 60.0 - m) * 60.0);
        char buf[32];
        sprintf_s(buf, "%c%02d\xB0 %02d' %02d\"", sign, d, m, s);
        return std::string(buf);
    }

    static const char* TrackingModeName(TrackingMode mode) {
        switch (mode) {
            case OFF:      return "OFF";
            case ALT_AZ:   return "ALT-AZ";
            case EQ_NORTH: return "EQ-NORTH";
            case EQ_SOUTH: return "EQ-SOUTH";
            default:       return "UNKNOWN";
        }
    }

private:
    // Simulation loop — runs at 10 Hz
    // Handles: sidereal drift, GoTo slewing, manual jog
    void SimLoop() {
        // Degrees per arcsecond per tick (10Hz)
        // Sidereal rate: 15 arcsec/sec = 15/3600 deg/sec = 0.0015 deg/tick at 10Hz
        const double SIDEREAL_DEG_PER_TICK = 15.0 / 3600.0 / 10.0;
        // Manual jog: rate 9 ≈ 3 deg/sec = 0.3 deg/tick at 10Hz
        const double JOG_DEG_PER_TICK_PER_RATE = 3.0 / 9.0 / 10.0;
        // GoTo slew speed: ~2 deg/sec
        const double GOTO_DEG_PER_TICK = 2.0 / 10.0;

        while (m_connected) {
            {
                std::lock_guard<std::mutex> lk(m_mutex);

                if (m_slewing) {
                    // GoTo: move toward target
                    double dRA  = m_gotoRA  - m_raHours;
                    double dDec = m_gotoDec - m_decDeg;
                    // Wrap RA difference
                    if (dRA >  12.0) dRA -= 24.0;
                    if (dRA < -12.0) dRA += 24.0;
                    // Convert RA target delta to degrees for comparison
                    double dRADeg = dRA * 15.0;
                    double dist = sqrt(dRADeg * dRADeg + dDec * dDec);

                    if (dist < 0.01) {
                        // Arrived
                        m_raHours = m_gotoRA;
                        m_decDeg  = m_gotoDec;
                        m_slewing = false;
                        printf("[MountEmulator] GoTo complete\n");
                    } else {
                        double step = std::min(GOTO_DEG_PER_TICK, dist);
                        m_raHours += (dRADeg / dist) * step / 15.0;
                        m_decDeg  += (dDec   / dist) * step;
                    }
                } else {
                    // Sidereal drift when tracking is OFF
                    if (m_tracking == OFF) {
                        m_raHours -= SIDEREAL_DEG_PER_TICK / 15.0;
                    }
                    // tracking ON = no drift (mount compensates)

                    // Manual jog
                    if (m_slewRateRA != 0) {
                        double jogDeg = m_slewRateRA * JOG_DEG_PER_TICK_PER_RATE;
                        m_raHours += jogDeg / 15.0;
                    }
                    if (m_slewRateDec != 0) {
                        m_decDeg += m_slewRateDec * JOG_DEG_PER_TICK_PER_RATE;
                    }
                }

                // Wrap RA to 0-24
                while (m_raHours <  0.0) m_raHours += 24.0;
                while (m_raHours >= 24.0) m_raHours -= 24.0;
                // Clamp DEC to ±90
                m_decDeg = std::clamp(m_decDeg, -90.0, 90.0);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // ── IMount new methods ───────────────────────────────────
    const std::string& ConnectionId() const override { return m_comPort; }

    bool PulseGuide(GuideDirection dir, int durationMs) override {
        // Emulator: simulate a guide pulse by nudging position
        if (!m_connected) return false;
        std::lock_guard<std::mutex> lk(m_mutex);
        // Convert durationMs to a small position nudge
        // At sidereal rate, 1000ms ≈ 15 arcsec ≈ 0.00417 deg
        double nudgeDeg = (durationMs / 1000.0) * 15.0 / 3600.0;
        switch (dir) {
            case GuideDirection::North: m_decDeg   += nudgeDeg; break;
            case GuideDirection::South: m_decDeg   -= nudgeDeg; break;
            case GuideDirection::East:  m_raHours  += nudgeDeg / 15.0; break;
            case GuideDirection::West:  m_raHours  -= nudgeDeg / 15.0; break;
        }
        printf("[MountEmulator] PulseGuide dir=%d %dms\n", (int)dir, durationMs);
        return true;
    }

    bool IsPulseGuiding() override { return false; } // emulator: instantaneous

    bool        m_connected = false;
    std::string m_comPort;

    // Position (protected by m_mutex)
    double      m_raHours  = 5.5833;
    double      m_decDeg   = -5.3833;
    TrackingMode m_tracking = ALT_AZ;

    // GoTo state
    double      m_gotoRA  = 0.0;
    double      m_gotoDec = 0.0;
    std::atomic<bool> m_slewing{false};

    // Active jog rates (-9 to +9, 0=stopped)
    int         m_slewRateRA  = 0;
    int         m_slewRateDec = 0;

    std::mutex  m_mutex;
    std::thread m_simThread;
};

} // namespace raduk::core
