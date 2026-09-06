#include "test_helpers.hpp"
#include "costgovernor/governor.hpp"
#include "costgovernor/plan.hpp"

using namespace costgovernor;

namespace {
std::shared_ptr<CostGovernor> make_gov(PriceSchedule& sched, const CostPolicy& pol) {
  auto clock = std::make_shared<MockClock>(1000);
  auto g = std::make_shared<CostGovernor>(clock);
  g->set_policy(pol);
  g->set_price_schedule(sched);
  return g;
}
PriceSchedule default_sched(std::int64_t now = 1000) {
  PriceSchedule s;
  s.id = PriceScheduleId(1); s.generation = PriceScheduleGeneration(1); s.created_at_ms = now;
  s.observations.push_back(th::price(EvidenceId(1), PriceKind::ACCELERATOR_TIME, MoneyMicros::from_micros(1000), now));
  s.observations.push_back(th::price(EvidenceId(2), PriceKind::TRANSFER, MoneyMicros::from_micros(10000), now));
  return s;
}
}

CG_TEST_CASE(Plan_deterministic_ranking) {
  PriceSchedule s = default_sched();
  auto g = make_gov(s, th::policy());
  g->create_budget(th::budget(BudgetId(1), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(100000)));
  std::vector<ExecutionPlan> plans;
  ExecutionPlan a = th::plan(PlanId(1), PlanGeneration(1), WorkloadId(1), WorkloadGeneration(1));
  a.expected_accelerator_time = AcceleratorNanoseconds(2'000'000'000);  // 2s -> 2000 micros
  ExecutionPlan b = th::plan(PlanId(2), PlanGeneration(1), WorkloadId(1), WorkloadGeneration(1));
  b.expected_accelerator_time = AcceleratorNanoseconds(1'000'000'000);  // 1s
  b.expected_transfer = TransferBytes(5'000'000'000);                    // 5GB -> 50000 micros
  plans.push_back(a); plans.push_back(b);
  CostDecision d = g->evaluate(WorkloadId(1), WorkloadGeneration(1), plans);
  CG_CHECK(d.kind == DecisionKind::AUTHORIZE);
  CG_CHECK(d.selected_plan == PlanId(1));
  // deterministic: second evaluation identical
  CostDecision d2 = g->evaluate(WorkloadId(1), WorkloadGeneration(1), plans);
  CG_CHECK(d2.selected_plan == d.selected_plan);
  CG_CHECK(d2.projected.total == d.projected.total);
  CG_CHECK(d.projected.total == MoneyMicros::from_micros(2000));
}

CG_TEST_CASE(Plan_hard_slo_dominates_cost) {
  PriceSchedule s = default_sched();
  auto g = make_gov(s, th::policy());
  g->create_budget(th::budget(BudgetId(1), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(100000)));
  ExecutionPlan a = th::plan(PlanId(1), PlanGeneration(1), WorkloadId(1), WorkloadGeneration(1));
  a.expected_accelerator_time = AcceleratorNanoseconds(2'000'000'000);
  a.meets_hard_slo = false;  // cheapest but violates hard latency SLO
  ExecutionPlan b = th::plan(PlanId(2), PlanGeneration(1), WorkloadId(1), WorkloadGeneration(1));
  b.expected_accelerator_time = AcceleratorNanoseconds(4'000'000'000);
  b.meets_hard_slo = true;
  std::vector<ExecutionPlan> plans{a, b};
  CostDecision d = g->evaluate(WorkloadId(1), WorkloadGeneration(1), plans);
  CG_CHECK(d.kind == DecisionKind::AUTHORIZE);
  CG_CHECK(d.selected_plan == PlanId(2));  // B chosen: hard SLO dominates cost
}

CG_TEST_CASE(Plan_hard_budget_no_feasible) {
  PriceSchedule s = default_sched();
  auto g = make_gov(s, th::policy());
  // hard budget smaller than even the cheapest legal execution
  g->create_budget(th::budget(BudgetId(1), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_micros(1500)));
  ExecutionPlan a = th::plan(PlanId(1), PlanGeneration(1), WorkloadId(1), WorkloadGeneration(1));
  a.expected_accelerator_time = AcceleratorNanoseconds(2'000'000'000);  // 2000 micros > 1500
  std::vector<ExecutionPlan> plans{a};
  CostDecision d = g->evaluate(WorkloadId(1), WorkloadGeneration(1), plans);
  CG_CHECK(d.kind == DecisionKind::NO_FEASIBLE_PLAN);
  CG_CHECK(d.status == Status::PLAN_INFEASIBLE);
  // never silently executes
}

CG_TEST_CASE(Plan_missing_price_insufficient_evidence) {
  PriceSchedule s = default_sched();  // no ENERGY price
  auto g = make_gov(s, th::policy());
  g->create_budget(th::budget(BudgetId(1), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(100000)));
  ExecutionPlan a = th::plan(PlanId(1), PlanGeneration(1), WorkloadId(1), WorkloadGeneration(1));
  a.expected_energy = EnergyMicroJoules(1'000'000);
  std::vector<ExecutionPlan> plans{a};
  CostDecision d = g->evaluate(WorkloadId(1), WorkloadGeneration(1), plans);
  CG_CHECK(d.kind == DecisionKind::NO_FEASIBLE_PLAN);
  // UNKNOWN never becomes FEASIBLE; missing price => insufficient evidence
  CG_CHECK(d.status == Status::PLAN_INFEASIBLE || d.binding_constraint == FeasibilityReason::MISSING_PRICE);
}

CG_TEST_CASE(Plan_stale_price_revalidation_required) {
  PriceSchedule s;
  s.id = PriceScheduleId(1); s.generation = PriceScheduleGeneration(1); s.created_at_ms = 1000;
  // price observed 60s in the past (> 30s max_age)
  s.observations.push_back(th::price(EvidenceId(1), PriceKind::ACCELERATOR_TIME, MoneyMicros::from_micros(1000), 1000 - 60000, DataLabel::POLICY, CoordinatorEpoch(1), WorkerBootId(1)));
  auto g = make_gov(s, th::policy());
  g->create_budget(th::budget(BudgetId(1), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(100000)));
  ExecutionPlan a = th::plan(PlanId(1), PlanGeneration(1), WorkloadId(1), WorkloadGeneration(1));
  a.expected_accelerator_time = AcceleratorNanoseconds(2'000'000'000);
  std::vector<ExecutionPlan> plans{a};
  CostDecision d = g->evaluate(WorkloadId(1), WorkloadGeneration(1), plans);
  CG_CHECK(d.kind == DecisionKind::NO_FEASIBLE_PLAN || d.kind == DecisionKind::REVALIDATE);
  CG_CHECK(d.binding_constraint == FeasibilityReason::STALE_PRICE || d.status == Status::STALE_EVIDENCE);
}

CG_TEST_CASE(Plan_per_request_per_token) {
  PriceSchedule s = default_sched();
  auto g = make_gov(s, th::policy());
  g->create_budget(th::budget(BudgetId(1), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(100000)));
  ExecutionPlan a = th::plan(PlanId(1), PlanGeneration(1), WorkloadId(1), WorkloadGeneration(1));
  a.expected_accelerator_time = AcceleratorNanoseconds(2'000'000'000);  // 2000 micros
  a.expected_requests = 2;
  a.expected_tokens = 500;
  std::vector<ExecutionPlan> plans{a};
  PlanEconomics e = g->evaluate_plan(a);
  CG_CHECK(e.projected.total == MoneyMicros::from_micros(2000));
  CG_CHECK(e.projected_per_request == MoneyMicros::from_micros(1000));  // 2000/2 ceil
  CG_CHECK(e.per_token_valid);
  CG_CHECK(e.projected_per_token == MoneyMicros::from_micros(4));      // 2000/500
}

CG_TEST_CASE(Plan_zero_tokens_no_div_zero) {
  PriceSchedule s = default_sched();
  auto g = make_gov(s, th::policy());
  g->create_budget(th::budget(BudgetId(1), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(100000)));
  ExecutionPlan a = th::plan(PlanId(1), PlanGeneration(1), WorkloadId(1), WorkloadGeneration(1));
  a.expected_accelerator_time = AcceleratorNanoseconds(2'000'000'000);
  a.expected_tokens = 0;
  PlanEconomics e = g->evaluate_plan(a);
  CG_CHECK(!e.per_token_valid);  // must not divide by zero
  CG_CHECK(e.projected.total == MoneyMicros::from_micros(2000));
}
