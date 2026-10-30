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
//  That last line was written before it was true. Naming a driver
//  did skip the refusal list, but main.cpp then dropped any
//  connected simulator unconditionally -- "simulator detected,
//  ignoring" -- so AscomProgId=ASCOM.Simulator.Telescope
//  connected and was thrown away, and the indoor run this file
//  exists to enable could not be done at all.
//
//  AcceptConnectedDriver is that second gate, now holding the
//  same rule as the first: refuse a simulator DISCOVERY found,
//  keep one the owner named. Both halves are pinned below.
//
//  What is checked here is the policy, lifted out of the search
//  so it can be read without COM or the registry. Whether a named
//  driver then connects is a different question and needs ASCOM
//  installed, so it belongs in docs/ASCOM-SIMULATOR-RUN.md.
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

static void Check(bool ok, const char* what) {
    if (ok) printf("  ok    %s\n", what);
    else  { printf("  FAIL  %s\n", what); ++g_fail; }
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

    // ── The second gate: what survives connecting ───────────
    //
    // The refusal list above decides what discovery will TRY. This
    // decides what is kept once a driver answers, and it is where the
    // generic path was being closed off.
    printf("\nAccepting a driver that connected\n");

    // A real mount is kept either way. Pinning is about simulators,
    // and must not become a general permission slip.
    Check(ASCOMMount::AcceptConnectedDriver(false, false),
          "a real mount discovered is kept");
    Check(ASCOMMount::AcceptConnectedDriver(false, true),
          "a real mount named in rig.ini is kept");

    // The whole point of the rejection: an unattended start must never
    // silently drive a pretend mount.
    Check(!ASCOMMount::AcceptConnectedDriver(true, false),
          "a simulator DISCOVERY found is refused, which is why the gate exists");

    // The whole point of the fix: naming one is the only way to drive
    // the generic ASCOM path without a mount on the desk.
    Check(ASCOMMount::AcceptConnectedDriver(true, true),
          "a simulator the owner NAMED is kept, so the indoor run is possible");

    // The two gates must not contradict each other. Every simulator in
    // the refusal list is refused by discovery and accepted when
    // pinned; if a future edit makes one of those untrue, the generic
    // path either reopens to unattended starts or closes again.
    {
        const char* sims[] = { "ASCOM.Simulator.Telescope",
                               "ASCOM.OmniSim.Telescope",
                               "ScopeSim.Telescope" };
        for (const char* id : sims) {
            const bool refusedByDiscovery = ASCOMMount::IsRefusedByDefault(id);
            Check(refusedByDiscovery &&
                  !ASCOMMount::AcceptConnectedDriver(true, false) &&
                   ASCOMMount::AcceptConnectedDriver(true, true),
                  id);
        }
    }

    if (g_fail == 0) printf("PASS\n"); else printf("FAIL: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
