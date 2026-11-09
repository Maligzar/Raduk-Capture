#pragma once
// ============================================================
// CrashDiag.h -- say what crashed, not only that something did
//
// 2026-11-09: the app crashed at launch and the log said only
//
//     [CRASH] Exception 0xE06D7363 at 00007FF86E52055C
//
// 0xE06D7363 is MSVC's code for a C++ exception nobody caught, and the
// address is RaiseException inside KernelBase, which is where every
// throw lands. Neither says what threw. And the log was opened with
// "w", so relaunching to look again erased it.
//
// Three things, all called from main.cpp's crash handler or LogInit:
//
//   RotateLog       the previous run's log is kept as <name>.prev.txt,
//                   so a crash survives the next launch.
//   CxxException    for 0xE06D7363, the thrown type ("std::out_of_range")
//                   and, when it is a std::exception, what() -- read from
//                   the throw information MSVC attaches to the record.
//   StackLines      the stack at the throw, while the filter still runs
//                   on top of it: module+offset for every frame, and the
//                   function where a PDB is beside the exe (CMakeLists
//                   builds one for Release, names only; file:line too in
//                   a Debug build, or with /Zi, which costs compiler memory).
//
// The handler runs in a process that is already failing, so every step
// is defensive and best-effort; a step that cannot run says so and the
// next one still runs.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

namespace raduk::core::crash {

inline constexpr DWORD kCxxException = 0xE06D7363;

// The previous log, kept beside it. Call before the log is opened.
inline void RotateLog(const std::string& path) {
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    std::string prev = path;
    const size_t dot = prev.find_last_of('.');
    const size_t slash = prev.find_last_of("\\/");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) prev.erase(dot);
    prev += ".prev.txt";
    MoveFileExA(path.c_str(), prev.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// ".?AVout_of_range@std@@" -> "std::out_of_range". Pure, tested.
inline std::string TypeNameFromDecorated(const char* d) {
    if (!d) return {};
    std::string s(d);
    for (const char* p : {".?AV", ".?AU", ".?AW4"})
        if (s.rfind(p, 0) == 0) { s.erase(0, strlen(p)); break; }
    while (!s.empty() && s.back() == '@') s.pop_back();
    std::vector<std::string> parts;
    size_t at = 0;
    while (at <= s.size()) {
        const size_t e = s.find('@', at);
        parts.push_back(s.substr(at, e == std::string::npos ? std::string::npos : e - at));
        if (e == std::string::npos) break;
        at = e + 1;
    }
    std::string out;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (it->empty()) continue;
        if (!out.empty()) out += "::";
        out += *it;
    }
    return out.empty() ? std::string(d) : out;
}

// The thrown type and message of an uncaught MSVC C++ exception, or ""
// when the record is not one (or this is not an MSVC x64 build).
inline std::string CxxException(const EXCEPTION_RECORD* r) {
#if defined(_MSC_VER) && defined(_M_X64)
    if (!r || r->ExceptionCode != kCxxException || r->NumberParameters < 4) return {};
    // MSVC's throw information, x64 layout (image-relative offsets).
    struct TypeDescriptor { const void* vft; void* spare; char name[1]; };
    struct PMD { int mdisp, pdisp, vdisp; };
    struct CatchableType { unsigned props; int pType; PMD disp; int size; int copy; };
    struct CatchableTypeArray { int count; int types[1]; };
    struct ThrowInfo { unsigned attrs; int unwind; int forward; int catchables; };
    const auto obj  = reinterpret_cast<const char*>(r->ExceptionInformation[1]);
    const auto ti   = reinterpret_cast<const ThrowInfo*>(r->ExceptionInformation[2]);
    const auto base = static_cast<uintptr_t>(r->ExceptionInformation[3]);
    if (!ti || !base || !ti->catchables) return "C++ exception (no type information)";
    const auto arr = reinterpret_cast<const CatchableTypeArray*>(base + ti->catchables);
    std::string type, what;
    for (int i = 0; i < arr->count && i < 32; ++i) {
        const auto ct = reinterpret_cast<const CatchableType*>(base + arr->types[i]);
        const auto td = reinterpret_cast<const TypeDescriptor*>(base + ct->pType);
        if (i == 0) type = TypeNameFromDecorated(td->name);
        // Any base that is std::exception gives what(). A simple, non-virtual
        // base (pdisp -1) is the only case handled; others are named only.
        if (obj && ct->disp.pdisp == -1 && strcmp(td->name, ".?AVexception@std@@") == 0) {
            const auto e = reinterpret_cast<const std::exception*>(obj + ct->disp.mdisp);
            const char* w = e->what();
            if (w) what = w;
        }
    }
    if (type.empty()) type = "C++ exception";
    return what.empty() ? type : type + ": " + what;
#else
    (void)r;
    return {};
#endif
}

// The stack at the call, one line per frame: function and file:line
// where symbols are available, else module+offset.
inline std::vector<std::string> StackLines(int maxFrames = 40) {
    std::vector<std::string> out;
    void* frames[64] = {};
    const USHORT n = RtlCaptureStackBackTrace(1, (DWORD)(maxFrames < 64 ? maxFrames : 64), frames, nullptr);
    HANDLE proc = GetCurrentProcess();
    static bool symInit = false;
    if (!symInit) {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
        symInit = SymInitialize(proc, nullptr, TRUE) != FALSE;
    }
    for (USHORT i = 0; i < n; ++i) {
        const DWORD64 a = reinterpret_cast<DWORD64>(frames[i]);
        char modPath[MAX_PATH] = "?";
        HMODULE mod = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(frames[i]), &mod))
            GetModuleFileNameA(mod, modPath, MAX_PATH);
        const char* modName = strrchr(modPath, '\\');
        modName = modName ? modName + 1 : modPath;
        char line[512];
        snprintf(line, sizeof line, "%2u  %s+0x%llx", (unsigned)i, modName,
                 (unsigned long long)(a - reinterpret_cast<DWORD64>(mod)));
        std::string s = line;
        if (symInit) {
            alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256] = {};
            auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
            sym->MaxNameLen = 255;
            DWORD64 disp = 0;
            if (SymFromAddr(proc, a, &disp, sym)) {
                s += "  ";
                s += sym->Name;
                IMAGEHLP_LINE64 ln = {};
                ln.SizeOfStruct = sizeof(ln);
                DWORD ldisp = 0;
                if (SymGetLineFromAddr64(proc, a, &ldisp, &ln) && ln.FileName) {
                    const char* f = strrchr(ln.FileName, '\\');
                    snprintf(line, sizeof line, "  (%s:%lu)", f ? f + 1 : ln.FileName, ln.LineNumber);
                    s += line;
                }
            }
        }
        out.push_back(s);
    }
    return out;
}

} // namespace raduk::core::crash
