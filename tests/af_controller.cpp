// ============================================================
//  Phase 153.1: autofocus's wiring, out of main.cpp
//  (src/app/AutofocusController.h)
//
//    1. The button: NOT NOW asks for a second press within 5 s; a second
//       press inside it starts, one after it asks again; abort aborts.
//    2. Starting: no focuser says so; otherwise AE to AUTO, the panel
//       told, and the sweep held for AE.
//    3. The wait: the sweep starts when AE settles, when a recording
//       starts, or at the settle limit -- not before.
//    4. The Moon path: a frame-filling target gets the fine pass alone,
//       only when the fine stage is enabled.
//    5. Cancel is quiet; Abort tells the panel.
//    6. Completion: success and failure messages, and Autorun told.
// ============================================================

#include "app/AutofocusController.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace raduk::app;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

struct FakeRoutine {
    bool running = false; int starts = 0, aborts = 0, lastPos = -1; bool lastFine = false;
    bool IsRunning() const { return running; }
    void Abort() { ++aborts; running = false; }
    bool Start(int pos, bool fine) { ++starts; lastPos = pos; lastFine = fine; running = true; return true; }
};
struct FakeAe {
    bool enabled = false, autoMode = false, settled = false;
    void SetEnabled(bool e) { enabled = e; }
    void SetAuto(bool a) { autoMode = a; }
    bool Settled() const { return settled; }
};
struct FakeFocuser : raduk::core::IFocuser {
    bool open = true; int pos = 12345;
    bool Open(int) override { open = true; return true; }
    void Close() override { open = false; }
    bool IsOpen() const override { return open; }
    bool MoveTo(int p) override { pos = p; return true; }
    bool MoveRelative(int s) override { pos += s; return true; }
    bool Stop() override { return true; }
    bool IsMoving() const override { return false; }
    bool MoveToDirect(int p) override { pos = p; return true; }
    void SetApproachBacklash(int) override {}
    int  ApproachBacklash() const override { return 0; }
    void SetApproachDirection(int) override {}
    int   GetPosition() const override { return pos; }
    // Phase 161.0a.
    bool readFails = false;
    bool TryGetPosition(int& p) const override { if (readFails) return false; p = pos; return true; }
    bool MoveToFar(int p) override { pos = p; return true; }
    bool MoveToDirectFar(int p) override { pos = p; return true; }
    void SetMaxTravel(int) override {}
    int  MaxTravel() const override { return 0; }
    raduk::core::MoveRefusal LastRefusalKind() const override { return raduk::core::MoveRefusal::None; }
    std::string LastRefusal() const override { return ""; }
    int   GetMaxPosition() const override { return 200000; }
    float GetTemperature() const override { return 10.0f; }
    void SetReverse(bool) override {}
    void SetBacklash(int) override {}
    int  GetBacklash() const override { return 0; }
    int FocuserId() const override { return 0; }
};
struct Result { bool success; int bestPosition; float bestSharpness; float improvement; std::string message; };

struct Rig {
    FakeRoutine af; FakeAe ae; FakeFocuser f; ManualClock clk; RecordingUiSink ui;
    std::vector<std::string> statuses, sessions; std::vector<bool> running;
    int shown = 0, failed = 0;
    AutofocusControllerT<FakeRoutine, FakeAe> c{af, ae, f, clk, ui, 20.0};
    Rig() {
        AutofocusHooks h;
        h.running = [this](bool r) { running.push_back(r); };
        h.status  = [this](const std::string& s) { statuses.push_back(s); };
        h.showAutoExposure = [this] { ++shown; };
        h.session = [this](const std::string& s) { sessions.push_back(s); };
        h.failed  = [this] { ++failed; };
        c.SetHooks(h);
    }
    std::string LastStatus() const { return statuses.empty() ? "" : statuses.back(); }
};

int main() {
    printf("Autofocus controller (phase 153.1)\n");

    {
        Rig r;
        r.c.Press(true, false, "no target in the frame");
        Check(!r.c.Busy() && r.LastStatus().find("Not now: no target in the frame") == 0,
              "NOT NOW: the first press asks for a second", r.LastStatus());
        r.clk.Advance(3.0);
        r.c.Press(true, false, "no target in the frame");
        Check(r.c.WaitingForAE() && r.ui.Saw("pressed twice"), "a second press within 5 s starts anyway");
        r.c.Abort();
        r.clk.Advance(10.0);
        r.c.Press(true, false, "no target in the frame");
        Check(!r.c.Busy(), "after the window, it asks again");
        r.clk.Advance(6.0);
        r.c.Press(true, false, "no target in the frame");
        Check(!r.c.Busy(), "a press after 5 s is a first press again");
        r.c.Press(true, true, "");
        Check(r.c.WaitingForAE(), "ready: one press starts");
        r.c.Press(false, true, "");
        Check(!r.c.Busy() && r.af.aborts >= 1 && r.LastStatus() == "Aborted" && r.ui.Saw("Abort requested"),
              "the button's second state aborts and says so");
    }
    {
        Rig r; r.f.open = false;
        r.c.Start();
        Check(!r.c.Busy() && r.LastStatus() == "No focuser connected" && r.shown == 0,
              "no focuser: said, and nothing else changes");
    }
    {
        Rig r;
        r.c.Start();
        Check(r.ae.enabled && r.ae.autoMode && r.shown == 1 && !r.running.empty() && r.running.back(),
              "starting puts AE on AUTO, shows it, and marks the button running");
        Check(r.LastStatus() == "AE AUTO on: settling exposure before focus", "and says it is waiting for AE");
        r.clk.Advance(5.0);
        Check(!r.c.Tick(false, false, true) && r.af.starts == 0, "no sweep while AE has not settled");
        r.ae.settled = true;
        Check(r.c.Tick(false, false, true) && r.af.starts == 1 && r.af.lastPos == 12345 && !r.af.lastFine,
              "the sweep starts from the focuser's position once AE settles");
        Check(r.ui.Saw("AE settled after 5.0s") && r.LastStatus() == "Sweeping, AE held until it ends" &&
              !r.sessions.empty() && r.sessions.back() == "AutoFocus: starting from pos=12345",
              "with the log, panel and session lines");
        Check(!r.c.Tick(false, false, true) && r.af.starts == 1, "and only once");
    }
    {
        Rig r; r.c.Start(); r.clk.Advance(1.0);
        Check(r.c.Tick(true, false, true) && r.af.starts == 1, "a recording starting does not wait for AE");
    }
    {
        Rig r; r.c.Start(); r.clk.Advance(19.9);
        Check(!r.c.Tick(false, false, true), "not before the settle limit");
        r.clk.Advance(0.2);
        Check(r.c.Tick(false, false, true) && r.ui.Saw("not settled, going anyway"),
              "at the limit it goes anyway, and says so");
    }
    {
        Rig r; r.c.Start(); r.ae.settled = true;
        r.c.Tick(false, true, true);
        Check(r.af.lastFine && r.ui.Saw("fills the frame"), "the Moon: the fine pass alone");
        Rig q; q.c.Start(); q.ae.settled = true;
        q.c.Tick(false, true, false);
        Check(!q.af.lastFine, "not when the fine stage is off");
    }
    {
        Rig r; r.c.Start(); const size_t n = r.statuses.size();
        r.c.Cancel();
        Check(!r.c.Busy() && r.af.aborts == 1 && r.statuses.size() == n, "Cancel stops quietly");
    }
    {
        Rig r;
        r.c.OnComplete(Result{true, 168000, 82.5f, 12.25f, ""});
        Check(r.LastStatus() == "AF done: pos=168000  sharp=82.5%  +12.2%" &&
              r.ui.Saw("[AF] Complete: pos=168000  sharp=82.5%  improvement=+12.2%") &&
              !r.running.empty() && !r.running.back() && r.failed == 0,
              "success: the panel, the log, the button", r.LastStatus());
        r.c.OnComplete(Result{true, 168000, 82.5f, 12.25f, "not bracketed"});
        Check(r.LastStatus().find("+12.2% \xE2\x80\x94 not bracketed") != std::string::npos,
              "a caveat on success still reaches the panel");
        r.c.OnComplete(Result{false, 0, 0, 0, "Focus got worse"});
        Check(r.LastStatus() == "Focus got worse" && r.sessions.back() == "AutoFocus failed: Focus got worse" &&
              r.ui.Saw("[AF] Failed: Focus got worse") && r.failed == 1,
              "failure: said everywhere, and Autorun told");
    }

    // Phase 161.0a: the sweep is centred on the focuser's position, so a
    // position that could not be read starts nothing.
    {
        Rig r;
        r.ae.settled = true;
        r.c.Start();
        r.f.readFails = true;
        Check(!r.c.Tick(false, false, true) && r.af.starts == 0,
              "a position that cannot be read: no sweep", r.LastStatus());
        Check(!r.c.Busy() && r.failed == 1 && !r.running.empty() && !r.running.back() &&
              r.LastStatus().find("could not be read") != std::string::npos,
              "said on the panel, the button released, Autorun told");
        r.f.readFails = false;
        r.c.Start();
        Check(r.c.Tick(false, false, true) && r.af.starts == 1 && r.af.lastPos == 12345,
              "and the next press, with a good read, sweeps from it");
    }

    if (g_fail) { printf("af_controller: %d FAILED\n", g_fail); return 1; }
    printf("af_controller: all passed\n");
    return 0;
}
