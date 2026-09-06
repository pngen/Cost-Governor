#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>

namespace costgovernor {

// Generic strongly-typed identity. Each distinct Tag yields a distinct C++ type
// so raw integers are never interchangeable across authority domains.
template <typename Tag>
class Id {
 public:
  using value_type = std::uint64_t;
  constexpr Id() noexcept = default;
  constexpr explicit Id(value_type v) noexcept : value_(v) {}
  [[nodiscard]] constexpr value_type value() const noexcept { return value_; }
  [[nodiscard]] constexpr bool valid() const noexcept { return value_ != 0; }
  [[nodiscard]] std::string to_string() const { return std::to_string(value_); }
  friend constexpr bool operator==(Id a, Id b) noexcept { return a.value_ == b.value_; }
  friend constexpr bool operator!=(Id a, Id b) noexcept { return !(a == b); }
  friend constexpr bool operator<(Id a, Id b) noexcept { return a.value_ < b.value_; }
  friend constexpr bool operator<=(Id a, Id b) noexcept { return a.value_ <= b.value_; }
  friend constexpr bool operator>(Id a, Id b) noexcept { return a.value_ > b.value_; }
  friend constexpr bool operator>=(Id a, Id b) noexcept { return a.value_ >= b.value_; }
 private:
  value_type value_{0};
};

// Tag types for the authority domains in the specification.
struct CoordinatorEpochTag {}; struct WorkerIdTag {}; struct WorkerBootIdTag {};
struct ServiceIdTag {}; struct ServiceGenerationTag {};
struct WorkloadIdTag {}; struct WorkloadGenerationTag {};
struct RequestIdTag {}; struct RequestGenerationTag {};
struct AttemptIdTag {}; struct AttemptGenerationTag {};
struct PlanIdTag {}; struct PlanGenerationTag {};
struct CostPolicyIdTag {}; struct CostPolicyGenerationTag {};
struct BudgetIdTag {}; struct BudgetGenerationTag {};
struct PriceScheduleIdTag {}; struct PriceScheduleGenerationTag {};
struct EvidenceIdTag {}; struct EvidenceGenerationTag {};
struct ResourceIdTag {}; struct ResourceGenerationTag {};
struct DeviceIdTag {}; struct DeviceGenerationTag {};
struct PlacementIdTag {}; struct PlacementGenerationTag {};
struct RecoveryGenerationTag {};
struct DispatchIdTag {}; struct InterventionIdTag {}; struct InterventionGenerationTag {};
struct ReservationIdTag {};

using CoordinatorEpoch = Id<CoordinatorEpochTag>;
using WorkerId = Id<WorkerIdTag>;
using WorkerBootId = Id<WorkerBootIdTag>;
using ServiceId = Id<ServiceIdTag>;
using ServiceGeneration = Id<ServiceGenerationTag>;
using WorkloadId = Id<WorkloadIdTag>;
using WorkloadGeneration = Id<WorkloadGenerationTag>;
using RequestId = Id<RequestIdTag>;
using RequestGeneration = Id<RequestGenerationTag>;
using AttemptId = Id<AttemptIdTag>;
using AttemptGeneration = Id<AttemptGenerationTag>;
using PlanId = Id<PlanIdTag>;
using PlanGeneration = Id<PlanGenerationTag>;
using CostPolicyId = Id<CostPolicyIdTag>;
using CostPolicyGeneration = Id<CostPolicyGenerationTag>;
using BudgetId = Id<BudgetIdTag>;
using BudgetGeneration = Id<BudgetGenerationTag>;
using PriceScheduleId = Id<PriceScheduleIdTag>;
using PriceScheduleGeneration = Id<PriceScheduleGenerationTag>;
using EvidenceId = Id<EvidenceIdTag>;
using EvidenceGeneration = Id<EvidenceGenerationTag>;
using ResourceId = Id<ResourceIdTag>;
using ResourceGeneration = Id<ResourceGenerationTag>;
using DeviceId = Id<DeviceIdTag>;
using DeviceGeneration = Id<DeviceGenerationTag>;
using PlacementId = Id<PlacementIdTag>;
using PlacementGeneration = Id<PlacementGenerationTag>;
using RecoveryGeneration = Id<RecoveryGenerationTag>;
using DispatchId = Id<DispatchIdTag>;
using InterventionId = Id<InterventionIdTag>;
using InterventionGeneration = Id<InterventionGenerationTag>;
using ReservationId = Id<ReservationIdTag>;

}  // namespace costgovernor

namespace std {
// Hash support so ids can be used as unordered_map keys.
template <typename Tag>
struct hash<costgovernor::Id<Tag>> {
  std::size_t operator()(const costgovernor::Id<Tag>& id) const noexcept {
    return std::hash<typename costgovernor::Id<Tag>::value_type>{}(id.value());
  }
};
}  // namespace std
