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
    static void compute(const uint8_t* data, size_t len, uint8_t out[20]) {
        uint32_t h0=0x67452301,h1=0xEFCDAB89,h2=0x98BADCFE,h3=0x10325476,h4=0xC3D2E1F0;
        // Pad message
        size_t padLen = ((len + 9 + 63) / 64) * 64;
        std::vector<uint8_t> msg(padLen, 0);
        memcpy(msg.data(), data, len);
        msg[len] = 0x80;
        uint64_t bitLen = (uint64_t)len * 8;
        for (int i = 0; i < 8; i++)
            msg[padLen-8+i] = (uint8_t)(bitLen >> (56-8*i));
        // Process blocks
        for (size_t i = 0; i < padLen; i += 64) {
            uint32_t w[80];
            for (int j = 0; j < 16; j++)
                w[j] = ((uint32_t)msg[i+j*4]<<24)|((uint32_t)msg[i+j*4+1]<<16)|
                        ((uint32_t)msg[i+j*4+2]<<8)|(uint32_t)msg[i+j*4+3];
            for (int j = 16; j < 80; j++) {
                uint32_t v=w[j-3]^w[j-8]^w[j-14]^w[j-16];
                w[j]=(v<<1)|(v>>31);
            }
            uint32_t a=h0,b=h1,c=h2,d=h3,e=h4;
            for (int j = 0; j < 80; j++) {
                uint32_t f,k;
                if      (j<20){f=(b&c)|((~b)&d);k=0x5A827999;}
                else if (j<40){f=b^c^d;          k=0x6ED9EBA1;}
                else if (j<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDC;}
                else          {f=b^c^d;          k=0xCA62C1D6;}
                uint32_t tmp=((a<<5)|(a>>27))+f+e+k+w[j];
                e=d;d=c;c=(b<<30)|(b>>2);b=a;a=tmp;
            }
            h0+=a;h1+=b;h2+=c;h3+=d;h4+=e;
        }
        uint32_t hh[5]={h0,h1,h2,h3,h4};
        for (int i=0;i<5;i++) for (int j=3;j>=0;j--)
            out[i*4+(3-j)]=(uint8_t)(hh[i]>>(j*8));
    }
}

// ── Base64 encode ─────────────────────────────────────────────
static std::string b64encode(const uint8_t* in, size_t len) {
    static const char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i=0;i<len;i+=3) {
        uint32_t v=((uint32_t)in[i]<<16)|((i+1<len)?(uint32_t)in[i+1]<<8:0)|((i+2<len)?(uint32_t)in[i+2]:0);
        out+=t[(v>>18)&63]; out+=t[(v>>12)&63];
        out+=(i+1<len)?t[(v>>6)&63]:'=';
        out+=(i+2<len)?t[v&63]:'=';
    }
    return out;
}

// ── WebSocket frame builder ───────────────────────────────────
static std::vector<uint8_t> buildTextFrame(const std::string& payload) {
    std::vector<uint8_t> frame;
    frame.push_back(0x81); // FIN + opcode text
    size_t len = payload.size();
    if (len <= 125) {
        frame.push_back((uint8_t)len);
    } else if (len <= 65535) {
        frame.push_back(126);
        frame.push_back((uint8_t)(len>>8));
        frame.push_back((uint8_t)(len&0xFF));
    } else {
        frame.push_back(127);
        for (int i=7;i>=0;i--) frame.push_back((uint8_t)(len>>(i*8)));
    }
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

static std::vector<uint8_t> buildCloseFrame() {
    return {0x88, 0x00}; // FIN + close opcode, no payload
}

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

    bool Start(uint16_t port = 8765) {
        if (m_running.load()) return true;

        WSADATA wd;
        if (WSAStartup(MAKEWORD(2,2), &wd) != 0) {
            printf("[WSServer] WSAStartup failed\n");
            return false;
        }

        m_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listenSock == INVALID_SOCKET) {
            printf("[WSServer] socket() failed: %d\n", WSAGetLastError());
            WSACleanup(); return false;
        }

        // Allow rapid restart without TIME_WAIT
        int opt = 1;
        setsockopt(m_listenSock, SOL_SOCKET, SO_REUSEADDR,
                   (char*)&opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = htons(port);

        if (bind(m_listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            printf("[WSServer] bind() failed: %d\n", WSAGetLastError());
            closesocket(m_listenSock); WSACleanup(); return false;
        }

        listen(m_listenSock, SOMAXCONN);
        m_port    = port;
        m_running.store(true);
        m_thread  = std::thread(&WebSocketServer::Run, this);
        printf("[WSServer] Listening on :%d\n", port);
        return true;
    }

    void Stop() {
        if (!m_running.load()) return;
        m_running.store(false);

        // Unblock accept() by closing the listen socket
        if (m_listenSock != INVALID_SOCKET) {
            closesocket(m_listenSock);
            m_listenSock = INVALID_SOCKET;
        }

        if (m_thread.joinable()) m_thread.join();

        // Close all clients
        {
            std::lock_guard<std::mutex> lk(m_clientMutex);
            for (auto& c : m_clients) {
                if (c.sock != INVALID_SOCKET) closesocket(c.sock);
            }
            m_clients.clear();
        }

        WSACleanup();
        printf("[WSServer] Stopped\n");
    }

    // Broadcast a JSON string to all connected, upgraded clients
    void Broadcast(const std::string& json) {
        if (!m_running.load() || json.empty()) return;
        auto frame = buildTextFrame(json);

        std::lock_guard<std::mutex> lk(m_clientMutex);
        for (auto& c : m_clients) {
            if (!c.upgraded || c.closed) continue;
            int sent = send(c.sock, (char*)frame.data(), (int)frame.size(), 0);
            if (sent == SOCKET_ERROR) {
                c.closed = true;
            }
        }
    }

    int ClientCount() const {
        std::lock_guard<std::mutex> lk(m_clientMutex);
        int n = 0;
        for (const auto& c : m_clients) if (!c.closed && c.upgraded) n++;
        return n;
    }

    bool IsRunning() const { return m_running.load(); }

private:
    void Run() {
        while (m_running.load()) {
            // Build fd_set from listen socket + all client sockets
            fd_set readSet;
            FD_ZERO(&readSet);

            if (m_listenSock != INVALID_SOCKET)
                FD_SET(m_listenSock, &readSet);

            {
                std::lock_guard<std::mutex> lk(m_clientMutex);
                for (auto& c : m_clients)
                    if (!c.closed && c.sock != INVALID_SOCKET)
                        FD_SET(c.sock, &readSet);
            }

            timeval tv{1, 0}; // 1 second timeout — lets us check m_running
            int sel = select(0, &readSet, nullptr, nullptr, &tv);
            if (sel == SOCKET_ERROR) break;
            if (sel == 0) {
                PurgeClosedClients();
                continue;
            }

            // New connection?
            if (m_listenSock != INVALID_SOCKET &&
                FD_ISSET(m_listenSock, &readSet)) {
                SOCKET cs = accept(m_listenSock, nullptr, nullptr);
                if (cs != INVALID_SOCKET) {
                    std::lock_guard<std::mutex> lk(m_clientMutex);
                    WsClient wc; wc.sock = cs;
                    m_clients.push_back(std::move(wc));
                    printf("[WSServer] Client connected (%d total)\n",
                           (int)m_clients.size());
                }
            }

            // Data on client sockets
            {
                std::lock_guard<std::mutex> lk(m_clientMutex);
                for (auto& c : m_clients) {
                    if (c.closed || c.sock == INVALID_SOCKET) continue;
                    if (!FD_ISSET(c.sock, &readSet)) continue;

                    char buf[4096];
                    int n = recv(c.sock, buf, sizeof(buf)-1, 0);
                    if (n <= 0) { c.closed = true; continue; }
                    buf[n] = 0;
                    c.recvBuf.append(buf, n);

                    if (!c.upgraded) {
                        HandleHandshake(c);
                    } else {
                        HandleWsData(c);
                    }
                }
            }

            PurgeClosedClients();
        }
    }

    // ── HTTP upgrade handshake (RFC 6455 §4.2) ────────────────
    void HandleHandshake(WsClient& c) {
        // Wait for double CRLF (end of HTTP headers)
        if (c.recvBuf.find("\r\n\r\n") == std::string::npos) return;

        // Extract Sec-WebSocket-Key
        std::string key;
        size_t pos = c.recvBuf.find("Sec-WebSocket-Key:");
        if (pos == std::string::npos) { c.closed = true; return; }
        pos += 18;
        while (pos < c.recvBuf.size() && c.recvBuf[pos] == ' ') pos++;
        size_t end = c.recvBuf.find("\r\n", pos);
        if (end == std::string::npos) { c.closed = true; return; }
        key = c.recvBuf.substr(pos, end - pos);

        // Compute accept value: SHA1(key + GUID) | base64
        static const char GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        std::string combined = key + GUID;
        uint8_t hash[20];
        sha1::compute((const uint8_t*)combined.data(), combined.size(), hash);
        std::string accept = b64encode(hash, 20);

        // Send 101 Switching Protocols
        // Include Access-Control-Allow-Origin so browsers can connect from
        // any origin (file://, Elminster IP, iPhone, etc.)
        std::string response =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Sec-WebSocket-Accept: " + accept + "\r\n"
            "\r\n";

        send(c.sock, response.data(), (int)response.size(), 0);
        c.upgraded = true;
        c.recvBuf.clear();
        printf("[WSServer] Client upgraded to WebSocket\n");
    }

    // ── WebSocket frame parser (client→server, masked per RFC 6455) ──
    void HandleWsData(WsClient& c) {
        while (c.recvBuf.size() >= 2) {
            const uint8_t* d = (const uint8_t*)c.recvBuf.data();
            uint8_t  opcode = d[0] & 0x0F;
            bool     masked = (d[1] & 0x80) != 0;
            uint64_t payLen = d[1] & 0x7F;
            size_t   hdrLen = 2;

            if (payLen == 126) {
                if (c.recvBuf.size() < 4) break;
                payLen = ((uint64_t)d[2]<<8)|d[3]; hdrLen = 4;
            } else if (payLen == 127) {
                if (c.recvBuf.size() < 10) break;
                payLen = 0;
                for (int i=0;i<8;i++) payLen=(payLen<<8)|d[2+i];
                hdrLen = 10;
            }

            size_t maskLen = masked ? 4 : 0;
            size_t totalLen = hdrLen + maskLen + payLen;
            if (c.recvBuf.size() < totalLen) break;

            if (opcode == 0x8) { // Close
                auto f = buildCloseFrame();
                send(c.sock, (char*)f.data(), (int)f.size(), 0);
                c.closed = true; break;
            }

            if (opcode == 0x9) { // Ping → Pong
                uint8_t pong[2] = {0x8A, 0x00};
                send(c.sock, (char*)pong, 2, 0);
            }

            if (opcode == 0x1 || opcode == 0x2) { // Text or Binary
                std::string payload;
                payload.resize(payLen);
                const uint8_t* maskKey = d + hdrLen;
                const uint8_t* payData = d + hdrLen + maskLen;
                for (uint64_t i = 0; i < payLen; i++)
                    payload[i] = (char)(payData[i] ^ (masked ? maskKey[i&3] : 0));

                // Dispatch command
                if (m_onCommand) m_onCommand(payload);
            }

            c.recvBuf.erase(0, totalLen);
        }
    }

    void PurgeClosedClients() {
        std::lock_guard<std::mutex> lk(m_clientMutex);
        for (auto it = m_clients.begin(); it != m_clients.end(); ) {
            if (it->closed) {
                if (it->sock != INVALID_SOCKET) {
                    closesocket(it->sock);
                    it->sock = INVALID_SOCKET;
                }
                it = m_clients.erase(it);
                printf("[WSServer] Client disconnected\n");
            } else {
                ++it;
            }
        }
    }

    SOCKET                              m_listenSock = INVALID_SOCKET;
    uint16_t                            m_port       = 8765;
    std::thread                         m_thread;
    std::atomic<bool>                   m_running{false};
    mutable std::mutex                  m_clientMutex;
    std::vector<WsClient>               m_clients;
    std::function<void(const std::string&)> m_onCommand;
};

} // namespace raduk::net
