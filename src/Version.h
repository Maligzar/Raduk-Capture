// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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

#define RADUK_VERSION_W  L"2.4.0"
#define RADUK_VERSION_A   "2.4.0"
