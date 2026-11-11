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
//
// ── Phase 132.10a: the app writes the page ──────────────────
//
// The page is carried in the exe (RadukCapture.rc, resource 305) and
// written to <data root>\data\dashboard.html on every [DASH], so the
// dashboard always matches the build that opened it and no longer has to
// be found on disk. What the app adds on the way out
// (PrepareDashboardHtml): the bundled fonts as @font-face, which the page
// has named since it was written and never had, and data-mode="night"
// when the app is in night vision. The shared palette is deliberately
// NOT injected yet: the dashboard's own :root uses the same variable
// names (--void, --panel, --text, --dim, --accent) with other meanings,
// and the palette's night block would override them. That lands with
// 132.10b, when the dashboard's CSS moves onto the roles.
//
// It opens in Edge's app mode (--app=) when Edge is found: a window with
// no address bar, so the token is never on screen, that reads as part of
// the app. Otherwise it opens in the default browser, as before.
// ============================================================

#include <cctype>
#include <cstring>
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

// The exe's copy of the page (RadukCapture.rc.in, RCDATA).
constexpr int kDashboardResourceId = 305;
constexpr const char* kDashboardWrittenName = "dashboard.html";

// The page as the app writes it. `headCss` goes in as the first thing
// inside <head>, so the page's own rules, which come later, still win
// any tie. `night` sets data-mode="night" on <html>, replacing any value
// already there. A page with no <head> or <html> tag still gets both:
// the style at the very start, and no attribute (there is nothing to put
// it on). Nothing else in the page is touched.
inline std::string PrepareDashboardHtml(const std::string& html, const std::string& headCss,
                                        bool night) {
    auto findTag = [](const std::string& t, const char* name) -> size_t {
        // "<name" followed by '>' or whitespace, case-insensitively.
        const size_t n = strlen(name);
        for (size_t i = t.find('<'); i != std::string::npos; i = t.find('<', i + 1)) {
            if (i + 1 + n > t.size()) return std::string::npos;
            bool eq = true;
            for (size_t k = 0; k < n && eq; ++k)
                eq = tolower((unsigned char)t[i + 1 + k]) == name[k];
            if (!eq) continue;
            const char c = i + 1 + n < t.size() ? t[i + 1 + n] : '\0';
            if (c == '>' || c == ' ' || c == '\t' || c == '\r' || c == '\n') return i;
        }
        return std::string::npos;
    };
    std::string out = html;

    // <html ...>: the night flag.
    const size_t h = findTag(out, "html");
    if (h != std::string::npos) {
        const size_t close = out.find('>', h);
        if (close != std::string::npos) {
            std::string tag = out.substr(h, close - h);   // "<html ..." without '>'
            // Drop an existing data-mode="..." (double or single quotes).
            for (size_t a = tag.find("data-mode"); a != std::string::npos; a = tag.find("data-mode")) {
                size_t e = tag.find('=', a);
                if (e == std::string::npos) { tag.erase(a, 9); continue; }
                size_t v = e + 1;
                while (v < tag.size() && tag[v] == ' ') ++v;
                size_t end = v;
                if (v < tag.size() && (tag[v] == '"' || tag[v] == '\'')) {
                    const size_t q = tag.find(tag[v], v + 1);
                    end = q == std::string::npos ? tag.size() : q + 1;
                } else {
                    while (end < tag.size() && tag[end] != ' ') ++end;
                }
                size_t start = a;
                while (start > 0 && tag[start - 1] == ' ') --start;
                tag.erase(start, end - start);
            }
            if (night) tag += " data-mode=\"night\"";
            out.replace(h, close - h, tag);
        }
    }

    // <head ...>: the style, first thing inside.
    if (headCss.empty()) return out;
    const std::string style = "<style id=\"raduk-app\">" + headCss + "</style>";
    const size_t hd = findTag(out, "head");
    if (hd != std::string::npos) {
        const size_t close = out.find('>', hd);
        if (close != std::string::npos) { out.insert(close + 1, style); return out; }
    }
    out.insert(0, style);
    return out;
}

// Edge's app-mode command line for a URL: a window with no address bar.
// The URL is ASCII (DashboardUrl percent-encodes it), so quoting it is
// enough; it cannot contain a '"'.
inline std::wstring EdgeAppArguments(const std::string& url) {
    return L"--app=\"" + std::wstring(url.begin(), url.end()) + L"\"";
}

// Where msedge.exe normally lives, most likely first, from the folders
// Windows reports. The registry's App Paths entry is checked before
// these by main.cpp; these are the fallback.
inline std::vector<std::wstring> EdgeCandidates(const std::wstring& programFilesX86,
                                                const std::wstring& programFiles,
                                                const std::wstring& localAppData) {
    std::vector<std::wstring> v;
    const wchar_t* tail = L"\\Microsoft\\Edge\\Application\\msedge.exe";
    for (const std::wstring* base : { &programFilesX86, &programFiles, &localAppData })
        if (!base->empty()) v.push_back(*base + tail);
    return v;
}

} // namespace raduk::core
