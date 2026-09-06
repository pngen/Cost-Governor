#pragma once
#include "costgovernor/governor.hpp"
#include <iostream>
namespace ex {
using namespace costgovernor;
inline std::shared_ptr<CostGovernor> gov(const char* currency = MoneyMicros::kDefaultCurrency) {
  const std::int64_t kNow = 1'000'000'000;
  auto clock = std::make_shared<MockClock>(kNow);
  auto g = std::make_shared<CostGovernor>(clock);
  CostPolicy p;
  p.currency = currency;
  p.epoch = CoordinatorEpoch(1);
  p.generation = CostPolicyGeneration(1);
  p.max_evidence_age_ms = 30000;
  p.price_schedule_max_age_ms = 30000;
  p.allow_policy = true; p.allow_measured = true; p.allow_synthetic = true;
  g->set_policy(p);
  g->set_worker_boot(WorkerBootId(1));
  return g;
}
inline void accel_price(std::shared_ptr<CostGovernor>& g, std::int64_t micros_per_sec,
                        std::int64_t now = 1'000'000'000) {
  PriceSchedule s;
  s.id = PriceScheduleId(1); s.generation = PriceScheduleGeneration(1); s.created_at_ms = now;
  PriceObservation o;
  o.id = EvidenceId(1); o.kind = PriceKind::ACCELERATOR_TIME;
  o.amount = MoneyMicros::from_micros(micros_per_sec);
  o.currency = MoneyMicros::kDefaultCurrency;
  o.label = DataLabel::POLICY; o.provenance = Provenance::CONFIGURED_POLICY;
  o.observed_at_ms = now; o.epoch = CoordinatorEpoch(1); o.boot = WorkerBootId(1);
  s.observations.push_back(o);
  g->set_price_schedule(s);
}
inline void transfer_price(std::shared_ptr<CostGovernor>& g, std::int64_t micros_per_gb, std::int64_t now = 1'000'000'000) {
  PriceObservation o;
  o.id = EvidenceId(2); o.kind = PriceKind::TRANSFER;
  o.amount = MoneyMicros::from_micros(micros_per_gb);
  o.currency = MoneyMicros::kDefaultCurrency;
  o.label = DataLabel::POLICY; o.provenance = Provenance::CONFIGURED_POLICY;
  o.observed_at_ms = now; o.epoch = CoordinatorEpoch(1); o.boot = WorkerBootId(1);
  PriceSchedule s = g->price_schedule();
  s.observations.push_back(o);
  g->set_price_schedule(s);
}
inline ExecutionPlan plan(PlanId id, std::int64_t ns) {
  ExecutionPlan p;
  p.id = id; p.generation = PlanGeneration(1); p.workload = WorkloadId(1); p.workload_generation = WorkloadGeneration(1);
  p.expected_accelerator_time = AcceleratorNanoseconds(ns);
  p.expected_requests = 1;
  return p;
}
}  // namespace ex
