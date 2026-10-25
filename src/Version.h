#pragma once
// ============================================================
// Version.h — build identification for RadukCapture
//
// RADUK_VERSION is bumped on EVERY pushed iteration, and the
// title bar shows it together with the compile timestamp of
// main.cpp — so a stale exe (launched from the wrong build dir,
// or run before a rebuild finished) is immediately obvious.
//
// The compile timestamp refreshes automatically whenever
// main.cpp recompiles; bumping the version here forces that
// recompile, so the two always move together.
// ============================================================

#define RADUK_VERSION_W  L"2.3.0"
#define RADUK_VERSION_A   "2.3.0"
