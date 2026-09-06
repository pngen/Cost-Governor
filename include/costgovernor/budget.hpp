#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include "costgovernor/identity.hpp"
#include "costgovernor/money.hpp"
#include "costgovernor/price.hpp"
#include "costgovernor/status.hpp"

namespace costgovernor {

enum class BudgetMode : std::uint8_t {
  HARD = 0,
  SOFT = 1,
  ADVISORY = 2,
};
constexpr std::string_view budget_mode_name(BudgetMode m) noexcept {
  switch (m) { case BudgetMode::HARD: return "HARD"; case BudgetMode::SOFT: return "SOFT"; case BudgetMode::ADVISORY: return "ADVISORY"; }
  return "UNKNOWN";
}

enum class BudgetObjective : std::uint8_t {
  MAX_COST_PER_REQUEST = 0,
  MAX_COST_PER_TOKEN = 1,
  MAX_COST_PER_OPERATION = 2,
  MAX_TOTAL_EXECUTION_COST = 3,
  MAX_ACCELERATOR_TIME_COST = 4,
  MAX_ENERGY_COST = 5,
  MAX_TRANSFER_COST = 6,
  MAX_RECOVERY_COST = 7,
  MAX_RETRY_COST = 8,
};
constexpr std::string_view budget_objective_name(BudgetObjective o) noexcept {
  switch (o) {
    case BudgetObjective::MAX_COST_PER_REQUEST: return "MAX_COST_PER_REQUEST";
    case BudgetObjective::MAX_COST_PER_TOKEN: return "MAX_COST_PER_TOKEN";
    case BudgetObjective::MAX_COST_PER_OPERATION: return "MAX_COST_PER_OPERATION";
    case BudgetObjective::MAX_TOTAL_EXECUTION_COST: return "MAX_TOTAL_EXECUTION_COST";
    case BudgetObjective::MAX_ACCELERATOR_TIME_COST: return "MAX_ACCELERATOR_TIME_COST";
    case BudgetObjective::MAX_ENERGY_COST: return "MAX_ENERGY_COST";
    case BudgetObjective::MAX_TRANSFER_COST: return "MAX_TRANSFER_COST";
    case BudgetObjective::MAX_RECOVERY_COST: return "MAX_RECOVERY_COST";
    case BudgetObjective::MAX_RETRY_COST: return "MAX_RETRY_COST";
  }
  return "UNKNOWN";
}

enum class BudgetLifecycle : std::uint8_t {
  CREATED = 0,
  ACTIVE = 1,
  RESERVED = 2,
  PARTIALLY_CONSUMED = 3,
  EXHAUSTED = 4,
  EXPIRED = 5,
  CANCELLED = 6,
  SUPERSEDED = 7,
  CLOSED = 8,
};
constexpr std::string_view budget_lifecycle_name(BudgetLifecycle s) noexcept {
  switch (s) {
    case BudgetLifecycle::CREATED: return "CREATED";
    case BudgetLifecycle::ACTIVE: return "ACTIVE";
    case BudgetLifecycle::RESERVED: return "RESERVED";
    case BudgetLifecycle::PARTIALLY_CONSUMED: return "PARTIALLY_CONSUMED";
    case BudgetLifecycle::EXHAUSTED: return "EXHAUSTED";
    case BudgetLifecycle::EXPIRED: return "EXPIRED";
    case BudgetLifecycle::CANCELLED: return "CANCELLED";
    case BudgetLifecycle::SUPERSEDED: return "SUPERSEDED";
    case BudgetLifecycle::CLOSED: return "CLOSED";
  }
  return "UNKNOWN";
}

// A budget: a generation-bound, single-objective, single-mode monetary limit.
// Later refactors can group several into a composite; hard constraints are
// always applied before optimization.
struct Budget {
  BudgetId id;
  BudgetGeneration generation;
  CostPolicyGeneration policy_generation;
  CoordinatorEpoch epoch;
  std::string currency = MoneyMicros::kDefaultCurrency;
  BudgetMode mode = BudgetMode::HARD;
  BudgetObjective objective = BudgetObjective::MAX_TOTAL_EXECUTION_COST;
  MoneyMicros limit{0};
  BudgetLifecycle lifecycle = BudgetLifecycle::CREATED;
  MoneyMicros reserved{0};
  MoneyMicros consumed{0};
  MoneyMicros released{0};
  std::int64_t created_at_ms = 0;
  std::int64_t expires_at_ms = 0;  // 0 = never
  DataLabel label = DataLabel::POLICY;

  // A budget is legally usable only while ACTIVE / RESERVED / PARTIALLY_CONSUMED
  // and not expired (when expires_at_ms != 0).
  [[nodiscard]] bool is_live() const noexcept {
    return (lifecycle == BudgetLifecycle::ACTIVE || lifecycle == BudgetLifecycle::RESERVED ||
            lifecycle == BudgetLifecycle::PARTIALLY_CONSUMED);
  }
  [[nodiscard]] bool is_terminal() const noexcept {
    return (lifecycle == BudgetLifecycle::EXHAUSTED || lifecycle == BudgetLifecycle::EXPIRED ||
            lifecycle == BudgetLifecycle::CANCELLED || lifecycle == BudgetLifecycle::SUPERSEDED ||
            lifecycle == BudgetLifecycle::CLOSED);
  }
  // remaining = limit - consumed (never negative for hard budgets).
  [[nodiscard]] MoneyMicros remaining() const { return limit - consumed; }
  // Violation if consumed exceeds limit (only meaningful when limit > 0).
  [[nodiscard]] bool over_hard() const noexcept { return consumed > limit; }
  [[nodiscard]] bool near_budget() const noexcept {
    if (limit.is_zero()) return false;
    MoneyMicros half = limit.checked_div(2, MoneyMicros::Rounding::kCeil);
    return consumed >= half;
  }
  [[nodiscard]] Status transition(BudgetLifecycle to) const noexcept;
  [[nodiscard]] bool valid_transition(BudgetLifecycle from, BudgetLifecycle to) const noexcept;
};

}  // namespace costgovernor
