#pragma once
// ============================================================
// TextConv.h — UTF-8 <-> UTF-16 at the Win32 edge, sized by the
// text rather than by a number someone picked once
//
// Every string the app keeps is UTF-8 std::string; every string
// Win32 takes or returns is wchar_t. The usual way to cross that
// line here was a stack array -- wchar_t buf[256] -- and the
// conversion calls truncate in silence when the text does not fit.
// That is fine for a label and wrong for anything the user typed:
// the profile EDIT dialog read its NOTES field through a 256-unit
// buffer and handed SAVE a shortened note, which then went to
// disk. planet_focus.ini ships a 323-character note, so the loss
// was one EDIT-and-SAVE away for anyone who opened it.
//
// These three ask how much room the text needs and then take it.
// They are the only sizing worth having; a caller that wants a
// fixed buffer back should have a reason that is written down.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>

namespace raduk::core {

// Both conversions pass an explicit length instead of -1, so the
// result carries no terminator and a string with an embedded NUL
// survives the trip intact.
inline std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                 nullptr, 0, nullptr, nullptr);
    if (sz <= 0) return "";
    std::string out((size_t)sz, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                        &out[0], sz, nullptr, nullptr);
    return out;
}

inline std::wstring ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                 nullptr, 0);
    if (sz <= 0) return L"";
    std::wstring out((size_t)sz, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], sz);
    return out;
}

// The whole of a control's text, however long it is. Ask the
// control first -- GetWindowTextLengthW is the only honest source
// for the size, since the user decides it.
inline std::string GetWindowTextUtf8(HWND h) {
    if (!h) return "";
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return "";
    std::wstring w((size_t)len + 1, L'\0');
    int got = GetWindowTextW(h, &w[0], len + 1);
    if (got <= 0) return "";
    w.resize((size_t)got);
    return ToUtf8(w);
}

} // namespace raduk::core
