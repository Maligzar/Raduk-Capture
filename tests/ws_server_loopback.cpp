// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// ============================================================
//  The real WebSocket server, over a real loopback socket
//
//  P5.6. ws_protocol and ws_fuzz test the parser; this tests that the
//  server actually USES it -- that a missing token is refused before
//  anything is dispatched, that the right one is let in, and that the
//  two defects the old server had at the socket level are gone: a
//  fragmented command dispatched as its first fragment, and a frame
//  that arrived with the handshake thrown away.
//
//  Loopback only, on a port the app does not use, so it runs on the
//  bench beside a running RadukCapture.
// ============================================================

#include "net/WebSocketServer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace raduk::net;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-60s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static const uint16_t kPort = 18765;

static SOCKET Dial() {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(kPort);
    if (connect(s, (sockaddr*)&a, sizeof(a)) != 0) { closesocket(s); return INVALID_SOCKET; }
    DWORD tmo = 3000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tmo, sizeof(tmo));
    return s;
}

static void SendAll(SOCKET s, const std::string& b) {
    send(s, b.data(), (int)b.size(), 0);
}

// Reads until the HTTP response's blank line, or until the peer closes.
static std::string ReadResponse(SOCKET s) {
    std::string r;
    char buf[1024];
    while (r.find("\r\n\r\n") == std::string::npos) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;
        r.append(buf, n);
    }
    return r;
}

static std::string Request(const std::string& target) {
    return "GET " + target + " HTTP/1.1\r\n"
           "Host: localhost\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
           "Sec-WebSocket-Version: 13\r\n\r\n";
}

static std::string Frame(bool fin, unsigned op, const std::string& p) {
    std::string f;
    f += char((fin ? 0x80 : 0) | op);
    f += char(0x80 | p.size());                  // tests keep payloads < 126
    const char key[4] = { 0x12, 0x34, 0x56, 0x78 };
    f.append(key, 4);
    for (size_t i = 0; i < p.size(); i++) f += char(p[i] ^ key[i & 3]);
    return f;
}

int main() {
    printf("\n=== WebSocket server over loopback ===\n");

    const std::string token = GenerateWsToken();
    Check(token.size() == 64, "the generator gives 64 hex characters", std::to_string(token.size()));
    Check(GenerateWsToken() != token, "  and a different one each time");

    std::mutex mu;
    std::vector<std::string> got;
    auto received = [&]() { std::lock_guard<std::mutex> lk(mu); return got; };
    auto waitFor = [&](size_t n) {
        for (int i = 0; i < 40 && received().size() < n; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return received();
    };

    WebSocketServer srv;
    srv.SetRequiredToken(token);
    srv.SetCommandCallback([&](const std::string& m) {
        std::lock_guard<std::mutex> lk(mu);
        got.push_back(m);
    });
    if (!srv.Start(kPort, false)) {
        printf("  could not bind port %u -- something else holds it\n", kPort);
        return 1;
    }

    printf("\n1. Who is let in\n");
    {
        SOCKET s = Dial();
        SendAll(s, Request("/") + Frame(true, 1, R"({"cmd":"mount_stop"})"));
        const std::string r = ReadResponse(s);
        Check(r.rfind("HTTP/1.1 401", 0) == 0, "no token: 401, before anything else", r.substr(0, 24));
        closesocket(s);
    }
    {
        SOCKET s = Dial();
        SendAll(s, Request("/?token=" + std::string(64, '0')));
        const std::string r = ReadResponse(s);
        Check(r.rfind("HTTP/1.1 401", 0) == 0, "a wrong token of the right length: 401");
        closesocket(s);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    Check(received().empty(), "and nothing a refused client sent was dispatched");

    printf("\n2. The right token, and what the old server lost\n");
    SOCKET s = Dial();
    {
        // The first command rides in the same write as the handshake.
        SendAll(s, Request("/?token=" + token) + Frame(true, 1, R"({"cmd":"first"})"));
        const std::string r = ReadResponse(s);
        Check(r.rfind("HTTP/1.1 101", 0) == 0, "the right token: 101 Switching Protocols");
        Check(r.find("Access-Control-Allow-Origin") == std::string::npos,
              "  with no Access-Control-Allow-Origin: *");
        auto g = waitFor(1);
        Check(g.size() == 1 && g[0] == R"({"cmd":"first"})",
              "a frame sent with the handshake is not thrown away");
    }
    {
        SendAll(s, Frame(false, 1, R"({"cmd":)"));
        SendAll(s, Frame(true,  0, R"("second"})"));
        auto g = waitFor(2);
        Check(g.size() == 2 && g[1] == R"({"cmd":"second"})",
              "a fragmented command is dispatched once, whole",
              g.size() >= 2 ? g[1] : "(nothing)");
    }
    {
        SendAll(s, Frame(true, 9, "echo me"));
        char buf[64] = {};
        int n = recv(s, buf, sizeof(buf), 0);
        Check(n == 9 && (unsigned char)buf[0] == 0x8A && buf[1] == 7 &&
              std::string(buf + 2, 7) == "echo me",
              "a ping gets its own payload back");
    }
    {
        // An unmasked frame: the connection must be dropped.
        std::string bad;
        bad += char(0x81); bad += char(5); bad += "hello";
        SendAll(s, bad);
        char buf[16];
        int n = recv(s, buf, sizeof(buf), 0);
        Check(n <= 0, "an unmasked frame drops the connection");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        Check(received().size() == 2, "  and is not dispatched");
    }
    closesocket(s);

    printf("\n3. B3: the preview goes only to a client that asked for it\n");
    {
        auto open = [&]() {
            SOCKET c = Dial();
            SendAll(c, Request("/?token=" + token));
            ReadResponse(c);
            return c;
        };
        SOCKET a = open();
        SOCKET b = open();
        SendAll(a, Frame(true, 1, R"({"cmd":"preview","arg":"on"})"));
        for (int i = 0; i < 40 && srv.PreviewSubscriberCount() != 1; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        Check(srv.PreviewSubscriberCount() == 1, "one client subscribed");
        Check(received().size() == 2, "  and the subscription is not dispatched as a rig command");

        std::vector<uint8_t> jpeg(300);
        for (size_t i = 0; i < jpeg.size(); i++) jpeg[i] = (uint8_t)(i * 7);
        Check(srv.BroadcastPreview(jpeg) == 1, "BroadcastPreview reaches exactly one client");

        std::string got;
        char buf[512];
        while (got.size() < 4 + jpeg.size()) {
            int n = recv(a, buf, sizeof(buf), 0);
            if (n <= 0) break;
            got.append(buf, n);
        }
        const bool header = got.size() >= 4 && (unsigned char)got[0] == 0x82 &&
                            (unsigned char)got[1] == 126 &&
                            (((unsigned char)got[2] << 8) | (unsigned char)got[3]) == 300;
        Check(header, "  as one binary frame of the right length");
        Check(got.size() == 4 + jpeg.size() &&
              std::equal(jpeg.begin(), jpeg.end(), got.begin() + 4,
                         [](uint8_t x, char y) { return x == (uint8_t)y; }),
              "  carrying the bytes unchanged");

        DWORD shortTmo = 300;
        setsockopt(b, SOL_SOCKET, SO_RCVTIMEO, (const char*)&shortTmo, sizeof(shortTmo));
        Check(recv(b, buf, sizeof(buf), 0) <= 0, "the client that did not ask gets nothing");

        SendAll(a, Frame(true, 1, R"({ "cmd" : "preview", "arg" : "off" })"));
        for (int i = 0; i < 40 && srv.PreviewSubscriberCount() != 0; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        Check(srv.PreviewSubscriberCount() == 0, "\"off\" unsubscribes, whitespace and all");
        Check(srv.BroadcastPreview(jpeg) == 0, "  and nothing more is sent");
        closesocket(a);
        closesocket(b);
    }

    printf("\n4. A server never given a token admits nobody\n");
    srv.Stop();
    WebSocketServer bare;
    bare.Start(kPort, false);
    {
        SOCKET t = Dial();
        SendAll(t, Request("/?token="));
        Check(ReadResponse(t).rfind("HTTP/1.1 401", 0) == 0, "empty token against empty config: 401");
        closesocket(t);
    }
    bare.Stop();

    printf("\n5. WsLanOpen does not open the door to this PC's own browser\n");
    {
        WebSocketServer lan;
        lan.SetRequiredToken(token);
        lan.SetLanOpen(true);
        lan.Start(kPort, false);
        SOCKET t = Dial();
        SendAll(t, Request("/"));
        Check(ReadResponse(t).rfind("HTTP/1.1 401", 0) == 0,
              "LAN-open server, loopback client, no token: still 401");
        closesocket(t);
        SOCKET u = Dial();
        SendAll(u, Request("/?token=" + token));
        Check(ReadResponse(u).rfind("HTTP/1.1 101", 0) == 0, "  and with the token: 101");
        closesocket(u);
        lan.Stop();
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
