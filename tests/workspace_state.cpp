// tests/workspace_state.cpp -- phase 101, the saved open/closed state
//
// Phase 98 recorded whether each panel was open and then never applied
// it on restore, so locations persisted and nothing else did. The
// recording half was already right; what had no test was the contract
// between the file and the restore, which is StateFor.

#include <cstdio>
#include <cstdlib>
#include <string>
#include "WorkspaceLayout.h"

static int failures = 0;
static int checks   = 0;
static void ok(bool cond, const char* what) {
    ++checks;
    if (!cond) { ++failures; printf("  FAIL: %s\n", what); }
}

using WL = raduk::gui::WorkspaceLayout;

static std::string TempPath(const char* leaf) {
    char buf[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, buf);
    return std::string(buf) + leaf;
}

static void Write(const std::string& path, const char* body) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { printf("  FAIL: could not write %s\n", path.c_str()); ++failures; return; }
    fputs(body, f);
    fclose(f);
}

int main() {
    printf("=== phase 101: the saved panel state ===\n\n");

    WL wl;
    const int screens = wl.Detect();
    printf("Detected %d screen(s)\n", screens);

    // A layout file for this machine's screen count, with one panel
    // explicitly closed, one explicitly open, one left to the default,
    // and one role that is not mentioned at all.
    char body[1024];
    snprintf(body, sizeof(body),
        "# test layout\n"
        "[monitors.%d]\n"
        "histogram = primary closed @100,100,400,300\n"
        "skymap    = primary open @200,200,500,400\n"
        "goto      = primary @300,300,320,600\n",
        screens);
    const std::string path = TempPath("raduk_ws_state_test.ini");
    Write(path, body);

    ok(wl.Load(path), "the layout file loads");
    wl.SelectProfile();

    printf("StateFor\n");
    ok(wl.StateFor("histogram") == WL::SavedState::Closed,
       "a role written 'closed' reads back closed");
    ok(wl.StateFor("skymap") == WL::SavedState::Open,
       "a role written 'open' reads back open");

    // An entry with neither token defaults to open, which is what the
    // writer relies on: it only ever writes the closed case, so the
    // absence of a token has to mean open.
    ok(wl.StateFor("goto") == WL::SavedState::Open,
       "an entry with no visibility token defaults to open");

    // The one that matters most. A role with no line on disk must read
    // Unknown, NOT open: a generated fallback has visible defaulting
    // to true, and treating that as a saved "open" would throw the
    // share view up as a black full-screen rectangle on first run.
    ok(wl.StateFor("share") == WL::SavedState::Unknown,
       "a role with no line on disk is Unknown, not Open");
    ok(wl.StateFor("objectpanel") == WL::SavedState::Unknown,
       "and so is any other unmentioned role");
    ok(wl.StateFor("no-such-role-anywhere") == WL::SavedState::Unknown,
       "a role that does not exist at all is Unknown");

    // A layout that was never loaded has no opinion about anything.
    printf("An empty layout\n");
    {
        WL fresh;
        ok(fresh.StateFor("histogram") == WL::SavedState::Unknown,
           "nothing loaded means no saved state");
    }

    // Round trip: the three states survive a write and a re-read. This
    // is the property the restore depends on, and it spans the writer
    // and the parser, which are the two halves that could drift.
    printf("Round trip through Save and Load\n");
    {
        const std::string rt = TempPath("raduk_ws_state_roundtrip.ini");
        ok(wl.Save(rt), "the loaded layout saves again");

        WL back;
        back.Detect();
        ok(back.Load(rt), "and loads back");
        back.SelectProfile();
        ok(back.StateFor("histogram") == WL::SavedState::Closed,
           "closed survives the round trip");
        ok(back.StateFor("skymap") == WL::SavedState::Open,
           "open survives the round trip");
        ok(back.StateFor("goto") == WL::SavedState::Open,
           "a defaulted-open entry survives as open");
        ok(back.StateFor("share") == WL::SavedState::Unknown,
           "and an absent role is still absent");
        remove(rt.c_str());
    }

    remove(path.c_str());
    printf("\n%d checks, %d failures\n", checks, failures);
    if (failures == 0) printf("PASS\n");
    return failures ? 1 : 0;
}
