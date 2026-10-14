// ============================================================
// WriteBuffer.cpp — Sequential write ring buffer
// Out-of-line definitions for WriteBuffer.h.
// ============================================================

#include "WriteBuffer.h"
#include <cstring>
#include <new>

namespace raduk::core {

bool WriteBuffer::Allocate(int numSlots, size_t frameSizeBytes) {
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

    // Touch every page now, while nothing is being captured.
    //
    // MEM_COMMIT reserves and commits but the pages stay demand-zero
    // until first written, so the first pass through the buffer pays a
    // soft fault per 4K page. At a few hundred slots that was invisible;
    // sized to a whole run it is roughly a million faults landing in the
    // middle of the capture they are supposed to be protecting. Paying
    // it here costs a fraction of a second at a moment when nothing is
    // waiting on us.
    for (int i = 0; i < numSlots; i++)
        memset(m_slots[i].data, 0, frameSizeBytes);

    m_writePos.store(0, std::memory_order_relaxed);
    m_readPos.store(0, std::memory_order_relaxed);
    m_overruns.store(0, std::memory_order_relaxed);
    m_allocated = true;

    double mb = (numSlots * frameSizeBytes) / (1024.0 * 1024.0);
    printf("[WriteBuffer] Allocated %d slots x %zu bytes = %.0f MB\n",
           numSlots, frameSizeBytes, mb);
    return true;
}

void WriteBuffer::Deallocate() {
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

bool WriteBuffer::Push(const uint8_t* data, size_t sizeBytes,
                       uint64_t timestampUs,
                       uint32_t width, uint32_t height, uint8_t bitDepth,
                       float sharpness,
                       uint32_t cropCX, uint32_t cropCY) {

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
    slot.cropCX      = cropCX;
    slot.cropCY      = cropCY;
    slot.ready.store(true, std::memory_order_release);

    m_writePos.fetch_add(1, std::memory_order_release);
    return true;
}

} // namespace raduk::core
