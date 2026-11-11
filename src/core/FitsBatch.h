#pragma once
// ============================================================
// FitsBatch.h -- convert several FITS cubes to SER, off the UI thread
//
// 2026-11-11: the owner had several FITS cubes to turn into SER for
// PIPP and asked for a button rather than a command line. Each cube is
// tens of gigabytes and minutes of disk, so the conversion runs on its
// own thread, one file after another, and the UI reads Status() on its
// tick. Cancel() stops between frames and removes the partial SER; the
// destructor cancels and joins, so closing the app never leaves a
// half-written file or a thread writing after exit.
//
// Pure C++ (std::thread), tested in tests/fits_batch.cpp.
// ============================================================

#include "FitsToSer.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace raduk::core::fits2ser {

class Batch {
public:
    struct Status {
        bool running = false;
        int  index = 0, total = 0;     // 1-based file being converted
        int  done = 0, failed = 0;
        std::string file, line;        // current file name, its last report
        std::string folder;            // where the last SER went
        unsigned version = 0;          // changes whenever anything above does
    };

    ~Batch() { Cancel(); Join(); }

    // False if a batch is already running or the list is empty.
    bool Start(std::vector<std::string> files) {
        if (Running() || files.empty()) return false;
        Join();
        m_cancel = false;
        {
            std::lock_guard<std::mutex> lk(m_mu);
            m_st = Status{};
            m_st.running = true;
            m_st.total = (int)files.size();
            m_st.version = ++m_ver;
        }
        m_thread = std::thread([this, files]() { Run(files); });
        return true;
    }

    void Cancel() { m_cancel = true; }
    bool Running() const { std::lock_guard<std::mutex> lk(m_mu); return m_st.running; }
    Status Get() const { std::lock_guard<std::mutex> lk(m_mu); return m_st; }

    // "D:\x\a.fits" -> "D:\x\a.ser"
    static std::string SerPathFor(const std::string& fits) {
        std::string s = fits;
        const size_t dot = s.find_last_of('.'), slash = s.find_last_of("\\/");
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) s.erase(dot);
        return s + ".ser";
    }

private:
    void Join() { if (m_thread.joinable()) m_thread.join(); }

    void Set(const std::function<void(Status&)>& f) {
        std::lock_guard<std::mutex> lk(m_mu);
        f(m_st);
        m_st.version = ++m_ver;
    }

    void Run(const std::vector<std::string> files) {
        for (size_t i = 0; i < files.size() && !m_cancel; ++i) {
            const std::string& in = files[i];
            const size_t slash = in.find_last_of("\\/");
            const std::string name = slash == std::string::npos ? in : in.substr(slash + 1);
            Set([&](Status& s) { s.index = (int)i + 1; s.file = name; s.line = "starting"; });
            const bool ok = Convert(in, SerPathFor(in),
                [&](const char* line) { Set([&](Status& s) { s.line = line; }); }, &m_cancel);
            Set([&](Status& s) {
                if (ok) { ++s.done; s.folder = slash == std::string::npos ? "." : in.substr(0, slash); }
                else ++s.failed;
            });
        }
        Set([&](Status& s) { s.running = false; });
    }

    mutable std::mutex m_mu;
    Status m_st;
    unsigned m_ver = 0;
    std::atomic<bool> m_cancel{false};
    std::thread m_thread;
};

} // namespace raduk::core::fits2ser
