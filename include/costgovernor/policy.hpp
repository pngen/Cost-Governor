#pragma once
#include <cstdint>
#include <string>
#include "costgovernor/identity.hpp"
#include "costgovernor/money.hpp"
#include "costgovernor/price.hpp"

namespace costgovernor {

// Static ranking policy order.
enum class RankingMode : std::uint8_t {
  LEXICOGRAPHIC = 0,         // deterministic ordered factors
  WEIGHTED_SCORE = 1,        // single opaque score (explanations still expose factors)
};
constexpr const char* ranking_mode_name(RankingMode m) noexcept {
  return m == RankingMode::LEXICOGRAPHIC ? "LEXICOGRAPHIC" : "WEIGHTED_SCORE";
}

// Cost governor policy. Generation-bound. All thresholds are explicit.
struct CostPolicy {
  CostPolicyId id;
  CostPolicyGeneration generation;
  CoordinatorEpoch epoch;
  std::string currency = MoneyMicros::kDefaultCurrency;
  RankingMode ranking = RankingMode::LEXICOGRAPHIC;

  // freshness: evidence newer than min_freshness_ms is fresh; older requires
  // revalidation (stale).
  std::int64_t min_evidence_freshness_ms = 0;   // 0 = require observed_at >= now - 0? policy sets
  std::int64_t max_evidence_age_ms = 30'000;    // default 30s
  std::int64_t price_schedule_max_age_ms = 30'000;
  std::int64_t max_projection_uncertainty = 100;  // percent, informational

  // hysteresis / cooldown in ms; prevents plan/placement/preemption flapping.
  std::int64_t hysteresis_ms = 0;
  std::int64_t cooldown_ms = 0;

  // explicit state thresholds (portion of limit, percent 0..100)
  std::int64_t near_budget_percent = 80;
  std::int64_t at_risk_percent = 90;

  // Retry / recovery budgets (soft by default).
  MoneyMicros retry_budget{0};
  MoneyMicros recovery_budget{0};
  bool recovery_cost_known = false;

  // allow these data labels as non-fabricated evidence
  bool allow_synthetic = false;
  bool allow_policy = true;
  bool allow_measured = true;

  [[nodiscard]] bool is_generation_current(CostPolicyGeneration g) const noexcept { return g == generation; }
  [[nodiscard]] bool accepts_label(DataLabel l) const noexcept {
    switch (l) {
      case DataLabel::REAL: return allow_measured;
      case DataLabel::POLICY: return allow_policy;
      case DataLabel::SYNTHETIC: return allow_synthetic;
      case DataLabel::UNSUPPORTED: default: return false;
    }
  }
};

}  // namespace costgovernor
