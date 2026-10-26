// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once
// ============================================================
// MountProxy.h — Phase 46
//
// One mount pointer that never goes stale.
//
// The mount object is handed out at startup and kept: the centroid
// tracker, the GoTo panel, both sky maps and the floating mount
// controller each hold it, and the tracker calls through it from its
// own thread. That is fine while the mount is chosen once and lives
// for the whole session, which is how it worked until now.
//
// Reconnecting breaks it. A reconnect is not Connect() on the same
// object: the right backend may have changed since launch -- CPWI
// started, so ASCOM is now the answer where NexStar direct was; or the
// hand control came up on a different COM port, because Windows
// assigns those by which socket the cable landed in. Picking again
// means a NEW object, and every pointer handed out earlier would point
// at the old one.
//
// So this sits in the middle. Holders get the proxy, once, and keep it
// for the session; the backend swaps underneath. The proxy IS an
// IMount, so nothing above it has to know.
//
// Disconnected is a first-class state, not an error: every call
// forwards to the current backend, or returns the same "no" that a
// disconnected mount has always returned. Callers already guard on
// IsConnected() because the mount could always be absent at startup,
// and that guard keeps working unchanged.
//
// THREAD SAFETY. The backend pointer is atomic and each call loads it
// exactly once, so a swap cannot tear a call in half. Retired backends
// are KEPT ALIVE rather than deleted, because the tracker thread may
// be inside a 500 ms PulseGuide on one at the moment it is replaced --
// and a mutex here would mean the render loop's Heartbeat() waiting on
// that pulse, which is the serial block Phase 6.2 removed. A session
// retires a handful of backends at most, each a few hundred bytes, and
// they are all freed when the proxy dies. Memory is the cheap resource
// in that trade.
// ============================================================

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "IMount.h"

namespace raduk::core {

class MountProxy : public IMount {
public:
    MountProxy() = default;

    // Install a backend, retiring whatever was there. Returns the raw
    // pointer for the few places that need the concrete object -- the
    // crash-time emergency stop registers the NexStar instance, and it
    // must be the real one, not this wrapper.
    IMount* SetBackend(std::unique_ptr<IMount> next) {
        IMount* raw = next.get();
        std::lock_guard<std::mutex> lk(m_swapMutex);
        if (next) m_owned.push_back(std::move(next));
        m_backend.store(raw, std::memory_order_release);
        return raw;
    }

    // Forget the backend without destroying it: a thread may still be
    // inside a call on it. Everything below then answers as a
    // disconnected mount.
    void ClearBackend() {
        std::lock_guard<std::mutex> lk(m_swapMutex);
        m_backend.store(nullptr, std::memory_order_release);
    }

    // The live backend, or null. For code that genuinely needs the
    // concrete mount rather than the interface.
    IMount* Backend() const { return m_backend.load(std::memory_order_acquire); }
    bool    HasBackend() const { return Backend() != nullptr; }

    // How many backends this session has been through. Only of
    // interest to the log, where it answers "how many times did the
    // mount drop tonight".
    size_t  BackendGenerations() const {
        std::lock_guard<std::mutex> lk(m_swapMutex);
        return m_owned.size();
    }

    // ── IMount, forwarded ──────────────────────────────────────
    //
    // Each one loads the backend once into a local. Reloading it for a
    // second use inside the same call would be the tearing this exists
    // to prevent.

    bool Connect(const std::string& progID, int baudRate = 9600) override {
        IMount* b = Backend();
        return b ? b->Connect(progID, baudRate) : false;
    }
    void Disconnect() override {
        IMount* b = Backend();
        if (b) b->Disconnect();
    }
    bool IsConnected() const override {
        IMount* b = Backend();
        return b ? b->IsConnected() : false;
    }

    bool GetRADec(double& raHours, double& decDegrees) override {
        IMount* b = Backend();
        return b ? b->GetRADec(raHours, decDegrees) : false;
    }
    bool GetAltAz(double& altDegrees, double& azDegrees) override {
        IMount* b = Backend();
        return b ? b->GetAltAz(altDegrees, azDegrees) : false;
    }

    bool GoToRADec(double raHours, double decDegrees) override {
        IMount* b = Backend();
        return b ? b->GoToRADec(raHours, decDegrees) : false;
    }
    bool IsSlewing() override {
        IMount* b = Backend();
        return b ? b->IsSlewing() : false;
    }
    void CancelGoTo() override {
        IMount* b = Backend();
        if (b) b->CancelGoTo();
    }

    bool SetTrackingMode(TrackingMode mode) override {
        IMount* b = Backend();
        return b ? b->SetTrackingMode(mode) : false;
    }
    TrackingMode GetTrackingMode() override {
        IMount* b = Backend();
        return b ? b->GetTrackingMode() : OFF;
    }
    // Unknown, not "off". With no mount there is no mode to report,
    // and claiming OFF is a claim about hardware that is not there --
    // the distinction Phase 69 put this method here for.
    bool TrackingModeKnown() const override {
        IMount* b = Backend();
        return b ? b->TrackingModeKnown() : false;
    }

    bool SlewFixed(int axis, int direction, int rate) override {
        IMount* b = Backend();
        return b ? b->SlewFixed(axis, direction, rate) : false;
    }
    void StopSlew(int axis) override {
        IMount* b = Backend();
        if (b) b->StopSlew(axis);
    }
    bool SlewVariable(int axis, int direction, double arcsecPerSec) override {
        IMount* b = Backend();
        return b ? b->SlewVariable(axis, direction, arcsecPerSec) : false;
    }
    bool SupportsVariableRate() const override {
        IMount* b = Backend();
        return b ? b->SupportsVariableRate() : false;
    }

    bool SyncRADec(double raHours, double decDegrees) override {
        IMount* b = Backend();
        return b ? b->SyncRADec(raHours, decDegrees) : false;
    }
    bool IsAligned() override {
        IMount* b = Backend();
        return b ? b->IsAligned() : false;
    }

    bool PulseGuide(GuideDirection dir, int durationMs) override {
        IMount* b = Backend();
        return b ? b->PulseGuide(dir, durationMs) : false;
    }
    bool IsPulseGuiding() override {
        IMount* b = Backend();
        return b ? b->IsPulseGuiding() : false;
    }

    void Heartbeat() override {
        IMount* b = Backend();
        if (b) b->Heartbeat();
    }

    bool GetVersion(int& major, int& minor) override {
        IMount* b = Backend();
        return b ? b->GetVersion(major, minor) : false;
    }
    // A reference has to refer to something that outlives the call, so
    // this cannot forward a null backend's answer. m_noId is that
    // something, and it stays empty.
    const std::string& ConnectionId() const override {
        IMount* b = Backend();
        return b ? b->ConnectionId() : m_noId;
    }

private:
    std::atomic<IMount*> m_backend{nullptr};
    // Guards the vector, not the reads. Appending to it while another
    // thread holds a raw pointer into an element is safe -- unique_ptr
    // elements do not move their pointees when the vector grows.
    mutable std::mutex   m_swapMutex;
    std::vector<std::unique_ptr<IMount>> m_owned;
    const std::string    m_noId;
};

} // namespace raduk::core
