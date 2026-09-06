#pragma once
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace costgovernor {

// Canonical internal money: integer micro-units of a single configured currency.
// No binary floating point is used as authoritative money. All arithmetic is
// checked; overflow is rejected rather than wrapping.
class MoneyMicros {
 public:
  using value_type = std::int64_t;
  static constexpr value_type kMicrosPerUnit = 1'000'000;
  static constexpr const char* kDefaultCurrency = "USD";

  constexpr MoneyMicros() noexcept = default;
  constexpr explicit MoneyMicros(value_type micros) noexcept : value_(micros) {}

  [[nodiscard]] static constexpr MoneyMicros from_micros(value_type micros) noexcept { return MoneyMicros(micros); }
  [[nodiscard]] static constexpr MoneyMicros from_units(value_type units) noexcept {
    return MoneyMicros(units * kMicrosPerUnit);
  }
  [[nodiscard]] constexpr value_type total_micros() const noexcept { return value_; }
  [[nodiscard]] constexpr value_type total_units() const noexcept { return value_ / kMicrosPerUnit; }
  [[nodiscard]] constexpr bool negative() const noexcept { return value_ < 0; }
  [[nodiscard]] constexpr bool is_zero() const noexcept { return value_ == 0; }

  [[nodiscard]] constexpr MoneyMicros operator+(MoneyMicros rhs) const {
    return MoneyMicros(checked_add(value_, rhs.value_));
  }
  [[nodiscard]] constexpr MoneyMicros operator-(MoneyMicros rhs) const {
    return MoneyMicros(checked_sub(value_, rhs.value_));
  }
  MoneyMicros& operator+=(MoneyMicros rhs) { value_ = checked_add(value_, rhs.value_); return *this; }
  MoneyMicros& operator-=(MoneyMicros rhs) { value_ = checked_sub(value_, rhs.value_); return *this; }

  [[nodiscard]] constexpr MoneyMicros checked_mul(value_type factor) const {
    return MoneyMicros(checked_mul_i64(value_, factor));
  }
  enum class Rounding { kTruncate, kCeil, kFloor };
  [[nodiscard]] constexpr MoneyMicros checked_div(value_type divisor, Rounding r = Rounding::kTruncate) const {
    if (divisor == 0) throw std::domain_error("division by zero");
    if (value_ == std::numeric_limits<value_type>::min() && divisor == -1)
      throw std::overflow_error("money division overflow");
    value_type q = value_ / divisor;
    value_type rem = value_ % divisor;
    if (rem == 0) return MoneyMicros(q);
    if (r == Rounding::kCeil && ((rem > 0) == (divisor > 0))) q += value_type{1};
    else if (r == Rounding::kFloor && ((rem > 0) != (divisor > 0))) q -= value_type{1};
    return MoneyMicros(q);
  }

  friend constexpr bool operator==(MoneyMicros a, MoneyMicros b) noexcept { return a.value_ == b.value_; }
  friend constexpr bool operator!=(MoneyMicros a, MoneyMicros b) noexcept { return !(a == b); }
  friend constexpr bool operator<(MoneyMicros a, MoneyMicros b) noexcept { return a.value_ < b.value_; }
  friend constexpr bool operator<=(MoneyMicros a, MoneyMicros b) noexcept { return a.value_ <= b.value_; }
  friend constexpr bool operator>(MoneyMicros a, MoneyMicros b) noexcept { return a.value_ > b.value_; }
  friend constexpr bool operator>=(MoneyMicros a, MoneyMicros b) noexcept { return a.value_ >= b.value_; }

  // Deterministic decimal serialization. Uses unsigned magnitude so the
  // minimum int64 value is handled without wrapping UB.
  [[nodiscard]] std::string to_string() const {
    bool neg = value_ < 0;
    std::uint64_t mag = neg ? (~static_cast<std::uint64_t>(value_) + 1ULL) : static_cast<std::uint64_t>(value_);
    std::uint64_t units = mag / static_cast<std::uint64_t>(kMicrosPerUnit);
    std::uint64_t frac = mag % static_cast<std::uint64_t>(kMicrosPerUnit);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s%llu.%06llu", neg ? "-" : "",
                  static_cast<unsigned long long>(units), static_cast<unsigned long long>(frac));
    return std::string(buf);
  }

 private:
  value_type value_{0};

  [[nodiscard]] static constexpr value_type checked_add(value_type a, value_type b) {
    value_type r = a + b;
    if ((a > 0 && b > 0 && r < 0) || (a < 0 && b < 0 && r >= 0))
      throw std::overflow_error("money addition overflow");
    return r;
  }
  [[nodiscard]] static constexpr value_type checked_sub(value_type a, value_type b) {
    if (b == std::numeric_limits<value_type>::min()) throw std::overflow_error("money subtraction overflow");
    return checked_add(a, -b);
  }
  [[nodiscard]] static constexpr value_type checked_mul_i64(value_type a, value_type b) {
    if (a == 0 || b == 0) return 0;
    if ((a == std::numeric_limits<value_type>::min() && b == -1) ||
        (b == std::numeric_limits<value_type>::min() && a == -1))
      throw std::overflow_error("money multiply overflow");
    value_type r = a * b;
    if (r / a != b) throw std::overflow_error("money multiply overflow");
    return r;
  }
};

}  // namespace costgovernor
