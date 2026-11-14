// ============================================================
//  Phase 153.2: recording's wiring, out of main.cpp
//  (src/app/RecordingController.h)
//
//    1. Begin: start, USB throttled, the window announced when there is
//       one, raw frames on screen; nothing when the start fails.
//    2. End: the one stop, USB restored; nothing when not recording.
//    3. Service: the gate and the hints published; the disk stop, the
//       limit and the derotation stop acted on, in that order; the
//       window warning once.
//    4. Complete: a sequence stopped by anything abnormal; auto-stack
//       only for a normal, non-empty SER outside a sequence, when on.
// ============================================================

#include "app/RecordingController.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace raduk::app;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

struct FakeCapture {
    enum class DiskStop { None = 0, DiskFull = 1, WriteFailed = 2 };
    static const char* DiskStopReason(DiskStop d) {
        return d == DiskStop::WriteFailed ? "disk write failed" : d == DiskStop::DiskFull ? "disk nearly full" : "";
    }
    bool recording = false, startOk = true, limit = false, derotStopped = false, gateOpen = true;
    DiskStop disk = DiskStop::None;
    double elapsed = 0.0;
    int stops = 0; bool lastNormal = true;
    uint32_t hx = 99, hy = 99; float hs = -1;
    bool StartRecording(int, int, int) { if (startOk) recording = true; return startOk; }
    void StopRecording(bool n) { recording = false; ++stops; lastNormal = n; }
    bool IsRecording() const { return recording; }
    DiskStop DiskStopped() const { return disk; }
    bool LimitReached() const { return limit; }
    bool DerotationStopped() const { return derotStopped; }
    double ElapsedSec() const { return elapsed; }
    void SetGateOpen(bool o) { gateOpen = o; }
    void SetFrameHints(uint32_t x, uint32_t y, float s) { hx = x; hy = y; hs = s; }
};

struct Rig {
    FakeCapture cap; RecordingUiSink ui;
    std::vector<bool> usb; std::vector<std::string> sessions;
    std::vector<std::pair<ToastLevel, std::string>> toasts; int fx = 0;
    RecordingControllerT<FakeCapture> c{cap, ui};
    Rig() {
        RecordingHooks h;
        h.usbCapturing = [this](bool on) { usb.push_back(on); };
        h.usbStatus = [] { return std::string("EAF 2 Hz"); };
        h.session = [this](const std::string& s) { sessions.push_back(s); };
        h.notify = [this](ToastLevel l, const std::string& s) { toasts.push_back({l, s}); };
        h.resetDisplayFx = [this] { ++fx; };
        c.SetHooks(h);
    }
};

int main() {
    printf("Recording controller (phase 153.2)\n");
    {
        Rig r;
        Check(r.c.Begin(640, 480, 8, false) && r.cap.recording, "Begin starts the recording");
        Check(r.usb.size() == 1 && r.usb[0] && r.sessions.back() == "USB capture mode \xE2\x80\x94 EAF 2 Hz" &&
              r.ui.Saw("[USB] Throttling EAF+mount: EAF 2 Hz") && r.fx == 1,
              "USB throttled, logged, raw frames on screen");
        Check(r.toasts.empty(), "no window, no notice");
        r.c.End(true, nullptr);
        Check(!r.cap.recording && r.cap.lastNormal && r.usb.back() == false &&
              r.ui.Saw("[REC] stopped (manual); USB polling restored: EAF 2 Hz"),
              "End stops, restores USB, says why");
        const int stops = r.cap.stops; const size_t usbN = r.usb.size();
        r.c.End(true, "again");
        Check(r.cap.stops == stops && r.usb.size() == usbN, "End does nothing when not recording");
    }
    {
        Rig r; r.cap.startOk = false;
        Check(!r.c.Begin(640, 480, 8, false) && r.usb.empty() && r.fx == 0, "a failed start changes nothing");
    }
    {
        Rig r; r.c.SetDerotWindow(222.0, "Jupiter");
        r.c.Begin(640, 480, 8, true);
        Check(r.ui.Saw("[DEROT] Jupiter: 222 s window, stop") && r.toasts.size() == 1 &&
              r.toasts[0].first == ToastLevel::Info &&
              r.toasts[0].second == "Jupiter: rotation blurs detail after 3.7 min (the run stops there)",
              "the window is announced, stopping", r.toasts.empty() ? "" : r.toasts[0].second);
        Rig q; q.c.SetDerotWindow(222.0, "Jupiter"); q.c.Begin(640, 480, 8, false);
        Check(!q.toasts.empty() && q.toasts[0].second.find("(it will warn)") != std::string::npos,
              "or warning");
        r.c.ClearDerotWindow();
        Check(r.c.DerotWindowSec() == 0.0 && r.c.DerotBody().empty(), "and cleared");
    }
    {
        Rig r; r.c.Begin(640, 480, 8, false);
        RecordingFrameView a; a.valid = true; a.centroidValid = true; a.centroidX = 320; a.centroidY = 200;
        a.sharpnessPct = 55.0f; a.seeingScore = 4.0f;
        Check(!r.c.Service(false, 6.0f, a) && r.cap.gateOpen, "gate off: frames pass");
        Check(r.cap.hx == 320 && r.cap.hy == 200 && r.cap.hs == 55.0f, "the writer's hints are published");
        Check(r.c.Service(true, 6.0f, a) && !r.cap.gateOpen, "gate on, seeing below: frames held back");
        a.seeingScore = 7.0f;
        Check(!r.c.Service(true, 6.0f, a) && r.cap.gateOpen, "seeing above: frames pass");
        RecordingFrameView none;
        Check(!r.c.Service(true, 6.0f, none) && r.cap.gateOpen && r.cap.hx == 0 && r.cap.hs == 0.0f,
              "no analysis yet: the gate stays open, no hints");
    }
    {
        Rig r; r.c.Begin(640, 480, 8, false);
        r.cap.disk = FakeCapture::DiskStop::WriteFailed; r.cap.limit = true;
        r.c.Service(false, 0, RecordingFrameView{});
        Check(!r.cap.recording && !r.cap.lastNormal && r.ui.Saw("stopped (disk write failed)") &&
              !r.toasts.empty() && r.toasts.back().first == ToastLevel::Error &&
              r.toasts.back().second.find("a disk write failed") != std::string::npos,
              "a failed write stops the run abnormally, ahead of the limit");
        Rig q; q.c.Begin(640, 480, 8, false); q.cap.disk = FakeCapture::DiskStop::DiskFull;
        q.c.Service(false, 0, RecordingFrameView{});
        Check(!q.toasts.empty() && q.toasts.back().second.find("nearly full") != std::string::npos,
              "a full disk says every frame was saved");
    }
    {
        Rig r; r.c.Begin(640, 480, 8, false); r.cap.limit = true;
        r.c.Service(false, 0, RecordingFrameView{});
        Check(!r.cap.recording && r.cap.lastNormal && r.ui.Saw("stopped (frame/time limit reached)") && r.toasts.empty(),
              "the limit stops the run normally");
        Rig q; q.c.Begin(640, 480, 8, false); q.cap.limit = true; q.cap.derotStopped = true;
        q.c.Service(false, 0, RecordingFrameView{});
        Check(q.ui.Saw("stopped (derotation window)") && !q.toasts.empty() &&
              q.toasts.back().second == "Run stopped at the derotation window",
              "the derotation stop says so");
    }
    {
        Rig r; r.c.SetDerotWindow(100.0, "Saturn"); r.c.Begin(640, 480, 8, false);
        const size_t n = r.toasts.size();
        r.cap.elapsed = 99.0; r.c.Service(false, 0, RecordingFrameView{});
        Check(r.toasts.size() == n, "no warning before the window");
        r.cap.elapsed = 101.0; r.c.Service(false, 0, RecordingFrameView{});
        Check(r.toasts.size() == n + 1 && r.toasts.back().first == ToastLevel::Warn && r.ui.Saw("window passed at 101 s"),
              "past it, one warning");
        r.cap.elapsed = 150.0; r.c.Service(false, 0, RecordingFrameView{});
        Check(r.toasts.size() == n + 1 && r.cap.recording, "only once, and the run goes on");
    }
    {
        Rig r;
        auto c = r.c.Complete("C:\\cap\\a.ser", true, true, false, false);
        Check(!c.abortAutorun && c.stack && !c.fitsNotice && r.ui.Saw("normal=1  autoStack=1"), "a normal SER is stacked");
        c = r.c.Complete("C:\\cap\\a.ser", false, true, false, false);
        Check(c.abortAutorun && !c.stack, "an abnormal end stops a sequence, no stack");
        c = r.c.Complete("", true, true, false, false);
        Check(!c.abortAutorun && !c.stack, "an empty run: nothing to stack");
        c = r.c.Complete("C:\\cap\\a.ser", true, false, false, false);
        Check(!c.stack, "auto-stack off: no stack");
        c = r.c.Complete("C:\\cap\\a.ser", true, true, true, false);
        Check(!c.stack && r.ui.Saw("passes over a run in a sequence"), "inside a sequence: no stack");
        c = r.c.Complete("C:\\cap\\a.fits", true, true, false, true);
        Check(!c.stack && c.fitsNotice && r.ui.Saw("passes over FITS"), "FITS: no stack, and a notice");
    }

    if (g_fail) { printf("rec_controller: %d FAILED\n", g_fail); return 1; }
    printf("rec_controller: all passed\n");
    return 0;
}
