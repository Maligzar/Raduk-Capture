// ============================================================
//  Phase 151.0: may the app reach the internet now? (src/core/NetPolicy.h)
//
//    1. Online by default; rig.ini Offline=1 blocks, and says why.
//    2. A network that looked down blocks for exactly the back-off, then
//       allows one try; a success clears it at once.
//    3. The log names each skipped host once per offline spell.
//    4. LAN and this-PC hosts are never treated as internet.
//    5. Many threads noting and asking at once (run under TSan too).
// ============================================================

#include "core/NetPolicy.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using raduk::core::NetPolicy;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

int main() {
    printf("Network policy (phase 151.0)\n");
    const int64_t B = NetPolicy::kBackoffMs;

    {
        NetPolicy p;
        Check(p.AllowInternet(0) && p.Why(0).empty(), "online by default, with nothing to say");
        p.SetConfigured(true);
        Check(!p.AllowInternet(0) && !p.AllowInternet(100 * B), "Offline=1 blocks, at any time");
        Check(p.Why(0).find("Offline=1") != std::string::npos, "and says it is the setting", p.Why(0));
        p.NoteSuccess();
        Check(!p.AllowInternet(0), "a success does not override the setting");
        p.SetConfigured(false);
        Check(p.AllowInternet(0), "Offline=0 allows again");
    }

    {
        NetPolicy p;
        const int64_t t = 5'000'000;
        p.NoteNetworkDown(t, "ssd.jpl.nasa.gov");
        Check(!p.AllowInternet(t) && !p.AllowInternet(t + B - 1), "down: blocked through the back-off");
        Check(p.AllowInternet(t + B), "one try allowed when it ends");
        const std::string why = p.Why(t + 60'000);
        Check(why.find("ssd.jpl.nasa.gov") != std::string::npos && why.find("next try in 9 min") != std::string::npos,
              "it names the host and the minutes left", why);
        Check(p.Why(t + B).empty(), "nothing to say once it has ended");
        p.NoteNetworkDown(t + B, "celestrak.org");
        Check(!p.AllowInternet(t + B + 1), "the next failure starts a fresh back-off");
        Check(p.Why(t + B + 1).find("celestrak.org") != std::string::npos,
              "and names the host that failed this time", p.Why(t + B + 1));
        p.NoteSuccess();
        Check(p.AllowInternet(t + B + 1) && !p.DetectedDown(t + B + 1), "a success clears it at once");
    }

    {
        NetPolicy p;
        p.NoteNetworkDown(0, "a");
        Check(p.FirstSkipOf("ssd.jpl.nasa.gov") && !p.FirstSkipOf("ssd.jpl.nasa.gov") &&
              p.FirstSkipOf("celestrak.org"), "each skipped host is logged once per spell");
        p.NoteNetworkDown(B, "a");
        Check(p.FirstSkipOf("ssd.jpl.nasa.gov"), "and again in the next spell");
    }

    {
        const char* local[] = { "localhost", "elminster", "STARMACHINE", "nas.local", "pi.lan",
                                "box.home.arpa", "10.0.0.5", "172.16.1.1", "172.31.255.1",
                                "192.168.1.20", "127.0.0.1", "169.254.3.4", "::1", "elminster." };
        bool all = true; std::string bad;
        for (const char* h : local) if (!NetPolicy::IsLocalHost(h)) { all = false; bad += h; bad += ' '; }
        Check(all, "LAN and this PC are never gated", bad);
        const char* inet[] = { "ssd.jpl.nasa.gov", "celestrak.org", "opale.imcce.fr", "pool.ntp.org",
                               "8.8.8.8", "172.32.0.1", "172.15.0.1", "192.169.1.1", "", "11.0.0.1" };
        all = true; bad.clear();
        for (const char* h : inet) if (NetPolicy::IsLocalHost(h)) { all = false; bad += h; bad += ' '; }
        Check(all, "internet hosts are", bad);
    }

    {
        NetPolicy p;
        std::atomic<int> allowed{0};
        std::vector<std::thread> ts;
        for (int i = 0; i < 8; ++i)
            ts.emplace_back([&, i] {
                for (int k = 0; k < 2000; ++k) {
                    const int64_t now = (int64_t)k * 1000;
                    if (p.AllowInternet(now)) allowed.fetch_add(1);
                    if ((k + i) % 97 == 0) p.NoteNetworkDown(now, "h" + std::to_string(i));
                    if ((k + i) % 389 == 0) p.NoteSuccess();
                    (void)p.Why(now);
                    (void)p.FirstSkipOf("h");
                }
            });
        for (auto& t : ts) t.join();
        Check(allowed.load() > 0, "8 threads noting and asking at once: no crash, no race");
    }

    if (g_fail) { printf("net_policy: %d FAILED\n", g_fail); return 1; }
    printf("net_policy: all passed\n");
    return 0;
}
