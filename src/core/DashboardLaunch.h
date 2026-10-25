#pragma once
// ============================================================
// DashboardLaunch.h -- the [DASH] button
//
// Opens RadukCaptureDashboard.html in the browser already holding the
// WebSocket access token, so connecting no longer means opening
// rig.ini and copying 64 hex characters by hand.
//
// The token rides in the URL FRAGMENT, file:///.../Dashboard.html#token=...
// A fragment is never sent to any server, and the dashboard moves it
// into its token box and strips it from the address bar on load
// (history.replaceState), so it does not sit on screen or in the page's
// own URL afterwards. The browser's history of typed or opened URLs
// may still record the first form; that history lives on the capture
// PC, beside the rig.ini the token comes from, so it adds nothing an
// intruder would not already have.
//
// Finding the file and building the link are pure and tested here
// (tests/dashboard_launch.cpp). Opening the browser is main.cpp's.
// ============================================================

#include <string>
#include <vector>

namespace raduk::core {

constexpr const wchar_t* kDashboardFile = L"RadukCaptureDashboard.html";

// Directories to look in, nearest first: beside the exe, then each
// parent up to four levels. The exe lives in build\Release or
// build_emu\Release, two below the repository root that holds the
// dashboard; an installed copy would keep it beside the exe.
inline std::vector<std::wstring> DashboardSearchDirs(const std::wstring& exeDir) {
    std::vector<std::wstring> dirs;
    std::wstring d = exeDir;
    while (!d.empty() && (d.back() == L'\\' || d.back() == L'/')) d.pop_back();
    for (int i = 0; i < 5 && !d.empty(); ++i) {
        dirs.push_back(d);
        const size_t cut = d.find_last_of(L"\\/");
        if (cut == std::wstring::npos) break;
        // Stop at a drive root: "D:" has no parent worth searching.
        if (cut <= 2) { dirs.push_back(d.substr(0, cut)); break; }
        d = d.substr(0, cut);
    }
    return dirs;
}

// A Windows path as a file:// URL: backslashes to slashes, and every
// byte that is not plainly safe percent-encoded from UTF-8, so a space
// or a '#' in a folder name cannot end the path early or start the
// fragment in the wrong place.
inline std::string FileUrlFromPath(const std::wstring& path) {
    std::string utf8;
    for (size_t i = 0; i < path.size(); ++i) {
        unsigned cp = path[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < path.size()) {
            const unsigned lo = path[i + 1];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            }
        }
        if (cp < 0x80) utf8 += (char)cp;
        else if (cp < 0x800) { utf8 += (char)(0xC0 | (cp >> 6)); utf8 += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            utf8 += (char)(0xE0 | (cp >> 12)); utf8 += (char)(0x80 | ((cp >> 6) & 0x3F));
            utf8 += (char)(0x80 | (cp & 0x3F));
        } else {
            utf8 += (char)(0xF0 | (cp >> 18)); utf8 += (char)(0x80 | ((cp >> 12) & 0x3F));
            utf8 += (char)(0x80 | ((cp >> 6) & 0x3F)); utf8 += (char)(0x80 | (cp & 0x3F));
        }
    }
    static const char hex[] = "0123456789ABCDEF";
    std::string url = "file:///";
    for (unsigned char c : utf8) {
        if (c == '\\') { url += '/'; continue; }
        const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '/' || c == ':' ||
                          c == '-' || c == '_' || c == '.' || c == '~';
        if (safe) url += (char)c;
        else { url += '%'; url += hex[c >> 4]; url += hex[c & 15]; }
    }
    return url;
}

// The link [DASH] opens. A token that is not plain hex is left out
// rather than escaped: the server would refuse it anyway, and the
// dashboard then falls back to whatever token it already holds.
inline std::string DashboardUrl(const std::wstring& dashboardPath, const std::string& token) {
    std::string url = FileUrlFromPath(dashboardPath);
    bool hex = !token.empty();
    for (char c : token)
        hex = hex && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
    if (hex) url += "#token=" + token;
    return url;
}

} // namespace raduk::core
