// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once
// ============================================================
// Readiness.h — Phase 68: is the rig actually ready to image?
//
// The device check at startup answers "are things plugged in". It
// runs before the camera opens and long before the mount connects,
// so alignment, tracking and focus are all unknowable at that point.
// This runs later, where they are not.
//
// Design rule: the app verifies what it can. Asking someone to
// self-certify a fact the machine already knows is theatre, and a
// checklist of those trains people to tick without reading. Only
// three things here need a human, and they are the three the
// software genuinely cannot observe.
//
// Second rule: silence when ready. A list that appears every launch
// becomes a speed bump within a week.
// ============================================================

#include <string>
#include <vector>
#include <cstdio>

namespace raduk::core {

enum class ReadyState {
    Pass,       // verified good
    Warn,       // works, but will bite
    Fail,       // imaging will not work until fixed
    Unknown,    // could not be determined
    Manual      // only a human can confirm
};

inline const char* ReadyStateName(ReadyState s) {
    switch (s) {
        case ReadyState::Pass:    return "OK";
        case ReadyState::Warn:    return "WARN";
        case ReadyState::Fail:    return "FAIL";
        case ReadyState::Unknown: return "????";
        case ReadyState::Manual:  return "CHECK";
    }
    return "????";
}

struct ReadyItem {
    std::string name;      // short label
    ReadyState  state = ReadyState::Unknown;
    std::string detail;    // what was measured, or what to do about it
    std::string helpAnchor;// Help section to send the user to
};

struct ReadyReport {
    std::vector<ReadyItem> items;

    bool AnyFailed() const {
        for (const auto& i : items)
            if (i.state == ReadyState::Fail) return true;
        return false;
    }
    bool AnyWarned() const {
        for (const auto& i : items)
            if (i.state == ReadyState::Warn) return true;
        return false;
    }
    // Whether the user should be interrupted. Manual items alone are
    // not grounds: they are reminders, not faults, and a dialog that
    // appears for them every single launch is the speed bump this
    // design exists to avoid.
    bool WorthShowing() const { return AnyFailed() || AnyWarned(); }

    int CountOf(ReadyState s) const {
        int n = 0;
        for (const auto& i : items) if (i.state == s) n++;
        return n;
    }

    // One line for the log, whether or not anything is shown.
    std::string Summary() const {
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "%d ok, %d warn, %d fail, %d unknown, %d to check by hand",
                 CountOf(ReadyState::Pass), CountOf(ReadyState::Warn),
                 CountOf(ReadyState::Fail), CountOf(ReadyState::Unknown),
                 CountOf(ReadyState::Manual));
        return std::string(buf);
    }

    void Add(const char* name, ReadyState st, const std::string& detail,
             const char* anchor = "") {
        items.push_back({name, st, detail, anchor});
    }
};

// Builds the report. Takes what it needs rather than reaching for
// globals, so it can be exercised without a rig attached.
//
// camOpen/framesSeen   - camera streaming at all
// droppedFrames        - USB trouble before a capture even starts
// focuserOpen/focusPos - autofocus silently cannot run without these
// mountConnected/Aligned/trackingKnown/tracking - the two that cost
//                        an entire session on 2026-10-08
// siteSet              - alt/az rates, sky map and ADC all need it
// diskFreeGB           - a 3000-frame full-frame run is about 25 GB
struct ReadyInputs {
    bool   camOpen        = false;
    long long framesSeen  = 0;
    int    droppedFrames  = 0;
    bool   focuserOpen    = false;
    int    focusPos       = 0;
    bool   mountConnected = false;
    bool   mountAligned   = false;
    bool   trackingKnown  = false;
    bool   trackingOn     = false;
    bool   siteSet        = false;
    double diskFreeGB     = -1.0;
    bool   profileLoaded  = false;
    // Phase 83: passed in rather than read from RigConfig so this
    // header stays dependency-free and testable, as the rest of
    // these inputs are.
    std::string filterName;          // empty = not supplied
    float  filterLowNm    = 0.0f;
    float  filterHighNm   = 0.0f;
};

inline ReadyReport EvaluateReadiness(const ReadyInputs& in) {
    ReadyReport r;
    char buf[200];

    // -- camera ------------------------------------------------
    if (!in.camOpen) {
        r.Add("Camera", ReadyState::Fail, "not open", "No signal / black preview");
    } else if (in.framesSeen <= 0) {
        r.Add("Camera", ReadyState::Fail, "open but no frames have arrived",
              "No signal / black preview");
    } else {
        snprintf(buf, sizeof(buf), "streaming, %lld frames", (long long)in.framesSeen);
        r.Add("Camera", ReadyState::Pass, buf);
    }

    if (in.droppedFrames > 0) {
        snprintf(buf, sizeof(buf),
                 "%d frames already dropped - reduce USB bandwidth",
                 in.droppedFrames);
        r.Add("USB", ReadyState::Warn, buf, "Frames dropping (LOST > 0)");
    } else if (in.camOpen) {
        r.Add("USB", ReadyState::Pass, "no dropped frames");
    }

    // -- focuser -----------------------------------------------
    if (!in.focuserOpen) {
        r.Add("Focuser", ReadyState::Warn,
              "not connected - autofocus unavailable",
              "Autofocus fails / doesn't converge");
    } else {
        snprintf(buf, sizeof(buf), "at %d", in.focusPos);
        r.Add("Focuser", ReadyState::Pass, buf);
    }

    // -- mount -------------------------------------------------
    if (!in.mountConnected) {
        r.Add("Mount", ReadyState::Fail, "not connected", "Mount not responding");
    } else {
        r.Add("Mount", ReadyState::Pass, "connected");

        // The one that matters most. Everything downstream rests on it,
        // and the app cannot do it -- only the hand control can.
        if (!in.mountAligned) {
            r.Add("Alignment", ReadyState::Fail,
                  "mount reports NOT aligned - hand control, "
                  "Align > Quick Align (needs no stars)",
                  "Mount will not track (TRK stays OFF)");
        } else {
            r.Add("Alignment", ReadyState::Pass, "mount reports aligned");
        }

        if (!in.trackingKnown) {
            r.Add("Tracking", ReadyState::Unknown,
                  "mode could not be read - a link problem, not a "
                  "tracking one",
                  "Mount will not track (TRK stays OFF)");
        } else if (!in.trackingOn) {
            r.Add("Tracking", ReadyState::Fail,
                  "off - acquisition and guiding cannot work; the planet "
                  "drifts out of the lock window in under a second",
                  "Mount will not track (TRK stays OFF)");
        } else {
            r.Add("Tracking", ReadyState::Pass, "driving");
        }
    }

    // -- site --------------------------------------------------
    if (!in.siteSet) {
        r.Add("Site", ReadyState::Fail,
              "no observing site - alt/az rates, the sky map and the ADC "
              "model all need one",
              "Observing site wrong or missing");
    } else {
        r.Add("Site", ReadyState::Pass, "configured");
    }

    // -- disk --------------------------------------------------
    if (in.diskFreeGB >= 0.0) {
        if (in.diskFreeGB < 5.0) {
            snprintf(buf, sizeof(buf), "%.0f GB free - not enough for a run",
                     in.diskFreeGB);
            r.Add("Disk", ReadyState::Fail, buf);
        } else if (in.diskFreeGB < 30.0) {
            snprintf(buf, sizeof(buf),
                     "%.0f GB free - a full-frame 3000-frame run is ~25 GB",
                     in.diskFreeGB);
            r.Add("Disk", ReadyState::Warn, buf);
        } else {
            snprintf(buf, sizeof(buf), "%.0f GB free", in.diskFreeGB);
            r.Add("Disk", ReadyState::Pass, buf);
        }
    }

    if (!in.profileLoaded)
        r.Add("Profile", ReadyState::Warn, "none loaded");

    // -- the three only a human can answer ---------------------
    // Deliberately few. Every row added here dilutes the ones above,
    // and a long list of unverifiable ticks is what teaches people to
    // stop reading.
    r.Add("Dust cap", ReadyState::Manual, "off?");
    r.Add("Tube cooled", ReadyState::Manual,
          "an 8-inch SCT wants 45-90 min; tube currents cap detail");
    r.Add("Cables", ReadyState::Manual,
          "dressed and free to rotate? press CABLES OK if so");

    // The filter is the fourth unobservable thing, but it changes only
    // when the camera comes off the scope -- far more rarely than a
    // dust cap. So it is stated, not asked: Manual never sets
    // WorthShowing(), so this row is silent on a good night and is
    // there to be read on a night the list already opened for some
    // other reason. The band matters because it drives the ADC figure.
    if (!in.filterName.empty()) {
        char fb[128];
        snprintf(fb, sizeof(fb),
                 "%s (%.0f-%.0fnm) - still on the camera? drives the ADC figure",
                 in.filterName.c_str(), in.filterLowNm, in.filterHighNm);
        // The anchor is a help search term, not a markdown anchor --
        // "Filters" lands on the section that explains the band and why
        // a wrong one moves the ADC figure.
        r.Add("Filter", ReadyState::Manual, fb, "Filters");
    }

    return r;
}

} // namespace raduk::core
