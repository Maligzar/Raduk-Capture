// ============================================================
//  One button, three devices: what it shows and what it says
//
//  Phase 46's device calls cannot run on the bench -- they are a
//  camera, a focuser and a mount. The decisions around them can, and
//  they are where this is easy to get quietly wrong:
//
//    - what the button reports when only two of three answered
//    - whether pressing it on a partial link picks up the missing
//      device or tears down the working ones
//    - whether the sentence afterwards describes what HAPPENED or
//      what was attempted
//
//  The last is the one with teeth. The button is pressed in the dark
//  at the start of a session by someone who then walks away from the
//  screen, and "Hardware connected" when the mount never answered is
//  how a night is lost to a telescope that was never going to track.
// ============================================================

#include "core/HardwareLink.h"

#include <cstdio>
#include <string>

using namespace raduk::core;

static int g_fail = 0;

static void Check(bool ok, const std::string& what, const std::string& detail) {
    printf("  [%s] %-52s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static DeviceLinks L(bool cam, bool foc, bool mnt) {
    DeviceLinks d; d.camera = cam; d.focuser = foc; d.mount = mnt; return d;
}

int main() {
    printf("\n=== Hardware link: the [HW] button ===\n");

    printf("\n1. What it reports\n");
    {
        Check(LinkStateOf(L(false,false,false)) == LinkState::None,
              "nothing connected reads None", LinkLabel(L(false,false,false)));
        Check(LinkStateOf(L(true,true,true)) == LinkState::All,
              "all three reads All", LinkLabel(L(true,true,true)));
        // Every partial combination, because "two of three" is the
        // state a rig spends real time in and there are six ways in.
        const DeviceLinks partials[] = {
            L(true,false,false), L(false,true,false), L(false,false,true),
            L(true,true,false),  L(true,false,true),  L(false,true,true),
        };
        bool allPartial = true;
        for (const DeviceLinks& d : partials)
            if (LinkStateOf(d) != LinkState::Partial) allPartial = false;
        Check(allPartial, "all six part-connected combinations read Partial", "");

        Check(LinkLabel(L(true,true,false)) == "[HW] 2/3",
              "the label counts rather than saying 'partial'",
              LinkLabel(L(true,true,false)));
        Check(LinkLabel(L(false,false,false)) == "[HW] OFF",
              "and says OFF rather than 0/3", LinkLabel(L(false,false,false)));
    }

    printf("\n2. What a press does\n");
    {
        Check(NextAction(L(true,true,true)) == LinkAction::DisconnectAll,
              "everything up -> disconnect (end of session)", "");
        Check(NextAction(L(false,false,false)) == LinkAction::ConnectMissing,
              "nothing up -> connect", "");
        // The decision that matters: a mount switched on late must not
        // cost you the camera and focuser that are already working.
        Check(NextAction(L(true,true,false)) == LinkAction::ConnectMissing,
              "partial -> connect the missing one, not tear down the rest", "");
        Check(NextAction(L(false,false,true)) == LinkAction::ConnectMissing,
              "mount alone -> still connect rather than disconnect", "");
    }

    printf("\n3. When to ask first\n");
    {
        Check(NeedsConfirmation(L(true,true,true), true),
              "disconnecting mid-recording asks first", "");
        Check(!NeedsConfirmation(L(true,true,true), false),
              "disconnecting with nothing recording does not", "");
        // Connecting cannot lose a recording, so it never stops to ask
        // -- and a partial link while recording is a connect.
        Check(!NeedsConfirmation(L(true,true,false), true),
              "connecting never asks, even while recording", "");
        Check(!NeedsConfirmation(L(false,false,false), true),
              "nor from nothing connected", "");
    }

    printf("\n4. What it says afterwards\n");
    {
        // A clean start: everything came up.
        Check(LinkSummary(L(false,false,false), L(true,true,true)) ==
              "Connected camera, focuser, mount",
              "a full connect names all three",
              LinkSummary(L(false,false,false), L(true,true,true)));

        // The one that matters. Two of three answered, and the
        // sentence must not read as success.
        const std::string two = LinkSummary(L(false,false,false), L(true,true,false));
        Check(two == "Connected camera, focuser - still no mount",
              "a partial connect names what is STILL missing", two);
        Check(two.find("mount") != std::string::npos,
              "  and the missing device by name, not a count", two);

        // Nothing moved at all -- a press that achieved nothing must
        // say so rather than reporting the devices it did not touch.
        const std::string none = LinkSummary(L(true,false,false), L(true,false,false));
        Check(none == "Nothing connected - still no focuser, mount",
              "a press that changed nothing says nothing happened", none);

        Check(LinkSummary(L(true,true,true), L(true,true,true)) ==
              "All hardware already connected",
              "and with everything up already, says that", "");

        // A deliberate teardown should not then complain that the
        // devices are missing: that was the point of the press.
        const std::string down = LinkSummary(L(true,true,true), L(false,false,false));
        Check(down == "Disconnected camera, focuser, mount",
              "a full disconnect does not list what is now missing", down);

        // A device lost while another is gained -- the mount dropping
        // during a reconnect of the camera. Both halves reported, and
        // the mount is named once: "disconnected mount - still no
        // mount" would read as a fault rather than as what happened.
        const std::string mixed = LinkSummary(L(false,true,true), L(true,true,false));
        Check(mixed == "Connected camera; disconnected mount",
              "a gain and a loss in one press are both reported, once each",
              mixed);

        // A partial teardown names what it took down, and does not
        // then repeat those as missing.
        const std::string half = LinkSummary(L(true,true,true), L(true,false,false));
        Check(half == "Disconnected focuser, mount",
              "a partial teardown names what it took down, once", half);

        // But a device that was missing BEFORE the press is still
        // worth naming, because nothing picked it up.
        const std::string stale = LinkSummary(L(true,false,true), L(true,false,false));
        Check(stale == "Disconnected mount - still no focuser",
              "a device missing before and after is still named", stale);
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
