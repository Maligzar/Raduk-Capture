// ============================================================
//  Phase 141.1: the PC clock against UTC, by SNTP
//
//    1. NTP timestamps round-trip to well under a microsecond.
//    2. A request is a client request carrying its send time.
//    3. Offset and round trip come out of a reply as RFC 4330 has them,
//       for a clock behind UTC, ahead of it, and with an asymmetric
//       server delay.
//    4. A reply that is not an answer to this request is refused:
//       wrong mode, kiss-o'-death, unsynchronised, a stale or forged
//       originate time, no time in it, too short.
//    5. Of several samples, the shortest round trip wins.
//    6. "Confirmed" is an uncertainty under 0.05 s, whatever the offset.
// ============================================================

#include "net/Sntp.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace raduk::net::sntp;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}
static std::string F(double v, int d = 6) { char b[64]; snprintf(b, sizeof(b), "%.*f", d, v); return b; }

// A server's reply to a request sent at t1 (PC clock), when the PC clock
// is `pcError` seconds off UTC (PC = UTC + pcError), with `out` seconds
// on the way there, `hold` at the server and `back` on the way back.
// Returns t4, the PC clock at arrival.
static double Reply(uint8_t* r, double t1, double pcError, double out, double hold, double back,
                    int stratum = 2, int mode = 4, int li = 0) {
    memset(r, 0, 48);
    r[0] = (uint8_t)((li << 6) | (4 << 3) | mode);
    r[1] = (uint8_t)stratum;
    const double t1Utc = t1 - pcError;
    const double t2 = t1Utc + out;          // server clock is UTC
    const double t3 = t2 + hold;
    PutTimestamp(&r[24], t1);               // the request's transmit time, echoed
    PutTimestamp(&r[32], t2);
    PutTimestamp(&r[40], t3);
    return (t3 + back) + pcError;           // back on the PC clock
}

int main() {
    printf("SNTP clock check (phase 141.1)\n");
    const double t1 = 1794432000.25;   // 2026-11-11, a quarter second past

    printf("\n1. Timestamps\n");
    {
        uint8_t b[8];
        PutTimestamp(b, t1);
        const double back = GetTimestamp(b);
        Check(std::fabs(back - t1) < 1e-6, "an NTP timestamp round-trips", F(back - t1, 9));
        uint8_t z[8] = {0};
        Check(GetTimestamp(z) == 0.0, "an all-zero timestamp reads as none");
    }

    printf("\n2. The request\n");
    {
        const auto q = Request(t1);
        Check(q.size() == 48 && q[0] == 0x23, "48 bytes, version 4, client mode");
        Check(std::fabs(GetTimestamp(&q[40]) - t1) < 1e-6, "it carries its send time");
    }

    printf("\n3. Offset and round trip\n");
    {
        uint8_t r[48];
        // PC clock 0.250 s behind UTC (PC = UTC - 0.25), symmetric 20 ms each way.
        double t4 = Reply(r, t1, -0.250, 0.020, 0.001, 0.020);
        Sample s = Read(r, 48, t1, t4);
        Check(s.ok, "a good reply is read", s.why);
        Check(std::fabs(s.offsetSec - 0.250) < 1e-6, "PC 0.25 s behind: offset +0.250", F(s.offsetSec));
        Check(std::fabs(s.delaySec - 0.040) < 1e-6, "round trip 40 ms, less the server's 1 ms", F(s.delaySec));
        Check(std::fabs(s.UncertaintySec() - 0.020) < 1e-6, "uncertainty is half the round trip");
        Check(Describe(s).find("0.250 s behind UTC") != std::string::npos, "described", Describe(s));

        t4 = Reply(r, t1, +1.5, 0.010, 0.0, 0.010);
        s = Read(r, 48, t1, t4);
        Check(s.ok && std::fabs(s.offsetSec + 1.5) < 1e-6, "PC 1.5 s ahead: offset -1.500", F(s.offsetSec));
        Check(Describe(s).find("ahead of UTC") != std::string::npos, "described as ahead", Describe(s));

        // Asymmetric path: 60 ms out, 0 back. SNTP cannot see asymmetry;
        // the error it causes is half the difference, inside the stated
        // uncertainty.
        t4 = Reply(r, t1, 0.0, 0.060, 0.0, 0.000);
        s = Read(r, 48, t1, t4);
        Check(s.ok && std::fabs(s.offsetSec - 0.030) < 1e-6 &&
              std::fabs(s.offsetSec) <= s.UncertaintySec() + 1e-9,
              "an asymmetric path errs by no more than the uncertainty",
              F(s.offsetSec) + " within " + F(s.UncertaintySec()));
    }

    printf("\n4. Refusals\n");
    {
        uint8_t r[48];
        double t4 = Reply(r, t1, 0, 0.01, 0, 0.01, 2, 3);
        Check(!Read(r, 48, t1, t4).ok, "client mode, not a server", Read(r, 48, t1, t4).why);
        t4 = Reply(r, t1, 0, 0.01, 0, 0.01, 0);
        Check(Read(r, 48, t1, t4).why.find("kiss") != std::string::npos, "stratum 0 is a refusal",
              Read(r, 48, t1, t4).why);
        t4 = Reply(r, t1, 0, 0.01, 0, 0.01, 16);
        Check(!Read(r, 48, t1, t4).ok, "stratum 16 is unsynchronised");
        t4 = Reply(r, t1, 0, 0.01, 0, 0.01, 2, 4, 3);
        Check(!Read(r, 48, t1, t4).ok, "leap indicator 3 is unsynchronised");
        t4 = Reply(r, t1, 0, 0.01, 0, 0.01);
        Check(!Read(r, 48, t1 + 0.5, t4).ok, "a reply to another request is refused",
              Read(r, 48, t1 + 0.5, t4).why);
        t4 = Reply(r, t1, 0, 0.01, 0, 0.01);
        memset(&r[32], 0, 16);
        Check(!Read(r, 48, t1, t4).ok, "a reply with no time is refused");
        Check(!Read(r, 40, t1, t4).ok, "a short reply is refused");
    }

    printf("\n5. Best of several\n");
    {
        uint8_t r[48];
        std::vector<Sample> all;
        double t4 = Reply(r, t1, -0.2, 0.080, 0, 0.080); all.push_back(Read(r, 48, t1, t4));
        t4 = Reply(r, t1, -0.2, 0.005, 0, 0.005);         all.push_back(Read(r, 48, t1, t4));
        t4 = Reply(r, t1, -0.2, 0.030, 0, 0.030);         all.push_back(Read(r, 48, t1, t4));
        Sample bad; bad.why = "timed out";                all.push_back(bad);
        const Sample b = Best(all);
        Check(b.ok && std::fabs(b.delaySec - 0.010) < 1e-6, "the 10 ms round trip is chosen", F(b.delaySec));
        const Sample none = Best({bad});
        Check(!none.ok && none.why == "timed out", "with no good sample, the reason is kept", none.why);
    }

    printf("\n6. Confirmed\n");
    {
        Sample s; s.ok = true; s.offsetSec = 3.0; s.delaySec = 0.040;
        Check(Confirmed(s), "a 3 s offset known to 20 ms is confirmed: it can be removed");
        s.delaySec = 0.120;
        Check(!Confirmed(s), "a 60 ms uncertainty is not");
        Check(Describe(s).find("NOT confirmed") != std::string::npos, "and says so", Describe(s));
        Sample f; f.why = "timed out";
        Check(!Confirmed(f) && Describe(f) == "clock not checked: timed out", "a failed check says why",
              Describe(f));
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
