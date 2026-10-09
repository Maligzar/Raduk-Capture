#pragma once
// ============================================================
// AlignGuide.h — Phase 67 step 4: "point the tube north", usefully
//
// Quick Align needs the tube levelled and pointed north. North is an
// abstraction at midnight in a front yard; the monitor two metres
// away is not. Given one number — the bearing from the mount to
// wherever the observer stands — this turns the instruction into
// something answerable without a compass.
//
// One number is enough because everything else is relative to it, and
// it is a question someone can answer in the dark: "standing at the
// computer, the scope is roughly which way from me?"
// ============================================================

#include <string>
#include <cmath>

namespace raduk::core {

// Normalises to (-180, 180]. Positive is clockwise.
inline double NormDelta(double deg) {
    while (deg >  180.0) deg -= 360.0;
    while (deg <= -180.0) deg += 360.0;
    return deg;
}

// Compass name for a bearing, to the nearest eighth.
inline const char* CompassName(double bearingDeg) {
    static const char* names[] = {"north", "north-east", "east", "south-east",
                                  "south", "south-west", "west", "north-west"};
    double b = std::fmod(bearingDeg, 360.0);
    if (b < 0) b += 360.0;
    int idx = (int)((b + 22.5) / 45.0) % 8;
    return names[idx];
}

// Where a world bearing appears to someone facing the mount.
//
// observerBearingDeg is measured FROM the mount TO the observer, so
// the observer's facing direction is the reverse of it. Relative
// angle is then positive clockwise, which is the observer's right.
inline double RelativeToObserverFacing(double targetBearingDeg,
                                       double observerBearingDeg) {
    double facing = observerBearingDeg + 180.0;   // observer looks at the mount
    return NormDelta(targetBearingDeg - facing);
}

// "behind you", "to your right", and so on.
inline const char* RelativeDirectionPhrase(double relDeg) {
    double a = relDeg < 0 ? -relDeg : relDeg;
    if (a <= 25.0)  return "straight ahead, beyond the mount";
    if (a >= 155.0) return "behind you";
    if (a < 65.0)   return relDeg > 0 ? "ahead and to your right"
                                      : "ahead and to your left";
    if (a <= 115.0) return relDeg > 0 ? "directly to your right"
                                      : "directly to your left";
    return relDeg > 0 ? "behind you and to the right"
                      : "behind you and to the left";
}

struct AlignGuidance {
    std::string northPhrase;   // where north is, from where you stand
    std::string action;        // what to do with the tube
    std::string note;          // the compass fact, for cross-checking
};

// Builds the instruction. The tube must point at the NORTH HORIZON,
// so when the observer is north of the mount the tube ends up aimed
// back towards them — which reads as wrong until it is said plainly,
// and is the whole reason for phrasing it this way.
inline AlignGuidance BuildQuickAlignGuidance(double observerBearingDeg) {
    AlignGuidance g;

    const double north = 0.0;
    double rel = RelativeToObserverFacing(north, observerBearingDeg);

    g.northPhrase = RelativeDirectionPhrase(rel);

    // How far the observer is from being due north of the mount.
    double offNorth = std::fabs(NormDelta(observerBearingDeg - north));

    if (offNorth <= 45.0) {
        g.action = "Point the tube back towards yourself - north is on your "
                   "side of the mount, so the tube should face your desk.";
    } else if (offNorth >= 135.0) {
        g.action = "Point the tube away from you, over the far side of the "
                   "mount.";
    } else {
        std::string side = (NormDelta(north - observerBearingDeg) > 0)
                               ? "left" : "right";
        g.action = "Swing the tube to your " + side +
                   " until it faces the north horizon.";
    }

    g.note = std::string("You are ") + CompassName(observerBearingDeg) +
             " of the mount, so you face " +
             CompassName(observerBearingDeg + 180.0) + " when looking at it.";
    return g;
}

} // namespace raduk::core
