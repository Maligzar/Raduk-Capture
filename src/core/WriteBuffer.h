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

class WriteBuffer {
public:
    WriteBuffer() = default;
    ~WriteBuffer() { Deallocate(); }

    WriteBuffer(const WriteBuffer&) = delete;
    WriteBuffer& operator=(const WriteBuffer&) = delete;

    // Allocate numSlots slots of frameSizeBytes each.
    // numSlots should be large enough to absorb disk write bursts.
    // At 47fps x 1.2MB/frame = ~56MB/s sustained. With 512 slots
    // (~600MB) we can absorb ~10 seconds of stall before overrun.
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
