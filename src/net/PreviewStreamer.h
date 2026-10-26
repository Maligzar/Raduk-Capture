// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once
// ============================================================
// PreviewStreamer.h -- B3
//
// JPEG-encodes the dashboard preview and hands it to the WebSocket
// server, on a thread of its own.
//
// The render loop calls Submit() with a PreviewImage it has just
// decimated (PreviewFrame.h) and returns at once. The thread keeps
// only the LATEST picture: if encoding or sending falls behind, older
// pictures are replaced, never queued. Nothing here touches the
// capture thread or the recording path, and nothing on the render loop
// waits for it -- so a slow browser costs preview frames, which nobody
// records, and never camera frames.
// ============================================================

#include "PreviewFrame.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace raduk::net {

// BGR24 in, a complete JPEG file out. Empty on any failure. Quality is
// WIC's 0..1 scale. Exposed so tests/preview_frame.cpp can round-trip
// it. Initialises COM on the calling thread if it is not already.
std::vector<uint8_t> EncodeJpeg(const PreviewImage& img, float quality = 0.75f);

class PreviewStreamer {
public:
    using Sink = std::function<void(const std::vector<uint8_t>& jpeg)>;

    PreviewStreamer() = default;
    ~PreviewStreamer() { Stop(); }
    PreviewStreamer(const PreviewStreamer&) = delete;
    PreviewStreamer& operator=(const PreviewStreamer&) = delete;

    void Start(Sink sink);
    void Stop();

    // Replaces whatever is waiting. Never blocks for longer than a swap.
    void Submit(PreviewImage&& img);

    // Counters for the session summary and the test.
    uint64_t Encoded()  const { return m_encoded.load(); }
    uint64_t Replaced() const { return m_replaced.load(); }
    // Last JPEG size, for the log: the figure that decides bandwidth.
    size_t   LastBytes() const { return m_lastBytes.load(); }

private:
    void Run();

    Sink                    m_sink;
    std::thread             m_thread;
    std::mutex              m_mutex;
    std::condition_variable m_cv;
    PreviewImage            m_pending;
    bool                    m_have = false;
    bool                    m_stop = false;
    std::atomic<uint64_t>   m_encoded{0};
    std::atomic<uint64_t>   m_replaced{0};
    std::atomic<size_t>     m_lastBytes{0};
};

} // namespace raduk::net
