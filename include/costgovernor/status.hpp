#pragma once
#include <cstdint>
#include <string>
#include <string_view>
namespace costgovernor {

// Typed outcome statuses for the cost-governance runtime.
enum class Status : std::uint8_t {
  OK = 0,
  INVALID_INPUT,
  INSUFFICIENT_EVIDENCE,
  STALE_EVIDENCE,
  STALE_AUTHORITY,
  MISSING_PRICE,
  CURRENCY_MISMATCH,
  BUDGET_EXCEEDED,
  BUDGET_EXPIRED,
  PLAN_INFEASIBLE,
  PLAN_SUPERSEDED,
  INTERVENTION_FAILED,
  OUTCOME_UNKNOWN,
  PERSISTENCE_CORRUPT,
  PROTOCOL_ERROR,
  RESOURCE_EXHAUSTED,
  CANCELLED,
  SHUTTING_DOWN,
  PERSISTENCE_UNSUPPORTED_VERSION,
};
constexpr std::string_view status_name(Status s) noexcept {
  switch (s) {
    case Status::OK: return "OK";
    case Status::INVALID_INPUT: return "INVALID_INPUT";
    case Status::INSUFFICIENT_EVIDENCE: return "INSUFFICIENT_EVIDENCE";
    case Status::STALE_EVIDENCE: return "STALE_EVIDENCE";
    case Status::STALE_AUTHORITY: return "STALE_AUTHORITY";
    case Status::MISSING_PRICE: return "MISSING_PRICE";
    case Status::CURRENCY_MISMATCH: return "CURRENCY_MISMATCH";
    case Status::BUDGET_EXCEEDED: return "BUDGET_EXCEEDED";
    case Status::BUDGET_EXPIRED: return "BUDGET_EXPIRED";
    case Status::PLAN_INFEASIBLE: return "PLAN_INFEASIBLE";
    case Status::PLAN_SUPERSEDED: return "PLAN_SUPERSEDED";
    case Status::INTERVENTION_FAILED: return "INTERVENTION_FAILED";
    case Status::OUTCOME_UNKNOWN: return "OUTCOME_UNKNOWN";
    case Status::PERSISTENCE_CORRUPT: return "PERSISTENCE_CORRUPT";
    case Status::PROTOCOL_ERROR: return "PROTOCOL_ERROR";
    case Status::RESOURCE_EXHAUSTED: return "RESOURCE_EXHAUSTED";
    case Status::CANCELLED: return "CANCELLED";
    case Status::SHUTTING_DOWN: return "SHUTTING_DOWN";
    case Status::PERSISTENCE_UNSUPPORTED_VERSION: return "PERSISTENCE_UNSUPPORTED_VERSION";
  }
  return "UNKNOWN";
}
// Exception carrying a typed Status plus a human diagnostic message.
class StatusError final : public std::runtime_error {
 public:
  StatusError(Status s, std::string msg)
      : std::runtime_error(std::move(msg)), status_(s) {}
  [[nodiscard]] Status status() const noexcept { return status_; }
 private:
  Status status_;
};
[[noreturn]] inline void throw_status(Status s, std::string msg) { throw StatusError(s, std::move(msg)); }

}  // namespace costgovernor
