#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "costgovernor/cost.hpp"
#include "costgovernor/identity.hpp"
#include "costgovernor/units.hpp"

namespace costgovernor {

enum class RecoveryKind : std::uint8_t {
  NONE = 0, RESTORE = 1, RESTART = 2, MIGRATE = 3, REHYDRATE = 4,
  SHADOW_PROMOTE = 5, FAILOVER = 6, RECOMPUTE = 7,
};
constexpr std::string_view recovery_kind_name(RecoveryKind k) noexcept {
  switch (k) {
    case RecoveryKind::NONE: return "NONE";
    case RecoveryKind::RESTORE: return "RESTORE";
    case RecoveryKind::RESTART: return "RESTART";
    case RecoveryKind::MIGRATE: return "MIGRATE";
    case RecoveryKind::REHYDRATE: return "REHYDRATE";
    case RecoveryKind::SHADOW_PROMOTE: return "SHADOW_PROMOTE";
    case RecoveryKind::FAILOVER: return "FAILOVER";
    case RecoveryKind::RECOMPUTE: return "RECOMPUTE";
  }
  return "UNKNOWN";
}

// A candidate execution plan. Describes expected economics; it never places or
// schedules anything itself.
struct ExecutionPlan {
  PlanId id;
  PlanGeneration generation;
  WorkloadId workload;
  WorkloadGeneration workload_generation;
  ResourceId resource;
  ResourceGeneration resource_generation;
  DeviceId device;
  DeviceGeneration device_generation;
  PlacementId placement;
  PlacementGeneration placement_generation;
  RecoveryGeneration recovery_generation;

  AcceleratorNanoseconds expected_accelerator_time;
  EnergyMicroJoules expected_energy;
  TransferBytes expected_transfer;
  MemoryByteNanoseconds expected_memory_hold;
  MemoryByteNanoseconds expected_residency_hold;
  Bytes expected_storage;
  std::int64_t expected_retries = 0;
  std::int64_t expected_requests = 1;
  std::int64_t expected_tokens = 0;
  std::int64_t expected_operations = 0;
  RecoveryKind recovery = RecoveryKind::NONE;
  std::int64_t recovery_recompute_ns = 0;

  bool meets_hard_slo = true;  // false => INFEASIBLE regardless of cost
  std::int64_t completion_probability_percent = 100;
  DataLabel label = DataLabel::POLICY;
};

enum class Feasibility : std::uint8_t {
  FEASIBLE = 0, INFEASIBLE = 1, DEFER = 2, REVALIDATION_REQUIRED = 3,
  INSUFFICIENT_EVIDENCE = 4, UNKNOWN = 5,
};
constexpr std::string_view feasibility_name(Feasibility f) noexcept {
  switch (f) {
    case Feasibility::FEASIBLE: return "FEASIBLE";
    case Feasibility::INFEASIBLE: return "INFEASIBLE";
    case Feasibility::DEFER: return "DEFER";
    case Feasibility::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
    case Feasibility::INSUFFICIENT_EVIDENCE: return "INSUFFICIENT_EVIDENCE";
    case Feasibility::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

enum class FeasibilityReason : std::uint8_t {
  NONE = 0, MISSING_PRICE, STALE_PRICE, MISSING_RESOURCE_EVIDENCE,
  STALE_RESOURCE_EVIDENCE, HARD_BUDGET_EXCEEDED, SLO_CONFLICT,
  CAPACITY_UNAVAILABLE, PLACEMENT_INVALID, RECOVERY_TOO_EXPENSIVE,
  TRANSFER_TOO_EXPENSIVE, ENERGY_TOO_EXPENSIVE, UNKNOWN_COST_COMPONENT,
};
constexpr std::string_view feasibility_reason_name(FeasibilityReason r) noexcept {
  switch (r) {
    case FeasibilityReason::NONE: return "NONE";
    case FeasibilityReason::MISSING_PRICE: return "MISSING_PRICE";
    case FeasibilityReason::STALE_PRICE: return "STALE_PRICE";
    case FeasibilityReason::MISSING_RESOURCE_EVIDENCE: return "MISSING_RESOURCE_EVIDENCE";
    case FeasibilityReason::STALE_RESOURCE_EVIDENCE: return "STALE_RESOURCE_EVIDENCE";
    case FeasibilityReason::HARD_BUDGET_EXCEEDED: return "HARD_BUDGET_EXCEEDED";
    case FeasibilityReason::SLO_CONFLICT: return "SLO_CONFLICT";
    case FeasibilityReason::CAPACITY_UNAVAILABLE: return "CAPACITY_UNAVAILABLE";
    case FeasibilityReason::PLACEMENT_INVALID: return "PLACEMENT_INVALID";
    case FeasibilityReason::RECOVERY_TOO_EXPENSIVE: return "RECOVERY_TOO_EXPENSIVE";
    case FeasibilityReason::TRANSFER_TOO_EXPENSIVE: return "TRANSFER_TOO_EXPENSIVE";
    case FeasibilityReason::ENERGY_TOO_EXPENSIVE: return "ENERGY_TOO_EXPENSIVE";
    case FeasibilityReason::UNKNOWN_COST_COMPONENT: return "UNKNOWN_COST_COMPONENT";
  }
  return "UNKNOWN";
}

// Economy of one candidate plan under a price schedule.
struct PlanEconomics {
  Feasibility feasible = Feasibility::UNKNOWN;
  FeasibilityReason reason = FeasibilityReason::NONE;
  CostBreakdown projected;      // projected cost under price schedule
  MoneyMicros projected_per_request{0};
  MoneyMicros projected_per_token{0};
  MoneyMicros projected_per_operation{0};
  bool per_token_valid = false;  // token count > 0
};

// Deterministic ranking of feasible plans. Always preserves factor
// contribution details; lexicographic order is the default.
struct PlanRanking {
  struct Factor { std::string name; MoneyMicros value; };
  std::vector<Factor> factors;
  std::int64_t rank = 0;
};

}  // namespace costgovernor
