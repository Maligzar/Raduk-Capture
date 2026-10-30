// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once
// ============================================================
// WriteBuffer.h — Sequential write ring buffer for Phase 2
// Separate from the preview RingBuffer. The capture thread
// copies frames here; the writer thread drains them to disk
// sequentially. Decouples disk I/O latency from capture.
//
// SPSC: one producer (capture thread via CaptureManager),
//       one consumer (writer thread in CaptureManager).
//
// Key difference from preview RingBuffer:
//   - Consumer reads SEQUENTIALLY (no skip-to-latest)
//   - Never overwrites unread frames — if full, capture
//     manager reports a buffer overrun
//   - Slots are large enough for full-frame RAW16
// ============================================================

#include <cstdint>
#include <cstdio>
#include <atomic>
#include <windows.h>

namespace raduk::core {

struct WriteSlot {
    uint8_t*              data          = nullptr;
    size_t                bufferCapacity = 0;
    size_t                dataSize       = 0;
    uint64_t              timestampUs    = 0;
    uint32_t              width          = 0;
    uint32_t              height         = 0;
    uint8_t               bitDepth       = 8;
    float                 sharpness      = 0.0f;  // Phase 11: lucky imaging score 0-100
    // Phase 76: where to measure sharpness. The writer thread scores each
    // frame itself, but has no idea where the planet is, and measuring the
    // frame centre reads background noise whenever the planet is off
    // centre. The centroid moves slowly next to the 256px crop, so the
    // analyser's last known one positions it well enough.
    uint32_t              cropCX         = 0;
    uint32_t              cropCY         = 0;
    std::atomic<bool>     ready{false};
};

// ── How big should the write buffer be? ─────────────────────
//
// It used to be a flat 512 slots, sized against an assumption written
// into this header: "at 47fps x 1.2MB/frame = ~56MB/s sustained".
// Both halves of that have been overtaken.
//
// Measured on this rig 2026-10-14: the capture drive sustains 292 MB/s
// writing frame-sized chunks the way SERWriter does, but with
// per-second figures ranging 141-423 MB/s. The average is comfortable
// and the dips are not -- 141 MB/s is below what 1280x960 RAW16 at 120
// fps demands, and absorbing exactly that kind of dip is what the
// buffer is FOR.
//
// The other thing that changed is the machine: 64 GB of RAM against a
// 512-slot buffer of 630 MB. A planetary run is bounded -- the capture
// limit is 3000 frames, which is 3.7 GB at 1280x960 RAW8 -- so there is
// no reason the buffer cannot simply hold the entire run. Then a disk
// stall of any length cannot drop a frame, because the disk is never on
// the critical path at all; the writer drains throughout and finishes
// shortly after the camera stops.
//
// Enlarging it does not DELAY any writing. The writer thread drains
// continuously either way; a bigger buffer only raises the ceiling on
// how much stall can be absorbed before frames are lost.
//
//   limitFrames    the run's frame limit, 0 if unbounded
//   limitSeconds   the run's time limit, 0 if unbounded
//   expectedFps    used only to convert a time limit into frames
//   frameBytes     one frame
//   availableBytes free PHYSICAL memory right now
//
// Returns a slot count, never 0.
inline int PlanWriteBufferSlots(uint32_t limitFrames,
                                double   limitSeconds,
                                double   expectedFps,
                                size_t   frameBytes,
                                uint64_t availableBytes) {
    // Never smaller than the old fixed size: this change must not make
    // any existing configuration worse.
    constexpr int FLOOR = 512;

    // Never more than this share of free physical memory. The app is
    // not the only thing running, and a buffer that forces the machine
    // to page has turned a disk problem into a worse disk problem.
    constexpr double RAM_SHARE = 0.5;

    // A backstop for an unbounded run, where "the whole run" has no
    // meaning. 4096 slots is 5 GB at 1280x960 RAW8 -- about 34 seconds
    // of total disk silence at 120 fps, which is far past any plausible
    // stall.
    constexpr int CEILING = 4096;

    if (frameBytes == 0) return FLOOR;

    // What the run actually needs.
    int wanted = 0;
    if (limitFrames > 0) {
        wanted = (int)limitFrames;
    } else if (limitSeconds > 0.0 && expectedFps > 0.0) {
        wanted = (int)(limitSeconds * expectedFps + 0.5);
    }
    if (wanted <= 0) wanted = CEILING;        // unbounded run
    if (wanted > CEILING) wanted = CEILING;
    if (wanted < FLOOR)   wanted = FLOOR;

    // What memory allows. This wins even over the floor -- on a machine
    // that cannot spare 630 MB, allocating it anyway is how you get a
    // failed allocation and no recording at all.
    const uint64_t budget = (uint64_t)(availableBytes * RAM_SHARE);
    int affordable = (int)(budget / frameBytes);
    if (affordable < 1) affordable = 1;

    return (wanted < affordable) ? wanted : affordable;
}

class WriteBuffer {
public:
    WriteBuffer() = default;
    ~WriteBuffer() { Deallocate(); }

    WriteBuffer(const WriteBuffer&) = delete;
    WriteBuffer& operator=(const WriteBuffer&) = delete;

    // Allocate numSlots slots of frameSizeBytes each.
    // numSlots should be large enough to absorb disk write bursts.
    // Sized by PlanWriteBufferSlots() below, not by a constant.
    bool Allocate(int numSlots, size_t frameSizeBytes);

    void Deallocate();

    bool IsAllocated() const { return m_allocated; }

    // Per-slot capacity in bytes (0 when unallocated) — lets the caller
    // detect that a frame-size change requires reallocation (review C3).
    size_t SlotCapacity() const {
        return (m_allocated && m_numSlots > 0) ? m_slots[0].bufferCapacity : 0;
    }

    // ── Producer (capture thread, via CaptureManager) ──────────

    // Try to push a frame. Returns false if buffer is full (overrun).
    // data is memcpy'd into the slot — caller's buffer is not retained.
    bool Push(const uint8_t* data, size_t sizeBytes,
              uint64_t timestampUs,
              uint32_t width, uint32_t height, uint8_t bitDepth,
              float sharpness = 0.0f,
              uint32_t cropCX = 0, uint32_t cropCY = 0);

    // ── Consumer (writer thread) ───────────────────────────────

    // Returns a pointer to the next ready slot, or nullptr if none ready.
    // Caller must call Pop() when done with the slot.
    const WriteSlot* Peek() {
        uint64_t rp = m_readPos.load(std::memory_order_relaxed);
        uint64_t wp = m_writePos.load(std::memory_order_acquire);
        if (rp >= wp) return nullptr;

        int idx = (int)(rp % m_numSlots);
        if (!m_slots[idx].ready.load(std::memory_order_acquire)) return nullptr;
        return &m_slots[idx];
    }

    // Advance read position after consuming a slot from Peek().
    void Pop() {
        uint64_t rp = m_readPos.load(std::memory_order_relaxed);
        int idx = (int)(rp % m_numSlots);
        m_slots[idx].ready.store(false, std::memory_order_release);
        m_readPos.fetch_add(1, std::memory_order_release);
    }

    // How many slots are currently filled (not yet drained)
    uint64_t Pending() const {
        return m_writePos.load(std::memory_order_relaxed)
             - m_readPos.load(std::memory_order_relaxed);
    }

    uint64_t Overruns() const { return m_overruns.load(std::memory_order_relaxed); }
    int NumSlots() const { return m_numSlots; }

private:
    WriteSlot*           m_slots     = nullptr;
    int                  m_numSlots  = 0;
    bool                 m_allocated = false;
    std::atomic<uint64_t> m_writePos{0};
    std::atomic<uint64_t> m_readPos{0};
    std::atomic<uint64_t> m_overruns{0};
};

} // namespace raduk::core
