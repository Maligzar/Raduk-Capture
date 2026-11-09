// ============================================================
//  2026-11-09: the crash handler says what threw
//
//  A launch crash was logged as "Exception 0xE06D7363 at <address in
//  KernelBase>", which says only that some C++ exception went uncaught.
//  CrashDiag.h reads the thrown type and what() from the exception
//  record, walks the stack, and keeps the previous log. Checked here:
//
//  1. Decorated type names come out readable.
//  2. (MSVC) A real std::out_of_range, caught as the SEH exception the
//     crash filter sees, gives back its type and its message.
//  3. A stack can be taken, and names this test's own module.
//  4. RotateLog keeps the old log as .prev.txt and replaces an old one.
// ============================================================

#include "core/CrashDiag.h"

#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

using namespace raduk::core::crash;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-56s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}

#if defined(_MSC_VER) && defined(_M_X64)
static void Thrower() { throw std::out_of_range("index 7 past 3"); }
static int Filter(EXCEPTION_POINTERS* ep, std::string* out) {
    *out = CxxException(ep->ExceptionRecord);
    return EXCEPTION_EXECUTE_HANDLER;
}
// No objects with destructors here: __try cannot share a frame with them.
static void Probe(std::string* out) {
    __try { Thrower(); }
    __except (Filter(GetExceptionInformation(), out)) {}
}
#endif

int main() {
    printf("\n=== Crash diagnostics ===\n");

    printf("\n1. Type names\n");
    Check(TypeNameFromDecorated(".?AVout_of_range@std@@") == "std::out_of_range", "std::out_of_range");
    Check(TypeNameFromDecorated(".?AVexception@std@@") == "std::exception", "std::exception");
    Check(TypeNameFromDecorated(".?AVfilesystem_error@filesystem@std@@") ==
              "std::filesystem::filesystem_error",
          "a nested namespace");
    Check(TypeNameFromDecorated(".?AUMyErr@@") == "MyErr", "a struct at global scope");

    printf("\n2. A real throw\n");
#if defined(_MSC_VER) && defined(_M_X64)
    {
        std::string got;
        Probe(&got);
        Check(got == "std::out_of_range: index 7 past 3", "type and what() read from the record", got);
    }
#else
    Check(true, "(MSVC x64 only; skipped here)");
#endif
    {
        EXCEPTION_RECORD r = {};
        r.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
        Check(CxxException(&r).empty(), "not a C++ exception: nothing to say");
    }

    printf("\n3. The stack\n");
    {
        const auto lines = StackLines(8);
        bool ours = false;
        for (const auto& l : lines) if (l.find(".exe+0x") != std::string::npos) ours = true;
        Check(!lines.empty() && ours, "frames come back, this exe's among them",
              lines.empty() ? "" : lines[0]);
    }

    printf("\n4. The previous log is kept\n");
    {
        const std::string log = "crash_diag_test.txt", prev = "crash_diag_test.prev.txt";
        { std::ofstream(prev) << "older"; }
        { std::ofstream(log) << "last run"; }
        RotateLog(log);
        std::ifstream p(prev);
        std::string s((std::istreambuf_iterator<char>(p)), {});
        p.close();
        Check(s == "last run" && GetFileAttributesA(log.c_str()) == INVALID_FILE_ATTRIBUTES,
              "the last run becomes .prev.txt, replacing the one before", s);
        RotateLog(log);   // nothing to rotate: no error, prev untouched
        std::ifstream p2(prev);
        std::string s2((std::istreambuf_iterator<char>(p2)), {});
        Check(s2 == "last run", "with no log, nothing changes");
        p2.close();
        DeleteFileA(prev.c_str());
    }

    printf("\n%s\n", g_fail ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return g_fail ? 1 : 0;
}
