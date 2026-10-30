// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once
// ============================================================
// WsProtocol.h -- P5.6
//
// The WebSocket protocol, with the sockets taken out.
//
// This is the only code in RadukCapture that an attacker can feed
// bytes to directly, and the commands behind it move the mount. It used
// to live inside WebSocketServer.cpp, interleaved with recv() and
// send(), which meant the only way to test it was to open a socket --
// so it was never tested at all. Here it is pure: bytes in, decisions
// out. tests/ws_protocol.cpp checks it against RFC 6455 and
// tests/ws_fuzz.cpp throws malformed input at it under AddressSanitizer.
//
// Three things live here:
//
//   FrameParser     client-to-server frames, RFC 6455 section 5
//   ParseHandshake  the HTTP upgrade request, section 4.2.1
//   TokenEquals     the shared-secret check, in constant time
//
// The server is the only caller. It keeps the sockets, the SHA-1 and
// the base64; nothing here needs Windows.
// ============================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace raduk::net::ws {

// Per frame. Matches WebSocketServer::MAX_WS_PAYLOAD, which existed
// before this file did and stays the server's own guard as well.
constexpr uint64_t kMaxFramePayload = 1024 * 1024;
// A message reassembled from fragments may not exceed this either --
// otherwise a peer could send a million legal 1 MB fragments.
constexpr size_t   kMaxMessage      = 1024 * 1024;
// RFC 6455 section 5.5: control frames carry at most 125 bytes.
constexpr size_t   kMaxControl      = 125;
// The handshake is a few hundred bytes from any real client.
constexpr size_t   kMaxHandshake    = 8192;

// ── UTF-8 ────────────────────────────────────────────────────
//
// RFC 6455 section 8.1: a text message that is not valid UTF-8 fails
// the connection. Strict: no overlong forms, no surrogates, nothing
// above U+10FFFF. The payload goes on to a JSON reader next, so
// refusing malformed text here removes a class of input that reader
// was never written to think about.
inline bool ValidUtf8(const std::string& s) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        const unsigned char c = p[i];
        if (c < 0x80) { i++; continue; }
        size_t need = 0;
        uint32_t cp = 0;
        if      ((c & 0xE0) == 0xC0) { need = 1; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { need = 2; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { need = 3; cp = c & 0x07; }
        else return false;                          // stray continuation, or 0xF8+
        if (n - i <= need) return false;            // truncated sequence
        for (size_t k = 1; k <= need; k++) {
            const unsigned char cc = p[i + k];
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        // Overlong encodings, surrogates, and beyond Unicode.
        if (need == 1 && cp < 0x80)                     return false;
        if (need == 2 && cp < 0x800)                    return false;
        if (need == 3 && cp < 0x10000)                  return false;
        if (cp >= 0xD800 && cp <= 0xDFFF)               return false;
        if (cp > 0x10FFFF)                              return false;
        i += need + 1;
    }
    return true;
}

// ── Frames ───────────────────────────────────────────────────

struct FeedResult {
    std::vector<std::string> messages;   // complete data messages, in order
    std::vector<std::string> pings;      // each one owed a pong with this payload
    bool        close = false;           // the peer sent Close
    bool        fail  = false;           // protocol violation -- drop the peer
    const char* why   = "";              // for the log; never peer-supplied text
};

// Consumes complete frames from the front of a receive buffer and
// leaves any trailing partial frame in place for the next read.
//
// Carries state between calls, because a message may arrive in
// fragments spread across several reads.
//
// What the old inline parser got wrong, and this one does not:
//
//   - Unmasked client frames were accepted. RFC 6455 section 5.1 says a
//     server MUST close the connection on one. Masking is what stops a
//     browser being used to poison an intermediary's cache.
//   - The FIN bit was never read. A message sent in fragments had its
//     FIRST FRAGMENT dispatched as if it were the whole command, and the
//     continuation frames were silently dropped.
//   - Reserved opcodes and RSV bits were accepted and ignored.
//   - A ping got an empty pong, not its own payload echoed back.
//   - Control frames were not held to 125 bytes or to being unfragmented.
//
// None of those is a memory-safety bug in the old code -- its length
// clamp held -- but each is a place where the parser and the peer
// disagree about what was said, and that disagreement is where the
// memory-safety bugs in parsers like this usually come from.
class FrameParser {
public:
    FeedResult Feed(std::string& buf) {
        FeedResult r;
        auto fail = [&](const char* why) -> FeedResult& {
            r.fail = true;
            r.why  = why;
            return r;
        };

        while (buf.size() >= 2) {
            const unsigned char* d = reinterpret_cast<const unsigned char*>(buf.data());
            const bool     fin    = (d[0] & 0x80) != 0;
            const unsigned rsv    =  d[0] & 0x70;
            const unsigned opcode =  d[0] & 0x0F;
            const bool     masked = (d[1] & 0x80) != 0;
            uint64_t       len    =  d[1] & 0x7F;
            size_t         hdr    = 2;

            if (rsv)     return fail("reserved bits set (no extension was negotiated)");
            if (!masked) return fail("unmasked client frame (RFC 6455 5.1)");

            if (len == 126) {
                if (buf.size() < 4) break;
                len = (uint64_t(d[2]) << 8) | d[3];
                hdr = 4;
                if (len < 126) return fail("non-minimal length encoding");
            } else if (len == 127) {
                if (buf.size() < 10) break;
                len = 0;
                for (int i = 0; i < 8; i++) len = (len << 8) | d[2 + i];
                hdr = 10;
                if (len >> 63)      return fail("64-bit length with the top bit set");
                if (len <= 0xFFFF)  return fail("non-minimal length encoding");
            }

            const bool control = (opcode & 0x8) != 0;
            if (control) {
                if (opcode != 0x8 && opcode != 0x9 && opcode != 0xA)
                    return fail("reserved control opcode");
                if (!fin)              return fail("fragmented control frame");
                if (len > kMaxControl) return fail("control frame over 125 bytes");
            } else {
                if (opcode != 0x0 && opcode != 0x1 && opcode != 0x2)
                    return fail("reserved data opcode");
            }

            // Clamp BEFORE any arithmetic with len, as the old parser did
            // -- an unclamped 64-bit length wraps the total below.
            if (len > kMaxFramePayload) return fail("frame too large");

            const size_t total = hdr + 4 + static_cast<size_t>(len);
            if (buf.size() < total) break;          // wait for the rest

            const unsigned char* mask = d + hdr;
            const unsigned char* pay  = d + hdr + 4;
            std::string payload(static_cast<size_t>(len), '\0');
            for (size_t i = 0; i < payload.size(); i++)
                payload[i] = static_cast<char>(pay[i] ^ mask[i & 3]);

            // Erase before acting: `d`, `mask` and `pay` all point into
            // buf, and nothing below may touch them after this line.
            buf.erase(0, total);

            switch (opcode) {
            case 0x8:                                // Close
                r.close = true;
                return r;                            // nothing after Close counts
            case 0x9:                                // Ping
                r.pings.push_back(std::move(payload));
                break;
            case 0xA:                                // Pong: unsolicited is legal
                break;
            case 0x1:
            case 0x2:                                // a new data message
                if (m_inMessage)
                    return fail("new message started inside a fragmented one");
                if (fin) {
                    if (opcode == 0x1 && !ValidUtf8(payload))
                        return fail("text message is not valid UTF-8");
                    r.messages.push_back(std::move(payload));
                } else {
                    m_inMessage = true;
                    m_opcode    = opcode;
                    m_message   = std::move(payload);
                }
                break;
            case 0x0:                                // continuation
                if (!m_inMessage)
                    return fail("continuation frame with no message to continue");
                if (m_message.size() + payload.size() > kMaxMessage)
                    return fail("reassembled message too large");
                m_message += payload;
                if (fin) {
                    if (m_opcode == 0x1 && !ValidUtf8(m_message))
                        return fail("text message is not valid UTF-8");
                    r.messages.push_back(std::move(m_message));
                    m_message.clear();
                    m_inMessage = false;
                }
                break;
            }
        }
        return r;
    }

    bool InMessage() const { return m_inMessage; }

private:
    bool        m_inMessage = false;
    unsigned    m_opcode    = 0;
    std::string m_message;
};

// ── Handshake ────────────────────────────────────────────────

struct Handshake {
    enum class State { Incomplete, Bad, Ok };
    State       state    = State::Incomplete;
    int         status   = 0;     // HTTP status to answer a Bad request with
    size_t      consumed = 0;     // request bytes, through the blank line
    std::string key;              // Sec-WebSocket-Key
    std::string token;            // from ?token= in the request target
};

namespace detail {
inline char Lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; }

inline bool IEquals(const std::string& a, const char* b) {
    size_t i = 0;
    for (; b[i]; i++) {
        if (i >= a.size() || Lower(a[i]) != Lower(b[i])) return false;
    }
    return i == a.size();
}

inline bool IContains(const std::string& hay, const char* needle) {
    std::string h, n(needle);
    for (char c : hay) h += Lower(c);
    for (char& c : n)  c = Lower(c);
    return h.find(n) != std::string::npos;
}

inline std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    return s.substr(a, b - a);
}

// The value of `name` in a query string, or empty. No percent-decoding:
// a token is hex, so anything that needs decoding is not a token.
inline std::string QueryValue(const std::string& target, const char* name) {
    const size_t q = target.find('?');
    if (q == std::string::npos) return "";
    size_t i = q + 1;
    const std::string want = std::string(name) + "=";
    while (i <= target.size()) {
        size_t amp = target.find('&', i);
        if (amp == std::string::npos) amp = target.size();
        const std::string pair = target.substr(i, amp - i);
        if (pair.compare(0, want.size(), want) == 0)
            return pair.substr(want.size());
        i = amp + 1;
    }
    return "";
}
} // namespace detail

// Parses the HTTP upgrade request at the front of buf. Never modifies
// buf: the caller erases `consumed` bytes, which matters because a
// client may send its first frame in the same packet as the handshake.
// The old code cleared the whole buffer and lost that frame.
//
// Header names are matched case-insensitively, as HTTP requires; the
// old code looked for "Sec-WebSocket-Key:" exactly and a client that
// sent "sec-websocket-key:" could not connect.
inline Handshake ParseHandshake(const std::string& buf) {
    Handshake h;
    const size_t end = buf.find("\r\n\r\n");
    if (end == std::string::npos) {
        if (buf.size() > kMaxHandshake) { h.state = Handshake::State::Bad; h.status = 431; }
        return h;
    }
    if (end + 4 > kMaxHandshake) { h.state = Handshake::State::Bad; h.status = 431; return h; }
    h.consumed = end + 4;

    // Request line: GET <target> HTTP/1.1
    const size_t lineEnd = buf.find("\r\n");
    const std::string line = buf.substr(0, lineEnd);
    if (line.compare(0, 4, "GET ") != 0) {
        h.state = Handshake::State::Bad; h.status = 400; return h;
    }
    const size_t sp = line.find(' ', 4);
    if (sp == std::string::npos) { h.state = Handshake::State::Bad; h.status = 400; return h; }
    const std::string target = line.substr(4, sp - 4);

    bool upgrade = false, version13 = false;
    size_t pos = lineEnd + 2;
    while (pos < end) {
        size_t eol = buf.find("\r\n", pos);
        if (eol == std::string::npos || eol > end) eol = end;
        const std::string hl = buf.substr(pos, eol - pos);
        pos = eol + 2;
        const size_t colon = hl.find(':');
        if (colon == std::string::npos) continue;
        const std::string name  = detail::Trim(hl.substr(0, colon));
        const std::string value = detail::Trim(hl.substr(colon + 1));
        if      (detail::IEquals(name, "Upgrade"))               upgrade   = detail::IContains(value, "websocket");
        else if (detail::IEquals(name, "Sec-WebSocket-Version")) version13 = (value == "13");
        else if (detail::IEquals(name, "Sec-WebSocket-Key"))     h.key     = value;
    }

    if (!upgrade)         { h.state = Handshake::State::Bad; h.status = 400; return h; }
    if (!version13)       { h.state = Handshake::State::Bad; h.status = 426; return h; }
    // A key is 16 random bytes base64-encoded: always 24 characters.
    if (h.key.size() != 24) { h.state = Handshake::State::Bad; h.status = 400; return h; }

    h.token = detail::QueryValue(target, "token");
    h.state = Handshake::State::Ok;
    return h;
}

// ── The token ────────────────────────────────────────────────
//
// Constant time in the length of what was supplied, so the time a
// rejection takes says nothing about how much of the token was right.
// An empty expected token matches NOTHING: a server that was never
// given a token must refuse everyone, not let everyone in.
inline bool TokenEquals(const std::string& expected, const std::string& got) {
    if (expected.empty()) return false;
    // A length mismatch is a mismatch. Folded in as a flag rather than
    // XOR-ing the sizes into an unsigned, which would truncate size_t.
    unsigned diff = (expected.size() != got.size()) ? 1u : 0u;
    const size_t n = got.size() > expected.size() ? got.size() : expected.size();
    for (size_t i = 0; i < n; i++) {
        const unsigned char a = i < expected.size() ? static_cast<unsigned char>(expected[i]) : 0;
        const unsigned char b = i < got.size()      ? static_cast<unsigned char>(got[i])      : 0;
        diff |= static_cast<unsigned>(a ^ b);
    }
    return diff == 0;
}

// A token from rig.ini is used only if it could have come from the
// generator: at least 32 hex characters (128 bits). Anything weaker --
// a hand-typed "1234" -- is refused rather than trusted, and the server
// then admits nobody until it is fixed. Hex only also keeps it safe in
// a URL query string with no escaping.
constexpr size_t kMinTokenHex = 32;
inline bool TokenLooksStrong(const std::string& t) {
    if (t.size() < kMinTokenHex) return false;
    for (char c : t) {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    return true;
}

// ── LAN peers without a token (owner's decision, 2026-10-25) ─
//
// With WsLan and WsLanOpen set in rig.ini, a client on the home
// network may connect without the token: the owner judged the network
// trusted and wanted any device on it -- Leoben on Elminster, a
// tablet -- to reach the dashboard with nothing to type. The cost was
// stated when it was chosen: any web page open in a browser on that
// network can then send mount commands.
//
// What stays guarded, at no cost to that decision:
//   - LOOPBACK still needs the token. A page in a browser on the
//     capture PC itself is the case P5.6 was about, and [DASH] supplies
//     the token there anyway.
//   - Only PRIVATE addresses are let in: 10/8, 172.16/12, 192.168/16
//     and link-local 169.254/16. A public address -- the port forwarded
//     by mistake, or a router doing something unexpected -- still needs
//     the token.
//
// `ipv4` is in host byte order: 192.168.1.17 is 0xC0A80111.
inline bool IsPrivateLanPeer(uint32_t ipv4) {
    const uint32_t a = ipv4 >> 24, b = (ipv4 >> 16) & 0xFF;
    if (a == 10) return true;
    if (a == 172 && b >= 16 && b <= 31) return true;
    if (a == 192 && b == 168) return true;
    if (a == 169 && b == 254) return true;
    return false;
}

// Whether a client may skip the token. The token itself is checked
// first and separately by the server; this is only the exemption.
inline bool PeerExemptFromToken(bool lanOpen, uint32_t peerIpv4) {
    return lanOpen && IsPrivateLanPeer(peerIpv4);
}

// ── B3: the preview subscription ─────────────────────────────
//
// The JPEG preview goes only to clients that ask for it, so a client
// that does not know about binary messages -- apex_bridge.py, say -- is
// never sent one. A client asks with {"cmd":"preview","arg":"on"} and
// stops with "off". The server answers this itself; it is not a rig
// command and never reaches the command queue.
//
// Whitespace is ignored, since JSON allows it anywhere between tokens
// and none of these four strings contains a space. Anything else is
// not a subscription and goes on to the command handler as before.
enum class PreviewRequest { None, On, Off };

inline PreviewRequest ParsePreviewRequest(const std::string& msg) {
    if (msg.size() > 128) return PreviewRequest::None;
    std::string s;
    for (char c : msg)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') s += c;
    if (s == R"({"cmd":"preview","arg":"on"})"  || s == R"({"arg":"on","cmd":"preview"})")
        return PreviewRequest::On;
    if (s == R"({"cmd":"preview","arg":"off"})" || s == R"({"arg":"off","cmd":"preview"})")
        return PreviewRequest::Off;
    return PreviewRequest::None;
}

} // namespace raduk::net::ws
