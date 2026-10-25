#pragma once
// ============================================================
// WebSocketServer.h — Phase 4: APEX telemetry broadcast
// Minimal RFC 6455 WebSocket server built on Winsock2.
// No external dependencies — pure Win32.
// ============================================================

// CRITICAL: winsock2.h must come before windows.h.
// WIN32_LEAN_AND_MEAN prevents windows.h from pulling in winsock.h (v1),
// which would cause struct redefinition errors against winsock2.h.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <sstream>

#include "WsProtocol.h"

#pragma comment(lib, "ws2_32.lib")

namespace raduk::net {

// ── SHA-1 for WebSocket handshake (RFC 6455 §4.2.2) ──────────
// Minimal self-contained implementation — only used once per
// client connection to compute the Sec-WebSocket-Accept header.
namespace sha1 {
    void compute(const uint8_t* data, size_t len, uint8_t out[20]);
}

// ── Base64 encode ─────────────────────────────────────────────
std::string b64encode(const uint8_t* in, size_t len);

// ── WebSocket frame builders ──────────────────────────────────
std::vector<uint8_t> buildTextFrame(const std::string& payload);
std::vector<uint8_t> buildBinaryFrame(const std::vector<uint8_t>& payload);
std::vector<uint8_t> buildCloseFrame();
std::vector<uint8_t> buildPongFrame(const std::string& payload);

// P5.6: 32 bytes from the OS's cryptographic generator, as 64 lowercase
// hex characters. Empty if the generator fails -- and an empty token
// admits nobody, so a failure here fails closed.
std::string GenerateWsToken();

// ── Client state ──────────────────────────────────────────────
struct WsClient {
    SOCKET  sock    = INVALID_SOCKET;
    bool    closed  = false;
    std::string recvBuf;  // partial HTTP or WS data
    bool    upgraded = false;
    ULONGLONG acceptedMs = 0;     // for the handshake deadline
    ws::FrameParser parser;       // carries fragment state between reads
    bool    wantsPreview = false; // B3: asked for the JPEG preview
};

// ── WebSocketServer ───────────────────────────────────────────

class WebSocketServer {
public:
    WebSocketServer() = default;
    ~WebSocketServer() { Stop(); }

    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;

    // Called with JSON string for each incoming command from a client
    void SetCommandCallback(std::function<void(const std::string&)> cb) {
        m_onCommand = cb;
    }

    // Phase 57 (review S1) — hardening limits. The command set drives the
    // mount and focuser, so the attack surface matters:
    //   MAX_WS_PAYLOAD  clamps the 64-bit wire length before any arithmetic
    //                   (a hostile frame could integer-wrap totalLen or
    //                   terminate the app via a huge resize).
    //   MAX_RECV_BUF    caps per-client receive-buffer growth (memory DoS
    //                   from a header that promises data that never comes).
    static constexpr uint64_t MAX_WS_PAYLOAD = 1  * 1024 * 1024;
    static constexpr size_t   MAX_RECV_BUF   = 2  * 1024 * 1024;

    // P5.6 -- more limits, for the same reason:
    //   MAX_CLIENTS        select() on Windows watches at most 64 sockets,
    //                      and nobody runs more than a few dashboards.
    //   HANDSHAKE_DEADLINE a connection that never finishes its upgrade
    //                      would otherwise hold a slot forever.
    static constexpr size_t    MAX_CLIENTS           = 16;
    static constexpr ULONGLONG HANDSHAKE_DEADLINE_MS = 10000;

    // P5.6: the shared secret every client must present as ?token= in
    // its upgrade request. Set it BEFORE Start(); the server thread reads
    // it without a lock. Empty refuses everyone -- that is deliberate.
    //
    // Loopback binding alone was never enough. Browsers do not apply the
    // same-origin policy to WebSockets, so any web page the observer has
    // open could connect to ws://localhost:8765 and slew the mount
    // (cross-site WebSocket hijacking). The token is what a hostile page
    // cannot know.
    void SetRequiredToken(const std::string& token) { m_token = token; }

    // Binds 127.0.0.1 by default. Set bindAny=true only on a trusted LAN
    // (remote APEX dashboard); the token is required either way.
    bool Start(uint16_t port = 8765, bool bindAny = false);

    void Stop();

    // Broadcast a JSON string to all connected, upgraded clients
    void Broadcast(const std::string& json);

    // B3: one JPEG, as a binary message, to the clients that asked for
    // the preview (ws::ParsePreviewRequest). A client whose socket is
    // not ready to take more is SKIPPED for this picture rather than
    // waited on -- a browser that has stopped reading costs it pictures,
    // never the caller time. Returns how many clients it went to.
    int BroadcastPreview(const std::vector<uint8_t>& jpeg);

    int ClientCount() const;
    int PreviewSubscriberCount() const;

    // Every send() on a client socket gives up after this long, and a
    // client whose send times out is dropped: a half-sent frame has
    // broken its stream. Before B3 a client that stopped reading could
    // block Broadcast() -- and the render loop that calls it -- for good.
    //
    // Sends happen under m_clientMutex, which also serialises them --
    // two threads writing one socket at once would interleave frames --
    // so this is also the most a stalled client can delay the server
    // thread reading commands, STOP included. Short on purpose; the
    // mount's own watchdog allows 8 s.
    static constexpr DWORD SEND_TIMEOUT_MS = 500;

    bool IsRunning() const { return m_running.load(); }

private:
    void Run();

    // ── HTTP upgrade handshake (RFC 6455 §4.2) ────────────────
    void HandleHandshake(WsClient& c);

    // ── WebSocket frame parser (client→server, masked per RFC 6455) ──
    void HandleWsData(WsClient& c);

    void PurgeClosedClients();

    SOCKET                              m_listenSock = INVALID_SOCKET;
    uint16_t                            m_port       = 8765;
    std::thread                         m_thread;
    std::atomic<bool>                   m_running{false};
    mutable std::mutex                  m_clientMutex;
    std::vector<WsClient>               m_clients;
    std::function<void(const std::string&)> m_onCommand;
    std::string                         m_token;
};

} // namespace raduk::net
