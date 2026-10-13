#pragma once
// ============================================================
// AIAssistant.h — Phase 50: Ollama WinHTTP streaming client
//
// Sends chat messages to Ollama at localhost:11434/api/chat.
// Streams NDJSON response chunks on a background thread.
// Each content token fires the OnToken callback on that thread
// — caller must PostMessage to update the UI safely.
//
// Message format (Ollama /api/chat):
//   POST http://localhost:11434/api/chat
//   {"model":"llama3.2:3b","stream":true,"messages":[...]}
//
// Response: newline-delimited JSON objects:
//   {"message":{"role":"assistant","content":"Hello"},"done":false}
//   {"done":true}
// ============================================================

#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <sstream>
#include <cstdio>

#pragma comment(lib, "winhttp.lib")

namespace raduk::core {

struct ChatMessage {
    std::string role;    // "system", "user", "assistant"
    std::string content;
};

class AIAssistant {
public:
    AIAssistant() = default;
    ~AIAssistant() { Cancel(); }

    // Callbacks — called from background thread, use PostMessage for UI updates
    void SetOnToken(std::function<void(const std::string&)> cb)  { m_onToken    = cb; }
    void SetOnDone (std::function<void()> cb)                    { m_onDone     = cb; }
    void SetOnError(std::function<void(const std::string&)> cb)  { m_onError    = cb; }
    // Failures also go to the log. Every error in here reached only the
    // chat panel, which is the one place you are not looking when the
    // answer is that nothing came back.
    void SetLogCallback(std::function<void(const std::string&)> cb)  { m_log        = cb; }

    bool IsBusy() const { return m_busy.load(); }

    // Send a message. systemPrompt is prepended as a "system" message.
    // history: prior turns. userMessage: latest input.
    // Returns false if already busy.
    bool Send(const std::string& systemPrompt,
              const std::vector<ChatMessage>& history,
              const std::string& userMessage) {
        if (m_busy.load()) return false;
        m_cancel.store(false);
        m_busy.store(true);

        // Build messages array
        std::vector<ChatMessage> messages;
        messages.push_back({"system", systemPrompt});
        for (const auto& m : history) messages.push_back(m);
        messages.push_back({"user", userMessage});

        // Build JSON body
        std::string body = BuildRequestBody(messages);

        // Launch background thread
        if (m_thread.joinable()) m_thread.detach();
        m_thread = std::thread([this, body]() { StreamThread(body); });
        return true;
    }

    // Cancel in-flight request
    void Cancel() {
        m_cancel.store(true);
        if (m_thread.joinable()) m_thread.detach();
        m_busy.store(false);
    }

    // Check if Ollama is reachable — quick HEAD request
    static bool IsOllamaRunning() {
        HINTERNET hSession = WinHttpOpen(L"RadukCapture/1.0",
            WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;

        HINTERNET hConnect = WinHttpConnect(hSession, L"localhost",
            11434, 0);
        bool ok = (hConnect != nullptr);
        if (hConnect) WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return ok;
    }

private:
    std::string BuildRequestBody(const std::vector<ChatMessage>& messages) {
        std::ostringstream ss;
        ss << "{\"model\":\"llama3.2:3b\",\"stream\":true,\"messages\":[";
        for (size_t i = 0; i < messages.size(); i++) {
            if (i > 0) ss << ",";
            ss << "{\"role\":\"" << JsonEscape(messages[i].role) << "\","
               << "\"content\":\"" << JsonEscape(messages[i].content) << "\"}";
        }
        ss << "]}";
        return ss.str();
    }

    // Simple JSON string escape
    static std::string JsonEscape(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if ((unsigned char)c < 0x20) {
                        char hex[8]; snprintf(hex, 8, "\\u%04x", (unsigned char)c);
                        out += hex;
                    } else out += c;
                    break;
            }
        }
        return out;
    }

    // Extract "content" field value from an Ollama NDJSON chunk.
    // Minimal parser — no full JSON lib needed for this simple structure.
    static std::string ExtractContent(const std::string& line) {
        // Look for "content":"<value>"
        const std::string key = "\"content\":\"";
        auto pos = line.find(key);
        if (pos == std::string::npos) return "";
        pos += key.size();

        std::string val;
        while (pos < line.size()) {
            char c = line[pos++];
            if (c == '"') break;
            if (c == '\\' && pos < line.size()) {
                char e = line[pos++];
                switch (e) {
                    case 'n': val += '\n'; break;
                    case 'r': val += '\r'; break;
                    case 't': val += '\t'; break;
                    default:  val += e;    break;
                }
            } else {
                val += c;
            }
        }
        return val;
    }

    static bool IsDone(const std::string& line) {
        return line.find("\"done\":true") != std::string::npos;
    }

    void StreamThread(const std::string& body) {
        HINTERNET hSession = nullptr, hConnect = nullptr, hRequest = nullptr;

        auto cleanup = [&]() {
            if (hRequest) WinHttpCloseHandle(hRequest);
            if (hConnect) WinHttpCloseHandle(hConnect);
            if (hSession) WinHttpCloseHandle(hSession);
            m_busy.store(false);
        };

        hSession = WinHttpOpen(L"RadukCapture/1.0",
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) { FireError("WinHttpOpen failed"); cleanup(); return; }

        hConnect = WinHttpConnect(hSession, L"localhost", 11434, 0);
        if (!hConnect) { FireError("Cannot connect to Ollama on localhost:11434.\n\nMake sure Ollama is running:\n  ollama serve"); cleanup(); return; }

        hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/api/chat",
            nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (!hRequest) { FireError("WinHttpOpenRequest failed"); cleanup(); return; }

        // Set timeouts — generous for LLM inference (first token can take 3-5s)
        DWORD sendTimeout    = 30000;   // 30s to send request
        DWORD receiveTimeout = 120000;  // 120s to receive first byte
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT,    &sendTimeout,    sizeof(sendTimeout));
        WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &receiveTimeout, sizeof(receiveTimeout));

        // Send request
        std::wstring headers = L"Content-Type: application/json";
        BOOL sent = WinHttpSendRequest(hRequest,
            headers.c_str(), (DWORD)headers.size(),
            (LPVOID)body.c_str(), (DWORD)body.size(),
            (DWORD)body.size(), 0);
        if (!sent) { FireError("Request send failed — is Ollama running?"); cleanup(); return; }

        if (!WinHttpReceiveResponse(hRequest, nullptr)) {
            FireError("No response from Ollama"); cleanup(); return;
        }

        // ── Check the status BEFORE reading tokens ────────────
        //
        // This was missing, and it is why a broken model server looked
        // like an app that had gone quiet. Ollama answers a failed
        // generation with HTTP 500 and a body of {"error":"..."} -- no
        // "content" field anywhere in it. The reader below would find no
        // tokens, fire m_onDone, and the panel would sit there with
        // nothing to show and nothing to say. The server had explained
        // itself perfectly well; nobody read the explanation.
        {
            DWORD status = 0, len = sizeof(status);
            if (WinHttpQueryHeaders(hRequest,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &len,
                    WINHTTP_NO_HEADER_INDEX) && status >= 400) {

                // Drain the body for the server's own description.
                std::string errBody;
                for (;;) {
                    DWORD avail = 0;
                    if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
                    char ebuf[2048];
                    DWORD want = (avail < sizeof(ebuf) - 1) ? avail : (DWORD)sizeof(ebuf) - 1;
                    DWORD got = 0;
                    if (!WinHttpReadData(hRequest, ebuf, want, &got) || got == 0) break;
                    ebuf[got] = '\0';
                    errBody += ebuf;
                    if (errBody.size() > 8192) break;
                }

                const std::string detail = ExtractError(errBody);
                std::string msg = "Ollama returned HTTP " + std::to_string(status);
                if (!detail.empty()) msg += ":\n\n" + detail;

                // The one failure worth naming, because the cause is not
                // in this program and the fix is not in this program
                // either. A machine with two NVIDIA cards of different
                // generations can have the model split onto the older
                // one, whose compute capability the shipped CUDA kernels
                // were never built for.
                if (detail.find("kernel image is invalid") != std::string::npos) {
                    msg += "\n\nThis is a GPU problem, not a RadukCapture one. "
                           "The model is being loaded onto a card whose compute "
                           "capability the CUDA build does not support. On a "
                           "two-card machine, restrict Ollama to the newer card "
                           "with CUDA_VISIBLE_DEVICES and restart it.";
                }

                FireError(msg);
                cleanup();
                return;
            }
        }

        // Read streaming response line by line.
        // available == 0 does NOT mean done — LLM inference takes 1-3s before
        // first token. Retry with backoff up to 120s total wait.
        std::string lineBuffer;
        char buf[4096];
        DWORD bytesRead = 0;
        int zeroCount = 0;

        while (!m_cancel.load()) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &available)) break;
            if (available == 0) {
                if (++zeroCount > 2400) break; // 2400 * 50ms = 120s max wait
                Sleep(50);
                continue;
            }
            zeroCount = 0; // reset on any data

            DWORD toRead = (available < (DWORD)(sizeof(buf) - 1))
                         ? available : (DWORD)(sizeof(buf) - 1);
            if (!WinHttpReadData(hRequest, buf, toRead, &bytesRead) || bytesRead == 0) break;
            buf[bytesRead] = '\0';

            // Split on newlines, process each NDJSON line
            for (DWORD i = 0; i < bytesRead; i++) {
                char c = buf[i];
                if (c == '\n') {
                    if (!lineBuffer.empty()) {
                        if (IsDone(lineBuffer)) {
                            if (m_onDone) m_onDone();
                            cleanup();
                            return;
                        }
                        std::string token = ExtractContent(lineBuffer);
                        if (!token.empty() && m_onToken) m_onToken(token);
                        lineBuffer.clear();
                    }
                } else {
                    lineBuffer += c;
                }
            }
        }

        if (m_onDone) m_onDone();
        cleanup();
    }

    void FireError(const std::string& msg) {
        if (m_log)     m_log("[AI] " + msg);
        if (m_onError) m_onError(msg);
    }

    // Pull "error":"..." out of a server response body.
    static std::string ExtractError(const std::string& body) {
        const std::string key = "\"error\":\"";
        auto pos = body.find(key);
        if (pos == std::string::npos) return "";
        pos += key.size();
        std::string val;
        while (pos < body.size()) {
            char c = body[pos++];
            if (c == '"') break;
            if (c == '\\' && pos < body.size()) {
                char e = body[pos++];
                if (e == 'n') val += ' ';
                else          val += e;
                continue;
            }
            val += c;
        }
        return val;
    }

    std::atomic<bool>   m_busy{false};
    std::atomic<bool>   m_cancel{false};
    std::thread         m_thread;

    std::function<void(const std::string&)> m_onToken;
    std::function<void()>                   m_onDone;
    std::function<void(const std::string&)> m_onError;
    std::function<void(const std::string&)> m_log;
};

} // namespace raduk::core
