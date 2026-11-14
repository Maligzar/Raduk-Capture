// ============================================================
//  The frame ring only goes forward (phase 161.1)
//
//  The reader takes the newest frame each pass. When the camera outruns
//  it, the frames in between stay READY in the ring, and GetLatestFrame's
//  newest-first scan used to hand one of them back whenever a pass found
//  nothing new: the preview stepped back in time, and autofocus's fine
//  pass could score a frame taken before the focuser had settled.
//
//  1. Single-threaded, exact: ten frames written, the newest read, and
//     then nothing until an eleventh arrives.
//  2. Two threads: a producer faster than the reader, with pauses. Every
//     frame the reader is given is newer than the one before.
//  3. A capture restart (frame numbers begin again at 0) does not freeze
//     the reader, and a re-allocation starts clean.
// ============================================================

#include "core/RingBuffer.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using namespace raduk::core;

static int g_fail = 0;
static void Check(bool ok, const char* what, const std::string& detail = "") {
    std::printf("  [%s] %-66s %s\n", ok ? "PASS" : "FAIL", what, detail.c_str());
    if (!ok) ++g_fail;
}
static std::string N(long long v) { return std::to_string(v); }

static bool Write(RingBuffer& ring, uint64_t frameNumber) {
    FrameSlot* s = ring.AcquireWriteSlot();
    if (!s) return false;
    s->frameNumber = frameNumber;
    s->data[0] = (uint8_t)frameNumber;
    s->dataSize = 16;
    ring.CommitWrite(s);
    return true;
}

int main() {
    std::printf("The frame ring only goes forward (phase 161.1)\n");

    std::printf("\n1. Ten frames, one reader\n");
    {
        RingBuffer ring;
        ring.Allocate(16, 16);
        Check(ring.GetLatestFrame() == nullptr, "an empty ring gives nothing");
        for (uint64_t i = 1; i <= 10; ++i) Write(ring, i);
        FrameSlot* f = ring.GetLatestFrame();
        Check(f && f->frameNumber == 10, "the newest of ten", f ? N((long long)f->frameNumber) : "none");
        ring.ReleaseRead(f);
        int older = 0;
        for (int k = 0; k < 20; ++k)
            if (FrameSlot* g = ring.GetLatestFrame()) { ++older; ring.ReleaseRead(g); }
        Check(older == 0, "then nothing: frames 1 to 9 are not handed back", N(older) + " older frames");
        Write(ring, 11);
        f = ring.GetLatestFrame();
        Check(f && f->frameNumber == 11, "the eleventh, when it arrives");
        ring.ReleaseRead(f);
        // Wrap the ring several times over.
        uint64_t last = 11; bool forward = true; int got = 0;
        for (uint64_t i = 12; i < 400; ++i) {
            Write(ring, i);
            if (i % 3 == 0) {
                for (int k = 0; k < 2; ++k)
                    if (FrameSlot* g = ring.GetLatestFrame()) {
                        if (g->frameNumber <= last) forward = false;
                        last = g->frameNumber; ++got;
                        ring.ReleaseRead(g);
                    }
            }
        }
        Check(forward && got > 100, "wrapped 24 times, read every third frame twice: always forward",
              N(got) + " frames");
    }

    std::printf("\n2. A producer that outruns the reader, and pauses\n");
    {
        RingBuffer ring;
        ring.Allocate(64, 16);
        std::atomic<bool> run{true};
        std::atomic<uint64_t> written{0};
        std::thread producer([&] {
            uint64_t n = 0;
            while (run.load()) {
                // Bursts, then a gap: the reader's passes land in both.
                for (int k = 0; k < 40 && run.load(); ++k) { if (Write(ring, ++n)) written.store(n); }
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
        });
        uint64_t last = 0; long long got = 0, backward = 0;
        const auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1500)) {
            if (FrameSlot* f = ring.GetLatestFrame()) {
                if (f->frameNumber <= last) ++backward;
                last = f->frameNumber; ++got;
                ring.ReleaseRead(f);
            }
            std::this_thread::yield();
        }
        run.store(false);
        producer.join();
        Check(backward == 0, "no frame older than the one before it", N(backward) + " went back");
        Check(got > 100 && written.load() > (uint64_t)got,
              "and the reader really was outrun",
              N(got) + " read of " + N((long long)written.load()) + " written");
    }

    std::printf("\n3. A capture restart, and a re-allocation\n");
    {
        RingBuffer ring;
        ring.Allocate(16, 16);
        for (uint64_t i = 100; i < 110; ++i) Write(ring, i);
        FrameSlot* f = ring.GetLatestFrame();
        ring.ReleaseRead(f);
        // Capture restarts: its frame counter begins again.
        Write(ring, 0); Write(ring, 1);
        f = ring.GetLatestFrame();
        Check(f && f->frameNumber == 1, "frame numbers beginning again do not freeze the reader",
              f ? N((long long)f->frameNumber) : "none");
        ring.ReleaseRead(f);
        ring.Allocate(16, 16);
        Write(ring, 7);
        f = ring.GetLatestFrame();
        Check(f && f->frameNumber == 7, "a re-allocated ring starts clean");
        ring.ReleaseRead(f);
    }

    std::printf("\n%s\n", g_fail ? "FAILED" : "ring_monotonic: all passed");
    return g_fail ? 1 : 0;
}
