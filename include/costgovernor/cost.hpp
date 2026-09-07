#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "costgovernor/identity.hpp"
#include "costgovernor/money.hpp"
#include "costgovernor/price.hpp"
#include "costgovernor/units.hpp"

namespace costgovernor {

// Breakdown components. Measurable components (ACCELERATOR_TIME..STORAGE) carry
// raw usage amounts; RETRY/RECOVERY/RECOMPUTATION/OTHER are derived categories
// surfaced in an explanation. UNKNOWN components never become zero.
enum class CostComponent : std::uint8_t {
  ACCELERATOR_TIME = 0,
  ENERGY = 1,
  TRANSFER = 2,
  MEMORY = 3,
  RESIDENCY = 4,
  STORAGE = 5,
  RETRY = 6,
  RECOVERY = 7,
  RECOMPUTATION = 8,
  OTHER = 9,
};
constexpr std::size_t kCostComponentCount = 10;
constexpr std::string_view cost_component_name(CostComponent c) noexcept {
  switch (c) {
    case CostComponent::ACCELERATOR_TIME: return "accelerator_time";
    case CostComponent::ENERGY: return "energy";
    case CostComponent::TRANSFER: return "transfer";
    case CostComponent::MEMORY: return "memory";
    case CostComponent::RESIDENCY: return "residency";
    case CostComponent::STORAGE: return "storage";
    case CostComponent::RETRY: return "retry";
    case CostComponent::RECOVERY: return "recovery";
    case CostComponent::RECOMPUTATION: return "recomputation";
    case CostComponent::OTHER: return "other";
  }
  return "other";
}

// A single attempt's observed/expected economic usage. Amounts are per attempt;
// request-level cost is the ordered sum across attempts.
struct CostEvidence {
  EvidenceId id;
  RequestId request;
  WorkloadId workload;
  AttemptId attempt;
  AttemptGeneration attempt_gen;
  WorkerId worker;             // which worker produced this evidence
  WorkerBootId boot;           // the worker's incarnation at evidence time
  CoordinatorEpoch epoch;
  EvidenceGeneration generation; // freshness/generation fence
  std::int64_t observed_at_ms = 0;
  DataLabel label = DataLabel::POLICY;  // how the numbers themselves were obtained
  bool completed_successfully = false;
  bool is_completion = false;   // true once the attempt reached a terminal result

  DeviceId device;               // placement/device the evidence was measured on
  ResourceId resource;           // resource scope (empty = global)

  AcceleratorNanoseconds accelerator_time;
  EnergyMicroJoules energy;
  TransferBytes transfer;
  MemoryByteNanoseconds memory_hold;
  MemoryByteNanoseconds residency_hold;
  Bytes storage_bytes;

  std::int64_t requests = 1;
  std::int64_t tokens = 0;
  std::int64_t operations = 0;

  // recovery accounting, if modeled
  std::int64_t recovery_actions = 0;
  std::int64_t recompute_ns = 0;  // extra accelerator time from recomputation
};

// A single breakdown line: known (present) or UNKNOWN.
struct ComponentValue {
  bool known = false;
  MoneyMicros value{0};
  [[nodiscard]] bool added_for_total() const noexcept { return known; }
};

// Computed cost for a set of evidence under a price schedule.
// Unknown components are represented by !known and are explicitly excluded from
// total (the caller must treat any_unknown as REQUIRING revalidation, never as
// zero).
struct CostBreakdown {
  std::string currency = MoneyMicros::kDefaultCurrency;
  DataLabel label = DataLabel::POLICY;
  std::array<ComponentValue, kCostComponentCount> components;
  MoneyMicros total{0};
  bool any_unknown = false;
  [[nodiscard]] const ComponentValue& get(CostComponent c) const noexcept { return components[static_cast<std::size_t>(c)]; }
};

// Converts measurable usage to money using exact integer arithmetic and
// CONFIGURED_POLICY / MEASURED prices. Throws MISSING_PRICE when a price is
// required for an observed amount but is absent, and CURRENCY_MISMATCH on
// currency mismatch. Never fabricates a price.
struct PriceMatchError {};
struct CostCalculator {
  // Price per unit that a measurable amount requires, keyed by price kind.
  [[nodiscard]] static MoneyMicros project_component(const PriceSchedule& schedule,
                                                     CostComponent comp,
                                                     std::int64_t amount,
                                                     const std::string& currency,
                                                     std::int64_t now_ms,
                                                     DeviceId device = DeviceId(),
                                                     ResourceId resource = ResourceId());
  [[nodiscard]] static CostBreakdown project(const PriceSchedule& schedule,
                                             const std::vector<CostEvidence>& attempts,
                                             const std::string& currency,
                                             std::int64_t now_ms);
};

}  // namespace costgovernor
