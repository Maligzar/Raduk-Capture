#pragma once
// ============================================================
// HelpWindow.h — Phase 49: In-App Help System
//
// Floating WS_EX_TOPMOST | WS_EX_TOOLWINDOW window (~700x900px)
// showing RadukCapture-Help.md content in a read-only EDIT
// control with monospace font.
//
// Search bar at top filters visible lines — type any substring
// to show only matching lines (section headers always shown).
//
// Toggle with ? key or [?] toolbar button.
// Loads RadukCapture-Help.md from same dir as exe at runtime.
// Falls back to embedded string if file not found.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Theme.h"
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace raduk::gui {

// Embedded fallback help text (used if Help.md not found on disk)
static const wchar_t* HELP_FALLBACK =
    L"RadukCapture Help\r\n"
    L"=================\r\n\r\n"
    L"Help file not found. Place RadukCapture-Help.md\r\n"
    L"in the same folder as RadukCapture.exe.\r\n\r\n"
    L"Key Reference\r\n"
    L"-------------\r\n"
    L"?       Toggle this Help window\r\n"
    L"Space   Emergency stop\r\n"
    L"A       Autofocus\r\n"
    L"B       Cycle Display FX\r\n"
    L"C       Clipping Warning\r\n"
    L"G       GoTo panel\r\n"
    L"H       Cycle Channel Mode (R/G/B/All)\r\n"
    L"M       Sky Map\r\n"
    L"N       Night Vision\r\n"
    L"P       Pause/Resume recording\r\n"
    L"Q       Quality Bar Graph\r\n"
    L"R       Reticle\r\n"
    L"S       Snapshot PNG\r\n"
    L"V       RGB Align\r\n"
    L"X       Pixel Value Readout\r\n"
    L"Z       Cycle Zoom\r\n"
    L"Arrows  Jog mount\r\n";

class HelpWindow {
public:
    HelpWindow() = default;
    ~HelpWindow() { Destroy(); }

    // Create the help window (hidden initially).
    // exeDir: folder containing the exe — used to find Help.md
    bool Create(HWND parent, const std::wstring& exeDir) {
        m_parent = parent;
        m_exeDir = exeDir;
        LoadHelpText();
        return RegisterAndCreate();
    }

    // For WorkspaceLayout, which needs to move this onto a chosen screen.
    HWND Handle() const { return m_hwnd; }

    void Show()    { if (m_hwnd) { ShowWindow(m_hwnd, SW_SHOW); SetForegroundWindow(m_hwnd); } }

    // Phase 68 step 6: open filtered to a section, so a failed
    // readiness row can send the reader straight to the explanation
    // rather than naming a heading they then have to hunt for.
    void ShowSection(const std::string& heading) {
        if (!m_hwnd) return;
        if (m_editSearch && !heading.empty()) {
            wchar_t w[256] = {};
            MultiByteToWideChar(CP_UTF8, 0, heading.c_str(), -1, w, 255);
            SetWindowTextW(m_editSearch, w);
            ApplyFilter();
        }
        Show();
    }
    void Hide()    { if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE); }
    void Toggle()  { if (!m_hwnd) return; IsVisible() ? Hide() : Show(); }
    bool IsVisible() const { return m_hwnd && IsWindowVisible(m_hwnd); }
    void Destroy() {
        if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
        if (m_monoFont) { DeleteObject(m_monoFont); m_monoFont = nullptr; }
        if (m_uiFont)   { DeleteObject(m_uiFont);   m_uiFont   = nullptr; }
    }

private:
    // ── Text loading ──────────────────────────────────────────

    void LoadHelpText() {
        // Try to load from exe directory
        std::wstring path = m_exeDir + L"\\RadukCapture-Help.md";
        HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, 0, nullptr);
        if (hf == INVALID_HANDLE_VALUE) {
            // Try current directory
            path = L"RadukCapture-Help.md";
            hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, 0, nullptr);
        }

        if (hf != INVALID_HANDLE_VALUE) {
            DWORD sz = GetFileSize(hf, nullptr);
            if (sz > 0 && sz < 2 * 1024 * 1024) { // 2MB cap
                std::vector<char> buf(sz + 1, 0);
                DWORD read = 0;
                ReadFile(hf, buf.data(), sz, &read, nullptr);
                CloseHandle(hf);
                // Convert UTF-8 to wide
                int wlen = MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)read, nullptr, 0);
                if (wlen > 0) {
                    std::wstring wide(wlen, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)read, wide.data(), wlen);
                    // Normalise line endings to \r\n for EDIT control
                    m_fullText.clear();
                    for (size_t i = 0; i < wide.size(); i++) {
                        if (wide[i] == L'\r') continue; // strip bare CR
                        if (wide[i] == L'\n') m_fullText += L"\r\n";
                        else                  m_fullText += wide[i];
                    }
                    SplitLines();
                    printf("[HelpWindow] Loaded %zu lines from %ls\n",
                           m_lines.size(), path.c_str());
                    return;
                }
            }
            CloseHandle(hf);
        }

        // Fallback to embedded string
        printf("[HelpWindow] Help.md not found — using embedded fallback\n");
        m_fullText = HELP_FALLBACK;
        SplitLines();
    }

    void SplitLines() {
        m_lines.clear();
        std::wstring line;
        for (wchar_t c : m_fullText) {
            if (c == L'\n') {
                m_lines.push_back(line);
                line.clear();
            } else if (c != L'\r') {
                line += c;
            }
        }
        if (!line.empty()) m_lines.push_back(line);
    }

    // Build display text from m_lines, filtered by m_searchTerm.
    // Section headers (lines starting with #, =, -, |, or ##) always shown.
    std::wstring BuildFilteredText(const std::wstring& term) const {
        if (term.empty()) return m_fullText;

        // Lowercase copy of search term for case-insensitive match
        std::wstring lterm = term;
        for (auto& c : lterm) c = (wchar_t)towlower(c);

        std::wstring result;
        result.reserve(m_fullText.size() / 2);

        for (const auto& ln : m_lines) {
            bool isHeader = (!ln.empty() && (
                ln[0] == L'#' || ln[0] == L'=' || ln[0] == L'-' ||
                ln[0] == L'|' || ln[0] == L'*'));
            bool isEmpty  = ln.empty();

            if (isHeader || isEmpty) {
                result += ln;
                result += L"\r\n";
                continue;
            }

            // Case-insensitive substring search
            std::wstring lln = ln;
            for (auto& c : lln) c = (wchar_t)towlower(c);
            if (lln.find(lterm) != std::wstring::npos) {
                result += ln;
                result += L"\r\n";
            }
        }
        return result;
    }

    void ApplyFilter() {
        if (!m_editContent || !m_editSearch) return;

        wchar_t searchBuf[256] = {};
        GetWindowTextW(m_editSearch, searchBuf, 256);
        std::wstring term(searchBuf);

        std::wstring filtered = BuildFilteredText(term);

        // Freeze, set text, unfreeze — prevents flicker on large updates
        SendMessage(m_editContent, WM_SETREDRAW, FALSE, 0);
        SetWindowTextW(m_editContent, filtered.c_str());
        SendMessage(m_editContent, WM_SETREDRAW, TRUE,  0);
        InvalidateRect(m_editContent, nullptr, TRUE);
    }

    // ── Window creation ──────────────────────────────────────

    bool RegisterAndCreate() {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = HelpWndProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(Theme::NV(RGB(7, 13, 22)));
        wc.lpszClassName = L"RadukHelpWindow";
        RegisterClassExW(&wc); // ignore already-registered

        // Position near right edge of screen
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        int winW = 720, winH = 900;
        int winX = screenW - winW - 20;
        int winY = (screenH - winH) / 2;

        m_hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"RadukHelpWindow",
            L"RadukCapture Help",
            WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
            winX, winY, winW, winH,
            nullptr, nullptr, GetModuleHandle(nullptr), this);

        if (!m_hwnd) {
            printf("[HelpWindow] CreateWindowExW failed: %lu\n", GetLastError());
            return false;
        }

        CreateControls();
        printf("[HelpWindow] Created\n");
        return true;
    }

    void CreateControls() {
        RECT cr; GetClientRect(m_hwnd, &cr);
        int cw = cr.right - cr.left;
        int ch = cr.bottom - cr.top;

        // Search bar — single-line EDIT at top
        const int SH = 24; // search bar height
        const int PAD = 4;

        // "Search:" label
        m_lblSearch = CreateWindowExW(0, L"STATIC", L"Filter:",
            WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
            PAD, PAD, 50, SH,
            m_hwnd, nullptr, GetModuleHandle(nullptr), nullptr);

        // Search edit box
        m_editSearch = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            PAD + 54, PAD, cw - PAD*2 - 54 - 70, SH,
            m_hwnd, (HMENU)ID_SEARCH, GetModuleHandle(nullptr), nullptr);

        // Clear button
        m_btnClear = CreateWindowExW(0, L"BUTTON", L"Clear",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            cw - PAD - 66, PAD, 66, SH,
            m_hwnd, (HMENU)ID_CLEAR, GetModuleHandle(nullptr), nullptr);

        // Read-only EDIT for content — multiline, scrollable
        m_editContent = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
            PAD, SH + PAD*2, cw - PAD*2, ch - SH - PAD*3,
            m_hwnd, (HMENU)ID_CONTENT, GetModuleHandle(nullptr), nullptr);

        // Monospace font for content
        HFONT monoFont = CreateFontW(
            -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
            L"Courier New");
        if (!monoFont) monoFont = (HFONT)GetStockObject(SYSTEM_FIXED_FONT);

        // UI font for label + buttons
        HFONT uiFont = CreateFontW(
            -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
        if (!uiFont) uiFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        SendMessage(m_editContent, WM_SETFONT, (WPARAM)monoFont, TRUE);
        SendMessage(m_editSearch,  WM_SETFONT, (WPARAM)uiFont,   TRUE);
        SendMessage(m_btnClear,    WM_SETFONT, (WPARAM)uiFont,   TRUE);
        SendMessage(m_lblSearch,   WM_SETFONT, (WPARAM)uiFont,   TRUE);

        // Set initial content
        SetWindowTextW(m_editContent, m_fullText.c_str());

        // Subclass the search edit to catch EN_CHANGE for live filtering
        SetWindowLongPtrW(m_editSearch, GWLP_USERDATA, (LONG_PTR)this);
        m_origSearchProc = (WNDPROC)SetWindowLongPtrW(
            m_editSearch, GWLP_WNDPROC, (LONG_PTR)SearchEditSubclassProc);
    }

    // ── Fonts sized from the window ───────────────────────────
    //
    // The controls already reflowed on WM_SIZE; the fonts did not.
    // Fixed at 13px, this window filling a 1920x1080 screen showed the
    // same tiny text it showed at 745px wide, which is most of the way
    // to not being resizable at all -- more text on screen at a size
    // you still cannot read from the eyepiece.
    //
    // Bounded by BOTH dimensions, and width is the one that bites.
    //
    // This is a monospace document with a natural column width: the
    // prose is hard-wrapped at about 72 characters. Size the font off
    // height alone and a tall narrow window gets text too wide for
    // itself -- at 745x900 an height-only rule picked 19px, which
    // pushed 72 columns past the right edge and introduced horizontal
    // scrolling on a window that had none before. Making a window
    // bigger must not make it worse.
    //
    // The width bound is MEASURED, not assumed. A first attempt
    // divided by a constant derived from "Courier New advances about
    // 0.6 em", which left prose two columns short of fitting -- close
    // enough to look deliberate and wrong enough to scroll. Asking the
    // font how wide 72 characters actually are costs one memory DC and
    // removes the guess for good.
    //
    // The tables in the help file run to 459 characters and will scroll
    // sideways whatever this returns. That was always true and is not
    // what this is protecting.
    static constexpr int PROSE_COLS = 72;

    static int FontPxFor(int cw, int ch) {
        // Height sets what a reader wants; width sets what fits.
        int px = ch / 46;
        if (px > 30) px = 30;

        // Room the content edit actually has: padding both sides, the
        // client edge, and the vertical scrollbar.
        const int avail = cw - (px / 3) * 2 - 6
                        - GetSystemMetrics(SM_CXVSCROLL);
        if (avail > 40) {
            HDC dc = CreateCompatibleDC(nullptr);
            if (dc) {
                wchar_t sample[PROSE_COLS + 1];
                for (int i = 0; i < PROSE_COLS; i++) sample[i] = L'M';
                sample[PROSE_COLS] = 0;
                while (px > 13) {
                    HFONT f = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE,
                                          FALSE, FALSE, DEFAULT_CHARSET,
                                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                          CLEARTYPE_QUALITY,
                                          FIXED_PITCH | FF_MODERN, L"Courier New");
                    if (!f) break;
                    HGDIOBJ old = SelectObject(dc, f);
                    SIZE sz = {};
                    GetTextExtentPoint32W(dc, sample, PROSE_COLS, &sz);
                    SelectObject(dc, old);
                    DeleteObject(f);
                    if (sz.cx <= avail) break;
                    px--;
                }
                DeleteDC(dc);
            }
        }
        return (px < 13) ? 13 : px;
    }

    void MakeFonts(int cw, int ch) {
        const int px = FontPxFor(cw, ch);
        if (px == m_fontPx && m_monoFont && m_uiFont) return;   // nothing to do
        m_fontPx = px;

        HFONT oldMono = m_monoFont, oldUi = m_uiFont;

        m_monoFont = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 FIXED_PITCH | FF_MODERN, L"Courier New");
        // The chrome stays a little smaller than the body: a filter box
        // as tall as a heading is just wasted space at the top of a
        // screen-filling window.
        const int uiPx = (px * 11) / 13;
        m_uiFont = CreateFontW(-uiPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        if (m_editContent) SendMessage(m_editContent, WM_SETFONT, (WPARAM)m_monoFont, TRUE);
        if (m_editSearch)  SendMessage(m_editSearch,  WM_SETFONT, (WPARAM)m_uiFont,   TRUE);
        if (m_btnClear)    SendMessage(m_btnClear,    WM_SETFONT, (WPARAM)m_uiFont,   TRUE);
        if (m_lblSearch)   SendMessage(m_lblSearch,   WM_SETFONT, (WPARAM)m_uiFont,   TRUE);

        // Deleted only after the controls have been given the new ones.
        // A font still selected into a control is not freed, and the
        // text goes blank.
        if (oldMono) DeleteObject(oldMono);
        if (oldUi)   DeleteObject(oldUi);
    }

    void OnResize(int cw, int ch) {
        if (!m_editContent || !m_editSearch || !m_btnClear) return;
        MakeFonts(cw, ch);

        // The search row grows with its font, or a 30px filter box
        // holds 13px text with a band of dead space under it.
        const int PAD   = (std::max)(4, m_fontPx / 3);
        const int SH    = m_fontPx * 2;
        const int lblW  = m_fontPx * 4;
        const int btnW  = m_fontPx * 5;

        if (m_lblSearch)
            SetWindowPos(m_lblSearch, nullptr, PAD, PAD, lblW, SH, SWP_NOZORDER);
        SetWindowPos(m_editSearch,  nullptr, PAD + lblW, PAD,
                     cw - PAD*3 - lblW - btnW, SH, SWP_NOZORDER);
        SetWindowPos(m_btnClear,    nullptr, cw - PAD - btnW, PAD,
                     btnW, SH, SWP_NOZORDER);
        SetWindowPos(m_editContent, nullptr, PAD, SH + PAD*2,
                     cw - PAD*2, ch - SH - PAD*3, SWP_NOZORDER);
    }

    // ── Window procedure ─────────────────────────────────────

    static LRESULT CALLBACK HelpWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        HelpWindow* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCT*>(lp);
            self = static_cast<HelpWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        } else {
            self = reinterpret_cast<HelpWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        switch (msg) {
            case WM_SIZE:
                if (self) self->OnResize(LOWORD(lp), HIWORD(lp));
                return 0;

            case WM_COMMAND: {
                int id = LOWORD(wp);
                int code = HIWORD(wp);
                if (id == ID_CLEAR && code == BN_CLICKED) {
                    if (self && self->m_editSearch) {
                        SetWindowTextW(self->m_editSearch, L"");
                        self->ApplyFilter();
                    }
                }
                return 0;
            }

            case WM_CLOSE:
                // Hide instead of destroy so window can be re-shown
                ShowWindow(hwnd, SW_HIDE);
                return 0;

            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLOREDIT: {
                HDC hdc = (HDC)wp;
                SetTextColor(hdc, Theme::NV(RGB(200, 232, 240)));
                SetBkColor(hdc,   Theme::NV(RGB(10,  21,  32)));
                HBRUSH bg = Theme::Brush(RGB(10, 21, 32));
                return (LRESULT)bg;
            }

            case WM_CTLCOLORBTN: {
                HDC hdc = (HDC)wp;
                SetTextColor(hdc, Theme::NV(RGB(200, 232, 240)));
                SetBkColor(hdc,   Theme::NV(RGB(10,  21,  32)));
                HBRUSH bg = Theme::Brush(RGB(10, 21, 32));
                return (LRESULT)bg;
            }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    // Subclass proc for search edit — fires ApplyFilter on every keystroke
    static LRESULT CALLBACK SearchEditSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        HelpWindow* self = reinterpret_cast<HelpWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (msg == WM_KEYUP || msg == WM_CHAR) {
            if (self) self->ApplyFilter();
        }
        return CallWindowProcW(self ? self->m_origSearchProc : DefWindowProcW, hwnd, msg, wp, lp);
    }

    // ── Control IDs ───────────────────────────────────────────
    enum : int { ID_SEARCH = 4100, ID_CONTENT = 4101, ID_CLEAR = 4102 };

    // ── Members ───────────────────────────────────────────────
    HWND            m_hwnd        = nullptr;
    HWND            m_parent      = nullptr;
    HWND            m_editSearch  = nullptr;
    HWND            m_editContent = nullptr;
    HWND            m_btnClear    = nullptr;
    HWND            m_lblSearch   = nullptr;
    HFONT           m_monoFont    = nullptr;
    HFONT           m_uiFont      = nullptr;
    int             m_fontPx      = 0;
    WNDPROC         m_origSearchProc = nullptr;
    std::wstring    m_exeDir;
    std::wstring    m_fullText;
    std::vector<std::wstring> m_lines;
};

} // namespace raduk::gui
