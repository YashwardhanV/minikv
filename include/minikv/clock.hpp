#pragma once

#include <chrono>
#include <cstdint>

namespace minikv {

/// Source of "now" for everything time-dependent (TTL expiry).
/// It is injected into Store instead of read globally, so tests can control
/// time exactly and never need to sleep.
class Clock {
  public:
    virtual ~Clock() = default;

    /// Current Unix time in milliseconds.
    virtual std::int64_t now_ms() const = 0;
};

/// Real wall-clock time. This is the only place in the project that touches
/// std::chrono, which keeps every other component deterministic and testable.
class SystemClock : public Clock {
  public:
    std::int64_t now_ms() const override {
        const auto since_epoch = std::chrono::system_clock::now().time_since_epoch();
        return static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch).count());
    }
};

/// Manually driven clock for tests: time only moves when the test says so.
class FakeClock : public Clock {
  public:
    /// Starts the clock at `start_ms` (Unix epoch milliseconds).
    explicit FakeClock(std::int64_t start_ms = 0) : now_ms_(start_ms) {}

    std::int64_t now_ms() const override { return now_ms_; }

    /// Jumps to an absolute time.
    void set(std::int64_t ms) { now_ms_ = ms; }

    /// Moves time forward (or backward, if negative) by `ms`.
    void advance(std::int64_t ms) { now_ms_ += ms; }

  private:
    std::int64_t now_ms_;
};

} // namespace minikv
