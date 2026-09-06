#pragma once
#include "costgovernor/governor.hpp"
#include "costgovernor/cost.hpp"
#include "cgtest.hpp"

namespace th {
using namespace costgovernor;

inline CostPolicy policy(CoordinatorEpoch epoch = CoordinatorEpoch(1), CostPolicyGeneration gen = CostPolicyGeneration(1)) {
  CostPolicy p;
  p.id = CostPolicyId(1);
  p.generation = gen;
  p.epoch = epoch;
  p.currency = MoneyMicros::kDefaultCurrency;
  p.max_evidence_age_ms = 30'000;
  p.price_schedule_max_age_ms = 30'000;
  p.near_budget_percent = 80;
  p.at_risk_percent = 90;
  p.allow_policy = true;
  p.allow_measured = true;
  p.allow_synthetic = true;
  return p;
}

inline Budget budget(BudgetId id, BudgetGeneration gen, CostPolicyGeneration pg, CoordinatorEpoch epoch,
                     MoneyMicros limit, BudgetMode mode = BudgetMode::HARD,
                     BudgetObjective obj = BudgetObjective::MAX_TOTAL_EXECUTION_COST) {
  Budget b;
  b.id = id; b.generation = gen; b.policy_generation = pg; b.epoch = epoch;
  b.currency = MoneyMicros::kDefaultCurrency; b.mode = mode; b.objective = obj;
  b.limit = limit;
  return b;
}

inline PriceObservation price(EvidenceId id, PriceKind kind, MoneyMicros per_unit,
                              std::int64_t now, DataLabel label = DataLabel::POLICY,
                              CoordinatorEpoch epoch = CoordinatorEpoch(1),
                              WorkerBootId boot = WorkerBootId(0)) {
  PriceObservation o;
  o.id = id; o.kind = kind; o.amount = per_unit; o.currency = MoneyMicros::kDefaultCurrency;
  o.label = label; o.provenance = Provenance::CONFIGURED_POLICY;
  o.observed_at_ms = now; o.epoch = epoch; o.boot = boot;
  return o;
}

inline CostEvidence attempt(EvidenceId id, RequestId req, AttemptId att, AttemptGeneration attGen,
                            CoordinatorEpoch epoch, WorkerBootId boot, EvidenceGeneration gen,
                            std::int64_t now, DataLabel label = DataLabel::POLICY) {
  CostEvidence e;
  e.id = id; e.request = req; e.attempt = att; e.attempt_gen = attGen;
  e.epoch = epoch; e.boot = boot; e.generation = gen; e.observed_at_ms = now; e.label = label;
  return e;
}

inline ExecutionPlan plan(PlanId id, PlanGeneration gen, WorkloadId wl, WorkloadGeneration wlGen) {
  ExecutionPlan p;
  p.id = id; p.generation = gen; p.workload = wl; p.workload_generation = wlGen;
  p.expected_requests = 1;
  return p;
}

}  // namespace th
