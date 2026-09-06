#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace costgovernor {

// Strongly-typed integer amount. Checked arithmetic; overflow is rejected.
// The unit tag gives each amount a distinct C++ type so amounts are never
// mixed across semantic domains.
template <typename Tag>
class Amount {
 public:
  using value_type = std::int64_t;
  constexpr Amount() noexcept = default;
  constexpr explicit Amount(value_type v) noexcept : value_(v) {}
  [[nodiscard]] static constexpr Amount from(value_type v) noexcept { return Amount(v); }
  [[nodiscard]] constexpr value_type count() const noexcept { return value_; }
  [[nodiscard]] constexpr bool negative() const noexcept { return value_ < 0; }
  [[nodiscard]] constexpr bool is_zero() const noexcept { return value_ == 0; }

  [[nodiscard]] constexpr Amount operator+(Amount rhs) const { return Amount(checked_add(value_, rhs.value_)); }
  [[nodiscard]] constexpr Amount operator-(Amount rhs) const { return Amount(checked_sub(value_, rhs.value_)); }
  Amount& operator+=(Amount rhs) { value_ = checked_add(value_, rhs.value_); return *this; }
  Amount& operator-=(Amount rhs) { value_ = checked_sub(value_, rhs.value_); return *this; }
  [[nodiscard]] constexpr Amount checked_mul(value_type factor) const { return Amount(checked_mul_i64(value_, factor)); }
  [[nodiscard]] constexpr Amount checked_div(value_type divisor) const {
    if (divisor == 0) throw std::domain_error("integer division by zero");
    if (value_ == std::numeric_limits<value_type>::min() && divisor == -1)
      throw std::overflow_error("integer division overflow");
    return Amount(value_ / divisor);
  }

  friend constexpr bool operator==(Amount a, Amount b) noexcept { return a.value_ == b.value_; }
  friend constexpr bool operator!=(Amount a, Amount b) noexcept { return !(a == b); }
  friend constexpr bool operator<(Amount a, Amount b) noexcept { return a.value_ < b.value_; }
  friend constexpr bool operator>=(Amount a, Amount b) noexcept { return a.value_ >= b.value_; }

  [[nodiscard]] std::string to_string() const { return std::to_string(value_); }
  [[nodiscard]] static constexpr std::string_view unit_name() noexcept { return Tag::name(); }

 private:
  value_type value_{0};
  [[nodiscard]] static constexpr value_type checked_add(value_type a, value_type b) {
    value_type r = a + b;
    if ((a > 0 && b > 0 && r < 0) || (a < 0 && b < 0 && r >= 0)) throw std::overflow_error("amount addition overflow");
    return r;
  }
  [[nodiscard]] static constexpr value_type checked_sub(value_type a, value_type b) {
    if (b == std::numeric_limits<value_type>::min()) throw std::overflow_error("amount subtraction overflow");
    return checked_add(a, -b);
  }
  [[nodiscard]] static constexpr value_type checked_mul_i64(value_type a, value_type b) {
    if (a == 0 || b == 0) return 0;
    if ((a == std::numeric_limits<value_type>::min() && b == -1) ||
        (b == std::numeric_limits<value_type>::min() && a == -1)) throw std::overflow_error("amount multiply overflow");
    value_type r = a * b;
    if (r / a != b) throw std::overflow_error("amount multiply overflow");
    return r;
  }
};

// Unit tags. name() is used for serialization / diagnostics.
struct DurationNsTag { static constexpr std::string_view name() { return "duration_ns"; } };
struct AccelNsTag { static constexpr std::string_view name() { return "accelerator_ns"; } };
struct EnergyMicroJTag { static constexpr std::string_view name() { return "energy_microj"; } };
struct TransferBytesTag { static constexpr std::string_view name() { return "transfer_bytes"; } };
struct MemoryByteNsTag { static constexpr std::string_view name() { return "memory_byte_ns"; } };
struct BytesTag { static constexpr std::string_view name() { return "bytes"; } };
struct TokensTag { static constexpr std::string_view name() { return "tokens"; } };
struct RequestsTag { static constexpr std::string_view name() { return "requests"; } };
struct OperationsTag { static constexpr std::string_view name() { return "operations"; } };
struct RetriesTag { static constexpr std::string_view name() { return "retries"; } };

using DurationNanoseconds = Amount<DurationNsTag>;
using AcceleratorNanoseconds = Amount<AccelNsTag>;
using EnergyMicroJoules = Amount<EnergyMicroJTag>;
using TransferBytes = Amount<TransferBytesTag>;
using MemoryByteNanoseconds = Amount<MemoryByteNsTag>;
using Bytes = Amount<BytesTag>;
using Tokens = Amount<TokensTag>;
using Requests = Amount<RequestsTag>;
using Operations = Amount<OperationsTag>;
using RetryCount = Amount<RetriesTag>;

// Denominator helpers for price-to-cost conversions (exact integer math).
namespace money_scale {
constexpr std::int64_t kNsPerSecond = 1'000'000'000;
constexpr std::int64_t kBytesPerGB = 1'000'000'000;
constexpr std::int64_t kMicroJoulesPerKWh = 1'000'000'000LL * 3'600'000LL;  // 3.6e15 (microjoules per kWh)
constexpr std::int64_t kBytesPerGBs = 1'000'000'000LL;  // byte-seconds per GB-seconds
}  // namespace money_scale

}  // namespace costgovernor
