// ============================================================
//  The WebSocket protocol, held to RFC 6455
//
//  P5.6. This is the only code an attacker can feed bytes to, and the
//  commands behind it move the mount. Until now it could only be tested
//  by opening a socket, so it never was.
//
//  These are the deterministic cases: one per rule, each named for what
//  it protects. tests/ws_fuzz.cpp is the other half -- what happens
//  under input nobody thought to write down.
// ============================================================

#include "net/WsProtocol.h"
#include "net/APEXTelemetry.h"

#include <cstdio>
#include <cstdint>
#include <string>

using namespace raduk::net;
using namespace raduk::net::ws;

static int g_fail = 0;

static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-58s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

// A client-to-server frame. Masked unless told otherwise, because a
// conforming client always masks; the tests that need an unmasked one
// say so.
static std::string Frame(bool fin, unsigned opcode, const std::string& payload,
                         bool masked = true, unsigned rsv = 0, int lenForm = 0) {
    std::string f;
    f += char((fin ? 0x80 : 0x00) | (rsv & 0x70) | (opcode & 0x0F));
    const uint64_t n = payload.size();
    const unsigned char m = masked ? 0x80 : 0x00;
    // lenForm forces an encoding: 0 = minimal, 16 = 2-byte, 64 = 8-byte.
    if ((lenForm == 0 && n < 126)) {
        f += char(m | n);
    } else if (lenForm == 16 || (lenForm == 0 && n <= 0xFFFF)) {
        f += char(m | 126);
        f += char((n >> 8) & 0xFF); f += char(n & 0xFF);
    } else {
        f += char(m | 127);
        for (int i = 7; i >= 0; i--) f += char((n >> (i * 8)) & 0xFF);
    }
    const unsigned char key[4] = { 0x37, 0xFA, 0x21, 0x3D };
    if (masked) for (unsigned char k : key) f += char(k);
    for (size_t i = 0; i < payload.size(); i++)
        f += char(payload[i] ^ (masked ? key[i & 3] : 0));
    return f;
}

static const char* kValidHandshake =
    "GET /?token=abc123 HTTP/1.1\r\n"
    "Host: localhost:8765\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "\r\n";

int main() {
    printf("\n=== WebSocket protocol: RFC 6455, server side ===\n");

    printf("\n1. Frames that should be accepted\n");
    {
        FrameParser p;
        std::string b = Frame(true, 0x1, R"({"cmd":"jog"})");
        FeedResult r = p.Feed(b);
        Check(!r.fail && r.messages.size() == 1 && r.messages[0] == R"({"cmd":"jog"})",
              "a masked text frame is one message", r.messages.empty() ? "" : r.messages[0]);
        Check(b.empty(), "and the buffer is consumed");
    }
    {
        FrameParser p;
        std::string b = Frame(true, 0x1, "one") + Frame(true, 0x1, "two");
        FeedResult r = p.Feed(b);
        Check(r.messages.size() == 2 && r.messages[1] == "two",
              "two frames in one read are two messages");
    }
    {
        FrameParser p;
        std::string payload(300, 'x');            // forces the 16-bit length
        std::string b = Frame(true, 0x2, payload);
        FeedResult r = p.Feed(b);
        Check(!r.fail && r.messages.size() == 1 && r.messages[0].size() == 300,
              "a 300-byte binary frame uses the 2-byte length");
    }
    {
        FrameParser p;
        std::string payload(70000, 'y');          // forces the 64-bit length
        std::string b = Frame(true, 0x2, payload);
        FeedResult r = p.Feed(b);
        Check(!r.fail && r.messages.size() == 1 && r.messages[0].size() == 70000,
              "a 70000-byte frame uses the 8-byte length");
    }

    printf("\n2. Partial frames wait, they do not guess\n");
    {
        FrameParser p;
        const std::string whole = Frame(true, 0x1, "hello");
        std::string b = whole.substr(0, 4);
        FeedResult r = p.Feed(b);
        Check(!r.fail && r.messages.empty() && b.size() == 4,
              "half a frame produces nothing and is kept");
        b += whole.substr(4);
        r = p.Feed(b);
        Check(r.messages.size() == 1 && r.messages[0] == "hello",
              "the rest arrives and completes it");
    }
    {
        // Byte at a time: the worst a slow or hostile peer can do.
        FrameParser p;
        const std::string whole = Frame(true, 0x1, "byte by byte");
        std::string b;
        std::vector<std::string> got;
        for (char c : whole) {
            b += c;
            FeedResult r = p.Feed(b);
            for (auto& m : r.messages) got.push_back(m);
            if (r.fail) { got.push_back("FAIL"); break; }
        }
        Check(got.size() == 1 && got[0] == "byte by byte",
              "one byte per read still yields exactly one message");
    }

    printf("\n3. Fragmentation -- what the old parser dropped\n");
    {
        // The old parser dispatched "{\"cmd\":" as a whole command and
        // threw the continuation away.
        FrameParser p;
        std::string b = Frame(false, 0x1, R"({"cmd":)")
                      + Frame(false, 0x0, R"("goto_target",)")
                      + Frame(true,  0x0, R"("arg":"M13"})");
        FeedResult r = p.Feed(b);
        Check(!r.fail && r.messages.size() == 1 &&
              r.messages[0] == R"({"cmd":"goto_target","arg":"M13"})",
              "three fragments reassemble into ONE command",
              r.messages.empty() ? "(none)" : r.messages[0]);
    }
    {
        // RFC 6455 5.4: control frames may arrive between fragments.
        FrameParser p;
        std::string b = Frame(false, 0x1, "par")
                      + Frame(true,  0x9, "ping-body")
                      + Frame(true,  0x0, "tial");
        FeedResult r = p.Feed(b);
        Check(!r.fail && r.messages.size() == 1 && r.messages[0] == "partial"
              && r.pings.size() == 1 && r.pings[0] == "ping-body",
              "a ping between fragments is answered, and the message survives");
    }
    {
        FrameParser p;
        std::string b = Frame(true, 0x0, "orphan");
        Check(p.Feed(b).fail, "a continuation with nothing to continue fails");
    }
    {
        FrameParser p;
        std::string b = Frame(false, 0x1, "first") + Frame(true, 0x1, "second");
        Check(p.Feed(b).fail, "a new message inside an unfinished one fails");
    }

    printf("\n4. Frames that must fail the connection\n");
    {
        FrameParser p;
        std::string b = Frame(true, 0x1, "hi", /*masked=*/false);
        FeedResult r = p.Feed(b);
        Check(r.fail, "an UNMASKED client frame (RFC 6455 5.1)", r.why);
    }
    {
        FrameParser p;
        std::string b = Frame(true, 0x1, "hi", true, /*rsv=*/0x40);
        Check(p.Feed(b).fail, "RSV1 set with no extension negotiated");
    }
    for (unsigned op : { 0x3u, 0x7u, 0xBu, 0xFu }) {
        FrameParser p;
        std::string b = Frame(true, op, "x");
        char lbl[64]; snprintf(lbl, sizeof(lbl), "reserved opcode 0x%X", op);
        Check(p.Feed(b).fail, lbl);
    }
    {
        FrameParser p;
        std::string b = Frame(true, 0x9, std::string(126, 'p'));
        Check(p.Feed(b).fail, "a ping over 125 bytes");
    }
    {
        FrameParser p;
        std::string b = Frame(false, 0x9, "frag");
        Check(p.Feed(b).fail, "a fragmented control frame");
    }
    {
        FrameParser p;
        std::string b = Frame(true, 0x1, "\xC3\x28");   // invalid 2-byte sequence
        Check(p.Feed(b).fail, "a text frame that is not valid UTF-8");
    }
    {
        // The same bytes are fine as BINARY: UTF-8 is a text-frame rule.
        FrameParser p;
        std::string b = Frame(true, 0x2, "\xC3\x28");
        Check(!p.Feed(b).fail, "  but the same bytes are fine in a binary frame");
    }
    {
        FrameParser p;
        std::string b = Frame(true, 0x1, "short", true, 0, /*lenForm=*/16);
        Check(p.Feed(b).fail, "a 5-byte payload sent with a 2-byte length (non-minimal)");
    }
    {
        // Hand-built: 64-bit length with the top bit set.
        FrameParser p;
        std::string b;
        b += char(0x81); b += char(0x80 | 127);
        b += char(0x80); for (int i = 0; i < 7; i++) b += char(0);
        b += std::string(4, '\0');
        Check(p.Feed(b).fail, "a 64-bit length with its top bit set");
    }
    {
        // A header promising 2 MB. Must fail on the HEADER, before any
        // allocation -- that is the attack the clamp exists for.
        FrameParser p;
        std::string b;
        const uint64_t n = 2 * 1024 * 1024;
        b += char(0x82); b += char(0x80 | 127);
        for (int i = 7; i >= 0; i--) b += char((n >> (i * 8)) & 0xFF);
        b += std::string(4, '\0');
        FeedResult r = p.Feed(b);
        Check(r.fail, "a 2 MB frame fails on its header alone", r.why);
    }
    {
        // Each fragment legal, the sum not.
        FrameParser p;
        std::string chunk(512 * 1024, 'z');
        std::string b = Frame(false, 0x2, chunk) + Frame(false, 0x0, chunk)
                      + Frame(true,  0x0, chunk);
        Check(p.Feed(b).fail, "three legal 512 KB fragments that sum past 1 MB");
    }

    printf("\n5. Control frames\n");
    {
        FrameParser p;
        std::string b = Frame(true, 0x9, "are you there");
        FeedResult r = p.Feed(b);
        Check(r.pings.size() == 1 && r.pings[0] == "are you there",
              "a ping's payload is handed back for the pong",
              "(the old code sent an empty pong)");
    }
    {
        FrameParser p;
        std::string b = Frame(true, 0xA, "unsolicited");
        FeedResult r = p.Feed(b);
        Check(!r.fail && r.messages.empty(), "an unsolicited pong is ignored");
    }
    {
        FrameParser p;
        std::string b = Frame(true, 0x8, "") + Frame(true, 0x1, "after close");
        FeedResult r = p.Feed(b);
        Check(r.close && r.messages.empty(), "nothing after a Close is acted on");
    }

    printf("\n6. The handshake\n");
    {
        Handshake h = ParseHandshake(kValidHandshake);
        Check(h.state == Handshake::State::Ok, "a valid upgrade request is accepted");
        Check(h.key == "dGhlIHNhbXBsZSBub25jZQ==", "  the key is extracted", h.key);
        Check(h.token == "abc123", "  the token comes from the query string", h.token);
        Check(h.consumed == std::string(kValidHandshake).size(),
              "  and consumed stops at the blank line");
    }
    {
        // The old code cleared the buffer after upgrading and lost this.
        const std::string frame = Frame(true, 0x1, "pipelined");
        const std::string buf = std::string(kValidHandshake) + frame;
        Handshake h = ParseHandshake(buf);
        Check(h.state == Handshake::State::Ok && buf.substr(h.consumed) == frame,
              "a frame sent in the same packet survives the upgrade");
    }
    {
        std::string req =
            "GET /?token=t HTTP/1.1\r\nupgrade: WebSocket\r\n"
            "sec-websocket-key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "SEC-WEBSOCKET-VERSION: 13\r\n\r\n";
        Check(ParseHandshake(req).state == Handshake::State::Ok,
              "header names are case-insensitive, as HTTP requires");
    }
    {
        Handshake h = ParseHandshake("GET / HTTP/1.1\r\nHost: x\r\n");
        Check(h.state == Handshake::State::Incomplete, "a request with no blank line yet waits");
    }
    {
        std::string req = kValidHandshake;
        req.replace(req.find("13\r\n"), 2, "8");
        Handshake h = ParseHandshake(req);
        Check(h.state == Handshake::State::Bad && h.status == 426,
              "version 8 gets 426 Upgrade Required", std::to_string(h.status));
    }
    {
        std::string req = kValidHandshake;
        req.erase(req.find("Upgrade: websocket\r\n"), 20);
        Check(ParseHandshake(req).status == 400, "no Upgrade header is a 400");
    }
    {
        std::string req = kValidHandshake;
        req.replace(req.find("dGhlIHNhbXBsZSBub25jZQ=="), 24, "short");
        Check(ParseHandshake(req).status == 400, "a key that is not 24 characters is a 400");
    }
    {
        std::string req = kValidHandshake;
        req.replace(0, 3, "PUT");
        Check(ParseHandshake(req).status == 400, "anything but GET is a 400");
    }
    {
        std::string huge = "GET / HTTP/1.1\r\nX: " + std::string(9000, 'a');
        Handshake h = ParseHandshake(huge);
        Check(h.state == Handshake::State::Bad && h.status == 431,
              "headers past 8 KB are refused, not buffered");
    }
    {
        Check(ParseHandshake("GET /?a=1&token=xyz&b=2 HTTP/1.1\r\nUpgrade: websocket\r\n"
                             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                             "Sec-WebSocket-Version: 13\r\n\r\n").token == "xyz",
              "the token is found among other parameters");
        Check(ParseHandshake("GET / HTTP/1.1\r\nUpgrade: websocket\r\n"
                             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                             "Sec-WebSocket-Version: 13\r\n\r\n").token.empty(),
              "  and is empty when there is none");
    }

    printf("\n7. The token\n");
    {
        Check( TokenEquals("abc123", "abc123"), "the right token matches");
        Check(!TokenEquals("abc123", "abc124"), "one character off does not");
        Check(!TokenEquals("abc123", "abc12"),  "a prefix does not");
        Check(!TokenEquals("abc123", "abc1234"), "one character too many does not");
        Check(!TokenEquals("abc123", ""),       "no token does not");
        // Fail CLOSED. A server never given a token refuses everyone,
        // including a client that also sends nothing.
        Check(!TokenEquals("", ""),             "an unconfigured server matches nobody, not everybody");
        Check(!TokenEquals("", "anything"),     "  not even someone who guesses");

        const std::string gen(64, 'a');
        Check( TokenLooksStrong(gen),                          "a 64-hex-character token is strong enough");
        Check( TokenLooksStrong(std::string(32, 'F')),         "32 hex characters (128 bits) is the floor");
        Check(!TokenLooksStrong(std::string(31, 'f')),         "31 is not");
        Check(!TokenLooksStrong("1234"),                       "a hand-typed 1234 is refused, not trusted");
        Check(!TokenLooksStrong(std::string(40, 'a') + "&x=1"), "anything but hex is refused (it rides in a URL)");
    }

    printf("\n8. The command parser behind it\n");
    {
        APEXCommand c;
        Check(APEXCommand::Parse(R"({"cmd":"jog","arg":"N"})", c) && c.cmd == "jog" && c.arg == "N",
              "a quoted arg");
        Check(APEXCommand::Parse(R"({"cmd":"move_eaf","arg":14500})", c) && c.arg == "14500",
              "a numeric arg");
        Check(!APEXCommand::Parse(R"({"arg":"N"})", c), "no cmd is refused");
        // isdigit() on a plain char is undefined for bytes >= 0x80, which
        // are negative on MSVC. This used to reach it directly.
        std::string hostile = R"({"cmd":"move_eaf","arg":)";
        hostile += "\xFF\xC3\x80" "123";
        const bool ok = APEXCommand::Parse(hostile, c);
        Check(ok && c.arg.empty(), "a high byte after \"arg\": is not a digit, and does not crash",
              "arg=\"" + c.arg + "\"");
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
