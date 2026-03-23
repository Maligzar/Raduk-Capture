#pragma once
// ============================================================
// AIChatPanel.h — Phase 50: Local AI Chat Window
//
// Floating WS_EX_TOPMOST | WS_EX_TOOLWINDOW window (~500x620px)
// Chat history in a read-only EDIT, user input at bottom.
// Streams tokens from AIAssistant in real-time via WM_USER+50.
//
// Toggle: / key or [AI] toolbar button.
// ============================================================

#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <cstdio>
#include "../core/AIAssistant.h"
#include "../core/AIContextBuilder.h"

namespace raduk::gui {

// WM_USER offsets for cross-thread messaging
static constexpr UINT WM_AI_TOKEN  = WM_USER + 50; // lParam = heap-allocated std::string*
static constexpr UINT WM_AI_DONE   = WM_USER + 51;
static constexpr UINT WM_AI_ERROR  = WM_USER + 52; // lParam = heap-allocated std::string*

class AIChatPanel {
public:
    AIChatPanel() = default;
    ~AIChatPanel() { Destroy(); }

    // Create the floating window (hidden initially).
    bool Create(HWND parent, const std::wstring& exeDir) {
        m_parent  = parent;
        m_exeDir  = exeDir;

        m_contextBuilder.LoadHelpFile(exeDir);
        m_knowledgeDB.Open("D:\\RadukCapture\\data\\object_knowledge.db");
        m_contextBuilder.SetKnowledgeDB(&m_knowledgeDB);

        // Wire AIAssistant callbacks — post messages to keep UI on main thread
        m_assistant.SetOnToken([this](const std::string& tok) {
            auto* s = new std::string(tok);
            PostMessageW(m_hwnd, WM_AI_TOKEN, 0, (LPARAM)s);
        });
        m_assistant.SetOnDone([this]() {
            PostMessageW(m_hwnd, WM_AI_DONE, 0, 0);
        });
        m_assistant.SetOnError([this](const std::string& err) {
            auto* s = new std::string(err);
            PostMessageW(m_hwnd, WM_AI_ERROR, 0, (LPARAM)s);
        });

        return RegisterAndCreate();
    }

    void Show()    { if (m_hwnd) { ShowWindow(m_hwnd, SW_SHOW); SetForegroundWindow(m_hwnd); } }
    void Hide()    { if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE); }
    void Toggle()  { if (!m_hwnd) return; IsVisible() ? Hide() : Show(); }
    bool IsVisible() const { return m_hwnd && IsWindowVisible(m_hwnd); }
    void Destroy() {
        m_assistant.Cancel();
        if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    }

    void SetSessionState(const raduk::core::AISessionState& state) {
        m_sessionState = state;
    }

    // Expose knowledge DB for ObjectInfoPanel to share
    raduk::core::ObjectKnowledgeDB& GetKnowledgeDB() { return m_knowledgeDB; }

private:
    static constexpr int PANEL_W = 500;
    static constexpr int PANEL_H = 620;

    enum : int {
        ID_EDIT_HISTORY = 4200,
        ID_EDIT_INPUT   = 4201,
        ID_BTN_SEND     = 4202,
        ID_BTN_CLEAR    = 4203,
    };

    bool RegisterAndCreate() {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = ChatWndProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(7, 13, 22));
        wc.lpszClassName = L"RadukAIChat";
        RegisterClassExW(&wc);

        // Position right side of screen
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        int wx = sw - PANEL_W - 20;
        int wy = (sh - PANEL_H) / 2;

        m_hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"RadukAIChat",
            L"RadukCapture AI  \u00B7  llama3.2:3b",
            WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
            wx, wy, PANEL_W, PANEL_H,
            nullptr, nullptr, GetModuleHandle(nullptr), this);

        if (!m_hwnd) {
            printf("[AIChatPanel] CreateWindowExW failed: %lu\n", GetLastError());
            return false;
        }

        CreateControls();
        AppendHistory(L"RadukScope AI ready. Ask me anything about your session, "
                      L"the sky tonight, or how to use RadukCapture.\r\n\r\n"
                      L"Type your question below and press Enter or [Send].\r\n");

        // Check if Ollama is running and warn if not
        if (!raduk::core::AIAssistant::IsOllamaRunning()) {
            AppendHistory(L"\u26A0 Ollama not detected on localhost:11434.\r\n"
                          L"To enable AI: install Ollama, then run:\r\n"
                          L"  ollama pull llama3.2:3b\r\n\r\n");
        }

        printf("[AIChatPanel] Created\n");
        return true;
    }

    void CreateControls() {
        RECT cr; GetClientRect(m_hwnd, &cr);
        int cw = cr.right, ch = cr.bottom;
        const int PAD = 6, BH = 28, IH = 28;

        // History EDIT — read-only, multiline, scrollable
        m_editHistory = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            PAD, PAD, cw - PAD*2, ch - IH - BH - PAD*4,
            m_hwnd, (HMENU)ID_EDIT_HISTORY, GetModuleHandle(nullptr), nullptr);

        // Input EDIT — single line
        int inputY = ch - IH - BH - PAD*2;
        m_editInput = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            PAD, inputY, cw - PAD*2, IH,
            m_hwnd, (HMENU)ID_EDIT_INPUT, GetModuleHandle(nullptr), nullptr);

        // Buttons
        int btnY = ch - BH - PAD;
        int sendW = (cw - PAD*3) * 3 / 4;
        int clearW = cw - PAD*3 - sendW;
        m_btnSend = CreateWindowExW(0, L"BUTTON", L"Send (Enter)",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            PAD, btnY, sendW, BH,
            m_hwnd, (HMENU)ID_BTN_SEND, GetModuleHandle(nullptr), nullptr);
        m_btnClear = CreateWindowExW(0, L"BUTTON", L"Clear",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            PAD + sendW + PAD, btnY, clearW, BH,
            m_hwnd, (HMENU)ID_BTN_CLEAR, GetModuleHandle(nullptr), nullptr);

        // Fonts
        HFONT histFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT inputFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (!histFont)  histFont  = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        if (!inputFont) inputFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        SendMessage(m_editHistory, WM_SETFONT, (WPARAM)histFont,  TRUE);
        SendMessage(m_editInput,   WM_SETFONT, (WPARAM)inputFont, TRUE);
        SendMessage(m_btnSend,     WM_SETFONT, (WPARAM)inputFont, TRUE);
        SendMessage(m_btnClear,    WM_SETFONT, (WPARAM)inputFont, TRUE);

        // Subclass input edit to catch Enter key
        SetWindowLongPtrW(m_editInput, GWLP_USERDATA, (LONG_PTR)this);
        m_origInputProc = (WNDPROC)SetWindowLongPtrW(
            m_editInput, GWLP_WNDPROC, (LONG_PTR)InputEditSubclass);
    }

    void OnResize(int cw, int ch) {
        const int PAD = 6, BH = 28, IH = 28;
        if (m_editHistory) SetWindowPos(m_editHistory, nullptr,
            PAD, PAD, cw-PAD*2, ch-IH-BH-PAD*4, SWP_NOZORDER);
        int inputY = ch - IH - BH - PAD*2;
        if (m_editInput) SetWindowPos(m_editInput, nullptr,
            PAD, inputY, cw-PAD*2, IH, SWP_NOZORDER);
        int btnY  = ch - BH - PAD;
        int sendW = (cw - PAD*3) * 3 / 4;
        int clearW= cw - PAD*3 - sendW;
        if (m_btnSend)  SetWindowPos(m_btnSend,  nullptr,
            PAD, btnY, sendW, BH, SWP_NOZORDER);
        if (m_btnClear) SetWindowPos(m_btnClear, nullptr,
            PAD + sendW + PAD, btnY, clearW, BH, SWP_NOZORDER);
    }

    void AppendHistory(const wchar_t* text) {
        if (!m_editHistory) return;
        int len = GetWindowTextLengthW(m_editHistory);
        SendMessageW(m_editHistory, EM_SETSEL, len, len);
        SendMessageW(m_editHistory, EM_REPLACESEL, FALSE, (LPARAM)text);
        SendMessageW(m_editHistory, EM_SCROLLCARET, 0, 0);
    }

    void AppendHistoryA(const std::string& text) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        if (wlen <= 0) return;
        std::wstring wide(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), wlen);
        // Normalise line endings
        std::wstring out;
        for (wchar_t c : wide) {
            if (c == L'\n') out += L"\r\n";
            else if (c != L'\r') out += c;
        }
        AppendHistory(out.c_str());
    }

    void DoSend() {
        if (!m_editInput || m_assistant.IsBusy()) return;

        wchar_t wbuf[1024] = {};
        GetWindowTextW(m_editInput, wbuf, 1024);
        SetWindowTextW(m_editInput, L"");

        if (wcslen(wbuf) == 0) return;

        // Convert to UTF-8
        char ubuf[1024] = {};
        WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, ubuf, 1024, nullptr, nullptr);
        std::string userMsg(ubuf);

        // Add to history display
        AppendHistory(L"You: ");
        AppendHistory(wbuf);
        AppendHistory(L"\r\n\r\nRadukScope: \u23F3 thinking...");
        m_streamingNow = true;
        // Disable send while waiting — re-enabled in WM_AI_DONE/ERROR
        if (m_btnSend) EnableWindow(m_btnSend, FALSE);

        // Add to conversation history
        m_history.push_back({"user", userMsg});

        // Build context
        std::string systemPrompt = m_contextBuilder.Build(
            m_sessionState, m_plannerSnapshot, userMsg);

        // Send to Ollama
        if (!m_assistant.Send(systemPrompt, m_history, userMsg)) {
            // Erase the "thinking..." placeholder
            int len = GetWindowTextLengthW(m_editHistory);
            // Remove last line by selecting back to "RadukScope: "
            SendMessageW(m_editHistory, EM_SETSEL, len - 14, len);
            SendMessageW(m_editHistory, EM_REPLACESEL, FALSE, (LPARAM)L"");
            AppendHistory(L"(busy — please wait)\r\n\r\n");
            m_history.pop_back();
            if (m_btnSend) EnableWindow(m_btnSend, TRUE);
        }
    }

    void DoClear() {
        m_assistant.Cancel();
        m_history.clear();
        m_streamingNow = false;
        if (m_editHistory) SetWindowTextW(m_editHistory, L"");
        AppendHistory(L"Conversation cleared.\r\n\r\n");
    }

    // ── Window procedure ──────────────────────────────────────
    static LRESULT CALLBACK ChatWndProc(HWND hwnd, UINT msg,
                                         WPARAM wp, LPARAM lp) {
        AIChatPanel* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCT*>(lp);
            self = static_cast<AIChatPanel*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        } else {
            self = reinterpret_cast<AIChatPanel*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        switch (msg) {
            case WM_SIZE:
                if (self) self->OnResize(LOWORD(lp), HIWORD(lp));
                return 0;

            case WM_COMMAND:
                if (self) {
                    int id = LOWORD(wp);
                    if (id == ID_BTN_SEND)  self->DoSend();
                    if (id == ID_BTN_CLEAR) self->DoClear();
                }
                return 0;

            // Streaming token from background thread
            case WM_AI_TOKEN: {
                auto* s = reinterpret_cast<std::string*>(lp);
                if (self && s) {
                    // First token — remove the "⏳ thinking..." placeholder
                    if (!self->m_firstTokenReceived) {
                        self->m_firstTokenReceived = true;
                        // Find and remove trailing " ⏳ thinking..."
                        int len = GetWindowTextLengthW(self->m_editHistory);
                        // The placeholder is 14 wide chars: " ⏳ thinking..."
                        int placeLen = (int)wcslen(L"\u23F3 thinking...");
                        SendMessageW(self->m_editHistory, EM_SETSEL,
                                     len - placeLen, len);
                        SendMessageW(self->m_editHistory, EM_REPLACESEL,
                                     FALSE, (LPARAM)L"");
                    }
                    self->AppendHistoryA(*s);
                }
                delete s;
                return 0;
            }
            // Streaming complete
            case WM_AI_DONE: {
                if (self) {
                    self->AppendHistory(L"\r\n\r\n");
                    self->m_streamingNow       = false;
                    self->m_firstTokenReceived = false;
                    if (self->m_btnSend) EnableWindow(self->m_btnSend, TRUE);
                    if (self->m_editInput) SetFocus(self->m_editInput);
                }
                return 0;
            }
            // Error from assistant
            case WM_AI_ERROR: {
                auto* s = reinterpret_cast<std::string*>(lp);
                if (self && s) {
                    // Remove thinking placeholder if still showing
                    if (!self->m_firstTokenReceived) {
                        int len = GetWindowTextLengthW(self->m_editHistory);
                        int placeLen = (int)wcslen(L"\u23F3 thinking...");
                        SendMessageW(self->m_editHistory, EM_SETSEL,
                                     len - placeLen, len);
                        SendMessageW(self->m_editHistory, EM_REPLACESEL,
                                     FALSE, (LPARAM)L"");
                    }
                    self->AppendHistory(L"\r\n\u26A0 Error: ");
                    self->AppendHistoryA(*s);
                    self->AppendHistory(L"\r\n\r\n");
                    self->m_streamingNow       = false;
                    self->m_firstTokenReceived = false;
                    if (self->m_btnSend) EnableWindow(self->m_btnSend, TRUE);
                }
                delete s;
                return 0;
            }

            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORSTATIC: {
                HDC hdc = (HDC)wp;
                SetTextColor(hdc, RGB(200, 232, 240));
                SetBkColor(hdc,   RGB(10,  21,  32));
                static HBRUSH bg = CreateSolidBrush(RGB(10, 21, 32));
                return (LRESULT)bg;
            }
            case WM_CTLCOLORBTN: {
                HDC hdc = (HDC)wp;
                SetTextColor(hdc, RGB(200, 232, 240));
                SetBkColor(hdc,   RGB(10,  21,  32));
                static HBRUSH bg = CreateSolidBrush(RGB(10, 21, 32));
                return (LRESULT)bg;
            }

            case WM_CLOSE:
                ShowWindow(hwnd, SW_HIDE);
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    // Subclass input edit — intercept Enter key to send
    static LRESULT CALLBACK InputEditSubclass(HWND hwnd, UINT msg,
                                               WPARAM wp, LPARAM lp) {
        AIChatPanel* self = reinterpret_cast<AIChatPanel*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_KEYDOWN && wp == VK_RETURN) {
            if (self) self->DoSend();
            return 0;
        }
        return CallWindowProcW(self ? self->m_origInputProc : DefWindowProcW,
                               hwnd, msg, wp, lp);
    }

    // ── Members ────────────────────────────────────────────────
    HWND     m_hwnd        = nullptr;
    HWND     m_parent      = nullptr;
    HWND     m_editHistory = nullptr;
    HWND     m_editInput   = nullptr;
    HWND     m_btnSend     = nullptr;
    HWND     m_btnClear    = nullptr;
    WNDPROC  m_origInputProc = nullptr;
    std::wstring m_exeDir;

    raduk::core::AIAssistant      m_assistant;
    raduk::core::AIContextBuilder m_contextBuilder;
    raduk::core::ObjectKnowledgeDB m_knowledgeDB;
    raduk::core::AISessionState   m_sessionState;

    // Planner snapshot — refreshed by main.cpp before each query
    // Stored as opaque vector via type erasure to avoid GoToPanel dep
    struct PlannerRow {
        std::string name, type, windowStr;
        float mag, altNow, peakAlt;
        int   minsLeft;
        double ra, dec;
    };
    std::vector<PlannerRow> m_plannerSnapshot;

    std::vector<raduk::core::ChatMessage> m_history;
    bool m_streamingNow        = false;
    bool m_firstTokenReceived  = false;

public:
    // Called from main.cpp to inject planner data before send.
    // Uses GoToPanel::PlannerEntry fields via template.
    template<typename PlannerEntry>
    void UpdatePlanner(const std::vector<PlannerEntry>& entries) {
        m_plannerSnapshot.clear();
        for (const auto& e : entries) {
            PlannerRow r;
            r.name      = e.name;
            r.type      = e.type;
            r.mag       = e.mag;
            r.altNow    = e.altNow;
            r.peakAlt   = e.peakAlt;
            r.minsLeft  = e.minsLeft;
            r.windowStr = e.windowStr;
            r.ra        = e.ra;
            r.dec       = e.dec;
            m_plannerSnapshot.push_back(r);
        }
    }
};

} // namespace raduk::gui
