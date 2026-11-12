#pragma once
// ============================================================
// ThreadName.h -- phase 135.5: threads a debugger can tell apart
//
// SetThreadDescription names the calling thread for Visual Studio's
// Threads window and for crash dumps, so "the writer thread" is not one
// of eight unnamed workers. Looked up at run time: it exists from
// Windows 10 1607, and a missing entry point must cost the name, never
// the start of a recording.
// ============================================================

#include <windows.h>

namespace raduk::core {

inline void NameThisThread(const wchar_t* name) {
    using Fn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    static const Fn fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription")));
    if (fn && name) fn(GetCurrentThread(), name);
}

} // namespace raduk::core
