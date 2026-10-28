// ============================================================
//  Which ASCOM telescope driver discovery will pick on its own
//
//  The generic path has never been exercised indoors, and the
//  reason was in the refusal list rather than in the hardware:
//  OmniSim, Simulator and ScopeSim were all refused, so there was
//  no driver left that could answer without a mount on the desk.
//  In a project that emulates its camera, its focuser and its
//  mount, that is the one device path with no bench story.
//
//  Three different kinds of thing were in one list:
//
//    ASIFocuser is a category error. A focuser among telescope
//    drivers is never right for anybody, so it is refused
//    unconditionally and this test pins that.
//
//    ASIMount and the hubs are facts about this rig, not about
//    ASCOM. They stay refused by default and the comments now say
//    which kind of reason they are.
//
//    The simulators are refused by default and reachable by
//    naming them, which is what makes the path testable.
//
//  What is checked here is the policy, lifted out of the search
//  so it can be read without COM or the registry. Whether a named
//  driver then connects is a different question and needs ASCOM
//  installed, so it belongs in CLEAR-NIGHT-CHECKLIST.md.
// ============================================================

#include "core/ASCOMMount.h"

#include <cstdio>
#include <string>

using namespace raduk::core;

static int g_fail = 0;

static void Refused(const char* id) {
    if (ASCOMMount::IsRefusedByDefault(id)) {
        printf("  ok    refused by default: %s\n", id);
    } else {
        printf("  FAIL  should be refused by default: %s\n", id);
        ++g_fail;
    }
}

static void Allowed(const char* id) {
    if (!ASCOMMount::IsRefusedByDefault(id)) {
        printf("  ok    allowed: %s\n", id);
    } else {
        printf("  FAIL  should be allowed: %s\n", id);
        ++g_fail;
    }
}

int main() {
    printf("ASCOM: what discovery refuses on its own\n");

    // A focuser is not a telescope. This one is not a preference
    // and must never become configurable.
    Refused("ASCOM.ASIFocuser.Focuser");

    // This rig's own reasons: no ZWO mount here, and ASIMount
    // takes COM3 which the NexStar serial path needs.
    Refused("ASCOM.ASIMount.Telescope");

    // Hubs. Correct as a default, wrong as a law: DeviceHub is the
    // standard way to share one mount with PHD2.
    Refused("ASCOM.DeviceHub.Telescope");
    Refused("ASCOM.JustAHub.Telescope");

    // The simulators. Refused for discovery so an unattended start
    // never silently points at a pretend mount, which is exactly
    // the failure that would waste a clear night.
    Refused("ASCOM.Simulator.Telescope");
    Refused("ASCOM.OmniSim.Telescope");
    Refused("ScopeSim.Telescope");

    // Everything else discovery may have.
    Allowed("ASCOM.CPWI.Telescope");
    Allowed("CPWI.Telescope");
    Allowed("Celestron.Telescope");
    Allowed("ASCOM.EQMOD.Telescope");
    Allowed("ASCOM.SkyWatcher.Telescope");

    // The matching is a substring search, so a name that merely
    // contains an allowed vendor is still judged on the refused
    // tokens. Pinned here because a future tidy-up to exact
    // matching would change behaviour silently.
    Refused("ASCOM.Celestron.DeviceHub.Telescope");

    if (g_fail == 0) printf("PASS\n"); else printf("FAIL: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
