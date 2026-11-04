#pragma once
// ============================================================
// SatDesk.h — Phase 116: elements on disk, passes, and the thread
// that flies one
//
// SatPass.h and SatTracker.h are pure and tested. This is the plumbing
// around them, and it has one job those cannot have: a 5 Hz loop on
// its own thread. The GoTo panel ticks every couple of seconds, and
// the render loop must never wait on a serial round trip.
//
// Elements come from CelesTrak's "visual" group, about 160 bright
// objects in 26 KB. They ask for no more than one fetch every two
// hours per file, so the file's own age decides. The request carries
// nothing about this site.
//
// THE THREAD'S LAST ACT IS ALWAYS TO STOP THE AXES. However the loop
// ends, the exit path stops both and puts tracking back. NexStar's
// own watchdog still stands behind that if the process dies.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "IMount.h"
#include "Paths.h"
#include "SatImageLock.h"
#include "SatPlan.h"
#include "SatTracker.h"
#include "../sky/SatPass.h"

#pragma comment(lib, "winhttp.lib")

namespace raduk::core {

class SatDesk {
public:
    static constexpr int kTickMs = 200;
    static constexpr double kMinFetchHours = 2.0;
    static constexpr double kPredictHours = 24.0;

    ~SatDesk() {
        StopTrack("closing");
        if (m_worker.joinable()) m_worker.join();
    }

    // Called with what a person should read. Any thread.
    void SetCaptureHandlers(std::function<void()> begin,
                            std::function<void()> end) {
        m_begin = std::move(begin);
        m_end   = std::move(end);
    }

    // Phase 117. The tap belongs to the capture thread's sink, so it
    // outlives this and is only pointed at.
    void SetFrameTap(SatFrameTap* tap) { m_tap = tap; }

    void SetImageCalibration(bool valid, const GuideResponse& r,
                             float altCalDeg, float rateArcsecPerSec) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_calValid = valid;
        m_calResp  = r;
        m_calAlt   = altCalDeg;
        m_calRate  = rateArcsecPerSec;
    }

    // Slide the prediction along its own track. Nearly all of what a
    // stale element set or a wrong PC clock gets wrong is timing, so
    // this one number brings the satellite onto the sensor, which is
    // what the image lock needs before it can do anything.
    void NudgeTime(double seconds) {
        if (!m_trackRunning.load()) return;
        const double t = m_timeOffset.load() + seconds;
        m_timeOffset.store(t);
        char b[64];
        snprintf(b, sizeof(b), "Satellite: time offset %+.1f s", t);
        Say(b);
    }

    static std::string ElementsPath() {
        return Paths::Root() + "\\satellites.tle";
    }

    static double FileAgeHours(const std::string& path) {
        WIN32_FILE_ATTRIBUTE_DATA a{};
        if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &a))
            return 1.0e9;
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        ULARGE_INTEGER n, f;
        n.LowPart = now.dwLowDateTime;   n.HighPart = now.dwHighDateTime;
        f.LowPart = a.ftLastWriteTime.dwLowDateTime;
        f.HighPart = a.ftLastWriteTime.dwHighDateTime;
        return (double)(n.QuadPart - f.QuadPart) / 36000000000.0;
    }

    // ── Elements and passes, off the UI thread ─────────────────
    bool Busy() const { return m_busy.load(); }

    // Fetch if allowed, then predict. Without a network it predicts
    // from whatever is on disk, because a stale list is still a list.
    void Refresh(const raduk::sky::LunarObserver& site,
                 const raduk::sky::SatPassLimits& lim, bool fetch) {
        if (m_busy.exchange(true)) return;
        if (m_worker.joinable()) m_worker.join();
        m_worker = std::thread([this, site, lim, fetch]() {
            if (fetch) Fetch();
            Predict(site, lim);
            m_fresh.store(true);
            m_busy.store(false);
        });
    }

    // True once, each time new rows have arrived.
    bool TakeFresh() { return m_fresh.exchange(false); }

    std::vector<SatPassRow> Rows() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_rows;
    }

    // ── Tracking ───────────────────────────────────────────────
    bool Tracking() const { return m_trackRunning.load(); }

    // How the last pass ended, once.
    SatOutcome TakeOutcome() { return m_outcome.exchange(SatOutcome::None); }

    // nullptr on success, or why not.
    const char* StartTrack(IMount* mount, const SatPassRow& row,
                           const raduk::sky::LunarObserver& site,
                           const raduk::sky::SatPassLimits& lim) {
        if (m_trackRunning.load()) return "a pass is already being tracked";
        if (!mount || !mount->IsConnected()) return "no mount";
        if (!mount->SupportsVariableRate())
            return "this mount has no variable-rate command";
        if (mount->GetTrackingMode() != IMount::ALT_AZ)
            return "this needs the mount in alt-az";
        if (!site.set) return "no site set";
        if (!row.pass.trackable) return "no part of that pass is in the band";
        const double left = (row.pass.trackEndJd - raduk::sky::PreciseJdNow())
                          * 86400.0;
        if (left < 10.0) return "that pass is over";
        if (m_trackThread.joinable()) m_trackThread.join();

        m_stop.store(false);
        m_timeOffset.store(0.0);
        m_outcome.store(SatOutcome::None);
        m_trackRunning.store(true);
        m_trackThread = std::thread([this, mount, row, site, lim]() {
            TrackLoop(mount, row, site, lim);
            m_trackRunning.store(false);
        });
        return nullptr;
    }

    // Blocks until the axes have been stopped: Escape must not return
    // with this thread still able to send a rate.
    void StopTrack(const char* why) {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_stopWhy = why ? why : "stopped";
        }
        m_stop.store(true);
        if (m_trackThread.joinable()) m_trackThread.join();
    }

    // Anything that happened since the last ask, for the log.
    std::vector<std::string> TakeMessages() {
        std::lock_guard<std::mutex> lk(m_mutex);
        std::vector<std::string> out(m_msgs.begin(), m_msgs.end());
        m_msgs.clear();
        return out;
    }

    std::string Status() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_status;
    }

private:
    void Say(const std::string& s) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_msgs.push_back(s);
        if (m_msgs.size() > 50) m_msgs.pop_front();
    }
    void SetStatus(const std::string& s) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_status = s;
    }

    void Fetch() {
        const std::string path = ElementsPath();
        const double age = FileAgeHours(path);
        if (age < kMinFetchHours) {
            char b[120];
            snprintf(b, sizeof(b), "Elements are %.1f hours old; CelesTrak "
                     "asks for no more than one fetch in %.0f.", age,
                     kMinFetchHours);
            Say(b);
            return;
        }
        std::string body;
        if (!HttpGet(L"celestrak.org",
                     L"/NORAD/elements/gp.php?GROUP=visual&FORMAT=tle", body)) {
            Say("Could not reach CelesTrak; using the elements on disk.");
            return;
        }
        int dropped = 0;
        const auto parsed = raduk::sky::ParseTleText(body, &dropped);
        // Only ever replace a good file with a good file. An error page
        // or a truncated body must not cost the set already here.
        if (parsed.size() < 20) {
            Say("CelesTrak's reply held no usable elements; keeping the file.");
            return;
        }
        const std::string tmp = path + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary);
            f << body;
            if (!f) { Say("Could not write the elements file."); return; }
        }
        MoveFileExA(tmp.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        char b[120];
        snprintf(b, sizeof(b), "Elements updated: %d satellites%s.",
                 (int)parsed.size(), dropped ? ", some damaged and dropped" : "");
        Say(b);
    }

    static bool HttpGet(const wchar_t* host, const wchar_t* path, std::string& out) {
        HINTERNET s = WinHttpOpen(L"RadukCapture", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!s) return false;
        WinHttpSetTimeouts(s, 5000, 5000, 10000, 15000);
        HINTERNET c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, nullptr,
                              WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                              WINHTTP_FLAG_SECURE) : nullptr;
        bool ok = r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
                    && WinHttpReceiveResponse(r, nullptr);
        if (ok) {
            DWORD status = 0, sz = sizeof(status);
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE |
                                WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status,
                                &sz, nullptr);
            ok = status == 200;
        }
        const size_t kMax = 4u * 1024u * 1024u;
        while (ok) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(r, &avail)) { ok = false; break; }
            if (avail == 0) break;
            std::string chunk(avail, '\0');
            DWORD got = 0;
            if (!WinHttpReadData(r, &chunk[0], avail, &got)) { ok = false; break; }
            out.append(chunk, 0, got);
            if (out.size() > kMax) { ok = false; break; }
        }
        if (r) WinHttpCloseHandle(r);
        if (c) WinHttpCloseHandle(c);
        WinHttpCloseHandle(s);
        return ok;
    }

    void Predict(const raduk::sky::LunarObserver& site,
                 const raduk::sky::SatPassLimits& lim) {
        std::ifstream f(ElementsPath(), std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        const auto tles = raduk::sky::ParseTleText(ss.str());

        std::vector<SatPassRow> rows;
        const double now = raduk::sky::PreciseJdNow();
        for (const auto& t : tles) {
            raduk::sky::Sgp4 s;
            if (!s.Init(t)) continue;
            for (const auto& p : raduk::sky::FindPasses(s, site, now,
                                                        kPredictHours, lim)) {
                if (!p.trackable) continue;
                rows.push_back({t.name, t, p});
            }
        }
        std::sort(rows.begin(), rows.end(),
                  [](const SatPassRow& a, const SatPassRow& b) {
                      return a.pass.riseJd < b.pass.riseJd;
                  });
        std::lock_guard<std::mutex> lk(m_mutex);
        m_rows = std::move(rows);
    }

    void TrackLoop(IMount* mount, const SatPassRow& row,
                   const raduk::sky::LunarObserver& site,
                   const raduk::sky::SatPassLimits& lim) {
        using namespace raduk::sky;
        Sgp4 sat;
        if (!sat.Init(row.tle)) { Say("Those elements will not propagate."); return; }

        const IMount::TrackingMode restore = mount->GetTrackingMode();
        const double base = row.pass.trackStartJd;
        auto secNow = [&]() { return (PreciseJdNow() - base) * 86400.0; };

        SatTracker::Config c;
        c.startSec = 0.0;
        c.endSec   = (row.pass.trackEndJd - base) * 86400.0;
        c.minAltDeg = lim.minAltDeg;
        c.maxAltDeg = lim.maxAltDeg;
        c.maxRateDeg = lim.maxRateDeg;
        SatTracker tr;
        tr.Configure(c);
        tr.Start(secNow());

        SatImageLock lock;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (m_calValid) lock.Configure(m_calResp, m_calAlt, m_calRate);
        }
        if (!lock.Calibrated())
            Say("Satellite: no guide calibration, so this flies on prediction "
                "alone. Calibrate on a planet or the Moon first.");
        if (m_tap) m_tap->Arm(lock.Calibrated());
        bool wasLocked = false;

        // Phase 118: the number a night plan is built on.
        double fromAlt = 0.0, fromAz = 0.0, slewBegan = 0.0;
        const bool haveFrom = mount->GetAltAz(fromAlt, fromAz);

        bool capturing = false;
        auto send = [&](int axis, double rate) {
            mount->SlewVariable(axis, rate >= 0.0 ? 0 : 1,
                                std::fabs(rate) * 3600.0);
        };
        const auto period = std::chrono::milliseconds(kTickMs);
        auto next = std::chrono::steady_clock::now();
        double lastSaid = -1e9;

        while (tr.Running()) {
            if (m_stop.load()) {
                std::string why;
                { std::lock_guard<std::mutex> lk(m_mutex); why = m_stopWhy; }
                tr.Abort(why.c_str());
            }

            // Aim at where it will be when the command takes effect.
            const double jd = PreciseJdNow()
                            + (0.1 + m_timeOffset.load()) / 86400.0;
            const SatRates q = SatRatesAt(sat, jd, site);
            SatTracker::Sky sky;
            sky.ok = q.ok; sky.altDeg = q.altDeg; sky.azDeg = q.azDeg;
            sky.altRate = q.altRate; sky.azRate = q.azRate;

            const auto phase = tr.State();
            double alt = 0.0, az = 0.0;
            bool ok = true, slewing = false;
            if (phase == SatTracker::Phase::Slewing)
                slewing = mount->IsSlewing();
            else if (phase == SatTracker::Phase::Acquiring ||
                     phase == SatTracker::Phase::Tracking)
                ok = mount->GetAltAz(alt, az);

            const double now = secNow();

            // Phase 117: the image moves the aim, and only the aim. The
            // tracker's stops all judge what is left.
            if (phase == SatTracker::Phase::Tracking) {
                float fx = 0.0f, fy = 0.0f;
                int fw = 0, fh = 0;
                const bool fix = m_tap && m_tap->Take(fx, fy, fw, fh);
                lock.Update(now, fix, fx, fy, fw, fh, (float)alt);
                const bool locked = lock.Locked(now);
                if (locked != wasLocked) {
                    wasLocked = locked;
                    Say(locked ? "Satellite: image lock." : "Satellite: image lost.");
                }
                sky.azDeg  += lock.BiasAzDeg();
                sky.altDeg += lock.BiasAltDeg();
            }

            const SatTracker::Output o = tr.Tick(now, sky, ok, alt, az, slewing);

            if (o.goTo) {
                const SatLook l = SatLookAt(sat, base, site);
                mount->GoToRADec(l.raHours, l.decDeg);
                slewBegan = now;
                Say("Satellite: slewing to where the window opens.");
                SetStatus("Slewing to the start of the pass");
            }
            if (phase == SatTracker::Phase::Slewing &&
                tr.State() == SatTracker::Phase::Holding && haveFrom) {
                const double deg = (std::max)(
                    std::fabs(WrapDeg180(row.pass.startAz - fromAz)),
                    std::fabs(row.pass.startAlt - fromAlt));
                const double sec = now - slewBegan;
                char b[120];
                snprintf(b, sizeof(b), "Satellite: slew of %.0f deg took %.0f s "
                         "(SatSlewDegPerSec %.1f here).", deg, sec,
                         sec > 0.0 ? deg / sec : 0.0);
                Say(b);
            }
            if (o.trackingOff) {
                mount->SetTrackingMode(IMount::OFF);
                mount->DriveAltAzAxes(true);
                Say("Satellite: moving.");
            }
            if (o.rates) {
                send(0, o.azRate);
                send(1, o.altRate);
            }
            if (o.beginCapture) {
                capturing = true;
                if (m_begin) m_begin();
                Say("Satellite: on target, recording.");
            }
            if (o.endCapture) {
                capturing = false;
                if (m_end) m_end();
            }

            if (now - lastSaid >= 1.0) {
                lastSaid = now;
                char b[120];
                if (phase == SatTracker::Phase::Holding)
                    snprintf(b, sizeof(b), "Waiting: moves in %.0f s", -now);
                else if (tr.Running() && phase != SatTracker::Phase::Slewing)
                    snprintf(b, sizeof(b), "%s  error %.2f deg%s%s",
                             phase == SatTracker::Phase::Tracking
                                 ? "Tracking" : "Closing in", tr.ErrorDeg(),
                             wasLocked ? "  image lock" : "",
                             tr.Saturated() ? "  at the mount's limit" : "");
                else b[0] = 0;
                if (b[0]) SetStatus(b);
            }

            next += period;
            std::this_thread::sleep_until(next);
        }

        // However it ended. Twice, because a stop that does not arrive
        // leaves a telescope flying with nobody commanding it.
        for (int i = 0; i < 2; ++i) {
            mount->StopSlew(0);
            mount->StopSlew(1);
        }
        if (capturing && m_end) m_end();
        if (m_tap) m_tap->Arm(false);
        mount->DriveAltAzAxes(false);
        mount->SetTrackingMode(restore);

        m_outcome.store(tr.State() == SatTracker::Phase::Complete ? SatOutcome::Complete
                      : tr.State() == SatTracker::Phase::Aborted  ? SatOutcome::Stopped
                      : tr.TelescopeFault() ? SatOutcome::Fault : SatOutcome::Lost);

        char b[220];
        const char* how = tr.State() == SatTracker::Phase::Complete ? "complete"
                        : tr.State() == SatTracker::Phase::Aborted  ? "stopped"
                        : "FAILED";
        snprintf(b, sizeof(b), "Satellite pass %s%s%s. Worst error on target "
                 "%.2f deg.", how, tr.Reason().empty() ? "" : ": ",
                 tr.Reason().c_str(), tr.PeakErrorDeg());
        Say(b);
        SetStatus(b);
    }

    mutable std::mutex m_mutex;
    std::vector<SatPassRow> m_rows;
    std::deque<std::string> m_msgs;
    std::string m_status, m_stopWhy;
    std::thread m_worker, m_trackThread;
    std::atomic<bool> m_busy{false}, m_fresh{false};
    std::atomic<bool> m_stop{false}, m_trackRunning{false};
    std::atomic<double> m_timeOffset{0.0};
    std::atomic<SatOutcome> m_outcome{SatOutcome::None};
    SatFrameTap*  m_tap = nullptr;
    bool          m_calValid = false;
    GuideResponse m_calResp;
    float         m_calAlt = -1.0f, m_calRate = 7.5f;
    std::function<void()> m_begin, m_end;
};

} // namespace raduk::core
