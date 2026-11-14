#pragma once
// ============================================================
//  Clock -- Phase 153.0: time, as something that can be replaced
//
//  The application core asks an IClock for the time instead of reading
//  steady_clock itself, so a test can run it on a clock it moves by hand.
//  Seconds, monotonic, arbitrary origin. 153.0 puts it under the core's
//  own feeds only; each subsystem moves onto it as its controller is
//  extracted (153.1 on), and with it the headless runs get faster.
//
//  Pure: no Windows.
// ============================================================

#include <atomic>
#include <chrono>

namespace raduk::app {

class IClock {
public:
    virtual ~IClock() = default;
    virtual double Now() const = 0;   // seconds, monotonic
};

// The real one: steady_clock.
class SteadyClock final : public IClock {
public:
    double Now() const override {
        return std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

// A test's: it moves only when told to.
class ManualClock final : public IClock {
public:
    explicit ManualClock(double start = 1000.0) : m_now(start) {}
    double Now() const override { return m_now.load(); }
    void   Advance(double sec)  { m_now.store(m_now.load() + sec); }
    void   Set(double sec)      { m_now.store(sec); }
private:
    std::atomic<double> m_now;
};

} // namespace raduk::app
