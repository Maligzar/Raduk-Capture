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
std::vector<uint8_t> buildCloseFrame();

// ── Client state ──────────────────────────────────────────────
struct WsClient {
    SOCKET  sock    = INVALID_SOCKET;
    bool    closed  = false;
    std::string recvBuf;  // partial HTTP or WS data
    bool    upgraded = false;
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

    // Binds 127.0.0.1 by default — the command set has NO authentication.
    // Set bindAny=true only on a trusted LAN (remote APEX dashboard).
    bool Start(uint16_t port = 8765, bool bindAny = false);

    void Stop();

    // Broadcast a JSON string to all connected, upgraded clients
    void Broadcast(const std::string& json);

    int ClientCount() const;

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
};

} // namespace raduk::net
