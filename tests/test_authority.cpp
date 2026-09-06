#include "test_helpers.hpp"
#include "costgovernor/governor.hpp"

using namespace costgovernor;

namespace {
std::shared_ptr<CostGovernor> fresh_gov() {
  auto clock = std::make_shared<MockClock>(1000);
  auto g = std::make_shared<CostGovernor>(clock);
  g->set_policy(th::policy());
  g->set_worker_boot(WorkerBootId(7));
  return g;
}
PriceSchedule sched(std::int64_t now = 1000) {
  PriceSchedule s;
  s.id = PriceScheduleId(1); s.generation = PriceScheduleGeneration(1); s.created_at_ms = now;
  s.observations.push_back(th::price(EvidenceId(1), PriceKind::ACCELERATOR_TIME, MoneyMicros::from_micros(1000), now, DataLabel::POLICY, CoordinatorEpoch(1), WorkerBootId(7)));
  return s;
}
}

CG_TEST_CASE(Authority_stale_epoch_rejects_attempt) {
  auto g = fresh_gov();
  CostEvidence e = th::attempt(EvidenceId(1), RequestId(1), AttemptId(1), AttemptGeneration(1), CoordinatorEpoch(1), WorkerBootId(7), EvidenceGeneration(1), 1000);
  e.accelerator_time = AcceleratorNanoseconds(1'000'000'000);
  CG_CHECK(g->record_attempt(e) == Status::OK);
  g->advance_epoch();  // epoch now 2
  CostEvidence stale = th::attempt(EvidenceId(2), RequestId(1), AttemptId(2), AttemptGeneration(1), CoordinatorEpoch(1), WorkerBootId(7), EvidenceGeneration(1), 1000);
  CG_CHECK(g->record_attempt(stale) == Status::STALE_AUTHORITY);
  // Attempt under the current epoch (2) is accepted.
  CostEvidence fresh = th::attempt(EvidenceId(3), RequestId(2), AttemptId(1), AttemptGeneration(2), CoordinatorEpoch(2), WorkerBootId(7), EvidenceGeneration(2), 1000);
  CG_CHECK(g->record_attempt(fresh) == Status::OK);
}

CG_TEST_CASE(Authority_boot_fencing_rejects_replay) {
  auto g = fresh_gov();
  CostEvidence e = th::attempt(EvidenceId(1), RequestId(1), AttemptId(1), AttemptGeneration(1), CoordinatorEpoch(1), WorkerBootId(7), EvidenceGeneration(1), 1000);
  e.accelerator_time = AcceleratorNanoseconds(1'000'000'000);
  CG_CHECK(g->record_attempt(e) == Status::OK);
  // worker restarts => new boot id; replay of old-boot evidence rejects
  g->set_worker_boot(WorkerBootId(99));
  CostEvidence replay = th::attempt(EvidenceId(1), RequestId(1), AttemptId(1), AttemptGeneration(1), CoordinatorEpoch(1), WorkerBootId(7), EvidenceGeneration(1), 1000);
  CG_CHECK(g->record_attempt(replay) == Status::STALE_AUTHORITY);
}

CG_TEST_CASE(Authority_stale_price_generation_rejects) {
  auto g = fresh_gov();
  PriceObservation o = th::price(EvidenceId(1), PriceKind::ACCELERATOR_TIME, MoneyMicros::from_micros(1000), 1000, DataLabel::POLICY, CoordinatorEpoch(1), WorkerBootId(7));
  CG_CHECK(g->publish_price(o) == Status::OK);
  // old-epoch price rejects
  PriceObservation stale = th::price(EvidenceId(2), PriceKind::ENERGY, MoneyMicros::from_micros(1000), 1000, DataLabel::POLICY, CoordinatorEpoch(0), WorkerBootId(7));
  CG_CHECK(g->publish_price(stale) == Status::STALE_AUTHORITY);
  // wrong-boot price rejects
  PriceObservation wrong_boot = th::price(EvidenceId(3), PriceKind::ENERGY, MoneyMicros::from_micros(1000), 1000, DataLabel::POLICY, CoordinatorEpoch(1), WorkerBootId(5));
  CG_CHECK(g->publish_price(wrong_boot) == Status::STALE_AUTHORITY);
}

CG_TEST_CASE(Authority_pre_dispatch_revalidation_price_change) {
  auto g = fresh_gov();
  g->set_price_schedule(sched());
  g->create_budget(th::budget(BudgetId(1), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(1000)));
  ExecutionPlan a = th::plan(PlanId(1), PlanGeneration(1), WorkloadId(1), WorkloadGeneration(1));
  a.expected_accelerator_time = AcceleratorNanoseconds(1'000'000'000);  // 1000 micros
  std::vector<ExecutionPlan> plans{a};
  CostDecision d = g->evaluate(WorkloadId(1), WorkloadGeneration(1), plans);
  CG_CHECK(d.kind == DecisionKind::AUTHORIZE);
  // dispatch under the agreed price generation is valid
  CG_CHECK(g->validate_for_dispatch(d, plans) == Status::OK);
  // advance price generation before dispatch => stale plan rejected
  PriceSchedule s2 = sched(); s2.generation = PriceScheduleGeneration(2);
  g->set_price_schedule(s2);
  CG_CHECK(g->validate_for_dispatch(d, plans) == Status::STALE_AUTHORITY);
}

CG_TEST_CASE(Authority_pre_dispatch_revalidation_epoch_change) {
  auto g = fresh_gov();
  g->set_price_schedule(sched());
  g->create_budget(th::budget(BudgetId(1), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(1000)));
  ExecutionPlan a = th::plan(PlanId(1), PlanGeneration(1), WorkloadId(1), WorkloadGeneration(1));
  a.expected_accelerator_time = AcceleratorNanoseconds(1'000'000'000);
  std::vector<ExecutionPlan> plans{a};
  CostDecision d = g->evaluate(WorkloadId(1), WorkloadGeneration(1), plans);
  g->advance_epoch();
  CG_CHECK(g->validate_for_dispatch(d, plans) == Status::STALE_AUTHORITY);
}
