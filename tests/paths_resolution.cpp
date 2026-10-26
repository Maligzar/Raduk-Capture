// ============================================================
//  Where the application decides to keep its files
//
//  Fifteen sites in src/ each carried their own copy of
//  "D:\RadukCapture\...". That is the largest single reason this
//  build does not run on a second machine, and it is not a
//  configuration problem: there was nothing to configure.
//
//  Paths::Decide is the whole policy, lifted out of the three
//  ways its inputs are discovered so it can be checked with no
//  ini file, no registry and no D: drive.
//
//  The case that matters most is the second one. The rig this was
//  written on must keep working untouched, with no ini edit and no
//  files moved, or the change is not safe to take. The case that
//  matters next is the one under it: a machine with no
//  D:\RadukCapture must never be handed that path anyway.
// ============================================================

#include "core/Paths.h"

#include <cstdio>
#include <string>

using namespace raduk::core;

static int g_fail = 0;

static void Check(const std::string& got, const std::string& want, const char* what) {
    if (got == want) {
        printf("  ok    %s\n", what);
    } else {
        printf("  FAIL  %s: got \"%s\", wanted \"%s\"\n", what, got.c_str(), want.c_str());
        ++g_fail;
    }
}

int main() {
    printf("Paths: where the data root resolves to\n");

    const std::string exe = "C:\\Tools\\RadukCapture";

    // 1. An explicit DataRoot wins over everything, including a
    //    legacy tree that exists.
    Check(Paths::Decide("E:\\Astro", true,  exe), "E:\\Astro",
          "DataRoot in rig.ini wins over an existing D:\\RadukCapture");
    Check(Paths::Decide("E:\\Astro", false, exe), "E:\\Astro",
          "DataRoot in rig.ini wins with no legacy tree");

    // 2. The migration. No ini setting, but the original tree is
    //    there, so nothing moves and nothing has to be edited.
    Check(Paths::Decide("", true, exe), "D:\\RadukCapture",
          "an existing D:\\RadukCapture is kept when nothing is configured");

    // 3. A machine that never had one falls back beside the exe,
    //    not onto somebody else's D: drive.
    Check(Paths::Decide("", false, exe), exe,
          "no ini and no legacy tree falls back to the exe directory");

    // Trailing separators must not survive into the root, or every
    // path built from it gets a doubled backslash.
    Check(Paths::Decide("E:\\Astro\\", false, exe), "E:\\Astro",
          "a trailing backslash is stripped");
    Check(Paths::Decide("E:/Astro/", false, exe), "E:/Astro",
          "a trailing forward slash is stripped");
    Check(Paths::Decide("E:\\Astro\\\\", false, exe), "E:\\Astro",
          "several trailing separators are stripped");

    // A drive root is the exception: "D:" means the current
    // directory on D:, which is not what anyone typing "D:\" meant.
    Check(Paths::Decide("D:\\", false, exe), "D:\\",
          "a bare drive root keeps its separator");

    if (g_fail == 0) printf("PASS\n"); else printf("FAIL: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
