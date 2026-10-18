#pragma once
// ============================================================
// HardwareLink.h — Phase 46
//
// What the [HW] button shows, what a press should do, and what to say
// afterwards. Three devices, one button.
//
// The device calls themselves cannot be tested on the bench -- they
// are the camera, the focuser and the mount -- but the decisions
// around them can, and they are the part that is easy to get subtly
// wrong: which state the button reports when only two of three
// answered, whether a press with a partial link connects the missing
// one or tears down the working ones, and whether the sentence
// afterwards says what actually happened or what was attempted.
//
// That last one matters more than it sounds. This button is pressed in
// the dark, at the start of a session, by someone who then walks away
// from the screen. "Hardware connected" when the mount did not answer
// is how a night gets lost to a telescope that was never going to
// track.
// ============================================================

#include <string>

namespace raduk::core {

// Which devices are live. Filled from cam.IsOpen(), focuser.IsOpen()
// and mount->IsConnected() -- never from what was asked for.
struct DeviceLinks {
    bool camera  = false;
    bool focuser = false;
    bool mount   = false;

    int  Count() const { return (camera ? 1 : 0) + (focuser ? 1 : 0) + (mount ? 1 : 0); }
    bool operator==(const DeviceLinks& o) const {
        return camera == o.camera && focuser == o.focuser && mount == o.mount;
    }
    bool operator!=(const DeviceLinks& o) const { return !(*this == o); }
};

enum class LinkState {
    None,       // nothing connected      -- red
    Partial,    // some connected         -- amber
    All,        // all three connected    -- green
};

inline LinkState LinkStateOf(const DeviceLinks& d) {
    const int n = d.Count();
    if (n == 0) return LinkState::None;
    if (n == 3) return LinkState::All;
    return LinkState::Partial;
}

// The button's label. Shows the count rather than a word, because
// "PARTIAL" does not say which of the three is missing and the count
// at least says how many to go looking for.
inline std::string LinkLabel(const DeviceLinks& d) {
    switch (LinkStateOf(d)) {
        case LinkState::All:  return "[HW] 3/3";
        case LinkState::None: return "[HW] OFF";
        default: break;
    }
    return "[HW] " + std::to_string(d.Count()) + "/3";
}

// What a press should do.
//
// A partial link CONNECTS rather than disconnects. The common way to
// arrive at partial is a device that was not ready at launch -- the
// mount switched on late, the focuser cable in the wrong socket -- and
// the wanted answer there is "pick up the missing one", not "tear down
// the two that work". Disconnecting everything is only offered when
// everything is up, which is the end-of-session case this button also
// exists for.
enum class LinkAction { ConnectMissing, DisconnectAll };

inline LinkAction NextAction(const DeviceLinks& d) {
    return (LinkStateOf(d) == LinkState::All) ? LinkAction::DisconnectAll
                                              : LinkAction::ConnectMissing;
}

// Disconnecting while a capture is running loses the rest of that
// recording, so it is worth a confirmation -- the same bar the app
// applies to exiting mid-capture. Connecting never needs one.
inline bool NeedsConfirmation(const DeviceLinks& d, bool recording) {
    return recording && NextAction(d) == LinkAction::DisconnectAll;
}

// What to say once it is done. Reports the devices by name and by what
// they ARE now, not by what was attempted, and names what is still
// missing -- a sentence that only lists successes reads as success.
//
// before/after are the real states either side of the attempt.
inline std::string LinkSummary(const DeviceLinks& before, const DeviceLinks& after) {
    auto join = [](const std::string& a, const std::string& b) {
        if (a.empty()) return b;
        return a + ", " + b;
    };

    std::string gained, lost, missing;
    if (after.camera  && !before.camera)  gained = join(gained, "camera");
    if (after.focuser && !before.focuser) gained = join(gained, "focuser");
    if (after.mount   && !before.mount)   gained = join(gained, "mount");

    if (before.camera  && !after.camera)  lost = join(lost, "camera");
    if (before.focuser && !after.focuser) lost = join(lost, "focuser");
    if (before.mount   && !after.mount)   lost = join(lost, "mount");

    // Only what was ALREADY missing and still is. A device this press
    // just disconnected is named by `lost`, and saying "disconnected
    // mount - still no mount" twice in one sentence reads like a fault
    // rather than the thing that was asked for.
    if (!after.camera  && !before.camera)  missing = join(missing, "camera");
    if (!after.focuser && !before.focuser) missing = join(missing, "focuser");
    if (!after.mount   && !before.mount)   missing = join(missing, "mount");

    // Nothing moved. Worth saying plainly: a button that reports
    // success when it achieved nothing is the failure mode here.
    if (gained.empty() && lost.empty()) {
        if (missing.empty()) return "All hardware already connected";
        return "Nothing connected - still no " + missing;
    }

    std::string out;
    if (!gained.empty()) out = "Connected " + gained;
    if (!lost.empty())   out = out.empty() ? ("Disconnected " + lost)
                                           : (out + "; disconnected " + lost);

    if (!missing.empty()) out += " - still no " + missing;

    return out;
}

} // namespace raduk::core
