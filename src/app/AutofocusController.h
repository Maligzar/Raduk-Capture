#pragma once
// ============================================================
//  AutofocusController -- Phase 153.1: autofocus's wiring, out of main.cpp
//
//  What main.cpp did round AutofocusRoutine, moved here unchanged in
//  behaviour so it can be tested:
//    - The button (phase 139.3): when the readiness check says NOT NOW,
//      the first press asks for a second within 5 s instead of refusing.
//    - Starting (phase 112): AE to AUTO, the camera section shown, and
//      the sweep held until AE has settled, a recording has started, or
//      AF_AE_SETTLE_LIMIT_S has passed.
//    - The Moon path (phase 145.3): a target that fills the frame gets
//      the edge metric's fine pass alone.
//    - AE between the stages (phase 161): released at the coarse focus,
//      held again for the fine pass.
//    - Aborting, and the quiet cancel the emergency paths use.
//    - The completion messages, the panel's and the session log's,
//      delivered on the frame loop's thread (phase 161.1).
//
//  The panel and the session log are reached through hooks (as
//  ProfileApplyHooks does), the log through IUiSink, and time through
//  IClock. A template over the routine and the AE assistant, so a test
//  drives it with fakes; the app uses AutofocusRoutine and
//  AutoExposureAssistant.
// ============================================================

#include <cstdio>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>

#include "../core/IFocuser.h"
#include "Clock.h"
#include "UiSink.h"

namespace raduk::app {

// What the controller changes outside itself.
struct AutofocusHooks {
    std::function<void(bool)>               running;          // the AUTO FOCUS button's state
    std::function<void(const std::string&)> status;           // the autofocus status line (UTF-8)
    std::function<void()>                   showAutoExposure; // CAMERA open, AE shown as AUTO
    std::function<void(const std::string&)> session;          // a Focuser line in the session log
    std::function<void()>                   failed;           // a sweep failed (Autorun reads it)
};

template <class Routine, class Ae>
class AutofocusControllerT {
public:
    static constexpr double kConfirmWindowSec = 5.0;

    AutofocusControllerT(Routine& af, Ae& ae, raduk::core::IFocuser& focuser,
                         const IClock& clock, IUiSink& ui, double aeSettleLimitSec)
        : m_af(af), m_ae(ae), m_focuser(focuser), m_clock(&clock), m_ui(&ui),
          m_settleLimit(aeSettleLimitSec) {}

    void SetHooks(AutofocusHooks h) { m_hooks = std::move(h); }
    void SetClock(const IClock& c)  { m_clock = &c; }
    void SetUi(IUiSink& u)          { m_ui = &u; }

    // Autofocus is sweeping, or waiting for AE before it does.
    // Phase 161.1: or finished, with the result not yet delivered. Busy
    // must not read false before the hooks (the button, the status line,
    // Autorun's "failed") have run.
    bool Busy() const { return m_af.IsRunning() || m_waitingForAE || m_resultPending.load(); }
    bool WaitingForAE() const { return m_waitingForAE; }

    // The AUTO FOCUS button. `ready`/`why` are the readiness check's.
    void Press(bool start, bool ready, const std::string& why) {
        if (!start) {
            Line("[AF] Abort requested\n");
            Abort();
            return;
        }
        const double now = m_clock->Now();
        if (!ready && m_focuser.IsOpen() && now > m_confirmUntil) {
            m_confirmUntil = now + kConfirmWindowSec;
            Status("Not now: " + why + ". Press again within 5 s to focus anyway");
            Line("[AF] Not ready (" + why + "): asked for a second press\n");
            return;
        }
        if (!ready) Line("[AF] Started although not ready (" + why + "): the owner pressed twice\n");
        m_confirmUntil = now - 1.0;
        Start();
    }

    // Every way of starting autofocus comes here (the button, Autorun,
    // a remote command): AE to AUTO, then the sweep once it has settled.
    void Start() {
        if (!m_focuser.IsOpen()) {
            Status("No focuser connected");
            return;
        }
        if (m_hooks.showAutoExposure) m_hooks.showAutoExposure();
        m_ae.SetEnabled(true);
        m_ae.SetAuto(true);
        if (m_hooks.running) m_hooks.running(true);
        Status("AE AUTO on: settling exposure before focus");
        Line("[AF] AE AUTO on; waiting for it to settle before the sweep\n");
        m_waitingForAE = true;
        m_waitStart = m_clock->Now();
    }

    // The user's abort: the panel says so.
    void Abort() {
        m_waitingForAE = false;
        m_af.Abort();
        if (m_hooks.running) m_hooks.running(false);
        Status("Aborted");
    }

    // The emergency paths' stop: no message of its own.
    void Cancel() {
        m_waitingForAE = false;
        m_af.Abort();
    }

    // From the 500 ms readout, after AE's tick. Starts the sweep once AE
    // is done or plainly will not be. Returns whether it started one.
    bool Tick(bool recording, bool targetFillsFrame, bool fineStageEnabled) {
        if (!m_waitingForAE) return false;
        const double waited = m_clock->Now() - m_waitStart;
        const bool settled = m_ae.Settled();
        if (!(settled || recording || waited >= m_settleLimit)) return false;
        m_waitingForAE = false;
        // Phase 161.0a: the sweep is centred on this, so it must be a
        // position that was read, not the last one remembered or 0.
        int pos = 0;
        if (!m_focuser.TryGetPosition(pos)) {
            if (m_hooks.running) m_hooks.running(false);
            Status("The focuser's position could not be read: autofocus not started");
            Session("AutoFocus not started: the focuser's position could not be read");
            Line("[AF] Not started: the focuser's position could not be read\n");
            if (m_hooks.failed) m_hooks.failed();
            return false;
        }
        char b[160];
        std::snprintf(b, sizeof(b), "[AF] AE %s after %.1fs; V-curve scan from pos=%d\n",
                      settled ? "settled" : "not settled, going anyway", waited, pos);
        Line(b);
        std::snprintf(b, sizeof(b), "AutoFocus: starting from pos=%d", pos);
        Session(b);
        Status("Sweeping, AE held until it ends");
        // A frame-filling target (the Moon) has no disc for the coarse
        // HFD sweep: the edge metric's fine pass runs alone.
        const bool fineOnly = targetFillsFrame && fineStageEnabled;
        if (fineOnly) Line("[AF] the target fills the frame: fine pass on the edge metric only\n");
        if (!m_af.Start(pos, fineOnly)) {
            if (m_hooks.running) m_hooks.running(false);
            Status("Autofocus could not start");
            Line("[AF] Not started: the routine refused (already running, or no focuser)\n");
            if (m_hooks.failed) m_hooks.failed();
            return false;
        }
        return true;
    }

    // ── Phase 161: AE is part of autofocus, start to finish ────
    // Before the sweep it settles (Start and Tick above). During a stage
    // it is held, because a change inside a stage bends that stage's
    // curve. Between the coarse sweep and the fine pass the routine
    // parks at the coarse focus and asks for AE again, since exposure
    // settled on a defocused disc can clip a focused one.
    //
    // What AE is held for right now, or nullptr when it may act.
    const char* ExposureHold() const {
        return (m_af.IsRunning() && !m_af.WantsExposure()) ? "autofocus" : nullptr;
    }
    // After AE's tick, each time round. Tells the routine AE is done when
    // it has looked and has nothing to change, or cannot act at all (not
    // in AUTO, or a recording holds it), so the routine never waits on an
    // answer that is not coming.
    void ExposureTicked(bool recording) {
        if (!m_af.WantsExposure()) return;
        if (recording || !m_ae.IsAuto() || m_ae.Settled()) m_af.ExposureSettled();
    }

    // The routine's completion. Called on the routine's own thread.
    //
    // Phase 161.1: it only leaves the result here. The hooks set window
    // text and write strings the window's thread also writes, and they
    // used to run on the scan thread: a data race, and a deadlock if the
    // window's thread was waiting in join() when SetWindowText tried to
    // reach it. Deliver(), called from the frame loop, runs them on the
    // thread the window belongs to.
    template <class Result>
    void OnComplete(const Result& r) {
        std::lock_guard<std::mutex> lk(m_resultMx);
        m_result = [this, r] { Complete(r); };
        m_resultPending.store(true);
    }

    // From the frame loop, every pass. Returns whether a result was
    // delivered.
    bool Deliver() {
        std::function<void()> f;
        {
            std::lock_guard<std::mutex> lk(m_resultMx);
            if (!m_result) return false;
            f = std::move(m_result);
            m_result = nullptr;
        }
        f();
        m_resultPending.store(false);   // after the hooks: Busy() until they have run
        return true;
    }

private:
    template <class Result>
    void Complete(const Result& r) {
        if (m_hooks.running) m_hooks.running(false);
        char b[320];
        if (r.success) {
            const char* sep = r.message.empty() ? "" : " \xE2\x80\x94 ";   // " — "
            std::snprintf(b, sizeof(b), "AF done: pos=%d  sharp=%.1f%%  +%.1f%%%s%s",
                          r.bestPosition, r.bestSharpness, r.improvement, sep, r.message.c_str());
            Status(b);
            std::snprintf(b, sizeof(b), "AutoFocus: pos=%d  sharp=%.1f%%  +%.1f%%%s%s",
                          r.bestPosition, r.bestSharpness, r.improvement, sep, r.message.c_str());
            Session(b);
            std::snprintf(b, sizeof(b), "[AF] Complete: pos=%d  sharp=%.1f%%  improvement=+%.1f%%%s%s\n",
                          r.bestPosition, r.bestSharpness, r.improvement, sep, r.message.c_str());
            Line(b);
        } else {
            Status(r.message);
            Session("AutoFocus failed: " + r.message);
            Line("[AF] Failed: " + r.message + "\n");
            if (m_hooks.failed) m_hooks.failed();
        }
    }


    void Status(const std::string& s)  { if (m_hooks.status) m_hooks.status(s); }
    void Session(const std::string& s) { if (m_hooks.session) m_hooks.session(s); }
    void Line(const std::string& s)    { m_ui->Line(s); }

    Routine&               m_af;
    Ae&                    m_ae;
    raduk::core::IFocuser& m_focuser;
    const IClock*          m_clock;
    IUiSink*               m_ui;
    double                 m_settleLimit;
    AutofocusHooks         m_hooks;
    bool                   m_waitingForAE = false;
    double                 m_waitStart = 0.0;
    double                 m_confirmUntil = -1e18;
    std::mutex             m_resultMx;            // phase 161.1
    std::function<void()>  m_result;
    std::atomic<bool>      m_resultPending{false};
};

} // namespace raduk::app
