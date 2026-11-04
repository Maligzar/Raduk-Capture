// ============================================================
//  manual-sat-track -- phase 116, not part of the gate
//
//  Flies a real ISS pass, in real time, through the real thread in
//  SatDesk and the emulated mount. It puts the site under the ISS's
//  path a couple of minutes ahead, so there is always a pass to fly,
//  and it takes about five minutes. Elements come from the file, so
//  pass its path, or use the app's UPDATE ELEMENTS first.
//
//  Then flies a second one and presses Stop partway, which is the
//  claim that has to be seen on the thread: the call returns with the
//  axes stopped and nothing is sent afterwards.
//
//  satellite_passes.cpp has every fault. This has the plumbing.
// ============================================================

#include <cstdio>
#include "../src/core/MountEmulator.h"
#include "../src/core/SatDesk.h"

using namespace raduk;

struct CountingMount : core::MountEmulator {
    std::atomic<int> rates{0}, stops{0};
    bool SlewVariable(int a, int d, double r) override {
        ++rates;
        return core::MountEmulator::SlewVariable(a, d, r);
    }
    void StopSlew(int a) override {
        ++stops;
        core::MountEmulator::StopSlew(a);
    }
};

static void Drain(core::SatDesk& d) {
    for (const auto& s : d.TakeMessages()) printf("  > %s\n", s.c_str());
    fflush(stdout);
}

static bool Fly(const char* line1, const char* line2, double stopAfterSec) {
    const sky::Tle t = sky::ParseTle("ISS", line1, line2);
    sky::Sgp4 s;
    if (!s.Init(t)) { printf("bad elements\n"); return false; }

    // Under the path, four minutes from now, and a little to one side.
    const double target = sky::PreciseJdNow() + 240.0 / 86400.0;
    const sky::Sgp4State st = s.AtJd(target);
    const double D = 57.29577951308232;
    const double lon = sky::WrapDeg180(std::atan2(st.r[1], st.r[0]) * D
                                       - sky::GreenwichMeanSiderealDeg(target));
    const double lat = std::atan2(st.r[2], std::hypot(st.r[0], st.r[1])) * D;
    sky::LunarObserver site{lat, sky::WrapDeg180(lon + 3.0), 100.0, true};
    core::RigConfig::Get().siteLatDeg = site.latDeg;
    core::RigConfig::Get().siteLonDeg = site.lonDeg;

    sky::SatPassLimits lim;
    const auto passes = sky::FindPasses(s, site, sky::PreciseJdNow(), 0.2, lim);
    if (passes.empty()) { printf("no pass found\n"); return false; }
    core::SatPassRow row{"ISS", t, passes[0]};
    for (const auto& p : passes)
        if (p.trackable) { row.pass = p; break; }
    printf("pass: %s\n", sky::DescribePass("ISS", row.pass, 0.0, lim).c_str());

    CountingMount m;
    m.Connect("EMU", 9600);
    m.SetTrackingMode(core::IMount::ALT_AZ);

    std::atomic<int> begun{0}, ended{0};
    core::SatDesk d;
    d.SetCaptureHandlers([&]() { ++begun; }, [&]() { ++ended; });
    if (const char* why = d.StartTrack(&m, row, site, lim)) {
        printf("refused: %s\n", why);
        return false;
    }
    const auto t0 = std::chrono::steady_clock::now();
    auto secs = [&]() {
        return std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t0).count();
    };
    double last = 0.0;
    bool stoppedByUs = false;
    while (d.Tracking()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        Drain(d);
        if (secs() - last >= 15.0) {
            last = secs();
            printf("  %5.0f s  %s\n", last, d.Status().c_str());
        }
        if (stopAfterSec > 0.0 && secs() > stopAfterSec && begun.load() > 0) {
            const int before = m.stops.load();
            d.StopTrack("test");
            const int rates = m.rates.load();
            std::this_thread::sleep_for(std::chrono::seconds(1));
            printf("  stop returned; stops sent %d; rates since %d\n",
                   m.stops.load() - before, m.rates.load() - rates);
            if (m.stops.load() - before < 2 || m.rates.load() != rates) return false;
            stoppedByUs = true;
            break;
        }
    }
    d.StopTrack("done");
    Drain(d);
    printf("status: %s\n", d.Status().c_str());
    printf("rates sent %d, capture begun %d ended %d, tracking mode now %s\n",
           m.rates.load(), begun.load(), ended.load(),
           core::IMount::TrackingModeName(m.GetTrackingMode()));
    m.Disconnect();
    return begun.load() == 1 && ended.load() == 1 &&
           (stoppedByUs || d.Status().find("complete") != std::string::npos);
}

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: line1 line2\n"); return 2; }
    printf("-- a whole pass\n");
    const bool a = Fly(argv[1], argv[2], 0.0);
    printf("-- stopped partway\n");
    const bool b = Fly(argv[1], argv[2], 190.0);
    printf("%s\n", (a && b) ? "All passed" : "FAILED");
    return (a && b) ? 0 : 1;
}
