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
    bool Allocate(int numSlots, size_t frameSizeBytes) {
        Deallocate();
        m_numSlots = numSlots;
        m_slots = new (std::nothrow) WriteSlot[numSlots];
        if (!m_slots) return false;

        for (int i = 0; i < numSlots; i++) {
            m_slots[i].data = static_cast<uint8_t*>(
                VirtualAlloc(nullptr, frameSizeBytes,
                             MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (!m_slots[i].data) {
                printf("[WriteBuffer] VirtualAlloc failed slot %d\n", i);
                Deallocate(); return false;
            }
            m_slots[i].bufferCapacity = frameSizeBytes;
            m_slots[i].ready.store(false, std::memory_order_relaxed);
        }

        m_writePos.store(0, std::memory_order_relaxed);
        m_readPos.store(0, std::memory_order_relaxed);
        m_overruns.store(0, std::memory_order_relaxed);
        m_allocated = true;

        double mb = (numSlots * frameSizeBytes) / (1024.0 * 1024.0);
        printf("[WriteBuffer] Allocated %d slots x %zu bytes = %.0f MB\n",
               numSlots, frameSizeBytes, mb);
        return true;
    }

    void Deallocate() {
        if (m_slots) {
            for (int i = 0; i < m_numSlots; i++) {
                if (m_slots[i].data) {
                    VirtualFree(m_slots[i].data, 0, MEM_RELEASE);
                    m_slots[i].data = nullptr;
                }
            }
            delete[] m_slots;
            m_slots = nullptr;
        }
        m_numSlots  = 0;
        m_allocated = false;
    }

    bool IsAllocated() const { return m_allocated; }

    // ── Producer (capture thread, via CaptureManager) ──────────

    // Try to push a frame. Returns false if buffer is full (overrun).
    // data is memcpy'd into the slot — caller's buffer is not retained.
    bool Push(const uint8_t* data, size_t sizeBytes,
              uint64_t timestampUs,
              uint32_t width, uint32_t height, uint8_t bitDepth,
              float sharpness = 0.0f) {

        uint64_t wp = m_writePos.load(std::memory_order_relaxed);
        uint64_t rp = m_readPos.load(std::memory_order_acquire);

        // Full check: if write is numSlots ahead of read, buffer is full
        if ((wp - rp) >= (uint64_t)m_numSlots) {
            m_overruns.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        int idx = (int)(wp % m_numSlots);
        WriteSlot& slot = m_slots[idx];

        if (sizeBytes > slot.bufferCapacity) {
            printf("[WriteBuffer] Frame too large: %zu > %zu\n",
                   sizeBytes, slot.bufferCapacity);
            return false;
        }

        memcpy(slot.data, data, sizeBytes);
        slot.dataSize    = sizeBytes;
        slot.timestampUs = timestampUs;
        slot.width       = width;
        slot.height      = height;
        slot.bitDepth    = bitDepth;
        slot.sharpness   = sharpness;
        slot.ready.store(true, std::memory_order_release);

        m_writePos.fetch_add(1, std::memory_order_release);
        return true;
    }

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
