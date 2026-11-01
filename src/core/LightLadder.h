// ============================================================
//  LightLadder -- how auto-exposure shares light between exposure
//  and gain
//
//  Phase 112, the owner's call: "AE should be changing all settings",
//  and option A of docs/Phase112-Plan-Camera-Review.md.
//
//  Exposure and gain are not interchangeable. Once the light is
//  matched, a shorter exposure freezes more seeing and a lower gain is
//  less noise, and on this sensor crossing 182 is worth having by
//  itself. So there is one path through the two, walked forwards for
//  more light and backwards for less:
//
//    (expMin, 0) -> (expMin, floor)      gain up to the HCG floor
//                -> (expKnee, floor)     exposure, the cheap light
//                -> (expKnee, gainKnee)  gain, keeping the seeing frozen
//                -> (expMax, gainKnee)   exposure, now costing sharpness
//                -> (expMax, gainMax)    gain, the last resort
//
//  A correction is measured in stops, because that is the one unit the
//  two levers share. One that crosses a rung spills into the next
//  lever, so a large error moves both sliders in one step.
//
//  Gain is 0.1 dB a unit, so a stop is 60.2 units whatever the gain is.
//  The assistant used to multiply gain by its correction factor as if
//  it were linear, which at gain 200 turned a 1.2 into 1.58.
//
//  A point off the path, set by hand or by a profile, is walked from
//  where it is: each rule looks only at the lever it would move.
//
//  The knees are CameraRails::RailsFor's. UNVERIFIED ON SKY.
// ============================================================

#pragma once

#include <algorithm>
#include <cmath>

namespace raduk::core {

inline constexpr double GAIN_UNITS_PER_STOP = 60.206;  // 200 log10(2)

struct LightLadder {
    double expMinMs  = 1.0;
    double expKneeMs = 20.0;
    double expMaxMs  = 50.0;
    int    gainFloor = 182;
    int    gainKnee  = 350;
    int    gainMax   = 570;
};

struct LightPoint {
    double expMs = 0.0;
    int    gain  = 0;
};

inline LightPoint WalkLadder(const LightLadder& l, double expMs, int gainIn,
                             double stops) {
    double exp  = expMs;
    double gain = (double)gainIn;
    if (exp <= 0.0) return { expMs, gainIn };

    // Each moves its lever toward `to` by as much of what is left as
    // fits, and does nothing if the lever is already past it.
    auto expUp = [&](double to) {
        if (stops <= 0.0 || exp >= to) return;
        const double take = (std::min)(stops, std::log2(to / exp));
        exp *= std::pow(2.0, take); stops -= take;
        if (stops <= 1e-9) stops = 0.0;
    };
    auto expDown = [&](double to) {
        if (stops >= 0.0 || exp <= to) return;
        const double take = (std::max)(stops, std::log2(to / exp));
        exp *= std::pow(2.0, take); stops -= take;
        if (stops >= -1e-9) stops = 0.0;
    };
    auto gainUp = [&](double to) {
        if (stops <= 0.0 || gain >= to) return;
        const double take = (std::min)(stops, (to - gain) / GAIN_UNITS_PER_STOP);
        gain += take * GAIN_UNITS_PER_STOP; stops -= take;
        if (stops <= 1e-9) stops = 0.0;
    };
    auto gainDown = [&](double to) {
        if (stops >= 0.0 || gain <= to) return;
        const double take = (std::max)(stops, (to - gain) / GAIN_UNITS_PER_STOP);
        gain += take * GAIN_UNITS_PER_STOP; stops -= take;
        if (stops >= -1e-9) stops = 0.0;
    };

    const double knee = (std::min)(l.expKneeMs, l.expMaxMs);
    gainUp(l.gainFloor);
    expUp(knee);
    gainUp(l.gainKnee);
    expUp(l.expMaxMs);
    gainUp(l.gainMax);

    gainDown(l.gainKnee);
    expDown(knee);
    gainDown(l.gainFloor);
    expDown(l.expMinMs);
    gainDown(0.0);

    // To the microsecond, which is the camera's resolution.
    return { std::round(exp * 1000.0) / 1000.0, (int)std::lround(gain) };
}

} // namespace raduk::core
