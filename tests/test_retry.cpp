#include "test_helpers.hpp"
#include "costgovernor/governor.hpp"
#include "costgovernor/cost.hpp"

using namespace costgovernor;

namespace {
std::shared_ptr<CostGovernor> gov() {
  auto clock = std::make_shared<MockClock>(1000);
  auto g = std::make_shared<CostGovernor>(clock);
  g->set_policy(th::policy());
  g->set_worker_boot(WorkerId(1), WorkerBootId(1));
  PriceSchedule s;
  s.id = PriceScheduleId(1); s.generation = PriceScheduleGeneration(1); s.created_at_ms = 1000;
  s.observations.push_back(th::price(EvidenceId(1), PriceKind::ACCELERATOR_TIME, MoneyMicros::from_micros(1000), 1000));
  s.observations.push_back(th::price(EvidenceId(2), PriceKind::ENERGY, MoneyMicros::from_micros(1000), 1000));
  s.observations.push_back(th::price(EvidenceId(3), PriceKind::TRANSFER, MoneyMicros::from_micros(10000), 1000));
  g->set_price_schedule(s);
  return g;
}
CostEvidence mk(EvidenceId id, AttemptId att, std::int64_t ns, bool completion, bool success) {
  CostEvidence e = th::attempt(id, RequestId(1), att, AttemptGeneration(1), CoordinatorEpoch(1), WorkerBootId(1), EvidenceGeneration(1), 1000);
  e.accelerator_time = AcceleratorNanoseconds(ns);
  e.is_completion = completion;
  e.completed_successfully = success;
  return e;
}
}

CG_TEST_CASE(Retry_request_cost_includes_failed_attempts) {
  auto g = gov();
  // attempt 1 fails after 2s GPU time
  CG_CHECK(g->record_attempt(mk(EvidenceId(10), AttemptId(1), 2'000'000'000, true, false)) == Status::OK);
  // attempt 2 fails after 1s
  CG_CHECK(g->record_attempt(mk(EvidenceId(11), AttemptId(2), 1'000'000'000, true, false)) == Status::OK);
  // attempt 3 succeeds after 3s
  CG_CHECK(g->record_attempt(mk(EvidenceId(12), AttemptId(3), 3'000'000'000, true, true)) == Status::OK);
  // total includes ALL attempts: (2+1+3)s * 1000 micros = 6000 micros
  CG_CHECK(g->realized_request_cost(RequestId(1)) == MoneyMicros::from_micros(6000));
  // breakdown: retry is a subset, not double-counted.
  auto attempts = g->attempts(RequestId(1));
  CostBreakdown bd = CostCalculator::project(g->price_schedule(), attempts, MoneyMicros::kDefaultCurrency, 1000);
  CG_CHECK(bd.total == MoneyMicros::from_micros(6000));
  CG_CHECK(bd.get(CostComponent::ACCELERATOR_TIME).value == MoneyMicros::from_micros(6000));
  CG_CHECK(bd.get(CostComponent::RETRY).value == MoneyMicros::from_micros(3000));  // failed attempts only
}

CG_TEST_CASE(Retry_duplicate_completion_no_double_charge) {
  auto g = gov();
  CG_CHECK(g->record_attempt(mk(EvidenceId(20), AttemptId(1), 1'000'000'000, true, false)) == Status::OK);
  CG_CHECK(g->record_attempt(mk(EvidenceId(21), AttemptId(2), 1'000'000'000, true, true)) == Status::OK);
  CostEvidence dup = mk(EvidenceId(21), AttemptId(2), 1'000'000'000, true, true);
  // duplicate attempt id+generation is rejected (never double-charge)
  CG_CHECK(g->record_attempt(dup) == Status::INVALID_INPUT);
  CG_CHECK(g->realized_request_cost(RequestId(1)) == MoneyMicros::from_micros(2000));
}

CG_TEST_CASE(Retry_stale_attempt_never_charges) {
  auto g = gov();
  CG_CHECK(g->record_attempt(mk(EvidenceId(30), AttemptId(1), 1'000'000'000, true, true)) == Status::OK);
  g->set_worker_boot(WorkerId(1), WorkerBootId(2));  // fresh boot
  CostEvidence stale = mk(EvidenceId(31), AttemptId(2), 5'000'000'000, true, true);
  stale.boot = WorkerBootId(1);  // old boot replay
  CG_CHECK(g->record_attempt(stale) == Status::STALE_AUTHORITY);
  CG_CHECK(g->realized_request_cost(RequestId(1)) == MoneyMicros::from_micros(1000));  // unchanged
}
