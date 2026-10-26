// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// ============================================================
//  Fuzzing the WebSocket protocol, under AddressSanitizer
//
//  P5.6. tests/ws_protocol.cpp checks the rules somebody thought to
//  write down. This throws what nobody thought of: valid traffic with
//  bytes flipped, inserted, deleted and cut short, delivered in random
//  read sizes, plus raw noise. run-tests.ps1 builds it with
//  /fsanitize=address, so an out-of-bounds read that happens to land
//  on mapped memory -- the kind that passes every ordinary test -- is a
//  crash here.
//
//  Deterministic: a fixed seed, so a failure reproduces exactly. Pass a
//  seed and an iteration count to explore further:
//
//      tests\_build\ws_fuzz.exe 12345 2000000
//
//  Four kinds of check, because "it did not crash" is the weakest:
//
//    1. Invariants that must hold for ANY input (sizes bounded, the
//       buffer only ever shrinks, nothing is produced after a failure).
//    2. A differential: unmutated traffic must decode to the same
//       messages however it is split across reads.
//    3. ValidUtf8 against an independent reference written from the
//       Unicode standard's table of well-formed sequences.
//    4. The handshake and command parsers on mutated requests.
// ============================================================

#include "net/WsProtocol.h"
#include "net/APEXTelemetry.h"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

using namespace raduk::net;
using namespace raduk::net::ws;

static int g_fail = 0;

static void Fail(const char* what, uint64_t iter) {
    if (g_fail < 20) printf("  [FAIL] %s (iteration %llu)\n", what, (unsigned long long)iter);
    g_fail++;
}

// xorshift64*: small, fast, and the same on every machine.
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    uint64_t Next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ull; }
    uint32_t Below(uint32_t n) { return n ? uint32_t(Next() % n) : 0; }
    bool     Chance(uint32_t oneIn) { return Below(oneIn) == 0; }
    char     Byte() { return char(Next() & 0xFF); }
};

static std::string Frame(Rng& rng, bool fin, unsigned opcode, const std::string& payload) {
    std::string f;
    f += char((fin ? 0x80 : 0) | (opcode & 0x0F));
    const uint64_t n = payload.size();
    if (n < 126) f += char(0x80 | n);
    else if (n <= 0xFFFF) { f += char(0x80 | 126); f += char(n >> 8); f += char(n & 0xFF); }
    else { f += char(0x80 | 127); for (int i = 7; i >= 0; i--) f += char((n >> (i * 8)) & 0xFF); }
    char key[4];
    for (char& k : key) { k = rng.Byte(); f += k; }
    for (size_t i = 0; i < payload.size(); i++) f += char(payload[i] ^ key[i & 3]);
    return f;
}

static std::string Utf8Text(Rng& rng, size_t len) {
    // Mostly ASCII JSON-ish, with real multi-byte characters mixed in.
    static const char* pieces[] = {
        "{\"cmd\":\"jog\",\"arg\":\"N\"}", "\xC3\xA9", "\xE2\x82\xAC", "\xF0\x9F\x94\xAD",
        "move_eaf", "14500", " ", "\"", "{", "}", ":", ","
    };
    std::string s;
    while (s.size() < len) s += pieces[rng.Below(sizeof(pieces) / sizeof(pieces[0]))];
    return s;
}

// A legal client conversation and the messages it should decode to.
struct Traffic { std::string bytes; std::vector<std::string> expect; size_t pings = 0; };

static Traffic MakeTraffic(Rng& rng) {
    Traffic t;
    const int nMsgs = 1 + int(rng.Below(6));
    for (int m = 0; m < nMsgs; m++) {
        const bool text = !rng.Chance(4);
        size_t len = rng.Chance(20) ? 126 + rng.Below(70000) : rng.Below(200);
        std::string msg = text ? Utf8Text(rng, len) : std::string();
        if (!text) for (size_t i = 0; i < len; i++) msg += rng.Byte();

        // Split into 1-4 fragments. A text fragment boundary may fall
        // inside a multi-byte character; that is legal, and validation
        // must happen on the reassembled message, not per fragment.
        const int nFrag = rng.Chance(3) ? 1 + int(rng.Below(4)) : 1;
        size_t at = 0;
        for (int f = 0; f < nFrag; f++) {
            const bool last = (f == nFrag - 1);
            const size_t take = last ? msg.size() - at
                                     : rng.Below(uint32_t(msg.size() - at + 1));
            t.bytes += Frame(rng, last, f == 0 ? (text ? 0x1 : 0x2) : 0x0, msg.substr(at, take));
            at += take;
            if (!last && rng.Chance(3)) {           // a ping between fragments
                std::string p; for (uint32_t i = 0, n = rng.Below(126); i < n; i++) p += rng.Byte();
                t.bytes += Frame(rng, true, 0x9, p);
                t.pings++;
            }
        }
        t.expect.push_back(msg);
    }
    return t;
}

static void Mutate(Rng& rng, std::string& s) {
    const int n = 1 + int(rng.Below(8));
    for (int k = 0; k < n && !s.empty(); k++) {
        const size_t at = rng.Below(uint32_t(s.size()));
        switch (rng.Below(6)) {
        case 0: s[at] = rng.Byte(); break;                        // replace
        case 1: s[at] ^= char(1 << rng.Below(8)); break;          // bit flip
        case 2: s.insert(at, 1, rng.Byte()); break;               // insert
        case 3: s.erase(at, 1); break;                            // delete
        case 4: s.resize(at); break;                              // truncate
        case 5: {                                                 // a hostile length
            static const char lens[] = { char(0xFE), char(0xFF), char(0x7E), char(0x7F) };
            s[at] = lens[rng.Below(4)];
            break;
        }
        }
    }
}

// Feed `bytes` to a fresh parser in random-sized reads, checking every
// invariant that must hold no matter what the bytes are.
static std::vector<std::string> FeedChunked(Rng& rng, const std::string& bytes, uint64_t iter,
                                            bool* failed, size_t* pings) {
    FrameParser p;
    std::string buf;
    std::vector<std::string> out;
    size_t at = 0;
    *failed = false;
    *pings = 0;
    while (at < bytes.size()) {
        const size_t take = 1 + rng.Below(rng.Chance(2) ? 8 : 4096);
        const size_t n = take < bytes.size() - at ? take : bytes.size() - at;
        buf.append(bytes, at, n);
        at += n;

        const size_t before = buf.size();
        FeedResult r = p.Feed(buf);
        if (buf.size() > before) Fail("the parser grew its own buffer", iter);
        for (auto& m : r.messages) {
            if (m.size() > kMaxMessage) Fail("a message larger than kMaxMessage", iter);
            out.push_back(m);
        }
        for (auto& pg : r.pings) {
            if (pg.size() > kMaxControl) Fail("a ping payload over 125 bytes", iter);
        }
        *pings += r.pings.size();
        if (r.fail) {
            if (!r.why || !*r.why) Fail("a failure with no reason", iter);
            *failed = true;
            return out;                              // the server drops the peer here
        }
        if (r.close) return out;
        // Anything left over must be less than one whole frame's header
        // plus payload limit -- the parser may not sit on a complete frame.
        if (buf.size() > 14 + kMaxFramePayload) Fail("buffer holds more than one frame", iter);
    }
    return out;
}

// ── An independent UTF-8 reference ───────────────────────────
// Written from Unicode 15, Table 3-7 (well-formed byte sequences),
// deliberately in a different shape from ValidUtf8 so the two cannot
// share a mistake.
static bool RefUtf8(const std::string& s) {
    const unsigned char* b = reinterpret_cast<const unsigned char*>(s.data());
    size_t i = 0, n = s.size();
    auto in = [](unsigned char c, unsigned lo, unsigned hi) { return c >= lo && c <= hi; };
    while (i < n) {
        unsigned char c = b[i];
        if (c <= 0x7F) { i += 1; continue; }
        if (in(c, 0xC2, 0xDF)) {
            if (i + 1 >= n || !in(b[i+1], 0x80, 0xBF)) return false;
            i += 2; continue;
        }
        if (c == 0xE0 || in(c, 0xE1, 0xEC) || c == 0xED || in(c, 0xEE, 0xEF)) {
            if (i + 2 >= n) return false;
            unsigned lo = 0x80, hi = 0xBF;
            if (c == 0xE0) lo = 0xA0;
            if (c == 0xED) hi = 0x9F;
            if (!in(b[i+1], lo, hi) || !in(b[i+2], 0x80, 0xBF)) return false;
            i += 3; continue;
        }
        if (c == 0xF0 || in(c, 0xF1, 0xF3) || c == 0xF4) {
            if (i + 3 >= n) return false;
            unsigned lo = 0x80, hi = 0xBF;
            if (c == 0xF0) lo = 0x90;
            if (c == 0xF4) hi = 0x8F;
            if (!in(b[i+1], lo, hi) || !in(b[i+2], 0x80, 0xBF) || !in(b[i+3], 0x80, 0xBF)) return false;
            i += 4; continue;
        }
        return false;
    }
    return true;
}

static const char* kRequest =
    "GET /?token=0123456789abcdef HTTP/1.1\r\n"
    "Host: localhost:8765\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "\r\n";

int main(int argc, char** argv) {
    const uint64_t seed  = argc > 1 ? strtoull(argv[1], nullptr, 10) : 0x5EED0056ull;
    const uint64_t iters = argc > 2 ? strtoull(argv[2], nullptr, 10) : 20000;
    printf("\n=== WebSocket fuzz: seed %llu, %llu iterations per stage ===\n",
           (unsigned long long)seed, (unsigned long long)iters);
#if defined(__SANITIZE_ADDRESS__)
    printf("  AddressSanitizer: ON\n");
#else
    printf("  AddressSanitizer: off -- invariants only, memory errors may go unseen\n");
#endif
    Rng rng(seed);

    // ── 1+2. Frames: legal traffic, split arbitrarily ────────
    printf("\n1. Legal traffic in random read sizes decodes identically\n");
    uint64_t decoded = 0;
    for (uint64_t it = 0; it < iters; it++) {
        Traffic t = MakeTraffic(rng);
        bool failed; size_t pings;
        auto got = FeedChunked(rng, t.bytes, it, &failed, &pings);
        if (failed)               { Fail("legal traffic was rejected", it); continue; }
        if (got != t.expect)      { Fail("legal traffic decoded to different messages", it); continue; }
        if (pings != t.pings)     { Fail("a ping was lost or invented", it); continue; }
        decoded += got.size();
    }
    printf("  %llu messages decoded, %d failure(s)\n", (unsigned long long)decoded, g_fail);

    // ── 1. Frames: mutated traffic and raw noise ─────────────
    printf("\n2. Mutated traffic and raw noise: invariants hold, nothing crashes\n");
    const int before2 = g_fail;
    uint64_t rejected = 0, survived = 0;
    for (uint64_t it = 0; it < iters; it++) {
        std::string bytes;
        if (rng.Chance(8)) {
            for (uint32_t i = 0, n = rng.Below(512); i < n; i++) bytes += rng.Byte();
        } else {
            bytes = MakeTraffic(rng).bytes;
            Mutate(rng, bytes);
        }
        bool failed; size_t pings;
        auto got = FeedChunked(rng, bytes, it, &failed, &pings);
        for (auto& m : got) if (m.size() > kMaxMessage) Fail("oversized message escaped", it);
        (failed ? rejected : survived)++;
    }
    printf("  %llu rejected, %llu accepted or incomplete, %d failure(s)\n",
           (unsigned long long)rejected, (unsigned long long)survived, g_fail - before2);
    if (rejected == 0) Fail("mutation never produced a rejection -- the fuzzer is not reaching the checks", 0);

    // ── 3. UTF-8 against the reference ───────────────────────
    printf("\n3. ValidUtf8 agrees with the Unicode table on every input\n");
    const int before3 = g_fail;
    uint64_t valid = 0;
    for (uint64_t it = 0; it < iters * 5; it++) {
        std::string s;
        const uint32_t n = rng.Below(12);
        for (uint32_t i = 0; i < n; i++) {
            // Bias toward the bytes where UTF-8 is interesting.
            static const unsigned char hot[] = { 0x7F, 0x80, 0xBF, 0xC0, 0xC1, 0xC2, 0xDF, 0xE0,
                0xA0, 0x9F, 0xED, 0xEF, 0xF0, 0x90, 0x8F, 0xF4, 0xF5, 0xFF };
            s += rng.Chance(2) ? char(hot[rng.Below(sizeof(hot))]) : rng.Byte();
        }
        const bool a = ValidUtf8(s), b = RefUtf8(s);
        if (a != b) {
            if (g_fail - before3 < 5) {
                printf("    disagreement on:");
                for (unsigned char c : s) printf(" %02X", c);
                printf("  (ValidUtf8 %d, reference %d)\n", a, b);
            }
            Fail("ValidUtf8 disagrees with the reference", it);
        }
        valid += a;
    }
    // Every scalar value, encoded properly, must pass; every surrogate must not.
    for (uint32_t cp = 0; cp <= 0x10FFFF; cp++) {
        std::string s;
        if (cp < 0x80) s += char(cp);
        else if (cp < 0x800) { s += char(0xC0 | (cp >> 6)); s += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { s += char(0xE0 | (cp >> 12)); s += char(0x80 | ((cp >> 6) & 0x3F)); s += char(0x80 | (cp & 0x3F)); }
        else { s += char(0xF0 | (cp >> 18)); s += char(0x80 | ((cp >> 12) & 0x3F)); s += char(0x80 | ((cp >> 6) & 0x3F)); s += char(0x80 | (cp & 0x3F)); }
        const bool surrogate = cp >= 0xD800 && cp <= 0xDFFF;
        if (ValidUtf8(s) == surrogate) { Fail("a code point was misjudged", cp); break; }
    }
    printf("  %llu random strings agreed (%llu valid); all 1,114,112 code points checked; %d failure(s)\n",
           (unsigned long long)(iters * 5), (unsigned long long)valid, g_fail - before3);

    // ── 4. Handshake and command parser ──────────────────────
    printf("\n4. Handshake and command parser on mutated input\n");
    const int before4 = g_fail;
    uint64_t ok = 0;
    for (uint64_t it = 0; it < iters; it++) {
        std::string req = kRequest;
        if (rng.Chance(2)) Mutate(rng, req);
        if (rng.Chance(4)) req += Frame(rng, true, 0x1, "{\"cmd\":\"jog\"}");   // pipelined
        Handshake h = ParseHandshake(req);
        if (h.state == Handshake::State::Ok) {
            ok++;
            if (h.consumed == 0 || h.consumed > req.size()) Fail("consumed is out of range", it);
            if (h.key.size() != 24)                          Fail("Ok with a key that is not 24 characters", it);
            if (req.compare(h.consumed - 4, 4, "\r\n\r\n") != 0) Fail("consumed does not end at the blank line", it);
        } else if (h.state == Handshake::State::Bad) {
            if (h.status != 400 && h.status != 426 && h.status != 431) Fail("an unexpected status", it);
        }

        std::string js;
        if (rng.Chance(2)) {
            js = "{\"cmd\":\"move_eaf\",\"arg\":14500}";
            Mutate(rng, js);
        } else {
            for (uint32_t i = 0, n = rng.Below(64); i < n; i++) js += rng.Byte();
        }
        APEXCommand c;
        if (APEXCommand::Parse(js, c)) {
            if (c.cmd.size() > js.size() || c.arg.size() > js.size()) Fail("a parsed field longer than its input", it);
        }
    }
    printf("  %llu of %llu requests upgraded; %d failure(s)\n",
           (unsigned long long)ok, (unsigned long long)iters, g_fail - before4);

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
