// Phase 77: does the app's own launch path actually stack a file?
//
// This question was open for days and answered wrongly twice -- first
// "the in-app launch does not work", then "AutoStakkert's own state is
// wedged, it is not ours". Both were reasoning about a measurement
// nobody could repeat on demand. This is that measurement, on demand.
//
// It drives AutoStakkertRunner exactly as main.cpp does, so what it
// exercises is the real CreateProcessW call, the real argument
// construction and the real wait -- not a reimplementation of them.
//
// MANUAL, and deliberately not in run-tests.ps1: it needs AutoStakkert
// installed and a real recording, and it takes about a minute. A gate
// that cannot run on a clean checkout is not a gate (see tests\README).
//
//   cl /nologo /std:c++20 /EHsc /O2 /I ..\src manual-autostakkert-launch.cpp
//   manual-autostakkert-launch.exe "D:\RadukCapture\captures\<file>.ser"
//
// Expect, for a 158 MB 1280x960 recording on this rig: ok=1, 8 files,
// about 50 s. Then check AutoStakkert's own log --
// debug_data\log.txt -- for the line that distinguishes the two
// failure modes in one glance:
//
//   "Processing started" ~0.45 s after "started..."   -> it ran
//   switches logged, no "Processing started"          -> AS did not begin
//   NO "command line ..." lines at all                -> the switches
//       never arrived: almost certainly launched from Git Bash or
//       another MSYS shell, which rewrites /apgrid into a path before
//       AutoStakkert sees it. Use PowerShell or cmd.
//
// Output lands in <recording>_AS, beside the recording, because that
// is where the feature puts it in normal use.

#include "core/AutoStakkertRunner.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        printf("usage: manual-autostakkert-launch <recording.ser>\n");
        return 2;
    }

    raduk::core::AutoStakkertRunner runner;
    if (!runner.IsAvailable()) {
        printf("[FAIL] AutoStakkert not found by the path probe\n");
        return 1;
    }
    printf("exe: %ls\n", runner.Path().c_str());

    // The same logger hook main.cpp wires to the session log. The
    // command line is the first thing to check when a launch goes
    // wrong, and it has been wrong in ways only a log revealed.
    runner.SetLogger([](const std::string& line) {
        printf("%s\n", line.c_str());
    });

    std::atomic<bool> done{false};
    raduk::core::StackResult result;

    const auto t0 = std::chrono::steady_clock::now();
    runner.StackAsync(argv[1], [&](raduk::core::StackResult r) {
        result = r;
        done.store(true);
    });

    // Ticks, so a wedged run looks wedged rather than merely quiet --
    // which is how this was mistaken for impatience the first time.
    int secs = 0;
    while (!done.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        secs += 5;
        printf("  %4ds  running=%d\n", secs, runner.IsRunning() ? 1 : 0);
        if (secs > 900) { printf("[FAIL] still running at 15 min\n"); break; }
    }

    const double wall = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count();

    printf("\n%s  ok=%d  files=%d  seconds=%.1f  wall=%.1f\n",
           result.ok ? "[PASS]" : "[FAIL]",
           result.ok ? 1 : 0, result.filesProduced, result.seconds, wall);
    printf("outputDir: %ls\n", result.outputDir.c_str());
    if (!result.error.empty()) printf("error: %s\n", result.error.c_str());
    return result.ok ? 0 : 1;
}
