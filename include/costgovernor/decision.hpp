#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "costgovernor/cost.hpp"
#include "costgovernor/identity.hpp"
#include "costgovernor/money.hpp"
#include "costgovernor/plan.hpp"
#include "costgovernor/policy.hpp"
#include "costgovernor/status.hpp"

namespace costgovernor {

// The set of authority generations a decision or intervention was made under.
// Pre-dispatch revalidation compares this snapshot against the *current*
// governor authority; any mismatch rejects the stale action without mutating
// state.
struct AuthorityContext {
  CoordinatorEpoch epoch;
  CostPolicyGeneration policy_generation;
  PriceScheduleGeneration price_generation;
  EvidenceGeneration evidence_generation;
  PlanGeneration plan_generation;
  WorkloadGeneration workload_generation;
  WorkerBootId boot;
  ResourceGeneration resource_generation;
  PlacementGeneration placement_generation;
  RecoveryGeneration recovery_generation;
  BudgetGeneration budget_generation;
  InterventionGeneration intervention_generation;

  // Equality helpers used for staleness detection.
  [[nodiscard]] bool jurisdiction_changed(const AuthorityContext& other) const noexcept {
    return !(epoch == other.epoch && policy_generation == other.policy_generation &&
             price_generation == other.price_generation && evidence_generation == other.evidence_generation &&
             plan_generation == other.plan_generation && workload_generation == other.workload_generation &&
             boot == other.boot && resource_generation == other.resource_generation &&
             placement_generation == other.placement_generation && recovery_generation == other.recovery_generation &&
             budget_generation == other.budget_generation);
  }
};

enum class CostState : std::uint8_t {
  WITHIN_BUDGET = 0, NEAR_BUDGET = 1, AT_RISK = 2, OVER_SOFT_BUDGET = 3,
  OVER_HARD_BUDGET = 4, REVALIDATION_REQUIRED = 5, INSUFFICIENT_EVIDENCE = 6, UNKNOWN = 7,
};
constexpr std::string_view cost_state_name(CostState s) noexcept {
  switch (s) {
    case CostState::WITHIN_BUDGET: return "WITHIN_BUDGET";
    case CostState::NEAR_BUDGET: return "NEAR_BUDGET";
    case CostState::AT_RISK: return "AT_RISK";
    case CostState::OVER_SOFT_BUDGET: return "OVER_SOFT_BUDGET";
    case CostState::OVER_HARD_BUDGET: return "OVER_HARD_BUDGET";
    case CostState::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
    case CostState::INSUFFICIENT_EVIDENCE: return "INSUFFICIENT_EVIDENCE";
    case CostState::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

enum class DecisionKind : std::uint8_t {
  AUTHORIZE = 0, REJECT = 1, DEFER = 2, REVALIDATE = 3, NO_FEASIBLE_PLAN = 4,
};
constexpr std::string_view decision_kind_name(DecisionKind k) noexcept {
  switch (k) { case DecisionKind::AUTHORIZE: return "AUTHORIZE"; case DecisionKind::REJECT: return "REJECT";
    case DecisionKind::DEFER: return "DEFER"; case DecisionKind::REVALIDATE: return "REVALIDATE";
    case DecisionKind::NO_FEASIBLE_PLAN: return "NO_FEASIBLE_PLAN"; }
  return "UNKNOWN";
}

// Structured what-would-change alternatives, computed deterministically.
struct WhatWouldChange {
  struct Alt {
    std::string name;                 // e.g. "CHEAPER_ACCELERATOR_AVAILABLE"
    bool applicable = false;
    Feasibility feasible = Feasibility::UNKNOWN;
    MoneyMicros projected_cost{0};
    MoneyMicros delta{0};             // new - baseline
  };
  std::vector<Alt> alternatives;
  [[nodiscard]] std::size_t size() const noexcept { return alternatives.size(); }
};

// Structured, correctness-critical explanation (never built from log strings).
struct CostExplanation {
  std::string objective;
  std::string currency = MoneyMicros::kDefaultCurrency;
  CostBreakdown projected;
  CostBreakdown realized;
  std::vector<PriceObservation> prices_used;
  std::int64_t evidence_age_ms = 0;
  DataLabel label = DataLabel::POLICY;
  std::vector<PlanId> candidate_set;
  std::vector<PlanId> rejected;
  FeasibilityReason binding_constraint = FeasibilityReason::NONE;
  PlanId selected;
  std::string tie_break_note;
  std::int64_t uncertainty_percent = 0;
  WhatWouldChange what_would_change;
  AuthorityContext authority;
};

struct CostDecision {
  DecisionKind kind = DecisionKind::NO_FEASIBLE_PLAN;
  PlanId selected_plan;
  PlanGeneration plan_generation;
  std::vector<PlanId> rejected_plans;
  FeasibilityReason binding_constraint = FeasibilityReason::NONE;
  CostBreakdown projected;
  CostBreakdown realized;
  MoneyMicros remaining_budget{0};
  CostState state = CostState::UNKNOWN;
  AuthorityContext authority;
  CostExplanation explanation;
  Status status = Status::OK;
  std::string detail;
};

// Cost-governance intents emitted to adjacent runtimes. The cost runtime never
// executes placement/scheduling itself; it emits an intent.
enum class InterventionKind : std::uint8_t {
  NO_ACTION = 0, SELECT_CHEAPER_PLAN, REPLAN, DEFER, REJECT, STOP_IF_SAFE,
  REDUCE_BATCH, CHANGE_PLACEMENT, CHANGE_ACCELERATOR_CLASS, PREFER_LOCAL_STATE,
  AVOID_TRANSFER, REUSE_WARM_STATE, EVICT_IDLE_STATE, RELEASE_RESERVATION,
  PREEMPT_LOWER_VALUE_WORK, REQUEST_CHEAPER_RECOVERY, SHED_LOW_VALUE_WORK,
  ESCALATE_BUDGET, MANUAL_INTERVENTION_REQUIRED,
};
constexpr std::string_view intervention_kind_name(InterventionKind k) noexcept {
  switch (k) {
    case InterventionKind::NO_ACTION: return "NO_ACTION";
    case InterventionKind::SELECT_CHEAPER_PLAN: return "SELECT_CHEAPER_PLAN";
    case InterventionKind::REPLAN: return "REPLAN";
    case InterventionKind::DEFER: return "DEFER";
    case InterventionKind::REJECT: return "REJECT";
    case InterventionKind::STOP_IF_SAFE: return "STOP_IF_SAFE";
    case InterventionKind::REDUCE_BATCH: return "REDUCE_BATCH";
    case InterventionKind::CHANGE_PLACEMENT: return "CHANGE_PLACEMENT";
    case InterventionKind::CHANGE_ACCELERATOR_CLASS: return "CHANGE_ACCELERATOR_CLASS";
    case InterventionKind::PREFER_LOCAL_STATE: return "PREFER_LOCAL_STATE";
    case InterventionKind::AVOID_TRANSFER: return "AVOID_TRANSFER";
    case InterventionKind::REUSE_WARM_STATE: return "REUSE_WARM_STATE";
    case InterventionKind::EVICT_IDLE_STATE: return "EVICT_IDLE_STATE";
    case InterventionKind::RELEASE_RESERVATION: return "RELEASE_RESERVATION";
    case InterventionKind::PREEMPT_LOWER_VALUE_WORK: return "PREEMPT_LOWER_VALUE_WORK";
    case InterventionKind::REQUEST_CHEAPER_RECOVERY: return "REQUEST_CHEAPER_RECOVERY";
    case InterventionKind::SHED_LOW_VALUE_WORK: return "SHED_LOW_VALUE_WORK";
    case InterventionKind::ESCALATE_BUDGET: return "ESCALATE_BUDGET";
    case InterventionKind::MANUAL_INTERVENTION_REQUIRED: return "MANUAL_INTERVENTION_REQUIRED";
  }
  return "UNKNOWN";
}

enum class InterventionLifecycle : std::uint8_t {
  PROPOSED = 0, AUTHORIZED = 1, DISPATCHED = 2, ACKNOWLEDGED = 3, EFFECTIVE = 4,
  INEFFECTIVE = 5, FAILED = 6, CANCELLED = 7, SUPERSEDED = 8, EXPIRED = 9, OUTCOME_UNKNOWN = 10,
};
constexpr std::string_view intervention_lifecycle_name(InterventionLifecycle s) noexcept {
  switch (s) {
    case InterventionLifecycle::PROPOSED: return "PROPOSED";
    case InterventionLifecycle::AUTHORIZED: return "AUTHORIZED";
    case InterventionLifecycle::DISPATCHED: return "DISPATCHED";
    case InterventionLifecycle::ACKNOWLEDGED: return "ACKNOWLEDGED";
    case InterventionLifecycle::EFFECTIVE: return "EFFECTIVE";
    case InterventionLifecycle::INEFFECTIVE: return "INEFFECTIVE";
    case InterventionLifecycle::FAILED: return "FAILED";
    case InterventionLifecycle::CANCELLED: return "CANCELLED";
    case InterventionLifecycle::SUPERSEDED: return "SUPERSEDED";
    case InterventionLifecycle::EXPIRED: return "EXPIRED";
    case InterventionLifecycle::OUTCOME_UNKNOWN: return "OUTCOME_UNKNOWN";
  }
  return "UNKNOWN";
}

// An intervention is generation-fenced: a stale one rejects without mutating.
struct CostIntervention {
  InterventionId id;
  InterventionGeneration generation;
  InterventionKind kind = InterventionKind::NO_ACTION;
  InterventionLifecycle lifecycle = InterventionLifecycle::PROPOSED;
  AuthorityContext authority;
  std::int64_t created_at_ms = 0;
  std::int64_t dispatched_at_ms = 0;
  std::int64_t acknowledged_at_ms = 0;
  std::int64_t verified_at_ms = 0;
  std::string detail;
};

enum class VerificationOutcome : std::uint8_t {
  EFFECTIVE = 0, PARTIALLY_EFFECTIVE, INEFFECTIVE, WORSE, CREATED_SECONDARY_VIOLATION,
  INSUFFICIENT_POST_ACTION_EVIDENCE, OUTCOME_UNKNOWN,
};
constexpr std::string_view verification_outcome_name(VerificationOutcome v) noexcept {
  switch (v) {
    case VerificationOutcome::EFFECTIVE: return "EFFECTIVE";
    case VerificationOutcome::PARTIALLY_EFFECTIVE: return "PARTIALLY_EFFECTIVE";
    case VerificationOutcome::INEFFECTIVE: return "INEFFECTIVE";
    case VerificationOutcome::WORSE: return "WORSE";
    case VerificationOutcome::CREATED_SECONDARY_VIOLATION: return "CREATED_SECONDARY_VIOLATION";
    case VerificationOutcome::INSUFFICIENT_POST_ACTION_EVIDENCE: return "INSUFFICIENT_POST_ACTION_EVIDENCE";
    case VerificationOutcome::OUTCOME_UNKNOWN: return "OUTCOME_UNKNOWN";
  }
  return "UNKNOWN";
}

}  // namespace costgovernor
