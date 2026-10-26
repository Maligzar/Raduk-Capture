// ============================================================
// WebSocketServer.cpp — out-of-line definitions for WebSocketServer.h
// (SHA-1 / base64 / frame helpers live here so every TU that includes
// the header does not get its own copy.)
// ============================================================
#include "WebSocketServer.h"

#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

namespace raduk::net {

std::string GenerateWsToken() {
    uint8_t bytes[32];
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        return "";
    static const char hex[] = "0123456789abcdef";
    std::string t;
    for (uint8_t b : bytes) { t += hex[b >> 4]; t += hex[b & 0xF]; }
    SecureZeroMemory(bytes, sizeof(bytes));
    return t;
}

// ── SHA-1 for WebSocket handshake (RFC 6455 §4.2.2) ──────────
// Minimal self-contained implementation — only used once per
// client connection to compute the Sec-WebSocket-Accept header.
namespace sha1 {
    void compute(const uint8_t* data, size_t len, uint8_t out[20]) {
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
std::string b64encode(const uint8_t* in, size_t len) {
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
namespace {
std::vector<uint8_t> buildDataFrame(uint8_t first, const uint8_t* p, size_t len) {
    std::vector<uint8_t> frame;
    frame.reserve(len + 10);
    frame.push_back(first);
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
    frame.insert(frame.end(), p, p + len);
    return frame;
}
} // namespace

std::vector<uint8_t> buildBinaryFrame(const std::vector<uint8_t>& payload) {
    return buildDataFrame(0x82, payload.data(), payload.size());   // FIN + binary
}

std::vector<uint8_t> buildTextFrame(const std::string& payload) {
    return buildDataFrame(0x81,                                    // FIN + text
                          reinterpret_cast<const uint8_t*>(payload.data()),
                          payload.size());
}

std::vector<uint8_t> buildCloseFrame() {
    return {0x88, 0x00}; // FIN + close opcode, no payload
}

// RFC 6455 5.5.3: a pong carries the ping's payload back. The parser has
// already held a ping to 125 bytes, so the 7-bit length always fits.
std::vector<uint8_t> buildPongFrame(const std::string& payload) {
    std::vector<uint8_t> frame;
    frame.push_back(0x8A);                   // FIN + pong
    frame.push_back((uint8_t)payload.size());
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

namespace {
// The only answers a refused handshake gets. Fixed strings: nothing the
// client sent is echoed back.
const char* StatusLine(int status) {
    switch (status) {
    case 401: return "HTTP/1.1 401 Unauthorized\r\n";
    case 426: return "HTTP/1.1 426 Upgrade Required\r\nSec-WebSocket-Version: 13\r\n";
    case 431: return "HTTP/1.1 431 Request Header Fields Too Large\r\n";
    case 503: return "HTTP/1.1 503 Service Unavailable\r\n";
    default:  return "HTTP/1.1 400 Bad Request\r\n";
    }
}

void Refuse(SOCKET s, int status) {
    std::string r = StatusLine(status);
    r += "Content-Length: 0\r\nConnection: close\r\n\r\n";
    send(s, r.data(), (int)r.size(), 0);
}
} // namespace

bool WebSocketServer::Start(uint16_t port, bool bindAny) {
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
    addr.sin_addr.s_addr = bindAny ? INADDR_ANY : htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(port);

    if (bind(m_listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("[WSServer] bind() failed: %d\n", WSAGetLastError());
        closesocket(m_listenSock); WSACleanup(); return false;
    }

    listen(m_listenSock, SOMAXCONN);
    m_port    = port;
    m_running.store(true);
    m_thread  = std::thread(&WebSocketServer::Run, this);
    printf("[WSServer] Listening on %s:%d\n",
           bindAny ? "0.0.0.0" : "127.0.0.1", port);
    return true;
}

void WebSocketServer::Stop() {
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

void WebSocketServer::Broadcast(const std::string& json) {
    if (!m_running.load() || json.empty()) return;
    auto frame = buildTextFrame(json);

    std::lock_guard<std::mutex> lk(m_clientMutex);
    for (auto& c : m_clients) {
        if (!c.upgraded || c.closed) continue;
        int sent = send(c.sock, (char*)frame.data(), (int)frame.size(), 0);
        // Short, not just failed: with SO_SNDTIMEO a timed-out send can
        // leave part of a frame on the wire, and that stream is broken.
        if (sent != (int)frame.size()) {
            c.closed = true;
        }
    }
}

int WebSocketServer::ClientCount() const {
    std::lock_guard<std::mutex> lk(m_clientMutex);
    int n = 0;
    for (const auto& c : m_clients) if (!c.closed && c.upgraded) n++;
    return n;
}

int WebSocketServer::PreviewSubscriberCount() const {
    std::lock_guard<std::mutex> lk(m_clientMutex);
    int n = 0;
    for (const auto& c : m_clients) if (!c.closed && c.upgraded && c.wantsPreview) n++;
    return n;
}

namespace {
// Room in the socket's send buffer right now, without waiting. Not a
// promise that a whole frame fits -- the send timeout covers the rest --
// but it is what keeps a client that has stopped reading from costing
// a timeout on every picture.
bool WritableNow(SOCKET s) {
    fd_set w; FD_ZERO(&w); FD_SET(s, &w);
    timeval tv{0, 0};
    return select(0, nullptr, &w, nullptr, &tv) == 1;
}
} // namespace

int WebSocketServer::BroadcastPreview(const std::vector<uint8_t>& jpeg) {
    if (!m_running.load() || jpeg.empty()) return 0;
    const auto frame = buildBinaryFrame(jpeg);

    std::lock_guard<std::mutex> lk(m_clientMutex);
    int sentTo = 0;
    for (auto& c : m_clients) {
        if (!c.upgraded || c.closed || !c.wantsPreview) continue;
        if (!WritableNow(c.sock)) continue;          // busy: skip this picture
        const int sent = send(c.sock, (const char*)frame.data(), (int)frame.size(), 0);
        if (sent != (int)frame.size()) { c.closed = true; continue; }
        ++sentTo;
    }
    return sentTo;
}

void WebSocketServer::Run() {
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
            sockaddr_in peer{};
            int peerLen = sizeof(peer);
            SOCKET cs = accept(m_listenSock, (sockaddr*)&peer, &peerLen);
            if (cs != INVALID_SOCKET) {
                std::lock_guard<std::mutex> lk(m_clientMutex);
                if (m_clients.size() >= MAX_CLIENTS) {
                    Refuse(cs, 503);
                    closesocket(cs);
                    printf("[WSServer] Refused a connection: %zu clients already\n",
                           m_clients.size());
                } else {
                    const DWORD sendTimeout = SEND_TIMEOUT_MS;
                    setsockopt(cs, SOL_SOCKET, SO_SNDTIMEO,
                               (const char*)&sendTimeout, sizeof(sendTimeout));
                    WsClient wc; wc.sock = cs; wc.acceptedMs = GetTickCount64();
                    wc.peerIpv4 = (peer.sin_family == AF_INET)
                                ? ntohl(peer.sin_addr.s_addr) : 0;
                    m_clients.push_back(std::move(wc));
                    printf("[WSServer] Client connected (%d total)\n",
                           (int)m_clients.size());
                }
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
                // Phase 57 (S1): cap buffered bytes per client — a header
                // promising data that never arrives must not grow this
                // buffer forever.
                if (c.recvBuf.size() + (size_t)n > MAX_RECV_BUF) {
                    printf("[WSServer] Client exceeded recv cap — dropping\n");
                    c.closed = true;
                    continue;
                }
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

// P5.6: the parsing is ws::ParseHandshake's; this decides and answers.
// Never print the request line or the token -- the console is the
// session log, and the session log is read by the AI assistant.
void WebSocketServer::HandleHandshake(WsClient& c) {
    const ws::Handshake h = ws::ParseHandshake(c.recvBuf);
    if (h.state == ws::Handshake::State::Incomplete) return;
    if (h.state == ws::Handshake::State::Bad) {
        printf("[WSServer] Refused a malformed upgrade request (%d)\n", h.status);
        Refuse(c.sock, h.status);
        c.closed = true;
        return;
    }
    const bool tokenOk = ws::TokenEquals(m_token, h.token);
    const bool lanExempt = !tokenOk && ws::PeerExemptFromToken(m_lanOpen, c.peerIpv4);
    if (!tokenOk && !lanExempt) {
        printf("[WSServer] Refused a client: %s\n",
               m_token.empty() ? "no WsToken is configured, so nobody is admitted"
                               : "missing or wrong token");
        Refuse(c.sock, 401);
        c.closed = true;
        return;
    }
    if (lanExempt) {
        // The peer's address, which is the owner's own device on their
        // own network -- never the token, which this client did not send.
        printf("[WSServer] Admitted %u.%u.%u.%u without a token (home network, WsLanOpen)\n",
               c.peerIpv4 >> 24, (c.peerIpv4 >> 16) & 0xFF,
               (c.peerIpv4 >> 8) & 0xFF, c.peerIpv4 & 0xFF);
    }

    // Compute accept value: SHA1(key + GUID) | base64
    static const char GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = h.key + GUID;
    uint8_t hash[20];
    sha1::compute((const uint8_t*)combined.data(), combined.size(), hash);
    std::string accept = b64encode(hash, 20);

    // No Access-Control-Allow-Origin. It never did anything for a
    // WebSocket -- browsers do not run CORS on the upgrade -- and it read
    // as a statement that any origin was welcome.
    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n"
        "\r\n";

    send(c.sock, response.data(), (int)response.size(), 0);
    c.upgraded = true;
    // Only the request. A client may send its first frame in the same
    // packet; the old code cleared the whole buffer and lost it.
    c.recvBuf.erase(0, h.consumed);
    printf("[WSServer] Client upgraded to WebSocket\n");
    if (!c.recvBuf.empty()) HandleWsData(c);
}

void WebSocketServer::HandleWsData(WsClient& c) {
    ws::FeedResult r = c.parser.Feed(c.recvBuf);

    // Pongs first: they answer frames that arrived before anything the
    // parser may have refused after them.
    for (const auto& p : r.pings) {
        auto f = buildPongFrame(p);
        send(c.sock, (char*)f.data(), (int)f.size(), 0);
    }
    for (const auto& m : r.messages) {
        // B3: the preview subscription is the server's own business --
        // per client, and not a rig command.
        switch (ws::ParsePreviewRequest(m)) {
        case ws::PreviewRequest::On:  c.wantsPreview = true;  continue;
        case ws::PreviewRequest::Off: c.wantsPreview = false; continue;
        case ws::PreviewRequest::None: break;
        }
        if (m_onCommand) m_onCommand(m);
    }
    if (r.fail) {
        // r.why is one of the parser's own fixed strings, never peer text.
        printf("[WSServer] Protocol violation (%s) -- dropping client\n", r.why);
        c.closed = true;
        return;
    }
    if (r.close) {
        auto f = buildCloseFrame();
        send(c.sock, (char*)f.data(), (int)f.size(), 0);
        c.closed = true;
    }
}

void WebSocketServer::PurgeClosedClients() {
    std::lock_guard<std::mutex> lk(m_clientMutex);
    const ULONGLONG now = GetTickCount64();
    for (auto& c : m_clients) {
        if (!c.closed && !c.upgraded && now - c.acceptedMs > HANDSHAKE_DEADLINE_MS) {
            printf("[WSServer] Dropping a client that never finished its upgrade\n");
            c.closed = true;
        }
    }
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

} // namespace raduk::net
