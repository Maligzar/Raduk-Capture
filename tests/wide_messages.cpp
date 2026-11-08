// ============================================================
//  Phase 132.6: text is read with the W messages
//
//  The project is built without UNICODE. The generic common-control
//  macros -- Header_GetItem, TabCtrl_GetItem, ListView_GetItemText,
//  ListView_GetColumn and the rest -- then send the A messages, which
//  write ANSI bytes into whatever buffer they are given. Given a
//  wchar_t buffer (HDITEMW, TCITEMW, LVITEMW), "Name" comes back as
//  two CJK glyphs. 132.3f shipped exactly that in StripDraw.h, and the
//  only thing that caught it was 132.6's ListView_GetItemText failing
//  to compile against a wide buffer; the other two compiled, because
//  those macros cast.
//
//  So this reads the window sources and refuses the generic text
//  getters and setters. Send the W message explicitly instead.
// ============================================================

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-58s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

static std::string Root() {
    std::string f = __FILE__;
    for (char& c : f) if (c == '\\') c = '/';
    const size_t at = f.rfind("/tests/");
    return at == std::string::npos ? std::string(".") : f.substr(0, at);
}

static std::string Read(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Line comments out, so a comment naming a macro is not a use of it.
static std::string Code(const std::string& s) {
    std::string o;
    bool str = false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '"' && (i == 0 || s[i - 1] != '\\')) str = !str;
        if (!str && s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n') ++i;
            o += '\n';
            continue;
        }
        o += s[i];
    }
    return o;
}

static const char* kGeneric[] = {
    "Header_GetItem(", "Header_SetItem(", "Header_InsertItem(",
    "TabCtrl_GetItem(", "TabCtrl_SetItem(", "TabCtrl_InsertItem(",
    "ListView_GetItemText(", "ListView_SetItemText(", "ListView_GetItem(",
    "ListView_SetItem(", "ListView_InsertItem(", "ListView_GetColumn(",
    "ListView_SetColumn(", "ListView_InsertColumn(",
};

static std::vector<std::string> Uses(const std::string& raw) {
    const std::string s = Code(raw);
    std::vector<std::string> out;
    for (const char* m : kGeneric) {
        const std::string k(m);
        for (size_t at = s.find(k); at != std::string::npos; at = s.find(k, at + 1)) {
            if (at > 0 && (std::isalnum((unsigned char)s[at - 1]) || s[at - 1] == '_')) continue;
            int line = 1;
            for (size_t i = 0; i < at; ++i) if (s[i] == '\n') ++line;
            out.push_back(k.substr(0, k.size() - 1) + " at line " + std::to_string(line));
        }
    }
    return out;
}

int main() {
    printf("\n=== Phase 132.6: wide messages in an ANSI build ===\n");

    printf("\n1. The scan finds what it is for\n");
    Check(Uses("Header_GetItem(hdr, i, &it);").size() == 1, "a generic getter is caught");
    Check(Uses("SendMessageW(hdr, HDM_GETITEMW, i, (LPARAM)&it);").empty(), "the W message is not");
    Check(Uses("// TabCtrl_GetItem(h, i, &ti);").empty(), "a comment is not code");

    printf("\n2. The window sources\n");
    std::vector<std::string> files = {"src/main.cpp"};
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(Root() + "/src/gui", ec)) {
        const std::string x = e.path().extension().string();
        if (x == ".h" || x == ".cpp") files.push_back("src/gui/" + e.path().filename().string());
    }
    Check(files.size() > 30, "found the window sources", std::to_string(files.size()) + " files");
    int bad = 0;
    for (const std::string& f : files) {
        for (const std::string& u : Uses(Read(Root() + "/" + f))) {
            ++bad;
            Check(false, f + " uses a generic text macro", u);
        }
    }
    Check(bad == 0, "every common-control text call names its W message");

    printf("\n%s\n", g_fail ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return g_fail ? 1 : 0;
}
