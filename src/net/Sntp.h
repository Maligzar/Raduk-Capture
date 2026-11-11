#pragma once
// ============================================================
//  Sntp.h -- phase 141.1: how far the PC clock is from UTC
//
//  IMCCE's PHEMU protocol asks for every point of a light curve to be
//  dated to UTC within 0.1 s, and for the clock's offset from UTC to be
//  measured before and after each observation. Its observer's guide says
//  not to trust a computer's own clock. Every SER frame here is stamped
//  from the Windows system clock (CaptureWriter's NowUtcTicks), so that
//  is the clock to measure.
//
//  This header is the pure half: building an SNTP request, reading the
//  reply, refusing a reply that is not an answer to this request, and
//  the offset and round-trip arithmetic (RFC 4330). SntpClient.h sends
//  it. tests/sntp_offset.cpp gates it.
//
//  Times here are UTC seconds since 1970 as doubles. A double carries
//  about a microsecond at today's dates, which is ample for a 0.1 s
//  requirement.
// ============================================================

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace raduk::net::sntp {

// Seconds from the NTP era (1900-01-01) to the Unix epoch.
inline constexpr double kNtpToUnix = 2208988800.0;

inline void PutTimestamp(uint8_t* p, double unixSec) {
    const double ntp = unixSec + kNtpToUnix;
    const double whole = std::floor(ntp);
    const uint32_t sec  = (uint32_t)(uint64_t)whole;
    const uint32_t frac = (uint32_t)((ntp - whole) * 4294967296.0);
    p[0] = (uint8_t)(sec >> 24);  p[1] = (uint8_t)(sec >> 16);
    p[2] = (uint8_t)(sec >> 8);   p[3] = (uint8_t)sec;
    p[4] = (uint8_t)(frac >> 24); p[5] = (uint8_t)(frac >> 16);
    p[6] = (uint8_t)(frac >> 8);  p[7] = (uint8_t)frac;
}

inline double GetTimestamp(const uint8_t* p) {
    const uint32_t sec  = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    const uint32_t frac = ((uint32_t)p[4] << 24) | ((uint32_t)p[5] << 16) | ((uint32_t)p[6] << 8) | p[7];
    if (sec == 0 && frac == 0) return 0.0;
    return (double)sec + (double)frac / 4294967296.0 - kNtpToUnix;
}

// A 48-byte client request. The transmit timestamp is set to the local
// send time, and the server copies it into its reply's originate field,
// which is how a reply is matched to this request.
inline std::vector<uint8_t> Request(double localSendUnix) {
    std::vector<uint8_t> b(48, 0);
    b[0] = 0x23;   // LI 0, version 4, mode 3 (client)
    PutTimestamp(&b[40], localSendUnix);
    return b;
}

struct Sample {
    bool        ok = false;
    std::string why;            // when not ok
    double      offsetSec = 0;  // UTC minus the PC clock: add it to a PC time to get UTC
    double      delaySec  = 0;  // round trip, less the server's own time
    int         stratum   = 0;
    // The offset is known to within half the round trip.
    double UncertaintySec() const { return delaySec / 2.0; }
};

// Reads a reply. t1 is the request's transmit timestamp as sent (the
// local clock), t4 the local clock when the reply arrived.
inline Sample Read(const uint8_t* r, size_t n, double t1, double t4) {
    Sample s;
    if (n < 48) { s.why = "reply too short"; return s; }
    const int mode = r[0] & 7;
    const int li   = r[0] >> 6;
    if (mode != 4) { s.why = "not a server reply"; return s; }
    s.stratum = r[1];
    if (s.stratum == 0) { s.why = "the server refused (kiss-o'-death)"; return s; }
    if (s.stratum > 15) { s.why = "the server is not synchronised"; return s; }
    if (li == 3) { s.why = "the server's clock is not synchronised"; return s; }
    const double origin = GetTimestamp(&r[24]);
    const double t2 = GetTimestamp(&r[32]);
    const double t3 = GetTimestamp(&r[40]);
    // The originate field must echo this request's transmit time, to
    // the 2^-32 s the format carries. Anything else is a stale or forged
    // reply, and an offset from it would be wrong with no sign of it.
    uint8_t mine[8];
    PutTimestamp(mine, t1);
    if (std::memcmp(mine, &r[24], 8) != 0 || origin == 0.0) {
        s.why = "the reply does not answer this request";
        return s;
    }
    if (t2 == 0.0 || t3 == 0.0) { s.why = "the reply carries no time"; return s; }
    s.offsetSec = ((t2 - t1) + (t3 - t4)) / 2.0;
    s.delaySec  = (t4 - t1) - (t3 - t2);
    if (s.delaySec < 0.0) { s.why = "the round trip came out negative"; return s; }
    s.ok = true;
    return s;
}

// Of several samples, the one with the shortest round trip: its offset
// is the least uncertain. Returns a not-ok sample if none is good, with
// the last reason.
inline Sample Best(const std::vector<Sample>& all) {
    Sample best;
    best.why = "no reply";
    bool have = false;
    for (const Sample& s : all) {
        if (!s.ok) { if (!have) best.why = s.why; continue; }
        if (!have || s.delaySec < best.delaySec) { best = s; have = true; }
    }
    return best;
}

// IMCCE: the clock's offset from UTC known to better than 0.1 s. With
// the offset itself known only to half the round trip, this file calls
// a check confirmed when that half is under 0.05 s, leaving room for
// the rest of the timing chain. The offset's size does not matter: a
// known offset can be removed in the reduction.
inline constexpr double kConfirmedUncertaintySec = 0.05;
inline bool Confirmed(const Sample& s) {
    return s.ok && s.UncertaintySec() < kConfirmedUncertaintySec;
}

// "PC clock 0.231 s behind UTC (+/- 0.012 s, stratum 2)"
inline std::string Describe(const Sample& s) {
    if (!s.ok) return "clock not checked: " + s.why;
    char b[160];
    const double o = s.offsetSec;
    snprintf(b, sizeof(b), "PC clock %.3f s %s UTC (+/- %.3f s, stratum %d)%s",
             std::fabs(o), o >= 0 ? "behind" : "ahead of", s.UncertaintySec(), s.stratum,
             Confirmed(s) ? "" : ", NOT confirmed to 0.05 s");
    return b;
}

}  // namespace raduk::net::sntp
