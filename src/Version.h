#pragma once
// ============================================================
// Version.h — build identification for RadukCapture
//
// The title bar shows the version together with the compile timestamp
// of main.cpp, so a stale exe (launched from the wrong build dir, or
// run before a rebuild finished) is immediately obvious.
//
// ── How it moves (the owner's rule, 2026-11-12) ─────────────
//
//   PATCH  every pull request bumps it (3.1.0 -> 3.1.1), and adds its
//          line to the Phase History in RadukCapture-Help.md.
//   MINOR  when a phase is complete, the PR that completes it bumps
//          MINOR and resets PATCH (3.1.4 -> 3.2.0).
//   MAJOR  and a git tag (vMAJOR.MINOR): the owner's call, at a release.
//
// CI enforces it: on a pull request, .github/workflows/build.yml fails
// unless this version is higher than the base branch's. When two PRs
// in flight both bump, the second to merge conflicts on this file;
// resolve it to the higher of the two plus one PATCH.
//
// It said "bumped on EVERY pushed iteration" and stayed at 3.0.0 from
// #73 through #144, about seventy merges. 3.1.0 is the catch-up.
//
// The compile timestamp refreshes automatically whenever
// main.cpp recompiles; bumping the version here forces that
// recompile, so the two always move together.
// ============================================================

#define RADUK_VERSION_W  L"3.1.1"
#define RADUK_VERSION_A   "3.1.1"
