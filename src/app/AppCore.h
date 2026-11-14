#pragma once
// ============================================================
//  AppCore -- Phase 153.0: the application's core, apart from its window
//
//  The composition root that main.cpp and a headless test both build.
//  It owns the devices and the frame pipeline, in the order RunApp built
//  them (so they are destroyed in the same order relative to each other),
//  and the seams a test replaces: a clock and a UI sink.
//
//  153.0 is the first step of strangling main.cpp (see
//  docs/Phase153-Plan-Main-Decomposition.md):
//    - The objects live here; main.cpp binds references to them under
//      their old names, so its other lines are untouched.
//    - The autofocus feeds have moved here (AutofocusFeed), and in 153.1
//      autofocus's wiring (AutofocusController). The rest of
//      the per-frame wiring moves as each controller is extracted
//      (153.1 autofocus, 153.2 recording, ... 153.5 tracking), and with
//      it the clock reaches each subsystem in turn.
//  Their configuration and starting order stay in main.cpp for now: the
//  headless harness (tests/app_headless.cpp) does its own.
// ============================================================

#include "../core/DeviceFactory.h"            // ASICamera, EAFFocuser (or their emulators)
#include "../core/AutofocusRoutine.h"
#include "../core/AutoExposureAssistant.h"
#include "../core/MountProxy.h"
#include "../core/RingBuffer.h"
#include "../core/CaptureThread.h"
#include "../core/FrameAnalyzer.h"
#include "../core/CentroidTracker.h"
#include "../core/CaptureManager.h"
#include "../core/PixelFormat.h"
#include "../core/SeeingMonitor.h"

#include "AutofocusController.h"
#include "AutorunController.h"
#include "AutofocusFeed.h"
#include "RecordingController.h"
#include "Clock.h"
#include "UiSink.h"

namespace raduk::app {

class AppCore {
public:
    // Devices and pipeline, in RunApp's order.
    raduk::core::ASICamera             cam;
    raduk::core::EAFFocuser            focuser;
    raduk::core::AutofocusRoutine      autofocus;
    raduk::core::AutoExposureAssistant ae;
    raduk::core::MountProxy            mountProxy;
    raduk::core::RingBuffer            ring;
    raduk::core::CaptureThread         capture;
    raduk::core::FrameAnalyzer         analyzer;
    raduk::core::CentroidTracker       tracker;
    raduk::core::CaptureManager        capturer;

    // Phase 153.1: autofocus's wiring (the button, AE before the sweep,
    // the Moon path, completion). The panel is reached through its hooks.
    using AutofocusController = AutofocusControllerT<raduk::core::AutofocusRoutine,
                                                     raduk::core::AutoExposureAssistant>;
    AutofocusController afCtl;
    // Phase 153.2: recording's wiring (start, the one stop, the per-frame
    // gate and stops, what a finished recording means).
    using RecordingController = RecordingControllerT<raduk::core::CaptureManager>;
    RecordingController recCtl;
    // Phase 153.3: the night's seeing against its best, and a sequence of
    // runs round AutorunRunner.
    raduk::core::SeeingMonitor seeing;
    AutorunController arCtl;

    AppCore() : afCtl(autofocus, ae, focuser, m_steady, m_null, raduk::core::AF_AE_SETTLE_LIMIT_S),
                recCtl(capturer, m_null),
                arCtl(seeing, m_null),
                m_clock(&m_steady), m_ui(&m_null) {}
    AppCore(const AppCore&) = delete;
    AppCore& operator=(const AppCore&) = delete;

    // The seams. Not owned; must outlive the core. Null restores the default.
    void SetClock(const IClock* c) { m_clock = c ? c : &m_steady; afCtl.SetClock(*m_clock); }
    void SetUi(IUiSink* u)         { m_ui = u ? u : &m_null; afCtl.SetUi(*m_ui); recCtl.SetUi(*m_ui); arCtl.SetUi(*m_ui); }
    const IClock& Clock() const    { return *m_clock; }
    IUiSink&      Ui()             { return *m_ui; }

    // ── The autofocus feeds, called from the frame loop ──────────
    // The newest frame, for the fine pass's edge score (phase 145.2).
    void FeedAutofocusFrame(const raduk::core::FrameSlot* frame,
                            const raduk::core::AnalysisResult& last) {
        m_afFeed.FastScore(autofocus, View(frame), View(last), *m_clock, m_ui);
    }
    // A fresh analysis, for the coarse sweep's score (1000/HFD).
    void FeedAutofocusAnalysis(const raduk::core::AnalysisResult& last, bool fresh) {
        AutofocusFeed::Sharpness(autofocus, View(last), fresh);
    }

    static FrameView View(const raduk::core::FrameSlot* f) {
        FrameView v;
        if (!f) return v;
        v.data = f->data; v.width = f->width; v.height = f->height;
        v.bitDepth = f->bitDepth;
        v.bayerRaw = raduk::core::IsBayerRaw(f->imgType);
        return v;
    }
    static AnalysisView View(const raduk::core::AnalysisResult& r) {
        AnalysisView v;
        v.valid = r.valid; v.centroidValid = r.centroidValid;
        v.centroidX = r.centroidX; v.centroidY = r.centroidY;
        v.centroidBgDN = r.centroidBgDN; v.centroidPeak = r.centroidPeak;
        v.surfaceTracked = r.surfaceTracked;
        v.focusValid = r.focusValid; v.focusScore = r.focusScore; v.focusHfdPx = r.focusHfdPx;
        return v;
    }

private:
    SteadyClock     m_steady;
    NullUiSink      m_null;
    const IClock*   m_clock;
    IUiSink*        m_ui;
    AutofocusFeed   m_afFeed;
};

} // namespace raduk::app
