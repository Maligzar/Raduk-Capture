#pragma once
// ============================================================
// MountEmulator.h — Software emulator for Celestron NexStar mount
// Implements IMount — same interface as ASCOMMount and NexStarMount.
// Simulates RA/DEC drift, slewing, tracking modes, PulseGuide.
// Enabled when RADUK_EMULATOR is defined in CMakeLists.txt.
//
// Phase 52 — closed-loop tracking emulation:
// The emulator models a fixed "sky target" (the planet). The pointing
// error (target − mount) is exposed via GetPointingErrorArcsec(), which
// CameraEmulator uses to render the planet displaced from frame centre.
// Guide pulses move the mount → error shrinks → planet re-centres, so
// the full CentroidTracker correction loop runs closed in emulation.
// Imperfect tracking is simulated by drifting the mount away from the
// target at a configurable rate even while "tracking" is on.
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
#include <algorithm>   // std::clamp - header was relying on its includer
#include <thread>
#include <atomic>
#include <mutex>
#include "RigConfig.h"
#include "SkyMath.h"
#include <ctime>
#include <chrono>
#include "IMount.h"

namespace raduk::core {

// Note: this class was previously named NexStarMount, colliding with the
// real serial backend's class name (ODR landmine if both headers were ever
// included). The alias below keeps existing emulator-build code compiling.
class MountEmulator : public IMount {
public:
    MountEmulator()  = default;
    ~MountEmulator() override { Disconnect(); }

    MountEmulator(const MountEmulator&) = delete;
    MountEmulator& operator=(const MountEmulator&) = delete;

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
        m_simThread = std::thread(&MountEmulator::SimLoop, this);
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

        // Phase 67: derive this from the emulated pointing instead of
        // returning constants. Static alt/az made the emulator useless
        // for anything azimuth-dependent -- cord wrap could never
        // accumulate, the ADC readout was always the same, and the sky
        // map's mount marker never moved. A test rig that cannot
        // reproduce a behaviour cannot verify it.
        double raH = 0.0, decD = 0.0;
        { std::lock_guard<std::mutex> lk(m_mutex); raH = m_raHours; decD = m_decDeg; }

        // Shared with the sky map and the cord-wrap predictor (SkyMath.h).
        // This was a local copy; three copies of the same conversion
        // drift, and an emulator that disagrees with the sky map is a
        // test rig that lies.
        RADecToAltAzNow(raH, decD, altDegrees, azDegrees);
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
    static constexpr int WATCHDOG_TIMEOUT_MS = 8000;
    void Heartbeat() override {} // no-op — no watchdog in emulator
    void EmergencyStop() { CancelGoTo(); StopSlew(0); StopSlew(1); }

    // ── Phase 52: closed-loop sky model ────────────────────────
    // The "planet" sits at a fixed sky position; CameraEmulator renders it
    // displaced from frame centre by (target − mount pointing).

    void SetSkyTarget(double raHours, double decDeg) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_targetRA  = raHours;
        m_targetDec = decDeg;
    }

    // Simulated tracking imperfection: the mount drifts away from the target
    // at this rate (arcsec/s on each axis) while tracking is ON. Gives the
    // CentroidTracker a real error signal to correct.
    void SetTrackingErrorRate(float raArcsecPerSec, float decArcsecPerSec) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_trackErrRA  = raArcsecPerSec;
        m_trackErrDec = decArcsecPerSec;
    }

    // Pointing error (target − mount) in arcsec. RA error is on-sky
    // (scaled by cos(dec)). Returns false when not connected — the camera
    // then renders the planet centred, matching the old open-loop behavior.
    // Thread-safe: called from CameraEmulator's frame-generator thread.
    bool GetPointingErrorArcsec(float& errRA, float& errDec) {
        if (!m_connected) return false;
        std::lock_guard<std::mutex> lk(m_mutex);
        double dRA = m_targetRA - m_raHours;
        if (dRA >  12.0) dRA -= 24.0;
        if (dRA < -12.0) dRA += 24.0;
        errRA  = (float)(dRA * 15.0 * 3600.0 * cos(m_decDeg * 3.14159265358979 / 180.0));
        errDec = (float)((m_targetDec - m_decDeg) * 3600.0);
        return true;
    }

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

    // Phase 67: RADUK_EMU_UNALIGNED=1 makes the emulator report itself
    // unaligned, so the whole tracking-blocked path -- the align
    // helper, refused acquisition, the warnings -- can be reproduced
    // without waiting for a cloudless night and an unaligned mount.
    bool IsAligned() override {
        char buf[8] = {};
        if (GetEnvironmentVariableA("RADUK_EMU_UNALIGNED", buf, sizeof(buf)) > 0
            && buf[0] == '1')
            return false;
        return true;
    }


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
    // Celestron NexStar fixed slew rates, arcsec/sec.
    //
    // Rates 1-7 are multiples of sidereal and 8-9 are the fast slew
    // rates, so the curve is roughly LOGARITHMIC. The emulator used to
    // interpolate it linearly as rate * (3 deg/s / 9), which made FINE
    // (rate 5) 1.67 deg/s instead of 241 arcsec/s - twenty-five times
    // too fast. A fine nudge threw the planet straight out of frame, so
    // nothing that depends on a SMALL mount move - centroid tracking
    // through a nudge, the motion plot, guide-pulse calibration - could
    // be tested on the bench at all.
    static double JogArcsecPerSec(int rate) {
        static const double kRate[10] = {
            0.0,        // 0 = stopped
            7.52,       // 1 = 0.5x sidereal
            15.04,      // 2 = 1x sidereal
            60.2,       // 3 = 4x
            120.3,      // 4 = 8x
            240.7,      // 5 = 16x    <- FINE
            481.3,      // 6 = 32x
            962.6,      // 7 = 64x    <- MED
            3600.0,     // 8 = 1 deg/s  <- FAST
            10800.0     // 9 = 3 deg/s  <- max slew
        };
        int r = rate < 0 ? -rate : rate;
        if (r > 9) r = 9;
        return kRate[r];
    }

    // Handles: sidereal drift, GoTo slewing, manual jog
    void SimLoop() {
        // Degrees per arcsecond per tick (10Hz)
        // Sidereal rate: 15 arcsec/sec = 15/3600 deg/sec = 0.0015 deg/tick at 10Hz
        const double SIDEREAL_DEG_PER_TICK = 15.0 / 3600.0 / 10.0;
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
                        // Arrived. Phase 52: the object slewed to IS the sky
                        // target, and the mount lands with a realistic GoTo
                        // pointing error (~45"/25") so acquisition starts with
                        // the planet off-centre and Centering has real work.
                        m_raHours   = m_gotoRA - (45.0 / 3600.0 / 15.0);
                        m_decDeg    = m_gotoDec - (25.0 / 3600.0);
                        m_targetRA  = m_gotoRA;
                        m_targetDec = m_gotoDec;
                        m_slewing = false;
                        printf("[MountEmulator] GoTo complete (sim pointing error 45\"/25\")\n");
                    } else {
                        double step = std::min(GOTO_DEG_PER_TICK, dist);
                        m_raHours += (dRADeg / dist) * step / 15.0;
                        m_decDeg  += (dDec   / dist) * step;
                    }
                } else {
                    // Sidereal drift when tracking is OFF
                    if (m_tracking == OFF) {
                        m_raHours -= SIDEREAL_DEG_PER_TICK / 15.0;
                    } else {
                        // Phase 52: tracking ON = mount compensates, but not
                        // perfectly — drift away from the target at the
                        // configured rate so the guide loop has work to do.
                        m_raHours -= (m_trackErrRA  / 3600.0 / 15.0) * 0.1; // per 100ms tick
                        m_decDeg  -= (m_trackErrDec / 3600.0)        * 0.1;
                    }

                    // Manual jog, on the real rate curve. 0.1 = one 10Hz
                    // tick; sign comes from the stored signed rate.
                    if (m_slewRateRA != 0) {
                        double as = JogArcsecPerSec(m_slewRateRA) * 0.1;
                        if (m_slewRateRA < 0) as = -as;
                        m_raHours += (as / 3600.0) / 15.0;
                    }
                    if (m_slewRateDec != 0) {
                        double as = JogArcsecPerSec(m_slewRateDec) * 0.1;
                        if (m_slewRateDec < 0) as = -as;
                        m_decDeg += as / 3600.0;
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

    // Phase 52: sky target ("the planet") + simulated tracking error rate.
    // Defaults match the connect position so the loop starts near-centred.
    double      m_targetRA    = 5.5833;
    double      m_targetDec   = -5.3833;
    float       m_trackErrRA  = 0.3f;   // arcsec/s
    float       m_trackErrDec = 0.1f;   // arcsec/s

    // Active jog rates (-9 to +9, 0=stopped)
    int         m_slewRateRA  = 0;
    int         m_slewRateDec = 0;

    std::mutex  m_mutex;
    std::thread m_simThread;
};

// Emulator builds historically instantiated this class as NexStarMount
// (DeviceFactory swaps headers, main.cpp code is shared). Keep that name
// working without the ODR hazard of two classes sharing it.
using NexStarMount = MountEmulator;

} // namespace raduk::core
