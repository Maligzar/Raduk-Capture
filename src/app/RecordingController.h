#pragma once
// ============================================================
//  RecordingController -- Phase 153.2: recording's wiring, out of main.cpp
//
//  What main.cpp did round CaptureManager, moved here unchanged in
//  behaviour so it can be tested:
//    - Begin: start, throttle USB for the EAF and mount, and announce the
//      derotation window (phase 108) when the target has one.
//    - End: the one way a recording stops (phase 98), so USB polling is
//      restored whoever stopped it.
//    - Service, every frame while recording: publish the quality gate and
//      the writer's hints, and act on what the capture thread raised --
//      the disk stopping the run (135.1), the frame/time limit or the
//      derotation window, and the one-off warning when a run passes the
//      window it was not stopped at.
//    - Complete: what a finished recording means -- whether a sequence
//      must stop, and whether to auto-stack (SER only, never in a
//      sequence, never an empty or abnormal run).
//
//  main.cpp still builds the configuration from the panel and works the
//  derotation window out (it needs the GoTo panel and the ephemeris), and
//  acts on Complete's answer (Autorun and AutoStakkert live there). The
//  panel, USB manager and session log are reached through hooks.
//  A template over the capture manager, so a test uses a fake.
// ============================================================

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

#include "UiSink.h"

namespace raduk::app {

struct RecordingHooks {
    std::function<void(bool)>                          usbCapturing;   // throttle EAF and mount, or not
    std::function<std::string()>                       usbStatus;      // the USB manager's status line
    std::function<void(const std::string&)>            session;        // a System line in the session log
    std::function<void(ToastLevel, const std::string&)> notify;        // a notification (UTF-8)
    std::function<void()>                              resetDisplayFx; // raw frames on screen for capture
};

// What the gate and the writer's hints need from the latest analysis.
struct RecordingFrameView {
    bool     valid = false;
    bool     centroidValid = false;
    uint32_t centroidX = 0, centroidY = 0;
    float    sharpnessPct = 0.0f;
    float    seeingScore = 0.0f;
};

// What a finished recording asks of the rest of the app.
struct RecordingCompletion {
    bool abortAutorun = false;   // anything but a normal end stops a sequence
    bool stack = false;          // auto-stack this file
    bool fitsNotice = false;     // auto-stack passed over a FITS cube: say how to stack it
};

template <class Capture>
class RecordingControllerT {
public:
    RecordingControllerT(Capture& cap, IUiSink& ui) : m_cap(cap), m_ui(&ui) {}

    void SetHooks(RecordingHooks h) { m_hooks = std::move(h); }
    void SetUi(IUiSink& u)          { m_ui = &u; }

    // ── The derotation window (phase 108) ──────────────────────
    void SetDerotWindow(double sec, const std::string& body) {
        m_derotSec = sec; m_derotBody = body; m_derotWarned = false;
    }
    void ClearDerotWindow() { m_derotSec = 0.0; m_derotBody.clear(); m_derotWarned = false; }
    double DerotWindowSec() const { return m_derotSec; }
    const std::string& DerotBody() const { return m_derotBody; }

    // Start, with the configuration already set on the capture manager.
    bool Begin(int width, int height, int bits, bool stopAtWindow) {
        if (!m_cap.StartRecording(width, height, bits)) return false;
        Usb(true);
        const std::string st = UsbStatus();
        Session("USB capture mode \xE2\x80\x94 " + st);
        Line("[USB] Throttling EAF+mount: " + st + "\n");
        if (m_derotSec > 0.0) {
            char b[200];
            std::snprintf(b, sizeof(b), "[DEROT] %s: %.0f s window, %s\n",
                          m_derotBody.c_str(), m_derotSec, stopAtWindow ? "stop" : "warn");
            Line(b);
            std::snprintf(b, sizeof(b), "%s: rotation blurs detail after %.1f min (%s)",
                          m_derotBody.c_str(), m_derotSec / 60.0,
                          stopAtWindow ? "the run stops there" : "it will warn");
            Notify(ToastLevel::Info, b);
        }
        if (m_hooks.resetDisplayFx) m_hooks.resetDisplayFx();
        return true;
    }

    // The one way a recording stops.
    void End(bool normal, const char* why) {
        if (!m_cap.IsRecording()) return;
        m_cap.StopRecording(normal);
        Usb(false);
        const std::string st = UsbStatus();
        Session("USB polling restored \xE2\x80\x94 " + st);
        Line(std::string("[REC] stopped (") + (why ? why : "manual") + "); USB polling restored: " + st + "\n");
    }

    // Every frame while recording. Returns whether the gate is holding
    // frames back (the panel shows it when the gate is enabled).
    bool Service(bool gateEnabled, float gateThreshold, const RecordingFrameView& a) {
        const bool seenOk = !gateEnabled || !a.valid || a.seeingScore >= gateThreshold;
        // The capture thread submits; this publishes its policy.
        m_cap.SetGateOpen(seenOk);
        m_cap.SetFrameHints((a.valid && a.centroidValid) ? a.centroidX : 0u,
                            (a.valid && a.centroidValid) ? a.centroidY : 0u,
                            a.valid ? a.sharpnessPct : 0.0f);

        using DS = typename Capture::DiskStop;
        const DS ds = m_cap.DiskStopped();
        if (ds != DS::None) {
            End(false, Capture::DiskStopReason(ds));
            Notify(ToastLevel::Error, ds == DS::WriteFailed
                ? "Recording stopped: a disk write failed. Frames still in memory were lost - check the drive"
                : "Recording stopped: the disk is nearly full. Every captured frame was saved");
        } else if (m_cap.LimitReached()) {
            const bool derot = m_cap.DerotationStopped();
            End(true, derot ? "derotation window" : "frame/time limit reached");
            if (derot) Notify(ToastLevel::Info, "Run stopped at the derotation window");
        } else if (m_derotSec > 0.0 && !m_derotWarned && m_cap.ElapsedSec() >= m_derotSec) {
            m_derotWarned = true;
            char b[96];
            std::snprintf(b, sizeof(b), "[DEROT] window passed at %.0f s\n", m_cap.ElapsedSec());
            Line(b);
            Notify(ToastLevel::Warn, "Past the derotation window - rotation is "
                                     "blurring this run, unless you derotate it");
        }
        return !seenOk;
    }

    // A recording finished (CaptureManager's completion, any thread).
    RecordingCompletion Complete(const std::string& path, bool normal, bool autoStack,
                                 bool autorunRunning, bool isFits) {
        char b[600];
        std::snprintf(b, sizeof(b), "[STACK] recording complete: normal=%d  autoStack=%d  path=%s\n",
                      normal ? 1 : 0, autoStack ? 1 : 0, path.empty() ? "(empty)" : path.c_str());
        Line(b);
        RecordingCompletion c;
        c.abortAutorun = !normal;
        if (!normal || path.empty() || !autoStack) return c;
        // Stacking takes minutes of CPU, and the next run is seconds away.
        if (autorunRunning) {
            Line("[STACK] auto-stack passes over a run in a sequence: " + path + "\n");
            return c;
        }
        // A FITS cube only stacks with someone at AutoStakkert's window.
        if (isFits) {
            Line("[STACK] auto-stack passes over FITS: " + path + "\n");
            c.fitsNotice = true;
            return c;
        }
        c.stack = true;
        return c;
    }

private:
    void Usb(bool on)                     { if (m_hooks.usbCapturing) m_hooks.usbCapturing(on); }
    std::string UsbStatus() const         { return m_hooks.usbStatus ? m_hooks.usbStatus() : std::string(); }
    void Session(const std::string& s)    { if (m_hooks.session) m_hooks.session(s); }
    void Notify(ToastLevel l, const std::string& s) { if (m_hooks.notify) m_hooks.notify(l, s); }
    void Line(const std::string& s)       { m_ui->Line(s); }

    Capture&       m_cap;
    IUiSink*       m_ui;
    RecordingHooks m_hooks;
    double         m_derotSec = 0.0;
    std::string    m_derotBody;
    bool           m_derotWarned = false;
};

} // namespace raduk::app
