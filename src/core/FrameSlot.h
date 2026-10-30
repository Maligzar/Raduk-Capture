// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once
// ============================================================
// FrameSlot.h — Frame metadata struct for the ring buffer
// Section 3.2 of Architecture Doc
// ============================================================

#ifdef RADUK_EMULATOR
#  include "ASICamera2Stub.h"
#else
#  include "ASICamera2.h"
#endif
#include <cstdint>
#include <atomic>

namespace raduk::core {

// State machine for each slot in the ring buffer.
// Transitions: FREE → WRITING → READY → READING → FREE
//   WRITING: Capture thread is filling this slot with frame data
//   READY:   Frame data is complete, available for reading
//   READING: GUI/analyzer thread is actively using this slot
//   FREE:    Slot is empty, available for the capture thread
//
// READING state is critical — without it, GetLatestFrame() returns the slot
// to FREE immediately, allowing the capture thread to overwrite it while the
// renderer is still reading it (use-after-free / data race crash).
enum class FrameState : uint8_t {
    FREE = 0,
    WRITING,
    READY,
    READING
};

struct FrameSlot {
    uint8_t*                data = nullptr;     // Pre-allocated frame buffer
    size_t                  bufferCapacity = 0; // Allocated size of data buffer
    size_t                  dataSize = 0;       // Actual bytes written this frame
    uint64_t                timestamp = 0;      // Microsecond timestamp (QPC)
    uint64_t                frameNumber = 0;    // Monotonic frame counter
    uint32_t                width = 0;
    uint32_t                height = 0;
    uint8_t                 bitDepth = 0;       // 8 or 16
    ASI_IMG_TYPE            imgType = ASI_IMG_RAW8;
    std::atomic<FrameState> state{FrameState::FREE};
};

} // namespace raduk::core
