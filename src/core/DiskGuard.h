#pragma once
// ============================================================
//  DiskGuard.h -- phase 135.1: how much free disk a run needs
//
//  The writer checks the output volume's free space once a second and
//  stops the run cleanly below a floor, so the file closes with its
//  header, trailer and sidecar instead of failing part way through a
//  frame. The floor is the frames already captured and waiting in the
//  RAM buffer, plus the SER trailer for every frame (8 bytes of
//  timestamp each), plus room for the sidecars, and never below
//  256 MiB: those frames are the night's, and they must always fit.
//
//  Pure: tests/capture_write_fail.cpp checks it at its edges.
// ============================================================

#include <algorithm>
#include <cstdint>

namespace raduk::core {

inline constexpr uint64_t kDiskFloorMinBytes   = 256ull * 1024 * 1024;
inline constexpr uint64_t kDiskSidecarBytes    = 4ull * 1024 * 1024;   // quality + observation, generous
inline constexpr uint64_t kSerTrailerPerFrame  = 8;                    // one 64-bit timestamp

// pendingFrames: in the write buffer, not yet on disk. framesTotal: the
// run's frames so far, written and pending (the trailer covers them all).
inline uint64_t DiskFloorBytes(uint64_t pendingFrames, uint64_t frameBytes,
                               uint64_t framesTotal) {
    const uint64_t need = pendingFrames * frameBytes
                        + framesTotal * kSerTrailerPerFrame
                        + kDiskSidecarBytes;
    return (std::max)(kDiskFloorMinBytes, need);
}

}  // namespace raduk::core
