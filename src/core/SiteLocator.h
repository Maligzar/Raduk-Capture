#pragma once
// ============================================================
// SiteLocator.h — Phase 66: ask Windows where we are
//
// The observing site used to be a hand-maintained rig.ini entry, and
// a missing or stale one silently pushed a wrong site to the mount.
// Windows already knows the answer, so ask it.
//
// How accurate does this need to be? At 0.203"/px the camera sees
// 12.99' x 7.31', so a cold GoTo must beat half the short axis —
// 3.65', or 0.061 deg. At latitude 34 that is 5.6 km of longitude.
// Windows Location over WiFi is good to roughly 1 km (0.6'), about an
// order of magnitude better than needed. GPS hardware is unnecessary.
//
// This is also the honest answer to "get it from the router": a router
// does not know where it is. Windows resolves nearby WiFi BSSIDs
// against a positioning database — the router is the signal, and this
// API is the supported way to read it.
//
// Consent: desktop apps need Settings > Privacy > Location services,
// with "Let desktop apps access your location" enabled. A refusal
// returns Denied, which is a clean outcome we report rather than a
// hang. Everything here is best-effort — failure means "ask the user",
// never "assume somewhere".
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <thread>
#include <future>
#include <chrono>
#include <cstdio>

namespace raduk::core {

struct SiteFix {
    bool        valid     = false;
    double      latDeg    = 0.0;
    double      lonDeg    = 0.0;
    double      elevM     = 0.0;
    // A WiFi-derived fix carries no altitude and reports 0.0, which is
    // sea level — a real value that would silently replace a correct
    // elevation and skew the ADC pressure model by ~6%. Only trust
    // elevM when this is true.
    bool        hasElev   = false;
    double      accuracyM = 0.0;   // horizontal accuracy as reported
    std::string status;            // human-readable outcome, always set
};

// Compiled against the Windows SDK's C++/WinRT headers when they are
// present. They ship with the SDK, but guarding means a toolchain
// without them degrades to "unavailable" instead of failing the build
// — this is an optional convenience, not something worth breaking a
// build over.
#if __has_include(<winrt/Windows.Devices.Geolocation.h>)
#define RADUK_HAVE_WINRT_GEOLOCATION 1
#else
#define RADUK_HAVE_WINRT_GEOLOCATION 0
#endif

#if RADUK_HAVE_WINRT_GEOLOCATION

} // namespace raduk::core

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Geolocation.h>

namespace raduk::core {

// Blocks for at most timeoutMs. Runs the query on its own thread: the
// WinRT calls block, and the apartment they need is not the one the
// GUI thread lives in.
inline SiteFix QueryWindowsLocation(int timeoutMs = 8000) {
    SiteFix fix;

    auto work = std::async(std::launch::async, [timeoutMs]() -> SiteFix {
        SiteFix f;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);

            using namespace winrt::Windows::Devices::Geolocation;

            auto access = Geolocator::RequestAccessAsync().get();
            if (access != GeolocationAccessStatus::Allowed) {
                f.status = (access == GeolocationAccessStatus::Denied)
                    ? "denied - enable Settings > Privacy > Location, "
                      "and 'Let desktop apps access your location'"
                    : "unspecified - location service unavailable";
                return f;
            }

            Geolocator geo;
            // Ask for the cheap answer. Metre-level precision would
            // invite a GPS fix and a long wait, and the requirement is
            // kilometres.
            geo.DesiredAccuracy(PositionAccuracy::Default);
            geo.ReportInterval(1000);

            auto pos    = geo.GetGeopositionAsync().get();
            auto coord  = pos.Coordinate();
            auto point  = coord.Point().Position();

            f.latDeg = point.Latitude;
            f.lonDeg = point.Longitude;

            // AltitudeAccuracy is an IReference: null means the fix has
            // no vertical component at all, which is the normal case for
            // WiFi positioning. Altitude then reads 0.0 and must not be
            // mistaken for sea level.
            auto altAcc = coord.AltitudeAccuracy();
            if (altAcc != nullptr) {
                f.elevM   = point.Altitude;
                f.hasElev = true;
            }

            auto acc = coord.Accuracy();
            f.accuracyM = acc;

            // A fix worse than the pointing requirement is worse than
            // useless: it looks authoritative while putting the target
            // outside the frame. 5.6 km is the longitude tolerance at
            // this latitude; allow a little margin and reject beyond.
            if (f.accuracyM > 10000.0) {
                char buf[128];
                snprintf(buf, sizeof(buf),
                         "fix too coarse (+/-%.0f km) to point a %.1f' field",
                         f.accuracyM / 1000.0, 7.31);
                f.status = buf;
                return f;
            }

            char buf[96];
            snprintf(buf, sizeof(buf), "ok (+/-%.0f m)", f.accuracyM);
            f.status = buf;
            f.valid  = true;
        } catch (winrt::hresult_error const& e) {
            char buf[160];
            snprintf(buf, sizeof(buf), "WinRT error 0x%08X",
                     (unsigned)e.code().value);
            f.status = buf;
        } catch (...) {
            f.status = "unexpected failure";
        }
        return f;
    });

    // The service can sit waiting on a WiFi scan. Bound it: a startup
    // path must not hang because location is slow or wedged.
    if (work.wait_for(std::chrono::milliseconds(timeoutMs))
            == std::future_status::ready) {
        fix = work.get();
    } else {
        fix.status = "timed out";
        // The future is deliberately left to finish on its own rather
        // than blocking here; its thread holds nothing we need.
        static std::future<SiteFix> s_abandoned;
        s_abandoned = std::move(work);
    }

    printf("[SiteLocator] Windows Location: %s%s\n",
           fix.status.c_str(), fix.valid ? "" : " - no fix");
    return fix;
}

#else  // no C++/WinRT Geolocation in this SDK

inline SiteFix QueryWindowsLocation(int /*timeoutMs*/ = 8000) {
    SiteFix fix;
    fix.status = "unavailable - SDK lacks Windows.Devices.Geolocation";
    printf("[SiteLocator] %s\n", fix.status.c_str());
    return fix;
}

#endif

} // namespace raduk::core
