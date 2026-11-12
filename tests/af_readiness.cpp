// ============================================================
//  Phase 139.3: autofocus readiness, one case per reason
//
//  A good Jupiter frame is READY; each case breaks one thing and must
//  get NOT NOW with that reason, and only that reason. Unknown seeing is
//  not held against it.
// ============================================================

#include "core/AfReadiness.h"

#include <cstdio>
#include <functional>
#include <string>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-58s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static AfReadyIn Good() {
    AfReadyIn in;
    in.focuserOpen = true; in.targetFound = true; in.focusValid = true;
    in.hfdPx = 18; in.framePct = 1.5f; in.clipPct = 0; in.peakDN = 180;
    in.cx = 640; in.cy = 480; in.fw = 1280; in.fh = 960;
    in.tracking = true;
    return in;
}

int main() {
    printf("Autofocus readiness (phase 139.3)\n\n");
    Check(AutofocusReadiness(Good()).ready, "a well exposed, centred, tracked planet: READY");

    struct Case { const char* name; std::function<void(AfReadyIn&)> f; const char* expect; };
    const Case cases[] = {
        {"no focuser",           [](AfReadyIn& i) { i.focuserOpen = false; },   "no focuser"},
        {"autofocus running",    [](AfReadyIn& i) { i.afRunning = true; },      "is running"},
        {"focuser moving",       [](AfReadyIn& i) { i.focuserMoving = true; },  "moving"},
        {"recording",            [](AfReadyIn& i) { i.recording = true; },      "recording"},
        {"empty frame",          [](AfReadyIn& i) { i.targetFound = false; },   "no target"},
        {"no focus measure",     [](AfReadyIn& i) { i.focusValid = false; },    "cannot be measured"},
        {"Moon filling the frame", [](AfReadyIn& i) { i.framePct = 80; },       "fills the frame"},
        {"tiny target",          [](AfReadyIn& i) { i.hfdPx = 2; },             "too small"},
        {"clipped",              [](AfReadyIn& i) { i.clipPct = 6; },           "clipped"},
        {"faint",                [](AfReadyIn& i) { i.peakDN = 25; },           "too faint"},
        {"near the left edge",   [](AfReadyIn& i) { i.cx = 60; },               "near the edge"},
        {"near the bottom edge", [](AfReadyIn& i) { i.cy = 920; },              "near the edge"},
        {"mount not tracking",   [](AfReadyIn& i) { i.tracking = false; },      "not tracking"},
        {"fading",               [](AfReadyIn& i) { i.fading = true; },         "fading"},
        {"unsteady seeing",      [](AfReadyIn& i) { i.seeingSteady = 0; },      "unsteady"},
    };
    for (const auto& c : cases) {
        AfReadyIn in = Good();
        c.f(in);
        const AfReadyOut o = AutofocusReadiness(in);
        Check(!o.ready && o.why.find(c.expect) != std::string::npos, std::string("NOT NOW: ") + c.name, o.why);
    }
    {
        AfReadyIn in = Good(); in.seeingSteady = 1;
        Check(AutofocusReadiness(in).ready, "steady seeing: READY");
        in.seeingSteady = -1;
        Check(AutofocusReadiness(in).ready, "seeing not known: not held against it");
    }
    {
        AfReadyIn in = Good(); in.focuserOpen = false; in.targetFound = false;
        Check(AutofocusReadiness(in).why.find("focuser") != std::string::npos,
              "several things wrong: the first one that applies is named");
    }

    // Phase 145.3: a frame-filling Moon has no disc and no HFD, but the
    // fine pass's edge metric can focus on the surface.
    {
        AfReadyIn in = Good();
        in.filled = true; in.targetFound = false; in.focusValid = false; in.framePct = 100;
        in.edgeOnlyOk = true;
        Check(AutofocusReadiness(in).ready, "a filled frame with the fine pass on: READY (no disc needed)");
        in.edgeOnlyOk = false;
        Check(AutofocusReadiness(in).why.find("AutofocusFineStage") != std::string::npos,
              "with it off: NOT NOW, and it says which setting would do it");
        in.edgeOnlyOk = true; in.tracking = false;
        Check(AutofocusReadiness(in).why.find("tracking") != std::string::npos,
              "a filled frame still needs the mount tracking");
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
