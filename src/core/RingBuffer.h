#pragma once
// ============================================================
// RingBuffer.h — Lock-free SPSC ring buffer for RadukCapture
// Section 3.2 of Architecture Doc
//
// Single Producer (capture thread) writes frames.
// Consumers read or skip to latest:
//   - Writer thread reads sequentially (Phase 2)
//   - Analyzer thread samples (Phase 3)
//   - GUI thread always skips to latest for preview (Milestone 3)
// ============================================================

#include "FrameSlot.h"
#include <cstdint>
#include <cstdio>
#include <windows.h>

namespace raduk::core {

// Default ring size — 64 slots is good for planetary ROI sizes (~256 MB).
// Can increase to 128 for full-frame RAW16 if RAM allows (~2 GB).
static constexpr int DEFAULT_RING_SIZE = 64;

class RingBuffer {
public:
    RingBuffer() = default;
    ~RingBuffer() { Deallocate(); }

    // Non-copyable
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    // ── Allocation ─────────────────────────────────────────────

    // Allocate all frame buffers for the given frame size.
    // Call this once at camera init, or when ROI/format changes.
    // frameSizeBytes = width * height * bytesPerPixel
    bool Allocate(int numSlots, size_t frameSizeBytes) {
        Deallocate();

        m_numSlots = numSlots;
        m_slots = new (std::nothrow) FrameSlot[numSlots];
        if (!m_slots) {
            printf("[RingBuffer] Failed to allocate %d slots\n", numSlots);
            return false;
        }

        for (int i = 0; i < numSlots; i++) {
            // Use VirtualAlloc for large page-aligned buffers
            m_slots[i].data = static_cast<uint8_t*>(
                VirtualAlloc(nullptr, frameSizeBytes,
                             MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (!m_slots[i].data) {
                printf("[RingBuffer] VirtualAlloc failed for slot %d (%zu bytes)\n",
                       i, frameSizeBytes);
                Deallocate();
                return false;
            }
            m_slots[i].bufferCapacity = frameSizeBytes;
            m_slots[i].state.store(FrameState::FREE, std::memory_order_relaxed);
        }

        m_writePos.store(0, std::memory_order_relaxed);
        m_totalFrames.store(0, std::memory_order_relaxed);
        m_overruns.store(0, std::memory_order_relaxed);
        m_allocated = true;

        double totalMB = (numSlots * frameSizeBytes) / (1024.0 * 1024.0);
        printf("[RingBuffer] Allocated %d slots x %zu bytes = %.1f MB\n",
               numSlots, frameSizeBytes, totalMB);
        return true;
    }

    // Free all frame buffers.
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
        m_numSlots = 0;
        m_allocated = false;
    }

    bool IsAllocated() const { return m_allocated; }
    int NumSlots() const { return m_numSlots; }

    // ── Producer (capture thread) ──────────────────────────────

    // Acquire the next slot for writing, overwriting the oldest unread
    // frame when the ring has wrapped. Returns nullptr only if every slot
    // is actively in use (consumer READING / a stuck WRITING) — drop the
    // frame in that case.
    //
    // Phase 56 (review C1): every slot claim is a CAS. The previous code
    // did an unconditional state.store(WRITING) after a failed CAS, racing
    // the consumer's READY→READING transition — the consumer could acquire
    // the slot between our load and store and get its frame stomped
    // mid-read (torn preview frames AND torn SER writes). The skip path
    // also desynced m_writePos from the slot actually written.
    FrameSlot* AcquireWriteSlot() {
        uint64_t pos = m_writePos.load(std::memory_order_relaxed);
        for (int k = 0; k < m_numSlots; k++) {
            int idx = static_cast<int>((pos + k) % m_numSlots);
            FrameSlot* slot = &m_slots[idx];

            FrameState expected = FrameState::FREE;
            if (slot->state.compare_exchange_strong(expected, FrameState::WRITING,
                    std::memory_order_acquire, std::memory_order_relaxed)) {
                m_acquiredOffset = k;
                if (k > 0) m_overruns.fetch_add(1, std::memory_order_relaxed);
                return slot;
            }
            // Overwrite an unread (READY) frame — CAS so a consumer that
            // just claimed it for READING wins and we move on.
            expected = FrameState::READY;
            if (slot->state.compare_exchange_strong(expected, FrameState::WRITING,
                    std::memory_order_acquire, std::memory_order_relaxed)) {
                m_acquiredOffset = k;
                m_overruns.fetch_add(1, std::memory_order_relaxed);
                return slot;
            }
            // READING or WRITING — never stomp; try the next slot.
        }
        return nullptr; // every slot busy — caller drops this frame
    }

    // Mark the slot as READY (frame data is complete) and advance write position.
    void CommitWrite(FrameSlot* slot) {
        slot->state.store(FrameState::READY, std::memory_order_release);
        // Advance so (m_writePos − 1) % numSlots is the slot just written —
        // keeps GetLatestFrame's newest-first scan anchored even when
        // AcquireWriteSlot skipped over busy slots.
        m_writePos.fetch_add(m_acquiredOffset + 1, std::memory_order_release);
        m_acquiredOffset = 0;
        m_totalFrames.fetch_add(1, std::memory_order_relaxed);
    }

    // Release the slot back to FREE without committing (e.g. on SDK error).
    void ReleaseWrite(FrameSlot* slot) {
        slot->state.store(FrameState::FREE, std::memory_order_release);
        m_acquiredOffset = 0;
    }

    // ── Consumer (GUI / analyzer) ──────────────────────────────

    // Get the most recent READY frame (for preview — skips to latest).
    // Transitions the slot to READING state so the capture thread won't
    // overwrite it while the caller is using it.
    // Returns nullptr if no frames are ready.
    // Caller MUST call ReleaseRead() when done with the frame.
    FrameSlot* GetLatestFrame() {
        uint64_t wp = m_writePos.load(std::memory_order_acquire);
        for (int i = 0; i < m_numSlots; i++) {
            int idx = static_cast<int>((wp - 1 - i) % m_numSlots);
            if (idx < 0) idx += m_numSlots;
            FrameSlot* slot = &m_slots[idx];

            FrameState expected = FrameState::READY;
            // Transition READY → READING (not FREE — caller holds it)
            if (slot->state.compare_exchange_strong(expected, FrameState::READING,
                    std::memory_order_acquire, std::memory_order_relaxed)) {
                return slot;
            }
        }
        return nullptr;
    }

    // Release a slot previously returned by GetLatestFrame() back to FREE.
    // Must be called after the caller is done reading the frame data.
    void ReleaseRead(FrameSlot* slot) {
        if (slot) {
            slot->state.store(FrameState::FREE, std::memory_order_release);
        }
    }

    // ── Stats ──────────────────────────────────────────────────

    uint64_t TotalFrames() const { return m_totalFrames.load(std::memory_order_relaxed); }
    uint64_t Overruns() const { return m_overruns.load(std::memory_order_relaxed); }

private:
    FrameSlot*              m_slots = nullptr;
    int                     m_numSlots = 0;
    bool                    m_allocated = false;
    std::atomic<uint64_t>   m_writePos{0};
    std::atomic<uint64_t>   m_totalFrames{0};
    std::atomic<uint64_t>   m_overruns{0};
    // Producer-thread-only: slots skipped by the last AcquireWriteSlot,
    // consumed by CommitWrite to keep m_writePos anchored (SPSC — no atomics)
    int                     m_acquiredOffset = 0;
};

} // namespace raduk::core
