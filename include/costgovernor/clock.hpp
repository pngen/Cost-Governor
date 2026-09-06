#pragma once
#include <cstdint>
namespace costgovernor {

// Injectable wall clock. Time is an integer number of milliseconds since an
// arbitrary epoch (Unix epoch by default). Deterministic tests inject MockClock
// so no sleeps are ever required.
class Clock {
 public:
  virtual ~Clock() = default;
  [[nodiscard]] virtual std::int64_t now_ms() const = 0;
};

class SystemClock final : public Clock {
 public:
  [[nodiscard]] std::int64_t now_ms() const override;
};

// Deterministic test clock.
class MockClock final : public Clock {
 public:
  MockClock() = default;
  explicit MockClock(std::int64_t now) : now_(now) {}
  void set(std::int64_t now) noexcept { now_ = now; }
  void advance(std::int64_t delta_ms) noexcept { now_ += delta_ms; }
  [[nodiscard]] std::int64_t now_ms() const override { return now_; }
 private:
  std::int64_t now_{0};
};

}  // namespace costgovernor
