#pragma once
// ============================================================
//  UiSink -- Phase 153.0: what the core tells the person, as an interface
//
//  The core reports through this rather than calling the control panel,
//  the notification bar or the log directly. main.cpp's implementation
//  forwards to those; a test's records every call, so it can check what
//  would have been shown without a window.
//
//  Pure: no Windows.
// ============================================================

#include <mutex>
#include <string>
#include <vector>

namespace raduk::app {

enum class ToastLevel { Info, Warn, Error };

class IUiSink {
public:
    virtual ~IUiSink() = default;
    // A status line in one area of the panel ("autofocus", "ae", ...).
    virtual void Status(const std::string& area, const std::string& text) = 0;
    // A notification.
    virtual void Toast(ToastLevel level, const std::string& text) = 0;
    // A line for the log file.
    virtual void Line(const std::string& text) = 0;
};

class NullUiSink final : public IUiSink {
public:
    void Status(const std::string&, const std::string&) override {}
    void Toast(ToastLevel, const std::string&) override {}
    void Line(const std::string&) override {}
};

// Records everything, for tests. Thread-safe: the core's threads report too.
class RecordingUiSink final : public IUiSink {
public:
    struct Entry { std::string kind, area, text; };

    void Status(const std::string& area, const std::string& text) override { Add({"status", area, text}); }
    void Toast(ToastLevel l, const std::string& text) override {
        Add({"toast", l == ToastLevel::Info ? "info" : l == ToastLevel::Warn ? "warn" : "error", text});
    }
    void Line(const std::string& text) override { Add({"line", "", text}); }

    std::vector<Entry> Entries() const { std::lock_guard<std::mutex> lk(m_mu); return m_entries; }
    // Has any entry's text contained this?
    bool Saw(const std::string& needle) const {
        std::lock_guard<std::mutex> lk(m_mu);
        for (const auto& e : m_entries) if (e.text.find(needle) != std::string::npos) return true;
        return false;
    }
    size_t Count() const { std::lock_guard<std::mutex> lk(m_mu); return m_entries.size(); }

private:
    void Add(Entry e) { std::lock_guard<std::mutex> lk(m_mu); m_entries.push_back(std::move(e)); }
    mutable std::mutex m_mu;
    std::vector<Entry> m_entries;
};

} // namespace raduk::app
